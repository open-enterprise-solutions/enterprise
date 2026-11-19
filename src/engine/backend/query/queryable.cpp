////////////////////////////////////////////////////////////////////////////
//	Description : ibBackendQueryable — THE SOURCE ITSELF, and everything its
//	              base answers on behalf of every source under it.
////////////////////////////////////////////////////////////////////////////

// ⭐⭐ THE QUERYABLE'S OWN FILE, beside its header — the arrangement queryColumn.{h,cpp} already has
// for a COLUMN (Max, 2026-08-26: *"make a separate file and put the queryable in it"*).
//
// These bodies used to live in the composer's file (then queryProvider.cpp, now provider/queryComposer.cpp),
// which is 4000 lines of something else: the RAM composer, the computed provider, the result sources. A base class answering "which metaobject
// stands behind me" was reachable only by knowing that a file named after PROVIDERS happened to
// carry it — and every reader who went looking learned the provider's internals on the way.
//
// What belongs here is what the BASE answers: the default engine it vends, the default column
// lookup, and the identity of a source with no metaobject. A concrete source overrides them in its
// own file; nothing here knows about any of them.

#include "backend/query/queryable.h"
#include "backend/query/dataQueryBuilder.h"   // ibDataQueryBuilder::GetPrimarySource — a nested query's key is its source's

#include <algorithm>                                       // std::none_of — the sort order's cell, re-keyed once
#include <atomic>                                          // the alias-column id counter below

// ==========================================================================
// ibBackendQueryable::GetProvider — the DB DEFAULT. A queryable vends its engine; the
// record / register / constant / tabular families are physical DB tables, so they all
// share one STATELESS static DB provider. Computed queryables override this
// (ibComputedRegisterQueryable in queryable.h) to vend a static ibComputedProvider. (docs §22.4)
// ==========================================================================
ibBackendQueryProvider& ibBackendQueryable::GetProvider() const
{
	return ibDbTableProviderInstance();   // stateless — the spec carries every per-query value
}

// A column the source LISTS, by name — then its sort order. Listing and resolving were two answers to one
// question, and a source that gave one without the other was half a source — the constant resolved `Value`
// and listed nothing.
std::shared_ptr<const ibBackendQueryColumn> ibBackendQueryable::ResolveColumnByName(const ibString& name) const
{
	for (std::shared_ptr<const ibBackendQueryColumn>& column : GetQueryColumns())
		if (column != nullptr && column->GetName().IsSameAs(name, false))
			return std::move(column);
	std::shared_ptr<const ibBackendQueryColumn> sort = GetSortColumn();
	return sort != nullptr && sort->GetName().IsSameAs(name, false) ? sort : nullptr;
}

// A SOURCE WITH NO METAOBJECT HAS NO GUID AND NO METAID — the base's answer. A metaobject-backed source
// answers both itself, beside its GetSourceMetaObject and off the same m_meta (the record, register and
// constant families; the tabular part, the sequence borders and the recalculation always did).
//
// ⚠ They were read off GetSourceMetaObject HERE, once, for every family (2026-08 — the same two lines
// in each had invited the drift a duplicate does), and that put the metaobject's header into the
// source's own file. A metaobject is the provider's, never the face's (2026-11-17, Max): each family
// answers in the class that holds the metaobject, where the three answers cannot drift apart.
//
// GetQueryTableName stays virtual on purpose: the PHYSICAL table is a different fact, and a temp
// source has one without a metaobject anywhere.
const ibUniqueKey& ibBackendQueryable::GetQueryTableGuid() const
{
	static const ibUniqueKey none;
	return none;
}

ibMetaID ibBackendQueryable::GetQueryTableId() const
{
	return 0;
}

// ==========================================================================
// ibAliasQueryable — the same table, read a second time (see queryable.h for why).
// ==========================================================================

// ⭐⭐ THE TWINS' IDs ARE NEGATIVE, like every id nothing declared.
//
// A metaID is an application number — positive and small. An id minted for a column nobody declared
// says so BY ITS SIGN (ibBackendQueryColumn::IsSyntheticId), which is what replaced five hand-carved
// bands in the positive space: bands have to be read before every addition, and nothing makes anyone
// read them — I walked into an occupied one adding a sixth (2026-09-06).
//
ibAliasQueryable::ibAliasQueryable(const std::shared_ptr<const ibBackendQueryable>& origin, const ibString& sqlAlias)
	: m_origin(origin), m_sqlAlias(sqlAlias)
{
	for (const std::shared_ptr<const ibBackendQueryColumn>& c : origin->GetQueryColumns()) {
		if (c == nullptr)
			continue;
		m_published.push_back(std::make_shared<ibAliasColumn>(c));
	}
	// …and the sort order, which the origin answers to without listing it (see the header).
	if (const std::shared_ptr<const ibBackendQueryColumn> sort = origin->GetSortColumn())
		m_sort = std::make_shared<ibAliasColumn>(sort);
}

