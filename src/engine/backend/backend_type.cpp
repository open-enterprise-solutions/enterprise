#include "backend_type.h"
#include "backend/compiler/enumUnit.h"

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

/////////////////////////////////////////////////////////////////////////////////////

bool ibBackendTypeSourceFactory::FilterSource(const ibSourceExplorer& src, const ibMetaID& id) const
{
	return !src.IsTableSection();
}

#include "backend/metaCollection/metaObject.h"             // ibValueMetaObject — deep-hop field resolve
#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject — the live source on the holder
#include "backend/query/queryColumn.h"                      // ibBackendSourceColumn — the leaf the dot returns

const ibBackendSourceColumn* ibBackendTypeSourceFactory::WalkSource(
	const std::vector<ibSourceId>& path, bool* valid, wxString* outText) const
{
	if (valid != nullptr) *valid = false;
	if (path.empty()) return nullptr;

	// Gate 1: path[0] must be one of THIS context's source attributes (form-local).
	ibBackendFormAttributeValue* headHolder = FindSourceHolder(path[0]);
	if (headHolder == nullptr) return nullptr;
	if (outText != nullptr) *outText = headHolder->GetAttributeName();
	if (valid != nullptr) *valid = true;   // a whole-attribute binding (length 1) is valid

	// The head attribute's LIVE source (now on the attribute side) resolves a hop to one of its
	// OWN columns, metadata-blind (a dynamic list → a queryable column). A CLOSED source
	// (HasOwnColumns) makes a MISS a BROKEN binding — the column was dropped by a Type/source
	// change; an OPEN metaobject source falls through to the config-wide reference dot-walk (a
	// dotted path's deeper hop lives in ANOTHER type, not this source's own columns).
	ibSourceDataObject* source = headHolder->GetSourceValue();
	const ibMetaData* metaData = GetMetaData();

	const ibBackendSourceColumn* leaf = nullptr;
	for (size_t i = 1; i < path.size(); ++i) {
		const ibBackendSourceColumn* col = source != nullptr ? source->GetSourceColumn(path[i]) : nullptr;
		if (col != nullptr) {
			if (outText != nullptr) { *outText += wxT("."); *outText += col->GetName(); }   // NAME, not synonym — match the head / field hops
			leaf = col;
			source = nullptr;   // descended past this source's own columns → reference territory
			continue;
		}
		// The live source did not resolve it: a CLOSED source → broken; else config-wide.
		if (source != nullptr && source->HasOwnColumns()) { if (valid != nullptr) *valid = false; return nullptr; }
		const ibValueMetaObject* field = metaData != nullptr ? metaData->FindAnyObjectByFilter(path[i], true) : nullptr;
		if (field == nullptr || !field->IsAllowed()) { if (valid != nullptr) *valid = false; return nullptr; }
		if (outText != nullptr) { *outText += wxT("."); *outText += field->GetName(); }
		leaf = dynamic_cast<const ibBackendSourceColumn*>(field);
	}
	return leaf;
}

ibBackendFormAttributeValue* ibBackendTypeSourceFactory::FindSourceHolder(const ibMetaID& id) const
{
	std::vector<ibBackendFormAttributeValue*> holders;
	GetSourceList(holders);
	for (ibBackendFormAttributeValue* holder : holders)
		if (holder != nullptr && holder->GetAttributeId() == id)
			return holder;
	return nullptr;
}