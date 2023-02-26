#include "core/metadata/metaObjects/metaObject.h"

#include "typeInfo.h"
#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

////////////////////////////////////////////////////////////////////////////

wxString wxVariantAttributeData::MakeString() const
{
	wxString description;
	for (auto clsid : GetClsids()) {
		if (m_metaData->IsRegisterObject(clsid) && description.IsEmpty()) {
			description = m_metaData->GetNameObjectFromID(clsid);
		}
		else if (m_metaData->IsRegisterObject(clsid)) {
			description = description +
				", " + m_metaData->GetNameObjectFromID(clsid);
		}
	}
	return description;
}

void wxVariantAttributeData::DoSetFromMetaId(const meta_identifier_t& id)
{
	if (m_metaData != NULL && id != wxNOT_FOUND) {
		IMetaAttributeObject* metaAttribute =
			dynamic_cast<IMetaAttributeObject*>(m_metaData->GetMetaObject(id));
		if (metaAttribute != NULL && metaAttribute->IsAllowed()) {
			ITypeWrapper::SetDefaultMetatype(metaAttribute->GetTypeDescription());
			return;
		}
		CMetaTableObject* metaTable =
			dynamic_cast<CMetaTableObject*>(m_metaData->GetMetaObject(id));
		if (metaTable != NULL && metaTable->IsAllowed()) {
			ITypeWrapper::SetDefaultMetatype(metaTable->GetTypeDescription());
			return;
		}

		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}
}

////////////////////////////////////////////////////////////////////////////

bool ITypeWrapper::LoadTypeData(CMemoryReader& dataReader)
{
	ClearAllMetatype();
	unsigned int count = dataReader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		const CLASS_ID& clsid = dataReader.r_u64();
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
	if (attrData == NULL)
		return false;
	SetDefaultMetatype(attrData->GetTypeDescription());
	return true;
}

void ITypeWrapper::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantAttributeData* attrData = new wxVariantAttributeData(metaData);
	wxASSERT(attrData);
	attrData->SetTypeDescription(m_typeDescription);
	variant = attrData;
}

#include "core/compiler/enum.h"

CValue ITypeAttribute::CreateValue() const
{
	CValue* refData = CreateValueRef();
	return refData ?
		refData : CValue();
}

CValue* ITypeAttribute::CreateValueRef() const
{
	if (ITypeAttribute::GetClsidCount() == 1) {
		IMetadata* metaData = GetMetadata();
		wxASSERT(metaData);
		const CLASS_ID& clsid = GetFirstClsid();
		if (metaData->IsRegisterObject(clsid)) {
			IObjectValueAbstract* so = metaData->GetAvailableObject(clsid);
			if (so->GetObjectType() == eObjectType::eObjectType_enum) {
				IEnumerationWrapper* retValue =
					metaData->CreateAndConvertObjectRef<IEnumerationWrapper>(so->GetClassName());
				wxASSERT(retValue);
				if (retValue != NULL && retValue->Init()) {
					CValue* enumValue =
						retValue->GetEnumVariantValue();
					wxASSERT(enumValue);
					delete retValue;
					return enumValue;
				}
				return retValue;
			}
			return metaData->CreateObjectRef(so->GetTypeClass());
		}
	}

	return NULL;
}

#include "frontend/windows/selectDataWnd.h"

CLASS_ID ITypeAttribute::ShowSelectType(IMetadata* metaData, const typeDescription_t& typeDescription)
{
	if (typeDescription.GetClsidCount() < 2)
		return typeDescription.GetFirstClsid();

	CSelectDataTypeWnd* selectDataType =
		new CSelectDataTypeWnd(metaData, typeDescription.GetClsids());
	CLASS_ID clsid = 0;
	if (selectDataType != NULL &&
		selectDataType->ShowModal(clsid))
		return clsid;
	return 0;
}

CValue ITypeAttribute::AdjustValue() const
{
	return CValueTypeDescription::AdjustValue(GetTypeDescription());
}

CValue ITypeAttribute::AdjustValue(const CValue& varValue) const
{
	return CValueTypeDescription::AdjustValue(GetTypeDescription(), varValue);
}

CLASS_ID ITypeAttribute::GetDataType() const
{
	return ShowSelectType(GetMetadata(), 
		ITypeAttribute::GetTypeDescription()
	);
}