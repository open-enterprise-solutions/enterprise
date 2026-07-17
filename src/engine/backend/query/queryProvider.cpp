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
#include "queryRamTable.h"                                            // ibQueryRamTable — L3's raw flat snapshot (names no runtime type but ibValue)
#include "querySelector.h"                                            // ibSelector — result.Select(mode) hands the drained snapshot to it
#include "dbTableProvider.h"                                          // ibDbTableProvider (vended by GetProvider) + ibRenderedPageCache (NewPageCache)
#include "resultSource.h"                                             // ibDataResultSource — the backing ibRamTableResultSource derives
#include "tempTableManager.h"                                         // ibTempTableManager — promote a computed leaf to a DB temp table (+ ibDbTempTableQueryable)

#include <map>                                                        // dot-walk join dedup + col->attr cache
#include <functional>                                                 // std::function — reference-hierarchy parent chain-up
#include <stdexcept>                                                  // guard for the not-yet-built multi-source composition path
#include <algorithm>                                                  // stable_sort — RAM ORDER BY over a composed result
#include <set>                                                        // DedupeRows — seen-row identity keys (plain UNION)

namespace {
// Build an empty RAM table with the given metadata columns (id / name / type), and append a src row to dst
// copying those columns by id — the structure-init + row-copy idiom the RAM cores repeat. (Pure helpers.)
ibQueryRamTable RamTableOf(const std::vector<const ibBackendQueryColumn*>& cols)
{
	ibQueryRamTable t;
	for (const ibBackendQueryColumn* c : cols)
		if (c != nullptr) t.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());
	return t;
}
void AppendRowByCols(const ibQueryRamTable& src, long srcRow, ibQueryRamTable& dst,
                     const std::vector<const ibBackendQueryColumn*>& cols)
{
	const long r = dst.AppendRow();
	for (const ibBackendQueryColumn* c : cols)
		if (c != nullptr) dst.SetCell(r, c->GetColumnId(), src.GetCell(srcRow, c->GetColumnId()));
}
} // namespace

