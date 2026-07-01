////////////////////////////////////////////////////////////////////////////
//	Description : ibDataQueryBuilder — the L3 DOOR. Accumulates a metadata query
//	              (From / Where / OrderBy / GroupBy / aggregate / SetValue) over a
//	              borrowed holder and is L2- and BACKING-blind: it knows nothing of
//	              ibQueryIR, of DB vs RAM, of the attribute field-machinery. All of
//	              that lives behind the provider in queryProvider.cpp; the terminal
//	              bodies (Select / SelectAggregate / Upsert / DeleteByKey / WriteRow)
//	              are defined THERE. This TU holds only the surface: the fluent verbs
//	              + EffectiveSort. See docs/query-language-arc.md §18, §22.
////////////////////////////////////////////////////////////////////////////

#include "dataQueryBuilder.h"
#include "queryProvider.h"                                           // ibBackendQueryProvider abstraction (NO L2 — the door runs through it)
#include "backend/session/session.h"                                 // ibSession::Current()->Holder()
#include "backend/guid.h"                                            // ibGuid -> wxString (WhereKey / WhereKeyIn)

// NOTE: this TU intentionally includes NO L2 (databaseQueryBuilder.h) and NO
// field-machinery (metaAttributeObject.h) — the door speaks columns + the provider
// ABSTRACTION it pulls from the queryable; queryProvider.cpp owns all the lowering.
// Keep it that way.

// ==========================================================================
// ibDataQueryBuilder — construction + fluent accumulation (no execution here).
// ==========================================================================

ibDataQueryBuilder::ibDataQueryBuilder()
	: m_holder(ibSession::Current() != nullptr ? ibSession::Current()->Holder() : nullptr)
{
}

ibDataQueryBuilder::ibDataQueryBuilder(ibDatabaseConnectionHolder* holder)
	: m_holder(holder)
{
}

