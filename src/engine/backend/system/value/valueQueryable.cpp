////////////////////////////////////////////////////////////////////////////
//	L4-2 — ibValueQueryable (script "Queryable"): the LINQ pushdown fold.
//	Translatable ops fold into the L3 door builder (lazy); terminals execute;
//	anything else materialises to an Array of references and re-dispatches
//	there (the RAM floor — always correct). (valueQueryable.h)
////////////////////////////////////////////////////////////////////////////

#include "valueQueryable.h"
#include "valueArray.h"                          // ibValueArray — the RAM floor container
#include "valueMap.h"                            // ibValueStructure — structure-shaped rows (no single row ref)
#include "valueTable.h"                          // ibValueModelTable — the ToTable materialised result

#include "backend/query/queryable.h"             // ibBackendQueryable / ibBackendQueryColumn
#include "backend/query/queryAst.h"              // ibQueryAstExpr — the recorded lambda body
#include "backend/query/queryLowering.h"         // LowerLambdaPredicate / LowerLambdaColumnPath
#include "backend/query/tempTableQueryable.h"    // ibTempTableQueryable — wrap a RAM value-table Join argument
#include "backend/query/tempTableManager.h"      // ibTempTableManager — materialise a computed RLS inner to a DB temp (semi-join server-side)
#include "backend/compiler/procUnitValues.h"     // ibValueFunction (the lambda value)
#include "backend/compiler/procUnit.h"           // InvokeLambdaWith2Args — run the Join result-selector in RAM
#include "backend/compiler/procContext.h"        // ibRunContext — captured-frame name lookup
#include "backend/compiler/typeCtor.h"           // SYSTEM_TYPE_REGISTER
#include "backend/backend_exception.h"

namespace {

// Resolve a captured outer local BY NAME from the lambda's captured frames —
// each frame knows its function (m_currentFunction) whose m_listLocals carries
// name -> slot. Module-body frames (null function) are skipped; an unresolved
// name makes the whole fold bail to RAM.
bool ResolveCapturedByName(const ibValueFunction* fn, const wxString& name, ibValue& out)
{
	for (const std::shared_ptr<ibRunContext>& sp : fn->m_capturedFrames) {
		const ibRunContext* frame = sp.get();
		if (frame == nullptr || frame->m_currentFunction == nullptr)
			continue;
		for (const ibByteCode::ibByteCodeVarInfo& local : frame->m_currentFunction->m_listLocals) {
			if (local.m_strRealName.CmpNoCase(name) != 0)
				continue;
			if (local.m_slotIndex < 0 || local.m_slotIndex >= frame->GetLocalCount())
				return false;
			out = *frame->m_pRefLocVars[local.m_slotIndex];
			return true;
		}
	}
	return false;
}

// Collect every Param node of the recorded AST into the captured-value map
// (resolved AT FOLD TIME — the chain is built and consumed in one expression,
// so the values cannot drift). False = some capture did not resolve -> RAM.
bool CollectCaptured(const ibQueryAstExpr& e, const ibValueFunction* fn,
                     std::map<wxString, ibValue>& out)
{
	if (e.m_kind == ibQueryAstExprKind::Param) {
		if (out.find(e.m_paramName) != out.end())
			return true;
		ibValue v;
		if (!ResolveCapturedByName(fn, e.m_paramName, v))
			return false;
		out.emplace(e.m_paramName, v);
		return true;
	}
	const ibQueryAstExprPtr* children[] = { &e.m_arg, &e.m_lhs, &e.m_rhs, &e.m_low, &e.m_high, &e.m_else };
	for (const ibQueryAstExprPtr* c : children)
		if (*c && !CollectCaptured(**c, fn, out)) return false;
	for (const ibQueryAstExprPtr& i : e.m_list)
		if (i && !CollectCaptured(*i, fn, out)) return false;
	for (const auto& wt : e.m_cases) {
		if (wt.first  && !CollectCaptured(*wt.first,  fn, out)) return false;
		if (wt.second && !CollectCaptured(*wt.second, fn, out)) return false;
	}
	return true;
}

// The lambda value of a pipeline op's first argument; null when absent / not a function.
ibValueFunction* LambdaOf(ibValue** args, long n)
{
	if (n < 1 || args == nullptr || args[0] == nullptr)
		return nullptr;
	return dynamic_cast<ibValueFunction*>(args[0]->GetRef());
}

// The recorded body AST of a lambda; null = not recorded (untranslatable body,
// a multi-parameter lambda, or an AOT-cache hit where the AST is not persisted).
std::shared_ptr<ibQueryAstExpr> AstOf(const ibValueFunction* fn)
{
	if (fn == nullptr)
		return nullptr;
	const ibByteCode::ibByteFunction* bfn = fn->GetFunction();
	return bfn != nullptr ? bfn->m_lambdaExprAst : nullptr;
}

// One result row as a script value: a REFERENCE object when the source has a
// single-reference key (catalogs / documents / charts / enums), otherwise a
// STRUCTURE of every column (registers / Data.From collections) — same chain,
// different row shape.
ibValue RowValue(ibDataQueryResult& sel, const ibBackendQueryColumn* refCol,
                 const std::vector<const ibBackendQueryColumn*>& cols,
                 const ibBackendQueryColumn* projectCol = nullptr,
                 const wxString& projectAlias = wxEmptyString)
{
	if (!projectAlias.IsEmpty())
		return sel.GetColumn(projectAlias);   // Select(x => x.Ref.Field) — the dot-walk leaf projected AS this alias
	if (projectCol != nullptr)
		return sel.GetValue(projectCol);   // Select(x => x.Field) — the projected scalar, not the whole row
	if (refCol != nullptr)
		return sel.GetValue(refCol);
	ibValueStructure* row = new ibValueStructure();
	for (const ibBackendQueryColumn* c : cols)
		if (c != nullptr) row->Insert(c->GetName(), sel.GetValue(c));
	return ibValue(row);
}

// Streaming iterator — Foreach over a Queryable reads the selection row by
// row (references or structures); Reset re-executes (the builder is by value).
class ibQueryableIteratorState : public ibValueIteratorState
{
public:
	ibQueryableIteratorState(const ibDataQueryBuilder& builder, long take,
	                         const ibBackendQueryColumn* refCol,
	                         std::vector<const ibBackendQueryColumn*> cols,
	                         std::shared_ptr<const ibBackendQueryable> ownedSource,
	                         const ibBackendQueryColumn* projectCol = nullptr,
	                         const wxString& projectAlias = wxEmptyString)
		: m_builder(builder), m_take(take), m_refCol(refCol), m_cols(std::move(cols)),
		  m_ownedSource(std::move(ownedSource)), m_projectCol(projectCol), m_projectAlias(projectAlias),
		  m_sel(Run(builder, take)) {}

