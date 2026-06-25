#include "propertyDynamicSource.h"

#include "backend/appData.h"
#include "backend/query/queryableFactory.h"
#include "backend/query/queryable.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/compiler/value.h"

wxObject* (*ibPropertyDynamicSource::ms_propertyDynamicSource)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&) = nullptr;

void ibPropertyDynamicSource::SetSource(const wxString& ns, const wxString& name)
{
	if (appData != nullptr && appData->GetQueryableFactory() != nullptr)
		SetVariable(appData->GetQueryableFactory()->Resolve(ns, name));
}

// Value = the source's table id (a stable variable id, NOT text).
bool ibPropertyDynamicSource::SetDataValue(const ibValue& varPropVal)
{
	if (appData == nullptr || appData->GetQueryableFactory() == nullptr)
		return false;
	SetVariable(appData->GetQueryableFactory()->ResolveById((ibMetaID)varPropVal.GetInteger()));
	return GetVariable() != nullptr;
}

bool ibPropertyDynamicSource::GetDataValue(ibValue& pvarPropVal) const
{
	const ibBackendQueryable* q = GetVariable();
	pvarPropVal = ibValue((wxLongLong_t)(q != nullptr ? q->GetQueryTableId() : 0));
	return true;
}

// Serialize the chosen source as its table id; resolve it back on read.
bool ibPropertyDynamicSource::ReadNodeValue(const ibDataValue& value)
{
	if (appData == nullptr || appData->GetQueryableFactory() == nullptr)
		return false;
	SetVariable(appData->GetQueryableFactory()->ResolveById((ibMetaID)value.AsInt()));
	return true;
}

bool ibPropertyDynamicSource::WriteNodeValue(ibDataValue& value) const
{
	const ibBackendQueryable* q = GetVariable();
	if (q == nullptr)
		return true;   // no source picked → the write FAILS (forbids serialising an incomplete list)
	value = ibDataValue::Int((int)q->GetQueryTableId());
	return true;
}
