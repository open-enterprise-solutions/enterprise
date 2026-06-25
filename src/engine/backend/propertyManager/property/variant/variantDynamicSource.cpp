#include "variantDynamicSource.h"

#include "backend/appData.h"                      // appData -> GetQueryableFactory
#include "backend/query/queryableFactory.h"       // ResolveById (live re-resolve)
#include "backend/query/queryable.h"              // GetQueryTableId / GetQueryName

ibVariantDataDynamicSource::ibVariantDataDynamicSource(const ibBackendQueryable* queryable)
	: wxVariantData(), m_tableId(queryable != nullptr ? (ibMetaID)queryable->GetQueryTableId() : wxNOT_FOUND)
{
}

const ibBackendQueryable* ibVariantDataDynamicSource::GetQueryable() const
{
	// Re-resolve LIVE every time — never a cached pointer. The factory hands back the descriptor's
	// own `&m_queryable` (stable per id while the source lives), or null if the source was deleted.
	if (m_tableId == wxNOT_FOUND || appData == nullptr || appData->GetQueryableFactory() == nullptr)
		return nullptr;
	return appData->GetQueryableFactory()->ResolveById(m_tableId);
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
