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
#include "queryRamTable.h"                                           // ibQueryRamTable — the empty selection an ALLOWED read yields on a refusal
#include "backend/session/session.h"                                 // ibSession::Current()->Holder()
#include "backend/guid.h"                                            // ibGuid -> wxString (WhereKey / WhereKeyIn)
#include "backend/backend_exception.h"                               // ibBackendAccessException

// NOTE: this TU intentionally includes NO L2 (databaseQueryBuilder.h) and NO
// field-machinery (metaAttributeObject.h) — the door speaks columns + the provider
// ABSTRACTION it pulls from the queryable; queryProvider.cpp owns all the lowering.
// Keep it that way. The table right is not asked here either: the policy walks the
// sources and resolves their metaobjects already, so it asks — see ibRuntimeAccessPolicy.

// ==========================================================================
// ibDataQueryBuilder — construction + fluent accumulation (no execution here).
// ==========================================================================

ibDataQueryBuilder::ibDataQueryBuilder()
	: 
	m_holder(ibSession::Current() != nullptr ? ibSession::Current()->Holder() : nullptr),
	// Pull the RLS policy from the SAME session the holder came from — no new
	// runtime include; the door still sees only the ibAccessPolicy interface.
	m_policy(ibSession::Current() != nullptr ? ibSession::Current()->GetAccessPolicy() : nullptr)
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