// RAM dot-walk resolution for a computed source — DEFINED BELOW (it needs MaterialiseLeaf /
// SelfReferenceColumn). Each reference-path leaf is resolved by materialising the hop targets and
// LEFT-joining them onto the computed rows (the RAM analog of the door's SelectPath auto-join).
namespace {
ibQueryRamTable ResolveComputedDotWalks(ibQueryRamTable rows, const ibBackendQueryable* primary,
                                        const ibDataQuerySpec& spec,
                                        std::vector<const ibBackendQueryColumn*>& present);
// ComputeRows over the computed source + dot-walk resolution + dot-walk WHERE (shared read/aggregate).
ibQueryRamTable ComputeRowsResolved(const ibDataQuerySpec& spec,
                                    std::vector<const ibBackendQueryColumn*>& present);
// Per-row RAM evaluation of a computed (arithmetic / CASE) expression — defined below; forward-declared
// here so the computed read / WHERE / aggregate paths above can evaluate SELECT / WHERE / SUM expressions.
ibValue EvalColumnExprRow(const ibQueryColumnExpr* e, const ibQueryRamTable& t, long row);

// A synthetic output column for a computed SELECT expression (Qty * Price AS x) over a computed source:
// the alias as its name + a unique id. ExecuteRead evaluates the expr into this column; GetColumn(alias)
// finds it by name, the sort / limit rebuild keeps it by id.
class ibComputedExprColumn : public ibBackendQueryColumn {
public:
	ibComputedExprColumn(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};
} // namespace

// Reference dot-walk resolution over a COMPUTED source — this layer owns no metadata, so it FORWARDS
// to the DB provider (the single metadata home). Resolution reads queryable->GetMetaData(), which a
// computed register carries (it mirrors the register), so Balance.Item.Name resolves on the RAM path.
const ibBackendQueryable* ibComputedProvider::ResolveReferenceTarget(const ibBackendQueryable* queryable,
                                                                     const ibBackendQueryColumn* refColumn) const
{
	static ibDbTableProvider s_db;   // stateless — resolution reads only its arguments' metadata
	return s_db.ResolveReferenceTarget(queryable, refColumn);
}

std::vector<const ibBackendQueryable*> ibComputedProvider::ResolveReferenceTargets(const ibBackendQueryable* queryable,
                                                                                   const ibBackendQueryColumn* refColumn) const
{
	static ibDbTableProvider s_db;
	return s_db.ResolveReferenceTargets(queryable, refColumn);
}

// ibComputedProvider — the RAM-computed virtual table (register slice / balance /
// turnover / subquery). Stateless: computes the rows from the spec, no physical scan, no
// L2. The flat conditions push into ComputeRows; the boolean predicate TREE, the ORDER BY
// and the page row limit (TOP) apply on the materialised RAM rows HERE — without this they
// would silently drop on a computed source.
ibDataQueryResult ibComputedProvider::ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& req)
{
	// ComputeRows + dot-walk resolution + dot-walk WHERE: plain conditions push into ComputeRows, a dot-walk
	// Ref.Field condition joins the reference leaf and filters in RAM. `cols` receives the source columns +
	// every joined leaf, so the DISTINCT / sort / limit rebuilds below keep them.
	std::vector<const ibBackendQueryColumn*> cols;
	ibQueryRamTable rows = ComputeRowsResolved(spec, cols);

	// COMPUTED output columns (SELECT Qty * Price, a CASE) — evaluate the expression per row and add it as a
	// column NAMED by its alias (the RAM analog of the DB provider projecting `expr AS alias`). Added to `cols`
	// so the sort / limit rebuild keeps it; read back by GetColumn(alias), resolved by name.
	std::vector<ibComputedExprColumn> exprCols;
	if (spec.m_selectExprs != nullptr && !spec.m_selectExprs->empty()) {
		exprCols.reserve(spec.m_selectExprs->size());
		ibMetaID exprId = 0x70000000u;   // synthetic ids, clear of metaIDs / COUNT(*) (0x40..) / subquery-agg (0x60..)
		for (const ibQueryColumnSelect& sc : *spec.m_selectExprs)
			exprCols.emplace_back(sc.m_alias, exprId++);
		for (size_t k = 0; k < exprCols.size(); ++k) {
			rows.AddColumn(exprCols[k].GetColumnId(), exprCols[k].GetName(), exprCols[k].GetTypeDesc());
			for (long i = 0; i < rows.RowCount(); ++i)
				rows.SetCell(i, exprCols[k].GetColumnId(),
				             EvalColumnExprRow((*spec.m_selectExprs)[k].m_expr.get(), rows, i));
			cols.push_back(&exprCols[k]);
		}
	}

	if (spec.m_predicate)
		rows = ibQueryComposer::FilterRows(rows, spec.m_predicate.get());

	// SELECT DISTINCT over a computed source (subquery / slice) -- dedup by the output columns while keeping
	// ALL columns (the sort below may key on one not in the select list). First occurrence wins.
	if (spec.m_distinct && spec.m_selectCols != nullptr && !spec.m_selectCols->empty()) {
		ibQueryRamTable deduped = RamTableOf(cols);
		std::set<wxString> seen;
		for (long i = 0; i < rows.RowCount(); ++i) {
			wxString key;
			for (const auto& sc : *spec.m_selectCols)
				key += rows.GetCell(i, sc.first->GetColumnId()).GetHashKey() + wxT("\x1f");
			if (!seen.insert(key).second) continue;
			AppendRowByCols(rows, i, deduped, cols);
		}
		rows = std::move(deduped);
	}

	if (spec.m_sorts != nullptr && !spec.m_sorts->empty()) {
		std::vector<long> order(static_cast<size_t>(rows.RowCount()));
		for (long i = 0; i < rows.RowCount(); ++i) order[static_cast<size_t>(i)] = i;
		std::stable_sort(order.begin(), order.end(), [&](long a, long b) {
			for (const ibQuerySortItem& s : *spec.m_sorts) {
				if (s.m_col == nullptr) continue;
				const ibValue va = rows.GetCell(a, s.m_col->GetColumnId());
				const ibValue vb = rows.GetCell(b, s.m_col->GetColumnId());
				const int c = ibQueryComposer::RamSortCompareKey(va, vb, s.m_ascending);
				if (c != 0) return c < 0;
			}
			return false;
		});
		ibQueryRamTable sorted = RamTableOf(cols);
		const long limit = (req.m_count > 0 && req.m_count < rows.RowCount()) ? req.m_count : rows.RowCount();
		for (long oi = 0; oi < limit; ++oi)
			AppendRowByCols(rows, order[static_cast<size_t>(oi)], sorted, cols);
		return ibDataQueryResult(std::move(sorted), spec.m_queryable);
	}

	if (req.m_count > 0 && req.m_count < rows.RowCount()) {
		ibQueryRamTable limited = RamTableOf(cols);
		for (long i = 0; i < req.m_count; ++i)
			AppendRowByCols(rows, i, limited, cols);
		return ibDataQueryResult(std::move(limited), spec.m_queryable);
	}

	return ibDataQueryResult(std::move(rows), spec.m_queryable);
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
// Select(col, alias) list, else (SELECT *) the inner primary source's full column set; an
// AGGREGATE inner exposes its GROUP BY keys + a synthetic numeric column per aggregate
// alias. The bodies live here (where ibDataQueryBuilder is complete). (docs §22 / §23)
// ==========================================================================

// Synthetic output column of an AGGREGATE subquery (SUM(x) AS total): a raw numeric field
// named by the aggregate alias, under its OWN unique model id (the RAM-table read key). The
// id range is clear of real metaIDs, the COUNT(*) receivers (0x40000000) and the totals
// synthetics (0x50000000).
namespace {
const ibMetaID kSubqueryAggColumnBase = 0x60000000u;

class ibSubqueryAggColumn final : public ibRawDBColumn
{
public:
	ibSubqueryAggColumn(const wxString& alias, ibMetaID id)
		: ibRawDBColumn(alias, RawType::Number), m_id(id) {}
	ibMetaID GetColumnId() const override { return m_id; }
private:
	ibMetaID m_id;
};

// One pushed-down outer condition against a materialised RAM cell — the aggregate
// subquery's post-filter (the condition references POST-aggregation output, HAVING
// semantics, so it cannot ride the inner WHERE). LIKE translates % / _ to wx wildcards.
bool MatchRamCondition(const ibValue& cell, const ibQueryCondition& c)
{
	switch (c.m_op) {   // ONE op now (m_comparison + m_explicitOp collapsed into m_op)
	case ibQueryFilterOp::Equal:    return  (cell == c.m_value);
	case ibQueryFilterOp::NotEqual: return !(cell == c.m_value);
	case ibQueryFilterOp::Like: {
		wxString p = c.m_value.GetString();
		p.Replace(wxT("%"), wxT("*"));
		p.Replace(wxT("_"), wxT("?"));
		return cell.GetString().Matches(p);
	}
	case ibQueryFilterOp::Less:         return cell < c.m_value;
	case ibQueryFilterOp::LessEqual:    return cell < c.m_value || cell == c.m_value;
	case ibQueryFilterOp::Greater:      return !(cell < c.m_value) && !(cell == c.m_value);
	case ibQueryFilterOp::GreaterEqual: return !(cell < c.m_value);
	}
	return false;
}
} // namespace

ibSubqueryQueryable::ibSubqueryQueryable(const ibDataQueryBuilder& inner, long topCount)
	: m_inner(std::make_unique<ibDataQueryBuilder>(inner)), m_top(topCount)
{
	const auto& aggs = m_inner->GetAggregates();
	m_aggregate = !aggs.empty();
	if (m_aggregate) {
		// AGGREGATE shape: exposed columns = the GROUP BY keys (real columns, by name) + one
		// owned synthetic numeric column per aggregate alias.
		for (const ibBackendQueryColumn* g : m_inner->GetGroupBy())
			if (g != nullptr) m_columns.push_back(g);
		ibMetaID nextId = kSubqueryAggColumnBase;
		for (const ibDataQueryBuilder::AggregateItem& a : aggs) {
			auto col = std::make_shared<ibSubqueryAggColumn>(a.m_alias, nextId++);
			m_ownedColumns.push_back(col);
			m_columns.push_back(col.get());
		}
		return;
	}
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
// into a RAM table keyed by column model-id — the same leaf-materialise recipe. m_top > 0
// limits the materialised rows (SELECT TOP n in the branch / subquery).
ibQueryRamTable ibSubqueryQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	ibQueryRamTable t;
	for (const ibBackendQueryColumn* col : m_columns)
		if (col != nullptr) t.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());

	if (m_aggregate) {
		// AGGREGATE inner — run SelectAggregate; group keys read by column, aggregate aliases by
		// name. The outer's pushed-down conditions reference POST-aggregation output (HAVING
		// semantics), so they apply as a RAM post-filter here, never on the inner WHERE.
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggs = m_inner->GetAggregates();
		const std::vector<const ibBackendQueryColumn*>&       groups = m_inner->GetGroupBy();

		ibDataQueryResult sel = m_inner->SelectAggregate();
		long emitted = 0;
		std::vector<ibValue> rowVals(m_columns.size());
		while (sel.Next()) {
			size_t k = 0;
			for (const ibBackendQueryColumn* g : groups)
				rowVals[k++] = sel.GetValue(g);
			for (const ibDataQueryBuilder::AggregateItem& a : aggs)
				rowVals[k++] = sel.GetColumn(a.m_alias);

			bool keep = true;
			for (const ibQueryCondition& c : extra) {
				if (c.m_col == nullptr) continue;
				for (size_t i = 0; i < m_columns.size(); ++i)
					if (m_columns[i] == c.m_col) { keep = MatchRamCondition(rowVals[i], c); break; }
				if (!keep) break;
			}
			if (!keep) continue;

			const long r = t.AppendRow();
			for (size_t i = 0; i < m_columns.size(); ++i)
				t.SetCell(r, m_columns[i]->GetColumnId(), rowVals[i]);
			if (m_top > 0 && ++emitted >= m_top) break;
		}
		return t;
	}

	// The subquery's exposed columns ARE the inner columns, so the outer's extra
	// conditions apply straight onto a copy of the inner query.
	ibDataQueryBuilder q(*m_inner);
	for (const ibQueryCondition& c : extra) {
		if (c.m_col == nullptr) continue;
		q.Where(c.m_col, c.m_op, c.m_value);   // ONE op — Where carries any ibQueryFilterOp now
	}

	ibReadPageRequest page; page.m_count = m_top;   // 0 = all rows; TOP n = the branch limit
	ibDataQueryResult sel = q.Execute(page);
	while (sel.Next()) {
		const long r = t.AppendRow();
		for (const ibBackendQueryColumn* col : m_columns)
			if (col != nullptr) t.SetCell(r, col->GetColumnId(), sel.GetValue(col));
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
	add(join->m_on.m_colL);
	add(join->m_on.m_colR);
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

// --- planner decision at the materialisation seam (temp-db foundation, docs/temp-db.md §7) ----
// The temp decision splits in two, each owned where its inputs live:
//   SHOULD — the size lever, HERE (WorthDbTemp): below the threshold a DB temp is not worth its
//            CREATE+INSERT round-trips; a small intermediate stays in RAM regardless of capability.
//            The promote sites call it with the EXACT materialised row count — real cost, no estimator.
//   CAN    — capability by presence + the runtime probe + graceful fallback, inside
//            ibTempTableManager::Materialise (returns null ⇒ the caller stays on RAM).
const long kTempTableMinRows = 1000;   // heuristic; tune against real numbers

bool WorthDbTemp(long rowCount) { return rowCount >= kTempTableMinRows; }

// Materialisation SEAM — read a leaf through its OWN provider (single-source, its
// conditions pushed down) and collect the needed columns into a RAM table, keyed by
// GetColumnId. (docs/query-language-arc.md §22.1a, docs/temp-db.md)
ibQueryRamTable MaterialiseLeafToRam(const ibBackendQueryable* leaf, ibDatabaseConnectionHolder* holder,
                                     const std::vector<ibQueryCondition>& conds,
                                     const std::vector<const ibBackendQueryColumn*>& cols)
{
	ibQueryRamTable t;
	for (const ibBackendQueryColumn* col : cols)
		t.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());

	ibDataQueryBuilder q(holder);
	q.From(leaf);
	for (const ibQueryCondition& c : conds) {
		q.Where(c.m_col, c.m_op, c.m_value);   // ONE op — Where carries any ibQueryFilterOp now
	}
	ibReadPageRequest page; page.m_count = 0;   // every matching row
	ibDataQueryResult sel = q.Execute(page);     // reads through the cursor — never names a runtime table
	while (sel.Next()) {
		const long r = t.AppendRow();
		for (const ibBackendQueryColumn* col : cols)
			t.SetCell(r, col->GetColumnId(), sel.GetValue(col));
	}
	return t;
}

// Leaf materialisation entry for the composer's join/union/totals. Deliberately RAM:
// temping a leaf pays only when the JOIN itself goes server-side, and the server-side
// promotions live ABOVE this level — PromoteComputedLeaf (computed ⋈ DB) and
// PromoteUnionBranches, which gate on WorthDbTemp with the REAL row count and own the
// temp manager (its Materialise carries the CAN-gate + the runtime fallback). A leaf
// that reaches here is being stitched in RAM, where a DB temp gains nothing
// (docs/temp-db.md §8). A new promotable RAM-stitch shape extends the promote family,
// not this function.
ibQueryRamTable MaterialiseLeaf(const ibBackendQueryable* leaf, ibDatabaseConnectionHolder* holder,
                                const std::vector<ibQueryCondition>& conds,
                                const std::vector<const ibBackendQueryColumn*>& cols)
{
	return MaterialiseLeafToRam(leaf, holder, conds, cols);
}

// One cell of a materialised RAM table at (row, column model-id).
ibValue RamCell(const ibQueryRamTable& t, long row, const ibBackendQueryColumn* col)
{
	return col != nullptr ? t.GetCell(row, col->GetColumnId()) : ibValue();
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

// RAM dot-walk resolution for a COMPUTED source (register slice / balance / turnover / subquery). Each
// ibDotWalkColumn (ref segments + leaf) is resolved by materialising every reference hop's TARGET as a RAM
// table (MaterialiseLeaf) and LEFT-joining it onto the computed rows, keyed on (segment ref column, target
// self-reference) — the RAM analog of ExpandDotWalkJoins, which builds SQL joins for a physical source. The
// leaf column lands in `rows` keyed by GetColumnId, so projection / filter / sort read it like a plain
// column. `present` accumulates every column now in `rows` (the source's own + each brought-in leaf) so the
// caller's DISTINCT / sort / limit rebuilds keep them. Sibling paths sharing a ref prefix reuse ONE join
// (joined). A non-single-target (composite) reference hop is SKIPPED — the leaf stays a null cell (mirrors
// the door's single-target guard), never a throw. (docs/query-language-arc.md §22 computed dot-walk)
ibQueryRamTable ResolveComputedDotWalks(ibQueryRamTable rows, const ibBackendQueryable* primary,
                                        const ibDataQuerySpec& spec,
                                        std::vector<const ibBackendQueryColumn*>& present)
{
	// Gather every dot-walk PATH used over the computed source — projection (m_dotWalks), ORDER BY (m_sorts),
	// GROUP BY (m_groupPaths) and aggregate inputs (m_aggregates[].m_path). Each is a reference chain to
	// materialise + LEFT-join; the leaf lands keyed by GetColumnId so every clause reads it as a plain column.
	// (WHERE dot-walk is a later slice — its flat conditions push into ComputeRows, which the register owns.)
	std::vector<const std::vector<const ibBackendQueryColumn*>*> paths;
	if (spec.m_dotWalks != nullptr)
		for (const ibDotWalkColumn& dw : *spec.m_dotWalks)
			if (dw.m_path.size() > 1) paths.push_back(&dw.m_path);
	if (spec.m_sorts != nullptr)
		for (const ibQuerySortItem& s : *spec.m_sorts)
			if (s.m_path.size() > 1) paths.push_back(&s.m_path);
	if (spec.m_groupPaths != nullptr)
		for (const std::vector<const ibBackendQueryColumn*>& gp : *spec.m_groupPaths)
			if (gp.size() > 1) paths.push_back(&gp);
	if (spec.m_aggregates != nullptr)
		for (const ibAggregateItem& a : *spec.m_aggregates)
			if (a.m_path.size() > 1) paths.push_back(&a.m_path);
	if (spec.m_conditions != nullptr)
		for (const ibQueryCondition& c : *spec.m_conditions)
			if (c.m_path.size() > 1) paths.push_back(&c.m_path);   // flat WHERE Ref.Field = X — join the leaf, filter in RAM
	// Boolean WHERE predicate (OR / NOT / IS NULL over dot-walk leaves) — walk the tree, collect each leaf's path.
	std::function<void(const ibQueryPredicate*)> gatherPred = [&](const ibQueryPredicate* p) {
		if (p == nullptr) return;
		if (p->m_leaf.m_path.size() > 1) paths.push_back(&p->m_leaf.m_path);   // Leaf condition dot-walk
		if (p->m_path.size() > 1)        paths.push_back(&p->m_path);          // IS NULL dot-walk
		for (const ibQueryPredicatePtr& ch : p->m_children) gatherPred(ch.get());
	};
	gatherPred(spec.m_predicate.get());

	std::map<wxString, bool> joined;   // ref-prefix key -> already joined (dedup sibling / repeated paths)
	for (const std::vector<const ibBackendQueryColumn*>* pathPtr : paths) {
		const std::vector<const ibBackendQueryColumn*>& path = *pathPtr;
		const ibBackendQueryable* curQ = primary;
		wxString prefixKey;
		for (size_t i = 0; i + 1 < path.size(); ++i) {
			const ibBackendQueryColumn* refCol = path[i];
			const ibBackendQueryable*   tgtQ   = (curQ != nullptr) ? curQ->GetProvider().ResolveReferenceTarget(curQ, refCol) : nullptr;
			const ibBackendQueryColumn* tgtKey = SelfReferenceColumn(tgtQ);
			const ibBackendQueryColumn* bring  = path[i + 1];   // the next hop's ref column, or the final leaf
			if (tgtQ == nullptr || tgtKey == nullptr)
				break;   // not a single-target reference — the leaf stays absent (a null cell), like the door's guard
			prefixKey += wxString::Format(wxT("%p|"), (const void*)refCol);
			if (joined.find(prefixKey) == joined.end()) {
				const ibQueryRamTable tgt = MaterialiseLeaf(tgtQ, spec.m_holder, {}, { tgtKey, bring });
				std::vector<const ibBackendQueryColumn*> outCols = present;               // keep every present column (LEFT) ...
				std::vector<bool> fromLeft(present.size(), true);
				outCols.push_back(bring);  fromLeft.push_back(false);                     // ... plus the brought-in target column (RIGHT)
				rows = ibQueryComposer::JoinRamTables(rows, tgt, refCol, tgtKey, outCols, fromLeft, ibQueryJoinKind::Left);
				present.push_back(bring);
				joined[prefixKey] = true;
			}
			curQ = tgtQ;
		}
	}
	return rows;
}

// ComputeRows over a COMPUTED source PLUS dot-walk resolution + a dot-walk WHERE. PLAIN door conditions
// push into ComputeRows (the register / slice filters them itself); a dot-walk condition (WHERE Ref.Field
// = X) the register cannot do, so the reference leaf is LEFT-joined in (ResolveComputedDotWalks) and the
// condition applied in RAM by its now-plain leaf column. `present` receives the source columns + every
// joined leaf (for the caller's DISTINCT / sort / limit rebuilds). Shared by the read + aggregate paths.
ibQueryRamTable ComputeRowsResolved(const ibDataQuerySpec& spec,
                                    std::vector<const ibBackendQueryColumn*>& present)
{
	// PLAIN conditions push into ComputeRows; a dot-walk OR computed-expr condition is deferred to the RAM
	// filter below (the register can filter neither a reference leaf nor an arithmetic / CASE expression).
	std::vector<ibQueryCondition> plainConds;
	if (spec.m_conditions != nullptr)
		for (const ibQueryCondition& c : *spec.m_conditions)
			if (c.m_path.empty() && !c.m_expr) plainConds.push_back(c);

	ibQueryRamTable rows = spec.m_queryable->ComputeRows(plainConds);
	present = spec.m_queryable->GetColumns();
	rows = ResolveComputedDotWalks(std::move(rows), spec.m_queryable, spec, present);

	// A dot-walk (Ref.Field = X) or computed-expr (Qty * Price > N) WHERE — the reference leaf is now a
	// JOINED plain column and the expr evaluates per row, so filter it in RAM (RamEvalLeaf handles both).
	if (spec.m_conditions != nullptr)
		for (ibQueryCondition c : *spec.m_conditions)
			if (!c.m_path.empty() || c.m_expr) {
				c.m_path.clear();   // a joined dot-walk leaf reads by m_col; an expr condition keeps m_expr
				rows = ibQueryComposer::FilterRows(rows, ibQueryPredicate::Leaf(c).get());
			}
	return rows;
}

// A column of `from` whose reference resolves to `to` — the referencing side of an auto-join.
const ibBackendQueryColumn* ReferenceColumnTo(const ibBackendQueryable* from, const ibBackendQueryable* to)
{
	for (const ibBackendQueryColumn* c : from->GetColumns())
		if (c != nullptr && from->GetProvider().ResolveReferenceTarget(from, c) == to) return c;
	return nullptr;
}

// Resolve a Join node's key columns: explicit, else DERIVED by reference (Join(b) auto-join —
// a referencing column on one side matched to the other's SELF-REFERENCE column).
// Returns false when neither given nor derivable. (docs §22.1)
bool ResolveJoinKeys(const ibQueryNode* join, const ibBackendQueryColumn*& onL, const ibBackendQueryColumn*& onR)
{
	if (join->m_on.m_cross) { onL = nullptr; onR = nullptr; return true; }   // CROSS / ON TRUE — valid keyless join (cartesian)
	onL = join->m_on.m_colL; onR = join->m_on.m_colR;
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
// --- boolean WHERE TREE over a composed (non-co-located) JOIN: a post-compose RAM filter ----------
// A pure AND-of-simple WHERE rides m_conditions and is pushed per leaf; an OR / NOT / IS NULL that spans
// leaves cannot be pushed, so it is evaluated against the materialised joined row here. Only PLAIN
// (non-dot-walk) leaves filter in RAM — a dot-walk leaf would need a further join not in the composed
// table. Each leaf column rides into the composed table via ReferencedColumns below.

// SQL LIKE on a string cell — case-insensitive, '%' = any run, '_' = one char (no escape handling).
bool RamLike(const wxString& s, const wxString& pat)
{
	const wxString S = s.Lower(), P = pat.Lower();
	std::function<bool(size_t, size_t)> m = [&](size_t si, size_t pi) -> bool {
		if (pi == P.size()) return si == S.size();
		if (P[pi] == '%')   return m(si, pi + 1) || (si < S.size() && m(si + 1, pi));
		if (si < S.size() && (P[pi] == '_' || P[pi] == S[si])) return m(si + 1, pi + 1);
		return false;
	};
	return m(0, 0);
}

// SQL three-valued (Kleene) logic: a comparison with a NULL operand is UNKNOWN, and a
// WHERE keeps a row ONLY on a definite TRUE. This is what makes the RAM filter agree with
// the database — e.g. NOT(col = x) DROPS a NULL-col row (it does not keep it), and the same
// settings feed the DB push-down and the RAM fallback alike. See tests/test_queryParity.cpp.
enum class RamTri { False = 0, True = 1, Unknown = 2 };

RamTri RamTriNot(RamTri a)
{
	return a == RamTri::Unknown ? RamTri::Unknown : (a == RamTri::True ? RamTri::False : RamTri::True);
}

// SQL NULL only (TYPE_NULL — what the DB driver yields for a NULL column). NOT TYPE_EMPTY
// (Undefined = composite with no type chosen): that is a runtime placeholder, never a DB null.
bool RamIsNullValue(const ibValue& v)
{
	return v.IsNull();
}

RamTri RamEvalLeaf(const ibQueryCondition& c, const ibQueryRamTable& t, long row)
{
	// A COMPUTED lhs (WHERE Qty * Price > 100, a CASE) evaluates its expression per row; else read the column.
	ibValue cell;
	if (c.m_expr)                cell = EvalColumnExprRow(c.m_expr.get(), t, row);
	else if (c.m_col != nullptr) cell = t.GetCell(row, c.m_col->GetColumnId());
	else                         return RamTri::False;
	// Any NULL operand -> UNKNOWN (the row survives only on a definite TRUE below).
	if (RamIsNullValue(cell) || RamIsNullValue(c.m_value))
		return RamTri::Unknown;
	bool res = false;
	switch (c.m_op) {   // ONE op now (m_comparison + m_explicitOp collapsed into m_op)
		case ibQueryFilterOp::Equal:        res =  cell.CompareValueEQ(c.m_value); break;
		case ibQueryFilterOp::NotEqual:     res = !cell.CompareValueEQ(c.m_value); break;
		case ibQueryFilterOp::Less:         res = cell.CompareValueLS(c.m_value) < 0; break;   // three-way int -> '<'
		case ibQueryFilterOp::LessEqual:    res = cell.CompareValueLE(c.m_value); break;
		case ibQueryFilterOp::Greater:      res = cell.CompareValueGT(c.m_value) > 0; break;   // three-way int -> '>'
		case ibQueryFilterOp::GreaterEqual: res = cell.CompareValueGE(c.m_value); break;
		case ibQueryFilterOp::Like:         res = RamLike(cell.GetString(), c.m_value.GetString()); break;
	}
	return res ? RamTri::True : RamTri::False;
}

RamTri RamEvalPredicate(const ibQueryPredicate* p, const ibQueryRamTable& t, long row)
{
	if (p == nullptr) return RamTri::True;
	switch (p->m_kind) {
		case ibQueryPredicateKind::Leaf:
			return RamEvalLeaf(p->m_leaf, t, row);
		case ibQueryPredicateKind::And: {
			RamTri acc = RamTri::True;                          // FALSE dominates; else UNKNOWN if any
			for (const auto& c : p->m_children) {
				const RamTri r = RamEvalPredicate(c.get(), t, row);
				if (r == RamTri::False)   return RamTri::False;
				if (r == RamTri::Unknown) acc = RamTri::Unknown;
			}
			return acc;
		}
		case ibQueryPredicateKind::Or: {
			RamTri acc = RamTri::False;                         // TRUE dominates; else UNKNOWN if any
			for (const auto& c : p->m_children) {
				const RamTri r = RamEvalPredicate(c.get(), t, row);
				if (r == RamTri::True)    return RamTri::True;
				if (r == RamTri::Unknown) acc = RamTri::Unknown;
			}
			return acc;
		}
		case ibQueryPredicateKind::Not:
			return p->m_children.empty() ? RamTri::True
			                             : RamTriNot(RamEvalPredicate(p->m_children.front().get(), t, row));
		case ibQueryPredicateKind::IsNull: {
			// IS NULL / IS NOT NULL are DEFINITE (never UNKNOWN).
			if (p->m_col == nullptr) return RamTri::False;
			const bool isNull = RamIsNullValue(t.GetCell(row, p->m_col->GetColumnId()));
			return (p->m_negated ? !isNull : isNull) ? RamTri::True : RamTri::False;
		}
	}
	return RamTri::True;
}

// RAM-filterable = every leaf is a PLAIN column (no dot-walk path). A dot-walk leaf needs a join the
// composed table doesn't carry, so that case still errors (kept honest, not silently dropped).
bool PredicateIsRamFilterable(const ibQueryPredicate* p)
{
	if (p == nullptr) return true;
	switch (p->m_kind) {
		case ibQueryPredicateKind::Leaf:   return p->m_leaf.m_path.empty() && p->m_leaf.m_col != nullptr;
		case ibQueryPredicateKind::IsNull: return p->m_path.empty()        && p->m_col != nullptr;
		case ibQueryPredicateKind::And:
		case ibQueryPredicateKind::Or:     for (const auto& c : p->m_children) if (!PredicateIsRamFilterable(c.get())) return false; return true;
		case ibQueryPredicateKind::Not:    return p->m_children.empty() || PredicateIsRamFilterable(p->m_children.front().get());
	}
	return false;
}

void GatherPredicateColumns(const ibQueryPredicate* p, const std::function<void(const ibBackendQueryColumn*)>& add)
{
	if (p == nullptr) return;
	switch (p->m_kind) {
		case ibQueryPredicateKind::Leaf:   add(p->m_leaf.m_col); break;
		case ibQueryPredicateKind::IsNull: add(p->m_col);        break;
		case ibQueryPredicateKind::And:
		case ibQueryPredicateKind::Or:     for (const auto& c : p->m_children) GatherPredicateColumns(c.get(), add); break;
		case ibQueryPredicateKind::Not:    if (!p->m_children.empty()) GatherPredicateColumns(p->m_children.front().get(), add); break;
	}
}

// Keep only the rows that satisfy the boolean predicate (a fresh table — same columns).
ibQueryRamTable RamFilter(const ibQueryRamTable& src, const ibQueryPredicate* pred)
{
	ibQueryRamTable out;
	for (const ibQueryRamColumn& c : src.Columns()) out.AddColumn(c.m_id, c.m_name, c.m_type);
	for (long r = 0; r < src.RowCount(); ++r) {
		if (RamEvalPredicate(pred, src, r) != RamTri::True) continue;
		const long nr = out.AppendRow();
		for (const ibQueryRamColumn& c : src.Columns()) out.SetCell(nr, c.m_id, src.GetCell(r, c.m_id));
	}
	return out;
}

// --- computed output columns (arithmetic / CASE) over a composed (multi-source) JOIN ----------------
// A computed column is SQL-pushed for a single source; over a JOIN the leaves materialise to RAM, so
// the expression is evaluated per joined row here off the column-based AST (Column / Const / Arith /
// Case — the CASE reuses the boolean predicate evaluator). Pure — unit-testable via EvalColumnExpr.
ibValue EvalColumnExprRow(const ibQueryColumnExpr* e, const ibQueryRamTable& t, long row)
{
	if (e == nullptr) return ibValue();
	switch (e->m_kind) {
		case ibQueryColumnExprKind::Column: return e->m_col != nullptr ? t.GetCell(row, e->m_col->GetColumnId()) : ibValue();
		case ibQueryColumnExprKind::Const:  return e->m_const;
		case ibQueryColumnExprKind::Arith: {
			const ibNumber a = EvalColumnExprRow(e->m_lhs.get(), t, row).GetNumber();
			const ibNumber b = EvalColumnExprRow(e->m_rhs.get(), t, row).GetNumber();
			switch (e->m_arith) {
				case ibQueryColumnArithOp::Add: return ibValue(a + b);
				case ibQueryColumnArithOp::Sub: return ibValue(a - b);
				case ibQueryColumnArithOp::Mul: return ibValue(a * b);
				case ibQueryColumnArithOp::Div: return ibValue(b.IsZero() ? ibNumber() : a / b);   // guard /0 -> 0
				case ibQueryColumnArithOp::Mod: return ibValue(b.IsZero() ? ibNumber() : a % b);
			}
			return ibValue();
		}
		case ibQueryColumnExprKind::Case: {
			for (const auto& wt : e->m_cases)
				if (RamEvalPredicate(wt.first.get(), t, row) == RamTri::True) return EvalColumnExprRow(wt.second.get(), t, row);
			return e->m_else ? EvalColumnExprRow(e->m_else.get(), t, row) : ibValue();
		}
	}
	return ibValue();
}

// Every source column an expression reads (so it rides into the composed table).
void GatherColumnExprColumns(const ibQueryColumnExpr* e, const std::function<void(const ibBackendQueryColumn*)>& add)
{
	if (e == nullptr) return;
	switch (e->m_kind) {
		case ibQueryColumnExprKind::Column: add(e->m_col); break;
		case ibQueryColumnExprKind::Const:  break;
		case ibQueryColumnExprKind::Arith:  GatherColumnExprColumns(e->m_lhs.get(), add); GatherColumnExprColumns(e->m_rhs.get(), add); break;
		case ibQueryColumnExprKind::Case:
			for (const auto& wt : e->m_cases) { GatherPredicateColumns(wt.first.get(), add); GatherColumnExprColumns(wt.second.get(), add); }
			GatherColumnExprColumns(e->m_else.get(), add);
			break;
	}
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
	GatherPredicateColumns(spec.m_predicate.get(), add);   // a boolean WHERE-tree leaf rides too (RAM post-filter)
	if (spec.m_selectExprs != nullptr)
		for (const ibQueryColumnSelect& sc : *spec.m_selectExprs) GatherColumnExprColumns(sc.m_expr.get(), add);   // computed cols ride too
	CollectJoinKeys(spec.m_root, cols);
	return cols;
}

// Mutually-recursive with RamUnion (a Union may nest inside a Join subtree and vice-versa).
ibQueryRamTable RamUnion(const ibDataQuerySpec& spec, const ibQueryNode* unionNode,
                         const std::vector<const ibBackendQueryColumn*>& outCols);

// Resolve a column NAME within a subtree's leaves -> the leaf's own same-named column (the first
// found). Lets a UNION branch that is itself a Join / nested Union align its output columns by name,
// just like a Source branch. (docs §22.1b)
const ibBackendQueryColumn* ResolveInSubtree(const ibQueryNode* node, const wxString& name)
{
	if (node == nullptr) return nullptr;
	if (node->m_kind == ibQueryNode::Kind::Source)
		return node->m_queryable != nullptr ? node->m_queryable->ResolveColumnByName(name) : nullptr;
	if (node->m_kind == ibQueryNode::Kind::Join) {
		if (const ibBackendQueryColumn* c = ResolveInSubtree(node->m_left.get(), name)) return c;
		return ResolveInSubtree(node->m_right.get(), name);
	}
	for (const auto& p : node->m_parts)
		if (const ibBackendQueryColumn* c = ResolveInSubtree(p.get(), name)) return c;
	return nullptr;
}

ibQueryRamTable MaterialiseNode(const ibQueryNode* node, const std::vector<const ibBackendQueryColumn*>& refCols,
                                const ibDataQuerySpec& spec, bool condsByName);

// --- Optimizer: smallest-first reorder of a pure-INNER join chain -----------------
// Flatten a maximal subtree of INNER Join nodes into "units" (Source / Union /
// non-inner / cross subtrees stay opaque, keeping their own order — Left/Right/Full
// are order-sensitive) + one resolved ON pair per flattened Join node. Returns false
// on an unresolvable key — the caller keeps the tree order.
bool FlattenInnerChain(const ibQueryNode* node, std::vector<const ibQueryNode*>& units,
                       std::vector<std::pair<const ibBackendQueryColumn*, const ibBackendQueryColumn*>>& keyPairs)
{
	if (node == nullptr) return false;
	if (node->m_kind == ibQueryNode::Kind::Join
		&& node->m_joinKind == ibQueryJoinKind::Inner && !node->m_on.m_cross
		&& node->m_on.m_op == ibJoinCompareOp::Eq && !node->m_on.m_exprL) {   // equi KEY only — the reorder hash-joins on key columns
		const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
		if (!ResolveJoinKeys(node, onL, onR)) return false;
		keyPairs.emplace_back(onL, onR);
		return FlattenInnerChain(node->m_left.get(), units, keyPairs)
		    && FlattenInnerChain(node->m_right.get(), units, keyPairs);
	}
	units.push_back(node);
	return true;
}

// Materialise the units and re-join them smallest-first (PlanInnerJoinOrder over the
// EXACT row counts — every unit is materialised anyway, so the cost input is real, not
// estimated; the intermediates shrink). Inner joins commute/associate, so any connected
// order returns the same row set; output order is unspecified here as everywhere in the
// stitch (ProjectToAliases applies the explicit sorts). Returns false on any anomaly —
// correctness never depends on the reorder, the caller falls back to the tree order.
bool JoinUnitsSmallestFirst(const std::vector<const ibQueryNode*>& units,
                            const std::vector<std::pair<const ibBackendQueryColumn*, const ibBackendQueryColumn*>>& keyPairs,
                            const std::vector<const ibBackendQueryColumn*>& refCols,
                            const ibDataQuerySpec& spec, bool condsByName,
                            ibQueryRamTable& out)
{
	const size_t n = units.size();

	// The needed column set = refCols + every flattened join key (defensive dedup —
	// the callers already collect join keys into refCols).
	std::vector<const ibBackendQueryColumn*> needed = refCols;
	auto addNeeded = [&needed](const ibBackendQueryColumn* c) {
		if (c == nullptr) return;
		for (const ibBackendQueryColumn* k : needed)
			if (k == c) return;
		needed.push_back(c);
	};
	for (const auto& kp : keyPairs) { addNeeded(kp.first); addNeeded(kp.second); }

	// Map every key endpoint to its owning unit -> the edge list for the planner.
	std::vector<std::pair<size_t, size_t>> edges;
	for (const auto& kp : keyPairs) {
		size_t a = n, b = n;
		for (size_t u = 0; u < n; ++u) {
			if (a == n && SubtreeProvides(units[u], kp.first))  a = u;
			if (b == n && SubtreeProvides(units[u], kp.second)) b = u;
		}
		if (a == n || b == n || a == b) return false;
		edges.emplace_back(a, b);
	}

	// Materialise every unit — the exact cost input.
	std::vector<ibQueryRamTable> tables;
	tables.reserve(n);
	std::vector<long> counts;
	for (const ibQueryNode* u : units) {
		tables.push_back(MaterialiseNode(u, needed, spec, condsByName));
		counts.push_back(tables.back().RowCount());
	}

	const std::vector<size_t> order = ibQueryComposer::PlanInnerJoinOrder(counts, edges);
	if (order.size() != n) return false;   // disconnected — tree order

	std::vector<bool> inAccum(n, false);
	inAccum[order[0]] = true;
	out = std::move(tables[order[0]]);

	for (size_t step = 1; step < n; ++step) {
		const size_t next = order[step];

		// The connecting edge: one endpoint already in the accumulated side, the other on `next`.
		const ibBackendQueryColumn* onAccum = nullptr; const ibBackendQueryColumn* onNext = nullptr;
		for (size_t e = 0; e < edges.size(); ++e) {
			if (inAccum[edges[e].first]  && edges[e].second == next) { onAccum = keyPairs[e].first;  onNext = keyPairs[e].second; break; }
			if (inAccum[edges[e].second] && edges[e].first  == next) { onAccum = keyPairs[e].second; onNext = keyPairs[e].first;  break; }
		}
		if (onAccum == nullptr || onNext == nullptr) return false;

		// This step's output: every needed column the accumulated side or `next` provides.
		std::vector<const ibBackendQueryColumn*> outCols;
		std::vector<bool> fromLeft;
		for (const ibBackendQueryColumn* c : needed) {
			bool left = false, right = false;
			for (size_t u = 0; u < n; ++u) {
				if (!SubtreeProvides(units[u], c)) continue;
				if (inAccum[u])      left = true;
				else if (u == next)  right = true;
			}
			if (left)       { outCols.push_back(c); fromLeft.push_back(true);  }
			else if (right) { outCols.push_back(c); fromLeft.push_back(false); }
		}

		out = ibQueryComposer::JoinRamTables(out, tables[next], onAccum, onNext, outCols, fromLeft, ibQueryJoinKind::Inner);
		inAccum[next] = true;
	}
	return true;
}

// Materialise ANY node to a RAM table keyed by GetColumnId, carrying the referenced columns its leaves
// provide. Source -> read the leaf; Join -> recurse + nested-loop; UNION -> RamUnion (a union nested
// inside a join). N-way left-deep falls out of the recursion. `condsByName` routes leaf conditions by
// NAME (a UNION branch) vs by column OWNERSHIP (a plain join). (§22.1)
ibQueryRamTable MaterialiseNode(const ibQueryNode* node, const std::vector<const ibBackendQueryColumn*>& refCols,
                                const ibDataQuerySpec& spec, bool condsByName = false)
{
	if (node->m_kind == ibQueryNode::Kind::Union)
		return RamUnion(spec, node, refCols);   // a UNION nested inside a JOIN subtree

	if (node->m_kind == ibQueryNode::Kind::Source) {
		std::vector<const ibBackendQueryColumn*> mine;
		for (const ibBackendQueryColumn* c : refCols)
			if (node->m_queryable->OwnsColumn(c)) mine.push_back(c);
		const std::vector<ibQueryCondition> conds = condsByName
			? LeafConditionsByName(spec, node->m_queryable)
			: LeafConditions(spec, node->m_queryable);
		return MaterialiseLeaf(node->m_queryable, spec.m_holder, conds, mine);
	}

	// Optimizer: a pure-INNER chain of 3+ units re-joins smallest-first with the EXACT
	// materialised row counts (PlanInnerJoinOrder) — smaller intermediates for free.
	// Any anomaly falls through to the tree order below; correctness never depends on it.
	if (node->m_joinKind == ibQueryJoinKind::Inner && !node->m_on.m_cross) {
		std::vector<const ibQueryNode*> units;
		std::vector<std::pair<const ibBackendQueryColumn*, const ibBackendQueryColumn*>> keyPairs;
		if (FlattenInnerChain(node, units, keyPairs) && units.size() >= 3) {
			ibQueryRamTable reordered;
			if (JoinUnitsSmallestFirst(units, keyPairs, refCols, spec, condsByName, reordered))
				return reordered;
		}
	}

	// Join: recurse, then nested-loop. Output = refCols this subtree provides.
	const ibQueryRamTable TL = MaterialiseNode(node->m_left.get(),  refCols, spec, condsByName);
	const ibQueryRamTable TR = MaterialiseNode(node->m_right.get(), refCols, spec, condsByName);

	std::vector<const ibBackendQueryColumn*> outCols;     // provided cols, with their side
	std::vector<bool> fromLeft;
	for (const ibBackendQueryColumn* c : refCols) {
		if      (SubtreeProvides(node->m_left.get(),  c)) { outCols.push_back(c); fromLeft.push_back(true);  }
		else if (SubtreeProvides(node->m_right.get(), c)) { outCols.push_back(c); fromLeft.push_back(false); }
	}

	const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
	ResolveJoinKeys(node, onL, onR);   // explicit or derived (the tree was validated resolvable)

	return ibQueryComposer::JoinRamTables(TL, TR, onL, onR, outCols, fromLeft, node->m_joinKind, node->m_on);
}

// Finalise a GetColumnId-keyed combined table into the result: ORDER BY (RAM stable
// sort over m_sorts, each key read by GetColumnId), project the select-list (alias-keyed
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
				const int c = ibQueryComposer::RamSortCompareKey(va, vb, s.m_ascending);
				if (c != 0) return c < 0;
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
	// COMPUTED output columns (arithmetic / CASE over the JOIN) — evaluated per row off the AST below.
	std::vector<ibMetaID> exprIds;
	const std::vector<ibQueryColumnSelect> noExprs;
	const std::vector<ibQueryColumnSelect>& exprs = spec.m_selectExprs != nullptr ? *spec.m_selectExprs : noExprs;
	for (const ibQueryColumnSelect& sc : exprs) {
		TO.AddColumn(seq, sc.m_alias, ibTypeDescription());
		exprIds.push_back(seq++);
	}
	// SELECT DISTINCT over the RAM stitch — dedup by the FULL output row (selectCols + computed exprs),
	// first occurrence wins, BEFORE the page limit so it yields up to `limit` DISTINCT rows. The single-DB
	// path renders SQL DISTINCT; the multi-source stitch has none, so it folds here. (UNION DISTINCT is a
	// separate fold at the UNION operator; this is DISTINCT over a JOIN / computed compose.)
	const long limit = (page.m_count > 0) ? page.m_count : rows;
	std::set<wxString> seenDistinct;
	long emitted = 0;
	for (long oi = 0; oi < rows && emitted < limit; ++oi) {
		const long i = order[static_cast<size_t>(oi)];
		std::vector<ibValue> outCells;
		outCells.reserve(spec.m_selectCols->size() + exprs.size());
		for (const auto& sc : *spec.m_selectCols)
			outCells.push_back(RamCell(TC, i, sc.first));
		for (const ibQueryColumnSelect& sc : exprs)
			outCells.push_back(EvalColumnExprRow(sc.m_expr.get(), TC, i));
		if (spec.m_distinct) {
			wxString key;
			for (const ibValue& v : outCells) key += v.GetHashKey() + wxT("\x1f");
			if (!seenDistinct.insert(key).second) continue;   // duplicate output row -> drop
		}
		const long r = TO.AppendRow();
		for (size_t k = 0; k < outIds.size();  ++k) TO.SetCell(r, outIds[k],  outCells[k]);
		for (size_t k = 0; k < exprIds.size(); ++k) TO.SetCell(r, exprIds[k], outCells[outIds.size() + k]);
		++emitted;
	}
	return ibDataQueryResult(std::move(TO), spec.m_queryable);
}

// RAM UNION — vertical stack: each branch (a Source, OR a nested Join / Union subtree) supplies the
// shared output columns BY NAME (heterogeneous branches, e.g. catalog ∪ temp ∪ a join), rows
// concatenate. A branch missing an output column yields NULL for it. Every GLOBAL Where condition is
// applied to EVERY branch BY NAME (condsByName) — a branch missing the named column drops that one
// condition. (A condition scoped to ONE branch needs the door to carry an alias-qualified Where — a
// door/spec surface feature, not this engine.) (docs §22.1b)
ibQueryRamTable RamUnion(const ibDataQuerySpec& spec, const ibQueryNode* unionNode,
                         const std::vector<const ibBackendQueryColumn*>& outCols)
{
	ibQueryRamTable TO;
	for (const ibBackendQueryColumn* c : outCols)
		TO.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());

	for (size_t pi = 0; pi < unionNode->m_parts.size(); ++pi) {
		const ibQueryNode* part = unionNode->m_parts[pi].get();
		if (part == nullptr) continue;

		// Resolve each output column to THIS branch's same-named column (in its leaves — a Join /
		// nested-Union branch resolves through ResolveInSubtree, a Source branch through its queryable).
		std::vector<const ibBackendQueryColumn*> branchCols;     // per outCol (null = absent here)
		std::vector<const ibBackendQueryColumn*> refForBranch;   // the present ones — what to materialise
		for (const ibBackendQueryColumn* c : outCols) {
			const ibBackendQueryColumn* bc = ResolveInSubtree(part, c->GetName());
			branchCols.push_back(bc);
			if (bc != nullptr) refForBranch.push_back(bc);
		}
		CollectJoinKeys(part, refForBranch);   // a Join branch must also carry its own join keys (else JoinRamTables can't match)

		// Materialise the whole branch subtree (Source / Join / nested Union), conditions BY NAME.
		const ibQueryRamTable TP = MaterialiseNode(part, refForBranch, spec, /*condsByName*/true);
		ibQueryComposer::AppendUnionBranch(TO, TP, outCols, branchCols);

		// Plain UNION (not ALL) dedupes the ACCUMULATED rows at its operator — SQL left-assoc
		// semantics (A UNION B UNION ALL C dedupes after B, keeps C's duplicates). A missing
		// flag (a node built before the flag existed) reads as ALL — the historic behaviour.
		const bool keepDups = pi >= unionNode->m_partAll.size() || unionNode->m_partAll[pi];
		if (pi > 0 && !keepDups)
			TO = ibQueryComposer::DedupeRows(TO, outCols);
	}
	return TO;
}

// Every Join node's keys resolvable? (explicit columns, OR derived from a reference
// between two Source leaves — Join(b) auto-join).
bool AllJoinsHaveKeys(const ibQueryNode* node)
{
	if (node == nullptr || node->m_kind != ibQueryNode::Kind::Join) return true;
	// A computed-ON join (a.x+1 <op> b.y) has no key columns but is a valid theta join (RAM nested-loop).
	if (node->m_on.m_exprL && node->m_on.m_exprR)
		return AllJoinsHaveKeys(node->m_left.get()) && AllJoinsHaveKeys(node->m_right.get());
	const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
	return ResolveJoinKeys(node, onL, onR) &&
	       AllJoinsHaveKeys(node->m_left.get()) && AllJoinsHaveKeys(node->m_right.get());
}

// Build the GetColumnId-keyed combined table for a multi-source tree carrying `refCols`.
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

// One aggregate over the rows of a bucket (column read by GetColumnId). COUNT and a null
// column => row count; SUM/AVG over the number value; MIN/MAX by ibValue compare.
ibValue AggregateOne(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRamTable& TC, const std::vector<long>& idx)
{
	using Fn = ibDataQueryBuilder::AggregateFn;
	// COUNT(*) — no source column AND no computed input — counts every row in the bucket.
	if (a.m_col == nullptr && !a.m_expr)
		return ibValue(ibNumber(static_cast<long>(idx.size())));

	// Every column aggregate IGNORES NULL operands (SQL semantics): COUNT(col) counts
	// non-null rows, SUM/AVG fold only non-null (AVG divides by the non-null count),
	// MIN/MAX skip NULL. A NULL row no longer inflates the result or wins MIN.
	ibNumber sum(0); long n = 0; ibValue best; bool have = false;
	for (long i : idx) {
		// A COMPUTED aggregate input (SUM(Qty * Price), MAX(CASE …)) evaluates its expression per row;
		// a plain input reads its source column. Same RAM expr evaluator the JOIN stitch uses.
		const ibValue v = a.m_expr ? EvalColumnExprRow(a.m_expr.get(), TC, i) : RamCell(TC, i, a.m_col);
		if (RamIsNullValue(v))
			continue;
		switch (a.m_fn) {
		case Fn::Count:             ++n; break;
		case Fn::Sum: case Fn::Avg: sum = sum + v.GetNumber(); ++n; break;
		case Fn::Min: if (!have || v < best) { best = v; have = true; } break;
		case Fn::Max: if (!have || v > best) { best = v; have = true; } break;
		default: break;
		}
	}
	switch (a.m_fn) {
	case Fn::Count: return ibValue(ibNumber(static_cast<long>(n)));
	case Fn::Sum: return ibValue(sum);
	case Fn::Avg: return n > 0 ? ibValue(sum / ibNumber(static_cast<long>(n))) : ibValue();
	case Fn::Min: case Fn::Max: return best;
	default: return ibValue();
	}
}

// Receiver id for a COUNT(*) aggregate (no source column): a synthetic id far from any real metaID,
// keyed by the aggregate's position, read back by GetColumn(alias). Column aggregates roll in-place.
const ibMetaID kAggSyntheticBase = 0x40000000u;

// Roll the aggregates over `rows` and write each onto the node: a COLUMN aggregate IN-PLACE into its
// own column (a.m_col) — so it reads as the row value on a leaf and the SUBTOTAL on a group node
// (GetValue(col)); a COUNT(*) into its synthetic receiver (read by GetColumn(alias)).
void ApplyAggregates(ibSelectorTree::Node& node, const ibQueryRamTable& TC, const std::vector<long>& rows,
                     const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i) {
		const ibDataQueryBuilder::AggregateItem& a = aggs[i];
		const ibMetaID id = (a.m_col != nullptr) ? a.m_col->GetColumnId() : (kAggSyntheticBase + static_cast<ibMetaID>(i));
		node.m_values[id] = AggregateOne(a, TC, rows);
	}
}

