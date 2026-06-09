////////////////////////////////////////////////////////////////////////////
//	Description : L3 query COMPOSER + the non-DB providers. The door
//	              (dataQueryBuilder.cpp) is metadata- and L2-BLIND: it builds an
//	              ibDataQuerySpec and runs it through the ibBackendQueryProvider
//	              abstraction it pulls from the queryable. THIS TU realizes:
//	                * ibComputedProvider + ibComputedProviderInstance — the RAM-
//	                  computed virtual table (register slice/balance, temp, subquery);
//	                * ibBackendQueryable::GetProvider — the DB default (a stateless
//	                  ibDbTableProvider) / ResolveColumnByName default;
//	                * ibSubqueryQueryable (nested-query source) out-of-line bodies;
//	                * ibQueryComposer — the multi-source relational-tree engine
//	                  (Join/Union materialise-to-RAM + stitch);
//	                * the result-sources + ibDataQueryResult selection;
//	                * ibDataQueryBuilder::NewPageCache.
//	              The BIG DB provider (real-table read/write engine + ibMetaIRBuilder
//	              lowering + Get/SetValueAttribute) lives in dbTableProvider.cpp.
//	              See docs/query-language-arc.md §18, §22.
////////////////////////////////////////////////////////////////////////////

#include "queryProvider.h"                                            // ibBackendQueryProvider / ibComputedProvider (+ dataQueryBuilder.h, queryable.h)
#include "queryRamTable.h"                                            // ibQueryRamTable — L3's raw ИТОГИ tree (names no runtime type but ibValue)
#include "dbTableProvider.h"                                          // ibDbTableProvider (vended by GetProvider) + ibRenderedPageCache (NewPageCache)
#include "resultSource.h"                                             // ibDataResultSource — the backing ibRamTableResultSource derives

#include <map>                                                        // dot-walk join dedup + col->attr cache
#include <stdexcept>                                                  // guard for the not-yet-built multi-source composition path
#include <algorithm>                                                  // stable_sort — RAM ORDER BY over a composed result

// ibComputedProvider — the RAM-computed virtual table (register slice / balance /
// turnover). Stateless: computes the rows from the spec, no physical scan, no L2.
ibDataQueryResult ibComputedProvider::ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& /*req*/)
{
	return ibDataQueryResult(spec.m_queryable->ComputeRows(*spec.m_conditions), spec.m_queryable);
}

// ==========================================================================
// ibBackendQueryable::GetProvider — the DB DEFAULT. A queryable vends its engine; the
// record / register / constant / tabular families are physical DB tables, so they all
// share one STATELESS static DB provider. Computed queryables override this
// (ibComputedRegisterQueryable in queryable.h) to vend a static ibComputedProvider. (docs §22.4)
// ==========================================================================
ibBackendQueryProvider& ibBackendQueryable::GetProvider() const
{
	static ibDbTableProvider s_dbProvider;   // stateless — the spec carries every per-query value
	return s_dbProvider;
}

// The shared stateless computed (RAM) provider — vended by ibComputedRegisterQueryable (in
// queryable.h, which may not name the concrete ibComputedProvider) and the temp / subquery
// computed sources. Declared in queryable.h, defined here where ibComputedProvider is complete.
ibBackendQueryProvider& ibComputedProviderInstance()
{
	static ibComputedProvider s_computedProvider;
	return s_computedProvider;
}

// Default: the base has no metadata to resolve a name against, so it owns no columns.
// Every concrete source (record / register / constant / tabular = attribute-by-name;
// temp / subquery = own column lookup) OVERRIDES this; the base is a null fallback.
const ibBackendQueryColumn* ibBackendQueryable::ResolveColumnByName(const wxString& /*name*/) const
{
	return nullptr;
}

// ==========================================================================
// ibSubqueryQueryable — a nested query as a source. It is a COMPUTED (RAM) source: the
// inner query is RUN and its rows materialised into an ibQueryRamTable, so the outer query
// reads / filters / joins it like any computed leaf. Columns = the inner explicit
// Select(col, alias) list, else (SELECT *) the inner primary source's full column set. The
// bodies live here (where ibDataQueryBuilder is complete). (docs §22 nested subquery)
// ==========================================================================
ibSubqueryQueryable::ibSubqueryQueryable(const ibDataQueryBuilder& inner)
	: m_inner(std::make_unique<ibDataQueryBuilder>(inner))
{
	const auto& selectCols = m_inner->GetSelectColumns();
	if (!selectCols.empty()) {
		for (const auto& sc : selectCols)
			if (sc.first != nullptr) m_columns.push_back(sc.first);
	}
	else if (const ibBackendQueryable* primary = m_inner->GetPrimarySource()) {
		m_columns = primary->GetColumns();
	}
}

ibSubqueryQueryable::~ibSubqueryQueryable() = default;   // unique_ptr<ibDataQueryBuilder> — complete here

// Vends the computed (RAM) provider — the stateless static every computed source uses.
ibBackendQueryProvider& ibSubqueryQueryable::GetProvider() const
{
	static ibComputedProvider s_computedProvider;
	return s_computedProvider;
}