// A NAMED QUERY this statement declares. The inner door is copied whole — it is an ordinary door,
// and what makes it a CTE is being declared here rather than executed on its own.
ibDataQueryBuilder& ibDataQueryBuilder::With(const wxString& name, const ibDataQueryBuilder& inner)
{
	if (name.IsEmpty())
		return *this;   // a nameless declaration cannot be read back — nothing to say
	// DECLARED ONCE. Two declarations of one name is a query the engine refuses, and the caller that
	// mentions a result twice means one declaration, not two.
	for (const ibNamedQuery& declared : m_with)
		if (declared.m_name.IsSameAs(name, false))
			return *this;
	m_with.push_back({ name, std::make_shared<ibDataQueryBuilder>(inner) });
	return *this;
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

// Depth-first collect of every Source leaf under a node (Join -> both sides; Union -> all parts).
static void CollectQueryableSources(const std::shared_ptr<ibQueryNode>& node,
                                    std::vector<const ibBackendQueryable*>& out)
{
	if (!node)
		return;
	switch (node->m_kind) {
	case ibQueryNode::Kind::Source:
		if (node->m_queryable != nullptr)
			out.push_back(node->m_queryable);
		break;
	case ibQueryNode::Kind::Join:
		CollectQueryableSources(node->m_left,  out);
		CollectQueryableSources(node->m_right, out);
		break;
	case ibQueryNode::Kind::Union:
		for (const auto& part : node->m_parts)
			CollectQueryableSources(part, out);
		break;
	}
}

bool ibDataQueryBuilder::GetSources(std::vector<const ibBackendQueryable*>& sources) const
{
	sources.clear();
	if (m_root)
		CollectQueryableSources(m_root, sources);
	else if (m_queryable != nullptr)
		sources.push_back(m_queryable);   // single-source builder set via ctor, no tree yet
	return !sources.empty();
}

ibQueryPredicatePtr ibDataQueryBuilder::GetWherePredicate() const
{
	// The .Where(tree) predicate, then every .Where(col,op,val) condition AND-folded onto it — one tree
	// (null = no WHERE at all). Mirrors how the SQL builder AND-combines m_predicate with m_conditions.
	ibQueryPredicatePtr result = m_predicate;
	for (const ibQueryCondition& c : m_conditions) {
		ibQueryPredicatePtr leaf = ibQueryPredicate::Leaf(c);
		result = result ? ibQueryPredicate::Compose(ibQueryPredicateKind::And, result, leaf) : leaf;
	}
	return result;
}

const ibBackendQueryable* ibDataQueryBuilder::AdoptOwnedSource(std::shared_ptr<const ibBackendQueryable> src)
{
	const ibBackendQueryable* raw = src.get();
	if (src) m_ownedSources.push_back(std::move(src));   // owned WITH the builder -> travels into any copy / subquery
	return raw;
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

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibBackendColumnRawDB& rawColumn, const ibValue& value)
{
	// Own a copy of the caller's temporary raw column; the condition holds a stable pointer.
	m_ownedRawColumns.push_back(std::make_shared<ibBackendColumnRawDB>(rawColumn));
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

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibQueryCondition& condition)
{
	m_conditions.push_back(condition);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereIn(const ibBackendQueryColumn* col, const std::vector<ibValue>& values)
{
	ibQueryCondition c;
	c.m_col = col;
	c.m_op  = ibQueryFilterOp::In;
	c.m_values.reserve(values.size());
	for (const ibValue& v : values)
		if (!v.IsNull()) c.m_values.push_back(v);   // a NULL key joins nothing; IN (…, NULL) misbehaves
	m_conditions.push_back(std::move(c));
	return *this;
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

ibDataQueryBuilder& ibDataQueryBuilder::OrderByExpr(const ibQueryColumnExprPtr& expr, bool ascending)
{
	if (!expr)
		return *this;
	ibQuerySortItem s;
	s.m_ascending = ascending;
	s.m_expr      = expr;   // computed sort — the provider lowers it (BuildColumnExpr) and orders on the expression
	m_sorts.push_back(std::move(s));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::GroupBy(const ibBackendQueryColumn* col)
{
	if (col) {   // plain key — empty parallel path / no expression
		m_groupBy.push_back(col);
		m_groupPaths.emplace_back();
		m_groupExprs.emplace_back();
		m_groupAliases.emplace_back();
	}
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::GroupBy(const std::vector<const ibBackendQueryColumn*>& path)
{
	if (path.empty()) return *this;
	if (path.size() == 1) return GroupBy(path.front());                  // not a dot-walk — plain key
	m_groupBy.push_back(path.back());                                    // leaf is the grouped column
	m_groupPaths.push_back(path);                                        // its reference path (provider joins it)
	m_groupExprs.emplace_back();
	m_groupAliases.emplace_back();
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::GroupByExpr(const ibQueryColumnExprPtr& expr, const wxString& alias)
{
	if (!expr || alias.IsEmpty())
		return *this;   // an unnamed computed key could not be read back — refuse it rather than group blind
	m_groupBy.push_back(nullptr);        // no column: the slot is the expression's
	m_groupPaths.emplace_back();
	m_groupExprs.push_back(expr);
	m_groupAliases.push_back(alias);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const ibBackendQueryColumn* col, const wxString& alias, bool distinct)
{
	// Route to the current context: Totals() → the TotalBy common set, else the GroupBy set.
	AggregateItem item{ fn, col, alias, {} };
	item.m_distinct = distinct;
	(m_aggInTotals ? m_totalAggregates : m_aggregates).push_back(std::move(item));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const std::vector<const ibBackendQueryColumn*>& path, const wxString& alias, bool distinct)
{
	if (path.empty()) return *this;
	if (path.size() == 1) return Aggregate(fn, path.front(), alias, distinct);   // not a dot-walk — plain input
	AggregateItem item{ fn, path.back(), alias, path };
	item.m_distinct = distinct;
	(m_aggInTotals ? m_totalAggregates : m_aggregates).push_back(std::move(item));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const ibQueryColumnExprPtr& expr, const wxString& alias, bool distinct)
{
	// COMPUTED aggregate input — SUM(Qty * Price). The provider lowers m_expr via BuildColumnExpr.
	if (!expr) return *this;
	AggregateItem item{ fn, nullptr, alias, {} };
	item.m_expr = expr;
	item.m_distinct = distinct;
	(m_aggInTotals ? m_totalAggregates : m_aggregates).push_back(std::move(item));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::TotalByLevel(ibTotalLevel level)
{
	// A level with no field is not a level. Fields with no column are dropped here rather than
	// downstream, where a null column reads as "group everything into one" — the opposite answer.
	level.m_fields.erase(std::remove_if(level.m_fields.begin(), level.m_fields.end(),
		[](const ibTotalField& f) { return f.m_col == nullptr; }), level.m_fields.end());
	if (!level.m_fields.empty()) m_totals.push_back(std::move(level));
	return *this;
}

// ⭐ THE DETAIL LEVEL — the rows themselves, under the deepest heading. It IS an empty level in the
// config (the fold reads "no fields" as "no group here"), and it has a verb of its own because the
// guard above must keep refusing an empty level that arrived by ACCIDENT: a level whose fields all
// failed to resolve is a level that did not resolve, not a request for detail rows. One shape
// downstream, two intents upstream, and no way to mistake the second for the first.
//
// Goes LAST, always — there is nothing to group below the rows.
//
// ⚠ LAST IN THE CONFIG IS NOT THE SAME AS DEEPEST IN THE TREE. In a cross-table the rows read down
// the page and the column keys stand across them, so a detail record hangs under the last ROW
// heading and carries cells of its own (Max, 2026-08-26: "its own line, cells by the columns").
// Where it hangs is the FOLD's answer — it is the one that knows which way each level reads — and
// keeping the level itself last is what makes every level's NUMBER mean the same thing it always
// did: the position of the dimension it belongs to, which is what the result's schema is numbered
// by too.
// ⭐ AND IT SAYS WHICH WAY IT READS, like every other level. Detail records are legitimate on BOTH
// axes (Max, 2026-08-25 and again 2026-08-26: "exactly as a detail record is in the rows, so in the
// columns"): down the page each record is a LINE of the table; across it each record is a COLUMN of
// its own. One level, one word of difference, and the fold hangs it accordingly.
ibDataQueryBuilder& ibDataQueryBuilder::TotalsDetails(ibTotalsAxis axis)
{
	ibTotalLevel level;
	level.m_axis = axis;
	m_totals.push_back(std::move(level));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::TotalBy(const ibBackendQueryColumn* col, ibDimensionKind dim)
{
	return TotalByLevel(ibTotalLevel::One(col, dim));
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

ibTotalField ibDataQueryBuilder::DeclareDimDotWalk(const std::vector<const ibBackendQueryColumn*>& path,
	const ibBackendQueryColumn* dimCol, const wxString& alias, ibDimensionKind dim)
{
	if (path.size() < 2 || dimCol == nullptr) return ibTotalField{ nullptr, dim };
	m_dimWalks.push_back(ibDotWalkColumn{ path, alias });    // provider projects the leaf under `alias`
	// A NON-SCALAR leaf is projected as a SPREAD under that alias and reassembles on the read — the
	// result has to be told, because only this call knows both halves (see StampResult).
	if (!path.back()->IsRawColumn())
		m_dimObjectReads.push_back({ dimCol->GetColumnId(), alias, path.back() });
	// The fold groups by dimCol's OWN unique model id, and the SQL names the leaf — so the field
	// carries the path that tells them apart (see ibTotalField).
	return ibTotalField{ dimCol, dim, path };
}

ibDataQueryBuilder& ibDataQueryBuilder::TotalByDotWalk(const std::vector<const ibBackendQueryColumn*>& path,
	const ibBackendQueryColumn* dimCol, const wxString& alias, ibDimensionKind dim)
{
	const ibTotalField field = DeclareDimDotWalk(path, dimCol, alias, dim);
	ibTotalLevel level; level.m_fields.push_back(field);
	return TotalByLevel(std::move(level));
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
	m_writeRows.back().emplace_back(column, value);
	m_writeAdditive.back().push_back(false);   // index-aligned with the row — always pushed, never sparse
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::SetValue(const ibBackendColumnRawDB& rawColumn, const ibValue& value)
{
	// Own a copy of the caller's temporary raw column; the assignment holds a stable pointer.
	m_ownedRawColumns.push_back(std::make_shared<ibBackendColumnRawDB>(rawColumn));
	m_writeRows.back().emplace_back(m_ownedRawColumns.back().get(), value);
	m_writeAdditive.back().push_back(false);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::AddValue(const ibBackendQueryColumn* column, const ibValue& delta)
{
	m_writeRows.back().emplace_back(column, delta);
	m_writeAdditive.back().push_back(true);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::AddValue(const ibBackendColumnRawDB& rawColumn, const ibValue& delta)
{
	m_ownedRawColumns.push_back(std::make_shared<ibBackendColumnRawDB>(rawColumn));
	m_writeRows.back().emplace_back(m_ownedRawColumns.back().get(), delta);
	m_writeAdditive.back().push_back(true);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::NextRow()
{
	// An empty current row is not a row. A caller whose loop ends with NextRow() — the natural way
	// to write one — would otherwise stage a phantom line of nulls that the INSERT then wrote.
	if (m_writeRows.back().empty())
		return *this;

	m_writeRows.emplace_back();
	m_writeAdditive.emplace_back();
	return *this;
}

std::vector<ibQuerySortItem> ibDataQueryBuilder::EffectiveSort(
	const ibBackendQueryable* queryable, const std::vector<ibQuerySortItem>& userSorts)
{
	// Effective order = user sort ++ the queryable's PRIMARY KEY (a catalog/document's data-reference; a
	// register's recorder+line+period / period+dimensions). The primary key IS the cursor tie-breaker — the
	// same key the anchor node carries as its row-key — and a reference key now compares by its real _RRRef
	// BLOB (BuildAnchorPredicate::ReferenceKeyBlob), so `_RRRef OP blob` agrees with `ORDER BY _RRRef`. L3 just
	// appends what the user sort does not already cover, for a TOTAL order. There is no second identity
	// question beside the key: delete-by-key and key-IN read the SAME primary key (dbTableProvider's
	// RowKeyColumn).
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
	spec.m_groupBy      = &m_groupBy;
	spec.m_totals       = &m_totals;        // the TOTALS levels — see the field's note; NOT the same list
	spec.m_groupPaths   = &m_groupPaths;
	spec.m_groupExprs   = &m_groupExprs;
	spec.m_groupAliases = &m_groupAliases;
	spec.m_aggregates  = &m_aggregates;
	spec.m_totalAggregates = &m_totalAggregates;   // what the LEVELS roll — see the field's note
	spec.m_having      = &m_having;
	spec.m_writeRows   = &m_writeRows;
	spec.m_writeAdditive = &m_writeAdditive;
	spec.m_dotWalks    = &m_dotWalks;
	spec.m_dimWalks    = &m_dimWalks;
	spec.m_selectExprs = &m_selectExprs;
	spec.m_selectCols  = &m_selectCols;
	spec.m_with        = &m_with;       // the named queries this statement declares
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
	for (const ibTotalLevel&  t : m_totals)                            // TotalBy dimension fields
		for (const ibTotalField& f : t.m_fields)      add(f.m_col);   // every field of every level
	return cols;
}

// Stamp the materialise-columns + the totals config (TotalBy levels + common totals aggregates) so
// result.Select(kind) folds automatically from one snapshot.
void ibDataQueryBuilder::StampResult(ibDataQueryResult& r) const
{
	r.SetMaterialiseColumns(MaterialiseColumns());
	r.SetTotals(m_totals, m_totalAggregates, m_totalsOverall);
	// …and HOW a dot-walked object dimension is read back — see ibDataQueryResult::m_objectReads.
	for (const ibDimObjectRead& read : m_dimObjectReads)
		r.SetObjectRead(read.m_dimColumnId, read.m_alias, read.m_leaf);
	// …and the source recipe, WITH the ownership of the sources built for this query: everything
	// stamped above is raw pointers into the columns those sources publish, and the result is read
	// after this builder is gone. (Max, 2026-08-19: "the two of them own it — the query dies, the
	// result still holds the skeleton".)
	r.SetSource(m_holder, m_queryable, m_selectCols, m_conditions, m_ownedSources);
}

// An empty temp table, as a result. Nothing here knows WHY the caller wants one — that
// judgement (a closed source under SELECT ALLOWED) belongs to the layer that read the flag.
ibDataQueryResult ibMakeEmptyQueryResult(const ibBackendQueryable* queryable)
{
	return ibDataQueryResult(ibQueryRamTable{}, queryable);
}

ibDataQueryResult ibDataQueryBuilder::Execute(const ibReadPageRequest& req) const
{
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;                   // the restricted query carries no policy (no re-entrancy)
		// The policy folds its restriction and lets us through, or refuses — and a refusal is
		// told, never mimed as an empty selection: it either threw with its own words, or said
		// false and the same exception is raised here.
		//
		// UNLESS the author wrote SELECT ALLOWED, which is exactly the sentence "show me what I
		// may see": the refusal then becomes an empty read and the query carries on.
		if (!m_policy->CheckSelect(guarded, ibAccessStage::Table)) {
			if (m_allowed)
				return ibMakeEmptyQueryResult(m_queryable);
			ibBackendAccessException::Error(_("reading"));
		}
		return guarded.Execute(req);                  // L4-1 + L4-2 both land here
	}
	ibDataQueryResult r = ibQueryComposer::ExecuteRead(BuildSpec(), req);
	StampResult(r);
	return r;
}

ibQueryRelPtr ibDataQueryBuilder::BuildRelation() const
{
	// A RESTRICTED read is not a relation somebody else composes: the policy folds its restriction
	// into the query it guards, and a relation handed out unrestricted could be joined past it. The
	// caller reads rows instead — same numbers, same restriction, one materialisation.
	if (m_policy != nullptr || m_queryable == nullptr)
		return nullptr;

	// …AND NEITHER IS A QUERY THAT STANDS ON ITS OWN DECLARATIONS. A relation goes INSIDE somebody
	// else's FROM, and a `WITH` cannot travel there: it belongs to the top of a statement, and this
	// one is not the top of anything. Handing the relation over regardless would render a name whose
	// declaration stayed behind — a statement the engine cannot parse rather than a slower plan.
	if (!m_with.empty())
		return nullptr;

	// ⭐ WHICH LOWERING, DECIDED BY THE SHAPE OF THE QUERY — the same way the terminal endings are
	// chosen (`SelectAggregate()` against `Execute()`), and by the same evidence: a query that names
	// aggregates or group keys FOLDS, one that names neither PROJECTS. Two lowerings rather than one
	// with a flag, because a GROUP BY and a paged read are genuinely different trees.
	//
	// Asked of THIS source's provider. A computed / temp source answers null to both (the base
	// default), which is the honest "cannot be composed with" rather than an empty relation.
	const ibDataQuerySpec spec = BuildSpec();
	const bool folds = !m_aggregates.empty() || !m_groupBy.empty();
	return folds ? m_queryable->GetProvider().BuildAggregateRelation(spec)
	             : m_queryable->GetProvider().BuildReadRelation(spec);
}

ibDataQueryResult ibDataQueryBuilder::Execute(const ibReadPageRequest& req,
                                             ibRenderedPageCache& cache,
                                             const wxString& signature) const
{
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckSelect(guarded, ibAccessStage::Table)) {        // the list / paging path lands here too
			if (m_allowed)
				return ibMakeEmptyQueryResult(m_queryable);
			ibBackendAccessException::Error(_("reading"));
		}
		return guarded.Execute(req, cache, signature);
	}
	ibDataQueryResult r = ibQueryComposer::ExecuteReadCached(BuildSpec(), req, cache, signature);
	StampResult(r);
	return r;
}

// ONE ROW OF ZEROS — what a refused AGGREGATE yields under SELECT ALLOWED when there is nothing to
// group by. `SELECT SUM(x) FROM T` returns one row whatever T holds, so answering with NO rows says
// "there is no such question", while the honest answer to "sum of what you may see" over nothing you
// may see is a sum of nothing: zero. (With a GROUP BY the empty result IS right — no groups.)
static ibDataQueryResult MakeZeroAggregateResult(const std::vector<ibAggregateItem>& aggregates,
                                                 const ibBackendQueryable* queryable)
{
	ibQueryRamTable table;
	ibMetaID id = 1;
	for (const ibAggregateItem& aggregate : aggregates)
		table.AddColumn(id++, aggregate.m_alias, ibTypeDescription());

	const long row = table.AppendRow();
	id = 1;
	for (size_t i = 0; i < aggregates.size(); ++i)
		table.SetCell(row, id++, ibValue(ibNumber(0)));

	return ibDataQueryResult(std::move(table), queryable);
}

// THE POLICY GUARDS EVERY READ, and an AGGREGATE is a read. It was guarded on the plain terminals
// only, which meant `SELECT Amount FROM X` was refused while `SELECT SUM(Amount) FROM X` answered —
// an inference leak: totals and counts over rows the reader may not see, with SELECT ALLOWED
// silently inert on that path too. (Found by Max, 2026-08-06, by reading; confirmed by the absence
// of any policy mention in BuildSpec and in the whole composer.)
//
// A read that must NOT be filtered says so where it is written, with WithAccessPolicy(nullptr) —
// the verb that already exists for the policy's own set computation. Totals regeneration is the
// case that matters: filtered by the caller's rights it would recompute stored totals from the rows
// that caller happens to see, and wrong totals do not look like an error.
ibDataQueryResult ibDataQueryBuilder::SelectAggregate() const
{
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckSelect(guarded, ibAccessStage::Table)) {
			if (m_allowed)
				return m_groupBy.empty() && m_groupExprs.empty()
					? MakeZeroAggregateResult(m_aggregates, m_queryable)
					: ibMakeEmptyQueryResult(m_queryable);
			ibBackendAccessException::Error(_("reading"));
		}
		return guarded.SelectAggregate();
	}
	return ibQueryComposer::ExecuteAggregate(BuildSpec());
}

// Paged single-level group read (docs: group-level paging) — GROUP BY dim ORDER BY dim [dim </> anchor]
// LIMIT count. Routes to the server-side keyset group page when the shape allows (CanPageGroupLevel), else
// the unpaged all-groups aggregate. The single-scalar-dim TOTALS drill uses this instead of the detail fold.
ibDataQueryResult ibDataQueryBuilder::SelectAggregatePage(const ibReadPageRequest& page) const
{
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckSelect(guarded, ibAccessStage::Table)) {
			// A PAGE OF GROUPS always groups — so an empty page is the right refusal here, and the
			// zero row above would be a group that does not exist.
			if (m_allowed)
				return ibMakeEmptyQueryResult(m_queryable);
			ibBackendAccessException::Error(_("reading"));
		}
		return guarded.SelectAggregatePage(page);
	}
	return ibQueryComposer::ExecuteGroupLevelPage(BuildSpec(), page);
}

// GUARDED TOO, though nothing calls it today. A read terminal with no policy on it is a loaded gun:
// the first caller gets an unchecked read and no one notices, because nothing looks wrong.
// ⭐ THE TOTALS, FOLDED BY THE DBMS — as a RESULT rather than as a bare tree, so a caller that
// speaks in results (the query lowering, and therefore every composition) can use the push-down it
// could not reach before.
//
// True only when the shape actually pushes down: single source, real DB queryable, keys the dialect
// can group by, and a driver that folds levels itself (Firebird 5 / PostgreSQL; SQLite does not).
// Anywhere else this answers false and the caller reads and folds as it always did — the two paths
// produce the same tree, so which one ran is a question of cost, never of the number.
bool ibDataQueryBuilder::TryTotalsPushdown(ibDataQueryResult& out) const
{
	if (m_policy != nullptr) {
		// ⭐⭐ A POLICY DECIDES WHAT MAY BE READ, NEVER WHICH ROAD READS IT.
		//
		// This used to answer `false` on the mere PRESENCE of one — and the door's default ctor pulls
		// the session's policy, so in enterprise mode every door has one and the server-side fold
		// could not fire for ANY report, at any setting, on any engine. Nothing said so: the refusal
		// was ahead of the shape, ahead of the dialect, ahead of the flag (found 2026-08-22, after
		// the flag was forced true and still nothing reached Firebird).
		//
		// The house already has the answer, one function down: ask ONCE, then read plainly.
		// `CheckSelect` both authorises and NARROWS — it writes the policy's conditions into the
		// query handed to it — so the guarded copy carries the restriction and needs no policy of its
		// own. Same three lines as SelectTotals / Insert / Update, and the same reason: a restricted
		// query must not re-enter the policy that restricted it.
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckSelect(guarded, ibAccessStage::Table))
			return false;   // not allowed — the ordinary detail read raises it, as it always did
		return guarded.TryTotalsPushdown(out);
	}
	// ⭐ DETAIL ROWS MAY BE ASKED FOR NOW. A level with no fields used to be refused here outright —
	// `GROUP BY ROLLUP` folds the rows away, so the answer would have looked right and been missing
	// everything under the last heading. It travels as the PHANTOM LEVEL instead (the row's own
	// identity as the deepest group key, dbTableProvider.cpp), and whether THAT is possible for this
	// source is a question for the tier that knows — the same one asked below. This file states no
	// opinion about it, which is why the check that used to live here is gone rather than moved.

	// ⚠ WHETHER it pushes down is asked of the COMPOSER, not decided here: this file deliberately
	// knows no L2 and no provider internals, and "can this dialect fold levels" is exactly such a
	// question. The door asks; the tier that owns the answer answers.
	ibSelectorTree folded;
	if (!ibQueryComposer::TryFoldTotalsInDbms(BuildSpec(), folded))
		return false;

	out = ibMakeEmptyQueryResult(m_queryable);
	out.SetTotals(m_totals, m_totalAggregates, m_totalsOverall);
	out.SetReadyTree(std::move(folded));
	return true;
}

ibSelectorTree ibDataQueryBuilder::SelectTotals() const
{
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckSelect(guarded, ibAccessStage::Table)) {
			// A TOTALS TREE has no empty form to hand back that is not a lie about its shape, so
			// ALLOWED cannot soften this one: an empty tree and a tree of zeros say different
			// things, and neither is "you may not read this".
			ibBackendAccessException::Error(_("reading"));
		}
		return guarded.SelectTotals();
	}
	return ibQueryComposer::ExecuteTotals(BuildSpec());
}

bool ibDataQueryBuilder::Insert() const
{
	// CREATE. The policy is asked before (the right, plus any check it folds onto the row being
	// made) and again after, with what the statement actually did.
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckCreate(guarded, ibAccessStage::Table))
			ibBackendAccessException::Error(_("creating a record"));
		const long affected = ibQueryComposer::ExecuteWrite(guarded.BuildSpec(), WriteKind::Insert);
		if (!m_policy->CheckCreate(guarded, ibAccessStage::Value, affected))
			ibBackendAccessException::Error(_("creating a record"));
		return affected >= 0;
	}
	return ibQueryComposer::ExecuteWrite(BuildSpec(), WriteKind::Insert) >= 0;
}

bool ibDataQueryBuilder::Upsert() const
{
	// UPSERT — a "don't know if it exists" write, kept only for callers that do not (yet) emit an explicit
	// create-vs-rewrite event (constants). Row-restricted objects / documents / registers use Insert
	// (create) or Update (rewrite), so this path never carries an RLS predicate to enforce. One native
	// UPDATE-OR-INSERT statement, closed by the dialect.
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckUpdate(guarded, ibAccessStage::Table))
			ibBackendAccessException::Error(_("changing a record"));
		const long affected = ibQueryComposer::ExecuteWrite(guarded.BuildSpec(), WriteKind::Upsert);
		if (!m_policy->CheckUpdate(guarded, ibAccessStage::Value, affected))
			ibBackendAccessException::Error(_("changing a record"));
		return affected >= 0;
	}
	return ibQueryComposer::ExecuteWrite(BuildSpec(), WriteKind::Upsert) >= 0;
}