// Add the synthetic receiver COLUMNS for COUNT(*) aggregates to a tree (column aggregates need none —
// their own column is already present). Read back by GetColumn(alias).
void AddSyntheticAggColumns(ibSelectorTree& tree, const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i)
		if (aggs[i].m_col == nullptr)
			tree.AddColumn(kAggSyntheticBase + static_cast<ibMetaID>(i), aggs[i].m_alias, ibTypeDescription());
}

// Identity key of a cell for hierarchy linking. A REFERENCE keys by its _RRRef bytes (NOT its display
// string): a row's OWN data-reference and a parent-ref pointing AT it carry the same _RRRef, so the
// child's parentKey matches the parent's rowKey on identity, not on name. Any other value keys by its
// string. (docs/query-language-arc.md §22.1b)
wxString CellKey(const ibValue& v)
{
	// Stable identity key for grouping / hierarchy linking — the value's own canonical key (a reference
	// keys by guid, anything else by string). ibValue::GetHashKey owns this, so the composer names no
	// runtime type and the key is identity-stable (not an object address). Works for non-metadata
	// sources (temp tables, plain columns) too.
	return v.GetHashKey();
}

// Context for the recursive hierarchy fold (invariant across the recursion).
struct HierBuildCtx {
	const ibQueryRamTable*                               detail;
	const ibBackendQueryColumn*                          rowKeyCol;
	const std::map<wxString, std::vector<long>>*         childrenOf;
	const std::vector<ibDataQueryBuilder::AggregateItem>* aggregates;
	ibDimensionKind                                      mode;
	std::vector<bool>*                                  visited;   // cycle guard (malformed parent-ref)
};