// Run the inner query (with the outer's pushed-down conditions) and materialise its rows
// into a RAM table keyed by column model-id — the same leaf-materialise recipe.
ibQueryRamTable ibSubqueryQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	ibQueryRamTable t;
	for (const ibBackendQueryColumn* col : m_columns)
		if (col != nullptr) t.AddColumn(col->GetModelID(), col->GetName(), col->GetTypeDesc());

	// The subquery's exposed columns ARE the inner columns, so the outer's extra
	// conditions apply straight onto a copy of the inner query.
	ibDataQueryBuilder q(*m_inner);
	for (const ibQueryCondition& c : extra) {
		if (c.m_col == nullptr) continue;
		if (c.m_explicitOp) q.WhereCompare(c.m_col, c.m_op, c.m_value);
		else                q.Where(c.m_col, c.m_comparison, c.m_value);
	}

	ibReadPageRequest page; page.m_count = 0;   // all rows
	ibDataQueryResult sel = q.Select(page);
	while (sel.Next()) {
		const long r = t.AppendRow();
		for (const ibBackendQueryColumn* col : m_columns)
			if (col != nullptr) t.SetCell(r, col->GetModelID(), sel.GetValue(col));
	}
	return t;
}

// ==========================================================================
// ibQueryComposer — realizes the relational tree the door built. A single Source leaf
// delegates to that leaf's provider (today's path). A Join / Union over leaves is the
// next arc (co-locate into one SQL where all leaves are SQL-able on one connection,
// else push down per leaf + materialise the rest + stitch); guarded until built, since
// no caller composes a multi-source tree yet. (docs/query-language-arc.md §22.1)
// ==========================================================================
namespace {
bool IsSingleSource(const ibDataQuerySpec& spec)
{
	return spec.m_root == nullptr || spec.m_root->m_kind == ibQueryNode::Kind::Source;
}

// Multi-source strategy (2026-06-08): materialise EVERY leaf to RAM (read it through its
// own provider into a RAM table), then JOIN / UNION the RAM tables in C++, and return a
// RAM-backed result. RAM is enough for tests — the whole crown is exercisable without a
// DB. The tempdb path (serialise leaves into a real temp table → server-side SQL join /
// co-location → generic-DB-read) is a FINAL optimisation, not on the test path.
// IsComputedInRam() stays the DB-vs-RAM indicator for when that lands.

// Precise guard for the multi-source cases the body does not handle yet (N-way / nested
// joins, UNION, auto-join-by-reference) — NOT a silent stub.
[[noreturn]] void RamCompositionNotYet()
{
	throw std::logic_error("ibQueryComposer: this multi-source shape (N-way / nested / UNION / "
	                       "auto-join) is a follow-up; today: two-leaf JOIN on explicit columns, in RAM");
}

// Columns the query references that a LEAF owns (select-list outputs + Where columns +
// the join keys), deduped — what to materialise for that leaf.
std::vector<const ibBackendQueryColumn*> LeafColumns(const ibDataQuerySpec& spec,
                                                     const ibBackendQueryable* leaf,
                                                     const ibQueryNode* join)
{
	std::vector<const ibBackendQueryColumn*> cols;
	auto add = [&](const ibBackendQueryColumn* c) {
		if (c == nullptr || !leaf->OwnsColumn(c)) return;
		for (const auto* e : cols) if (e == c) return;
		cols.push_back(c);
	};
	for (const auto& s : *spec.m_selectCols) add(s.first);
	for (const ibQueryCondition& c : *spec.m_conditions) add(c.m_col);
	add(join->m_onLeft);
	add(join->m_onRight);
	return cols;
}

// The spec's Where conditions a leaf owns (by object) — pushed down into a JOIN leaf's
// materialise read (the condition column belongs to exactly one join leaf).
std::vector<ibQueryCondition> LeafConditions(const ibDataQuerySpec& spec, const ibBackendQueryable* leaf)
{
	std::vector<ibQueryCondition> out;
	for (const ibQueryCondition& c : *spec.m_conditions)
		if (c.m_col != nullptr && leaf->OwnsColumn(c.m_col))
			out.push_back(c);
	return out;
}

// For UNION: every condition applies to EVERY branch, resolved BY NAME (each branch has
// its own same-named column). A branch missing the column drops that one condition.
std::vector<ibQueryCondition> LeafConditionsByName(const ibDataQuerySpec& spec, const ibBackendQueryable* leaf)
{
	std::vector<ibQueryCondition> out;
	for (const ibQueryCondition& c : *spec.m_conditions) {
		if (c.m_col == nullptr) continue;
		const ibBackendQueryColumn* bc = leaf->ResolveColumnByName(c.m_col->GetName());
		if (bc == nullptr) continue;
		ibQueryCondition nc = c;
		nc.m_col = bc;
		out.push_back(nc);
	}
	return out;
}

// --- planner decision at the materialisation seam (temp-db foundation, docs/temp-db.md) -------
// Below this many rows a DB temp table is not worth its CREATE+INSERT round-trips — a small
// intermediate stays in RAM regardless of capability. Heuristic; tune against real numbers.
const long kTempTableMinRows = 1000;

// Where a materialised intermediate lands — RAM (the composer's always-works floor) or a DB TEMP
// table (server-side join, the optimisation lever). DbTemp needs the driver to vend a
// temp dialect (capability by PRESENCE) AND the set large enough to pay the round-trips; absence
// OR small size ⇒ RAM. A DbTemp choice that fails at runtime falls back to RAM (handled at the
// materialisation site). (docs/temp-db.md)
enum class ibMaterialisation { Ram, DbTemp };

ibMaterialisation ChooseMaterialisation(const ibTempTableDialect* tempDialect, long sizeHint)
{
	if (tempDialect == nullptr)                        return ibMaterialisation::Ram;   // no capability → floor
	if (sizeHint >= 0 && sizeHint < kTempTableMinRows) return ibMaterialisation::Ram;   // too small → RAM cheaper
	return ibMaterialisation::DbTemp;                                                   // CAN + worth it
}

// Materialisation SEAM — read a leaf through its OWN provider (single-source, its
// conditions pushed down) and collect the needed columns into a RAM table, keyed by
// GetModelID. The RAM impl; the planner-dispatched entry is MaterialiseLeaf below — the
// tempdb path swaps in THERE (serialise into a real temp table, push the join server-side)
// without touching the join. (docs/query-language-arc.md §22.1a, docs/temp-db.md)
ibQueryRamTable MaterialiseLeafToRam(const ibBackendQueryable* leaf, ibDatabaseConnectionHolder* holder,
                                     const std::vector<ibQueryCondition>& conds,
                                     const std::vector<const ibBackendQueryColumn*>& cols)
{
	ibQueryRamTable t;
	for (const ibBackendQueryColumn* col : cols)
		t.AddColumn(col->GetModelID(), col->GetName(), col->GetTypeDesc());

	ibDataQueryBuilder q(holder);
	q.From(leaf);
	for (const ibQueryCondition& c : conds) {
		if (c.m_explicitOp) q.WhereCompare(c.m_col, c.m_op, c.m_value);
		else                q.Where(c.m_col, c.m_comparison, c.m_value);
	}
	ibReadPageRequest page; page.m_count = 0;   // every matching row
	ibDataQueryResult sel = q.Select(page);     // reads through the cursor — never names a runtime table
	while (sel.Next()) {
		const long r = t.AppendRow();
		for (const ibBackendQueryColumn* col : cols)
			t.SetCell(r, col->GetModelID(), sel.GetValue(col));
	}
	return t;
}

// Planner-dispatched leaf materialisation — the composer's join/union/totals go through HERE, not
// straight to RAM. Consults ChooseMaterialisation and materialises accordingly. The DB-temp path
// (serialise into a real temp table, return a server-side-joinable source, RAM fallback on
// failure) lands with the temp-table manager/adapter; the temp dialect + a real size hint plug in
// at this seam then. Until built it always resolves to RAM — the decision lives here so the
// feature wires in without touching the join below. (docs/temp-db.md)
ibQueryRamTable MaterialiseLeaf(const ibBackendQueryable* leaf, ibDatabaseConnectionHolder* holder,
                                const std::vector<ibQueryCondition>& conds,
                                const std::vector<const ibBackendQueryColumn*>& cols)
{
	// TODO(temp-db): tempDialect = holder's GetTempTableDialect(); sizeHint from stats / a probe.
	// On DbTemp the manager serialises into a temp table and the join goes server-side (with a RAM
	// fallback on CREATE/INSERT failure). nullptr ⇒ no driver vends temp yet ⇒ RAM.
	if (ChooseMaterialisation(/*tempDialect*/ nullptr, /*sizeHint*/ -1) == ibMaterialisation::DbTemp) {
		// (DB-temp materialisation — pending the temp-table manager; falls through to RAM.)
	}
	return MaterialiseLeafToRam(leaf, holder, conds, cols);
}

// One cell of a materialised RAM table at (row, column model-id).
ibValue RamCell(const ibQueryRamTable& t, long row, const ibBackendQueryColumn* col)
{
	return col != nullptr ? t.GetCell(row, col->GetModelID()) : ibValue();
}

// Source leaves of a subtree (depth-first) — for "which side provides a column".
void CollectLeaves(const ibQueryNode* node, std::vector<const ibQueryNode*>& out)
{
	if (node == nullptr) return;
	if (node->m_kind == ibQueryNode::Kind::Source) { out.push_back(node); return; }
	CollectLeaves(node->m_left.get(),  out);
	CollectLeaves(node->m_right.get(), out);
	for (const auto& p : node->m_parts) CollectLeaves(p.get(), out);
}

// Does any leaf of this subtree own the column? (column -> side routing in a JOIN.)
bool SubtreeProvides(const ibQueryNode* node, const ibBackendQueryColumn* col)
{
	std::vector<const ibQueryNode*> leaves;
	CollectLeaves(node, leaves);
	for (const ibQueryNode* leaf : leaves)
		if (leaf->m_queryable != nullptr && leaf->m_queryable->OwnsColumn(col))
			return true;
	return false;
}

// This source's SELF-REFERENCE column — the join key a reference TO it binds to (its own
// reference, the _RRRef blob: source.<ref>_RRRef = target.<selfref>_RRRef). It is the source's
// reference key, vended as the front of GetPrimaryKeyColumns (catalog/document: the data-reference
// attribute; empty for registers / temp, which are not reference targets). One key authority — no
// IsReferenceAttribute. (docs §22.1)
const ibBackendQueryColumn* SelfReferenceColumn(const ibBackendQueryable* q)
{
	const std::vector<const ibBackendQueryColumn*> keys = q->GetPrimaryKeyColumns();
	return keys.empty() ? nullptr : keys.front();
}

// A column of `from` whose reference resolves to `to` — the referencing side of an auto-join.
const ibBackendQueryColumn* ReferenceColumnTo(const ibBackendQueryable* from, const ibBackendQueryable* to)
{
	for (const ibBackendQueryColumn* c : from->GetColumns())
		if (c != nullptr && from->ResolveReferenceTarget(c) == to) return c;
	return nullptr;
}

// Resolve a Join node's key columns: explicit, else DERIVED by reference (Join(b) auto-join —
// a referencing column on one side matched to the other's SELF-REFERENCE column).
// Returns false when neither given nor derivable. (docs §22.1)
bool ResolveJoinKeys(const ibQueryNode* join, const ibBackendQueryColumn*& onL, const ibBackendQueryColumn*& onR)
{
	onL = join->m_onLeft; onR = join->m_onRight;
	if (onL != nullptr && onR != nullptr) return true;
	const ibBackendQueryable* qL = (join->m_left  && join->m_left->m_kind  == ibQueryNode::Kind::Source) ? join->m_left->m_queryable  : nullptr;
	const ibBackendQueryable* qR = (join->m_right && join->m_right->m_kind == ibQueryNode::Kind::Source) ? join->m_right->m_queryable : nullptr;
	if (qL == nullptr || qR == nullptr) return false;
	if (const ibBackendQueryColumn* lref = ReferenceColumnTo(qL, qR)) { onL = lref; onR = SelfReferenceColumn(qR); return onR != nullptr; }
	if (const ibBackendQueryColumn* rref = ReferenceColumnTo(qR, qL)) { onR = rref; onL = SelfReferenceColumn(qL); return onL != nullptr; }
	return false;
}

// Every column the query references — select-list + all join keys (whole tree) + Where.
// Each subtree materialises the subset its leaves provide; the keys/cols ride down so an
// inner join keeps what an outer join (or the final projection) still needs.
void CollectJoinKeys(const ibQueryNode* node, std::vector<const ibBackendQueryColumn*>& out)
{
	if (node == nullptr || node->m_kind != ibQueryNode::Kind::Join) return;
	const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
	if (ResolveJoinKeys(node, onL, onR)) { if (onL) out.push_back(onL); if (onR) out.push_back(onR); }
	CollectJoinKeys(node->m_left.get(),  out);
	CollectJoinKeys(node->m_right.get(), out);
}
std::vector<const ibBackendQueryColumn*> ReferencedColumns(const ibDataQuerySpec& spec)
{
	std::vector<const ibBackendQueryColumn*> cols;
	auto add = [&](const ibBackendQueryColumn* c) {
		if (c == nullptr) return;
		for (const auto* e : cols) if (e == c) return;
		cols.push_back(c);
	};
	for (const auto& s : *spec.m_selectCols)             add(s.first);
	for (const ibQuerySortItem& s : *spec.m_sorts)       add(s.m_col);   // ORDER BY cols ride too (sorted post-compose)
	for (const ibQueryCondition& c : *spec.m_conditions) add(c.m_col);
	CollectJoinKeys(spec.m_root, cols);
	return cols;
}

// Materialise ANY node to a RAM table keyed by GetModelID, carrying the referenced
// columns its leaves provide. Source -> read the leaf; Join -> recurse both sides and
// nested-loop on (onLeft = onRight). N-way left-deep falls out of the recursion. (§22.1)
ibQueryRamTable MaterialiseNode(const ibQueryNode* node, const std::vector<const ibBackendQueryColumn*>& refCols,
                                const ibDataQuerySpec& spec)
{
	if (node->m_kind == ibQueryNode::Kind::Source) {
		std::vector<const ibBackendQueryColumn*> mine;
		for (const ibBackendQueryColumn* c : refCols)
			if (node->m_queryable->OwnsColumn(c)) mine.push_back(c);
		return MaterialiseLeaf(node->m_queryable, spec.m_holder, LeafConditions(spec, node->m_queryable), mine);
	}

	// Join: recurse, then nested-loop. Output = refCols this subtree provides.
	const ibQueryRamTable TL = MaterialiseNode(node->m_left.get(),  refCols, spec);
	const ibQueryRamTable TR = MaterialiseNode(node->m_right.get(), refCols, spec);

	std::vector<const ibBackendQueryColumn*> outCols;     // provided cols, with their side
	std::vector<bool> fromLeft;
	for (const ibBackendQueryColumn* c : refCols) {
		if      (SubtreeProvides(node->m_left.get(),  c)) { outCols.push_back(c); fromLeft.push_back(true);  }
		else if (SubtreeProvides(node->m_right.get(), c)) { outCols.push_back(c); fromLeft.push_back(false); }
	}

	const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
	ResolveJoinKeys(node, onL, onR);   // explicit or derived (the tree was validated resolvable)

	return ibQueryComposer::JoinRamTables(TL, TR, onL, onR, outCols, fromLeft);
}

// Finalise a GetModelID-keyed combined table into the result: ORDER BY (RAM stable
// sort over m_sorts, each key read by GetModelID), project the select-list (alias-keyed
// output), and LIMIT to the page count. One finaliser for both JOIN and UNION.
ibDataQueryResult ProjectToAliases(const ibQueryRamTable& TC, const ibDataQuerySpec& spec, const ibReadPageRequest& page)
{
	const long rows = TC.RowCount();

	std::vector<long> order(static_cast<size_t>(rows));
	for (long i = 0; i < rows; ++i) order[static_cast<size_t>(i)] = i;
	if (!spec.m_sorts->empty())
		std::stable_sort(order.begin(), order.end(), [&](long a, long b) {
			for (const ibQuerySortItem& s : *spec.m_sorts) {
				if (s.m_col == nullptr) continue;
				const ibValue va = RamCell(TC, a, s.m_col);
				const ibValue vb = RamCell(TC, b, s.m_col);
				if (va == vb) continue;
				const bool lt = (va < vb);
				return s.m_ascending ? lt : !lt;
			}
			return false;
		});

	ibQueryRamTable TO;
	std::vector<ibMetaID> outIds;
	ibMetaID seq = 1;
	for (const auto& s : *spec.m_selectCols) {
		TO.AddColumn(seq, s.second, s.first->GetTypeDesc());   // output keyed by seq, named by alias
		outIds.push_back(seq++);
	}
	const long limit = (page.m_count > 0) ? page.m_count : rows;
	for (long oi = 0; oi < rows && oi < limit; ++oi) {
		const long i = order[static_cast<size_t>(oi)];
		const long r = TO.AppendRow();
		for (size_t k = 0; k < spec.m_selectCols->size(); ++k)
			TO.SetCell(r, outIds[k], RamCell(TC, i, (*spec.m_selectCols)[k].first));
	}
	return ibDataQueryResult(std::move(TO), spec.m_queryable);
}

// RAM UNION — vertical stack: each branch supplies the shared output columns BY NAME
// (heterogeneous branches, e.g. catalog ∪ temp, each with its own same-named column), rows
// concatenate. A branch missing an output column yields NULL for it. Returns a RAM-backed
// selection; read via GetColumn(alias). Every GLOBAL Where condition is applied to EVERY
// branch BY NAME (LeafConditionsByName) — a branch missing the named column drops that one
// condition. (A condition scoped to ONE branch needs the door to carry an alias-qualified
// Where — a follow-up that wants door/spec API, not this engine.) (docs §22.1b)
ibQueryRamTable RamUnion(const ibDataQuerySpec& spec, const ibQueryNode* unionNode,
                         const std::vector<const ibBackendQueryColumn*>& outCols)
{
	ibQueryRamTable TO;
	for (const ibBackendQueryColumn* c : outCols)
		TO.AddColumn(c->GetModelID(), c->GetName(), c->GetTypeDesc());

	for (const auto& partPtr : unionNode->m_parts) {
		const ibQueryNode* part = partPtr.get();
		const ibBackendQueryable* q = (part != nullptr) ? part->m_queryable : nullptr;
		if (q == nullptr) continue;

		// Resolve each output column to THIS branch's same-named column (null = absent).
		std::vector<const ibBackendQueryColumn*> branchCols;
		std::vector<const ibBackendQueryColumn*> toRead;
		for (const ibBackendQueryColumn* c : outCols) {
			const ibBackendQueryColumn* bc = q->ResolveColumnByName(c->GetName());
			branchCols.push_back(bc);
			if (bc != nullptr) toRead.push_back(bc);
		}

		const ibQueryRamTable TP = MaterialiseLeaf(q, spec.m_holder, LeafConditionsByName(spec, q), toRead);
		ibQueryComposer::AppendUnionBranch(TO, TP, outCols, branchCols);
	}
	return TO;
}

// Every Join node's keys resolvable? (explicit columns, OR derived from a reference
// between two Source leaves — Join(b) auto-join).
bool AllJoinsHaveKeys(const ibQueryNode* node)
{
	if (node == nullptr || node->m_kind != ibQueryNode::Kind::Join) return true;
	const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
	return ResolveJoinKeys(node, onL, onR) &&
	       AllJoinsHaveKeys(node->m_left.get()) && AllJoinsHaveKeys(node->m_right.get());
}

// Build the GetModelID-keyed combined table for a multi-source tree carrying `refCols`.
// JOIN -> recursive materialise; UNION -> branch concatenation. ONE entry for read AND
// aggregate (they differ only by which columns they ask to carry).
ibQueryRamTable Compose(const ibDataQuerySpec& spec, const std::vector<const ibBackendQueryColumn*>& refCols)
{
	if (spec.m_root->m_kind == ibQueryNode::Kind::Union)
		return RamUnion(spec, spec.m_root, refCols);
	return MaterialiseNode(spec.m_root, refCols, spec);   // Join (single-source handled earlier)
}

// Columns an aggregate needs in the combined table: group keys + aggregate inputs +
// Where columns + join keys.
std::vector<const ibBackendQueryColumn*> AggregateRefCols(const ibDataQuerySpec& spec)
{
	std::vector<const ibBackendQueryColumn*> cols;
	auto add = [&](const ibBackendQueryColumn* c) {
		if (c == nullptr) return;
		for (const auto* e : cols) if (e == c) return;
		cols.push_back(c);
	};
	for (const ibBackendQueryColumn* g : *spec.m_groupBy)        add(g);
	for (const auto& a : *spec.m_aggregates)                     add(a.m_col);
	for (const ibQueryCondition& c : *spec.m_conditions)         add(c.m_col);
	CollectJoinKeys(spec.m_root, cols);
	return cols;
}

// One aggregate over the rows of a bucket (column read by GetModelID). COUNT and a null
// column => row count; SUM/AVG over the number value; MIN/MAX by ibValue compare.
ibValue AggregateOne(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRamTable& TC, const std::vector<long>& idx)
{
	using Fn = ibDataQueryBuilder::AggregateFn;
	if (a.m_fn == Fn::Count || a.m_col == nullptr)
		return ibValue(ibNumber(static_cast<long>(idx.size())));

	ibNumber sum(0); long n = 0; ibValue best; bool have = false;
	for (long i : idx) {
		const ibValue v = RamCell(TC, i, a.m_col);
		switch (a.m_fn) {
		case Fn::Sum: case Fn::Avg: sum = sum + v.GetNumber(); ++n; break;
		case Fn::Min: if (!have || v < best) { best = v; have = true; } break;
		case Fn::Max: if (!have || v > best) { best = v; have = true; } break;
		default: break;
		}
	}
	switch (a.m_fn) {
	case Fn::Sum: return ibValue(sum);
	case Fn::Avg: return n > 0 ? ibValue(sum / ibNumber(static_cast<long>(n))) : ibValue();
	case Fn::Min: case Fn::Max: return best;
	default: return ibValue();
	}
}

// RAM GROUP BY over a composed combined table: bucket rows by the group keys, fold the
// aggregates per bucket. Group columns ride keyed by GetModelID (read via GetValue);
// aggregate columns are named by their alias (read via GetColumn(alias)).
ibDataQueryResult RamAggregate(const ibQueryRamTable& TC, const ibDataQuerySpec& spec)
{
	const long rows = TC.RowCount();

	std::vector<wxString> keyOrder;                    // first-seen group order
	std::map<wxString, std::vector<long>> buckets;
	for (long i = 0; i < rows; ++i) {
		wxString key;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			key += RamCell(TC, i, g).GetString() + wxT("\x1f");
		auto it = buckets.find(key);
		if (it == buckets.end()) { buckets.emplace(key, std::vector<long>{ i }); keyOrder.push_back(key); }
		else it->second.push_back(i);
	}
	if (spec.m_groupBy->empty() && rows > 0) {          // aggregate with no GROUP BY = one bucket
		std::vector<long> all; all.reserve(static_cast<size_t>(rows));
		for (long i = 0; i < rows; ++i) all.push_back(i);
		buckets.emplace(wxString(), all); keyOrder.push_back(wxString());
	}

	ibQueryRamTable TO;
	for (const ibBackendQueryColumn* g : *spec.m_groupBy)
		TO.AddColumn(g->GetModelID(), g->GetName(), g->GetTypeDesc());
	const ibMetaID aggBaseId = 0x40000000u;             // far from any metaID — aggregates read by alias NAME
	{
		ibMetaID aggId = aggBaseId;
		for (const auto& a : *spec.m_aggregates)
			TO.AddColumn(aggId++, a.m_alias, a.m_col != nullptr ? a.m_col->GetTypeDesc() : ibTypeDescription());
	}

	for (const wxString& key : keyOrder) {
		const std::vector<long>& idx = buckets[key];
		const long r = TO.AppendRow();
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			TO.SetCell(r, g->GetModelID(), RamCell(TC, idx.front(), g));
		ibMetaID aId = aggBaseId;
		for (const auto& a : *spec.m_aggregates)
			TO.SetCell(r, aId++, AggregateOne(a, TC, idx));
	}
	return ibDataQueryResult(std::move(TO), spec.m_queryable);
}

// Multi-level totals fold (hierarchical totals): a node holds the aggregates over its rows; recurse a
// subtotal node per distinct value of groupFields[level], carrying the group-key path
// down. Root (level 0) = grand total. (docs/query-language-arc.md §22.1b)
void FoldTotals(ibQueryRamTable::Node& node, const ibQueryRamTable& TC, const std::vector<long>& rows,
                const std::vector<const ibBackendQueryColumn*>& groupFields,
                const std::vector<ibDataQueryBuilder::AggregateItem>& aggs, size_t level,
                unsigned int aggBaseId, const std::map<ibMetaID, ibValue>& keys)
{
	node.m_values = keys;
	for (size_t a = 0; a < aggs.size(); ++a)
		node.m_values[static_cast<ibMetaID>(aggBaseId + a)] = AggregateOne(aggs[a], TC, rows);
	if (level >= groupFields.size())
		return;

	std::vector<wxString> order;
	std::map<wxString, std::vector<long>> buckets;
	std::map<wxString, ibValue> keyVal;
	for (long i : rows) {
		const ibValue v = RamCell(TC, i, groupFields[level]);
		const wxString k = v.GetString();
		if (buckets.find(k) == buckets.end()) { order.push_back(k); keyVal[k] = v; }
		buckets[k].push_back(i);
	}
	for (const wxString& k : order) {
		ibQueryRamTable::Node* child = node.AddChild(static_cast<int>(level) + 1);
		std::map<ibMetaID, ibValue> childKeys = keys;
		childKeys[groupFields[level]->GetModelID()] = keyVal[k];
		FoldTotals(*child, TC, buckets[k], groupFields, aggs, level + 1, aggBaseId, childKeys);
	}
}

// Compose a multi-source tree, in RAM. JOIN of any depth (N-way left-deep) on explicit
// columns, and UNION of Source branches. Auto-join-by-reference is the remaining shape.
ibDataQueryResult ComposeMultiSource(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
{
	const ibQueryNode* root = spec.m_root;
	if (root == nullptr)
		RamCompositionNotYet();

	const bool ok = (root->m_kind == ibQueryNode::Kind::Union && !root->m_parts.empty()) ||
	                (root->m_kind == ibQueryNode::Kind::Join && AllJoinsHaveKeys(root));
	if (!ok)
		RamCompositionNotYet();

	return ProjectToAliases(Compose(spec, ReferencedColumns(spec)), spec, page);
}
} // namespace

ibDataQueryResult ibQueryComposer::ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
{
	if (IsSingleSource(spec))
		return spec.m_queryable->GetProvider().ExecuteRead(spec, page);
	return ComposeMultiSource(spec, page);
}

ibDataQueryResult ibQueryComposer::ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& page,
                                                     ibRenderedPageCache& cache, const wxString& signature)
{
	if (IsSingleSource(spec))
		return spec.m_queryable->GetProvider().ExecuteReadCached(spec, page, cache, signature);
	return ComposeMultiSource(spec, page);   // a join has no single rendered-SQL cache
}

