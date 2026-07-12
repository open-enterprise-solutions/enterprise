#include "variantDynamicSource.h"

#include "backend/appData.h"                      // appData -> GetQueryableFactory (global fallback)
#include "backend/metaData.h"                     // ibMetaData::GetSourceFactory — resolve THROUGH the owner's config
#include "backend/query/queryableFactory.h"       // ResolveById (live re-resolve)
#include "backend/query/queryable.h"              // GetQueryTableId / GetQueryName
#include "backend/propertyManager/propertyObject.h"   // ibPropertyObject::GetMetaData

ibVariantDataDynamicSource::ibVariantDataDynamicSource(const ibBackendQueryable* queryable, const ibPropertyObject* owner)
	: wxVariantData(), m_tableId(queryable != nullptr ? (ibMetaID)queryable->GetQueryTableId() : wxNOT_FOUND), m_owner(owner)
{
}

// The factory this cell re-resolves through: the OWNER config's own factory (the metadata it runs on behalf of), which
// descends to the global factory INTERNALLY on a miss — so a copied / other-config list resolves ITS source, never
// whatever registered globally last. Entry is ALWAYS the metadata; a null owner-config resolves nothing (no appData /
// active-metadata grab). (CONST owner: the non-const GetMetaData wxFAILs.)
static ibQueryableFactory* ibVariantSourceFactory(const ibPropertyObject* owner)
{
	const ibMetaData* md = owner != nullptr ? owner->GetMetaData() : nullptr;
	ibQueryableFactory* factory = md != nullptr ? md->GetSourceFactory() : nullptr;
	// No owner config → the global base factory (common/plugin; empty today) — a harmless last resort.
	return factory != nullptr ? factory : ibApplicationData::GetQueryableFactory();
}

const ibBackendQueryable* ibVariantDataDynamicSource::GetQueryable() const
{
	// Re-resolve LIVE every time — never a cached pointer. Goes THROUGH the owner's metadata factory (per-config →
	// global), which hands back the descriptor's own `&m_queryable`, or null if the source was deleted.
	if (m_tableId == wxNOT_FOUND)
		return nullptr;
	ibQueryableFactory* factory = ibVariantSourceFactory(m_owner);
	return factory != nullptr ? factory->ResolveById(m_tableId) : nullptr;
}

const ibQueryableSourceDescriptor* ibVariantDataDynamicSource::GetDescriptor() const
{
	// Re-resolve LIVE, same id as GetQueryable, THROUGH the owner's metadata factory — the descriptor (its command +
	// column surface, FillSourceExplorer) is the copy's OWN, so the columns are the copy's. Null when deleted.
	if (m_tableId == wxNOT_FOUND)
		return nullptr;
	ibQueryableFactory* factory = ibVariantSourceFactory(m_owner);
	return factory != nullptr ? factory->ResolveDescriptorById(m_tableId) : nullptr;
}

void ibVariantDataDynamicSource::SetQueryable(const ibBackendQueryable* queryable)
{
	m_tableId = queryable != nullptr ? (ibMetaID)queryable->GetQueryTableId() : wxNOT_FOUND;
}

wxString ibVariantDataDynamicSource::MakeString() const
{
	const ibBackendQueryable* q = GetQueryable();
	return q != nullptr ? q->GetQueryName() : wxString();
}