// Attach the subtree rooted at detail-row `rowIdx` under `parent`; returns the subtree's detail-row
// indices (for the parent's subtotal). Sets the node's values (the row copied), m_hasChildren (from
// the data — its key appears as a parent), and a subtotal rolled over the WHOLE subtree (aggregated
// from raw leaves -> COUNT/AVG correct). HierarchyOnly omits a leaf element but still counts it.
std::vector<long> AttachHierNode(const HierBuildCtx& ctx, ibSelectorTree::Node* parent, long rowIdx, int level)
{
	if (rowIdx < 0 || rowIdx >= static_cast<long>(ctx.visited->size()) || (*ctx.visited)[static_cast<size_t>(rowIdx)])
		return {};                                   // cycle / out of range -> stop
	(*ctx.visited)[static_cast<size_t>(rowIdx)] = true;

	const wxString key = CellKey(ctx.detail->GetCell(rowIdx, ctx.rowKeyCol->GetColumnId()));
	const auto cit = ctx.childrenOf->find(key);
	const bool hasKids = (cit != ctx.childrenOf->end() && !cit->second.empty());

	if (ctx.mode == ibDimensionKind::HierarchyOnly && !hasKids)
		return { rowIdx };                           // a leaf element: not a node, but counts upward

	ibSelectorTree::Node* node = parent->AddChild(level);
	for (const ibQueryRamColumn& col : ctx.detail->Columns())
		node->m_values[col.m_id] = ctx.detail->GetCell(rowIdx, col.m_id);
	node->m_hasChildren = hasKids;

	std::vector<long> subtreeRows = { rowIdx };
	if (hasKids)
		for (long childIdx : cit->second) {
			std::vector<long> kids = AttachHierNode(ctx, node, childIdx, level + 1);
			subtreeRows.insert(subtreeRows.end(), kids.begin(), kids.end());
		}
	ApplyAggregates(*node, *ctx.detail, subtreeRows, *ctx.aggregates);
	return subtreeRows;
}

// Does a bucket satisfy every HAVING condition? Each HavingItem compares its aggregate (fn over col,
// COUNT(*) when col is null) to a value. Used to drop a group in the RAM fold — the computed source's
// register can't apply HAVING, so the fold does (a SUM > N group filter is core reporting).
bool PassesHaving(const std::vector<ibDataQueryBuilder::HavingItem>& having,
                  const ibQueryRamTable& TC, const std::vector<long>& idx)
{
	for (const ibDataQueryBuilder::HavingItem& h : having) {
		ibDataQueryBuilder::AggregateItem ai;                 // m_alias / m_path / m_expr default-empty
		ai.m_fn = h.m_fn; ai.m_col = h.m_col;
		const ibValue agg = AggregateOne(ai, TC, idx);
		bool ok = true;
		switch (h.m_op) {
		case ibQueryFilterOp::Greater:      ok =  (agg >  h.m_value); break;
		case ibQueryFilterOp::GreaterEqual: ok = !(agg <  h.m_value); break;
		case ibQueryFilterOp::Less:         ok =  (agg <  h.m_value); break;
		case ibQueryFilterOp::LessEqual:    ok = !(agg >  h.m_value); break;
		case ibQueryFilterOp::Equal:        ok =  (agg == h.m_value); break;
		case ibQueryFilterOp::NotEqual:     ok = !(agg == h.m_value); break;
		default:                            ok = true;                break;
		}
		if (!ok) return false;
	}
	return true;
}