// A REWRITE THAT HIT NOTHING IS A MISS ON THE KEY, NOT A WRITE OF ZERO ROWS — AND IT MUST SAY SO.
//
// `Update()` used to answer `affected >= 0`, so nought rows read as success. The form said "saved",
// the object advanced its in-memory version marker on commit, and the row was never touched: the next
// save then compared the advanced marker against the untouched row and reported "data was changed by
// another user" — about a change that had never happened, on a base with one user. That was the
// visible half. The invisible half was worse: edits simply evaporated.
//
// It happens whenever the key bytes on hand do not match the key bytes stored. Live case: a table
// created before ibReference lost its trailing metaID keeps 20-byte _RRRef values with the old tag,
// the rewrite keys on that very column with today's 16-byte blob, and nothing matches — for those
// rows the object could be read and deleted (DELETE keys off uuid) but never changed.
//
// The distinction that keeps this honest: a rewrite keyed on the PRIMARY KEY alone addresses ONE
// named row, so nought means the row is gone or unreachable — raise. A caller that narrowed with
// .Where(...) asked a QUESTION about a set, and an empty set is a legitimate answer — stay quiet.
// (That is the shape of "clear my children's parent": no children is not a failure.)
void ibDataQueryBuilder::RaiseIfKeyedRewriteMissed(long affected) const
{
	if (affected != 0)
		return;                       // wrote something, or the driver reported failure (< 0) — not this question
	if (!m_conditions.empty() || m_predicate)
		return;                       // a narrowed rewrite: an empty set is an answer, not a miss

	ibBackendCoreException::Error(
		_("'%s': the row to rewrite was not found by its key - nothing was written"),
		m_queryable != nullptr ? m_queryable->GetQueryName() : wxString(wxT("?")));
}