ibDataQueryBuilder& ibDataQueryBuilder::From(const ibBackendQueryable* queryable, const wxString& alias)
{
	m_queryable = queryable;                              // primary source (single-source fast path)
	m_root      = ibQueryNode::Source(queryable, alias); // the relational tree root
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Join(const ibBackendQueryable* queryable, ibQueryJoinKind kind, const wxString& alias)
{
	return Join(queryable, nullptr, nullptr, kind, alias);   // auto-join by reference (dot-walk style)
}

ibDataQueryBuilder& ibDataQueryBuilder::Join(const ibBackendQueryable* queryable,
	const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	ibQueryJoinKind kind, const wxString& alias)
{
	return Join(queryable, onLeft, onRight, ibJoinCompareOp::Eq, kind, alias);   // equality -> hash-join fast path
}

// The single point that appends a left-deep JOIN node (new source on the right) carrying the ON condition.
ibDataQueryBuilder& ibDataQueryBuilder::JoinNode(const ibBackendQueryable* queryable, ibQueryJoinKind kind,
	const wxString& alias, const ibJoinOn& on)
{
	auto node = std::make_shared<ibQueryNode>();   // left-deep accumulation: (prior tree) joined with the new source
	node->m_kind     = ibQueryNode::Kind::Join;
	node->m_left     = m_root;
	node->m_right    = ibQueryNode::Source(queryable, alias);
	node->m_joinKind = kind;
	node->m_on       = on;
	m_root = node;
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Join(const ibBackendQueryable* queryable,
	const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	ibJoinCompareOp onOp, ibQueryJoinKind kind, const wxString& alias)
{
	ibJoinOn on; on.m_colL = onLeft; on.m_colR = onRight; on.m_op = onOp;   // onOp != Eq -> theta (RAM nested-loop)
	return JoinNode(queryable, kind, alias, on);
}

ibDataQueryBuilder& ibDataQueryBuilder::Join(const ibBackendQueryable* queryable,
	const ibQueryColumnExprPtr& onExprL, const ibQueryColumnExprPtr& onExprR,
	ibJoinCompareOp onOp, ibQueryJoinKind kind, const wxString& alias)
{
	// Computed ON (a.x+1 <op> b.y): no key columns -- both sides are exprs evaluated per pair (RAM theta).
	ibJoinOn on; on.m_exprL = onExprL; on.m_exprR = onExprR; on.m_op = onOp;
	return JoinNode(queryable, kind, alias, on);
}

ibDataQueryBuilder& ibDataQueryBuilder::CrossJoin(const ibBackendQueryable* queryable, ibQueryJoinKind kind, const wxString& alias)
{
	// CROSS / ON TRUE -- keyless cartesian; the composer's RAM stitch does the product on a null key.
	ibJoinOn on; on.m_cross = true;
	return JoinNode(queryable, kind, alias, on);
}

ibDataQueryBuilder& ibDataQueryBuilder::Union(const ibBackendQueryable* queryable, const wxString& alias,
                                              bool keepDuplicates)
{
	// Stack vertically: extend an existing Union, else wrap the prior root + the new part.
	// m_partAll rides parallel to m_parts (parts[0] = true — no operator precedes the first branch).
	auto rhs = ibQueryNode::Source(queryable, alias);
	if (m_root && m_root->m_kind == ibQueryNode::Kind::Union) {
		m_root->m_parts.push_back(rhs);
		m_root->m_partAll.push_back(keepDuplicates);
	}
	else {
		auto node = std::make_shared<ibQueryNode>();
		node->m_kind = ibQueryNode::Kind::Union;
		if (m_root) { node->m_parts.push_back(m_root); node->m_partAll.push_back(true); }
		node->m_parts.push_back(rhs);
		node->m_partAll.push_back(keepDuplicates);
		m_root = node;
	}
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibBackendQueryColumn* col,
                                              ibQueryFilterOp comparison, const ibValue& value)
{
	m_conditions.push_back(ibQueryCondition{ col, comparison, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibBackendQueryColumn* col, const ibValue& value)
{
	m_conditions.push_back(ibQueryCondition{ col, ibQueryFilterOp::Equal, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibRawDBColumn& rawColumn, const ibValue& value)
{
	// Own a copy of the caller's temporary raw column; the condition holds a stable pointer.
	m_ownedRawColumns.push_back(std::make_shared<ibRawDBColumn>(rawColumn));
	m_conditions.push_back(ibQueryCondition{ m_ownedRawColumns.back().get(), ibQueryFilterOp::Equal, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereCompare(const ibBackendQueryColumn* col,
                                                     ibQueryFilterOp op, const ibValue& value)
{
	return Where(col, op, value);   // identical now (one m_op) — kept as a named alias for ordered/LIKE callers
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereLike(const ibBackendQueryColumn* col, const ibValue& pattern)
{
	return WhereCompare(col, ibQueryFilterOp::Like, pattern);
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereExpr(const ibQueryColumnExprPtr& expr,
                                                  ibQueryFilterOp comparison, const ibValue& value)
{
	if (!expr)
		return *this;
	ibQueryCondition c;
	c.m_op         = comparison;
	c.m_value      = value;
	c.m_expr       = expr;
	m_conditions.push_back(std::move(c));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereExprCompare(const ibQueryColumnExprPtr& expr,
                                                         ibQueryFilterOp op, const ibValue& value)
{
	return WhereExpr(expr, op, value);   // identical now (one m_op) — kept as a named alias for ordered/LIKE callers
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibQueryPredicatePtr& predicate)
{
	// Full boolean WHERE tree. AND-combine if the door is handed more than one (each Where(tree)
	// adds a conjunct); the provider AND-folds it with the flat verb conditions / row-key filters.
	if (!predicate)
		return *this;
	m_predicate = m_predicate
		? ibQueryPredicate::Compose(ibQueryPredicateKind::And, m_predicate, predicate)
		: predicate;
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const std::vector<const ibBackendQueryColumn*>& path,
	ibQueryFilterOp comparison, const ibValue& value)
{
	if (path.empty())
		return *this;
	ibQueryCondition c;
	c.m_col        = path.back();   // the leaf — carries the type for the provider's compare
	c.m_op         = comparison;
	c.m_value      = value;
	c.m_path       = path;
	m_conditions.push_back(std::move(c));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereCompare(const std::vector<const ibBackendQueryColumn*>& path,
	ibQueryFilterOp op, const ibValue& value)
{
	return Where(path, op, value);   // identical now (one m_op) — kept as a named alias for ordered/LIKE callers
}

ibDataQueryBuilder& ibDataQueryBuilder::OrderBy(const ibBackendQueryColumn* col, bool ascending)
{
	m_sorts.push_back(ibQuerySortItem{ col, ascending });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::OrderBy(const std::vector<const ibBackendQueryColumn*>& path, bool ascending)
{
	if (path.empty())
		return *this;
	ibQuerySortItem s;
	s.m_col       = path.back();
	s.m_ascending = ascending;
	s.m_path      = path;
	m_sorts.push_back(std::move(s));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::GroupBy(const ibBackendQueryColumn* col)
{
	if (col) { m_groupBy.push_back(col); m_groupPaths.emplace_back(); }   // plain key — empty parallel path
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::GroupBy(const std::vector<const ibBackendQueryColumn*>& path)
{
	if (path.empty()) return *this;
	if (path.size() == 1) return GroupBy(path.front());                  // not a dot-walk — plain key
	m_groupBy.push_back(path.back());                                    // leaf is the grouped column
	m_groupPaths.push_back(path);                                        // its reference path (provider joins it)
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const ibBackendQueryColumn* col, const wxString& alias)
{
	// Route to the current context: Totals() → the TotalBy common set, else the GroupBy set.
	(m_aggInTotals ? m_totalAggregates : m_aggregates).push_back(AggregateItem{ fn, col, alias, {} });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const std::vector<const ibBackendQueryColumn*>& path, const wxString& alias)
{
	if (path.empty()) return *this;
	if (path.size() == 1) return Aggregate(fn, path.front(), alias);     // not a dot-walk — plain aggregate input
	(m_aggInTotals ? m_totalAggregates : m_aggregates).push_back(AggregateItem{ fn, path.back(), alias, path });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const ibQueryColumnExprPtr& expr, const wxString& alias)
{
	// COMPUTED aggregate input — SUM(Qty * Price). The provider lowers m_expr via BuildColumnExpr.
	if (!expr) return *this;
	AggregateItem item{ fn, nullptr, alias, {} };
	item.m_expr = expr;
	(m_aggInTotals ? m_totalAggregates : m_aggregates).push_back(std::move(item));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::TotalBy(const ibBackendQueryColumn* col, ibDimensionKind dim)
{
	if (col != nullptr) m_totals.push_back(ibTotalLevel{ col, dim });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::TotalBy(const std::vector<const ibBackendQueryColumn*>& path, ibDimensionKind dim)
{
	if (path.empty()) return *this;
	if (path.size() == 1) return TotalBy(path.front(), dim);   // not a dot-walk — plain dimension column
	// A dot-walk dimension without a synthetic column would project the leaf under its own physical name and
	// CLASH with the main table on a self-reference (Parent.Code). The lowering must route through
	// TotalByDotWalk with a synthetic column; reaching here with a raw path is a programming error.
	wxASSERT_MSG(false, wxT("dot-walk TOTALS dimension must go through TotalByDotWalk (synthetic column)"));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::TotalByDotWalk(const std::vector<const ibBackendQueryColumn*>& path,
	const ibBackendQueryColumn* dimCol, const wxString& alias, ibDimensionKind dim)
{
	if (path.size() < 2 || dimCol == nullptr) return *this;
	m_dimWalks.push_back(ibDotWalkColumn{ path, alias });    // provider projects the leaf scalar AS `alias`
	m_totals.push_back(ibTotalLevel{ dimCol, dim });         // fold groups by dimCol's OWN unique model id
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Having(AggregateFn fn,
	const ibBackendQueryColumn* col, ibQueryFilterOp op, const ibValue& value)
{
	m_having.push_back(HavingItem{ fn, col, op, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::SelectPath(
	const std::vector<const ibBackendQueryColumn*>& path, const wxString& alias)
{
	if (!path.empty())
		m_dotWalks.push_back(ibDotWalkColumn{ path, alias });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Select(const ibBackendQueryColumn* col, const wxString& alias)
{
	if (col != nullptr)
		m_selectCols.emplace_back(col, alias);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::SelectExpr(const ibQueryColumnExprPtr& expr, const wxString& alias)
{
	if (expr != nullptr)
		m_selectExprs.push_back(ibQueryColumnSelect{ expr, alias });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereKey(const ibGuid& rowGuid)
{
	// Row-key equality — m_col == nullptr means the row-key column in the IR builder.
	m_conditions.push_back(ibQueryCondition{
		nullptr, ibQueryFilterOp::Equal, ibValue(wxString(rowGuid)) });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereKeyIn(const std::vector<ibGuid>& rowGuids)
{
	for (const ibGuid& g : rowGuids)
		m_keyIn.push_back(ibValue(wxString(g)));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::SetValue(const ibBackendQueryColumn* column, const ibValue& value)
{
	m_writeValues.emplace_back(column, value);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::SetValue(const ibRawDBColumn& rawColumn, const ibValue& value)
{
	// Own a copy of the caller's temporary raw column; the assignment holds a stable pointer.
	m_ownedRawColumns.push_back(std::make_shared<ibRawDBColumn>(rawColumn));
	m_writeValues.emplace_back(m_ownedRawColumns.back().get(), value);
	return *this;
}

std::vector<ibQuerySortItem> ibDataQueryBuilder::EffectiveSort(
	const ibBackendQueryable* queryable, const std::vector<ibQuerySortItem>& userSorts)
{
	// Effective order = user sort ++ the queryable's PRIMARY KEY (a catalog/document's data-reference; a
	// register's recorder+line+period / period+dimensions). The primary key IS the cursor tie-breaker — the
	// same key the anchor node carries as its row-key — and a reference key now compares by its real _RRRef
	// BLOB (BuildAnchorPredicate::ReferenceKeyBlob), so `_RRRef OP blob` agrees with `ORDER BY _RRRef`. L3 just
	// appends what the user sort does not already cover, for a TOTAL order. GetIdentitySort (the uuid string)
	// stays the row-key field for delete-by-key / key-in restore — a different concern.
	std::vector<ibQuerySortItem> effective = userSorts;
	for (const ibBackendQueryColumn* pk : queryable->GetPrimaryKeyColumns()) {
		if (pk == nullptr) continue;
		bool dup = false;
		for (const ibQuerySortItem& e : effective)
			if (e.m_col == pk) { dup = true; break; }
		if (!dup) effective.push_back(ibQuerySortItem{ pk, true });
	}
	return effective;
}

// ==========================================================================
// Terminals — THIN delegators. The door builds the spec (a view of its accumulated
// state) and runs it through the ibBackendQueryProvider it pulls from the queryable.
// It names NO concrete provider and NO L2 type — the provider owns all of that, and a
// computed source's provider realizes the read its own way. (docs §22.4)
// ==========================================================================
ibDataQuerySpec ibDataQueryBuilder::BuildSpec() const
{
	ibDataQuerySpec spec;
	spec.m_holder      = m_holder;
	spec.m_queryable   = m_queryable;       // current/primary source (single-source fast path)
	spec.m_root        = m_root.get();      // the relational tree — the composer walks it
	spec.m_conditions  = &m_conditions;
	spec.m_predicate   = m_predicate;       // full boolean WHERE tree (null = none)
	spec.m_keyIn       = &m_keyIn;
	spec.m_sorts       = &m_sorts;
	spec.m_groupBy     = &m_groupBy;
	spec.m_groupPaths  = &m_groupPaths;
	spec.m_aggregates  = &m_aggregates;
	spec.m_having      = &m_having;
	spec.m_writeValues = &m_writeValues;
	spec.m_dotWalks    = &m_dotWalks;
	spec.m_dimWalks    = &m_dimWalks;
	spec.m_selectExprs = &m_selectExprs;
	spec.m_selectCols  = &m_selectCols;
	spec.m_distinct    = m_distinct;
	spec.m_topCount    = m_top;
	return spec;
}

// The door runs the query through the COMPOSER (the tree executor) — never a concrete
// provider. For a single source the composer delegates to that source's provider;
// multi-source (Join / Union) it orchestrates. The door is blind to which.
// Columns a later result.Select(mode) must drain into the snapshot = the Select() output list +
// the aggregate inputs + the TotalBy dimension fields (deduped). The door knows them all; it stamps
// them on the result so a fold reads exactly what it needs from one snapshot.
std::vector<const ibBackendQueryColumn*> ibDataQueryBuilder::MaterialiseColumns() const
{
	std::vector<const ibBackendQueryColumn*> cols;
	auto add = [&](const ibBackendQueryColumn* c) {
		if (c == nullptr) return;
		for (const ibBackendQueryColumn* e : cols) if (e == c) return;
		cols.push_back(c);
	};
	for (const auto& sc : m_selectCols)              add(sc.first);
	for (const AggregateItem& a : m_aggregates)       add(a.m_col);
	for (const AggregateItem& a : m_totalAggregates)  add(a.m_col);   // totals aggregates roll from the snapshot
	for (const ibTotalLevel&  t : m_totals)           add(t.m_col);   // TotalBy dimension fields
	return cols;
}

// Stamp the materialise-columns + the totals config (TotalBy levels + common totals aggregates) so
// result.Select(kind) folds automatically from one snapshot.
void ibDataQueryBuilder::StampResult(ibDataQueryResult& r) const
{
	r.SetMaterialiseColumns(MaterialiseColumns());
	r.SetTotals(m_totals, m_totalAggregates);
	r.SetSource(m_holder, m_queryable, m_selectCols, m_conditions);   // lazy sub-selections + ref-dimension resolution
}

ibDataQueryResult ibDataQueryBuilder::Execute(const ibReadPageRequest& req) const
{
	ibDataQueryResult r = ibQueryComposer::ExecuteRead(BuildSpec(), req);
	StampResult(r);
	return r;
}

ibDataQueryResult ibDataQueryBuilder::Execute(const ibReadPageRequest& req,
                                             ibRenderedPageCache& cache,
                                             const wxString& signature) const
{
	ibDataQueryResult r = ibQueryComposer::ExecuteReadCached(BuildSpec(), req, cache, signature);
	StampResult(r);
	return r;
}

ibDataQueryResult ibDataQueryBuilder::SelectAggregate() const
{
	return ibQueryComposer::ExecuteAggregate(BuildSpec());
}

ibSelectorTree ibDataQueryBuilder::SelectTotals() const
{
	return ibQueryComposer::ExecuteTotals(BuildSpec());
}

bool ibDataQueryBuilder::Insert() const
{
	return ibQueryComposer::ExecuteWrite(BuildSpec(), WriteKind::Insert);
}

bool ibDataQueryBuilder::Upsert() const
{
	return ibQueryComposer::ExecuteWrite(BuildSpec(), WriteKind::Upsert);
}

bool ibDataQueryBuilder::Delete() const
{
	return ibQueryComposer::ExecuteWrite(BuildSpec(), WriteKind::Delete);
}