// RAM GROUP BY over a composed combined table: bucket rows by the group keys, fold the
// aggregates per bucket. Group columns ride keyed by GetColumnId (read via GetValue);
// aggregate columns are named by their alias (read via GetColumn(alias)).
ibDataQueryResult RamAggregate(const ibQueryRamTable& TC, const ibDataQuerySpec& spec)
{
	const long rows = TC.RowCount();

	std::vector<wxString> keyOrder;                    // first-seen group order
	std::map<wxString, std::vector<long>> buckets;
	for (long i = 0; i < rows; ++i) {
		wxString key;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			key += RamCell(TC, i, g).GetHashKey() + wxT("\x1f");   // identity key — two refs with the same name don't merge
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
		TO.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
	const ibMetaID aggBaseId = 0x40000000u;             // far from any metaID — aggregates read by alias NAME
	{
		ibMetaID aggId = aggBaseId;
		for (const auto& a : *spec.m_aggregates)
			TO.AddColumn(aggId++, a.m_alias, a.m_col != nullptr ? a.m_col->GetTypeDesc() : ibTypeDescription());
	}

	for (const wxString& key : keyOrder) {
		if (spec.m_topCount > 0 && TO.RowCount() >= spec.m_topCount)
			break;   // SELECT TOP n + GROUP BY — cap the folded groups (first-seen order)
		const std::vector<long>& idx = buckets[key];
		if (spec.m_having != nullptr && !PassesHaving(*spec.m_having, TC, idx))
			continue;   // HAVING drops this group (the register can't apply it; the RAM fold does)
		const long r = TO.AppendRow();
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			TO.SetCell(r, g->GetColumnId(), RamCell(TC, idx.front(), g));
		ibMetaID aId = aggBaseId;
		for (const auto& a : *spec.m_aggregates)
			TO.SetCell(r, aId++, AggregateOne(a, TC, idx));
	}
	return ibDataQueryResult(std::move(TO), spec.m_queryable);
}

// Multi-level totals fold (hierarchical totals): a node holds the aggregates over its rows; recurse a
// subtotal node per distinct value of groupFields[level], carrying the group-key path
// down. Root (level 0) = grand total. (docs/query-language-arc.md §22.1b)
void FoldTotals(ibSelectorTree::Node& node, const ibQueryRamTable& TC, const std::vector<long>& rows,
                const std::vector<const ibBackendQueryColumn*>& groupFields,
                const std::vector<ibDataQueryBuilder::AggregateItem>& aggs, size_t level,
                const std::map<ibMetaID, ibValue>& keys)
{
	node.m_values = keys;
	ApplyAggregates(node, TC, rows, aggs);   // subtotals IN-PLACE in each aggregate's own column
	if (level >= groupFields.size())
		return;

	std::vector<wxString> order;
	std::map<wxString, std::vector<long>> buckets;
	std::map<wxString, ibValue> keyVal;
	for (long i : rows) {
		const ibValue v = RamCell(TC, i, groupFields[level]);
		const wxString k = v.GetHashKey();   // identity key — refs group by guid, not display name
		if (buckets.find(k) == buckets.end()) { order.push_back(k); keyVal[k] = v; }
		buckets[k].push_back(i);
	}
	for (const wxString& k : order) {
		ibSelectorTree::Node* child = node.AddChild(static_cast<int>(level) + 1);
		std::map<ibMetaID, ibValue> childKeys = keys;
		childKeys[groupFields[level]->GetColumnId()] = keyVal[k];
		FoldTotals(*child, TC, buckets[k], groupFields, aggs, level + 1, childKeys);
	}
	node.m_hasChildren = !node.m_children.empty();   // eager build -> has children == populated
}

// Promote a (computed-leaf ⋈ DB-leaf) join to server-side: materialise the COMPUTED leaf (register
// slice / balance / subquery / temp) into a DB temp table, then the join is DB⋈DB and the co-located
// fast path runs it in ONE SQL — instead of materialising BOTH leaves to RAM and stitching in C++.
// The computed leaf's columns are REMAPPED onto the temp table's same-named raw columns (conditions /
// sorts / select list / join key). Emits the rebuilt query state into the caller's buffers (which must
// outlive the spec2 use) and returns the manager (keeps the temp table alive across the read); null =
// not this shape / no temp capability / a runtime failure / an unresolved remap -> the caller stays on
// the RAM composer. (docs/temp-db.md §8 — the server-side push-down)
std::unique_ptr<ibTempTableManager> PromoteComputedLeaf(
	const ibDataQuerySpec& spec,
	std::shared_ptr<ibQueryNode>& outRoot,
	std::vector<ibQueryCondition>& outConds,
	std::vector<ibQuerySortItem>& outSorts,
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>>& outSelects,
	std::vector<const ibBackendQueryColumn*>& outGroupBy,
	std::vector<ibDataQueryBuilder::AggregateItem>& outAggregates,
	std::vector<ibDataQueryBuilder::HavingItem>& outHaving,
	const ibBackendQueryable*& outPrimary)
{
	const ibQueryNode* root = spec.m_root;
	if (root == nullptr || root->m_kind != ibQueryNode::Kind::Join)
		return nullptr;
	const ibQueryNode* nL = root->m_left.get();
	const ibQueryNode* nR = root->m_right.get();
	if (nL == nullptr || nR == nullptr ||
	    nL->m_kind != ibQueryNode::Kind::Source || nR->m_kind != ibQueryNode::Kind::Source)
		return nullptr;
	const ibBackendQueryable* qL = nL->m_queryable;
	const ibBackendQueryable* qR = nR->m_queryable;
	if (qL == nullptr || qR == nullptr)                                  return nullptr;
	if (root->m_joinKind != ibQueryJoinKind::Inner)                      return nullptr;
	if (root->m_on.m_colL == nullptr || root->m_on.m_colR == nullptr)         return nullptr;   // explicit keys only
	if (!spec.m_dotWalks->empty() || !spec.m_keyIn->empty())             return nullptr;

	// Exactly ONE computed leaf + one real DB leaf (both-DB is the plain co-located path; both-computed
	// is out of scope — neither is SQL-joinable).
	const bool lRam = qL->IsComputedInRam();
	const bool rRam = qR->IsComputedInRam();
	if (lRam == rRam)
		return nullptr;
	const ibBackendQueryable* computed = lRam ? qL : qR;

	// (The temp now stores columns in the metadata storage format — WriteFieldsOf spread filled via
	// SetValueColumn — so reference / enum / variant values round-trip from a temp exactly like from a
	// real table, for KEYS and OUTPUTS alike. No output-type guard needed.)

	// Materialise the computed leaf (its ctor filters are baked in; the door conditions IT owns are
	// pushed down as compute filters) into a DB temp table.
	std::vector<ibQueryCondition> computedConds;
	for (const ibQueryCondition& c : *spec.m_conditions)
		if (c.m_col != nullptr && computed->OwnsColumn(c.m_col)) computedConds.push_back(c);
	ibQueryRamTable rows = computed->ComputeRows(computedConds);

	// SHOULD-gate (temp-db.md §7) with the exact materialised count; CAN lives in Materialise.
	if (!WorthDbTemp(rows.RowCount()))
		return nullptr;

	std::unique_ptr<ibTempTableManager> mgr =
		ibTempTableManager::Materialise(spec.m_holder, rows, computed->GetMetaData());
	if (!mgr)
		return nullptr;
	const ibDbTempTableQueryable* temp = mgr->Queryable();

	// Remap a referenced column: a computed-leaf column -> the temp queryable's same-named raw column;
	// a DB-leaf column unchanged. A computed column with no temp counterpart fails the promote (-> RAM).
	bool remapOk = true;
	auto remap = [&](const ibBackendQueryColumn* col) -> const ibBackendQueryColumn* {
		if (col == nullptr)               return nullptr;
		if (!computed->OwnsColumn(col))   return col;
		const ibBackendQueryColumn* t = temp->ResolveColumnByName(col->GetName());
		if (t == nullptr) remapOk = false;
		return t;
	};

	outConds.clear();
	for (const ibQueryCondition& c : *spec.m_conditions) { ibQueryCondition nc = c; nc.m_col = remap(c.m_col); outConds.push_back(nc); }
	outSorts.clear();
	for (const ibQuerySortItem& s : *spec.m_sorts)       { ibQuerySortItem ns = s; ns.m_col = remap(s.m_col); outSorts.push_back(ns); }
	outSelects.clear();
	for (const auto& sc : *spec.m_selectCols)            outSelects.push_back({ remap(sc.first), sc.second });

	// Aggregate terminal columns too (empty for a plain read) — so the same promote serves the
	// computed⋈DB totals path (the aggregate caller wires spec2 at these; the read caller ignores them).
	outGroupBy.clear();
	for (const ibBackendQueryColumn* g : *spec.m_groupBy)          outGroupBy.push_back(remap(g));
	outAggregates.clear();
	for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) { ibDataQueryBuilder::AggregateItem na = a; na.m_col = remap(a.m_col); outAggregates.push_back(na); }
	outHaving.clear();
	for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having)        { ibDataQueryBuilder::HavingItem nh = h; nh.m_col = remap(h.m_col); outHaving.push_back(nh); }

	// Swapped tree: the computed Source node -> a temp Source node; the DB node stays; keys remapped.
	std::shared_ptr<ibQueryNode> tempNode = ibQueryNode::Source(temp);
	outRoot = std::make_shared<ibQueryNode>();
	outRoot->m_kind     = ibQueryNode::Kind::Join;
	outRoot->m_joinKind = ibQueryJoinKind::Inner;
	outRoot->m_left     = lRam ? tempNode : root->m_left;
	outRoot->m_right    = rRam ? tempNode : root->m_right;
	outRoot->m_on.m_colL   = remap(root->m_on.m_colL);
	outRoot->m_on.m_colR  = remap(root->m_on.m_colR);
	outPrimary = (spec.m_queryable == computed) ? static_cast<const ibBackendQueryable*>(temp) : spec.m_queryable;

	if (!remapOk)
		return nullptr;   // a referenced computed column had no temp counterpart -> RAM (mgr drops here)
	return mgr;
}

// Promote the COMPUTED branches of a UNION to DB temp tables, so the whole union runs server-side. A
// UNION resolves each branch's columns BY NAME (the temp's columns keep the source names), so no
// remap is needed — only the branch's queryable is swapped for its temp. Returns the managers (keep
// the temps alive) + the rebuilt union root in `outRoot`; an EMPTY vector means "not promoted" (no
// computed branch, an unsupported shape, a small set, or no temp capability) -> the caller stays on
// RamUnion. (docs/temp-db.md)
std::vector<std::unique_ptr<ibTempTableManager>> PromoteUnionBranches(const ibDataQuerySpec& spec,
                                                                      std::shared_ptr<ibQueryNode>& outRoot)
{
	std::vector<std::unique_ptr<ibTempTableManager>> mgrs;
	const ibQueryNode* root = spec.m_root;
	if (root == nullptr || root->m_kind != ibQueryNode::Kind::Union || root->m_parts.empty())
		return mgrs;

	std::shared_ptr<ibQueryNode> newRoot = std::make_shared<ibQueryNode>();
	newRoot->m_kind = ibQueryNode::Kind::Union;
	newRoot->m_partAll = root->m_partAll;   // UNION-vs-ALL flags ride with the rebuilt parts
	bool anyComputed = false;
	for (const auto& part : root->m_parts) {
		const ibQueryNode* p = part.get();
		if (p == nullptr || p->m_kind != ibQueryNode::Kind::Source || p->m_queryable == nullptr)
			return {};   // unsupported branch shape -> RAM
		if (p->m_queryable->IsComputedInRam()) {
			anyComputed = true;
			ibQueryRamTable rows = p->m_queryable->ComputeRows(std::vector<ibQueryCondition>{});   // ctor filters baked in
			if (!WorthDbTemp(rows.RowCount())) return {};   // SHOULD-gate: small set -> RAM
			std::unique_ptr<ibTempTableManager> mgr =
				ibTempTableManager::Materialise(spec.m_holder, rows, p->m_queryable->GetMetaData());
			if (!mgr) return {};   // no temp capability / failure -> RAM
			newRoot->m_parts.push_back(ibQueryNode::Source(mgr->Queryable()));
			mgrs.push_back(std::move(mgr));
		}
		else {
			newRoot->m_parts.push_back(part);   // a real DB branch — unchanged
		}
	}
	if (!anyComputed)
		return {};   // all branches already DB -> the plain CanColocateUnion path handles it
	outRoot = newRoot;
	return mgrs;
}