	bool MoveNext(ibValue& current) override {
		if (!m_sel.Next()) return false;
		current = RowValue(m_sel, m_refCol, m_cols, m_projectCol, m_projectAlias);
		return true;
	}
	void Reset() override { m_sel = Run(m_builder, m_take); }

private:
	static ibDataQueryResult Run(const ibDataQueryBuilder& builder, long take) {
		ibReadPageRequest page;
		page.m_count = take;
		return builder.Execute(page);
	}
	ibDataQueryBuilder          m_builder;
	long                        m_take;
	const ibBackendQueryColumn* m_refCol;
	std::vector<const ibBackendQueryColumn*>  m_cols;          // structure-row column set
	std::shared_ptr<const ibBackendQueryable> m_ownedSource;   // keeps a Data.From wrapper alive
	const ibBackendQueryColumn* m_projectCol;                  // single-column scalar projection (null = full row)
	wxString                    m_projectAlias;                // dot-walk scalar projection, read by alias
	ibDataQueryResult           m_sel;
};

} // namespace

//////////////////////////////////////////////////////////////////////
// ibValueQueryable
//////////////////////////////////////////////////////////////////////

ibValueQueryable::ibValueQueryable(const ibBackendQueryable* queryable, const wxString& sourceName)
	: ibValue(ibValueTypes::TYPE_VALUE), m_queryable(queryable), m_sourceName(sourceName)
{
	m_builder.From(queryable);
}

ibValueQueryable::ibValueQueryable(std::shared_ptr<const ibBackendQueryable> owned, const wxString& sourceName)
	: ibValue(ibValueTypes::TYPE_VALUE), m_queryable(owned.get()), m_sourceName(sourceName),
	  m_ownedSource(std::move(owned))
{
	m_builder.From(m_queryable);
}

ibValueQueryable* ibValueQueryable::CloneLink() const
{
	ibValueQueryable* link = new ibValueQueryable();
	link->m_queryable   = m_queryable;
	link->m_sourceName  = m_sourceName;
	link->m_builder     = m_builder;
	link->m_take         = m_take;
	link->m_projectCol   = m_projectCol;
	link->m_projectAlias = m_projectAlias;
	link->m_ops          = m_ops;
	link->m_ownedSource = m_ownedSource;   // shared — every chain link keeps the From wrapper alive
	return link;
}

const ibBackendQueryColumn* ibValueQueryable::RowReferenceColumn() const
{
	if (m_queryable == nullptr)
		return nullptr;
	const std::vector<const ibBackendQueryColumn*> pk = m_queryable->GetPrimaryKeyColumns();
	return pk.size() == 1 ? pk.front() : nullptr;   // composite key (register) = no single row reference
}

ibDataQueryResult ibValueQueryable::ExecuteAccumulated() const
{
	ibReadPageRequest page;
	page.m_count = m_take;   // 0 = all rows
	return m_builder.Execute(page);
}

std::shared_ptr<const ibBackendQueryable> ibValueQueryable::AsSource() const
{
	// Wrap the accumulated builder (From / Join / Where — SELECT * over the restricted rows) as a
	// subquery source the L3 door can read FROM. Copy the builder and let the COPY own the Data.From
	// source (if any) so the subquery is SELF-CONTAINED: its control block manages everything it
	// references — no raw pointer back into THIS value's m_ownedSource (which would dangle if the
	// value died before the subquery). For a metadata source m_ownedSource is null (nothing to adopt).
	ibDataQueryBuilder inner = m_builder;
	if (m_ownedSource)
		inner.AdoptOwnedSource(m_ownedSource);
	inner.WithAccessPolicy(nullptr);   // a subquery inside an already-enforced query — no RLS re-entry
	return std::make_shared<ibSubqueryQueryable>(inner, m_take);
}

wxString ibValueQueryable::GetString() const
{
	if (m_ops.empty())
		return wxString::Format(wxT("Queryable(%s)"), m_sourceName);
	wxString ops;
	for (const wxString& op : m_ops)
		ops += (ops.IsEmpty() ? wxT("") : wxT(", ")) + op;
	return wxString::Format(wxT("Queryable(%s | %s)"), m_sourceName, ops);
}

