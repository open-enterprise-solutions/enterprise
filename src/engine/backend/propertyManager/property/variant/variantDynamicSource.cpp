#include "variantDynamicSource.h"

#include "backend/appData.h"                      // appData -> GetQueryableFactory (global fallback)
#include "backend/metaData.h"                     // ibMetaData::GetSourceFactory — resolve THROUGH the owner's application
#include "backend/query/queryableFactory.h"       // ResolveDescriptorById (live re-resolve)
#include "backend/query/queryable.h"              // GetQueryTableId / GetQueryName
#include "backend/propertyManager/propertyObject.h"   // ibPropertyObject::GetMetaData

ibVariantDynamicSource::ibVariantDynamicSource(const std::shared_ptr<const ibBackendQueryable>& queryable, const ibPropertyObject* owner)
	: ibVariant(ibValueTypes::TYPE_VALUE), m_tableId(queryable != nullptr ? (ibMetaID)queryable->GetQueryTableId() : ibNOT_FOUND),
	// Resolve the owner's SPECIFIC application ONCE, here, and keep the metadata — NOT the owner pointer (which, for a
	// form-attribute's transient dynamic list, dangles once the list is re-materialised). (CONST owner: the non-const
	// GetMetaData wxFAILs.)
	  m_metaData(owner != nullptr ? owner->GetMetaData() : nullptr)
{
}

// The factory this cell re-resolves through: the SPECIFIC application's own factory (where the queryable lives), which
// descends to the global factory INTERNALLY on a miss — so a copied / other-application list resolves ITS source, never
// whatever registered globally last. Null metadata → the global base factory (common/plugin; empty today).
static ibQueryableFactory* ibVariantSourceFactory(const ibMetaData* md)
{
	ibQueryableFactory* factory = md != nullptr ? md->GetSourceFactory() : nullptr;
	return factory != nullptr ? factory : ibApplicationInstance::GetQueryableFactory();
}

std::shared_ptr<const ibBackendQueryable> ibVariantDynamicSource::GetQueryable() const
{
	// Re-resolve LIVE every time — never a cached pointer. The descriptor by the id, THROUGH the owner's metadata
	// factory (per-application → global), asked for its source as declared; null if the source was deleted.
	if (m_tableId == ibNOT_FOUND)
		return nullptr;
	ibQueryableFactory* factory = ibVariantSourceFactory(m_metaData);
	ibQueryableSourceDescriptor* const descriptor = factory != nullptr ? factory->ResolveDescriptorById(m_tableId) : nullptr;
	return descriptor != nullptr ? descriptor->CreateQueryable(nullptr, 0) : nullptr;
}

const ibQueryableSourceDescriptor* ibVariantDynamicSource::GetDescriptor() const
{
	// Re-resolve LIVE, same id as GetQueryable, THROUGH the owner's metadata factory — the descriptor (its command +
	// column surface, FillSourceExplorer) is the copy's OWN, so the columns are the copy's. Null when deleted.
	if (m_tableId == ibNOT_FOUND)
		return nullptr;
	ibQueryableFactory* factory = ibVariantSourceFactory(m_metaData);
	return factory != nullptr ? factory->ResolveDescriptorById(m_tableId) : nullptr;
}

void ibVariantDynamicSource::SetQueryable(const std::shared_ptr<const ibBackendQueryable>& queryable)
{
	m_tableId = queryable != nullptr ? (ibMetaID)queryable->GetQueryTableId() : ibNOT_FOUND;
}

ibString ibVariantDynamicSource::GetString() const
{
	const std::shared_ptr<const ibBackendQueryable> q = GetQueryable();
	return q != nullptr ? q->GetQueryName() : ibString();
}