// Compose a multi-source tree, in RAM. JOIN of any depth (N-way left-deep) on explicit
// columns, and UNION of Source branches. Auto-join-by-reference is the remaining shape.
ibDataQueryResult ComposeMultiSource(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
{
	const ibQueryNode* root = spec.m_root;
	if (root == nullptr)
		RamCompositionNotYet();

	// Fast path: a 2-leaf inner join of two real DB tables on scalar keys, scalar outputs — run
	// the WHOLE join + cross-table filter in ONE server-side SELECT (the DBMS does the work, only
	// the projected scalars transit). Anything outside this shape falls through to the RAM compose
	// below (materialise each leaf, stitch in C++). (docs/query-language-arc.md §22.1a)
	if (ibDbTableProvider::CanColocateJoin(spec))
		return ibDbTableProvider::ExecuteColocatedJoin(spec, page);

	// (computed ⋈ DB): materialise the computed leaf into a DB temp table, remap its columns onto the
	// temp, and run the now-DB⋈DB join SERVER-SIDE. The buffers + manager live for this block, so the
	// temp table is alive across ExecuteColocatedJoin (whose result is RAM-backed); on any miss the
	// manager drops and we fall through to the RAM compose. (docs/temp-db.md §8)
	{
		std::shared_ptr<ibQueryNode>  pRoot;
		std::vector<ibQueryCondition> pConds;
		std::vector<ibQuerySortItem>  pSorts;
		std::vector<std::pair<const ibBackendQueryColumn*, wxString>> pSelects;
		std::vector<const ibBackendQueryColumn*>            pGroupBy;    // empty for a read — unused here
		std::vector<ibDataQueryBuilder::AggregateItem>      pAggs;
		std::vector<ibDataQueryBuilder::HavingItem>         pHaving;
		const ibBackendQueryable*     pPrimary = nullptr;
		if (std::unique_ptr<ibTempTableManager> mgr =
		        PromoteComputedLeaf(spec, pRoot, pConds, pSorts, pSelects, pGroupBy, pAggs, pHaving, pPrimary)) {
			ibDataQuerySpec spec2 = spec;
			spec2.m_root       = pRoot.get();
			spec2.m_conditions = &pConds;
			spec2.m_sorts      = &pSorts;
			spec2.m_selectCols = &pSelects;
			spec2.m_queryable  = pPrimary;
			if (ibDbTableProvider::CanColocateJoin(spec2))
				return ibDbTableProvider::ExecuteColocatedJoin(spec2, page);
			// not co-locatable after the swap -> mgr drops the temp, fall through to RAM
		}
	}

	// UNION of real DB tables -> one server-side UNION ALL (scalar outputs). Else the RAM stack.
	if (ibDbTableProvider::CanColocateUnion(spec))
		return ibDbTableProvider::ExecuteColocatedUnion(spec, page);

	// Mixed UNION (some branch computed): materialise the computed branches to DB temp tables and run
	// the whole union server-side. The managers (temps) live for this block, across ExecuteColocatedUnion
	// (whose result is RAM-backed); on a miss they drop and we fall to RamUnion.
	if (root->m_kind == ibQueryNode::Kind::Union) {
		std::shared_ptr<ibQueryNode> uRoot;
		std::vector<std::unique_ptr<ibTempTableManager>> uMgrs = PromoteUnionBranches(spec, uRoot);
		if (!uMgrs.empty() && uRoot) {
			ibDataQuerySpec spec2 = spec;
			spec2.m_root = uRoot.get();
			if (ibDbTableProvider::CanColocateUnion(spec2))
				return ibDbTableProvider::ExecuteColocatedUnion(spec2, page);
		}
	}

	const bool ok = (root->m_kind == ibQueryNode::Kind::Union && !root->m_parts.empty()) ||
	                (root->m_kind == ibQueryNode::Kind::Join && AllJoinsHaveKeys(root));
	if (!ok)
		RamCompositionNotYet();

	// A boolean WHERE TREE (OR / NOT / IS NULL across leaves) cannot be pushed per leaf (each leaf
	// materialises with its OWN flat conditions), so it is evaluated as a POST-COMPOSE RAM filter over
	// the joined rows (its leaf columns ride into the composed table via ReferencedColumns). Only a
	// dot-walk leaf inside such a tree still errors — it needs a join the composed table doesn't carry.
	if (spec.m_predicate && !PredicateIsRamFilterable(spec.m_predicate.get()))
		throw std::logic_error("ibQueryComposer: a DOT-WALK leaf inside a boolean WHERE (OR/NOT/IN/IS NULL) "
		                       "across a non-co-located JOIN is not yet supported (keep the dot-walk filter "
		                       "to an AND-of-comparisons, or use a co-locatable join)");

	ibQueryRamTable composed = Compose(spec, ReferencedColumns(spec));
	if (spec.m_predicate)
		composed = RamFilter(composed, spec.m_predicate.get());
	return ProjectToAliases(composed, spec, page);
}

// Promote a SINGLE computed source (register slice / balance / subquery) to SERVER-SIDE: materialise its
// rows into a DB temp table, remap every column reference onto the temp's same-named columns, and run the
// query over the temp — a REAL DB table (ibDbTempTableQueryable inherits the DB provider) — as ordinary
// server-side SQL (WHERE / GROUP BY / SUM(expr) / HAVING). A reference column travels as its ibReference
// blob (the temp stores the metadata spread); grouping / filtering BY the reference needs NO decomposition
// (the aggregate GROUPs by the full spread, the read side reconstructs via GetValue). A DOT-WALK THROUGH a
// reference (Balance.Item.Name) is promoted too: the path's FIRST segment (the reference the computed source
// owns) maps to the temp, the deeper segments stay on their target catalogs, and the temp — a real DB source
// — auto-joins them SERVER-SIDE through the ordinary dot-walk join chain (ibRefJoinChain). Null = not computed
// / no connection / not worth a temp / no temp capability / an unresolved remap -> the caller stays on the RAM
// fold. The out-buffers outlive the spec2 use; the manager keeps the temp alive across the read. (docs/temp-db.md)
std::unique_ptr<ibTempTableManager> PromoteSingleComputed(
	const ibDataQuerySpec& spec,
	std::vector<ibQueryCondition>& outConds,
	std::vector<ibQuerySortItem>& outSorts,
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>>& outSelects,
	std::vector<const ibBackendQueryColumn*>& outGroupBy,
	std::vector<std::vector<const ibBackendQueryColumn*>>& outGroupPaths,
	std::vector<ibDataQueryBuilder::AggregateItem>& outAggs,
	std::vector<ibDataQueryBuilder::HavingItem>& outHaving,
	const ibBackendQueryable*& outPrimary)
{
	const ibBackendQueryable* q = spec.m_queryable;
	if (q == nullptr || !q->IsComputedInRam() || spec.m_holder == nullptr)
		return nullptr;

	// Materialise the computed rows (own conditions pushed as compute filters) into a DB temp table.
	std::vector<ibQueryCondition> ownConds;
	if (spec.m_conditions)
		for (const ibQueryCondition& c : *spec.m_conditions)
			if (c.m_path.empty() && !c.m_expr && c.m_col != nullptr && q->OwnsColumn(c.m_col)) ownConds.push_back(c);
	ibQueryRamTable rows = q->ComputeRows(ownConds);
	if (!WorthDbTemp(rows.RowCount()))
		return nullptr;
	std::unique_ptr<ibTempTableManager> mgr = ibTempTableManager::Materialise(spec.m_holder, rows, q->GetMetaData());
	if (!mgr)
		return nullptr;
	const ibDbTempTableQueryable* temp = mgr->Queryable();
	if (temp == nullptr)
		return nullptr;

	// Remap a source column onto the temp's same-named column; a miss fails the promote (-> RAM).
	bool ok = true;
	auto remap = [&](const ibBackendQueryColumn* c) -> const ibBackendQueryColumn* {
		if (c == nullptr) return nullptr;
		const ibBackendQueryColumn* t = temp->ResolveColumnByName(c->GetName());
		if (t == nullptr) ok = false;
		return t;
	};
	// A DOT-WALK path lives HALF on the temp (its FIRST segment is the reference the computed source owns) and
	// HALF on the target catalogs (the deeper segments + leaf — real DB sources the temp joins server-side via
	// ibRefJoinChain). So remap ONLY the first segment onto the temp; keep the rest. A plain (empty) path is a
	// scalar column remapped whole.
	auto remapPath = [&](const std::vector<const ibBackendQueryColumn*>& p) {
		std::vector<const ibBackendQueryColumn*> np = p;
		if (!np.empty()) np[0] = remap(np[0]);
		return np;
	};

	if (spec.m_conditions)
		for (const ibQueryCondition& c : *spec.m_conditions) {
			ibQueryCondition n = c;
			n.m_col  = c.m_path.empty() ? remap(c.m_col) : c.m_col;   // scalar -> temp; a dot-walk leaf stays on its catalog
			n.m_path = remapPath(c.m_path);
			outConds.push_back(n);
		}
	if (spec.m_sorts)
		for (const ibQuerySortItem& s : *spec.m_sorts) {
			ibQuerySortItem n = s;
			n.m_col  = s.m_path.empty() ? remap(s.m_col) : s.m_col;
			n.m_path = remapPath(s.m_path);
			outSorts.push_back(n);
		}
	if (spec.m_selectCols)
		for (const auto& sc : *spec.m_selectCols) outSelects.push_back({ remap(sc.first), sc.second });

	// GROUP BY: a scalar / reference key maps onto the temp (a reference rides its blob spread and GROUPs
	// server-side by the full spread); a DOT-WALK key's leaf stays on its catalog (joined), only the path's
	// first segment maps to the temp. outGroupBy and outGroupPaths stay index-parallel (the aggregate reads
	// the leaf's fields qualified by the path's join alias).
	static const std::vector<std::vector<const ibBackendQueryColumn*>> kNoPaths;
	const std::vector<std::vector<const ibBackendQueryColumn*>>& groupPaths = spec.m_groupPaths ? *spec.m_groupPaths : kNoPaths;
	if (spec.m_groupBy)
		for (size_t gi = 0; gi < spec.m_groupBy->size(); ++gi) {
			const ibBackendQueryColumn* g = (*spec.m_groupBy)[gi];
			const std::vector<const ibBackendQueryColumn*> gp = (gi < groupPaths.size()) ? groupPaths[gi]
			                                                  : std::vector<const ibBackendQueryColumn*>{};
			outGroupBy.push_back(gp.empty() ? remap(g) : g);   // scalar/reference -> temp; a dot-walk leaf stays on its catalog
			outGroupPaths.push_back(remapPath(gp));
		}

	if (spec.m_aggregates)
		for (const auto& a : *spec.m_aggregates) {
			ibDataQueryBuilder::AggregateItem n = a;
			n.m_col  = a.m_path.empty() ? remap(a.m_col) : a.m_col;   // scalar input -> temp; a dot-walk leaf stays on its catalog
			n.m_path = remapPath(a.m_path);
			// n.m_expr (SUM(Qty*Price)) references the source columns by field NAME -> lowers over the temp's
			// same-named fields; no pointer remap needed.
			outAggs.push_back(n);
		}
	if (spec.m_having)
		for (const auto& h : *spec.m_having) { ibDataQueryBuilder::HavingItem n = h; n.m_col = remap(h.m_col); outHaving.push_back(n); }
	if (!ok)
		return nullptr;   // a column had no temp counterpart -> RAM

	outPrimary = temp;
	return mgr;
}
} // namespace

// Aggregated read over a COMPUTED source (a register slice / a subquery): compute the rows
// (door conditions pushed as compute filters), then the RAM GROUP-BY fold. Without this
// override the base default would return the RAW rows — silently dropping the aggregation.
// HAVING is not folded on the RAM path (the lowering gates it).
ibDataQueryResult ibComputedProvider::ExecuteAggregate(const ibDataQuerySpec& spec)
{
	// ComputeRows + dot-walk resolution + dot-walk WHERE: GROUP BY Ref.Field, SUM(Ref.Field) and WHERE
	// Ref.Field = X over the computed source all resolve the reference leaves in RAM before the fold.
	std::vector<const ibBackendQueryColumn*> cols;
	ibQueryRamTable rows = ComputeRowsResolved(spec, cols);
	if (spec.m_predicate)   // boolean WHERE (OR / NOT / IS NULL) — filter before the fold (the register can't)
		rows = ibQueryComposer::FilterRows(rows, spec.m_predicate.get());
	return RamAggregate(rows, spec);
}

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
	if (IsSingleSource(spec)) {
		// Push to server: materialise the computed source into a DB temp table and run the aggregate as
		// server-side SQL (GROUP BY / SUM(expr) / HAVING over a real table). On any miss -> the RAM fold.
		if (spec.m_queryable != nullptr && spec.m_queryable->IsComputedInRam()) {
			std::vector<ibQueryCondition> pConds; std::vector<ibQuerySortItem> pSorts;
			std::vector<std::pair<const ibBackendQueryColumn*, wxString>> pSelects;
			std::vector<const ibBackendQueryColumn*>       pGroupBy;
			std::vector<std::vector<const ibBackendQueryColumn*>> pGroupPaths;   // parallel to pGroupBy — a dot-walk key's path
			std::vector<ibDataQueryBuilder::AggregateItem> pAggs;
			std::vector<ibDataQueryBuilder::HavingItem>    pHaving;
			const ibBackendQueryable* pPrimary = nullptr;
			if (std::unique_ptr<ibTempTableManager> mgr =
			        PromoteSingleComputed(spec, pConds, pSorts, pSelects, pGroupBy, pGroupPaths, pAggs, pHaving, pPrimary)) {
				ibDataQuerySpec spec2 = spec;
				spec2.m_conditions = &pConds;  spec2.m_sorts      = &pSorts;      spec2.m_selectCols = &pSelects;
				spec2.m_groupBy    = &pGroupBy; spec2.m_groupPaths = &pGroupPaths; spec2.m_aggregates = &pAggs;   spec2.m_having = &pHaving;
				spec2.m_queryable  = pPrimary;
				// The DB aggregate result is a CURSOR over the temp; the mgr DROPs the temp when this block exits.
				// Drain it into RAM HERE (mgr still alive) so the returned result outlives the temp — the same
				// RAM-backed shape RamAggregate yields (group keys by GetColumnId, aggregates named by alias).
				ibDataQueryResult lazy = pPrimary->GetProvider().ExecuteAggregate(spec2);   // server-side SQL
				ibQueryRamTable out;
				for (const ibBackendQueryColumn* g : pGroupBy)
					if (g != nullptr) out.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
				ibMetaID aggId = 0x40000000u;   // far from any metaID — aggregates read by alias NAME (as RamAggregate)
				std::vector<std::pair<ibMetaID, wxString>> aggCols;
				for (const ibDataQueryBuilder::AggregateItem& a : pAggs) {
					out.AddColumn(aggId, a.m_alias, ibTypeDescription());
					aggCols.emplace_back(aggId, a.m_alias);
					++aggId;
				}
				while (lazy.Next()) {
					const long r = out.AppendRow();
					for (const ibBackendQueryColumn* g : pGroupBy) {
						if (g == nullptr) continue;
						// The provider projected every group key by its FULL spread (reference / variant / scalar
						// alike) — so the read here is uniform and metadata-blind: GetValue reconstructs the value.
						// The reference-typing (what to project, how to rebuild) lives in the provider, not here.
						out.SetCell(r, g->GetColumnId(), lazy.GetValue(g));
					}
					for (const std::pair<ibMetaID, wxString>& ac : aggCols)
						out.SetCell(r, ac.first, lazy.GetColumn(ac.second));
				}
				return ibDataQueryResult(std::move(out), pPrimary);
			}
		}
		return spec.m_queryable->GetProvider().ExecuteAggregate(spec);   // RAM fallback
	}

	// Fast path: a 2-leaf inner join of two real DB tables, scalar group keys / aggregate inputs —
	// run the JOIN + GROUP BY + aggregates in ONE server-side SELECT instead of materialising both
	// leaves to RAM and folding in C++ below. (docs/query-language-arc.md §22.1a)
	if (ibDbTableProvider::CanColocateAggregate(spec))
		return ibDbTableProvider::ExecuteColocatedAggregate(spec);

	// (computed ⋈ DB) totals: materialise the computed leaf into a DB temp table, remap its columns
	// (group keys / aggregate inputs / having / join key) onto the temp, and run JOIN + GROUP BY
	// SERVER-SIDE. Same promote as the read path; on a miss the temp drops and we RAM-fold below.
	{
		std::shared_ptr<ibQueryNode>  pRoot;
		std::vector<ibQueryCondition> pConds;
		std::vector<ibQuerySortItem>  pSorts;
		std::vector<std::pair<const ibBackendQueryColumn*, wxString>> pSelects;
		std::vector<const ibBackendQueryColumn*>            pGroupBy;
		std::vector<ibDataQueryBuilder::AggregateItem>      pAggs;
		std::vector<ibDataQueryBuilder::HavingItem>         pHaving;
		const ibBackendQueryable*     pPrimary = nullptr;
		if (std::unique_ptr<ibTempTableManager> mgr =
		        PromoteComputedLeaf(spec, pRoot, pConds, pSorts, pSelects, pGroupBy, pAggs, pHaving, pPrimary)) {
			ibDataQuerySpec spec2 = spec;
			spec2.m_root       = pRoot.get();
			spec2.m_conditions = &pConds;
			spec2.m_sorts      = &pSorts;
			spec2.m_groupBy    = &pGroupBy;
			spec2.m_aggregates = &pAggs;
			spec2.m_having     = &pHaving;
			spec2.m_queryable  = pPrimary;
			if (ibDbTableProvider::CanColocateAggregate(spec2))
				return ibDbTableProvider::ExecuteColocatedAggregate(spec2);
			// not co-locatable after the swap -> mgr drops the temp, fall through to RAM
		}
	}

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

// totals fold over a combined row set -> a RAW totals TREE (L3's own ibQueryRamTable). L3
// stops here — flat-vs-hierarchical rendering is the runtime's call. Group columns keyed by
// GetColumnId; aggregates by a synthetic id (read by alias). Grand total = the root (level
// 0). Pure (no DB) — exposed so the fold is unit-testable directly. (docs §22.1b)
ibSelectorTree ibQueryComposer::BuildTotalsTree(const ibQueryRamTable& detail,
		const std::vector<const ibBackendQueryColumn*>& groupFields,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	const long n = detail.RowCount();
	std::vector<long> all; all.reserve(static_cast<size_t>(n));
	for (long i = 0; i < n; ++i) all.push_back(i);

	ibSelectorTree tree;
	for (const ibBackendQueryColumn* g : groupFields)
		tree.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
	for (const ibDataQueryBuilder::AggregateItem& a : aggregates)   // aggregate column = its OWN source column
		if (a.m_col != nullptr) tree.AddColumn(a.m_col->GetColumnId(), a.m_col->GetName(), a.m_col->GetTypeDesc());
	AddSyntheticAggColumns(tree, aggregates);                       // + COUNT(*) receivers
	FoldTotals(tree.Root(), detail, all, groupFields, aggregates, 0, {});
	return tree;   // raw — the runtime decides flat vs. hierarchical
}