ibDataQueryResult ibQueryComposer::ExecuteAggregate(const ibDataQuerySpec& spec)
{
	if (IsSingleSource(spec))
		return spec.m_queryable->GetProvider().ExecuteAggregate(spec);

	// Aggregate OVER a composed result: compose the leaves (carrying group / aggregate /
	// key / Where columns) into a RAM table, then GROUP BY in RAM.
	const ibQueryNode* root = spec.m_root;
	const bool ok = root != nullptr &&
	                ((root->m_kind == ibQueryNode::Kind::Union && !root->m_parts.empty()) ||
	                 (root->m_kind == ibQueryNode::Kind::Join && AllJoinsHaveKeys(root)));
	if (!ok)
		RamCompositionNotYet();

	return RamAggregate(Compose(spec, AggregateRefCols(spec)), spec);
}

// ИТОГИ fold over a combined row set -> a RAW totals TREE (L3's own ibQueryRamTable). L3
// stops here — flat-vs-hierarchical rendering is the runtime's call. Group columns keyed by
// GetModelID; aggregates by a synthetic id (read by alias). Grand total = the root (level
// 0). Pure (no DB) — exposed so the fold is unit-testable directly. (docs §22.1b)
ibQueryRamTable ibQueryComposer::BuildTotalsTree(const ibQueryRamTable& detail,
		const std::vector<const ibBackendQueryColumn*>& groupFields,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	const long n = detail.RowCount();
	std::vector<long> all; all.reserve(static_cast<size_t>(n));
	for (long i = 0; i < n; ++i) all.push_back(i);

	ibQueryRamTable tree;
	for (const ibBackendQueryColumn* g : groupFields)
		tree.AddColumn(g->GetModelID(), g->GetName(), g->GetTypeDesc());
	const unsigned int aggBaseId = 0x40000000u;
	for (size_t a = 0; a < aggregates.size(); ++a) {
		const ibDataQueryBuilder::AggregateItem& ai = aggregates[a];
		tree.AddColumn(static_cast<ibMetaID>(aggBaseId + a), ai.m_alias,
		               ai.m_col != nullptr ? ai.m_col->GetTypeDesc() : ibTypeDescription());
	}
	FoldTotals(tree.Root(), detail, all, groupFields, aggregates, 0, aggBaseId, {});
	return tree;   // raw — the runtime decides flat vs. hierarchical
}

