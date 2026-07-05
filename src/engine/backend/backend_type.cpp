#include "backend_type.h"
#include "backend/compiler/enumUnit.h"
#include "backend/system/value/valueTable.h"   // g_valueTableCLSID (the _table default); the primitive value clsids come via value.h

//***********************************************************************
//*                         Type factory                                *
//***********************************************************************


ibValue ibBackendTypeFactory::CreateValue() const
{
	ibValue* refData = CreateValueRef();
	return refData ?
		refData : ibValue();
}

ibValue* ibBackendTypeFactory::CreateValueRef() const
{
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (typeDesc.GetClsidCount() == 1) {
		const ibClassID& clsid = typeDesc.GetFirstClsid();
		if (ibValue::IsRegisterCtor(clsid)) {
			const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
			if (so->GetObjectTypeCtor() == ibCtorObjectType::ibCtorObjectType_object_enum) {
				try {
					std::shared_ptr<ibValueEnumerationWrapper> enumVal(
						ibValue::CreateAndConvertObjectRef<ibValueEnumerationWrapper>(so->GetClassName())
					);
					return enumVal->GetEnumVariantValue();
				}
				catch (...) {
				}
				return nullptr;
			}
			try {
				return ibValue::CreateObjectRef(so->GetClassType());
			}
			catch (...) {
				return nullptr;
			}
		}
	}
	return nullptr;
}

#include "backend/system/value/valueType.h"

ibValue ibBackendTypeFactory::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(GetTypeDesc());
}

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(GetTypeDesc(), varValue);
}

/////////////////////////////////////////////////////////////////////////////////////

ibValue ibBackendTypeConfigFactory::CreateValue() const
{
	ibValue* refData = CreateValueRef();
	if (refData == nullptr)
		return ibValue();
	return refData;
}

#include "backend/metadata.h"
#include "backend/objCtor.h"

ibValue* ibBackendTypeConfigFactory::CreateValueRef() const
{
	ibMetaData const* metaData = GetMetaData();
	wxASSERT(metaData);
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (typeDesc.GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(typeDesc.GetFirstClsid());
		if (so != nullptr) {
			try {
				return metaData->CreateObjectRef(so->GetClassType());
			}
			catch (...) {
				return nullptr;
			}
		}
	}
	return ibBackendTypeFactory::CreateValueRef();
}

ibValue ibBackendTypeConfigFactory::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(
		GetTypeDesc(),
		GetMetaData()
	);
}

ibValue ibBackendTypeConfigFactory::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(
		GetTypeDesc(),
		varValue,
		GetMetaData()
	);
}

// The one filter-kind -> default value clsid mapping. Static so both ibVariantDataAttribute::DoSetDefault-
// MetaType and ibValueControl::AutoBindNewSource resolve the SAME default type for a given filter kind.
ibClassID ibBackendTypeConfigFactory::GetDefaultTypeByFilter(ibSelectorDataType filterDataType)
{
	switch (filterDataType) {
	case ibSelectorDataType::ibSelectorDataType_boolean:  return g_valueBooleanCLSID;
	case ibSelectorDataType::ibSelectorDataType_resource: return g_valueNumberCLSID;
	case ibSelectorDataType::ibSelectorDataType_table:    return g_valueTableCLSID;
	case ibSelectorDataType::ibSelectorDataType_reference:
	default:                                              return g_valueStringCLSID;
	}
}

/////////////////////////////////////////////////////////////////////////////////////

#include "backend/query/queryColumn.h"                      // ibBackendSourceColumn — the leaf the dot returns

const ibBackendSourceColumn* ibBackendTypeSourceFactory::WalkSource(
	const ibSourceDescription& desc, bool* valid, wxString* outText) const
{
	if (valid != nullptr) *valid = false;
	const std::vector<ibSourceId>& path = desc.GetPath();
	if (path.empty()) return nullptr;

	// Gate 1: path[0] must be one of THIS context's source attributes (form-local).
	ibBackendFormAttributeValue* headHolder = FindSourceHolder(path[0]);
	if (headHolder == nullptr) return nullptr;
	if (outText != nullptr) *outText = headHolder->GetName();
	// A whole-attribute binding (length 1) is valid with no column leaf.
	if (path.size() == 1) { if (valid != nullptr) *valid = true; return nullptr; }

	// Deeper hops delegate to THE shared structure-resolve hop — it walks each source's EXPLORER (the same
	// self-describing structure the runtime value-hop steps), descending into each reference's OWN columns.
	// No metaID -> name -> FindAnyObjectByFilter fallback: the reference-as-source explorer already holds the
	// target's columns, so a miss is a genuinely broken binding. ONE resolve path, the design-time twin of
	// ContinueHops (which the tablebox renderer + GetValueByPath fetch values through).
	ibSourceDataObject* source = headHolder->GetSourceValue();
	const ibBackendSourceColumn* leaf = nullptr;
	const bool resolved = (source != nullptr) && source->WalkColumns(path, 1, leaf, outText);
	if (valid != nullptr) *valid = resolved;
	return resolved ? leaf : nullptr;
}

ibBackendFormAttributeValue* ibBackendTypeSourceFactory::FindSourceHolder(const ibMetaID& id) const
{
	std::vector<ibBackendFormAttributeValue*> holders;
	GetSourceList(holders);
	for (ibBackendFormAttributeValue* holder : holders)
		if (holder != nullptr && id == holder->GetId())
			return holder;
	return nullptr;
}