// RECURSIVE hierarchy fold (parent-ref). From a WHOLE materialised detail table -> the Node tree the
// runtime already consumes. The sibling of BuildTotalsTree (fixed columns); here the grouping is by a
// row's parent-ref recursively, unbounded depth. m_hasChildren comes from the data; each node's
// subtotal rolls over its subtree. `mode` = the placement (Elements / Hierarchy / HierarchyOnly).
ibSelectorTree ibQueryComposer::BuildHierarchyTree(const ibQueryRamTable& detail,
		const ibBackendQueryColumn* rowKeyCol, const ibBackendQueryColumn* parentKeyCol,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates, ibDimensionKind mode)
{
	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : detail.Columns())   // aggregates roll IN-PLACE into their own columns
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);              // + COUNT(*) receivers

	const long n = detail.RowCount();

	// Elements: every row as a leaf NODE under the root, in order, no nesting. A grand total (if
	// aggregates) on the root. (The tree carries no flat rows — a leaf is a one-level node.)
	if (mode == ibDimensionKind::Elements) {
		std::vector<long> all; all.reserve(static_cast<size_t>(n));
		for (long r = 0; r < n; ++r) {
			ibSelectorTree::Node* leaf = tree.Root().AddChild(1);
			for (const ibQueryRamColumn& col : detail.Columns())
				leaf->m_values[col.m_id] = detail.GetCell(r, col.m_id);
			all.push_back(r);
		}
		ApplyAggregates(tree.Root(), detail, all, aggregates);   // grand total in-place
		tree.Root().m_hasChildren = !tree.Root().m_children.empty();
		return tree;
	}

	// Recursive nest by parent-ref: index children by parent key, identify roots (no / unknown parent).
	std::map<wxString, std::vector<long>> childrenOf;
	std::map<wxString, long>              keyToRow;
	for (long r = 0; r < n; ++r) {
		keyToRow[CellKey(detail.GetCell(r, rowKeyCol->GetColumnId()))] = r;
		childrenOf[CellKey(detail.GetCell(r, parentKeyCol->GetColumnId()))].push_back(r);
	}
	std::vector<long> roots;
	for (long r = 0; r < n; ++r) {
		const wxString pk = CellKey(detail.GetCell(r, parentKeyCol->GetColumnId()));
		if (pk.empty() || keyToRow.find(pk) == keyToRow.end()) roots.push_back(r);
	}

	std::vector<bool> visited(static_cast<size_t>(n), false);
	HierBuildCtx ctx{ &detail, rowKeyCol, &childrenOf, &aggregates, mode, &visited };
	std::vector<long> allRows;
	for (long rootIdx : roots) {
		std::vector<long> rows = AttachHierNode(ctx, &tree.Root(), rootIdx, 1);
		allRows.insert(allRows.end(), rows.begin(), rows.end());
	}
	tree.Root().m_hasChildren = !tree.Root().m_children.empty();
	ApplyAggregates(tree.Root(), detail, allRows, aggregates);   // grand total in-place
	return tree;
}

namespace {

// Context for the recursive reference-value hierarchy fold (invariant across the recursion).
struct RefHierCtx {
	const ibQueryRamTable*                                detail;
	const ibBackendQueryColumn*                           refCol;
	const std::map<wxString, std::vector<long>>*          rowsByVal;    // value-key -> snapshot rows with that value
	const std::map<wxString, std::vector<wxString>>*      childrenOf;   // value-key -> child value-keys (target parent-map)
	const std::map<wxString, ibValue>*                    valOf;        // value-key -> the ibValue (node display)
	const std::vector<ibDataQueryBuilder::AggregateItem>* aggregates;
	ibDimensionKind                                       dim;
};

// Attach the value-subtree rooted at `valueKey` under `parent`; returns the subtree's snapshot rows
// (for the parent's subtotal). A node = one target-catalog value (folder), carrying the rows whose
// refCol == this value + (recursively) descendant values' rows. Cycle-guarded.
std::vector<long> AttachRefNode(const RefHierCtx& ctx, ibSelectorTree::Node* parent, const wxString& valueKey,
                                int level, std::map<wxString, char>& visited)
{
	if (valueKey.empty() || visited[valueKey]) return {};
	visited[valueKey] = 1;

	const auto cit = ctx.childrenOf->find(valueKey);
	const bool hasKids = (cit != ctx.childrenOf->end() && !cit->second.empty());

	ibSelectorTree::Node* node = parent->AddChild(level);
	const auto vit = ctx.valOf->find(valueKey);
	if (vit != ctx.valOf->end()) node->m_values[ctx.refCol->GetColumnId()] = vit->second;

	std::vector<long> ownRows;
	const auto rit = ctx.rowsByVal->find(valueKey);
	if (rit != ctx.rowsByVal->end()) ownRows = rit->second;   // this value's own rows

	std::vector<long> subtree = ownRows;
	if (hasKids)
		for (const wxString& childKey : cit->second) {
			std::vector<long> kr = AttachRefNode(ctx, node, childKey, level + 1, visited);
			subtree.insert(subtree.end(), kr.begin(), kr.end());
		}
	// Hierarchy: the value's own detail rows hang under it as leaf elements; HierarchyOnly omits them
	// (folders only). Either way the subtotal counts them.
	if (ctx.dim == ibDimensionKind::Hierarchy)
		for (long r : ownRows) {
			ibSelectorTree::Node* leaf = node->AddChild(level + 1);
			for (const ibQueryRamColumn& col : ctx.detail->Columns())
				leaf->m_values[col.m_id] = ctx.detail->GetCell(r, col.m_id);
		}
	node->m_hasChildren = hasKids || (ctx.dim == ibDimensionKind::Hierarchy && !ownRows.empty());
	ApplyAggregates(*node, *ctx.detail, subtree, *ctx.aggregates);   // subtotal over the value-subtree, in-place
	return subtree;
}

} // namespace

ibSelectorTree ibQueryComposer::BuildReferenceHierarchy(const ibQueryRamTable& snapshot,
		const ibBackendQueryColumn* refCol, const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
		ibDatabaseConnectionHolder* holder, const ibBackendQueryable* source, ibDimensionKind dim)
{
	// Resolve the target catalog of the reference field + its hierarchy keys.
	const ibBackendQueryable* target = (source != nullptr) ? source->GetProvider().ResolveReferenceTarget(source, refCol) : nullptr;
	const ibBackendQueryColumn* tRowKey = (target != nullptr && !target->GetPrimaryKeyColumns().empty())
	                                       ? target->GetPrimaryKeyColumns().front() : nullptr;
	const ibBackendQueryColumn* tParent = (target != nullptr) ? target->GetHierarchyColumn() : nullptr;

	// No target / no holder / no parent column → flat group by the value (degrade, not crash).
	if (target == nullptr || holder == nullptr || tRowKey == nullptr || tParent == nullptr)
		return BuildTotalsTree(snapshot, { refCol }, aggregates);

	// Materialise the target's parent-map (value-key -> parent value-key) through the door.
	std::map<wxString, wxString> parentOf;
	{
		ibDataQueryBuilder q(holder);
		q.From(source->GetProvider().ResolveReferenceTarget(source, refCol)).Select(tRowKey, wxEmptyString).Select(tParent, wxEmptyString);
		ibSelector ts = q.Execute(ibReadPageRequest{}).Select(ibSelectKind::ibSelectKind_Direct);
		while (ts.Next())
			parentOf[CellKey(ts.GetValue(tRowKey))] = CellKey(ts.GetValue(tParent));
	}

	// Index the snapshot rows by the refCol value; build the value->children map from parentOf.
	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : snapshot.Columns())
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);              // + COUNT(*) receivers

	std::map<wxString, std::vector<long>> rowsByVal;
	std::map<wxString, ibValue>           valOf;
	const long n = snapshot.RowCount();
	for (long r = 0; r < n; ++r) {
		const ibValue v = snapshot.GetCell(r, refCol->GetColumnId());
		const wxString k = CellKey(v);
		rowsByVal[k].push_back(r);
		valOf[k] = v;
	}
	std::map<wxString, std::vector<wxString>> childrenOf;   // among VALUES PRESENT (+ their ancestors)
	std::map<wxString, char> seen;
	std::function<void(const wxString&)> chainUp = [&](const wxString& key) {
		if (key.empty() || seen[key]) return;
		seen[key] = 1;
		const auto pit = parentOf.find(key);
		const wxString par = (pit != parentOf.end()) ? pit->second : wxString();
		childrenOf[par].push_back(key);   // par empty => a root
		chainUp(par);
	};
	for (const auto& kv : rowsByVal) chainUp(kv.first);

	RefHierCtx ctx{ &snapshot, refCol, &rowsByVal, &childrenOf, &valOf, &aggregates, dim };
	std::map<wxString, char> visited;
	std::vector<long> allRows;
	const auto rit = childrenOf.find(wxString());   // roots = values whose parent is empty/unknown
	if (rit != childrenOf.end())
		for (const wxString& rootKey : rit->second) {
			std::vector<long> rows = AttachRefNode(ctx, &tree.Root(), rootKey, 1, visited);
			allRows.insert(allRows.end(), rows.begin(), rows.end());
		}
	tree.Root().m_hasChildren = !tree.Root().m_children.empty();
	ApplyAggregates(tree.Root(), snapshot, allRows, aggregates);   // grand total in-place
	return tree;
}

// === GENERAL multi-level dimension combiner =================================================
namespace {

struct DimCtx {
	const ibQueryRamTable*                                snapshot;
	const std::vector<ibTotalLevel>*                      levels;
	const std::vector<ibDataQueryBuilder::AggregateItem>* aggregates;
	ibDatabaseConnectionHolder*                           holder;
	const ibBackendQueryable*                             source;
};

// Parent-map (value-key -> parent value-key) for a level field: the target catalog of the reference
// field (cross), or the source itself when the field is the source's OWN parent column (self). Read
// through the door. Empty when there is no hierarchy target / no holder.
std::map<wxString, wxString> ParentMapForField(const DimCtx& ctx, const ibBackendQueryColumn* field)
{
	std::map<wxString, wxString> pm;
	const ibBackendQueryable* target = (ctx.source != nullptr) ? ctx.source->GetProvider().ResolveReferenceTarget(ctx.source, field) : nullptr;
	if (target == nullptr && ctx.source != nullptr && field == ctx.source->GetHierarchyColumn())
		target = ctx.source;
	if (target == nullptr || ctx.holder == nullptr) return pm;
	const std::vector<const ibBackendQueryColumn*> keys = target->GetPrimaryKeyColumns();
	const ibBackendQueryColumn* rk = keys.empty() ? nullptr : keys.front();
	const ibBackendQueryColumn* pk = target->GetHierarchyColumn();
	if (rk == nullptr || pk == nullptr) return pm;
	ibDataQueryBuilder q(ctx.holder);
	q.From(target).Select(rk, wxEmptyString).Select(pk, wxEmptyString);
	ibSelector ts = q.Execute(ibReadPageRequest{}).Select(ibSelectKind::ibSelectKind_Direct);
	while (ts.Next())
		pm[CellKey(ts.GetValue(rk))] = CellKey(ts.GetValue(pk));
	return pm;
}

void FoldDimLevel(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows, size_t levelIdx);

// Attach one value of a Hierarchy level under `parent`: sub-values (hierarchy depth) + the NEXT level
// inside this value's own rows (Hierarchy only). Returns the value-subtree's rows (for the subtotal).
std::vector<long> AttachDimValue(const DimCtx& ctx, ibSelectorTree::Node* parent, size_t levelIdx,
	const wxString& valueKey,
	const std::map<wxString, std::vector<long>>& byVal, const std::map<wxString, ibValue>& valOf,
	const std::map<wxString, std::vector<wxString>>& childrenOf, std::map<wxString, char>& visited)
{
	if (valueKey.empty() || visited[valueKey]) return {};
	visited[valueKey] = 1;
	const ibTotalLevel& level = (*ctx.levels)[levelIdx];

	ibSelectorTree::Node* node = parent->AddChild(parent->m_level + 1);
	node->m_values = parent->m_values;   // inherit the grouping fields available from the levels above (same rule as the Elements branch)
	const auto vit = valOf.find(valueKey);
	if (vit != valOf.end()) node->m_values[level.m_col->GetColumnId()] = vit->second;

	std::vector<long> ownRows;
	const auto rit = byVal.find(valueKey);
	if (rit != byVal.end()) ownRows = rit->second;

	std::vector<long> subtree = ownRows;
	const auto cit = childrenOf.find(valueKey);          // sub-values (deeper in the catalog hierarchy)
	if (cit != childrenOf.end())
		for (const wxString& childKey : cit->second) {
			std::vector<long> kr = AttachDimValue(ctx, node, levelIdx, childKey, byVal, valOf, childrenOf, visited);
			subtree.insert(subtree.end(), kr.begin(), kr.end());
		}
	FoldDimLevel(ctx, node, ownRows, levelIdx + 1);      // next level inside this value's own rows
	node->m_hasChildren = !node->m_children.empty();
	ApplyAggregates(*node, *ctx.snapshot, subtree, *ctx.aggregates);
	return subtree;
}

// Fold `rows` at `levelIdx` under `node`: group by the level field, then either flat groups (Elements)
// or the field's reference hierarchy (Hierarchy/HierarchyOnly). Recurses the next level inside.
void FoldDimLevel(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows, size_t levelIdx)
{
	if (levelIdx >= ctx.levels->size()) return;          // leaf level reached
	const ibTotalLevel& level = (*ctx.levels)[levelIdx];

	std::map<wxString, std::vector<long>> byVal;
	std::map<wxString, ibValue>           valOf;
	std::vector<wxString>                 order;
	for (long r : rows) {
		const ibValue v = ctx.snapshot->GetCell(r, level.m_col->GetColumnId());
		const wxString k = CellKey(v);
		if (byVal.find(k) == byVal.end()) { order.push_back(k); valOf[k] = v; }
		byVal[k].push_back(r);
	}

	if (level.m_dim == ibDimensionKind::Elements) {
		for (const wxString& k : order) {
			ibSelectorTree::Node* child = node->AddChild(node->m_level + 1);
			// A SUBGROUP inherits the grouping fields AVAILABLE from the levels above (Max) — copy the parent
			// group's stamped dimension values down, then add this level's own. So a display column that
			// dot-walks an ANCESTOR dimension's reference (e.g. the parent grouped by Reference, this level by
			// Reference.DataVersion) resolves against the inherited value in the subgroup header, not just at the
			// top level. Aggregates are (re)stamped by ApplyAggregates below, so inheriting them is harmless.
			child->m_values = node->m_values;
			child->m_values[level.m_col->GetColumnId()] = valOf[k];
			FoldDimLevel(ctx, child, byVal[k], levelIdx + 1);   // next level inside
			child->m_hasChildren = !child->m_children.empty();
			ApplyAggregates(*child, *ctx.snapshot, byVal[k], *ctx.aggregates);
		}
		return;
	}

	// Hierarchy / HierarchyOnly — arrange the values into the catalog's parent-ref tree.
	const std::map<wxString, wxString> parentOf = ParentMapForField(ctx, level.m_col);
	std::map<wxString, std::vector<wxString>> childrenOf;
	std::map<wxString, char> seen;
	std::function<void(const wxString&)> chainUp = [&](const wxString& key) {
		if (key.empty() || seen[key]) return;
		seen[key] = 1;
		const auto pit = parentOf.find(key);
		const wxString par = (pit != parentOf.end()) ? pit->second : wxString();
		childrenOf[par].push_back(key);
		chainUp(par);
	};
	for (const auto& kv : byVal) chainUp(kv.first);
	std::map<wxString, char> visited;
	const auto rit = childrenOf.find(wxString());
	if (rit != childrenOf.end())
		for (const wxString& rootKey : rit->second)
			AttachDimValue(ctx, node, levelIdx, rootKey, byVal, valOf, childrenOf, visited);
}

} // namespace