// RAM inner join: nested-loop over (onLeft == onRight); each output column is read from
// its tagged side. Keyed by GetModelID throughout. Pure — the multi-source JOIN core.
ibQueryRamTable ibQueryComposer::JoinRamTables(const ibQueryRamTable& left, const ibQueryRamTable& right,
		const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
		const std::vector<const ibBackendQueryColumn*>& outCols, const std::vector<bool>& fromLeft)
{
	ibQueryRamTable out;
	for (const ibBackendQueryColumn* c : outCols)
		out.AddColumn(c->GetModelID(), c->GetName(), c->GetTypeDesc());

	for (long i = 0; i < left.RowCount(); ++i) {
		const ibValue keyL = (onLeft != nullptr) ? left.GetCell(i, onLeft->GetModelID()) : ibValue();
		for (long j = 0; j < right.RowCount(); ++j) {
			const ibValue keyR = (onRight != nullptr) ? right.GetCell(j, onRight->GetModelID()) : ibValue();
			if (!(keyL == keyR))
				continue;
			const long r = out.AppendRow();
			for (size_t k = 0; k < outCols.size(); ++k) {
				const ibQueryRamTable& src = fromLeft[k] ? left : right;
				const long srcRow = fromLeft[k] ? i : j;
				out.SetCell(r, outCols[k]->GetModelID(), src.GetCell(srcRow, outCols[k]->GetModelID()));
			}
		}
	}
	return out;
}