void ibValueQueryable::MaterialiseThenRam(ibLinqMethod method, ibValue& ret, ibValue** args, long n)
{
	// The RAM floor: stream the ACCUMULATED query into an Array (references for
	// single-key sources, structures otherwise — folded Where/OrderBy/Take stay
	// server-side), then run the op there with the ordinary RAM machinery.
	const ibBackendQueryColumn* refCol = RowReferenceColumn();
	const std::vector<const ibBackendQueryColumn*> cols = m_queryable->GetColumns();
	ibValueArray* arr = new ibValueArray();
	ibValue host(arr);   // owns via TYPE_REFFER for the dispatch below
	ibDataQueryResult sel = ExecuteAccumulated();
	while (sel.Next())
		arr->Add(RowValue(sel, refCol, cols, m_projectCol, m_projectAlias));
	host.DispatchLinqMethod(method, ret, args, n);
}

bool ibValueQueryable::JoinPushDown(ibValue& ret, ibValue** args, long n)
{
	// --- arg shape: Join(inner, leftKey, rightKey, projection) -------------------
	if (n < 4 || args == nullptr)
		return false;
	for (long i = 0; i < 4; ++i)
		if (args[i] == nullptr) return false;

	ibValueFunction* leftKeyFn  = nullptr; args[1]->ConvertToValue(leftKeyFn);
	ibValueFunction* rightKeyFn = nullptr; args[2]->ConvertToValue(rightKeyFn);
	ibValueFunction* projFn     = nullptr; args[3]->ConvertToValue(projFn);
	// a non-lambda key / projection -> the RAM floor.
	if (leftKeyFn == nullptr || rightKeyFn == nullptr || projFn == nullptr)
		return false;

	// --- resolve the inner source to a queryable leaf ----------------------------
	// inner is either ANOTHER queryable (taken BARE — its own folds aren't transferred into
	// the join node, so a folded inner would silently lose them) OR a RAM value table, which
	// we wrap as a computed leaf (ibTempTableQueryable, exactly like Data.From) so the
	// composer temp-promotes it into a DB temp table and joins server-side. Anything else
	// (an arbitrary collection / a folded queryable) -> the RAM floor (ibValueJoinState).
	const ibBackendQueryable*                 innerQ   = nullptr;
	const ibBackendQueryColumn*               innerRef = nullptr;   // single row reference, or null = structure rows
	std::shared_ptr<const ibBackendQueryable> innerWrap;            // keeps a wrapped RAM table alive for this scope
	ibValueQueryable* iq = nullptr;
	if (args[0]->ConvertToValue(iq)) {
		if (iq->m_queryable == nullptr)            return false;
		if (iq->m_take != 0 || !iq->m_ops.empty()) return false;   // bare only
		innerQ   = iq->m_queryable;
		innerRef = iq->RowReferenceColumn();
	} else {
		ibValueModelTable* tbl = nullptr;
		if (!args[0]->ConvertToValue(tbl) || tbl == nullptr)
			return false;                                          // not a queryable, not a value table -> RAM
		innerWrap = std::make_shared<ibTempTableQueryable>(*args[0]);   // RAM table as a computed leaf
		innerQ    = innerWrap.get();                               // innerRef stays null -> structure rows
	}

	// --- source gates ------------------------------------------------------------
	// Self-join is ambiguous in the column model (same leaf) -> RAM. Either side may be
	// reference-keyed (catalog / document) OR structure-shaped (register / Data.From / a
	// wrapped table): the row is reconstructed per side below, so no single-PK requirement.
	if (m_queryable == nullptr || innerQ == nullptr) return false;
	if (m_queryable == innerQ)                       return false;

	// --- outer folded-state gate -------------------------------------------------
	// Outer may carry a Where (co-locates as a per-leaf filter); NOT OrderBy / Take — those
	// reshape the order / row set BEFORE the join, which this path does not reproduce
	// server-side.
	if (m_take != 0)                                 return false;
	for (const wxString& op : m_ops)
		if (op.StartsWith(wxT("OrderBy")))           return false;

	// --- key selectors -> ONE join column each -----------------------------------
	// Same recorded AST the text language lowers; a computed / multi-segment key
	// lowers to != 1 column -> the RAM floor.
	std::shared_ptr<ibQueryAstExpr> leftAst  = AstOf(leftKeyFn);
	std::shared_ptr<ibQueryAstExpr> rightAst = AstOf(rightKeyFn);
	if (!leftAst || !rightAst) return false;
	const std::vector<const ibBackendQueryColumn*> lcols =
		ibQueryLowering::LowerLambdaColumnPath(m_queryable, *leftAst);
	const std::vector<const ibBackendQueryColumn*> rcols =
		ibQueryLowering::LowerLambdaColumnPath(innerQ, *rightAst);
	if (lcols.size() != 1 || rcols.size() != 1 || lcols.front() == nullptr || rcols.front() == nullptr)
		return false;

	// --- per-side projection plan -------------------------------------------------
	// A reference-keyed side projects JUST its row-reference column (the projection reads
	// attributes through the rehydrated reference, lazily); a structure-shaped side
	// projects ALL its columns (the row is a structure of them) — exactly the two shapes
	// an ibValueQueryable row takes (RowValue). Distinct alias prefixes ("o"/"i") keep the
	// two sides apart in the select list; read back BY ALIAS, robust across the co-located,
	// temp-promoted AND RAM-stitch paths.
	const ibBackendQueryColumn* outerRef = RowReferenceColumn();

	ibValueQueryable* link = CloneLink();          // carries outer From + folded Where
	ibValue           linkOwner(link);             // own the transient link for this scope
	link->m_builder.Join(innerQ, lcols.front(), rcols.front(), ibQueryJoinKind::Inner);

	std::vector<const ibBackendQueryColumn*> outerCols, innerCols;
	std::vector<wxString>                    outerAlias, innerAlias;
	auto plan = [](const ibBackendQueryColumn* refCol, const ibBackendQueryable* q, const wxString& pfx,
	               std::vector<const ibBackendQueryColumn*>& cols, std::vector<wxString>& alias) {
		if (refCol != nullptr) {
			cols.push_back(refCol);
			alias.push_back(pfx + wxT("ref"));
			return;
		}
		const std::vector<const ibBackendQueryColumn*> all = q->GetColumns();
		for (size_t i = 0; i < all.size(); ++i) {
			if (all[i] == nullptr) continue;
			cols.push_back(all[i]);
			alias.push_back(pfx + wxString::Format(wxT("%lu"), (unsigned long) i));
		}
	};
	plan(outerRef, m_queryable,        wxT("o"), outerCols, outerAlias);
	plan(innerRef, innerQ, wxT("i"), innerCols, innerAlias);
	if (outerCols.empty() || innerCols.empty())
		return false;   // a source with no projectable columns -> RAM
	for (size_t i = 0; i < outerCols.size(); ++i) link->m_builder.Select(outerCols[i], outerAlias[i]);
	for (size_t i = 0; i < innerCols.size(); ++i) link->m_builder.Select(innerCols[i], innerAlias[i]);

	// --- execute (co-located / temp-promoted / RAM-stitched — the composer decides) --
	ibReadPageRequest page;                        // outer Take gated out -> all joined rows
	ibDataQueryResult sel = link->m_builder.Execute(page);

	// --- result-selector in RAM over the reconstructed (outer, inner) rows -------
	// One row per side: the rehydrated reference cell (reference-keyed source) OR a
	// structure of the side's projected columns — matching RowValue, so the projection
	// lambda sees the same shapes it would on the RAM floor.
	auto rowOf = [&sel](const ibBackendQueryColumn* refCol,
	                    const std::vector<const ibBackendQueryColumn*>& cols,
	                    const std::vector<wxString>& alias) -> ibValue {
		if (refCol != nullptr)
			return sel.GetColumn(alias.front());   // the rehydrated reference cell
		ibValueStructure* st = new ibValueStructure();
		for (size_t i = 0; i < cols.size(); ++i)
			st->Insert(cols[i]->GetName(), sel.GetColumn(alias[i]));
		return ibValue(st);
	};

	ibValueArray* out = new ibValueArray();
	ibValue       result(out);                     // own immediately (exception-safe)
	while (sel.Next()) {
		ibValue  outerRow = rowOf(outerRef, outerCols, outerAlias);
		ibValue  innerRow = rowOf(innerRef, innerCols, innerAlias);
		ibValue  projected;
		ibValue* projArgs[2] = { &outerRow, &innerRow };
		if (!InvokeLambda(*args[3], projArgs, 2, projected))
			return false;                          // gated unreachable; RAM floor re-runs if hit
		out->Add(projected);
	}
	ret = result;
	return true;
}

