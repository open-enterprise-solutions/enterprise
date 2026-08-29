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
#include "queryRowCursor.h"                                           // ibQueryRow / ibQueryRowCursor — what a fold reads (one pass, no table)
#include "querySelector.h"                                            // ibSelector — result.Select(mode) hands the drained snapshot to it
#include "dbTableProvider.h"                                          // ibDbTableProvider (vended by GetProvider) + ibRenderedPageCache (NewPageCache)
#include "resultSource.h"                                             // ibDataResultSource — the backing ibRamTableResultSource derives
#include "tempTableManager.h"                                         // ibTempTableManager — promote a computed leaf to a DB temp table (+ ibDbTempTableQueryable)

#include "backend/diagnostics/journal.h"                              // ibJournal — the technology journal

#include <map>                                                        // dot-walk join dedup + col->attr cache
#include <functional>                                                 // std::function — reference-hierarchy parent chain-up
#include <stdexcept>                                                  // guard for the not-yet-built multi-source composition path
#include <algorithm>                                                  // stable_sort — RAM ORDER BY over a composed result
#include <set>                                                        // DedupeRows — seen-row identity keys (plain UNION)
#include <unordered_set>                                              // DISTINCT folds — keyed by value, see ibValueHash
#include <unordered_map>                                              // RAM hash-join index

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
// It takes a ROW, whoever holds it (a materialised table's row, a streaming cursor's current row): the
// evaluation reads the current row and nothing else, so it names nothing bigger. The (table, index)
// spelling stays for the paths that address rows by index.
ibValue EvalColumnExprRow(const ibQueryColumnExpr* e, const ibQueryRow& row);
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
	// COMPUTED: an expression evaluated per row, named by its alias — stored nowhere.
	Kind               GetColumnKind()   const override { return Kind::Computed; }
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

	// ⭐ A COMPUTED SOURCE IS RAM BY NATURE — a register's slice / balance / turnover, a nested query,
	// a temp we filled. Journalled all the same, and for the same reason as the folds: what a reader
	// needs is not that this source is unusual, but how many rows it brought home before anything
	// above it began filtering.
	ibJournalInfo(wxT("query.road"), wxT("RAM: computed source '%s' read in memory - %ld rows"),
	              spec.m_queryable != nullptr ? spec.m_queryable->GetQueryName() : wxString(wxT("<none>")),
	              rows.RowCount());

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

	// A DOT-WALK TOTALS DIMENSION IS PUBLISHED UNDER ITS OWN ALIAS — the RAM twin of the DB
	// provider's `<leaf fields> AS dim0…`. The walk above brought the leaf in keyed by its own id,
	// which is the very thing that cannot identify the level (a self-reference walks back to the
	// SAME column). The reader asks for the alias — ColumnObject falls back to Column(prefix) on a
	// RAM source — so the value is copied into a column NAMED for the dimension.
	std::vector<ibComputedExprColumn> dimCols;
	if (spec.m_dimWalks != nullptr && !spec.m_dimWalks->empty()) {
		dimCols.reserve(spec.m_dimWalks->size());
		ibMetaID dimId = 0x71000000u;   // synthetic ids, clear of the computed-expression block above
		for (const ibDotWalkColumn& dw : *spec.m_dimWalks)
			dimCols.emplace_back(dw.m_alias, dimId++);
		for (size_t k = 0; k < dimCols.size(); ++k) {
			const ibBackendQueryColumn* leaf = (*spec.m_dimWalks)[k].m_path.empty()
				? nullptr : (*spec.m_dimWalks)[k].m_path.back();
			if (leaf == nullptr)
				continue;
			rows.AddColumn(dimCols[k].GetColumnId(), dimCols[k].GetName(), leaf->GetTypeDesc());
			for (long i = 0; i < rows.RowCount(); ++i)
				rows.SetCell(i, dimCols[k].GetColumnId(), rows.GetCell(i, leaf->GetColumnId()));
			cols.push_back(&dimCols[k]);
		}
	}

	if (spec.m_predicate)
		rows = ibQueryComposer::FilterRows(rows, spec.m_predicate.get());

	// SELECT DISTINCT over a computed source (subquery / slice) -- dedup by the output columns while keeping
	// ALL columns (the sort below may key on one not in the select list). First occurrence wins.
	if (spec.m_distinct && spec.m_selectCols != nullptr && !spec.m_selectCols->empty()) {
		ibQueryRamTable deduped = RamTableOf(cols);
		// A row identity is the SEQUENCE of its cells — see ibValueSeqHash (value.h).
		// It used to be those cells folded into one string through GetHashKey and
		// joined with \x1f: a text conversion per cell per row, then a tree of
		// strings compared character by character.
		std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual> seen;
		for (long i = 0; i < rows.RowCount(); ++i) {
			std::vector<ibValue> key;
			key.reserve(spec.m_selectCols->size());
			for (const auto& sc : *spec.m_selectCols)
				key.push_back(rows.GetCell(i, sc.first->GetColumnId()));
			if (!seen.insert(std::move(key)).second) continue;
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

// (THE QUERYABLE'S OWN BODIES MOVED OUT — GetProvider, ResolveColumnByName and the two projections
//  of its metaobject now live in queryable.cpp, beside their header, the way a column's do in
//  queryColumn.cpp. This file is the COMPOSER and the computed providers; a base class answering
//  "which metaobject stands behind me" was findable here only by knowing it happened to be here.)

// The shared stateless computed (RAM) provider — vended by ibComputedRegisterQueryable (in
// queryable.h, which may not name the concrete ibComputedProvider) and the temp / subquery
// computed sources. Declared in queryable.h, defined here where ibComputedProvider is complete.
ibBackendQueryProvider& ibComputedProviderInstance()
{
	static ibComputedProvider s_computedProvider;
	return s_computedProvider;
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

// Stand-in ids for a JOIN stitch's output columns that cannot be keyed by their own metaID, because
// two of them share it (a self-join selects the same column through two aliases). Clear of the same
// ranges as everything above: COUNT(*) receivers at 0x40000000, totals synthetics at 0x50000000,
// subquery aggregates at 0x60000000.
const ibMetaID kStitchSyntheticBase = 0x70000000u;

class ibSubqueryAggColumn final : public ibBackendColumnRawDB
{
public:
	ibSubqueryAggColumn(const wxString& alias, ibMetaID id)
		: ibBackendColumnRawDB(alias, RawType::Number), m_id(id) {}
	ibMetaID GetColumnId() const override { return m_id; }
private:
	ibMetaID m_id;
};

// ⭐⭐ A COLUMN OF THE INNER QUERY, SEEN UNDER ITS OUTPUT NAME.
//
// A nested table's columns are its OUTPUT — `… AS Attribute1Code` is what the outer query can write,
// and the only thing it can write. The exposed set was the inner columns themselves, so an aliased
// projection appeared under the underlying column's name (`Code`) and the outer query asking for the
// alias was told there is no such attribute — about a name the inner query plainly declares.
//
// Renaming in place is not possible (the same column object belongs to its own table and is read
// through it), so the two questions are separated: this answers "what is it called out here", and
// the value is fetched through the real column it stands for.
class ibSubqueryAliasColumn final : public ibBackendQueryColumn
{
public:
	// 🛑 WHAT IT NEEDS FROM THE COLUMN IT STANDS FOR IS TAKEN HERE, NOT KEPT AS A POINTER. It asks
	// exactly two things — the physical name and the type — and both are known at construction, so
	// holding the column afterwards buys nothing and costs a lifetime: the source may be a nested
	// wrapper's own column, which dies with that wrapper (see ibSubqueryQueryable::OwnsColumnStorage),
	// while this alias is read for as long as the outer query runs. That is what crashed on 2026-08-19
	// — `GetTypeDesc` through a freed column (0xdddddddd) while a report was being composed.
	ibSubqueryAliasColumn(const wxString& alias, const ibBackendQueryColumn* from, ibMetaID id)
		: m_alias(alias)
		, m_physical(from != nullptr ? from->GetPhysicalName() : alias)
		, m_type(from != nullptr ? from->GetTypeDesc() : ibTypeDescription())
		, m_id(id) {}

	wxString GetName() const override { return m_alias; }
	wxString GetPhysicalName() const override { return m_physical; }
	// The TYPE is the column's own — an alias renames, it does not re-type — taken at construction.
	ibTypeDescription& GetTypeDesc() const override { return m_type; }
	ibMetaID GetColumnId() const override { return m_id; }

private:
	wxString                  m_alias;
	wxString                  m_physical;
	mutable ibTypeDescription m_type;   // GetTypeDesc returns a non-const ref (engine-wide signature)
	ibMetaID                  m_id;
};

// A COMPUTED projection of the inner query — `a * b`, a CASE — seen from outside under its alias.
//
// ⚠ ITS TYPE IS UNKNOWN, and that is said rather than guessed: the door records a computed select as
// expression + alias and nothing else, so there is no column to take a type from. An EMPTY type
// description is how this engine already spells "unknown" — the places that ask (which aggregates
// fit, can this be unfolded) treat it as "offer everything" instead of believing a wrong answer.
class ibSubqueryExprColumn final : public ibBackendQueryColumn
{
public:
	ibSubqueryExprColumn(const wxString& alias, ibMetaID id) : m_alias(alias), m_id(id) {}
	// …and WITH a type where the schema knows one — an aggregate over a typed column, a constant
	// projection. Empty stays "unknown"; what is known travels.
	ibSubqueryExprColumn(const wxString& alias, ibMetaID id, const ibTypeDescription& type)
		: m_alias(alias), m_type(type), m_id(id) {}

	wxString GetName() const override { return m_alias; }
	wxString GetPhysicalName() const override { return m_alias; }
	ibTypeDescription& GetTypeDesc() const override { return m_type; }
	ibMetaID GetColumnId() const override { return m_id; }

private:
	wxString                  m_alias;
	mutable ibTypeDescription m_type;   // empty = unknown; GetTypeDesc returns a non-const ref
	ibMetaID                  m_id;
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
	// SET-valued: reads m_values, not m_value. An empty set matches nothing (same answer the SQL side
	// renders), so the loop falling through to false is the correct empty-IN semantics, not an oversight.
	case ibQueryFilterOp::In:
		for (const ibValue& v : c.m_values)
			if (cell == v) return true;
		return false;
	}
	return false;
}
} // namespace

ibSubqueryQueryable::ibSubqueryQueryable(const ibDataQueryBuilder& inner, long topCount)
	: m_inner(std::make_unique<ibDataQueryBuilder>(inner)), m_top(topCount)
{
	// ⭐ A QUERY THAT GROUPS IS A QUERY THAT FOLDS, whether or not it also aggregates. `GROUP BY Ref`
	// with no SUM beside it is a DISTINCT over the keys — one row per key — and it has to run the same
	// UNPAGED, full-spread GROUP BY. Read as a page instead, the projection drops the `_TYPE` field a
	// reference is reassembled from, and the read dies on a field the SELECT never listed
	// ("Field 'fldNNNN_TYPE' not found in the resultset"). The provider guards that shape already
	// (CanPageGroupLevel); the fold is decided HERE, so the guard is never reached with the wrong read.
	const auto& aggs = m_inner->GetAggregates();
	m_aggregate = !aggs.empty() || !m_inner->GetGroupBy().empty();
	if (m_aggregate) {
		// AGGREGATE shape: exposed columns = the GROUP BY keys (real columns, by name) + one
		// owned synthetic numeric column per aggregate alias.
		for (const ibBackendQueryColumn* g : m_inner->GetGroupBy())
			if (g != nullptr) m_columns.push_back(g);
		for (const ibBackendQueryColumn* g : m_inner->GetGroupBy())
			if (g != nullptr) { m_readFrom.push_back(g); m_readAlias.push_back(wxEmptyString); }
		ibMetaID nextId = kSubqueryAggColumnBase;
		for (const ibDataQueryBuilder::AggregateItem& a : aggs) {
			auto col = std::make_shared<ibSubqueryAggColumn>(a.m_alias, nextId++);
			m_ownedColumns.push_back(col);
			m_columns.push_back(col.get());
			m_readFrom.push_back(nullptr);
			m_readAlias.push_back(a.m_alias);
		}
		m_readPrefix.assign(m_columns.size(), wxEmptyString);
		return;
	}
	const auto& selectCols = m_inner->GetSelectColumns();
	if (!selectCols.empty()) {
		// AN ALIAS IS THE NAME OUT HERE. Where the inner query gave one, that is what the outer query
		// writes — and the value still comes from the real column (m_readFrom, parallel to m_columns).
		ibMetaID nextId = kSubqueryAggColumnBase + 0x1000u;
		for (const auto& sc : selectCols) {
			if (sc.first == nullptr)
				continue;
			if (!sc.second.IsEmpty() && sc.second != sc.first->GetName()) {
				auto col = std::make_shared<ibSubqueryAliasColumn>(sc.second, sc.first, nextId++);
				m_ownedColumns.push_back(col);
				m_columns.push_back(col.get());
			}
			else {
				m_columns.push_back(sc.first);
			}
			m_readFrom.push_back(sc.first);
			m_readAlias.push_back(wxEmptyString);
		}
	}
	else if (const ibBackendQueryable* primary = m_inner->GetPrimarySource()) {
		m_columns = primary->GetColumns();
		m_readFrom = m_columns;
		m_readAlias.assign(m_columns.size(), wxEmptyString);
	}

	// ⭐ THE DOT-WALKED SELECTIONS, under their aliases. They never reach the select list — the door
	// records them separately and the result gives them back BY ALIAS — so they need their own pass,
	// and their own way of being read.
	ibMetaID walkId = kSubqueryAggColumnBase + 0x2000u;
	for (const ibDotWalkColumn& walk : m_inner->GetDotWalks()) {
		if (walk.m_alias.IsEmpty() || walk.m_path.empty() || walk.m_path.back() == nullptr)
			continue;
		auto col = std::make_shared<ibSubqueryAliasColumn>(walk.m_alias, walk.m_path.back(), walkId++);
		m_ownedColumns.push_back(col);
		m_columns.push_back(col.get());
		m_readFrom.push_back(nullptr);
		m_readAlias.push_back(walk.m_alias);
	}

	// ⭐ AND THE COMPUTED ONES — arithmetic, CASE. A third list, a third way in, and the same rule:
	// what the inner query publishes under a name, the outer query can name.
	//
	// (This is the whole class, walked once instead of a fix per report: a projection reaches the
	// door as a plain column, a dot-walk, an aggregate or an expression, and a nested table has to
	// publish all four. Two of them were missing.)
	ibMetaID exprId = kSubqueryAggColumnBase + 0x3000u;
	for (const ibQueryColumnSelect& computed : m_inner->GetSelectExprs()) {
		if (computed.m_alias.IsEmpty())
			continue;
		auto col = std::make_shared<ibSubqueryExprColumn>(computed.m_alias, exprId++);
		m_ownedColumns.push_back(col);
		m_columns.push_back(col.get());
		m_readFrom.push_back(nullptr);
		m_readAlias.push_back(computed.m_alias);
	}

	m_readPrefix.assign(m_columns.size(), wxEmptyString);   // derived here: no schema, so no spread to reassemble
}

// ⭐⭐ TOLD ITS OUTPUT, not guessing it. Every published column comes from the inner select's schema:
// the name it is known by out here, and where its value comes from — a real column, or an alias the
// door answers to. That covers all four kinds of projection at once (plain, dot-walk, aggregate,
// computed) because the schema IS what the inner query produces, whichever way each one got there.
ibSubqueryQueryable::ibSubqueryQueryable(const ibDataQueryBuilder& inner, long topCount,
                                         const std::vector<ibSubqueryOutput>& outputs)
	: m_inner(std::make_unique<ibDataQueryBuilder>(inner)), m_top(topCount)
{
	// Grouping folds, with or without an aggregate beside it — same rule as the derived ctor above,
	// and the reason it matters is written there.
	m_aggregate = !m_inner->GetAggregates().empty() || !m_inner->GetGroupBy().empty();

	ibMetaID nextId = kSubqueryAggColumnBase + 0x4000u;
	for (const ibSubqueryOutput& out : outputs) {
		if (out.m_name.IsEmpty())
			continue;

		// ⭐ FIRST, KEEP WHAT WE ARE ABOUT TO POINT AT. `m_col` may be a column the inner schema
		// MINTED (a dot-walk leaf, a synthetic measure) whose storage belongs to that schema — a
		// local of the caller. Every list below (m_columns, m_readFrom) holds it by bare pointer and
		// is read on every fetch, long after the caller returned, so the share travels with it.
		if (out.m_owned)
			m_ownedColumns.push_back(out.m_owned);

		// THE SIMPLE CASE: a real column that already answers to this name IS the published column.
		if (out.m_alias.IsEmpty() && out.m_objectPrefix.IsEmpty()
		    && out.m_col != nullptr && out.m_col->GetName() == out.m_name) {
			m_columns.push_back(out.m_col);
			m_readFrom.push_back(out.m_col);
			m_readAlias.push_back(wxEmptyString);
			m_readPrefix.push_back(wxEmptyString);
			continue;
		}

		// Otherwise a thin column carries the NAME and the TYPE, and the value is fetched the way the
		// schema says. The type is taken from the schema rather than from the column, because those are
		// different questions: a dot-walk leaf is read by alias yet still holds THAT column's type, and
		// a reference with no type is not a reference — the outer query could not walk into it.
		std::shared_ptr<ibBackendQueryColumn> col = out.m_col != nullptr
			? std::static_pointer_cast<ibBackendQueryColumn>(
			      std::make_shared<ibSubqueryAliasColumn>(out.m_name, out.m_col, nextId++))
			: std::static_pointer_cast<ibBackendQueryColumn>(
			      std::make_shared<ibSubqueryExprColumn>(out.m_name, nextId++, out.m_type));
		m_ownedColumns.push_back(col);
		m_columns.push_back(col.get());
		m_readFrom.push_back(out.m_col);
		m_readAlias.push_back(out.m_objectPrefix.IsEmpty() ? out.m_alias : wxString());
		m_readPrefix.push_back(out.m_objectPrefix);
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
// See the header: the configuration this wrapper reads is the inner query's own, and it is what a
// reference walk needs to find its target. Asked of the inner source rather than stored, so a
// wrapper built over another wrapper answers through the chain and never goes stale.
const ibMetaData* ibSubqueryQueryable::GetMetaData() const
{
	if (!m_inner)
		return nullptr;
	const ibBackendQueryable* primary = m_inner->GetPrimarySource();
	return primary != nullptr ? primary->GetMetaData() : nullptr;
}

ibQueryRamTable ibSubqueryQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	ibQueryRamTable t;
	for (const ibBackendQueryColumn* col : m_columns)
		if (col != nullptr) t.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());

	// ⭐ ONE READ RULE for every shape, the same three-way rule the selection reader uses: a reference /
	// enum / composite leaf is REASSEMBLED from its prefixed field spread, an aliased projection (a
	// dot-walk, an aggregate, a computed expression) is asked for BY NAME, everything else comes through
	// its real column. Which of the three applies was decided when the exposed set was built — here it
	// is only obeyed, so the aggregate path and the plain path cannot drift apart.
	auto readCell = [this](const ibDataQueryResult& sel, size_t i) -> ibValue {
		if (i < m_readPrefix.size() && !m_readPrefix[i].IsEmpty() && m_readFrom[i] != nullptr)
			return sel.GetColumnObject(m_readPrefix[i], m_readFrom[i]);
		if (i < m_readAlias.size() && !m_readAlias[i].IsEmpty())
			return sel.GetColumn(m_readAlias[i]);
		if (i < m_readFrom.size() && m_readFrom[i] != nullptr)
			return sel.GetValue(m_readFrom[i]);
		return ibValue();
	};

	if (m_aggregate) {
		// AGGREGATE inner — run SelectAggregate; group keys read by column, aggregate aliases by
		// name. The outer's pushed-down conditions reference POST-aggregation output (HAVING
		// semantics), so they apply as a RAM post-filter here, never on the inner WHERE.
		ibDataQueryResult sel = m_inner->SelectAggregate();
		long emitted = 0;
		std::vector<ibValue> rowVals(m_columns.size());
		while (sel.Next()) {
			for (size_t i = 0; i < m_columns.size(); ++i)
				rowVals[i] = m_columns[i] != nullptr ? readCell(sel, i) : ibValue();

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
		ibJournalInfo(wxT("query.road"), wxT("RAM: nested query (aggregate) computed in memory - %ld rows"),
		              t.RowCount());
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
		// ⚠ READ THE WAY THE SCHEMA SAYS, STORE UNDER THE EXPOSED COLUMN. Where the inner query gave an
		// alias the two differ, and asking the result for the alias column would find nothing — the
		// result knows the inner query's own columns, not the names it publishes them under.
		for (size_t i = 0; i < m_columns.size(); ++i) {
			if (m_columns[i] == nullptr)
				continue;
			t.SetCell(r, m_columns[i]->GetColumnId(), readCell(sel, i));
		}
	}
	// ⭐ A NESTED QUERY THAT RAN HERE IS THE CTE ROAD NOT TAKEN. Since the lowering declares a nested
	// source to the server where it can (`WITH q_sub0 AS (…)`, ResolveFrom), reaching this line means
	// something sent it back: no `WITH` in the engine, a shape the declaration cannot carry, or a
	// source of its own that lives in memory. The refusal itself is journalled where it is decided —
	// this says what it cost.
	ibJournalInfo(wxT("query.road"), wxT("RAM: nested query computed in memory - %ld rows"), t.RowCount());
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

// --- semi-join key reduction (sideways information passing) ---------------------------------
// The RAM stitch reads each leaf whole and joins afterwards, so a leaf pays for every row it holds
// even when almost none of them can join. Once ONE side of a join is materialised its join-key values
// are KNOWN — pushing them into the other side as `key IN (…)` makes that read fetch only rows that
// can possibly join. It is a pure reduction: a row the filter removes could not have appeared in the
// result, so the answer is identical and only the read shrinks (docs/query-language-arc.md).
//
// The cap is the whole cost model. Beyond it the IN list itself (bind parameters, a long rendered
// predicate, the DBMS's own list handling) costs more than the read it saves — and a key set that
// large is usually not selective anyway. Above the cap we simply do not reduce; correctness never
// depends on the reduction happening.
const size_t kSemiJoinMaxKeys = 512;   // heuristic; tune against real numbers

// Distinct non-NULL values of `col` across `t`, or false when the set is unusable for a reduction:
// no column, an empty table (nothing to reduce BY — the join yields nothing anyway and the caller
// keeps the plain path), or more distinct keys than the cap. NULLs are skipped: a NULL key matches
// nothing in an equi-join, so it must not enter the set (and would poison an IN list).
bool CollectDistinctJoinKeys(const ibQueryRamTable& t, const ibBackendQueryColumn* col,
                             std::vector<ibValue>& out)
{
	out.clear();
	if (col == nullptr || t.RowCount() <= 0)
		return false;
	// Keyed by the VALUES, same policy as the DISTINCT fold and the RAM join —
	// no text conversion per row, no tree of strings (see ibValueHash, value.h).
	std::unordered_set<ibValue, ibValueHash, ibValueEqual> seen;
	for (long i = 0; i < t.RowCount(); ++i) {
		const ibValue v = t.GetCell(i, col->GetColumnId());
		if (v.IsNull() || v.IsEmpty())
			continue;
		if (!seen.insert(v).second)
			continue;
		if (seen.size() > kSemiJoinMaxKeys)
			return false;                                      // too many keys — not worth the IN list
		out.push_back(v);
	}
	return !out.empty();
}

// The reduction condition for the OTHER side's key column, appended to `extra` (the per-leaf extras the
// materialisation carries down). Ownership routing happens at the Source node: only the leaf that owns
// `col` picks it up.
void AppendSemiJoinCondition(const ibBackendQueryColumn* col, std::vector<ibValue> keys,
                             std::vector<ibQueryCondition>& extra)
{
	ibQueryCondition c;
	c.m_col    = col;
	c.m_op     = ibQueryFilterOp::In;
	c.m_values = std::move(keys);
	extra.push_back(std::move(c));
}

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
	for (const ibQueryCondition& c : conds)
		q.Where(c);   // VERBATIM — rebuilding from (col, op, value) drops m_values / m_path / m_expr
	ibReadPageRequest page; page.m_count = 0;   // every matching row
	ibDataQueryResult sel = q.Execute(page);     // reads through the cursor — never names a runtime table
	while (sel.Next()) {
		const long r = t.AppendRow();
		for (const ibBackendQueryColumn* col : cols)
			t.SetCell(r, col->GetColumnId(), sel.GetValue(col));
	}
	// ⭐⭐ THE LINE THAT SAYS "THESE ROWS CAME HOME". Every road that ends in our memory rather than in
	// the DBMS passes through here, so ONE line answers the question a wrong-looking report actually
	// raises: was this computed by the server, or did we read it all and do the work ourselves?
	//
	// The ROW COUNT is the point of it. "Went to RAM" is a road; 400 000 rows is a diagnosis — the
	// composition's own acceptance criterion is that memory grows with the number of GROUPS, and this
	// is where that is either true or visibly not. Debug only: the whole call compiles away in Release.
	ibJournalInfo(wxT("query.road"), wxT("RAM: materialised leaf '%s' - %ld rows, %u columns"),
	              leaf != nullptr ? leaf->GetQueryName() : wxString(wxT("<none>")),
	              t.RowCount(), static_cast<unsigned>(cols.size()));
	return t;
}

// ⭐ THE SAME READ, NOT MATERIALISED — a leaf read through its own provider and handed over as a
// CURSOR. Same source, same pushed-down conditions, same columns as MaterialiseLeafToRam above; the
// difference is that the rows are never all in hand, which is the whole of what a totals fold needs.
// It owns the query and the result, so the cursor may outlive the expression that made it.
class ibLeafRowCursor : public ibQueryRowCursor
{
public:
	ibLeafRowCursor(const ibBackendQueryable* leaf, ibDatabaseConnectionHolder* holder,
	                const std::vector<ibQueryCondition>& conds,
	                const std::vector<const ibBackendQueryColumn*>& cols)
		: m_query(holder), m_cols(cols)
	{
		for (const ibBackendQueryColumn* col : cols)
			if (col != nullptr)
				m_columns.push_back(ibQueryRamColumn{ col->GetColumnId(), col->GetName(), col->GetTypeDesc() });

		m_query.From(leaf);
		for (const ibQueryCondition& c : conds)
			m_query.Where(c);   // VERBATIM — rebuilding from (col, op, value) drops m_values / m_path / m_expr
		ibReadPageRequest page; page.m_count = 0;   // every matching row
		m_result = std::make_unique<ibDataQueryResult>(m_query.Execute(page));
		ibJournalInfo(wxT("query.road"), wxT("STREAM: reading leaf '%s' - %u columns, rows not held"),
		              leaf != nullptr ? leaf->GetQueryName() : wxString(wxT("<none>")),
		              static_cast<unsigned>(m_columns.size()));
	}

	bool Next() override { return m_result != nullptr && m_result->Next(); }

	ibValue Get(ibMetaID id) const override
	{
		if (m_result == nullptr)
			return ibValue();
		for (const ibBackendQueryColumn* col : m_cols)
			if (col != nullptr && col->GetColumnId() == id)
				return m_result->GetValue(col);
		return ibValue();
	}

	const std::vector<ibQueryRamColumn>& Columns() const override { return m_columns; }

private:
	ibDataQueryBuilder                       m_query;    // outlives the result it produced
	std::unique_ptr<ibDataQueryResult>       m_result;
	std::vector<const ibBackendQueryColumn*> m_cols;
	std::vector<ibQueryRamColumn>            m_columns;
};

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
	// ⭐ A TOTALS DIMENSION IS A DOT-WALK LIKE ANY OTHER (m_dimWalks — TOTALS BY Ref.Ref). It was the
	// one walker this gathering did not know about, so over a COMPUTED source (a subquery wrapper, a
	// register slice) the level's value was never brought in: the DB provider joins and projects it,
	// this path simply had no case for it, and the level folded every row into one empty group.
	if (spec.m_dimWalks != nullptr)
		for (const ibDotWalkColumn& dw : *spec.m_dimWalks)
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

	// ENFORCE the plain conditions here. They are HANDED to ComputeRows, but honouring them there is an
	// optimisation, not a duty — every register compute (balance / turnover / slice) ignores the parameter
	// and returns its whole set. Before the semi-join reduction `extra` was always empty, so nobody noticed;
	// now a key filter can ride in it and a source that drops it would read (and join) far more than asked.
	// Re-filtering a source that DID honour the conditions is idempotent — the rows it dropped are already
	// gone — so this is correct for both kinds of implementation.
	//
	// Restricted to conditions whose column is actually IN the returned table: a column the source does not
	// vend reads back as an empty cell, which the three-valued evaluator would take for NULL and use to drop
	// EVERY row. Applied BEFORE the dot-walk resolution below, so the reference joins run on fewer rows.
	if (!plainConds.empty()) {
		std::set<ibMetaID> vended;
		for (const ibQueryRamColumn& rc : rows.Columns())
			vended.insert(rc.m_id);
		for (const ibQueryCondition& c : plainConds)
			if (c.m_col != nullptr && vended.count(c.m_col->GetColumnId()) != 0)
				rows = ibQueryComposer::FilterRows(rows, ibQueryPredicate::Leaf(c).get());
	}

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

RamTri RamEvalLeaf(const ibQueryCondition& c, const ibQueryRow& row)
{
	// A COMPUTED lhs (WHERE Qty * Price > 100, a CASE) evaluates its expression per row; else read the column.
	ibValue cell;
	if (c.m_expr)                cell = EvalColumnExprRow(c.m_expr.get(), row);
	else if (c.m_col != nullptr) cell = row.Get(c.m_col);
	else                         return RamTri::False;
	// SET-valued `In` — handled BEFORE the scalar null guard below, which would read the unset m_value as
	// NULL and answer UNKNOWN for every row. Semantics match SQL: a NULL probe is UNKNOWN, an empty set is
	// FALSE, otherwise membership. (m_values itself never carries NULL — the producer strips them.)
	if (c.m_op == ibQueryFilterOp::In) {
		if (RamIsNullValue(cell)) return RamTri::Unknown;
		for (const ibValue& v : c.m_values)
			if (cell.CompareValueEQ(v)) return RamTri::True;
		return RamTri::False;
	}
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
		case ibQueryFilterOp::In:           break;   // unreachable — returned above; listed to keep the switch exhaustive
	}
	return res ? RamTri::True : RamTri::False;
}

RamTri RamEvalPredicate(const ibQueryPredicate* p, const ibQueryRow& row)
{
	if (p == nullptr) return RamTri::True;
	switch (p->m_kind) {
		case ibQueryPredicateKind::Leaf:
			return RamEvalLeaf(p->m_leaf, row);
		case ibQueryPredicateKind::And: {
			RamTri acc = RamTri::True;                          // FALSE dominates; else UNKNOWN if any
			for (const auto& c : p->m_children) {
				const RamTri r = RamEvalPredicate(c.get(), row);
				if (r == RamTri::False)   return RamTri::False;
				if (r == RamTri::Unknown) acc = RamTri::Unknown;
			}
			return acc;
		}
		case ibQueryPredicateKind::Or: {
			RamTri acc = RamTri::False;                         // TRUE dominates; else UNKNOWN if any
			for (const auto& c : p->m_children) {
				const RamTri r = RamEvalPredicate(c.get(), row);
				if (r == RamTri::True)    return RamTri::True;
				if (r == RamTri::Unknown) acc = RamTri::Unknown;
			}
			return acc;
		}
		case ibQueryPredicateKind::Not:
			return p->m_children.empty() ? RamTri::True
			                             : RamTriNot(RamEvalPredicate(p->m_children.front().get(), row));
		case ibQueryPredicateKind::IsNull: {
			// IS NULL / IS NOT NULL are DEFINITE (never UNKNOWN).
			if (p->m_col == nullptr) return RamTri::False;
			const bool isNull = RamIsNullValue(row.Get(p->m_col));
			return (p->m_negated ? !isNull : isNull) ? RamTri::True : RamTri::False;
		}
	}
	return RamTri::True;
}

// THE SAME QUESTION, ASKED OF A TABLE ROW. Every caller that holds the whole materialised table
// (the composed JOIN filter, the RAM sort / limit rebuilds) says (table, index) and always did; the
// evaluation itself never wanted more than the row, so that is what it takes now and this is the
// one line that turns the pair into one.
RamTri RamEvalPredicate(const ibQueryPredicate* p, const ibQueryRamTable& t, long row)
{
	return RamEvalPredicate(p, ibRamTableRow(t, row));
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
ibValue EvalColumnExprRow(const ibQueryColumnExpr* e, const ibQueryRow& row)
{
	if (e == nullptr) return ibValue();
	switch (e->m_kind) {
		case ibQueryColumnExprKind::Column:
			// ⚠ A NAMED FIELD HAS NO MEANING IN RAM, and saying so is the point. A materialised row holds
			// the value WHOLE (one cell per column), never the field spread the DB path writes — so a
			// per-field expression asked here would have to answer with the whole value under a field's
			// name, and the caller would reassemble an object out of several copies of itself. The
			// expression is built per field precisely to be pushed to the server; if one reaches this
			// evaluator, the read took the join / multi-source road and the reading has to be split
			// differently, which is a defect to be told about rather than a case to fake.
			wxASSERT_MSG(e->m_field.IsEmpty(), wxT("a per-field column expression cannot be evaluated over a RAM row"));
			return e->m_col != nullptr && e->m_field.IsEmpty() ? row.Get(e->m_col) : ibValue();
		case ibQueryColumnExprKind::Const:  return e->m_const;
		case ibQueryColumnExprKind::Arith: {
			const ibNumber a = EvalColumnExprRow(e->m_lhs.get(), row).GetNumber();
			const ibNumber b = EvalColumnExprRow(e->m_rhs.get(), row).GetNumber();
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
				if (RamEvalPredicate(wt.first.get(), row) == RamTri::True) return EvalColumnExprRow(wt.second.get(), row);
			return e->m_else ? EvalColumnExprRow(e->m_else.get(), row) : ibValue();
		}
		case ibQueryColumnExprKind::PeriodTrunc: {
			// The RAM twin of the SQL truncation. It must agree with the dialect's expression to the
			// day, or a query answers differently depending on whether it happened to co-locate — so
			// this walks the calendar rather than approximating (no "30-day month" arithmetic).
			const ibValue v = EvalColumnExprRow(e->m_lhs.get(), row);
			return ibValue(ibTruncateToPeriod(v.GetDateTime(), e->m_periodUnit));
		}
		case ibQueryColumnExprKind::WindowAgg:
			// ⭐ A WINDOW CANNOT BE EVALUATED ON ONE ROW, and that is a fact about the thing rather than
			// a gap here: its value is a fold over the PARTITION this row belongs to, so a row alone
			// does not carry the answer. The expression is built only where the engine computes it
			// (ibDataQueryBuilder::CanPushWindow), and where it does not, the FOLD produces the figure
			// from the tree and no such expression exists. Reaching this point means one was built for
			// a read that then took the RAM road — worth being told about, not worth faking.
			wxFAIL_MSG(wxT("a window aggregate has no value on a single row — it belongs to the server road"));
			return ibValue();
	}
	return ibValue();
}

// The (table, index) spelling — see the note on RamEvalPredicate's twin above.
ibValue EvalColumnExprRow(const ibQueryColumnExpr* e, const ibQueryRamTable& t, long row)
{
	return EvalColumnExprRow(e, ibRamTableRow(t, row));
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
		case ibQueryColumnExprKind::PeriodTrunc: GatherColumnExprColumns(e->m_lhs.get(), add); break;
		// ⭐ THE PARTITION COUNTS TOO. A window names its input AND the levels it is computed over, and
		// every one of those columns has to reach the read — a partition key nobody projected is a
		// query that cannot be built. (Missing here, the figure would have come back partitioned by
		// nothing, which is a plausible number and the wrong one.)
		case ibQueryColumnExprKind::WindowAgg:
			GatherColumnExprColumns(e->m_lhs.get(), add);
			for (const ibQueryColumnExprPtr& key : e->m_partition)
				GatherColumnExprColumns(key.get(), add);
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

	// ⭐⭐ AND THE TOTALS' OWN COLUMNS RIDE TOO. The fold that builds the levels runs OVER THIS TABLE:
	// it reads each level's key and each resource straight out of the composed rows. A column nobody
	// listed here never reaches them, so the level groups every row under one empty key and the
	// resource counts rows whose values it cannot see — a figure with nothing under it to explain
	// where it came from (Max, 2026-08-22: "the detail records have to justify where that 63 came
	// from").
	//
	// It cost nothing to miss on a SINGLE source, where the fold reads the driver's own result and
	// may ask the source for any column it likes. The moment a second source joins, the rows are
	// composed HERE, and what was not asked for is simply not in them.
	if (spec.m_totals != nullptr)
		for (const ibTotalLevel& level : *spec.m_totals)
			for (const ibTotalField& field : level.m_fields)
				add(field.m_col);
	if (spec.m_totalAggregates != nullptr)
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_totalAggregates)
			add(a.m_col);                                  // null for COUNT(*) — `add` ignores it

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
                                const ibDataQuerySpec& spec, bool condsByName,
                                const std::vector<ibQueryCondition>* extra);

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
	//
	// SEMI-JOIN reduction rides along: before reading unit u, every edge to an ALREADY-materialised unit
	// hands down its key values, so u fetches only rows that can join. Sound unconditionally here —
	// FlattenInnerChain admits only INNER, non-cross, equi-KEY, non-computed ON, so a row the filter drops
	// could not have reached the result. Several edges compose (each is another AND-ed filter), and the
	// reduction is transitive: a unit reduced early makes the keys it hands on narrower still.
	//
	// The counts fed to PlanInnerJoinOrder stay EXACT — they are measured after the reduction, which is
	// what the planner should see anyway: reduced tables are the ones it will actually join.
	// Materialisation ORDER: computed / RAM units first. A computed source is built in memory whatever we
	// do and its provider ignores pushed conditions, so reading it early costs nothing — and once it is
	// read, its keys reduce every DB unit joined to it. Tree order is kept within each group; this decides
	// only what gets READ first, not the join order (PlanInnerJoinOrder below still owns that).
	std::vector<size_t> matOrder;
	matOrder.reserve(n);
	auto isComputedUnit = [&units](size_t u) {
		return units[u]->m_kind == ibQueryNode::Kind::Source && units[u]->m_queryable != nullptr
		    && units[u]->m_queryable->IsComputedInRam();
	};
	for (size_t u = 0; u < n; ++u) if (isComputedUnit(u)) matOrder.push_back(u);
	for (size_t u = 0; u < n; ++u) if (!isComputedUnit(u)) matOrder.push_back(u);

	std::vector<ibQueryRamTable> tables(n);
	std::vector<bool>            materialised(n, false);
	std::vector<long>            counts(n, 0);
	for (const size_t idx : matOrder) {
		std::vector<ibQueryCondition> extra;
		for (size_t e = 0; e < edges.size(); ++e) {
			size_t src = n;
			const ibBackendQueryColumn* mineKey = nullptr; const ibBackendQueryColumn* srcKey = nullptr;
			if (edges[e].second == idx && materialised[edges[e].first]) {
				src = edges[e].first;  srcKey = keyPairs[e].first;  mineKey = keyPairs[e].second;
			}
			else if (edges[e].first == idx && materialised[edges[e].second]) {
				src = edges[e].second; srcKey = keyPairs[e].second; mineKey = keyPairs[e].first;
			}
			if (src == n) continue;
			std::vector<ibValue> keys;
			if (CollectDistinctJoinKeys(tables[src], srcKey, keys))
				AppendSemiJoinCondition(mineKey, std::move(keys), extra);
		}
		tables[idx] = MaterialiseNode(units[idx], needed, spec, condsByName,
		                              extra.empty() ? nullptr : &extra);
		counts[idx] = tables[idx].RowCount();
		materialised[idx] = true;
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
//
// `extra` carries SEMI-JOIN key reductions down the recursion: conditions produced from an already-
// materialised side's join keys. They are routed by OWNERSHIP at the Source node — an extra whose column
// this leaf does not own is simply not its business — so a caller can hand the same set to a whole
// subtree without knowing which leaf inside it holds the key.
ibQueryRamTable MaterialiseNode(const ibQueryNode* node, const std::vector<const ibBackendQueryColumn*>& refCols,
                                const ibDataQuerySpec& spec, bool condsByName = false,
                                const std::vector<ibQueryCondition>* extra = nullptr)
{
	if (node->m_kind == ibQueryNode::Kind::Union)
		return RamUnion(spec, node, refCols);   // a UNION nested inside a JOIN subtree

	if (node->m_kind == ibQueryNode::Kind::Source) {
		std::vector<const ibBackendQueryColumn*> mine;
		for (const ibBackendQueryColumn* c : refCols)
			if (node->m_queryable->OwnsColumn(c))
				mine.push_back(c);
		// ⭐ WHAT THIS LEAF AGREED TO READ. A column no source claims is silently absent from the
		// composed rows, and everything downstream then reads it as empty — a dimension that folds
		// every row under one blank key, a figure with nothing under it to explain itself. The
		// refusal is invisible by construction, so the journal is where it becomes visible.
		//
		// ⚠ ORPHANED, not merely "not mine". A column the OTHER side of the join provides is normal
		// and says nothing; what is worth a line is a column NO leaf in the tree claims, because that
		// one reaches nobody. And the whole list is assembled inside the gate — walking refCols twice
		// to build strings is real work, and it must not happen when nobody is listening.
		ibJournalIf {
			wxString claimed, orphaned;
			for (const ibBackendQueryColumn* c : refCols) {
				if (node->m_queryable->OwnsColumn(c))
					claimed += (claimed.IsEmpty() ? wxString() : wxT(", ")) + c->GetName();
				else if (!SubtreeProvides(spec.m_root, c))
					orphaned += (orphaned.IsEmpty() ? wxString() : wxT(", ")) + c->GetName();
			}
			ibJournalInfo(wxT("query.compose"), wxT("source %s: claimed [%s]%s%s"),
				node->m_queryable->GetQueryName(),
				claimed,
				orphaned.IsEmpty() ? wxT("") : wxT("  ORPHANED ["),
				orphaned.IsEmpty() ? wxT("") : (orphaned + wxT("]")));
		}
		std::vector<ibQueryCondition> conds = condsByName
			? LeafConditionsByName(spec, node->m_queryable)
			: LeafConditions(spec, node->m_queryable);
		if (extra != nullptr)
			for (const ibQueryCondition& e : *extra)
				if (e.m_col != nullptr && node->m_queryable->OwnsColumn(e.m_col))
					conds.push_back(e);
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

	const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
	ResolveJoinKeys(node, onL, onR);   // explicit or derived (the tree was validated resolvable)

	// SEMI-JOIN reduction. One side is materialised first; its join-key values are then known, so the other
	// side reads only rows that can possibly join. A row the filter drops could not have appeared in the
	// result, so the answer is identical — only the read shrinks.
	//
	// WHICH side drives is the whole trick. Reducing costs nothing on a COMPUTED source (it is built in
	// memory either way and its provider ignores pushed conditions) and everything on a real DB read. So
	// when exactly one side is a computed leaf, materialise THAT one first and let its keys shrink the
	// other's scan — the DB⋈RAM case, where the whole complaint lives. Otherwise keep tree order.
	//
	// DIRECTION IS A CORRECTNESS GATE, not just a preference. For a LEFT join only left→right is sound:
	// the right is the null-producing side, so a right row matching no left key contributes nothing either
	// way, while the preserved left side must never lose a row. Driving from the right is therefore INNER
	// only. Never applied to RIGHT / FULL / cross, to a computed ON (no column key), or to a THETA ON —
	// an equality key set says nothing about which rows satisfy `a.x > b.y`.
	auto isComputedLeaf = [](const ibQueryNode* n) {
		return n != nullptr && n->m_kind == ibQueryNode::Kind::Source
		    && n->m_queryable != nullptr && n->m_queryable->IsComputedInRam();
	};
	const bool reducible = onL != nullptr && onR != nullptr && !node->m_on.m_cross
	                    && node->m_on.m_exprL == nullptr && node->m_on.m_op == ibJoinCompareOp::Eq;
	const bool driveFromRight = reducible && node->m_joinKind == ibQueryJoinKind::Inner
	                         && isComputedLeaf(node->m_right.get()) && !isComputedLeaf(node->m_left.get());

	std::vector<ibQueryCondition> passed;
	if (extra != nullptr)
		passed = *extra;

	ibQueryRamTable TL, TR;
	if (driveFromRight) {
		TR = MaterialiseNode(node->m_right.get(), refCols, spec, condsByName, extra);
		std::vector<ibValue> keys;
		if (CollectDistinctJoinKeys(TR, onR, keys))
			AppendSemiJoinCondition(onL, std::move(keys), passed);
		TL = MaterialiseNode(node->m_left.get(), refCols, spec, condsByName,
		                     passed.empty() ? nullptr : &passed);
	}
	else {
		TL = MaterialiseNode(node->m_left.get(), refCols, spec, condsByName, extra);
		if (reducible && (node->m_joinKind == ibQueryJoinKind::Inner || node->m_joinKind == ibQueryJoinKind::Left)) {
			std::vector<ibValue> keys;
			if (CollectDistinctJoinKeys(TL, onL, keys))
				AppendSemiJoinCondition(onR, std::move(keys), passed);
		}
		TR = MaterialiseNode(node->m_right.get(), refCols, spec, condsByName,
		                     passed.empty() ? nullptr : &passed);
	}

	std::vector<const ibBackendQueryColumn*> outCols;     // provided cols, with their side
	std::vector<bool> fromLeft;
	for (const ibBackendQueryColumn* c : refCols) {
		if      (SubtreeProvides(node->m_left.get(),  c)) { outCols.push_back(c); fromLeft.push_back(true);  }
		else if (SubtreeProvides(node->m_right.get(), c)) { outCols.push_back(c); fromLeft.push_back(false); }
	}

	return ibQueryComposer::JoinRamTables(TL, TR, onL, onR, outCols, fromLeft, node->m_joinKind, node->m_on);
}

// Finalise a GetColumnId-keyed combined table into the result: ORDER BY (RAM stable
// sort over m_sorts, each key read by GetColumnId), project the select-list (alias-keyed
// output), and LIMIT to the page count. One finaliser for both JOIN and UNION.
ibDataQueryResult ProjectToAliases(const ibQueryRamTable& TC, const ibDataQuerySpec& spec, const ibReadPageRequest& page)
{
	const long rows = TC.RowCount();

	// (The journal line about what was composed and what is carried out of it is written BELOW,
	// after the carried set is complete — see the end of the column loop. Written here it reported
	// the SELECT list alone and read as if the totals' columns had been dropped, which was true of
	// the code for one afternoon and false of it afterwards: a diagnostic that keeps saying the old
	// answer is worse than none.)

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

	// ⭐⭐ KEYED BY THE COLUMN ITSELF, the way the UNION stitch below already keys its output. The
	// selection that reads this table asks for a cell BY COLUMN — `m_table.GetCell(row,
	// col->GetColumnId())` — so an output keyed by a running number answers EVERY such question with
	// nothing: add a second source to a query and its dimensions came back empty while the row count
	// stayed right, because COUNT counts rows and never asks for a value (Max, 2026-08-22, adding a
	// constant to a document query: "the dimensions broke"). The figure was left with nothing under
	// it to explain where it came from, which is what detail records are FOR.
	//
	// The running number was not pointless: two DIFFERENT columns can share a metaID — a self-join
	// selects `T AS a` and `T AS b`, and keying both by their own id would fold them into one cell.
	// So the id is used where it is UNIQUE in this output, and a synthetic one stands in for the
	// duplicates. Those keep answering by ALIAS, which is the only thing that told them apart before.
	ibQueryRamTable TO;
	std::vector<ibMetaID> outIds;
	std::map<ibMetaID, int> ownCount;
	for (const auto& s : *spec.m_selectCols)
		if (s.first != nullptr)
			++ownCount[s.first->GetColumnId()];

	ibMetaID seq = kStitchSyntheticBase;
	std::vector<const ibBackendQueryColumn*> outCols;
	for (const auto& s : *spec.m_selectCols) {
		const ibMetaID own = s.first != nullptr ? s.first->GetColumnId() : 0;
		const ibMetaID id  = (own != 0 && ownCount[own] == 1) ? own : seq++;
		TO.AddColumn(id, s.second, s.first->GetTypeDesc());    // named by alias, as before
		outIds.push_back(id);
		outCols.push_back(s.first);
	}

	// ⭐⭐ AND EVERY COLUMN THE TOTALS WILL BE ASKED FOR, whether or not the SELECT list names it.
	// The fold runs over the RESULT of this projection, reading each level's key and each resource
	// by column; a column the composed table holds but this list does not carry is dropped right
	// here, and the fold then finds nothing where the value was (journal, 2026-08-22: "composed 63
	// rows, holds [Value, Posted, Number], output wants [Value]").
	//
	// They can be missing from the SELECT list for a perfectly ordinary reason: a projection whose
	// NAME a dimension or a resource already claimed is not projected twice — one name, one column,
	// which is the rule everywhere else — so `SELECT Posted, Number … TOTALS COUNT(Number) BY Posted`
	// projects neither of them under its own name. On a single source that cost nothing: the fold
	// reads the driver's result and may ask it for any column. Composed rows have only what was put
	// in them.
	// ⚠ THE TEST IS THE ID, NOT THE POINTER. What the fold asks for is "a column keyed by this
	// metaID"; whether some other entry happens to be the same C++ object is a different question
	// and answers it wrongly. `SELECT a.Number AS N1, a.Number AS N2 … BY Number` puts the same
	// pointer in twice, so both select entries take SYNTHETIC ids (the id is not unique) — and a
	// pointer test would then see the column as "already carried" and never add it under its own id,
	// leaving the fold with nothing and every row in one blank group.
	auto carriesId = [&outIds](ibMetaID id) {
		for (const ibMetaID had : outIds)
			if (had == id)
				return true;
		return false;
	};
	auto carry = [&](const ibBackendQueryColumn* c) {
		if (c == nullptr)
			return;
		const ibMetaID own = c->GetColumnId();
		if (own != 0 && carriesId(own))
			return;                       // its own id is already out there — the fold will find it
		const ibMetaID id = (own != 0) ? own : seq++;
		TO.AddColumn(id, c->GetName(), c->GetTypeDesc());
		outIds.push_back(id);
		outCols.push_back(c);
	};
	if (spec.m_totals != nullptr)
		for (const ibTotalLevel& level : *spec.m_totals)
			for (const ibTotalField& field : level.m_fields)
				carry(field.m_col);
	if (spec.m_totalAggregates != nullptr)
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_totalAggregates)
			carry(a.m_col);

	// ⭐⭐ DISTINCT AND A TOTAL OVER A FIELD THE SELECT DOES NOT NAME CANNOT BOTH BE HONOURED, and the
	// combination is refused rather than answered.
	//
	// DISTINCT collapses rows by what the author selected. A resource over a column outside that list
	// then sums whichever row survived the collapse — one arbitrary row per duplicate group. On a
	// query that fans out (`SELECT DISTINCT Doc.Posted … JOIN Lines … TOTALS SUM(Lines.Amount)`) the
	// figure comes back looking perfectly reasonable and being the amount of one line instead of all
	// of them.
	//
	// Refusing is the only answer that keeps both promises. Deduping over the carried columns instead
	// would silently change what DISTINCT means — rows the author's list cannot tell apart would stop
	// collapsing — and answering with the arbitrary row is the failure this arc spent the day
	// removing: a wrong number that looks right is worse than a missing one, because nobody checks it.
	if (spec.m_distinct && outCols.size() > spec.m_selectCols->size())
		ibBackendCoreException::Error(
			_("SELECT DISTINCT cannot be totalled over a field the selection does not carry: "
			  "DISTINCT folds rows by the selected fields, so a resource over any other field would "
			  "add up one arbitrary row per fold. Add the field to SELECT, or drop DISTINCT."));

	// ⭐ WHAT THE COMPOSED TABLE HOLDS, against what leaves it. Between the leaves claiming their
	// columns and the fold reading them there are two silent filters — the join's own column list
	// and this projection — and a column dropped by either is indistinguishable downstream from a
	// column that was read and found empty.
	ibJournalIf {
		wxString have;
		for (const ibQueryRamColumn& c : TC.Columns())
			have += (have.IsEmpty() ? wxString() : wxT(", "))
			      + wxString::Format(wxT("%s#%u"), c.m_name, static_cast<unsigned>(c.m_id));
		wxString out;
		for (size_t k = 0; k < outCols.size(); ++k)
			if (outCols[k] != nullptr)
				out += (out.IsEmpty() ? wxString() : wxT(", "))
				     + wxString::Format(wxT("%s#%u"), outCols[k]->GetName(), static_cast<unsigned>(outIds[k]));
		ibJournalInfo(wxT("query.stitch"),
			wxT("IN MEMORY: %ld composed rows, holds [%s]  carries out [%s]"),
			rows, have, out);
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
	// The output cells ARE the row identity — they are already a vector of values,
	// so the fold below inserts them directly instead of rendering them to text.
	std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual> seenDistinct;
	long emitted = 0;
	for (long oi = 0; oi < rows && emitted < limit; ++oi) {
		const long i = order[static_cast<size_t>(oi)];
		// Every carried column, in the order `outIds` names them — the SELECT list first, then the
		// totals' own columns appended above.
		std::vector<ibValue> outCells;
		outCells.reserve(outCols.size() + exprs.size());
		for (const ibBackendQueryColumn* c : outCols)
			outCells.push_back(RamCell(TC, i, c));
		for (const ibQueryColumnSelect& sc : exprs)
			outCells.push_back(EvalColumnExprRow(sc.m_expr.get(), TC, i));

		// ⚠ DISTINCT IS OVER WHAT THE QUERY SELECTED, not over what the fold needs. The totals'
		// columns ride along invisibly; letting them into the row identity would make two rows
		// that the author's SELECT list cannot tell apart count as different, which is exactly what
		// DISTINCT is asked to prevent.
		if (spec.m_distinct) {
			std::vector<ibValue> identity;
			identity.reserve(spec.m_selectCols->size() + exprs.size());
			identity.insert(identity.end(), outCells.begin(),
				outCells.begin() + static_cast<long>(spec.m_selectCols->size()));
			identity.insert(identity.end(), outCells.begin() + static_cast<long>(outCols.size()), outCells.end());
			if (!seenDistinct.insert(identity).second)
				continue;   // duplicate output row -> drop
		}
		const long r = TO.AppendRow();
		for (size_t k = 0; k < outIds.size();  ++k) TO.SetCell(r, outIds[k],  outCells[k]);
		for (size_t k = 0; k < exprIds.size(); ++k) TO.SetCell(r, exprIds[k], outCells[outCols.size() + k]);
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
	// A COMPUTED group key reads columns of its own (the period a truncation is applied to). They
	// must ride into the composed table or the fold would evaluate the expression against a table
	// that does not carry its input.
	if (spec.m_groupExprs != nullptr)
		for (const ibQueryColumnExprPtr& e : *spec.m_groupExprs)
			if (e) GatherColumnExprColumns(e.get(), add);
	for (const auto& a : *spec.m_aggregates)                     add(a.m_col);
	for (const ibQueryCondition& c : *spec.m_conditions)         add(c.m_col);
	CollectJoinKeys(spec.m_root, cols);
	return cols;
}

// ⭐⭐ AN AGGREGATE IS AN ACCUMULATOR — it always was, and holding the rows was how it hid.
//
// SUM, COUNT, AVG, MIN, MAX all fold one row at a time and forget it; the only reason this used to
// take a bucket (a table + a list of row indices) is that the rows happened to be lying there. Said
// as a state that rows are FED into, the same arithmetic serves BOTH folds: the one that already has
// its rows (a composed JOIN result — AggregateOne below feeds them in a loop) and the streaming one,
// which never holds a row longer than the moment it reads it.
//
// One statement of the semantics, and it is the awkward half that makes this matter: every column
// aggregate IGNORES NULL operands (SQL), COUNT(col) counts non-null rows, AVG divides by the
// non-null count, MIN/MAX skip NULL, and DISTINCT folds each different VALUE once — keyed by the
// value's identity, never by its presentation (two references display the same string far more
// often than they are the same reference, and a display-keyed count would quietly merge them).
// Two copies of that is two answers waiting to disagree.
struct ibAggAcc
{
	ibNumber m_sum{ 0 };
	long     m_n     = 0;      // operands folded (non-null, DISTINCT-filtered) — COUNT / the AVG divisor
	long     m_rows  = 0;      // rows fed, null or not — COUNT(*), which counts rows and not values
	ibValue  m_best;
	bool     m_have  = false;
	// DISTINCT only — the one place memory grows with the DATA rather than with the groups, and it
	// grows with the number of DIFFERENT VALUES in a group, which is what DISTINCT means.
	std::unordered_set<ibValue, ibValueHash, ibValueEqual> m_seen;

	void Feed(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRow& row)
	{
		using Fn = ibDataQueryBuilder::AggregateFn;
		++m_rows;
		// COUNT(*) — no source column AND no computed input — counts the row itself; there is no value to read.
		if (a.m_col == nullptr && !a.m_expr)
			return;
		// A COMPUTED aggregate input (SUM(Qty * Price), MAX(CASE …)) evaluates its expression per row;
		// a plain input reads its source column. Same RAM expr evaluator the JOIN stitch uses.
		const ibValue v = a.m_expr ? EvalColumnExprRow(a.m_expr.get(), row) : row.Get(a.m_col);
		if (RamIsNullValue(v))
			return;
		if (a.m_distinct && !m_seen.insert(v).second)
			return;   // already folded this value
		switch (a.m_fn) {
		case Fn::Count:             ++m_n; break;
		case Fn::Sum: case Fn::Avg: m_sum = m_sum + v.GetNumber(); ++m_n; break;
		case Fn::Min: if (!m_have || v < m_best) { m_best = v; m_have = true; } break;
		case Fn::Max: if (!m_have || v > m_best) { m_best = v; m_have = true; } break;
		default: break;
		}
	}

	ibValue Result(const ibDataQueryBuilder::AggregateItem& a) const
	{
		using Fn = ibDataQueryBuilder::AggregateFn;
		switch (a.m_fn) {
		case Fn::Count: return ibValue(ibNumber((a.m_col == nullptr && !a.m_expr) ? m_rows : m_n));
		case Fn::Sum: return ibValue(m_sum);
		case Fn::Avg: return m_n > 0 ? ibValue(m_sum / ibNumber(m_n)) : ibValue();
		case Fn::Min: case Fn::Max: return m_best;
		default: return ibValue();
		}
	}
};

// One aggregate over the rows of a bucket — the accumulator above, fed the rows the caller already
// holds. (column read by GetColumnId.)
ibValue AggregateOne(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRamTable& TC, const std::vector<long>& idx)
{
	ibAggAcc acc;
	for (long i : idx)
		acc.Feed(a, ibRamTableRow(TC, i));
	return acc.Result(a);
}

// Receiver id for a COUNT(*) aggregate (no source column): a synthetic id far from any real metaID,
// keyed by the aggregate's position, read back by GetColumn(alias). Column aggregates roll in-place.
const ibMetaID kAggSyntheticBase = 0x40000000u;

// ⭐⭐ DOES THIS AGGREGATE NEED A SLOT OF ITS OWN — i.e. one that is not its source column's?
//
// Two reasons, and they are the same reason seen twice: THERE IS NOTHING TO ROLL IN PLACE INTO.
//
//   * COUNT(*) has no source column at all;
//   * an aggregate with an AREA that did not get a receiver — folded here rather than by the server.
//     Its figure is NOT the column's roll-up: ApplyScopedAggregates keeps it at the named level and
//     CARRIES IT DOWN, so writing it in place overwrites the column under the whole area. A detail
//     row then shows its item's 30 where its own 15 belongs (CI, 2026-08-27), and if a ladder
//     aggregate names that column too, its every subtotal goes the same way.
//
// ⚠ The lowering already re-projects a repeated column so two aggregates do not share a slot — but
// only when it is SCALAR (a reference cannot be re-projected as one column), and only through that
// one road: this seam is public and takes the items it is handed. The rule belongs where the slot is
// decided, not with whoever happened to build the list.
//
// ⚠ ASKED ONCE, so the slot and the COLUMN declared for it (AddSyntheticAggColumns) cannot disagree:
// a figure declared under one name and written under another reads back as nothing at all.
bool AggNeedsOwnSlot(const ibDataQueryBuilder::AggregateItem& agg)
{
	return agg.m_col == nullptr || (agg.m_scopeDepth > 0 && agg.m_ownedReceiver == nullptr);
}

// WHERE AN AGGREGATE'S FIGURE LANDS — its OWN column when it rolls in place, its synthetic receiver
// otherwise. Asked in one place because both folds (bucketed and streaming) write the same slots,
// and a second answer to this is a figure that reads back under a different name than it was written.
ibMetaID AggSlotId(const std::vector<ibDataQueryBuilder::AggregateItem>& aggs, size_t i)
{
	return AggNeedsOwnSlot(aggs[i]) ? (kAggSyntheticBase + static_cast<ibMetaID>(i))
	                                : aggs[i].m_col->GetColumnId();
}

// Roll the aggregates over `rows` and write each onto the node: a COLUMN aggregate IN-PLACE into its
// own column (a.m_col) — so it reads as the row value on a leaf and the SUBTOTAL on a group node
// (GetValue(col)); a COUNT(*) into its synthetic receiver (read by GetColumn(alias)).
void ApplyAggregates(ibSelectorTree::Node& node, const ibQueryRamTable& TC, const std::vector<long>& rows,
                     const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i)
		node.m_values[AggSlotId(aggs, i)] = AggregateOne(aggs[i], TC, rows);
}

// ⭐⭐ THE FIGURES THAT BELONG TO ONE LEVEL — `TOTALS SUM(x) OVER Item`.
//
// An aggregate with an area is folded exactly like any other: every node on the path already holds
// its own roll-up of the rows beneath it. What makes it different is only WHICH of those numbers is
// the answer — the one computed at the named level. So this is a single pass over the finished tree
// rather than a second kind of fold:
//
//   * ON that level — keep what is there;
//   * BELOW it — carry that value down, unchanged. Inside the area the figure IS constant, which is
//     what lets a detail row show the share's denominator;
//   * ABOVE it, or in another branch — ERASE it. Several values live up there and any one of them
//     would be a lie. ⚠ Erased, NOT zeroed: a zero joins sums as an addend and divides as a
//     denominator, while an absent value reads back as the runtime's NULL — "no such figure here".
//
// Done after the fold, so both roads (streaming and snapshot) get it from one place, and so a tree
// the DBMS folded would be treated the same the day the push-down learns to bring one.
void ApplyScopedAggregates(ibSelectorTree& tree, const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i) {
		const ibDataQueryBuilder::AggregateItem& agg = aggs[i];
		if (agg.m_scopeDepth <= 0)
			continue;                                   // area comes from the ladder — nothing to fix up
		const ibMetaID slot = AggSlotId(aggs, i);
		const wxString branch = agg.m_scopeBranch ? agg.m_scopeBranch->m_name : wxString();

		// `carried` = the value of the nearest ancestor that WAS the area, or null while above it.
		const std::function<void(ibSelectorTree::Node&, const ibValue*)> walk =
			[&](ibSelectorTree::Node& node, const ibValue* carried) {
				const ibValue* pass = carried;
				if (carried != nullptr) {
					node.m_values[slot] = *carried;     // below the area — the area's own figure
				}
				else if (node.m_level == agg.m_scopeDepth && node.m_branch.IsSameAs(branch, false)
				         && node.m_kind != ibSelectorNodeKind::Branch) {
					const auto it = node.m_values.find(slot);
					if (it != node.m_values.end())
						pass = &it->second;             // this IS the area — keep it, hand it down
				}
				else {
					node.m_values.erase(slot);          // above it, or another branch: no such figure
				}
				for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
					if (child != nullptr)
						walk(*child, pass);
			};
		walk(tree.Root(), nullptr);
	}
}

// Add the synthetic receiver COLUMNS to a tree — one for every aggregate that does not roll into a
// column of its own (AggNeedsOwnSlot: a COUNT(*), or an area folded here for want of windows). An
// aggregate that DOES roll in place needs none: its column is already present. Read by GetColumn(alias).
void AddSyntheticAggColumns(ibSelectorTree& tree, const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i)
		if (AggNeedsOwnSlot(aggs[i]))
			tree.AddColumn(AggSlotId(aggs, i), aggs[i].m_alias, ibTypeDescription());
}

// CellKey is GONE. Hierarchy linking keys by the VALUE itself now (ibValueHash / ibValueEqual,
// value.h): a REFERENCE compares by its _RRRef there — a row's own data-reference and a parent-ref
// pointing AT it are the same value, which is what the link needs — and no cell is rendered to text
// to say so. (docs/query-language-arc.md §22.1b)

// Context for the recursive hierarchy fold (invariant across the recursion).
// ---------------------------------------------------------------------------
// VALUE-KEYED INDEXES, all of them, in one place.
//
// Every hierarchy / grouping structure below keys by the VALUE itself through ibValueHash +
// ibValueEqual (value.h) — a reference matches by its guid there, which is what these need, and
// nothing is rendered to text per row to say so.
using ibRowsByValue    = std::unordered_map<ibValue, std::vector<long>,    ibValueHash, ibValueEqual>;
using ibRefChildren    = std::unordered_map<ibValue, std::vector<ibValue>, ibValueHash, ibValueEqual>;

// A LEVEL'S KEY — the values of its fields, in the level's own order. One field is the ordinary
// case and stays a one-element key rather than a shape of its own, so nothing downstream branches
// on how many fields a level has.
using ibLevelKey = std::vector<ibValue>;

struct ibLevelKeyHash
{
	std::size_t operator()(const ibLevelKey& key) const
	{
		// Mixed in 64 bits and folded down at the end: on a 32-bit build std::size_t is half the
		// width, and mixing IN it silently throws away the upper half of every value's hash.
		const ibValueHash one;
		wxULongLong_t h = 0xcbf29ce484222325ull;
		for (const ibValue& value : key) {
			h ^= static_cast<wxULongLong_t>(one(value));
			h *= 0x100000001b3ull;
		}
		return static_cast<std::size_t>(h ^ (h >> 32));
	}
};

struct ibLevelKeyEqual
{
	bool operator()(const ibLevelKey& left, const ibLevelKey& right) const
	{
		if (left.size() != right.size()) return false;
		const ibValueEqual same;
		for (std::size_t i = 0; i < left.size(); ++i)
			if (!same(left[i], right[i])) return false;
		return true;
	}
};

using ibRowsByLevelKey = std::unordered_map<ibLevelKey, std::vector<long>, ibLevelKeyHash, ibLevelKeyEqual>;
using ibValueSeen      = std::unordered_map<ibValue, char,                 ibValueHash, ibValueEqual>;
using ibValueParentMap = std::unordered_map<ibValue, ibValue,              ibValueHash, ibValueEqual>;

// "No parent" — the row is a root. An absent value, an SQL null, or an empty string: the same three
// the old string key collapsed to "" and tested with .empty().
inline bool IsNoKey(const ibValue& v)
{
	return v.IsEmpty() || v.IsNull()
		|| (v.GetType() == ibValueTypes::TYPE_STRING && v.GetString().IsEmpty());
}

struct HierBuildCtx {
	const ibQueryRamTable*                               detail;
	const ibBackendQueryColumn*                          rowKeyCol;
	const ibRowsByValue*                                childrenOf;
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

	const ibValue key = ctx.detail->GetCell(rowIdx, ctx.rowKeyCol->GetColumnId());
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

	// One group key slot: a plain COLUMN, or a COMPUTED expression (GroupByExpr — e.g. the period
	// truncated to month). The RAM fold has to honour computed keys for the same reason the SQL path
	// does: if it silently grouped by the raw column instead, a multi-source read would return one
	// group per distinct instant while a single-source read returned one per month — the same query
	// answering differently depending on whether it happened to co-locate.
	const size_t groupCount = spec.m_groupBy->size();
	auto groupExprAt = [&](size_t gi) -> const ibQueryColumnExpr* {
		if (spec.m_groupExprs == nullptr || gi >= spec.m_groupExprs->size()) return nullptr;
		return (*spec.m_groupExprs)[gi].get();
	};
	auto groupCell = [&](size_t gi, long row) -> ibValue {
		if (const ibQueryColumnExpr* e = groupExprAt(gi))
			return EvalColumnExprRow(e, TC, row);
		return RamCell(TC, row, (*spec.m_groupBy)[gi]);
	};

	// A GROUP KEY IS THE TUPLE OF ITS GROUP CELLS — see ibValueSeqHash (value.h).
	// Folding it into one string through GetHashKey cost a text conversion per
	// cell per row (a number through ToString, a reference through Format) and
	// then keyed a tree that compared those strings character by character. Two
	// references that display alike still do not merge: the values are compared,
	// and a reference compares by guid.
	std::vector<std::vector<ibValue>> keyOrder;        // first-seen group order
	std::unordered_map<std::vector<ibValue>, std::vector<long>,
	                   ibValueSeqHash, ibValueSeqEqual> buckets;
	for (long i = 0; i < rows; ++i) {
		std::vector<ibValue> key;
		key.reserve(groupCount);
		for (size_t gi = 0; gi < groupCount; ++gi)
			key.push_back(groupCell(gi, i));
		auto it = buckets.find(key);
		if (it == buckets.end()) { keyOrder.push_back(key); buckets.emplace(std::move(key), std::vector<long>{ i }); }
		else it->second.push_back(i);
	}
	if (spec.m_groupBy->empty() && rows > 0) {          // aggregate with no GROUP BY = one bucket
		std::vector<long> all; all.reserve(static_cast<size_t>(rows));
		for (long i = 0; i < rows; ++i) all.push_back(i);
		buckets.emplace(std::vector<ibValue>(), all); keyOrder.push_back(std::vector<ibValue>());
	}

	// A computed key has no model column to take an id / name from, so it gets a synthetic id in its
	// own band and is read back by ALIAS — the same arrangement the aggregates already use.
	const ibMetaID grpExprBaseId = 0x50000000u;
	ibQueryRamTable TO;
	{
		ibMetaID gid = grpExprBaseId;
		for (size_t gi = 0; gi < groupCount; ++gi) {
			if (groupExprAt(gi) != nullptr)
				TO.AddColumn(gid++, (*spec.m_groupAliases)[gi], ibTypeDescription());
			else {
				const ibBackendQueryColumn* g = (*spec.m_groupBy)[gi];
				TO.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
			}
		}
	}
	const ibMetaID aggBaseId = 0x40000000u;             // far from any metaID — aggregates read by alias NAME
	{
		ibMetaID aggId = aggBaseId;
		for (const auto& a : *spec.m_aggregates)
			TO.AddColumn(aggId++, a.m_alias, a.m_col != nullptr ? a.m_col->GetTypeDesc() : ibTypeDescription());
	}

	for (const std::vector<ibValue>& key : keyOrder) {
		if (spec.m_topCount > 0 && TO.RowCount() >= spec.m_topCount)
			break;   // SELECT TOP n + GROUP BY — cap the folded groups (first-seen order)
		const std::vector<long>& idx = buckets[key];
		if (spec.m_having != nullptr && !PassesHaving(*spec.m_having, TC, idx))
			continue;   // HAVING drops this group (the register can't apply it; the RAM fold does)
		const long r = TO.AppendRow();
		{
			ibMetaID gid = grpExprBaseId;
			for (size_t gi = 0; gi < groupCount; ++gi) {
				// Every row in the bucket shares the key by construction, so the first one carries it.
				if (groupExprAt(gi) != nullptr)
					TO.SetCell(r, gid++, groupCell(gi, idx.front()));
				else
					TO.SetCell(r, (*spec.m_groupBy)[gi]->GetColumnId(), groupCell(gi, idx.front()));
			}
		}
		ibMetaID aId = aggBaseId;
		for (const auto& a : *spec.m_aggregates)
			TO.SetCell(r, aId++, AggregateOne(a, TC, idx));
	}
	return ibDataQueryResult(std::move(TO), spec.m_queryable);
}

// (The recursive multi-level totals fold that stood here is gone: it bucketed the rows level by
// level to build what one streaming descent builds — see ibStreamingFold, further down.)

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
	//
	// ⭐ AND THE VERDICT IS WRITTEN DOWN. This is the other place a query quietly changes cost without
	// changing its answer: a computed leaf either travels into a DB temp table and the join happens in
	// the database, or it stays in memory and the join is stitched here. Nobody can tell from the
	// outside which happened, and the difference on a large leaf is the whole query.
	if (!WorthDbTemp(rows.RowCount())) {
		ibJournalInfo(wxT("query.join"), wxT("IN MEMORY: computed leaf %s has %ld row(s) ")
			wxT("- not worth a DB temp table, joining in memory"),
			computed->GetQueryName(), rows.RowCount());
		return nullptr;
	}

	std::unique_ptr<ibTempTableManager> mgr =
		ibTempTableManager::Materialise(spec.m_holder, rows, computed->GetMetaData());
	if (!mgr) {
		ibJournalInfo(wxT("query.join"), wxT("IN MEMORY: computed leaf %s (%ld row(s)) ")
			wxT("could not be written to a DB temp table, joining in memory"),
			computed->GetQueryName(), rows.RowCount());
		return nullptr;
	}
	ibJournalInfo(wxT("query.join"), wxT("IN THE DATABASE: computed leaf %s (%ld row(s)) ")
		wxT("promoted to a temp table, the join runs there"),
		computed->GetQueryName(), rows.RowCount());
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
	if (ibDbTableProvider::CanColocateJoin(spec)) {
		ibJournalInfo(wxT("query.road"), wxT("SERVER: join co-located into one SELECT"));
		return ibDbTableProvider::ExecuteColocatedJoin(spec, page);
	}

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

	// Every server-side shape above was refused: the leaves are read whole and stitched here. The
	// per-leaf lines below say how much that cost (MaterialiseLeafToRam); this one says the road.
	ibJournalInfo(wxT("query.road"), wxT("RAM: %s stitched in memory"),
	              root->m_kind == ibQueryNode::Kind::Union ? wxT("union") : wxT("join"));

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
	// The single-source twin of PromoteComputedLeaf, and it used to make the same three decisions in
	// silence. Same words as its neighbour: what happened, to what, and how much of it there was.
	if (!WorthDbTemp(rows.RowCount())) {
		ibJournalInfo(wxT("query.temp"), wxT("IN MEMORY: computed source %s has %ld row(s) ")
			wxT("- not worth a DB temp table, folding in memory"),
			q->GetQueryName(), rows.RowCount());
		return nullptr;
	}
	std::unique_ptr<ibTempTableManager> mgr = ibTempTableManager::Materialise(spec.m_holder, rows, q->GetMetaData());
	if (!mgr) {
		ibJournalInfo(wxT("query.temp"), wxT("IN MEMORY: computed source %s (%ld row(s)) ")
			wxT("could not be written to a DB temp table, folding in memory"),
			q->GetQueryName(), rows.RowCount());
		return nullptr;
	}
	ibJournalInfo(wxT("query.temp"), wxT("IN THE DATABASE: computed source %s (%ld row(s)) ")
		wxT("promoted to a temp table, the aggregate runs there"),
		q->GetQueryName(), rows.RowCount());
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
	ibJournalInfo(wxT("query.road"), wxT("RAM: aggregate folded in memory over '%s' - %ld rows read"),
	              spec.m_queryable != nullptr ? spec.m_queryable->GetQueryName() : wxString(wxT("<none>")),
	              rows.RowCount());
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

namespace {

// ⭐ WHAT A LEVEL FIELD CONTRIBUTES TO THE KEY — its cell, or the START OF THE PERIOD containing it
// when the field is read BY PERIODS. `ibTruncateToPeriod` is the RAM twin of the expression the SQL
// road groups by, walking the calendar rather than approximating it — so a row lands in the same
// bucket whichever road read it, which is the only way two roads may exist at all.
ibValue LevelKeyValue(const ibTotalField& field, const ibQueryRow& row)
{
	const ibValue v = row.Get(field.m_col);
	if (!field.ByPeriods() || v.GetType() != TYPE_DATE)
		return v;
	return ibValue(ibTruncateToPeriod(v.GetDateTime(), field.m_periods->m_unit));
}

// ⭐⭐ THE STREAMING FOLD — rows in, tree out, ONE PASS, and nothing kept but the tree.
//
// The recursive fold this replaces bucketed the rows level by level: a vector of row indices per
// group, per level, over a snapshot that was itself the whole detail. It read every row exactly once
// per level and never looked back, which is to say it never needed any of it — the shape was there
// because the rows were.
//
// Fed a row at a time, the same tree comes out of ONE descent: the row's key at each level names the
// node it belongs to (opened on first sight, so groups keep first-seen order exactly as before), and
// every node on the path takes the row into its ACCUMULATORS. Memory is then a function of the
// number of GROUPS — the composition's acceptance criterion — and the detail rows are never all in
// hand at once.
//
// Two nodes are NOT held here: a DETAIL leaf (a level with no fields is one node per row — the
// report asked to print the rows, so it is paying for rows on purpose) and the tree itself.
class ibStreamingFold
{
public:
	// ⭐ ONE LADDER OF LEVELS — the common ones, or one branch's. Everything the fold used to ask of
	// the whole list is asked of this instead, which is what lets a branch be a cross-table of its
	// own: where the columns start and whether the last level is the records are facts about a
	// LADDER, and a branch is one.
	struct Section {
		const ibTotalBranch* m_branch = nullptr;   // null = the common ladder
		std::size_t m_from = 0, m_to = 0;          // its levels, as a half-open range of m_levels
		std::size_t m_across = 0;                  // the first of them that reads ACROSS the page
		std::size_t m_depth  = 1;                  // the DEPTH its first level stands at
		bool        m_details = false;             // its last level is the records…
		bool        m_detailsAcross = false;       // …and they read across rather than down

		// ⚠ DEPTH IS COUNTED WITHIN THE LADDER, not by position in the flat list. Two branches begin
		// at the same place, so their first levels must carry the same number — otherwise the second
		// branch would print one rung deeper than the first for no reason a reader could see.
		int DepthOf(std::size_t li) const { return static_cast<int>(m_depth + (li - m_from)); }
		int DepthOfDetails() const { return static_cast<int>(m_depth + (m_to - m_from) - 1); }
	};

	// `identity` = the column that IS the row (the source's single reference key), 0 when it has none.
	// The snapshot fold reads it off the source into its DimCtx; this one is told, for the same use.
	ibStreamingFold(ibSelectorTree& tree, const std::vector<ibTotalLevel>& levels,
	                const std::vector<ibDataQueryBuilder::AggregateItem>& aggs,
	                const std::vector<ibQueryRamColumn>& rowColumns, ibMetaID identity = 0)
		: m_levels(levels), m_aggs(aggs), m_rowColumns(rowColumns), m_identity(identity)
	{
		// WHERE THE PAGE STOPS AND THE PAGE-WIDTH STARTS — asked of the levels, which say which way
		// they read. Everything from here on is a column key of a cross-table.
		// ⚠ THE DETAIL LEVEL COUNTS HERE TOO. A column axis made of NOTHING BUT records — "the
		// resources laid out across the page, one column per row" — starts at that level, and
		// skipping it because it has no fields would leave the axis unfound.
		// ⭐⭐ THE LEVELS FALL INTO SECTIONS, AND A REPORT WITHOUT `SPLIT` HAS EXACTLY ONE. A section is
		// a ladder: the levels that fold one after another under a shared head. Written without
		// branches — every report there has ever been — the whole list is that one section and every
		// question below is asked of it, so nothing about the old road changes shape.
		//
		// `SPLIT` opens the next one. The common levels come first (they carry no branch), and each
		// branch's levels follow as a section of their own — which is what lets the same rows be
		// folded by characteristic on one side and by series on the other.
		for (std::size_t li = 0; li < m_levels.size(); ++li) {
			const ibTotalBranch* branch = m_levels[li].m_branch.get();
			if (m_sections.empty() || branch != m_sections.back().m_branch)
				m_sections.push_back(Section{ branch, li, li });
			m_sections.back().m_to = li + 1;
		}
		// ⚠ THE COMMON SECTION EXISTS EVEN WHEN NOTHING IS COMMON. A query may fork straight away —
		// `TOTALS SUM(x) BY SPLIT Item SPLIT Unit`, no shared level at all — and there the first
		// section found above is already a BRANCH. Read as the common ladder it would make branch one
		// the trunk and hang branch two INSIDE it, which is the opposite of what was asked. So an
		// empty common section is put in front: every branch then forks from the root, where they all
		// belong. (An empty list — `TOTALS BY OVERALL` — lands here too, and one empty section is
		// exactly right for it.)
		if (m_sections.empty() || m_sections.front().m_branch != nullptr)
			m_sections.insert(m_sections.begin(), Section{ nullptr, 0, 0 });

		// EACH SECTION READS ITSELF THE SAME WAY THE WHOLE LIST USED TO. Where the page stops and the
		// page-width starts, and whether the last level is the records: both are facts about a
		// LADDER, and a branch is a ladder — so a branch may be a cross-table of its own, which is
		// the point of asking this per section rather than once for the query.
		//
		// ⚠ THE DETAIL LEVEL COUNTS TOWARDS THE COLUMN AXIS TOO. A column axis made of NOTHING BUT
		// records — "the resources laid out across the page, one column per row" — starts at that
		// level, and skipping it because it has no fields would leave the axis unfound.
		for (Section& s : m_sections) {
			s.m_across = s.m_to;
			for (std::size_t li = s.m_from; li < s.m_to; ++li)
				if (m_levels[li].m_axis == ibTotalsAxis::Columns) { s.m_across = li; break; }
			s.m_details       = s.m_to > s.m_from && m_levels[s.m_to - 1].m_fields.empty();
			s.m_detailsAcross = s.m_details && m_levels[s.m_to - 1].m_axis == ibTotalsAxis::Columns;
		}
		// EVERY BRANCH BEGINS WHERE THE COMMON LADDER ENDED, so they all start at one depth — the
		// number of common ROW levels plus one. The common section itself starts at 1, which is the
		// number its first level has always carried.
		for (std::size_t si = 1; si < m_sections.size(); ++si)
			m_sections[si].m_depth = 1 + (m_sections.front().m_across - m_sections.front().m_from);
		m_pool.push_back(FoldNode{ &tree.Root(), std::vector<ibAggAcc>(aggs.size()), {}, {}, 0 });   // the root IS the grand total
	}

	void Feed(const ibQueryRow& row)
	{
		std::size_t cur = 0;
		FeedNode(cur, row);                                   // the grand total takes every row

		// ⭐⭐ AND THE COLUMNS HANG UNDER THE GRAND TOTAL TOO — which is what "what does this column
		// add up to" IS. The root is a heading like any other; the only thing that made its cells
		// special was that nothing produced them.
		//
		// 🛑 THEY USED TO BE A SECOND FOLD — the whole output read again, by its column keys alone,
		// with a flag telling the driver which pass it was in. The reason given was that the two sets
		// of subtotals "cannot come out of one tree", and that was true only while the tree was ONE
		// CHAIN: rows-then-columns has no node standing for "the columns alone". Now every heading
		// carries its own column branch, and the root is the heading over everything.
		FeedAcross(cur, row, m_sections.front());

		// THE COMMON LADDER — the levels written before the first `SPLIT`, which in a report that has
		// none is all of them. This is the whole of what Feed used to do.
		cur = FeedLadder(cur, row, m_sections.front());

		// ⭐⭐ …AND THEN EACH BRANCH, FROM THE NODE THE COMMON LADDER ENDED ON. The same row goes down
		// every branch: that is what `SPLIT` says — one read, folded several ways, each with its own
		// order of groupings (Max, 2026-08-27). A report with no branches does not enter this loop.
		for (std::size_t bi = 1; bi < m_sections.size(); ++bi) {
			const std::size_t fork = BranchChild(cur, bi);
			// FED LIKE THE HEADING IT STANDS UNDER, and for the same reason a detail leaf is: a fork
			// covers exactly the rows its parent does, so its figure is the parent's. Left unfed it
			// would report a nought — a total that reconciles to nothing, sitting where a reader who
			// walks into the branch would find it.
			FeedNode(fork, row);
			FeedLadder(fork, row, m_sections[bi]);
		}
	}

	// ⭐ ONE LADDER, FED FROM `cur` DOWN — the levels of one section, in order, each opening the node
	// its key names and taking the row into it. Returns the node the last ROW level opened, because
	// that is what the records hang under and what a branch forks from.
	std::size_t FeedLadder(std::size_t cur, const ibQueryRow& row, const Section& section)
	{
		// 🛑⭐⭐ "A LEVEL KEYED BY THE ROW'S IDENTITY **IS** THE ROW" WAS TRIED HERE AND TAKEN OUT AGAIN.
		// It stamped the row's fields onto such a heading and stopped the descent under it, and the
		// result is worth remembering: the sheet printed 72 lines that each looked exactly like a
		// detail record, so the groupings became INVISIBLE — a heading that carries a row is not
		// distinguishable from a row (Max, 2026-08-29: *"I set a grouping by reference and one by data
		// version; I expect to see the first, the second, and the third — the detail records. Right
		// now I see nothing except the detail records"*).
		//
		// The ladder is read LITERALLY: what a person names is what they get, one heading per rung and
		// the records at the bottom. A rung that happens to hold one row is still a rung — "it holds
		// one row" is a fact about the data, not permission to delete a level the user configured.
		for (std::size_t li = section.m_from; li < section.m_across; ++li) {
			const ibTotalLevel& level = m_levels[li];
			if (level.m_fields.empty())
				break;   // the detail level — handled below, where the row axis ends
			ibLevelKey key;
			key.reserve(level.m_fields.size());
			for (const ibTotalField& field : level.m_fields)
				key.push_back(LevelKeyValue(field, row));      // one field is the degenerate one-element key
			cur = Child(cur, std::move(key), level, section.DepthOf(li), /*across*/false);
			FeedNode(cur, row);


			// ⭐⭐ AND THE COLUMNS HANG UNDER EVERY ROW HEADING, not only under the deepest one.
			//
			// 🛑 THEY USED TO HANG UNDER THE LAST ROW LEVEL ALONE, because the fold was one chain and
			// the column keys were simply its tail. So a table with two row groupings printed its
			// figures against the INNER heading and left the outer one blank (Max, 2026-08-26: "if
			// one more grouping appears, it shows no totals above"). The cells of an outer heading
			// are a fold over a SUBSET of the keys — the row prefix and the columns, skipping what
			// is between — and a subset is not a prefix, so no single chain can carry it.
			//
			// It costs a walk down the column axis per row heading the row belongs to, and holds
			// `headings × column keys` nodes. Memory is still counted in GROUPS, which is the whole
			// criterion of the reports arc.
			FeedAcross(cur, row, section);

			// ⚠ NOTHING IS SKIPPED HERE. Every rung the ladder names is folded, and the records under
			// the last one — which is what the LIST does, one rung per fetch, the next when a node is
			// opened. A sheet that stopped early had rungs the list plainly showed, and the two roads
			// giving one answer is the whole requirement (Max, 2026-08-29: *"I need the result for
			// lists and the result for report tables to be the same"*).
		}

		// ⭐ AND THE ROW ITSELF, under the last ROW heading — not under the last heading of all. In an
		// ordinary report those are the same node and this reads exactly as it always did; in a table
		// the columns stand ACROSS the detail row, which is what makes it a line of the table with
		// cells beside it rather than something hanging inside one cell.
		//
		// Its LEVEL is the one past the last dimension of THIS ladder — the number it has always had
		// in a report with one — so a printer that lays rows out by depth is told nothing new, and no
		// column key shares a number with it.
		//
		// ⚠ THE RECORDS BELONG TO THE BRANCH THAT ASKED FOR THEM. Two branches that both declare a
		// detail level both get the row, each under its own headings — the same fact shown in two
		// cuts, which is what a split IS. A branch that declared none holds no records and pays for
		// none.
		// ⭐⭐ AND THE RECORDS ARE ALWAYS THERE. A list gets them for nothing — it drills to the rows —
		// so a report is the same read with the records level added, unconditionally. Every rule that
		// tried to decide when they were "redundant" produced a second universe: the list showed rows
		// the sheet did not (Max, 2026-08-30: *"for the report the detail records are simply always
		// added"*, and *"you have two universes right now"*).
		if (section.m_details && !section.m_detailsAcross) {
			const std::size_t leaf = AddDetail(cur, row, section.DepthOfDetails(), section);
			if (leaf != kNoNode) {
				// ⚠ AND IT IS FED LIKE A HEADING — one row's worth. Without this its accumulators
				// stay empty and Finish writes a zero into the very total that is supposed to say
				// what this record contributed.
				FeedNode(leaf, row);
				FeedAcross(leaf, row, section);
			}
		}
		return cur;
	}

	// ⭐ THE FIGURES ARE WRITTEN WHEN THE LAST ROW HAS BEEN READ. An accumulator IS the subtotal
	// while it is still being fed, and asking it earlier would be asking a question whose answer is
	// not in yet. In-place in each aggregate's OWN column, exactly as ApplyAggregates writes them —
	// so a figure reads as the row value on a leaf and as the subtotal on a heading.
	// ⭐⭐ …AND A DETAIL ROW IN A TABLE IS FIGURED LIKE ANY OTHER HEADING. In an ordinary report a
	// record and a measure share ONE column, so the record shows its own value there — `COUNT(Number)`
	// printing 1 where the document's number belongs is the defect that rule exists for. A TABLE has
	// a column per measure and a total at the right, and there a record is "a sub-group of one row"
	// (Max, 2026-08-26): its cells and its row total are FIGURES — COUNT is 1, SUM is the value —
	// not the row's fields spilled into the totals column.
	//
	// The two cases separate themselves and need no flag: a record is only ever POOLED when there is
	// a column axis (see AddDetail), so this loop reaches exactly the records that are laid out as
	// headings and never touches the ones that share their column with a measure.
	void Finish()
	{
		for (FoldNode& n : m_pool) {
			for (std::size_t i = 0; i < m_aggs.size(); ++i)
				n.m_node->m_values[AggSlotId(m_aggs, i)] = n.m_accs[i].Result(m_aggs[i]);
			n.m_node->m_hasChildren = !n.m_node->m_children.empty();
		}
	}

	// THE NUMBER OF GROUPS — what the journal prints beside the number of rows, because that pair is
	// the acceptance criterion stated as two numbers.
	long NodeCount() const { return static_cast<long>(m_pool.size()); }

private:
	// A node under construction: the tree node it already is, one accumulator per aggregate, and the
	// children it has opened, by their level key. Pool indices rather than pointers — the pool grows
	// as the tree does, and an index survives that.
	//
	// ⭐ TWO MAPS, because a heading in a table opens children of two kinds and their keys are values
	// of DIFFERENT fields: "True" as a row key and "True" as a column key are not the same child.
	// One map would have merged them silently — the worst kind of agreement.
	struct FoldNode {
		ibSelectorTree::Node* m_node = nullptr;
		std::vector<ibAggAcc> m_accs;
		std::unordered_map<ibLevelKey, std::size_t, ibLevelKeyHash, ibLevelKeyEqual> m_children;
		std::unordered_map<ibLevelKey, std::size_t, ibLevelKeyHash, ibLevelKeyEqual> m_acrossChildren;
		std::size_t           m_acrossCount = 0;   // cells written so far — where the next one is inserted
		// THE FORKS THIS HEADING HAS OPENED, by section. Not a map, because a branch is not reached by
		// a KEY — it is named by the query and there is a fixed handful of them. Left EMPTY until the
		// first fork is opened, so a report without `SPLIT` carries nothing extra per node.
		std::vector<std::size_t> m_branchChildren;
	};


	static constexpr std::size_t kNoNode = static_cast<std::size_t>(-1);

	// THE CELLS OF ONE HEADING — the column axis walked from the heading down, feeding a node per
	// level so an upper column level keeps its own subtotal exactly as it always did.
	void FeedAcross(std::size_t heading, const ibQueryRow& row, const Section& section)
	{
		std::size_t cur = heading;
		for (std::size_t li = section.m_across; li < section.m_to; ++li) {
			const ibTotalLevel& level = m_levels[li];
			if (level.m_fields.empty()) {
				// ⭐ THE RECORDS, ACROSS THE PAGE — each one a column of its own, which is what a
				// detail level declared on the COLUMN axis asks for. Nothing groups here and nothing
				// follows: a record is the end of an axis whichever way that axis reads.
				if (section.m_detailsAcross) {
					const std::size_t leaf = AddDetail(cur, row, section.DepthOfDetails(), section);
					if (leaf != kNoNode)
						FeedNode(leaf, row);
				}
				break;
			}
			ibLevelKey key;
			key.reserve(level.m_fields.size());
			for (const ibTotalField& field : level.m_fields)
				key.push_back(LevelKeyValue(field, row));
			cur = Child(cur, std::move(key), level, section.DepthOf(li), /*across*/ li == section.m_across);
			FeedNode(cur, row);
		}
	}

	// ⭐⭐ THE FORK ITSELF — the node a branch's ladder hangs from, opened on the heading the common
	// levels ended on. It carries no key and no figure: what it says is "from here the rows go
	// another way", and a walk that was not told which way goes through them in order.
	//
	// Opened ONCE per heading and remembered by branch, exactly as a level's child is remembered by
	// its key — a heading meets every row that belongs to it, and the second row must find the fork
	// the first one made rather than start a second copy of it.
	std::size_t BranchChild(std::size_t parent, std::size_t branch)
	{
		std::vector<std::size_t>& opened = m_pool[parent].m_branchChildren;
		if (opened.empty())
			opened.assign(m_sections.size(), kNoNode);
		if (opened[branch] != kNoNode)
			return opened[branch];

		ibSelectorTree::Node* parentNode = m_pool[parent].m_node;
		// AT THE PARENT'S OWN DEPTH — the fork is not a level, so it must not spend one. What hangs
		// under it is numbered as though it hung under the heading itself, which is what keeps two
		// branches at the SAME depth however many levels stand above them.
		ibSelectorTree::Node* node = parentNode->AddChild(parentNode->m_level);
		node->m_kind   = ibSelectorNodeKind::Branch;
		node->m_values = parentNode->m_values;   // the keys above stay readable inside the branch
		node->m_branch = parentNode->m_branch;   // …and so does the branch above, until this fork names its own
		if (const ibTotalBranch* named = m_sections[branch].m_branch)
			node->m_branch = named->m_name;

		const std::size_t idx = m_pool.size();
		m_pool.push_back(FoldNode{ node, std::vector<ibAggAcc>(m_aggs.size()), {}, {}, 0 });
		// ⚠ RE-READ THE POOL ENTRY — the push_back above may have reallocated (same trap as Child).
		m_pool[parent].m_branchChildren[branch] = idx;
		return idx;
	}

	void FeedNode(std::size_t idx, const ibQueryRow& row)
	{
		std::vector<ibAggAcc>& accs = m_pool[idx].m_accs;
		for (std::size_t i = 0; i < m_aggs.size(); ++i)
			accs[i].Feed(m_aggs[i], row);
	}

	std::size_t Child(std::size_t parent, ibLevelKey&& key, const ibTotalLevel& level, int childLevel, bool across)
	{
		auto& opened = across ? m_pool[parent].m_acrossChildren : m_pool[parent].m_children;
		const auto it = opened.find(key);
		if (it != opened.end())
			return it->second;

		ibSelectorTree::Node* parentNode = m_pool[parent].m_node;
		// THE CELLS FIRST, THE SUB-HEADINGS AFTER — see Node::InsertChild. A walk is pre-order, and
		// that order is the only thing that says which heading a cell stands beside.
		ibSelectorTree::Node* child      = across
			? parentNode->InsertChild(m_pool[parent].m_acrossCount++, childLevel)
			: parentNode->AddChild(childLevel);
		// A SUBGROUP inherits the grouping fields available from the levels above, so a display column
		// that dot-walks an ancestor dimension resolves in the subgroup header too. Only the KEYS are
		// there to inherit at this point — the figures are written at the end, by Finish().
		child->m_values = parentNode->m_values;
		// ⭐ …AND WHICH BRANCH IT STANDS IN, inherited exactly as the keys above are. A branch is a
		// FACT ABOUT THE NODE — "the rows went this way to reach me" — and not a state of whoever is
		// walking: stamped only on the fork, a walk that had descended past it could no longer tell
		// whose nodes it was looking at, and every output printed every branch's headings (Max, live,
		// 2026-08-27: 125 rows of its own followed by 125 blank ones belonging to the other table).
		child->m_branch = parentNode->m_branch;
		for (std::size_t i = 0; i < level.m_fields.size() && i < key.size(); ++i)
			child->m_values[level.m_fields[i].m_col->GetColumnId()] = key[i];

		const std::size_t idx = m_pool.size();
		m_pool.push_back(FoldNode{ child, std::vector<ibAggAcc>(m_aggs.size()), {} });
		// ⚠ RE-READ THE POOL ENTRY: the push_back above may have reallocated, and a reference taken
		// before it points into the old buffer.
		(across ? m_pool[parent].m_acrossChildren : m_pool[parent].m_children).emplace(std::move(key), idx);
		return idx;
	}

	// ⭐ A DETAIL ROW IS A NODE LIKE ANY OTHER NOW — it is returned, because a table hangs its cells
	// under it. It still opens no map of children: what stands across it is opened by FeedAcross,
	// and nothing groups below a row.
	std::size_t AddDetail(std::size_t parent, const ibQueryRow& row, int childLevel, const Section& section)
	{
		ibSelectorTree::Node* parentNode = m_pool[parent].m_node;
		ibSelectorTree::Node* leaf       = parentNode->AddChild(childLevel);
		leaf->m_kind   = ibSelectorNodeKind::Detail;
		leaf->m_values = parentNode->m_values;                 // the group keys above stay readable on the row
		leaf->m_branch = parentNode->m_branch;                 // …and whose branch it is read on (see Child)
		for (const ibQueryRamColumn& col : m_rowColumns)
			leaf->m_values[col.m_id] = row.Get(col.m_id);

		// ⚠ AND NOTHING IS ROLLED ON TOP OF THEM — a detail row's figure is the value it already
		// holds, which the loop above has just written; rolling COUNT over the single row would put a
		// 1 where the document's number belongs. EXCEPT where the row holds nothing to write: COUNT(*)
		// has no input column at all, so its receiver is filled here or comes back blank.
		for (std::size_t i = 0; i < m_aggs.size(); ++i) {
			if (RowCarries(m_aggs[i].m_col))
				continue;
			ibAggAcc one;
			one.Feed(m_aggs[i], row);
			leaf->m_values[AggSlotId(m_aggs, i)] = one.Result(m_aggs[i]);
		}

		// ⚠ IN THE POOL ONLY WHERE SOMETHING HANGS UNDER IT — a table's cells do, an ordinary
		// report's rows do not. A pool entry costs an accumulator per aggregate, and a report reads
		// as many detail rows as the source has: paying that per ROW is exactly the memory the
		// streaming fold exists to not spend (the arc's criterion is groups, not rows).
		if (section.m_across >= section.m_to)
			return kNoNode;
		const std::size_t idx = m_pool.size();
		m_pool.push_back(FoldNode{ leaf, std::vector<ibAggAcc>(m_aggs.size()), {} });
		return idx;   // …and from here on it is a heading like any other — see Finish
	}

	bool RowCarries(const ibBackendQueryColumn* col) const
	{
		if (col == nullptr)
			return false;
		for (const ibQueryRamColumn& c : m_rowColumns)
			if (c.m_id == col->GetColumnId())
				return true;
		return false;
	}

	const std::vector<ibTotalLevel>&                      m_levels;
	const std::vector<ibDataQueryBuilder::AggregateItem>& m_aggs;
	const std::vector<ibQueryRamColumn>&                  m_rowColumns;   // what a DETAIL row writes
	ibMetaID                                              m_identity = 0; // …and the column that IS the row
	std::vector<FoldNode>                                 m_pool;
	// THE LADDERS THIS FOLD BUILDS — one without `SPLIT`, and then one per branch. What used to be
	// three fields about "the levels" now belongs to each section, because with branches there is no
	// longer a single answer to "where do the columns start".
	std::vector<Section>                                  m_sections;
};

// --- PERIODS: the padding ---------------------------------------------------------------------
// A period nothing happened in has no row to come back, and that missing row is exactly what the
// reader is looking at. So after the fold — EITHER fold, ours or the DBMS's — the periodic levels
// are filled in: every unit between the bounds gets a node, and the ones the data produced stay
// where they are.
//
// It is a pass over NODES, not rows: a year by months adds at most twelve, and it costs the same
// whether they were folded here or on the server. Which is why it lives in one place and both roads
// end with it.

// The figure an EMPTY period reports. A sum or a count of nothing IS nought — that is the number a
// chart needs and the one a reader expects on a quiet month. A MIN / MAX / AVG of nothing is not a
// number at all, and writing 0 there would be inventing a smallest value that never existed.
ibValue EmptyPeriodFigure(const ibDataQueryBuilder::AggregateItem& a)
{
	using Fn = ibDataQueryBuilder::AggregateFn;
	return (a.m_fn == Fn::Sum || a.m_fn == Fn::Count) ? ibValue(ibNumber(0L)) : ibValue();
}

// Fill in the missing periods among ONE node's children. The children carry the period in `id`;
// bounds that were written win, and where they were not the series runs between the first and the
// last period the data holds — the honest default, since with no bounds and no rows there is no
// series at all.
void PadPeriodChildren(ibSelectorTree::Node& parent, const ibTotalField& field, int childLevel,
                       const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	const ibMetaID id = field.m_col->GetColumnId();
	const ibTotalPeriods& periods = *field.m_periods;

	// ⚠ THE CHILDREN OF THIS LEVEL, and only those. A heading in a CROSS-TABLE stands over children
	// of two kinds — the sub-headings under it and the cells across it — and a cell knows nothing
	// about the period this level pads. Taken by their LEVEL, which is the one thing that says
	// which of them this call is about; without it the first cell answered "not a period level
	// after all" and the quiet months were silently never filled in.
	std::map<wxDateTime, ibSelectorTree::Node*> have;   // ordered by the moment itself
	std::vector<ibSelectorTree::Node*>          others; // …the ones this level does not speak for
	for (const std::unique_ptr<ibSelectorTree::Node>& child : parent.m_children) {
		if (child == nullptr) continue;
		if (child->m_level != childLevel) { others.push_back(child.get()); continue; }
		const auto it = child->m_values.find(id);
		if (it == child->m_values.end() || it->second.GetType() != TYPE_DATE) return;   // not a period level after all
		have[it->second.GetDateTime()] = child.get();
	}

	wxDateTime from = !periods.m_from.IsEmpty() ? ibTruncateToPeriod(periods.m_from.GetDateTime(), periods.m_unit)
	                                            : (have.empty() ? wxDateTime() : have.begin()->first);
	wxDateTime to   = !periods.m_to.IsEmpty()   ? ibTruncateToPeriod(periods.m_to.GetDateTime(),   periods.m_unit)
	                                            : (have.empty() ? wxDateTime() : have.rbegin()->first);
	if (!from.IsValid() || !to.IsValid() || from > to)
		return;                                        // nothing to pad between — and saying so is not a failure

	// ⚠ THE BOUNDS PAD, THEY DO NOT FILTER. A period OUTSIDE them that the data produced is still a
	// period that happened; dropping it here would turn a display setting into a silent WHERE.
	std::map<wxDateTime, ibSelectorTree::Node*> series = have;
	std::vector<std::unique_ptr<ibSelectorTree::Node>> made;
	for (wxDateTime t = from; t <= to; t = ibNextPeriodStart(t, periods.m_unit)) {
		if (series.find(t) != series.end())
			continue;
		auto node = std::make_unique<ibSelectorTree::Node>();
		node->m_level  = childLevel;
		node->m_values = parent.m_values;              // the keys above stay readable, as on any node
		node->m_values[id] = ibValue(t);
		for (size_t i = 0; i < aggregates.size(); ++i)
			node->m_values[AggSlotId(aggregates, i)] = EmptyPeriodFigure(aggregates[i]);
		series[t] = node.get();
		made.push_back(std::move(node));
	}
	if (made.empty())
		return;

	// Rebuild the child list along the SERIES, keeping the direction the data came in: a report that
	// reads newest-first must not have its filled months arrive ascending in the middle of it.
	const bool descending = parent.m_children.size() > 1 && have.size() > 1
	                     && parent.m_children.front() != nullptr && parent.m_children.back() != nullptr
	                     && parent.m_children.front()->m_values.count(id) && parent.m_children.back()->m_values.count(id)
	                     && parent.m_children.front()->m_values.at(id).GetDateTime()
	                        > parent.m_children.back()->m_values.at(id).GetDateTime();

	std::map<ibSelectorTree::Node*, std::unique_ptr<ibSelectorTree::Node>> owned;
	for (std::unique_ptr<ibSelectorTree::Node>& child : parent.m_children)
		if (child != nullptr) { ibSelectorTree::Node* raw = child.get(); owned[raw] = std::move(child); }
	for (std::unique_ptr<ibSelectorTree::Node>& node : made) { ibSelectorTree::Node* raw = node.get(); owned[raw] = std::move(node); }

	parent.m_children.clear();
	parent.m_children.reserve(series.size() + others.size());
	auto take = [&owned, &parent](ibSelectorTree::Node* raw) {
		const auto it = owned.find(raw);
		if (it != owned.end() && it->second)          // an empty slot is not a row — see ibSelector::Next
			parent.m_children.push_back(std::move(it->second));
	};
	// ⚠ THE OTHERS KEEP THEIR PLACE AT THE FRONT — a heading's cells are written before its
	// sub-headings, and a pre-order reader is what that order is for.
	for (ibSelectorTree::Node* raw : others)
		take(raw);
	if (descending) for (auto it = series.rbegin(); it != series.rend(); ++it) take(it->second);
	else            for (auto it = series.begin();  it != series.end();  ++it) take(it->second);
	parent.m_hasChildren = !parent.m_children.empty();
}

// Walk to the nodes whose CHILDREN are the given level, and pad each one's children.
//
// ⚠ BY LEVEL, NOT BY DEPTH. They were the same number while a fold was one chain; in a CROSS-TABLE
// they are not — a column key hangs directly under whichever row heading it belongs to, so a node
// two steps down may be level 3. The node says which level it is, so that is what is asked.
void PadLevel(ibSelectorTree::Node& node, int levelIndex, const ibTotalField& field,
              const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	const int childLevel = levelIndex + 1;
	for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
		if (child != nullptr && child->m_level == childLevel) {
			PadPeriodChildren(node, field, childLevel, aggregates);
			return;   // this node's children ARE the level — nothing deeper here belongs to it
		}
	for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
		if (child != nullptr && child->m_kind != ibSelectorNodeKind::Detail)
			PadLevel(*child, levelIndex, field, aggregates);
}

// A HIERARCHY UNFOLD IS NOT A ONE-PASS FOLD. It arranges the level's VALUES into the target
// catalog's parent chain — a shape known only once every value has been seen, and one that hangs
// the rows themselves under a folder (Hierarchy). So those folds keep reading a whole table, and
// this is the question that says which road a set of levels takes.
bool LevelsUnfoldHierarchy(const std::vector<ibTotalLevel>& levels)
{
	for (const ibTotalLevel& level : levels)
		for (const ibTotalField& field : level.m_fields)
			if (field.m_dim != ibDimensionKind::Elements)
				return true;
	return false;
}

} // namespace

// ⭐⭐ FILL IN THE PERIODS NOBODY REPORTED — the second half of what `BY … PERIODS(unit, …)` means.
//
// Both folds end here, and they must: the DBMS returns the periods it HAS, and so does the RAM fold;
// neither can invent the quiet month, because neither is looking at a calendar. This is the pass
// that does, and it works on the finished tree, so there is exactly one statement of what padding
// means whichever road produced the tree.
void ibQueryComposer::PadPeriodLevels(ibSelectorTree& tree, const std::vector<ibTotalLevel>& levels,
                                      const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	for (size_t li = 0; li < levels.size(); ++li) {
		// ONE FIELD, because a period is a scale and a tuple has none: `BY (Period, Warehouse)
		// PERIODS(Month)` would ask for the months of each warehouse, which is a different report
		// (and one the author can write as two levels). Refused where it is written; ignored here.
		if (!levels[li].IsSingleField() || !levels[li].m_fields.front().ByPeriods())
			continue;
		PadLevel(tree.Root(), static_cast<int>(li), levels[li].m_fields.front(), aggregates);
	}
}

// totals fold over a combined row set -> a RAW totals TREE (L3's own ibQueryRamTable). L3
// stops here — flat-vs-hierarchical rendering is the runtime's call. Group columns keyed by
// GetColumnId; aggregates by a synthetic id (read by alias). Grand total = the root (level
// 0). Pure (no DB) — exposed so the fold is unit-testable directly. (docs §22.1b)
//
// ⭐ IT TAKES A CURSOR. One pass, no table: every group column is a one-field level, so this is the
// streaming fold with the levels spelled the flat way. (The snapshot overload below is the same
// call with the table handed over as a cursor — see queryRowCursor.h.)
ibSelectorTree ibQueryComposer::BuildTotalsTree(ibQueryRowCursor& rows,
		const std::vector<const ibBackendQueryColumn*>& groupFields,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	ibSelectorTree tree;
	for (const ibBackendQueryColumn* g : groupFields)
		tree.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
	for (const ibDataQueryBuilder::AggregateItem& a : aggregates)   // aggregate column = its OWN source column
		if (a.m_col != nullptr) tree.AddColumn(a.m_col->GetColumnId(), a.m_col->GetName(), a.m_col->GetTypeDesc());
	AddSyntheticAggColumns(tree, aggregates);                       // + COUNT(*) receivers

	std::vector<ibTotalLevel> levels;
	levels.reserve(groupFields.size());
	for (const ibBackendQueryColumn* g : groupFields)
		levels.push_back(ibTotalLevel::One(g, ibDimensionKind::Elements));

	ibStreamingFold fold(tree, levels, aggregates, rows.Columns());
	long read = 0;
	while (rows.Next()) { fold.Feed(rows); ++read; }
	fold.Finish();
	ibJournalInfo(wxT("query.road"), wxT("STREAM: totals folded %ld rows into %ld nodes (%u levels)"),
	              read, fold.NodeCount(), static_cast<unsigned>(levels.size()));
	return tree;   // raw — the runtime decides flat vs. hierarchical
}

ibSelectorTree ibQueryComposer::BuildTotalsTree(const ibQueryRamTable& detail,
		const std::vector<const ibBackendQueryColumn*>& groupFields,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	ibRamTableCursor rows(detail);
	return BuildTotalsTree(rows, groupFields, aggregates);
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
	ibRowsByValue childrenOf;
	std::unordered_map<ibValue, long, ibValueHash, ibValueEqual> keyToRow;
	for (long r = 0; r < n; ++r) {
		keyToRow[detail.GetCell(r, rowKeyCol->GetColumnId())] = r;
		childrenOf[detail.GetCell(r, parentKeyCol->GetColumnId())].push_back(r);
	}
	std::vector<long> roots;
	for (long r = 0; r < n; ++r) {
		const ibValue pk = detail.GetCell(r, parentKeyCol->GetColumnId());
		if (IsNoKey(pk) || keyToRow.find(pk) == keyToRow.end()) roots.push_back(r);
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
// Indexed by the VALUE itself (ibValueHash, value.h). Keying by a rendered string cost a text
// conversion per row — and needed a THIRD map, `valOf`, whose only job was to give the value back
// once you had the string. The value is the key now, so there is nothing to give back.

struct RefHierCtx {
	const ibQueryRamTable*                                detail;
	const ibBackendQueryColumn*                           refCol;
	const ibRowsByValue*                                  rowsByVal;    // value -> snapshot rows carrying it
	const ibRefChildren*                                  childrenOf;   // value -> child values (target parent-map)
	const std::vector<ibDataQueryBuilder::AggregateItem>* aggregates;
	ibDimensionKind                                       dim;
};

// Attach the value-subtree rooted at `valueKey` under `parent`; returns the subtree's snapshot rows
// (for the parent's subtotal). A node = one target-catalog value (folder), carrying the rows whose
// refCol == this value + (recursively) descendant values' rows. Cycle-guarded.
std::vector<long> AttachRefNode(const RefHierCtx& ctx, ibSelectorTree::Node* parent, const ibValue& valueKey,
                                int level, ibValueSeen& visited)
{
	if (IsNoKey(valueKey) || visited[valueKey]) return {};
	visited[valueKey] = 1;

	const auto cit = ctx.childrenOf->find(valueKey);
	const bool hasKids = (cit != ctx.childrenOf->end() && !cit->second.empty());

	ibSelectorTree::Node* node = parent->AddChild(level);
	node->m_values[ctx.refCol->GetColumnId()] = valueKey;   // the key IS the value the node shows

	std::vector<long> ownRows;
	const auto rit = ctx.rowsByVal->find(valueKey);
	if (rit != ctx.rowsByVal->end()) ownRows = rit->second;   // this value's own rows

	std::vector<long> subtree = ownRows;
	if (hasKids)
		for (const ibValue& childKey : cit->second) {
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

	// Materialise the target's parent-map (value -> parent value) through the door.
	ibValueParentMap parentOf;
	{
		ibDataQueryBuilder q(holder);
		q.From(source->GetProvider().ResolveReferenceTarget(source, refCol)).Select(tRowKey, wxEmptyString).Select(tParent, wxEmptyString);
		ibSelector ts = q.Execute(ibReadPageRequest{}).Select(ibSelectKind::ibSelectKind_Direct);
		while (ts.Next())
			parentOf[ts.GetValue(tRowKey)] = ts.GetValue(tParent);
	}

	// Index the snapshot rows by the refCol value; build the value->children map from parentOf.
	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : snapshot.Columns())
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);              // + COUNT(*) receivers

	// `valOf` is gone: it existed only to map a rendered key back to the value it came from, and the
	// value is the key now.
	ibRowsByValue rowsByVal;
	const long n = snapshot.RowCount();
	for (long r = 0; r < n; ++r)
		rowsByVal[snapshot.GetCell(r, refCol->GetColumnId())].push_back(r);

	ibRefChildren childrenOf;   // among VALUES PRESENT (+ their ancestors)
	ibValueSeen   seen;
	std::function<void(const ibValue&)> chainUp = [&](const ibValue& key) {
		if (IsNoKey(key) || seen[key]) return;
		seen[key] = 1;
		const auto pit = parentOf.find(key);
		const ibValue par = (pit != parentOf.end()) ? pit->second : ibValue();
		childrenOf[par].push_back(key);   // an absent parent => a root
		chainUp(par);
	};
	for (const auto& kv : rowsByVal) chainUp(kv.first);

	RefHierCtx ctx{ &snapshot, refCol, &rowsByVal, &childrenOf, &aggregates, dim };
	ibValueSeen visited;
	std::vector<long> allRows;
	const auto rit = childrenOf.find(ibValue());   // roots = values whose parent is empty/unknown
	if (rit != childrenOf.end())
		for (const ibValue& rootKey : rit->second) {
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
	// WHERE THE PAGE STOPS AND THE PAGE-WIDTH STARTS — the same seam the streaming fold reads off
	// the levels (ibStreamingFold). This road is taken when a level unfolds a HIERARCHY, and a
	// cross-table grouped by a hierarchy is still a cross-table.
	size_t                                                across = 0;
	bool                                                  details = false;   // the last level is the rows
	// ⭐ THE COLUMN THAT IS THE ROW'S IDENTITY — the source's reference key, 0 when it has none (a RAM
	// table, a join). A level grouped BY it holds one row per group by construction, so its headings are
	// the rows themselves; see AttachDimValue. Read once, here, rather than per node.
	ibMetaID                                              identity = 0;
};

// Parent-map (value-key -> parent value-key) for a level field: the target catalog of the reference
// field (cross), or the source itself when the field is the source's OWN parent column (self). Read
// through the door. Empty when there is no hierarchy target / no holder.
ibValueParentMap ParentMapForField(const DimCtx& ctx, const ibBackendQueryColumn* field)
{
	ibValueParentMap pm;
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
		pm[ts.GetValue(rk)] = ts.GetValue(pk);
	return pm;
}

// `across` — this call is building the COLUMN branch of a cross-table: the levels past the seam,
// hung under one row heading. Everything else is the ordinary walk down the page.
// ⭐ `branch` — WHICH LADDER THIS DESCENT IS ON. Null is the common one, which is every report
// without `SPLIT`. Carried rather than read off the node, because a branch has an IDENTITY (the
// shared ibTotalBranch) and two nameless ones must not fold into each other.
// ⭐ `depth` — THE NUMBER THIS LEVEL STANDS AT, carried rather than derived from `levelIdx`. Depth is
// counted WITHIN THE LADDER (the streaming fold says the same in Section::DepthOf): two branches
// begin in the same place, so their first levels must carry the same number — computed from the flat
// list, the second branch printed one rung deeper for no reason a reader could see.
void FoldDimLevel(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows, size_t levelIdx,
                  bool across = false, const ibTotalBranch* branch = nullptr, int depth = 1, int indent = 0);

// ⭐ THE CELLS OF ONE HEADING — the column axis, folded under the heading that is being built. Called
// for EVERY row heading rather than only the deepest, because an outer heading's cells are a fold
// over a SUBSET of the keys (its own prefix and the columns), and a subset is not a prefix of the
// nesting order — no single chain can carry it. Same rule, same reason as ibStreamingFold::FeedAcross.
void FoldAcross(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows)
{
	if (ctx.across < ctx.levels->size())
		FoldDimLevel(ctx, node, rows, ctx.across, /*across*/true);
}

// Attach one value of a Hierarchy level under `parent`: sub-values (hierarchy depth) + the NEXT level
// inside this value's own rows (Hierarchy only). Returns the value-subtree's rows (for the subtotal).
std::vector<long> AttachDimValue(const DimCtx& ctx, ibSelectorTree::Node* parent, size_t levelIdx,
	const ibValue& valueKey,
	const ibRowsByValue& byVal,
	const ibRefChildren& childrenOf, ibValueSeen& visited, bool across = false,
	const ibTotalBranch* branch = nullptr, int depth = 1, int indent = 0)
{
	if (IsNoKey(valueKey) || visited[valueKey]) return {};
	visited[valueKey] = 1;
	const ibTotalLevel& level = (*ctx.levels)[levelIdx];

	// ⚠ THE LEVEL'S NUMBER, not the depth under the parent: a hierarchy nests VALUES inside one
	// level, so every folder and every item here belongs to the same level and must say so.
	ibSelectorTree::Node* node = parent->AddChild(depth);
	node->m_indent = indent;             // …and how far inside that level it stands — the hierarchy step
	node->m_values = parent->m_values;   // inherit the grouping fields available from the levels above (same rule as the Elements branch)
	node->m_branch = parent->m_branch;   // …and the branch it stands in, inherited exactly as the keys are
	// A hierarchy level is single-field by construction (FoldDimLevel routes it here only then), so
	// its key is the head field's value.
	node->m_values[level.HeadCol()->GetColumnId()] = valueKey;   // the key IS the value

	std::vector<long> ownRows;
	const auto rit = byVal.find(valueKey);
	if (rit != byVal.end()) ownRows = rit->second;

	// ⭐⭐ A LEVEL KEYED BY THE ROW'S OWN IDENTITY **IS** THE ROW, so the node carries the row's fields.
	// Grouping a catalog by its Reference and unfolding the hierarchy is how a TREE is written — the
	// shape of the list on the screen — and there every line is an element, drawn at its own depth. The
	// key determines the row completely (that is what an identity is), so every column it was read with
	// belongs on this node just as it would on a detail record.
	//
	// 🛑 Without it a tree could only be printed by grouping over the PARENT, and that reading loses
	// exactly what it is asked for: a folder holding no elements is nobody's parent and never appears, a
	// folder's own Code and Description are not the key and print blank, and every element needs a second
	// node under its own heading (Max, 2026-08-29: *"the hierarchy has to be output over the Reference"*).
	//
	// Asked of the SOURCE, not of the row count: "this level's key is the identity" is a fact about the
	// query, true for every node of the level at once. A group that merely happens to hold one row is a
	// different thing entirely and is left alone — a field that appears only where a group came out
	// single would be worse than one that never appears.
	//
	// ⚠ AND A NODE WITH NO ROW OF ITS OWN IS CLEARED, not left inheriting. A folder is pulled into the
	// tree by `chainUp` whenever something UNDER it matched, so under a filter it can stand there with no
	// row behind it — and the values it inherited from its parent are that parent's, not a blank. Left
	// alone it would print its parent's Code and Description as if they were its own, which is worse than
	// printing nothing: a wrong line reads as data.
	if (ctx.identity != 0 && ctx.identity == level.HeadCol()->GetColumnId()) {
		for (const ibQueryRamColumn& col : ctx.snapshot->Columns())
			node->m_values[col.m_id] = ownRows.empty()
				? ibValue()
				: ctx.snapshot->GetCell(ownRows.front(), col.m_id);
		node->m_values[level.HeadCol()->GetColumnId()] = valueKey;   // …its own key survives either way
	}

	std::vector<long> subtree = ownRows;
	const auto cit = childrenOf.find(valueKey);          // sub-values (deeper in the catalog hierarchy)
	if (cit != childrenOf.end())
		for (const ibValue& childKey : cit->second) {
			// ⭐ A SUB-VALUE KEEPS THE RUNG AND TAKES ONE MORE STEP IN. The level is the ladder's and does
			// not move; the indentation is the tree's and does (Max, 2026-08-29: *"if there is a sub-folder
			// it has to add one"*).
			std::vector<long> kr = AttachDimValue(ctx, node, levelIdx, childKey, byVal, childrenOf, visited, across, branch, depth, indent + 1);
			subtree.insert(subtree.end(), kr.begin(), kr.end());
		}
	// A hierarchy nests VALUES inside ONE level, so a sub-value keeps this level's depth; the NEXT
	// level goes one deeper — and stands where this value stands, not where the level began.
	//
	// ⭐⭐ …AND THE NEXT LEVEL GOES INSIDE THIS VALUE'S OWN ROWS — EXCEPT THE RECORDS, UNDER A RUNG
	// WHOSE KEY IS THE ROW ITSELF.
	//
	// 🛑 BOTH WIDER RULES WERE TRIED AND BOTH WERE WRONG, which is why the narrow one is written out:
	//   * stop the whole descent — the second grouping a person set then vanished from the sheet while
	//     the list still reached it by drilling (*"the groupings work in the list, the report does not
	//     see them"*);
	//   * stop nothing — a tree over the reference then printed every element TWICE, bold heading and
	//     plain row, one under the other (*"the hierarchy doubles"*).
	// What is degenerate is only the RECORDS level: this node already IS that row, fields and all. A
	// further GROUPING is a different question and is folded as asked.
	const bool keyIsTheRow = ctx.identity != 0 && level.HeadCol() != nullptr
		&& ctx.identity == level.HeadCol()->GetColumnId();
	const bool nextIsDetails = (levelIdx + 1 >= ctx.levels->size())
		|| (*ctx.levels)[levelIdx + 1].m_fields.empty();
	if (!(keyIsTheRow && nextIsDetails))
		FoldDimLevel(ctx, node, ownRows, levelIdx + 1, across, branch, depth + 1, indent);

	// ⭐ AND THE CELLS OF THIS HEADING — over its WHOLE subtree, which is what its own figures are
	// computed from (ApplyAggregates below reads the same rows). A folder of a hierarchy stands for
	// everything under it, so its row of cells has to say the same thing its total does.
	//
	// Built last and inserted FIRST (FoldDimLevel's across pass inserts rather than appends): the
	// subtree is not known until the children have been walked, while a pre-order reader needs the
	// cells before the sub-headings to tell whose they are.
	if (!across)
		FoldAcross(ctx, node, subtree);
	node->m_hasChildren = !node->m_children.empty();
	ApplyAggregates(*node, *ctx.snapshot, subtree, *ctx.aggregates);
	return subtree;
}

// Fold `rows` at `levelIdx` under `node`: group by the level field, then either flat groups (Elements)
// or the field's reference hierarchy (Hierarchy/HierarchyOnly). Recurses the next level inside.
void FoldDimLevel(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows, size_t levelIdx,
                  bool across, const ibTotalBranch* branch, int depth, int indent)
{
	if (levelIdx >= ctx.levels->size()) return;          // leaf level reached

	// ⭐⭐ WHERE THE LADDER FORKS — the snapshot road's half of `SPLIT`.
	//
	// 🛑 THE TWO ROADS MUST FOLD THE SAME SHAPE (the note over BuildDimensionTree says so), and for a
	// while only one of them knew about branches: the streaming fold cuts the levels into sections
	// and opens a fork per section, while this one walked the flat list and quietly folded every
	// branch as one deeper ladder. It is not only a test's problem — this road is the one a query
	// takes whenever a level UNFOLDS A HIERARCHY, so a report that split its totals and grouped by a
	// hierarchy lost its branches with nothing said (found by CI, 2026-08-27).
	//
	// The rule is the fold's, not the walk's: levels of one branch stand together, a fork carries no
	// key and spends no level, and every branch that starts here is fed THE SAME rows.
	if (!across) {
		const ibTotalBranch* here = (*ctx.levels)[levelIdx].m_branch.get();
		if (here != branch) {
			// Inside a branch and the next level belongs to another one: this section has ended.
			if (branch != nullptr)
				return;
			// On the common ladder and a branch begins: open one fork per section, side by side.
			for (size_t at = levelIdx; at < ctx.levels->size(); ) {
				const ibTotalBranch* section = (*ctx.levels)[at].m_branch.get();
				size_t end = at;
				while (end < ctx.levels->size() && (*ctx.levels)[end].m_branch.get() == section)
					++end;
				if (section == nullptr) {   // a common level after a branch — cannot happen, but do not eat it
					FoldDimLevel(ctx, node, rows, at, across, nullptr, depth, indent);
					break;
				}
				// AT THE PARENT'S OWN DEPTH — same rule as ibStreamingFold::BranchChild, so two
				// branches stand at one depth however many levels stood above them.
				ibSelectorTree::Node* fork = node->AddChild(node->m_level);
				fork->m_indent = indent;              // …and inside whatever hierarchy step it was opened in
				fork->m_kind   = ibSelectorNodeKind::Branch;
				fork->m_values = node->m_values;      // the keys above stay readable inside the branch
				fork->m_branch = section->m_name.IsEmpty() ? node->m_branch : section->m_name;
				// ⭐ EVERY BRANCH STARTS AT THE SAME DEPTH — this one, the number the level would have
				// had without the fork. A fork spends none of its own (it is opened at the parent's
				// depth above), so the branches stand level with each other however many of them there
				// are. Counted from the flat list instead, branch two printed a rung deeper.
				FoldDimLevel(ctx, fork, rows, at, across, section, depth, indent);
				FoldAcross(ctx, fork, rows);
				fork->m_hasChildren = !fork->m_children.empty();
				ApplyAggregates(*fork, *ctx.snapshot, rows, *ctx.aggregates);   // the heading's own figure
				at = end;
			}
			return;
		}
	}

	// ⭐ WHERE THE ROWS END. Down the page the ladder stops at the seam — everything past it reads
	// ACROSS, and those levels are walked by FoldAcross under each heading, not by falling into them
	// here. What still hangs below a heading is the DETAIL level, which is last in the config and
	// reads down the page like every other row level (ibDataQueryBuilder::TotalsDetails).
	if (!across && levelIdx >= ctx.across) {
		if (!ctx.details)
			return;
		levelIdx = ctx.levels->size() - 1;
	}
	const ibTotalLevel& level = (*ctx.levels)[levelIdx];

	// ⭐⭐ A LEVEL WITH NO FIELDS IS THE DETAIL RECORDS. Grouping by nothing does not make one group
	// of everything — that is what OVERALL means, at the other end of the list — it makes NO group
	// at all, so what hangs here is the rows themselves: one node per row, carrying the values it
	// was read with. That is the whole of "a detail record is an empty grouping", and it is one
	// primitive rather than a second kind of level: the settings tree writes an empty grouping node
	// and the fold answers with rows.
	//
	// The rows are ALREADY HERE — this is the snapshot the levels above were folded from — so the
	// detail level costs nodes, not a second read. (It does cost nodes: a report that prints every
	// row holds every row, which is what printing every row means. The server-side fold is refused
	// upstream when details are asked for, since ROLLUP returns no rows to hang.)
	if (level.m_fields.empty()) {
		if (across)
			return;   // nothing groups here, and there are no detail records across the page
		for (long r : rows) {
			// ⚠ NUMBERED PAST THE LAST DIMENSION, not "one deeper than the heading it hangs under".
			// In a table it hangs under the last ROW heading while the column keys are numbered on
			// past it, and two nodes sharing a number is exactly what a printer cannot untangle.
			// ⭐ …AND INSIDE A BRANCH IT IS THE BRANCH'S LAST RUNG, not the flat list's end — the same
			// rule the streaming fold spells as Section::DepthOfDetails(). Outside one the two agree,
			// so the old number is kept where there are no branches at all.
			ibSelectorTree::Node* leaf = node->AddChild(
				branch != nullptr ? depth : static_cast<int>(ctx.levels->size()));
			leaf->m_indent = indent;           // …standing wherever the heading above it stands in its tree
			leaf->m_kind = ibSelectorNodeKind::Detail;
			leaf->m_values = node->m_values;   // the group keys above stay readable on the row
			leaf->m_branch = node->m_branch;   // …and whose branch it is read on
			for (const ibQueryRamColumn& col : ctx.snapshot->Columns())
				leaf->m_values[col.m_id] = ctx.snapshot->GetCell(r, col.m_id);
			// ⭐⭐ AND NOTHING IS ROLLED ON TOP OF THEM. A detail row is the BOTTOM floor of the one
			// column that carries two things: the figure at every heading, the row's own value here.
			// Rolling the resources over this single row overwrote that value with the row's
			// "contribution" — invisible for SUM, where a sum of one row IS the row, and wrong for
			// every other function: `COUNT(Number)` put a 1 where the document's number belongs
			// (Max, 2026-08-22: "the rows returned a number, and it should be the document number").
			//
			// There is nothing to compute here anyway. What a row contributes to the total above it
			// is the value it already holds, which the loop above has just written.
			//
			// ⚠ EXCEPT WHERE THE ROW HOLDS NOTHING. `COUNT(*)` has no input column at all — its
			// receiver is a synthetic id that is not in the snapshot — so the loop above cannot
			// fill it and the parent's value is copied BEFORE the parent's own aggregates run. Left
			// alone it came back blank on every detail row while staying in the schema: a column
			// that is there and empty, which is the shape this whole day was spent removing. Those
			// aggregates, and only those, are still rolled over the single row.
			auto snapshotCarries = [&ctx](const ibBackendQueryColumn* col) {
				if (col == nullptr)
					return false;
				for (const ibQueryRamColumn& c : ctx.snapshot->Columns())
					if (c.m_id == col->GetColumnId())
						return true;
				return false;
			};
			// ⭐⭐ IN A TABLE A RECORD IS FIGURED LIKE A HEADING — "a sub-group of one row" (Max,
			// 2026-08-26): every measure is rolled over its single row, so a cell and the row total
			// hold FIGURES (COUNT is 1, SUM is the value). The rule above — a record shows its own
			// value — belongs to the ordinary report, where a record and a measure SHARE one column;
			// a table gives each measure a column of its own and there is nothing to share.
			//
			// Told apart by the same fact everywhere else uses: is there a column axis at all.
			const bool inATable = ctx.across < ctx.levels->size();
			std::vector<ibDataQueryBuilder::AggregateItem> rolled;
			for (const ibDataQueryBuilder::AggregateItem& a : *ctx.aggregates)
				if (inATable || !snapshotCarries(a.m_col))
					rolled.push_back(a);
			if (!rolled.empty())
				ApplyAggregates(*leaf, *ctx.snapshot, std::vector<long>{ r }, rolled);

			// …AND ITS CELLS — its own column groups, exactly as a heading has them.
			FoldAcross(ctx, leaf, std::vector<long>{ r });
			leaf->m_hasChildren = !leaf->m_children.empty();
		}
		return;
	}

	// A HIERARCHY UNFOLD walks ONE parent chain, so it is a single-field level's answer — there is no
	// one chain to walk for a key made of several fields. The lowering refuses that combination where
	// it is written, so this reads the head field and never has to blend the two readings.
	//
	// ⭐⭐ …AND ONLY WHERE THERE IS A CHAIN TO WALK. The unfold is a REQUEST — "read this field down its
	// parent chain" — and a request is answered by what the data can do, not by the word alone. A
	// document has no parent column; a catalog nobody has filled a parent in is the same thing said with
	// data. Either way the chain is one link long, and a level that unfolds a chain of one link IS an
	// ordinary grouping (Max, 2026-08-29: *"there cannot be levels there by definition — it is one level,
	// because there is no hierarchy at all"*).
	//
	// 🛑 Taken on the WORD, the fold went down the hierarchy road expecting a tree and had none: it built
	// roots, walked for children that could not exist, and handed the printer a shape that a flat source
	// cannot have — rows spread across seven levels where one was possible. The mess is not in what it
	// then did; it is in having entered at all (Max: *"you expect a hierarchy and you have a flat one"*).
	//
	// The parent map is read FIRST and its emptiness IS the answer, so this asks the same authority the
	// walk would have asked anyway and costs nothing extra.
	const ibValueParentMap parentOf = (level.IsSingleField() && level.HeadDim() != ibDimensionKind::Elements)
		? ParentMapForField(ctx, level.HeadCol())
		: ibValueParentMap{};

	if (level.IsSingleField() && level.HeadDim() != ibDimensionKind::Elements && !parentOf.empty()) {
		// Grouped by the VALUE, in first-seen order. `valOf` is gone with the string key — it only ever
		// mapped a rendered key back to the value it was rendered from.
		//
		// 🛑⭐⭐ …AND FIRST-SEEN ORDER HAS TO BE KEPT SOMEWHERE, because the bucket map is UNORDERED. The
		// Elements branch below has always carried its own `order` vector for exactly this reason; this
		// branch walked `byVal` itself, so the tree came out in HASH order and the read's `ORDER BY` — the
		// author's sort, the level keys, everything the query was careful to state — was thrown away at the
		// last step. Nothing said so: the rows were all there, in an order with no name (Max, 2026-08-29:
		// *"a strange sort, but it does print everything now"*).
		//
		// The fold's rule everywhere else is *a group stands where its FIRST ROW stood*, and that is all
		// this restores: the keys in arrival order, and the walk below started from them.
		ibRowsByValue          byVal;
		std::vector<ibValue>   keyOrder;
		keyOrder.reserve(rows.size());
		for (long r : rows) {
			const ibValue key = ctx.snapshot->GetCell(r, level.HeadCol()->GetColumnId());
			auto it = byVal.find(key);
			if (it == byVal.end()) {
				keyOrder.push_back(key);
				byVal.emplace(key, std::vector<long>{ r });
			}
			else {
				it->second.push_back(r);
			}
		}

		// Hierarchy / HierarchyOnly — arrange the values into the catalog's parent-ref tree. (`parentOf`
		// was read above the branch: its emptiness is what decides whether this road is taken at all.)
		ibRefChildren childrenOf;
		ibValueSeen   seen;
		std::function<void(const ibValue&)> chainUp = [&](const ibValue& key) {
			if (IsNoKey(key) || seen[key]) return;
			seen[key] = 1;
			const auto pit = parentOf.find(key);
			const ibValue par = (pit != parentOf.end()) ? pit->second : ibValue();
			// ⭐⭐ "NO PARENT" ARRIVES AS TWO DIFFERENT VALUES, AND THE ROOT BUCKET IS ONE. A key the
			// parent-map does not mention gives a default `ibValue()`; a key it DOES mention as
			// top-level gives an EMPTY REFERENCE — a reference value with a null guid, which is not
			// equal to `ibValue()` and hashes elsewhere. `IsNoKey` already reads both as "root", so
			// they are filed under the same key here; the roots are looked up under `ibValue()` below.
			//
			// 🛑 Filed under whatever came, a hierarchical catalog lost its whole tree: every top-level
			// element went into the empty-REFERENCE bucket, the root lookup read the empty-VALUE one and
			// found nobody, and the only node that survived was the one whose parent happened to be
			// missing from the map altogether. One node out of five rows — which is exactly what the
			// walk saw (Max, 2026-08-29: the hierarchical sheet came out blank, twice).
			childrenOf[IsNoKey(par) ? ibValue() : par].push_back(key);
			chainUp(par);
		};
		// ⭐⭐ …AND A KEY STANDS WHERE ITS OWN ROW STOOD, NOT WHERE ITS FIRST CHILD DID. `chainUp` walks
		// UPWARD, so filing straight from it puts a folder in its parent's list the moment the first row
		// UNDER it is met — and a folder whose contents sort early jumped to the front while its own row
		// sat further down the read. Two passes say it properly:
		//
		//   1. every key that HAS a row, in the order the rows arrived — the fold's rule everywhere else;
		//   2. only then the ancestors that have NO row of their own (pulled in by a filter that kept a
		//      child and dropped the folder). They have nothing to be ordered by, so they follow.
		for (const ibValue& key : keyOrder) {
			if (IsNoKey(key) || seen[key])
				continue;
			seen[key] = 1;
			const auto pit = parentOf.find(key);
			const ibValue par = (pit != parentOf.end()) ? pit->second : ibValue();
			childrenOf[IsNoKey(par) ? ibValue() : par].push_back(key);
		}
		for (const ibValue& key : keyOrder) {
			const auto pit = parentOf.find(key);
			if (pit != parentOf.end())
				chainUp(pit->second);
		}
		ibValueSeen visited;
		const auto rit = childrenOf.find(ibValue());
		if (rit != childrenOf.end())
			for (const ibValue& rootKey : rit->second)
				AttachDimValue(ctx, node, levelIdx, rootKey, byVal, childrenOf, visited, across, branch, depth, indent);

		// ⭐⭐ …AND THE ROWS THAT HAVE NO KEY AT THIS LEVEL BELONG TO THE NODE ITSELF. Grouping a catalog by
		// its PARENT makes "no parent" a real group — it means the TOP LEVEL — and `chainUp` refuses such a
		// key at the door (it is what ends the walk up the chain), so those rows had no heading to hang
		// under and simply vanished: a catalog printed one folder and lost every element standing beside it
		// (Max, 2026-08-29). They are not a heading either — the top level of a tree has no caption — so
		// they go where they belong, straight under the node this level is being built on, at the same
		// depth. The next level inside them still runs, so a sub-grouping under a top-level row keeps working.
		//
		// ⚠ AND ONE LEVEL SHALLOWER, which is the whole of what "belonging to the node" means here. A
		// heading stands at `depth` and its own rows one deeper; rows that belong to the NODE stand where
		// the headings do. Passed `depth`, they came out alongside a heading's children and the sheet drew
		// them INSIDE the last folder — the indentation said they were its contents (Max, 2026-08-29:
		// *"the levels are broken"*).
		// ⭐⭐ AN EMPTY VALUE IS A VALUE, AND ITS ROWS ARE A GROUP LIKE ANY OTHER — "not set" is what a
		// person calls it, and it gets a heading of its own at this rung.
		//
		// 🛑 THEY USED TO HANG ON THE NODE ITSELF, headingless, and that cost BOTH roads at once. On the
		// sheet seventy documents printed as bare rows under someone else's heading. In the LIST they
		// vanished outright: a fetch returns the nodes OF ITS RUNG, and rows attached to the rung above
		// are numbered past the last dimension — so the list showed one group and lost seventy-one rows
		// (Max, 2026-08-30: *"and the rows disappear altogether"*). Losing rows is the worst answer a
		// list can give, and it came from a special case that need not exist.
		//
		// ⚠ THIS IS NOT THE "NO PARENT" SENTINEL. `IsNoKey` reads two different facts through one
		// spelling: a row whose VALUE at this level is empty (data — a group), and a key with no PARENT
		// (structure — the root of the tree, filed in `childrenOf[ibValue()]` above). Only the first is
		// changed here; the parent-map root bucket is untouched.
		// ⭐⭐ …AND THE REMAINDER GOES ON IT. `visited` says which keys the walk down the roots actually
		// reached; whatever it did not is a row the tree has no place for — a value whose parent chain
		// the map does not reach, or one that simply has no value at this level. Read together they are
		// one thing: everything not hanging anywhere hangs HERE (Max, 2026-08-30: *"you just need to
		// hang the remainder of the values onto the empty element"*).
		//
		// 🛑 THE ALTERNATIVE IS SILENT LOSS, and that is what it was: a fetch returns the nodes of its
		// rung, so rows attached nowhere are rows nobody can reach — a list showed one group and lost
		// seventy-one rows. A fold's contract is that every row it was given is somewhere in the tree it
		// returns; a row that is in none of the nodes is a row the fold ate.
		std::vector<long> orphaned;
		for (const ibValue& key : keyOrder) {
			if (!IsNoKey(key) && visited[key])
				continue;                                   // attached under its own folder chain
			const auto bit = byVal.find(key);
			if (bit != byVal.end())
				orphaned.insert(orphaned.end(), bit->second.begin(), bit->second.end());
		}
		if (!orphaned.empty()) {
			ibSelectorTree::Node* empty = node->AddChild(depth);
			empty->m_indent = indent;
			empty->m_values = node->m_values;   // the rungs above stay readable on this heading
			empty->m_branch = node->m_branch;
			empty->m_values[level.HeadCol()->GetColumnId()] = ibValue();   // …and its own key, which is empty
			FoldDimLevel(ctx, empty, orphaned, levelIdx + 1, across, branch, depth + 1, indent);
			if (!across)
				FoldAcross(ctx, empty, orphaned);
			empty->m_hasChildren = !empty->m_children.empty();
			ApplyAggregates(*empty, *ctx.snapshot, orphaned, *ctx.aggregates);
		}
		return;
	}

	// GROUPED BY THE TUPLE of the level's fields, in first-seen order. One field is the degenerate
	// case of the same walk — a one-element key — so a level of two fields costs one more cell read
	// per row and nothing else.
	ibRowsByLevelKey        byKey;
	std::vector<ibLevelKey> order;
	for (long r : rows) {
		ibLevelKey key;
		key.reserve(level.m_fields.size());
		for (const ibTotalField& field : level.m_fields)
			key.push_back(LevelKeyValue(field, ibRamTableRow(*ctx.snapshot, r)));   // truncated when the field is BY PERIODS

		const auto it = byKey.find(key);
		if (it == byKey.end()) {
			order.push_back(key);
			byKey.emplace(std::move(key), std::vector<long>{ r });
		}
		else {
			it->second.push_back(r);
		}
	}

	// ⚠ THE LEVEL'S NUMBER, NOT THE DEPTH BELOW THIS NODE. Down the page the two agree; across it
	// they do not — a column key hangs directly under whichever row heading it belongs to, and its
	// number has to say WHICH LEVEL it is so the schema and the tree keep meaning the same thing.
	const int childLevel = depth;   // the depth this level stands at — WITHIN its ladder, see the declaration
	// …and cells are INSERTED before the sub-headings a previous pass may already have added, so a
	// pre-order reader meets a heading's own cells before anything nested under it.
	std::size_t insertAt = 0;

	for (const ibLevelKey& key : order) {
		ibSelectorTree::Node* child = across
			? node->InsertChild(insertAt++, childLevel)
			: node->AddChild(childLevel);
		child->m_indent = indent;   // …inside whatever hierarchy step the level above it stands in
		// A SUBGROUP inherits the grouping fields AVAILABLE from the levels above (Max) — copy the parent
		// group's stamped dimension values down, then add this level's own. So a display column that
		// dot-walks an ANCESTOR dimension's reference (e.g. the parent grouped by Reference, this level by
		// Reference.DataVersion) resolves against the inherited value in the subgroup header, not just at the
		// top level. Aggregates are (re)stamped by ApplyAggregates below, so inheriting them is harmless.
		child->m_values = node->m_values;
		child->m_branch = node->m_branch;   // …and which branch it stands in — a fact about the NODE
		for (std::size_t i = 0; i < level.m_fields.size(); ++i)
			child->m_values[level.m_fields[i].m_col->GetColumnId()] = key[i];   // every field of the key IS a value

		const std::vector<long>& own = byKey[key];

		// ⭐⭐ A LEVEL KEYED BY THE ROW'S OWN IDENTITY **IS** THE ROW — the same rule AttachDimValue
		// states, and it belongs to the LEVEL rather than to either branch. A field can be grouped by
		// with any unfold, and which branch that takes is not the grouping's business: a HIERARCHY
		// unfold over a source with no parent column (a document grouped by its own reference) is an
		// ordinary grouping by the time it gets here, and the heading came out holding nothing but the
		// reference while a record printed the same document again underneath (Max, 2026-08-29, live).
		//
		// ⭐⭐ AND IT IS A RECORD, NOT A HEADING WEARING A RECORD'S FIELDS. A node has ONE STATUS: either it
		// is a GROUPING — a key, a caption drawn across the line, figures — or it is a ROW, which is its own
		// values in its own columns. Stamped onto a Group node the row's fields made it both at once, and a
		// printer that draws both draws them on top of each other: the reference's presentation over the
		// number, in one cell (Max, 2026-08-29: *"you hand back a row that is in two statuses at the same
		// time — hierarchy and grouping"*).
		//
		// ⚠ AND ONLY THE SOURCE'S OWN TABLE IS READ THIS WAY. Grouping a document by a CATALOG it refers to
		// stays an ordinary grouping, hierarchy unfold or not — the catalog's folders are headings and the
		// documents hang under them. It is the MAIN TABLE, grouped by its own reference, that turns into
		// records, and that is what lets a list show every field on the line (Max, 2026-08-29).
		//
		// There is nothing below such a node — the group and the row are one thing here — so the next level
		// is not entered and the figures are not rolled over it: a record shows what it holds, exactly as
		// the detail branch above says.
		// ⭐⭐ A LEVEL KEYED BY THE ROW'S OWN IDENTITY **IS** THE ROW, so the node carries the row's fields.
		// Its key tells one row from another, so every group holds exactly one — and a heading that stands
		// for one row may as well show it. Asked of the SOURCE, not of the row count: a group that merely
		// happens to hold one row is a different thing and is left alone.
		// 🛑⭐⭐ AND IT IS TAKEN OUT AGAIN, FOR THE `Elements` SPELLING. Stamping the row's fields onto
		// such a heading made it indistinguishable from a record, so the groupings a person had set
		// went INVISIBLE — 72 lines that all read as detail rows — and stopping the descent under it
		// dropped every rung below, which the LIST still showed by drilling into the node. Both halves
		// of the rule cost more than they saved (Max, 2026-08-29: *"I set a grouping by reference and
		// one by data version; I expect the first, the second and the third"* and *"the list keeps
		// diverging from the report"*).
		//
		// A rung that happens to hold one row is still a rung: "it holds one row" is a fact about the
		// data, not permission to delete a level somebody configured. The LADDER IS READ LITERALLY —
		// one heading per rung, records at the bottom — which is exactly what the list draws, and one
		// answer for both roads is the requirement.
		//
		// ⚠ AND AN ORDINARY RUNG IS AN ORDINARY RUNG, whatever its key happens to be. The "a level keyed
		// by the row's identity IS the row" rule belongs to the TREE — `AttachDimValue`, where a node
		// genuinely stands for one element and the tree itself is the display — and it was tried here
		// as well, twice. Both times it took away what a person had asked for: a document list grouped
		// by its own reference printed headings and NO detail records (Max, 2026-08-30: *"the problem
		// is that in documents the detail records are not printed"*). A group holding one row is still
		// a group, and the rows under it are still the rows.
		FoldDimLevel(ctx, child, own, levelIdx + 1, across, branch, depth + 1, indent);   // next level inside, same ladder, one deeper
		// ⭐ AND THE CELLS OF THIS HEADING — under EVERY row heading, not only the deepest (FoldAcross).
		if (!across)
			FoldAcross(ctx, child, own);
		child->m_hasChildren = !child->m_children.empty();
		ApplyAggregates(*child, *ctx.snapshot, own, *ctx.aggregates);
	}
}

} // namespace

// ⭐⭐ THE COMPOSITION'S OWN FOLD, TAKING A CURSOR. This is the road every report travels, so this is
// where "memory grows with the number of GROUPS" is either true or not.
//
// One pass builds the whole tree when the levels group by VALUES — which is what a report's levels
// do. The exception says its own name: a level that unfolds a reference HIERARCHY arranges its
// values into the target catalog's parent chain, a shape that is not known until every value has
// been seen (and, in Hierarchy, hangs the rows themselves under their folder). That fold reads a
// whole table, so the cursor is drained into one and the road is journalled — the answer is the
// same either way, and the only thing at stake is what it costs.
ibSelectorTree ibQueryComposer::BuildDimensionTree(ibQueryRowCursor& rows,
		const std::vector<ibTotalLevel>& levels, const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
		ibDatabaseConnectionHolder* holder, const ibBackendQueryable* source)
{
	if (LevelsUnfoldHierarchy(levels)) {
		ibQueryRamTable snapshot = ibDrainToRamTable(rows);
		ibJournalInfo(wxT("query.road"), wxT("RAM: drained %ld detail rows - a level unfolds a reference hierarchy"),
		              snapshot.RowCount());
		return BuildDimensionTree(snapshot, levels, aggregates, holder, source);
	}

	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : rows.Columns())
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);

	// …AND WHICH COLUMN IS THE ROW'S IDENTITY. The parameter arrived here all along and only the
	// SNAPSHOT road read it, so an ordinary grouping — which streams — folded without ever knowing.
	ibMetaID identity = 0;
	if (source != nullptr) {
		const std::vector<const ibBackendQueryColumn*> keys = source->GetPrimaryKeyColumns();
		if (keys.size() == 1 && keys.front() != nullptr)
			identity = keys.front()->GetColumnId();
	}
	ibStreamingFold fold(tree, levels, aggregates, rows.Columns(), identity);
	long read = 0;
	while (rows.Next()) { fold.Feed(rows); ++read; }
	fold.Finish();
	PadPeriodLevels(tree, levels, aggregates);   // the quiet months, which no fold can produce
	ApplyScopedAggregates(tree, aggregates);     // …and the figures that belong to ONE level (OVER)
	ibJournalInfo(wxT("query.road"), wxT("STREAM: folded %ld rows into %ld nodes (%u levels)"),
	              read, fold.NodeCount(), static_cast<unsigned>(levels.size()));
	return tree;
}

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

	// THE SEAM AND THE ROWS — asked of the levels, exactly as the streaming fold asks (the two roads
	// must fold the same shape; which one a query takes depends only on whether a level unfolds a
	// hierarchy).
	DimCtx ctx{ &snapshot, &levels, &aggregates, holder, source };
	ctx.across = levels.size();
	for (size_t li = 0; li < levels.size(); ++li)
		if (levels[li].m_axis == ibTotalsAxis::Columns) { ctx.across = li; break; }
	ctx.details = !levels.empty() && levels.back().m_fields.empty();
	// …AND WHICH COLUMN IS THE ROW'S IDENTITY, asked of the source once. A level grouped by it holds one
	// row per group by construction, and its headings ARE those rows (AttachDimValue).
	if (source != nullptr) {
		const std::vector<const ibBackendQueryColumn*> keys = source->GetPrimaryKeyColumns();
		if (keys.size() == 1 && keys.front() != nullptr)
			ctx.identity = keys.front()->GetColumnId();
	}

	FoldDimLevel(ctx, &tree.Root(), all, 0);
	// …AND THE ROOT'S OWN CELLS — the column totals, which are the cells of the heading over
	// everything (see ibStreamingFold::Feed for why this replaced a second fold).
	FoldAcross(ctx, &tree.Root(), all);
	tree.Root().m_hasChildren = !tree.Root().m_children.empty();
	ApplyAggregates(tree.Root(), snapshot, all, aggregates);   // grand total in-place
	PadPeriodLevels(tree, levels, aggregates);                 // …and the periods nothing happened in
	ApplyScopedAggregates(tree, aggregates);                   // …and the figures that belong to ONE level
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

	// INDEXED BY THE KEY VALUE, not by a string made from it. Building
	// GetHashKey() per row meant a text conversion (ToString for a number,
	// wxString::Format for a reference) on BOTH sides of every join, and then a
	// std::map comparing those strings character by character. ibValueHash /
	// ibValueEqual (value.h) index the values themselves — the same policy the
	// LINQ join uses, so the two joins now agree on what "same key" means.
	//
	// ⚠ One behaviour follows from that agreement: matching is by the value
	// ORDER, so a number no longer matches the string that spells it (`1` vs
	// "1"). Through GetHashKey they collided, because both became the text "1".
	// Same-kind keys — the case a real join is made of, a column against a
	// column — are unaffected, and 1 vs 1.0 still match.
	std::unordered_map<ibValue, std::vector<long>, ibValueHash, ibValueEqual> rightByKey;
	for (long j = 0; j < right.RowCount(); ++j) {
		if (onRight != nullptr) {
			const ibValue keyR = right.GetCell(j, onRight->GetColumnId());
			if (RamIsNullValue(keyR)) continue;   // NULL key: unmatchable (RIGHT/FULL emit it below as unmatched)
			rightByKey[keyR].push_back(j);
		}
		else rightByKey[ibValue()].push_back(j);   // keyless cross
	}
	std::vector<char> rightMatched(static_cast<size_t>(right.RowCount()), 0);
	for (long i = 0; i < left.RowCount(); ++i) {
		auto it = rightByKey.cend();
		if (onLeft != nullptr) {
			const ibValue keyL = left.GetCell(i, onLeft->GetColumnId());
			if (!RamIsNullValue(keyL)) it = rightByKey.find(keyL);   // NULL key -> stays end() -> no match
		}
		else it = rightByKey.find(ibValue());   // keyless cross
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

	// Row identity = the sequence of its cells (ibValueSeqHash, value.h), not a
	// string folded from them.
	std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual> seen;
	for (long i = 0; i < src.RowCount(); ++i) {
		std::vector<ibValue> key;
		key.reserve(cols.size());
		for (const ibBackendQueryColumn* c : cols)
			key.push_back(src.GetCell(i, c->GetColumnId()));
		if (!seen.insert(std::move(key)).second)
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
// ⭐ CAN THE DBMS FOLD THIS TOTALS ITSELF, and if so — fold it. The same two push-downs
// ExecuteTotals takes when it can, asked as a QUESTION so a caller that must otherwise read detail
// rows (the query lowering) can take the cheap road when it exists and its own road when it does
// not. False = nothing was read; the caller proceeds exactly as before.
bool ibQueryComposer::TryFoldTotalsInDbms(const ibDataQuerySpec& spec, ibSelectorTree& out)
{
	if (IsSingleSource(spec) && ibDbTableProvider::CanPushRollupTotals(spec)) {
		out = ibDbTableProvider::ExecuteRollupTotals(spec);
		return true;
	}
	if (!IsSingleSource(spec) && ibDbTableProvider::CanPushColocatedRollupTotals(spec)) {
		out = ibDbTableProvider::ExecuteColocatedRollupTotals(spec);
		return true;
	}
	return false;
}

// CAN A STATEMENT DECLARE A NAMED QUERY HERE (`WITH …`)? The connected driver's own answer, asked
// through L2's question rather than read off its dictionary — same road as the rollup gate above.
bool ibQueryComposer::CanDeclareNamedQuery(ibDatabaseConnectionHolder* holder)
{
	ibConnectionScope scope(holder);
	return scope && ibCanUseCte(scope.get());
}

ibSelectorTree ibQueryComposer::ExecuteTotals(const ibDataQuerySpec& spec)
{
	// The one that needs it most: totals read the detail AND fold it, often in more than one pass.

	// Push-down: a single-source totals on a ROLLUP-capable DBMS runs server-side (GROUP BY ROLLUP),
	// the DBMS computing every subtotal level — only the aggregated rows transit, no raw detail.
	if (IsSingleSource(spec) && ibDbTableProvider::CanPushRollupTotals(spec)) {
		// ⭐ SAID ON BOTH ROADS, and that is the whole value of it: a journal that speaks only when
		// something goes to RAM cannot tell "the fold ran on the server" from "nobody journalled it".
		// A right-looking report is consistent with either, which is exactly how the server fold once
		// went a whole arc without executing once.
		ibJournalInfo(wxT("query.road"), wxT("SERVER: totals folded by GROUP BY ROLLUP (single source)"));
		return ibDbTableProvider::ExecuteRollupTotals(spec);
	}

	// Multi-source co-located JOIN on a ROLLUP-capable dialect: push GROUP BY ROLLUP server-side (the
	// DBMS computes every subtotal level; only aggregated rows transit) instead of materialising the
	// leaves and folding the totals tree in RAM. Same ibSelectorTree either way — perf, not correctness.
	if (!IsSingleSource(spec) && ibDbTableProvider::CanPushColocatedRollupTotals(spec)) {
		ibJournalInfo(wxT("query.road"), wxT("SERVER: totals folded by GROUP BY ROLLUP (co-located join)"));
		return ibDbTableProvider::ExecuteColocatedRollupTotals(spec);
	}

	// …and here the fold is OURS: the detail is read whole and the tree is built in memory. Correct
	// either way — the numbers do not depend on the road — so this line is the only thing that tells
	// a person which one they got.
	ibJournalInfo(wxT("query.road"), wxT("RAM: totals folded in memory (%s, %u levels)"),
	              IsSingleSource(spec) ? wxT("single source") : wxT("composed sources"),
	              static_cast<unsigned>(spec.m_totals != nullptr ? spec.m_totals->size() : 0));

	if (IsSingleSource(spec)) {
		// ⭐ READ AND FOLD IN ONE PASS. The source's group + sum columns, unfiltered by page, straight
		// off the cursor: the fold takes the rows as they arrive and keeps only the tree. This road
		// used to materialise the whole detail first — the one thing a totals read must not do.
		std::vector<const ibBackendQueryColumn*> cols = *spec.m_groupBy;
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			if (a.m_col != nullptr) cols.push_back(a.m_col);
		ibLeafRowCursor rows(spec.m_queryable, spec.m_holder, LeafConditions(spec, spec.m_queryable), cols);
		return BuildTotalsTree(rows, *spec.m_groupBy, *spec.m_aggregates);
	}

	ibQueryRamTable combined;
	{
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
	: m_source(std::make_shared<ibRamTableResultSource>(std::move(ramTable), queryable))
{
}

ibDataQueryResult::~ibDataQueryResult() = default;
ibDataQueryResult::ibDataQueryResult(ibDataQueryResult&&) noexcept = default;
ibDataQueryResult& ibDataQueryResult::operator=(ibDataQueryResult&&) noexcept = default;

bool     ibDataQueryResult::Next()                                                     { return m_source->Next(); }
ibValue  ibDataQueryResult::GetValue(const ibBackendQueryColumn* col)            const { return m_source->Value(col); }
ibValue  ibDataQueryResult::GetValue(const ibBackendColumnRawDB& rawColumn)             const { return m_source->Value(&rawColumn); }
ibValue  ibDataQueryResult::GetColumn(const wxString& alias)                     const { return m_source->Column(alias); }
ibValue  ibDataQueryResult::GetColumnObject(const wxString& prefix, const ibBackendQueryColumn* col) const { return m_source->ColumnObject(prefix, col); }

void ibDataQueryResult::SetMaterialiseColumns(std::vector<const ibBackendQueryColumn*> cols)
{
	m_matColumns = std::move(cols);
}

void ibDataQueryResult::SetTotals(std::vector<ibTotalLevel> levels, std::vector<ibAggregateItem> aggregates,
	bool overall)
{
	m_totalLevels     = std::move(levels);
	m_totalAggregates = std::move(aggregates);
	m_totalsOverall   = overall;
}

void ibDataQueryResult::SetReadyTree(ibSelectorTree tree)
{
	// The push-down's answer, carried to whoever walks this result. Held by shared pointer because a
	// result is moved around and the tree is not a thing to copy.
	m_readyTree = std::make_shared<ibSelectorTree>(std::move(tree));
}

void ibDataQueryResult::SetObjectRead(ibMetaID dimColumnId, const wxString& prefix, const ibBackendQueryColumn* leaf)
{
	if (prefix.IsEmpty() || leaf == nullptr)
		return;
	m_objectReads[dimColumnId] = ibObjectRead{ prefix, leaf };
}

void ibDataQueryResult::SetSource(ibDatabaseConnectionHolder* holder, const ibBackendQueryable* queryable,
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> selectCols,
	std::vector<ibQueryCondition> conditions,
	std::vector<std::shared_ptr<const ibBackendQueryable>> owned)
{
	m_srcHolder     = holder;
	m_srcQueryable  = queryable;
	m_srcSelectCols = std::move(selectCols);
	m_srcConditions = std::move(conditions);
	// SHARE, don't copy — the sources keep their identity, and every column pointer stamped on this
	// result lives inside one of them.
	m_ownedSources  = std::move(owned);
}

namespace {

// ⭐⭐ THE RESULT'S ROWS, HANDED OVER AS A CURSOR — and this is where "the fold takes a cursor"
// stops being an abstraction and becomes one fewer copy of the detail.
//
// Select() used to DRAIN the backing into an ibQueryRamTable right here: every row of the read,
// materialised, before the fold had seen any of it. A report over a register with a million
// movements held a million rows to print forty headings.
//
// The plan the door stamped travels with it — WHICH columns (m_matColumns) and HOW each is read
// (m_objectReads: a dot-walked dimension is a reference / enum / composite projected as a whole
// SPREAD under an alias prefix, so asking for the column itself would find nothing and the level
// would fold every row into one empty group).
class ibResultRowCursor : public ibQueryRowCursor
{
public:
	ibResultRowCursor(std::shared_ptr<ibDataResultSource> source,
	                  const std::vector<const ibBackendQueryColumn*>& cols,
	                  std::map<ibMetaID, std::pair<wxString, const ibBackendQueryColumn*>> objectReads)
		: m_source(std::move(source)), m_objectReads(std::move(objectReads))
	{
		for (const ibBackendQueryColumn* c : cols)
			if (c != nullptr) {
				m_columns.push_back(ibQueryRamColumn{ c->GetColumnId(), c->GetName(), c->GetTypeDesc() });
				m_cols.push_back(c);
			}
	}

	bool Next() override { return m_source != nullptr && m_source->Next(); }

	ibValue Get(ibMetaID id) const override
	{
		if (m_source == nullptr)
			return ibValue();
		const auto objectRead = m_objectReads.find(id);
		if (objectRead != m_objectReads.end())
			return m_source->ColumnObject(objectRead->second.first, objectRead->second.second);
		for (const ibBackendQueryColumn* c : m_cols)
			if (c->GetColumnId() == id)
				return m_source->Value(c);
		return ibValue();
	}

	const std::vector<ibQueryRamColumn>& Columns() const override { return m_columns; }

private:
	std::shared_ptr<ibDataResultSource>      m_source;    // shared with the result it came from — ONE scan
	std::vector<const ibBackendQueryColumn*> m_cols;      // the columns as the backing reads them
	std::vector<ibQueryRamColumn>            m_columns;   // …and as the tree declares them
	std::map<ibMetaID, std::pair<wxString, const ibBackendQueryColumn*>> m_objectReads;
};

} // namespace

// selection = result.Select(mode). The rows go to the ibSelector AS A CURSOR — it folds them per the
// mode (streamed for the group folds, drained into a table for the ones that need every row
// addressable), and the caller sees one interface, never the backing or the form.
ibSelector ibDataQueryResult::Select(ibSelectKind mode)
{
	// ⭐ THE DBMS MAY HAVE FOLDED IT ALREADY. When the totals were pushed down (GROUP BY ROLLUP), the
	// tree is complete and there is no cursor of detail rows to read — reading one here would fetch
	// exactly the rows the push-down exists to leave in the database.
	if (m_readyTree) {
		ibSelector s(ibQueryRamTable(), mode);
		s.WithTotals(m_totalLevels, m_totalAggregates, m_totalsOverall);
		s.WithReadyTree(m_readyTree);
		return s;
	}

	std::map<ibMetaID, std::pair<wxString, const ibBackendQueryColumn*>> objectReads;
	for (const auto& kv : m_objectReads)
		objectReads.emplace(kv.first, std::make_pair(kv.second.m_prefix, kv.second.m_leaf));

	ibSelector s(std::make_unique<ibResultRowCursor>(m_source, m_matColumns, std::move(objectReads)), mode);
	s.WithTotals(m_totalLevels, m_totalAggregates, m_totalsOverall);            // fold by the door's TotalBy config
	s.WithSource(m_srcHolder, m_srcQueryable, m_srcSelectCols, m_srcConditions);  // enable lazy sub-selections
	// ⚠ AND IT IS NOT FOLDED HERE. A selection is configured AFTER it is made — MakeChild adds
	// ByParentRef / ByGroups / Aggregating to what this hands back — so folding at this point would
	// fold by a configuration that is not complete yet, and the settings arriving next would have
	// nothing left to change. Whoever finishes configuring is the one who says ReadRows().
	return s;
}

// ONE BRANCH OF THE SAME FOLD. The read, the fold and the tree are the ones above — only the WALK
// is narrowed, which is why this is a line and not a second Select: a branch is not another result.
ibSelector ibDataQueryResult::Select(ibSelectKind mode, const wxString& branch)
{
	// ⭐⭐ FOLDED ONCE, WALKED MANY TIMES. The branches share the fold as well as the read: the rows
	// come as a cursor, so the SECOND branch to ask would scan a cursor the first one already drank
	// dry — live, that surfaced as "Error retrieving Next record" from the driver the moment a
	// composition's second output started (2026-08-27).
	//
	// The fold happens on a selection of its own, thrown away straight after, because a selection
	// builds its tree and its VISITS together (EnsureWalk) and the visits depend on things the
	// caller states afterwards — the branch, WalkOverall, the level's OrderBy. Folding on the
	// branch's own selection would freeze a walk configured only half way.
	if (m_readyTree == nullptr && m_branchTree == nullptr) {
		ibSelector fold = Select(mode);
		fold.ReadRows();                  // one pass over the cursor, and it is released here
		m_branchTree = fold.FoldedTree();
	}

	ibSelector s = Select(mode);
	s.WalkBranch(branch);
	if (m_branchTree != nullptr)
		s.WithReadyTree(m_branchTree);    // hands over a Clone(), so one branch's walk cannot disturb another's
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