// RAM UNION stacking: append a branch's rows, each output column read from the branch's
// same-named column (null = absent -> NULL). Vertical concat for heterogeneous branches.
void ibQueryComposer::AppendUnionBranch(ibQueryRamTable& out, const ibQueryRamTable& branch,
		const std::vector<const ibBackendQueryColumn*>& outCols,
		const std::vector<const ibBackendQueryColumn*>& branchCols)
{
	for (long i = 0; i < branch.RowCount(); ++i) {
		const long r = out.AppendRow();
		for (size_t k = 0; k < outCols.size(); ++k)
			out.SetCell(r, outCols[k]->GetModelID(),
			            branchCols[k] != nullptr ? branch.GetCell(i, branchCols[k]->GetModelID()) : ibValue());
	}
}

// ИТОГИ (hierarchical totals): fold the detail rows into a subtotal TREE. The group
// columns are the LEVELS (in order); the aggregates the sums folded at every level + the
// grand total. Input = the single source materialised, or the composed multi-source
// result. The tree lives in L3's own ibQueryRamTable. (docs/query-language-arc.md §22.1b)
ibQueryRamTable ibQueryComposer::ExecuteTotals(const ibDataQuerySpec& spec)
{
	ibQueryRamTable combined;
	if (IsSingleSource(spec)) {
		// Materialise the source's group + sum columns, unfiltered by page.
		std::vector<const ibBackendQueryColumn*> cols = *spec.m_groupBy;
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			if (a.m_col != nullptr) cols.push_back(a.m_col);
		combined = MaterialiseLeaf(spec.m_queryable, spec.m_holder,
		                           LeafConditions(spec, spec.m_queryable), cols);
	} else {
		const ibQueryNode* root = spec.m_root;
		const bool ok = root != nullptr &&
		                ((root->m_kind == ibQueryNode::Kind::Union && !root->m_parts.empty()) ||
		                 (root->m_kind == ibQueryNode::Kind::Join && AllJoinsHaveKeys(root)));
		if (!ok)
			RamCompositionNotYet();
		combined = Compose(spec, AggregateRefCols(spec));
	}
	return BuildTotalsTree(combined, *spec.m_groupBy, *spec.m_aggregates);
}