bool ibValueQueryable::TryJoinThroughL3(ibValue& receiver, ibValue& ret, ibValue** args, long n)
{
	// Layer 2 — the RAM-receiver entry, dispatched from the base LINQ switch when `.Join()`
	// is called on a value table (a non-queryable receiver). It is worth routing to L3 ONLY
	// when the INNER argument is a REAL DB queryable: that is the leaf the RAM receiver is
	// temp-promoted INTO. (A RAM ⋈ RAM join gains nothing server-side -> stay on the RAM
	// hash-join.)
	if (n < 4 || args == nullptr || args[0] == nullptr)
		return false;
	ibValueQueryable* innerQV = nullptr;
	if (!args[0]->ConvertToValue(innerQV) || innerQV->GetQueryable() == nullptr)
		return false;
	if (innerQV->GetQueryable()->IsComputedInRam())
		return false;   // inner also computed -> no DB leaf to promote into -> RAM hash-join

	// The receiver must be a value table we can wrap as a computed leaf (an arbitrary RAM
	// collection — array of structures / references — stays on the RAM hash-join).
	ibValueModelTable* tbl = nullptr;
	if (!receiver.ConvertToValue(tbl) || tbl == nullptr)
		return false;

	// Wrap the receiver as a queryable and REUSE the queryable-side push-down: the wrapped
	// RAM table is the OUTER (computed) leaf, the inner DB queryable is args[0]. The composer
	// temp-promotes the computed outer and runs the join server-side. leftKey / rightKey /
	// projection map 1:1 onto JoinPushDown's outer / inner / result-selector.
	ibValueQueryable wrapped(std::make_shared<ibTempTableQueryable>(ibValue(&receiver)), wxT("Join"));
	return wrapped.JoinPushDown(ret, args, n);
}

