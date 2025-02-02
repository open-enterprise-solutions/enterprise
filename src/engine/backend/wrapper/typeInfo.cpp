#include "backend/metaCollection/metaObject.h"

#include "typeInfo.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

////////////////////////////////////////////////////////////////////////////

bool ITypeWrapper::LoadTypeData(CMemoryReader& dataReader)
{
	ClearAllMetatype();
	unsigned int count = dataReader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		const class_identifier_t& clsid = dataReader.r_u64();
		m_typeDescription.m_clsids.insert(clsid);
	}
	dataReader.r(&m_typeDescription.m_typeData, sizeof(typeDescription_t::typeData_t));
	return true;
}

bool ITypeWrapper::SaveTypeData(CMemoryWriter& dataWritter) const
{
	dataWritter.w_u32(m_typeDescription.m_clsids.size());
	for (auto clsid : m_typeDescription.m_clsids)
		dataWritter.w_u64(clsid);
	dataWritter.w(&m_typeDescription.m_typeData, sizeof(typeDescription_t::typeData_t));
	return true;
}

////////////////////////////////////////////////////////////////////////////

bool ITypeWrapper::LoadFromVariant(const wxVariant& variant)
{
	wxVariantAttributeData* attrData =
		dynamic_cast<wxVariantAttributeData*>(variant.GetData());
	if (attrData == nullptr)
		return false;
	SetDefaultMetatype(attrData->GetTypeDescription());
	return true;
}

void ITypeWrapper::SaveToVariant(wxVariant& variant, IMetaData* metaData) const
{
	wxVariantAttributeData* attrData = new wxVariantAttributeData(metaData);
	wxASSERT(attrData);
	attrData->SetTypeDescription(m_typeDescription);
	variant = attrData;
}

#include "backend/compiler/enum.h"

CValue ITypeAttribute::CreateValue() const
{
	CValue* refData = CreateValueRef();
	return refData ?
		refData : CValue();
}

CValue* ITypeAttribute::CreateValueRef() const
{
	if (ITypeAttribute::GetClsidCount() == 1) {
		IMetaData* metaData = GetMetaData();
		wxASSERT(metaData);
		const class_identifier_t& clsid = GetFirstClsid();
		if (metaData->IsRegisterCtor(clsid)) {
			IAbstractTypeCtor* so = metaData->GetAvailableCtor(clsid);
			if (so->GetObjectTypeCtor() == eCtorObjectType::eCtorObjectType_object_enum) {
				try {
					std::shared_ptr<IEnumerationWrapper> enumVal(
						metaData->CreateAndConvertObjectRef<IEnumerationWrapper>(so->GetClassName())
					);
					return enumVal->GetEnumVariantValue();
				}
				catch (...) {
				}
				return nullptr;
			}
			try {
				return metaData->CreateObjectRef(so->GetClassType());
			}
			catch (...) {
				return nullptr;
			}
		}
	}

	return nullptr;
}

CValue ITypeAttribute::AdjustValue() const
{
	return CValueTypeDescription::AdjustValue(GetTypeDescription());
}

CValue ITypeAttribute::AdjustValue(const CValue& varValue) const
{
	return CValueTypeDescription::AdjustValue(GetTypeDescription(), varValue);
}

////////////////////////////////////////////////////////////////////////////

wxString wxVariantAttributeData::MakeString() const
{
	wxString description;
	for (auto clsid : GetClsids()) {
		if (m_metaData->IsRegisterCtor(clsid) && description.IsEmpty()) {
			description = m_metaData->GetNameObjectFromID(clsid);
		}
		else if (m_metaData->IsRegisterCtor(clsid)) {
			description = description +
				", " + m_metaData->GetNameObjectFromID(clsid);
		}
	}
	return description;
}

void wxVariantAttributeData::DoSetFromMetaId(const meta_identifier_t& id)
{
	if (m_metaData != nullptr && id != wxNOT_FOUND) {
		IMetaObjectAttribute* metaAttribute =
			dynamic_cast<IMetaObjectAttribute*>(m_metaData->GetMetaObject(id));
		if (metaAttribute != nullptr && metaAttribute->IsAllowed()) {
			ITypeWrapper::SetDefaultMetatype(metaAttribute->GetTypeDescription());
			return;
		}
		CMetaObjectTable* metaTable =
			dynamic_cast<CMetaObjectTable*>(m_metaData->GetMetaObject(id));
		if (metaTable != nullptr && metaTable->IsAllowed()) {
			ITypeWrapper::SetDefaultMetatype(metaTable->GetTypeDescription());
			return;
		}

		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}
}