// The key / hierarchy columns are the ORIGIN's answers, translated to OUR twins: they are read
// through this reading of the table, so handing back the origin's objects would route them to the
// other side of the join — the very confusion this class exists to end. A column the origin names
// but does not publish (it is not in GetQueryColumns) is dropped rather than passed through untranslated.
std::vector<std::shared_ptr<const ibBackendQueryColumn>> ibAliasQueryable::GetPrimaryKeyColumns() const
{
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys;
	for (const std::shared_ptr<const ibBackendQueryColumn>& k : m_origin->GetPrimaryKeyColumns())
		for (const std::shared_ptr<ibAliasColumn>& c : m_published)
			if (c->Origin() == k) { keys.push_back(c); break; }
	return keys;
}

// ⭐ THE NESTED QUERY'S KEY — the inner source's, republished as the columns THIS wrapper exposes.
//
// 🛑 IT WAS NOT ANSWERED AT ALL, and the silence reached every report. A COMPOSITION always renders
// its source as a nested query (queryLowering.cpp, the note over `q_sub<n>`), so a report reads
// through this wrapper; the base's empty answer left `DimCtx::identity` at 0, and a level keyed by
// the row's own identity then stopped BEING the row — a catalog grouped by Reference printed its
// headings with Code and Description blank, which is the exact defect `AttachDimValue`'s rule exists
// to prevent. Measured 2026-09-09: source `q_sub0` keys=0 against `Goods` keys=1 on the road that
// does not wrap, same query, same settings.
//
// ⚠ ALL OR NOTHING. A key half-projected is not a key: if any of the inner source's key columns is
// not published here, this wrapper genuinely does not carry the identity, and saying so with an
// empty answer beats handing back a partial key that reads exactly like a whole one.
std::vector<std::shared_ptr<const ibBackendQueryColumn>> ibSubqueryQueryable::GetPrimaryKeyColumns() const
{
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys;
	if (!m_inner)
		return keys;
	const std::shared_ptr<const ibBackendQueryable> inner = m_inner->GetPrimarySource();
	if (inner == nullptr)
		return keys;
	for (const std::shared_ptr<const ibBackendQueryColumn>& k : inner->GetPrimaryKeyColumns()) {
		std::shared_ptr<const ibBackendQueryColumn> published;
		// The exposed column IS the inner one where nothing was renamed; where the inner query gave
		// an alias, `m_readFrom` is what names the real column behind it (they run in parallel).
		for (std::size_t i = 0; i < m_columns.size(); ++i) {
			if (m_columns[i] == k || (i < m_readFrom.size() && m_readFrom[i] == k)) { published = m_columns[i]; break; }
		}
		if (published == nullptr)
			return {};   // the key is not carried — say nothing
		keys.push_back(std::move(published));
	}
	return keys;
}

std::shared_ptr<const ibBackendQueryColumn> ibAliasQueryable::GetHierarchyColumn() const
{
	const std::shared_ptr<const ibBackendQueryColumn> h = m_origin->GetHierarchyColumn();
	if (h == nullptr)
		return nullptr;
	for (const std::shared_ptr<ibAliasColumn>& c : m_published)
		if (c->Origin() == h) return c;
	return nullptr;
}

// ⭐ THE ORIGIN'S ROWS, UNDER THIS READING'S IDS — computed by the origin, keyed as this side of the join carries them,
// as a table read through an alias is (MaterialiseLeafToRam: the read the origin's, the key the twin's). The conditions
// come on our twins and go to the origin as its own columns. The rows came back under the origin's ids, and every
// reader keying them by this reading's columns found none: two temporary tables in one statement - the second is a
// twin, the tables number their columns alike - could not be copied into a base in memory, and a semi-join over a
// twin took no keys (2026-11-19).
ibQueryRamTable ibAliasQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	std::vector<ibQueryCondition> asked = extra;
	for (ibQueryCondition& c : asked) {
		if (const std::shared_ptr<const ibBackendQueryColumn> origin = OriginOf(c.m_col))
			c.m_col = origin;
		for (std::shared_ptr<const ibBackendQueryColumn>& step : c.m_path)
			if (const std::shared_ptr<const ibBackendQueryColumn> origin = OriginOf(step))
				step = origin;
	}
	ibQueryRamTable computed = m_origin->ComputeRows(asked);

	ibQueryRamTable rows;
	std::vector<std::pair<ibMetaID, ibMetaID>> rekey;
	for (const ibQueryRamColumn& held : computed.Columns())
		for (const std::shared_ptr<ibAliasColumn>& c : m_published)
			if (c->Origin()->GetColumnId() == held.m_id) {
				rows.AddColumn(c->GetColumnId(), held.m_name, held.m_type, held.m_caption, held.m_available);
				rekey.emplace_back(held.m_id, c->GetColumnId());
				break;
			}
	// …and the sort order's cell, where the origin's rows hold it under an id no published column takes (a cell is moved
	// once: AppendRowsRekeyed).
	if (m_sort != nullptr && std::none_of(rekey.begin(), rekey.end(),
			[this](const std::pair<ibMetaID, ibMetaID>& k) { return k.first == m_sort->Origin()->GetColumnId(); }))
		rekey.emplace_back(m_sort->Origin()->GetColumnId(), m_sort->GetColumnId());
	rows.AppendRowsRekeyed(std::move(computed), rekey);
	return rows;
}