void ibValueQueryable::DispatchLinqMethod(ibLinqMethod method, ibValue& ret, ibValue** args, long n)
{
	using M = ibLinqMethod;

	if (m_queryable == nullptr)
		ibBackendCoreException::Error(_("Queryable: the source is not initialised"));

	switch (method) {

	case M::Where: {
		ibValueFunction* fn = LambdaOf(args, n);
		if (fn == nullptr)
			ibBackendCoreException::Error(_("Queryable.Where: a function argument is required"));
		if (std::shared_ptr<ibQueryAstExpr> ast = AstOf(fn)) {
			std::map<wxString, ibValue> captured;
			if (CollectCaptured(*ast, fn, captured)) {
				if (ibQueryPredicatePtr pred = ibQueryLowering::LowerLambdaPredicate(m_queryable, *ast, captured)) {
					ibValueQueryable* link = CloneLink();
					link->m_builder.Where(pred);
					link->m_ops.push_back(wxT("Where"));
					ret = link;
					return;
				}
			}
		}
		MaterialiseThenRam(method, ret, args, n);   // untranslatable -> the RAM floor
		return;
	}

	case M::OrderBy:
	case M::OrderByDescending: {
		ibValueFunction* fn = LambdaOf(args, n);
		const bool ascending = (method == M::OrderBy);
		if (fn != nullptr) {
			if (std::shared_ptr<ibQueryAstExpr> ast = AstOf(fn)) {
				const std::vector<const ibBackendQueryColumn*> cols =
					ibQueryLowering::LowerLambdaColumnPath(m_queryable, *ast);
				if (!cols.empty()) {
					ibValueQueryable* link = CloneLink();
					if (cols.size() == 1) link->m_builder.OrderBy(cols.front(), ascending);
					else                  link->m_builder.OrderBy(cols, ascending);
					link->m_ops.push_back(ascending ? wxT("OrderBy") : wxT("OrderByDescending"));
					ret = link;
					return;
				}
			}
		}
		MaterialiseThenRam(method, ret, args, n);
		return;
	}

	case M::Take: {
		if (n < 1 || args == nullptr || args[0] == nullptr)
			ibBackendCoreException::Error(_("Queryable.Take: a row count is required"));
		const long count = args[0]->GetInteger();
		if (count <= 0)
			ibBackendCoreException::Error(_("Queryable.Take: the row count must be positive"));
		ibValueQueryable* link = CloneLink();
		link->m_take = (m_take > 0 && m_take < count) ? m_take : count;
		link->m_ops.push_back(wxString::Format(wxT("Take %ld"), count));
		ret = link;
		return;
	}

	case M::Count: {
		// Server-side COUNT(*) over the folded filter; an explicit Take caps it.
		ibDataQueryBuilder b = m_builder;
		b.Count(wxT("cnt"));
		ibDataQueryResult r = b.SelectAggregate();
		long count = r.Next() ? r.GetColumn(wxT("cnt")).GetInteger() : 0;
		if (m_take > 0 && count > m_take)
			count = m_take;
		ret = ibValue(ibNumber(count));
		return;
	}

	case M::Any: {
		// One-row probe — no aggregate round-trip needed.
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel = m_builder.Execute(page);
		ret = ibValue(sel.Next());
		return;
	}

	case M::First:
	case M::FirstOrDefault: {
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel = m_builder.Execute(page);
		if (sel.Next()) ret = RowValue(sel, RowReferenceColumn(), m_queryable->GetColumns(), m_projectCol, m_projectAlias);
		else            ret = ibValue();   // empty selection -> Undefined (both variants, v1)
		return;
	}

	case M::ToArray: {
		const ibBackendQueryColumn* refCol = RowReferenceColumn();
		const std::vector<const ibBackendQueryColumn*> cols = m_queryable->GetColumns();
		ibValueArray* arr = new ibValueArray();
		ibDataQueryResult sel = ExecuteAccumulated();
		while (sel.Next())
			arr->Add(RowValue(sel, refCol, cols, m_projectCol, m_projectAlias));
		ret = arr;
		return;
	}

	case M::ToTable: {
		// Materialise into the BUILT-IN value table (ibValueModelTable): every column of
		// the source becomes a typed table column, rows fill by model id — the result is
		// UI-bindable and round-trips straight back through Data.From.
		ibValueModelTable* table = new ibValueModelTable();
		ibValueModelTable::ibValueModelColumnCollection* tcols = table->GetColumnCollection();
		const std::vector<const ibBackendQueryColumn*> cols = m_queryable->GetColumns();
		for (const ibBackendQueryColumn* c : cols) {
			if (c == nullptr) continue;
			ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* tc =
				tcols->AddColumn(c->GetName(), c->GetTypeDesc(), c->GetName());
			tc->SetColumnID(c->GetColumnId());
		}
		ibDataQueryResult sel = ExecuteAccumulated();
		while (sel.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
			for (const ibBackendQueryColumn* c : cols)
				if (c != nullptr) line->SetValueByMetaID(c->GetColumnId(), sel.GetValue(c));
			wxDELETE(line);
		}
		ret = table;
		return;
	}

	case M::Join: {
		// Try the L3 push-down (DB⋈DB through the door); on anything outside the v1
		// slice fall to the RAM floor — the unchanged ibValueJoinState path.
		if (JoinPushDown(ret, args, n))
			return;
		MaterialiseThenRam(method, ret, args, n);
		return;
	}

	case M::Select: {
		// v1: a scalar projection lowers server-side — the read then yields the projected value
		// per row instead of the whole row. A plain column (x => x.Field) reads by pointer
		// (m_projectCol); a DOT-WALK leaf (x => x.Ref.Field) is projected onto the door via
		// SelectPath AS an alias (m_projectAlias) and read back by name. An arithmetic / CASE
		// projection (x => x.A * 2, x => IIF(c, a, b)) lowers as a computed output column (SelectExpr).
		// A structure (x => new {A, B}) or a projection CHAINED onto an existing scalar projection
		// (the lambda would run over the scalar, not the row) is not yet lowerable -> the RAM floor.
		const bool alreadyProjected = (m_projectCol != nullptr) || !m_projectAlias.IsEmpty();
		if (!alreadyProjected) {
			ibValueFunction* fn = LambdaOf(args, n);
			if (fn != nullptr) {
				if (std::shared_ptr<ibQueryAstExpr> ast = AstOf(fn)) {
					const std::vector<const ibBackendQueryColumn*> cols =
						ibQueryLowering::LowerLambdaColumnPath(m_queryable, *ast);
					if (cols.size() == 1 && cols.front() != nullptr) {
						ibValueQueryable* link = CloneLink();
						link->m_projectCol = cols.front();       // plain column — read by pointer
						link->m_ops.push_back(wxT("Select"));
						ret = link;
						return;
					}
					if (cols.size() > 1) {                        // dot-walk leaf — project through the door
						ibValueQueryable* link = CloneLink();
						const wxString alias = wxT("__proj");
						link->m_builder.SelectPath(cols, alias);
						link->m_projectAlias = alias;
						link->m_ops.push_back(wxT("Select"));
						ret = link;
						return;
					}
					// arithmetic / CASE projection (x => x.A * 2, x => IIF(c, a, b)) — a computed
					// output column lowered server-side, read back by its alias like a dot-walk leaf.
					std::map<wxString, ibValue> captured;
					if (CollectCaptured(*ast, fn, captured)) {
						if (ibQueryColumnExprPtr projExpr = ibQueryLowering::LowerLambdaColumnExpr(m_queryable, *ast, captured)) {
							ibValueQueryable* link = CloneLink();
							const wxString alias = wxT("__proj");
							link->m_builder.SelectExpr(projExpr, alias);
							link->m_projectAlias = alias;
							link->m_ops.push_back(wxT("Select"));
							ret = link;
							return;
						}
					}
				}
			}
		}
		MaterialiseThenRam(method, ret, args, n);
		return;
	}

	default:
		// GroupBy / Skip / Distinct / Contains / ... — the RAM floor: the folded
		// prefix still runs server-side, the rest runs over the materialised refs.
		MaterialiseThenRam(method, ret, args, n);
		return;
	}
}

