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
	// Left-deep accumulation: (prior tree) ⋈ new source.
	auto node = std::make_shared<ibQueryNode>();
	node->m_kind     = ibQueryNode::Kind::Join;
	node->m_left     = m_root;
	node->m_right    = ibQueryNode::Source(queryable, alias);
	node->m_onLeft   = onLeft;
	node->m_onRight  = onRight;
	node->m_joinKind = kind;
	m_root = node;
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Union(const ibBackendQueryable* queryable, const wxString& alias)
{
	// Stack vertically: extend an existing Union, else wrap the prior root + the new part.
	auto rhs = ibQueryNode::Source(queryable, alias);
	if (m_root && m_root->m_kind == ibQueryNode::Kind::Union) {
		m_root->m_parts.push_back(rhs);
	}
	else {
		auto node = std::make_shared<ibQueryNode>();
		node->m_kind = ibQueryNode::Kind::Union;
		if (m_root) node->m_parts.push_back(m_root);
		node->m_parts.push_back(rhs);
		m_root = node;
	}
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibBackendQueryColumn* col,
                                              ibComparisonType comparison, const ibValue& value)
{
	m_conditions.push_back(ibQueryCondition{ col, comparison, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibBackendQueryColumn* col, const ibValue& value)
{
	m_conditions.push_back(ibQueryCondition{ col, ibComparisonType::ibComparisonType_Equal, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibRawDBColumn& rawColumn, const ibValue& value)
{
	// Own a copy of the caller's temporary raw column; the condition holds a stable pointer.
	m_ownedRawColumns.push_back(std::make_shared<ibRawDBColumn>(rawColumn));
	m_conditions.push_back(ibQueryCondition{ m_ownedRawColumns.back().get(), ibComparisonType::ibComparisonType_Equal, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereCompare(const ibBackendQueryColumn* col,
                                                     ibQueryFilterOp op, const ibValue& value)
{
	ibQueryCondition c;
	c.m_col        = col;
	c.m_value      = value;
	c.m_explicitOp = true;
	c.m_op         = op;
	m_conditions.push_back(c);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereLike(const ibBackendQueryColumn* col, const ibValue& pattern)
{
	return WhereCompare(col, ibQueryFilterOp::Like, pattern);
}

ibDataQueryBuilder& ibDataQueryBuilder::OrderBy(const ibBackendQueryColumn* col, bool ascending)
{
	m_sorts.push_back(ibQuerySortItem{ col, ascending });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::GroupBy(const ibBackendQueryColumn* col)
{
	if (col) m_groupBy.push_back(col);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const ibBackendQueryColumn* col, const wxString& alias)
{
	m_aggregates.push_back(AggregateItem{ fn, col, alias });
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

ibDataQueryBuilder& ibDataQueryBuilder::WhereKey(const ibGuid& rowGuid)
{
	// Row-key equality — m_col == nullptr means the row-key column in the IR builder.
	m_conditions.push_back(ibQueryCondition{
		nullptr, ibComparisonType::ibComparisonType_Equal, ibValue(wxString(rowGuid)) });
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
	// Effective order = user sort ++ the queryable's identity tail (recorder+line
	// / period?+dims for registers; the uuid column for catalogs — a REAL column now,
	// no null sentinel). The queryable decides its identity; L3 just appends what is not
	// already in the user sort, so the cursor has a TOTAL order with no family fork. Pure
	// (no L2, no field-machinery) — that is why it stays on the door.
	std::vector<ibQuerySortItem> effective = userSorts;
	for (const ibQuerySortItem& id : queryable->GetIdentitySort()) {
		bool dup = false;
		for (const ibQuerySortItem& e : effective)
			// Column descriptors are stable singletons (an attribute upcast yields one
			// deterministic column pointer), so pointer equality == the former metaID
			// equality — no resolve needed for the identity-tail dedup.
			if (e.m_col == id.m_col) { dup = true; break; }
		if (!dup) effective.push_back(id);
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
	spec.m_keyIn       = &m_keyIn;
	spec.m_sorts       = &m_sorts;
	spec.m_groupBy     = &m_groupBy;
	spec.m_aggregates  = &m_aggregates;
	spec.m_having      = &m_having;
	spec.m_writeValues = &m_writeValues;
	spec.m_dotWalks    = &m_dotWalks;
	spec.m_selectCols  = &m_selectCols;
	return spec;
}

// The door runs the query through the COMPOSER (the tree executor) — never a concrete
// provider. For a single source the composer delegates to that source's provider;
// multi-source (Join / Union) it orchestrates. The door is blind to which.
ibDataQueryResult ibDataQueryBuilder::Select(const ibReadPageRequest& req) const
{
	return ibQueryComposer::ExecuteRead(BuildSpec(), req);
}

ibDataQueryResult ibDataQueryBuilder::Select(const ibReadPageRequest& req,
                                             ibRenderedPageCache& cache,
                                             const wxString& signature) const
{
	return ibQueryComposer::ExecuteReadCached(BuildSpec(), req, cache, signature);
}

ibDataQueryResult ibDataQueryBuilder::SelectAggregate() const
{
	return ibQueryComposer::ExecuteAggregate(BuildSpec());
}

ibQueryRamTable ibDataQueryBuilder::SelectTotals() const
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
