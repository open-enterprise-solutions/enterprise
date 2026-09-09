////////////////////////////////////////////////////////////////////////////
//	Description : ibBackendQueryable — THE SOURCE ITSELF, and everything its
//	              base answers on behalf of every source under it.
////////////////////////////////////////////////////////////////////////////

// ⭐⭐ THE QUERYABLE'S OWN FILE, beside its header — the arrangement queryColumn.{h,cpp} already has
// for a COLUMN (Max, 2026-08-26: *"make a separate file and put the queryable in it"*).
//
// These bodies used to live in queryProvider.cpp, which is 4000 lines of something else: the RAM
// composer, the computed provider, the result sources. A base class answering "which metaobject
// stands behind me" was reachable only by knowing that a file named after PROVIDERS happened to
// carry it — and every reader who went looking learned the provider's internals on the way.
//
// What belongs here is what the BASE answers: the default engine it vends, the default column
// lookup, and the two projections of its metaobject. A concrete source overrides them in its own
// file; nothing here knows about any of them.

#include "backend/query/queryable.h"
#include "backend/query/dbTableProvider.h"                 // ibDbTableProvider — the DB default this base vends
#include "backend/metaCollection/genericData.h"            // ibValueMetaObjectGenericData — the metaobject asked below

#include <atomic>                                          // the alias-column id counter below

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

// Default: the base has no metadata to resolve a name against, so it owns no columns.
// Every concrete source (record / register / constant / tabular = attribute-by-name;
// temp / subquery = own column lookup) OVERRIDES this; the base is a null fallback.
const ibBackendQueryColumn* ibBackendQueryable::ResolveColumnByName(const wxString& /*name*/) const
{
	return nullptr;
}

// ⭐⭐ THE METAOBJECT IS ASKED ONCE, AND ITS GUID AND ID ARE READ OFF IT.
//
// These two used to be pure virtuals of their own, and every metaobject-backed source implemented
// both with the same two lines — `m_meta->GetGuid()`, `m_meta->GetMetaID()`. Three questions, one
// fact: "which metaobject stands behind this source". A source with none (temp / subquery /
// computed) answered a hand-written empty, which is precisely what falling through to no metaobject
// says by itself.
//
// The cost of the duplicate is not the typing. A projection published beside the thing it projects
// invites a consumer to reach for whichever is nearest, and the two drift the moment one is
// overridden and the other forgotten — the same shape that let a SORT stand in for a KEY until an
// enumeration reordered itself and every reader of that key read a number.
//
// GetQueryTableName stays virtual on purpose: the PHYSICAL table is a different fact, and a temp
// source has one without a metaobject anywhere.
ibGuid ibBackendQueryable::GetQueryTableGuid() const
{
	const ibValueMetaObjectGenericData* const meta = GetSourceMetaObject();
	return meta != nullptr ? meta->GetGuid() : wxNullGuid;
}

ibMetaID ibBackendQueryable::GetQueryTableId() const
{
	const ibValueMetaObjectGenericData* const meta = GetSourceMetaObject();
	return meta != nullptr ? meta->GetMetaID() : 0;
}

// ==========================================================================
// ibAliasQueryable — the same table, read a second time (see queryable.h for why).
// ==========================================================================

// ⭐⭐ THE TWINS' IDs ARE NEGATIVE, like every id nothing declared.
//
// A metaID is a configuration number — positive and small. An id minted for a column nobody declared
// says so BY ITS SIGN (ibBackendQueryColumn::IsSyntheticId), which is what replaced five hand-carved
// bands in the positive space: bands have to be read before every addition, and nothing makes anyone
// read them — I walked into an occupied one adding a sixth (2026-09-06).
//
ibAliasQueryable::ibAliasQueryable(const ibBackendQueryable* origin, const wxString& sqlAlias)
	: m_origin(origin), m_sqlAlias(sqlAlias)
{
	for (const ibBackendQueryColumn* c : origin->GetColumns()) {
		if (c == nullptr)
			continue;
		m_owned.push_back(std::make_shared<ibAliasColumn>(c));
		m_published.push_back(m_owned.back().get());
	}
}

// Twinned on demand — see the header for why the published face is not enough. Matched by the ORIGIN
// object rather than by the name a second time: two names could reach one column (a synonym, a
// case difference), and minting a second twin for it would put the same field in the result twice
// under two identities.
const ibBackendQueryColumn* ibAliasQueryable::ResolveColumnByName(const wxString& name) const
{
	for (const std::shared_ptr<ibAliasColumn>& c : m_owned)
		if (c->GetName().IsSameAs(name, false)) return c.get();

	const ibBackendQueryColumn* const origin = m_origin->ResolveColumnByName(name);
	if (origin == nullptr)
		return nullptr;
	for (const std::shared_ptr<ibAliasColumn>& c : m_owned)
		if (c->Origin() == origin) return c.get();

	m_owned.push_back(std::make_shared<ibAliasColumn>(origin));
	return m_owned.back().get();
}

// The key / hierarchy columns are the ORIGIN's answers, translated to OUR twins: they are read
// through this reading of the table, so handing back the origin's objects would route them to the
// other side of the join — the very confusion this class exists to end. A column the origin names
// but does not publish (it is not in GetColumns) is dropped rather than passed through untranslated.
std::vector<const ibBackendQueryColumn*> ibAliasQueryable::GetPrimaryKeyColumns() const
{
	std::vector<const ibBackendQueryColumn*> keys;
	for (const ibBackendQueryColumn* k : m_origin->GetPrimaryKeyColumns())
		for (const std::shared_ptr<ibAliasColumn>& c : m_owned)
			if (c->Origin() == k) { keys.push_back(c.get()); break; }
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
std::vector<const ibBackendQueryColumn*> ibSubqueryQueryable::GetPrimaryKeyColumns() const
{
	std::vector<const ibBackendQueryColumn*> keys;
	if (!m_inner)
		return keys;
	const ibBackendQueryable* const inner = m_inner->GetPrimarySource();
	if (inner == nullptr)
		return keys;
	for (const ibBackendQueryColumn* k : inner->GetPrimaryKeyColumns()) {
		const ibBackendQueryColumn* published = nullptr;
		// The exposed column IS the inner one where nothing was renamed; where the inner query gave
		// an alias, `m_readFrom` is what names the real column behind it (they run in parallel).
		for (std::size_t i = 0; i < m_columns.size(); ++i) {
			const ibBackendQueryColumn* const from = (i < m_readFrom.size()) ? m_readFrom[i] : nullptr;
			if (m_columns[i] == k || from == k) { published = m_columns[i]; break; }
		}
		if (published == nullptr)
			return std::vector<const ibBackendQueryColumn*>();   // the key is not carried — say nothing
		keys.push_back(published);
	}
	return keys;
}

const ibBackendQueryColumn* ibAliasQueryable::GetHierarchyColumn() const
{
	const ibBackendQueryColumn* h = m_origin->GetHierarchyColumn();
	if (h == nullptr)
		return nullptr;
	for (const std::shared_ptr<ibAliasColumn>& c : m_owned)
		if (c->Origin() == h) return c.get();
	return nullptr;
}