ibSelectorTree ibQueryComposer::BuildDimensionTree(const ibQueryRamTable& snapshot,
		const std::vector<ibTotalLevel>& levels, const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
		ibDatabaseConnectionHolder* holder, const ibBackendQueryable* source)
{
	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : snapshot.Columns())
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);

	std::vector<long> all; all.reserve(static_cast<size_t>(snapshot.RowCount()));
	for (long r = 0; r < snapshot.RowCount(); ++r) all.push_back(r);

	const DimCtx ctx{ &snapshot, &levels, &aggregates, holder, source };
	FoldDimLevel(ctx, &tree.Root(), all, 0);
	tree.Root().m_hasChildren = !tree.Root().m_children.empty();
	ApplyAggregates(tree.Root(), snapshot, all, aggregates);   // grand total in-place
	return tree;
}

// Greedy smallest-first order for a flattened pure-INNER join chain (header doc).
// Start = the smallest unit; each step joins the smallest unit edge-connected to the
// already-joined prefix. Empty = disconnected (or a malformed edge) — caller keeps
// the tree order. Pure — unit-testable.
std::vector<size_t> ibQueryComposer::PlanInnerJoinOrder(const std::vector<long>& rowCounts,
                                                        const std::vector<std::pair<size_t, size_t>>& edges)
{
	const size_t n = rowCounts.size();
	std::vector<size_t> order;
	if (n == 0) return order;

	std::vector<std::vector<size_t>> adj(n);
	for (const auto& e : edges) {
		if (e.first >= n || e.second >= n || e.first == e.second) return {};
		adj[e.first].push_back(e.second);
		adj[e.second].push_back(e.first);
	}

	std::vector<bool> joined(n, false);
	size_t start = 0;
	for (size_t u = 1; u < n; ++u)
		if (rowCounts[u] < rowCounts[start]) start = u;
	joined[start] = true;
	order.push_back(start);

	while (order.size() < n) {
		size_t best = n;
		for (size_t u = 0; u < n; ++u) {
			if (joined[u]) continue;
			bool connected = false;
			for (size_t v : adj[u])
				if (joined[v]) { connected = true; break; }
			if (!connected) continue;
			if (best == n || rowCounts[u] < rowCounts[best]) best = u;
		}
		if (best == n) return {};   // disconnected graph
		joined[best] = true;
		order.push_back(best);
	}
	return order;
}

// RAM inner join: nested-loop over (onLeft == onRight); each output column is read from
// its tagged side. Keyed by GetColumnId throughout. Pure — the multi-source JOIN core.
ibQueryRamTable ibQueryComposer::JoinRamTables(const ibQueryRamTable& left, const ibQueryRamTable& right,
		const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
		const std::vector<const ibBackendQueryColumn*>& outCols, const std::vector<bool>& fromLeft,
		ibQueryJoinKind kind, const ibJoinOn& on)
{
	ibQueryRamTable out;
	for (const ibBackendQueryColumn* c : outCols)
		out.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());

	// Emit one output row from a (left row, right row) pair — a negative index = that side is absent
	// (an OUTER join's unmatched row), so its columns yield NULL cells.
	auto emit = [&](long li, long rj) {
		const long r = out.AppendRow();
		for (size_t k = 0; k < outCols.size(); ++k) {
			const long srcRow = fromLeft[k] ? li : rj;
			const ibQueryRamTable& src = fromLeft[k] ? left : right;
			out.SetCell(r, outCols[k]->GetColumnId(), srcRow >= 0 ? src.GetCell(srcRow, outCols[k]->GetColumnId()) : ibValue());
		}
	};

	// Hash-join by the join key's IDENTITY key (GetHashKey — guid for refs): index the right side once,
	// probe per left row. O(n+m).
	//   - No join key column (onLeft/onRight == nullptr) -> keyless CROSS: empty key both sides -> cartesian.
	//   - Key column present but the VALUE is NULL/empty -> SQL `NULL = NULL` is UNKNOWN -> never matches.
	//     The row is left unindexed/unprobed: INNER drops it; an OUTER join keeps it unmatched on its own
	//     side (LEFT -> emit(i,-1); RIGHT -> the trailing rightMatched pass emits it).
	// INNER: matched pairs only. LEFT/FULL: also unmatched-left. RIGHT/FULL: also unmatched-right.
	const bool keepLeft  = (kind == ibQueryJoinKind::Left  || kind == ibQueryJoinKind::Full);
	const bool keepRight = (kind == ibQueryJoinKind::Right || kind == ibQueryJoinKind::Full);

	// THETA join — a nested loop over every (left, right) pair, the ON comparison evaluated per pair with SQL
	// three-valued logic (a NULL operand is UNKNOWN -> never matches). O(n*m); the hash index below is equi-only.
	// Taken for a non-equi comparison on key columns OR a COMPUTED ON (a.x+1 <op> b.y): each side is then an
	// expression evaluated against ITS OWN table per row (lhs over left, rhs over right). A keyless CROSS and a
	// plain-equi key join stay on the hash route.
	const bool computedOn = (on.m_exprL != nullptr && on.m_exprR != nullptr);
	if (computedOn || (on.m_op != ibJoinCompareOp::Eq && onLeft != nullptr && onRight != nullptr)) {
		auto thetaMatch = [](const ibValue& a, const ibValue& b, ibJoinCompareOp op) -> bool {
			switch (op) {
				case ibJoinCompareOp::Ne: return !a.CompareValueEQ(b);
				case ibJoinCompareOp::Lt: return a.CompareValueLS(b) < 0;
				case ibJoinCompareOp::Le: return a.CompareValueLE(b);
				case ibJoinCompareOp::Gt: return a.CompareValueGT(b) > 0;
				case ibJoinCompareOp::Ge: return a.CompareValueGE(b);
				default:                  return a.CompareValueEQ(b);
			}
		};
		std::vector<char> rMatched(static_cast<size_t>(right.RowCount()), 0);
		for (long i = 0; i < left.RowCount(); ++i) {
			const ibValue keyL = on.m_exprL ? EvalColumnExprRow(on.m_exprL.get(), left, i) : left.GetCell(i, onLeft->GetColumnId());
			bool matched = false;
			if (!RamIsNullValue(keyL))
				for (long j = 0; j < right.RowCount(); ++j) {
					const ibValue keyR = on.m_exprR ? EvalColumnExprRow(on.m_exprR.get(), right, j) : right.GetCell(j, onRight->GetColumnId());
					if (RamIsNullValue(keyR)) continue;        // NULL operand -> UNKNOWN -> no match
					if (thetaMatch(keyL, keyR, on.m_op)) { emit(i, j); rMatched[static_cast<size_t>(j)] = 1; matched = true; }
				}
			if (!matched && keepLeft) emit(i, -1);             // unmatched left (LEFT / FULL)
		}
		if (keepRight)
			for (long j = 0; j < right.RowCount(); ++j)
				if (!rMatched[static_cast<size_t>(j)]) emit(-1, j);   // unmatched right (RIGHT / FULL)
		return out;
	}

	std::map<wxString, std::vector<long>> rightByKey;
	for (long j = 0; j < right.RowCount(); ++j) {
		if (onRight != nullptr) {
			const ibValue keyR = right.GetCell(j, onRight->GetColumnId());
			if (RamIsNullValue(keyR)) continue;   // NULL key: unmatchable (RIGHT/FULL emit it below as unmatched)
			rightByKey[keyR.GetHashKey()].push_back(j);
		}
		else rightByKey[ibValue().GetHashKey()].push_back(j);   // keyless cross
	}
	std::vector<char> rightMatched(static_cast<size_t>(right.RowCount()), 0);
	for (long i = 0; i < left.RowCount(); ++i) {
		std::map<wxString, std::vector<long>>::const_iterator it = rightByKey.end();
		if (onLeft != nullptr) {
			const ibValue keyL = left.GetCell(i, onLeft->GetColumnId());
			if (!RamIsNullValue(keyL)) it = rightByKey.find(keyL.GetHashKey());   // NULL key -> stays end() -> no match
		}
		else it = rightByKey.find(ibValue().GetHashKey());   // keyless cross
		if (it == rightByKey.end()) {
			if (keepLeft) emit(i, -1);   // unmatched left (LEFT / FULL), including a NULL-key left row
			continue;
		}
		for (const long j : it->second) { emit(i, j); rightMatched[static_cast<size_t>(j)] = 1; }
	}
	if (keepRight)
		for (long j = 0; j < right.RowCount(); ++j)
			if (!rightMatched[static_cast<size_t>(j)]) emit(-1, j);   // unmatched right (incl. NULL-key right)
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
			out.SetCell(r, outCols[k]->GetColumnId(),
			            branchCols[k] != nullptr ? branch.GetCell(i, branchCols[k]->GetColumnId()) : ibValue());
	}
}

ibQueryRamTable ibQueryComposer::DedupeRows(const ibQueryRamTable& src,
		const std::vector<const ibBackendQueryColumn*>& cols)
{
	// Row identity = the concatenated IDENTITY hash of every output cell (GetHashKey — a
	// reference keys by guid, so two cells pointing at the same row match; display strings
	// don't fool it). First occurrence wins; row order is preserved.
	ibQueryRamTable out;
	for (const ibBackendQueryColumn* c : cols)
		out.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());

	std::set<wxString> seen;
	for (long i = 0; i < src.RowCount(); ++i) {
		wxString key;
		for (const ibBackendQueryColumn* c : cols)
			key += src.GetCell(i, c->GetColumnId()).GetHashKey() + wxT("\x1f");
		if (!seen.insert(key).second)
			continue;
		const long r = out.AppendRow();
		for (const ibBackendQueryColumn* c : cols)
			out.SetCell(r, c->GetColumnId(), src.GetCell(i, c->GetColumnId()));
	}
	return out;
}

ibQueryRamTable ibQueryComposer::FilterRows(const ibQueryRamTable& src, const ibQueryPredicate* predicate)
{
	return RamFilter(src, predicate);   // the post-compose boolean-WHERE-tree filter (defined above)
}

ibValue ibQueryComposer::EvalColumnExpr(const ibQueryColumnExpr* expr, const ibQueryRamTable& table, long row)
{
	return EvalColumnExprRow(expr, table, row);   // the per-row computed-column evaluator (defined above)
}

// ExecuteGroupLevelPage — the paged single-level group read (door SelectAggregatePage). On a pageable shape
// (CanPageGroupLevel: one plain scalar dim over a single source) the level's groups run server-side as
// GROUP BY dim ORDER BY dim [dim </> anchor] LIMIT count — a keyset-paged group read; otherwise the unpaged
// aggregate (all groups). The lowering's single-scalar-dim TOTALS drill routes here instead of the RAM fold.
ibDataQueryResult ibQueryComposer::ExecuteGroupLevelPage(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
{
	if (ibDbTableProvider::CanPageGroupLevel(spec))
		return ibDbTableProvider::ExecuteGroupLevelPage(spec, page);
	return ExecuteAggregate(spec);   // not pageable server-side -> unpaged all-groups (defensive fallback)
}

// totals (hierarchical totals): fold the detail rows into a subtotal TREE. The group
// columns are the LEVELS (in order); the aggregates the sums folded at every level + the
// grand total. Input = the single source materialised, or the composed multi-source
// result. The tree lives in L3's own ibQueryRamTable. (docs/query-language-arc.md §22.1b)
ibSelectorTree ibQueryComposer::ExecuteTotals(const ibDataQuerySpec& spec)
{
	// Push-down: a single-source totals on a ROLLUP-capable DBMS runs server-side (GROUP BY ROLLUP),
	// the DBMS computing every subtotal level — only the aggregated rows transit, no raw detail.
	if (IsSingleSource(spec) && ibDbTableProvider::CanPushRollupTotals(spec))
		return ibDbTableProvider::ExecuteRollupTotals(spec);

	// Multi-source co-located JOIN on a ROLLUP-capable dialect: push GROUP BY ROLLUP server-side (the
	// DBMS computes every subtotal level; only aggregated rows transit) instead of materialising the
	// leaves and folding the totals tree in RAM. Same ibSelectorTree either way — perf, not correctness.
	if (!IsSingleSource(spec) && ibDbTableProvider::CanPushColocatedRollupTotals(spec))
		return ibDbTableProvider::ExecuteColocatedRollupTotals(spec);

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

long ibQueryComposer::ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind)
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
		return (m_row >= 0 && col != nullptr) ? m_table.GetCell(m_row, col->GetColumnId()) : ibValue();
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
ibValue  ibDataQueryResult::GetColumnObject(const wxString& prefix, const ibBackendQueryColumn* col) const { return m_source->ColumnObject(prefix, col); }

void ibDataQueryResult::SetMaterialiseColumns(std::vector<const ibBackendQueryColumn*> cols)
{
	m_matColumns = std::move(cols);
}

void ibDataQueryResult::SetTotals(std::vector<ibTotalLevel> levels, std::vector<ibAggregateItem> aggregates)
{
	m_totalLevels     = std::move(levels);
	m_totalAggregates = std::move(aggregates);
}

void ibDataQueryResult::SetSource(ibDatabaseConnectionHolder* holder, const ibBackendQueryable* queryable,
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> selectCols,
	std::vector<ibQueryCondition> conditions)
{
	m_srcHolder     = holder;
	m_srcQueryable  = queryable;
	m_srcSelectCols = std::move(selectCols);
	m_srcConditions = std::move(conditions);
}

// selection = result.Select(mode). Drain the cursor ONCE into a flat snapshot (the stamped
// materialise-columns) and hand it to an ibSelector — which folds it per the mode (flat / hierarchy
// / hierarchy-only) polymorphically; the caller sees one interface, never the backing or the form.
ibSelector ibDataQueryResult::Select(ibSelectKind mode)
{
	ibQueryRamTable snapshot;
	for (const ibBackendQueryColumn* c : m_matColumns)
		if (c != nullptr) snapshot.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());

	while (Next()) {
		const long r = snapshot.AppendRow();
		for (const ibBackendQueryColumn* c : m_matColumns)
			if (c != nullptr) snapshot.SetCell(r, c->GetColumnId(), m_source->Value(c));
	}
	ibSelector s(std::move(snapshot), mode);
	s.WithTotals(m_totalLevels, m_totalAggregates);                             // fold by the door's TotalBy config
	s.WithSource(m_srcHolder, m_srcQueryable, m_srcSelectCols, m_srcConditions);  // enable lazy sub-selections
	return s;
}

// Factory for the opaque build-once cache (defined where ibRenderedPageCache is
// complete) — the list model owns the result via shared_ptr.
std::shared_ptr<ibRenderedPageCache> ibDataQueryBuilder::NewPageCache()
{
	return std::make_shared<ibRenderedPageCache>();
}

// One ORDER BY key compared for the RAM sort floor. Delegates to ibValue::CompareValueLS — the
// SQL-aligned three-way total order (NULL = smallest) — then applies the sort direction, giving
// NULLS FIRST on ASC / NULLS LAST on DESC deterministically. The NULL rule lives in ONE place
// (ibValue) now, shared with operator< / std::sort / set / map.
//   return <0: a orders before b ; >0: b before a ; 0: equal on this key (try the next key).
int ibQueryComposer::RamSortCompareKey(const ibValue& va, const ibValue& vb, bool ascending)
{
	const int c = va.CompareValueLS(vb);
	return ascending ? c : -c;
}