bool ibDataQueryBuilder::Update() const
{
	// REWRITE of an existing row — ONE guarded UPDATE: SET the columns, WHERE the key AND the folded RLS
	// predicate. Under a policy 0 rows affected = the row is not one this role may write (the row filter
	// excluded it) -> ACCESS DENIED (fail-closed, mirrors Delete). This is what makes a write Restrict BITE
	// on the object main row — a plain UPSERT (UPDATE OR INSERT, no WHERE) folds the predicate but ignores it.
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckUpdate(guarded, ibAccessStage::Table))   // the right on the table, plus the folded restriction
			ibBackendAccessException::Error(_("changing a record"));
		const long affected = ibQueryComposer::ExecuteWrite(guarded.BuildSpec(), WriteKind::Update);
		// The SECOND question, the one only the result can answer: did the rewrite happen? Where
		// the right controls a row, nothing rewritten means the filter kept the statement off a row
		// that exists — the policy says so and this returns false or throws. Where it controls a
		// table, the count means nothing and the policy waves it through.
		if (!m_policy->CheckUpdate(guarded, ibAccessStage::Value, affected))
			ibBackendAccessException::Error(_("changing a record"));
		RaiseIfKeyedRewriteMissed(affected);
		return affected >= 0;
	}
	const long affected = ibQueryComposer::ExecuteWrite(BuildSpec(), WriteKind::Update);
	RaiseIfKeyedRewriteMissed(affected);
	return affected >= 0;
}

bool ibDataQueryBuilder::Delete() const
{
	if (m_policy != nullptr) {
		ibDataQueryBuilder guarded(*this);
		guarded.m_policy = nullptr;
		if (!m_policy->CheckDelete(guarded, ibAccessStage::Table))   // the right on the table, plus the folded restriction
			ibBackendAccessException::Error(_("deleting a record"));
		const long affected = ibQueryComposer::ExecuteWrite(guarded.BuildSpec(), WriteKind::Delete);
		// The door does not interpret this number — it hands it back. Deleting rows that are not
		// there is ordinary for a register set; for a row-controlled record it is the filter having
		// kept the statement off a row that exists. Which of the two it is, the policy knows.
		// -1 = the write itself threw (a DB error, not a denial).
		if (!m_policy->CheckDelete(guarded, ibAccessStage::Value, affected))
			ibBackendAccessException::Error(_("deleting a record"));
		return affected >= 0;
	}
	return ibQueryComposer::ExecuteWrite(BuildSpec(), WriteKind::Delete) >= 0;
}