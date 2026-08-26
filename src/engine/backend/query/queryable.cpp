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