std::shared_ptr<ibValueIteratorState> ibValueQueryable::CreateIterator()
{
	if (m_queryable == nullptr)
		return nullptr;
	// References for single-key sources, structure rows otherwise (registers / Data.From).
	return std::make_shared<ibQueryableIteratorState>(
		m_builder, m_take, RowReferenceColumn(), m_queryable->GetColumns(), m_ownedSource, m_projectCol, m_projectAlias);
}

//////////////////////////////////////////////////////////////////////
// ibValueQueryDecorator — universal query decorator (Join / Where fold straight into the target)
//////////////////////////////////////////////////////////////////////

// Optional ON comparison for a decorator Join — default equality; a theta ON (s.Date >= a.From).
static ibJoinCompareOp JoinOpFromString(const wxString& s)
{
	if (s == wxT("<>") || s == wxT("!=")) return ibJoinCompareOp::Ne;
	if (s == wxT("<"))                    return ibJoinCompareOp::Lt;
	if (s == wxT("<="))                   return ibJoinCompareOp::Le;
	if (s == wxT(">"))                    return ibJoinCompareOp::Gt;
	if (s == wxT(">="))                   return ibJoinCompareOp::Ge;
	return ibJoinCompareOp::Eq;   // "=" / anything else
}

// Map a `restrict` ON predicate's Compare op (from its AST) to the join's ON comparison.
static ibJoinCompareOp JoinOpFromCompare(ibQueryCompareOp c)
{
	switch (c) {
	case ibQueryCompareOp::Ne: return ibJoinCompareOp::Ne;
	case ibQueryCompareOp::Lt: return ibJoinCompareOp::Lt;
	case ibQueryCompareOp::Le: return ibJoinCompareOp::Le;
	case ibQueryCompareOp::Gt: return ibJoinCompareOp::Gt;
	case ibQueryCompareOp::Ge: return ibJoinCompareOp::Ge;
	default:                   return ibJoinCompareOp::Eq;
	}
}

ibValueQueryDecorator::ibValueQueryDecorator(ibDataQueryBuilder* target, const ibBackendQueryable* source, const wxString& sourceName)
	: ibValue(ibValueTypes::TYPE_VALUE), m_target(target), m_source(source), m_sourceName(sourceName)
{
	// No own builder, no policy dance: the target IS the (already policy-nulled) main query, so folding
	// Join/Where in here and running it does NOT re-enter the policy — no recursion.
}

wxString ibValueQueryDecorator::GetString() const
{
	return wxString::Format(wxT("QueryDecorator(%s)"), m_sourceName);
}

// RLS folds ONLY dot-walk conditions; mark each lowered LEAF so the provider renders it as a correlated
// EXISTS (a semi-join that FILTERS once/zero per row, never multiplies) on READS too — matching the write
// path — instead of a projection JOIN. Behavioural tag on the condition (ibQueryCondition::m_asExists);
// takes effect only for a dot-walk leaf (the provider gates on `!m_path.empty()`), a plain leaf is inert.
static void ibMarkRestrictExists(const ibQueryPredicatePtr& pred)
{
	if (!pred)
		return;
	if (pred->m_kind == ibQueryPredicateKind::Leaf)
		pred->m_leaf.m_asExists = true;
	for (const ibQueryPredicatePtr& child : pred->m_children)
		ibMarkRestrictExists(child);
}

