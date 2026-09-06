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
					// ⚠⚠ THE VALUE BELONGS TO THE ENUMERATION, AND THE ENUMERATION DIES HERE.
					//
					// This read used to hold the enumeration in a std::shared_ptr — a second owner over
					// an object the runtime's own count already owns — and that is what stopped
					// compiling when ibValue became ibBackendRuntimeOwned. Refusing it uncovered a
					// live defect behind it: the variant is the enumeration's MEMBER
					// (ibValuePtr m_value), so `return enumVal->GetEnumVariantValue()` copies the
					// pointer, the enumeration is then destroyed, its member releases the variant to
					// zero — and the caller IncrRef's freed memory (ibValue::operator=(ibValue*)).
					//
					// So the value is given a reference of its OWN before its holder goes. It comes
					// back at refcount 1 and the caller's assignment takes it to 2.
					//
					// 🛑 THAT LEAVES ONE REFERENCE UNRELEASED, and it is deliberate and TEMPORARY:
					// a leaked count on a rarely-taken path is not a use-after-free, and the honest
					// fix is a different question — this enumeration should be reached from the
					// registry that already keeps one, not built and thrown away per read. Named here
					// rather than hidden, because the balanced-looking version was the broken one.
					ibValuePtr<ibValueEnumerationWrapper> enumVal(
						ibValue::CreateAndConvertObjectRef<ibValueEnumerationWrapper>(so->GetClassName())
					);
					ibValue* const variant = enumVal ? enumVal->GetEnumVariantValue() : nullptr;
					if (variant != nullptr)
						variant->IncrRef();
					return variant;
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

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const
{
	return ibValueTypeDescription::AdjustValue(limit, varValue);
}

/////////////////////////////////////////////////////////////////////////////////////

ibValue ibBackendTypeConfigFactory::CreateValue() const
{
	ibValue* refData = CreateValueRef();
	if (refData == nullptr)
		return ibValue();
	return refData;
}

#include "backend/metaData.h"
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

ibValue ibBackendTypeConfigFactory::AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const
{
	return ibValueTypeDescription::AdjustValue(
		limit,
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
	const std::vector<ibSourceHop>& path = desc.GetPath();
	if (path.empty()) return nullptr;

	// Gate 1: path[0] must be one of THIS context's source attributes (form-local).
	ibBackendFormAttributeValue* headHolder = FindSourceHolder(desc.GetFirst());
	if (headHolder == nullptr) return nullptr;
	if (outText != nullptr) *outText = headHolder->GetName();
	// A whole-attribute binding (length 1) is valid with no column leaf.
	if (path.size() == 1) { if (valid != nullptr) *valid = true; return nullptr; }

	// Deeper hops delegate to THE shared structure-resolve hop — it walks each source's EXPLORER (the same
	// self-describing structure the runtime value-hop steps), descending into each reference's OWN columns.
	// No metaID -> name -> FindAnyObjectByFilter fallback: the reference-as-source explorer already holds the
	// target's columns, so a miss is a genuinely broken binding. ONE resolve path, the design-time twin of
	// ResolvePath (which the tablebox renderer + GetValueByPath fetch values through).
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