bool ibQueryComposer::ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind)
{
	// Writes always target a single real table — there is no multi-source write.
	return spec.m_queryable->GetProvider().ExecuteWrite(spec, kind);
}

namespace {

// RAM table source — walks an ibQueryRamTable (L3's OWN table; NO runtime type). Value(col)
// reads by the column's model id (register-slice dimensions self-describe their metaID; temp
// columns their source id); Column(alias) by the output column name (aggregates / derived
// columns with no column object). EVERY RAM source rides on this — multi-source composition
// AND a computed virtual table (slice / balance / turnover / temp), so neither produces a
// runtime ibValueModelTable. Needs no queryable. (docs §22.1a, §22.6)
class ibRamTableResultSource : public ibDataResultSource {
public:
	ibRamTableResultSource(ibQueryRamTable&& table, const ibBackendQueryable* /*queryable*/)
		: m_table(std::move(table)) {}

	bool Next() override { return ++m_row < m_table.RowCount(); }

	ibValue Value(const ibBackendQueryColumn* col) const override {
		return (m_row >= 0 && col != nullptr) ? m_table.GetCell(m_row, col->GetModelID()) : ibValue();
	}
	ibValue Column(const wxString& alias) const override {
		for (const ibQueryRamColumn& c : m_table.Columns())
			if (c.m_name == alias) return m_table.GetCell(m_row, c.m_id);
		return ibValue();
	}

private:
	ibQueryRamTable m_table;     // owns the composed rows
	long            m_row = -1;
};

} // namespace