// The join ON operator (ibJoinCompareOp) as a filter operator (ibQueryFilterOp) — the semi-join correlation
// stores the latter (the provider's FilterOpToBinOp renders it), so a `restrict … join … on s.k <op> a.k`
// carries its comparison into the EXISTS correlation, not just Eq.
static ibQueryFilterOp ibJoinCompareToFilterOp(ibJoinCompareOp op)
{
	switch (op) {
	case ibJoinCompareOp::Ne: return ibQueryFilterOp::NotEqual;
	case ibJoinCompareOp::Lt: return ibQueryFilterOp::Less;
	case ibJoinCompareOp::Le: return ibQueryFilterOp::LessEqual;
	case ibJoinCompareOp::Gt: return ibQueryFilterOp::Greater;
	case ibJoinCompareOp::Ge: return ibQueryFilterOp::GreaterEqual;
	default:                   return ibQueryFilterOp::Equal;
	}
}

void ibValueQueryDecorator::DispatchLinqMethod(ibLinqMethod method, ibValue& ret, ibValue** args, long n)
{
	using M = ibLinqMethod;
	if (m_source == nullptr || m_target == nullptr)
		ibBackendCoreException::Error(_("QueryDecorator: not initialised"));

	switch (method) {

	case M::Where: {
		// Where(predicate) — folds a filter straight into the TARGET query (lazy). The predicate lowers
		// against the decorated SOURCE (its columns), same as the classic queryable's Where.
		ibValueFunction* fn = LambdaOf(args, n);
		if (fn == nullptr)
			ibBackendCoreException::Error(_("QueryDecorator.Where: a function argument is required"));
		std::shared_ptr<ibQueryAstExpr> ast = AstOf(fn);
		std::map<wxString, ibValue> captured;
		if (ast && CollectCaptured(*ast, fn, captured)) {
			if (ibQueryPredicatePtr pred = ibQueryLowering::LowerLambdaPredicate(m_source, *ast, captured)) {
				ibMarkRestrictExists(pred);   // RLS dot-walk lowers as a correlated EXISTS on reads too — filter, not JOIN
				m_target->Where(pred);
				ret = new ibValueQueryDecorator(m_target, m_source, m_sourceName);   // chain: same target
				return;
			}
		}
		ibBackendCoreException::Error(_("QueryDecorator.Where: the condition cannot be lowered to SQL"));
		return;
	}

	case M::Join: {
		// Join(inner, onCond)                        — the `restrict` form: ONE `(s, a) => s.k <op> a.k`
		//   lambda; its Compare AST splits into left key (vs source) + right key (vs inner) + op.
		// Join(inner, leftKey, rightKey [, opString]) — the explicit LINQ form: separate key selectors,
		//   op as a string 4th arg.
		// inner is a FULL classic ibValueQueryable (Data.Catalogs.X / Data.Registers.Y — its own
		// Where/folds ride along) OR a computed value table (a pre-selected set: "execute a query
		// then join its data"), temp-promoted. The main source stays real, so the query still pages.
		if (n < 2 || args == nullptr || args[0] == nullptr || args[1] == nullptr)
			ibBackendCoreException::Error(_("QueryDecorator.Join: expects (source, onCond) or (source, leftKey, rightKey [, op])"));

		const ibBackendQueryable*                 innerBase = nullptr;   // right-key lowers against this
		std::shared_ptr<const ibBackendQueryable> innerSource;           // the source we join + own
		ibValueQueryable* innerQ = nullptr;
		if (args[0]->ConvertToValue(innerQ) && innerQ != nullptr && innerQ->GetQueryable() != nullptr) {
			innerBase   = innerQ->GetQueryable();
			innerSource = innerQ->AsSource();                            // carries the inner's OWN Where / folds
		} else {
			ibValueModelTable* tbl = nullptr;
			if (!args[0]->ConvertToValue(tbl) || tbl == nullptr)
				ibBackendCoreException::Error(_("QueryDecorator.Join: the first argument must be a Data source or a value table"));
			innerSource = std::make_shared<ibTempTableQueryable>(*args[0]);   // computed set -> DB temp table
			innerBase   = innerSource.get();
		}

		// Resolve the join to left key (vs source) + right key (vs inner) + op — from ONE ON predicate
		// (the `restrict` form) or from the explicit key-selector arguments.
		std::shared_ptr<ibQueryAstExpr> leftAst, rightAst;
		ibJoinCompareOp op = ibJoinCompareOp::Eq;
		if (n == 2) {
			ibValueFunction* onFn = nullptr; args[1]->ConvertToValue(onFn);
			std::shared_ptr<ibQueryAstExpr> onAst = onFn ? AstOf(onFn) : nullptr;
			if (!onAst || onAst->m_kind != ibQueryAstExprKind::Compare || !onAst->m_lhs || !onAst->m_rhs)
				ibBackendCoreException::Error(_("QueryDecorator.Join: the ON must be an `s.k <op> a.k` comparison"));
			leftAst  = onAst->m_lhs;                                     // s.k  — lowers vs source
			rightAst = onAst->m_rhs;                                     // a.k  — lowers vs inner
			op       = JoinOpFromCompare(onAst->m_cmp);
		} else {
			if (args[2] == nullptr)
				ibBackendCoreException::Error(_("QueryDecorator.Join: expects (source, leftKey, rightKey [, op])"));
			ibValueFunction* leftFn  = nullptr; args[1]->ConvertToValue(leftFn);
			ibValueFunction* rightFn = nullptr; args[2]->ConvertToValue(rightFn);
			leftAst  = leftFn  ? AstOf(leftFn)  : nullptr;
			rightAst = rightFn ? AstOf(rightFn) : nullptr;
			if (!leftAst || !rightAst)
				ibBackendCoreException::Error(_("QueryDecorator.Join: key selectors must be simple field lambdas"));
			op = (n >= 4 && args[3] != nullptr && !args[3]->IsEmpty())
				? JoinOpFromString(args[3]->GetString()) : ibJoinCompareOp::Eq;
		}

		const std::vector<const ibBackendQueryColumn*> lcols = ibQueryLowering::LowerLambdaColumnPath(m_source, *leftAst);
		const std::vector<const ibBackendQueryColumn*> rcols = ibQueryLowering::LowerLambdaColumnPath(innerBase, *rightAst);
		if (lcols.size() != 1 || rcols.size() != 1 || lcols.front() == nullptr || rcols.front() == nullptr)
			ibBackendCoreException::Error(_("QueryDecorator.Join: each key must be exactly one column - for a multi-hop key restrict with Where(x => x.Ref.Field = ...) instead"));

		// DISPATCH by inner kind (docs/access-policy-rls.md — semi-join):
		if (innerQ != nullptr && innerQ->IsSingleSource()) {
			// A REAL, SINGLE-source source (a permission REGISTER / catalog) → a correlated EXISTS (semi-join):
			// the outer row passes iff a permitting row EXISTS in the inner. FILTERS once/zero per row — never
			// multiplies — and runs SERVER-SIDE in the one statement. The inner's OWN conditions (its .Where —
			// incl. dot-walk and captured runtime Params) ride along via GetWherePredicate; the base is metadata
			// (stable for the config's life), the leaf columns are its attributes, so the spec has no on-the-fly
			// ownership. A MULTI-source inner (its own Join/Union — NOT in GetWherePredicate) falls to the full
			// subquery path below, so its inner joins are not silently dropped (which would weaken the filter).
			ibSemiJoinExists sj;
			sj.m_inner    = innerBase;                     // the real register/table
			sj.m_where    = innerQ->GetWherePredicate();   // the inner's own conditions (lowered vs innerBase)
			sj.m_outerKey = lcols.front();                 // s.k
			sj.m_innerKey = rcols.front();                 // a.k
			sj.m_op       = ibJoinCompareToFilterOp(op);
			m_target->AddSemiJoin(sj);
		}
		else {
			// A COMPUTED inner — a value table / JSON-built set, OR a MULTI-source register whose own Join/Union
			// can't collapse into GetWherePredicate (so the register-direct fast path above would drop those inner
			// joins and WEAKEN the filter). To semi-join it (FILTER once/zero — never multiply) it must be a REAL
			// server-side table: a computed-in-RAM leaf has no name for EXISTS to scan. So MATERIALISE it onto the
			// session connection — the inner's own Where/folds are already baked into its rows (AsSource carried
			// them; a value table has none) — and semi-join over the temp: the EXISTS then runs SERVER-SIDE in the
			// one statement, exactly like the register-direct path. On Firebird (no temp dialect) Materialise
			// returns null and we fall back to the RAM-composer INNER JOIN (multiplies — the known FB gap until the
			// pure-SQL-subquery-EXISTS lands; docs/access-policy-rls.md).
			ibQueryRamTable rows = innerSource->ComputeRows({});
			std::shared_ptr<ibTempTableManager> mgr =
				ibTempTableManager::Materialise(m_target->GetHolder(), rows, innerSource->GetMetaData());
			const ibDbTempTableQueryable* temp     = mgr ? mgr->Queryable() : nullptr;
			const ibBackendQueryColumn*   innerKey = temp ? temp->ResolveColumnByName(rcols.front()->GetName()) : nullptr;
			if (temp != nullptr && innerKey != nullptr) {
				ibSemiJoinExists sj;
				sj.m_inner    = temp;                          // the materialised permission rows (a real DB temp)
				sj.m_where    = nullptr;                       // the inner's conditions are already baked into the rows
				sj.m_outerKey = lcols.front();                 // s.k
				sj.m_innerKey = innerKey;                      // a.k — remapped onto the temp's same-named column
				sj.m_op       = ibJoinCompareToFilterOp(op);
				m_target->AddSemiJoin(sj);
				m_target->AdoptTempManager(std::move(mgr));    // temp lives across Execute/pages; DROP on builder death
			} else {
				const ibBackendQueryable* raw = m_target->AdoptOwnedSource(std::move(innerSource));
				m_target->Join(raw, lcols.front(), rcols.front(), op, ibQueryJoinKind::Inner);
			}
		}
		ret = new ibValueQueryDecorator(m_target, m_source, m_sourceName);   // chain: same target
		return;
	}

	default:
		ibBackendCoreException::Error(_("QueryDecorator: only Join and Where are available on a decorator"));
		return;
	}
}

bool ibValueQueryDecorator::CompareValueEQ(const ibValue& cParam) const
{
	// Source = "Document.Receipt" — a module identifies the source by its canonical FULL NAME
	// string ("<ClassName>.<Name>", GetFullName), which the policy passes as m_sourceName.
	if (cParam.m_typeClass == ibValueTypes::TYPE_STRING)
		return m_sourceName == cParam.GetString();
	return false;
}

// System type — vended only (the Data global / future Data.From), never New'ed.
SYSTEM_TYPE_REGISTER(ibValueQueryable, "Queryable", system_to_clsid("VL_QRBL"));

// System type — constructed ONLY by a policy (never vended, never New'ed).
SYSTEM_TYPE_REGISTER(ibValueQueryDecorator, "QueryDecorator", system_to_clsid("VL_QDEC"));
