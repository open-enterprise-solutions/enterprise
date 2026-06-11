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
#include "backend/compiler/procUnitValues.h"     // ibValueFunction (the lambda value)
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
                 const std::vector<const ibBackendQueryColumn*>& cols)
{
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
	                         std::shared_ptr<const ibBackendQueryable> ownedSource)
		: m_builder(builder), m_take(take), m_refCol(refCol), m_cols(std::move(cols)),
		  m_ownedSource(std::move(ownedSource)), m_sel(Run(builder, take)) {}

	bool MoveNext(ibValue& current) override {
		if (!m_sel.Next()) return false;
		current = RowValue(m_sel, m_refCol, m_cols);
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
	link->m_take        = m_take;
	link->m_ops         = m_ops;
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
		arr->Add(RowValue(sel, refCol, cols));
	host.DispatchLinqMethod(method, ret, args, n);
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
		if (sel.Next()) ret = RowValue(sel, RowReferenceColumn(), m_queryable->GetColumns());
		else            ret = ibValue();   // empty selection -> Undefined (both variants, v1)
		return;
	}

	case M::ToArray: {
		const ibBackendQueryColumn* refCol = RowReferenceColumn();
		const std::vector<const ibBackendQueryColumn*> cols = m_queryable->GetColumns();
		ibValueArray* arr = new ibValueArray();
		ibDataQueryResult sel = ExecuteAccumulated();
		while (sel.Next())
			arr->Add(RowValue(sel, refCol, cols));
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
			tc->SetColumnID(c->GetModelID());
		}
		ibDataQueryResult sel = ExecuteAccumulated();
		while (sel.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
			for (const ibBackendQueryColumn* c : cols)
				if (c != nullptr) line->SetValueByMetaID(c->GetModelID(), sel.GetValue(c));
			wxDELETE(line);
		}
		ret = table;
		return;
	}

	default:
		// Select / GroupBy / Skip / Contains / ... — the RAM floor: the folded
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
		m_builder, m_take, RowReferenceColumn(), m_queryable->GetColumns(), m_ownedSource);
}

// System type — vended only (the Data global / future Data.From), never New'ed.
SYSTEM_TYPE_REGISTER(ibValueQueryable, "Queryable", string_to_clsid("VL_QRBL"));