// ==========================================================================
// ibDataQueryResult — the L3 selection: a thin value handle over a polymorphic
// ibDataResultSource. Ready ibValue rows out; the backing is hidden even from this
// class. Special members out-of-line where the source types are complete.
// ==========================================================================

ibDataQueryResult::ibDataQueryResult(ibQueryRamTable&& ramTable, const ibBackendQueryable* queryable)
	: m_source(std::make_unique<ibRamTableResultSource>(std::move(ramTable), queryable))
{
}

ibDataQueryResult::~ibDataQueryResult() = default;
ibDataQueryResult::ibDataQueryResult(ibDataQueryResult&&) noexcept = default;
ibDataQueryResult& ibDataQueryResult::operator=(ibDataQueryResult&&) noexcept = default;

bool     ibDataQueryResult::Next()                                                     { return m_source->Next(); }
ibValue  ibDataQueryResult::GetValue(const ibBackendQueryColumn* col)            const { return m_source->Value(col); }
ibValue  ibDataQueryResult::GetValue(const ibRawDBColumn& rawColumn)             const { return m_source->Value(&rawColumn); }
ibValue  ibDataQueryResult::GetColumn(const wxString& alias)                     const { return m_source->Column(alias); }

// Factory for the opaque build-once cache (defined where ibRenderedPageCache is
// complete) — the list model owns the result via shared_ptr.
std::shared_ptr<ibRenderedPageCache> ibDataQueryBuilder::NewPageCache()
{
	return std::make_shared<ibRenderedPageCache>();
}
