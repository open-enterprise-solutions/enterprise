#include "metadata/metaObjects/metaObject.h"

#include "attributeInfo.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

////////////////////////////////////////////////////////////////////////////

wxString wxVariantAttributeData::MakeString() const
{
	wxString description;
	for (auto clsid : m_clsids) {
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
			IAttributeWrapper::SetDefaultMetatype(
				metaAttribute->GetClsids(), metaAttribute->GetDescription()
			);
			return;
		}
		CMetaTableObject* metaTable =
			dynamic_cast<CMetaTableObject*>(m_metaData->GetMetaObject(id));
		if (metaTable != NULL && metaTable->IsAllowed()) {
			IAttributeWrapper::SetDefaultMetatype(
				metaTable->GetTableClsid()
			);
			return;
		}

		IAttributeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}
}

////////////////////////////////////////////////////////////////////////////

bool IAttributeWrapper::LoadData(CMemoryReader& dataReader)
{
	m_clsids.clear(); unsigned int count = dataReader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		CLASS_ID clsid = dataReader.r_u64();
		m_clsids.insert(clsid);
	}
	dataReader.r(&m_metaDescription, sizeof(metaDescription_t));
	return true;
}

bool IAttributeWrapper::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_u32(m_clsids.size());
	for (auto clsid : m_clsids) {
		dataWritter.w_u64(clsid);
	}
	dataWritter.w(&m_metaDescription, sizeof(metaDescription_t));
	return true;
}

////////////////////////////////////////////////////////////////////////////

bool IAttributeWrapper::LoadFromVariant(const wxVariant& variant)
{
	wxVariantAttributeData* attrData =
		dynamic_cast<wxVariantAttributeData*>(variant.GetData());

	if (attrData == NULL)
		return false;

	m_metaDescription = attrData->m_metaDescription; m_clsids.clear();

	for (unsigned int idx = 0; idx < attrData->GetTypeCount(); idx++) {
		m_clsids.insert(attrData->GetById(idx));
	}

	return true;
}

void IAttributeWrapper::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantAttributeData* list = new wxVariantAttributeData(metaData);
	list->m_metaDescription = m_metaDescription;
	for (auto clsid : m_clsids) {
		list->AppendRecord(clsid);
	}
	variant = list;
}
////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierNumber, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierDate, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierString, CValue);

CValue IAttributeInfo::CreateValue() const
{
	CValue* refData = CreateValueRef();
	return refData ?
		refData : CValue();
}

#include "compiler/enum.h"

CValue* IAttributeInfo::CreateValueRef() const
{
	auto clsids = GetClsids();

	if (clsids.size() == 1) {
		auto foundedIt = clsids.begin();
		std::advance(foundedIt, 0);
		IMetadata* metaData = GetMetadata();
		wxASSERT(metaData);
		if (metaData->IsRegisterObject(*foundedIt)) {
			IObjectValueAbstract* so = metaData->GetAvailableObject(*foundedIt);
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
			return metaData->CreateObjectRef(so->GetClassName());
		}
	}

	return NULL;
}

CValue IAttributeInfo::AdjustValue() const
{
	CValueTypeDescription* vt = GetValueTypeDescription();
	wxASSERT(vt);
	CValue retVal =
		vt->AdjustValue();
	delete vt;
	return retVal;
}

CValue IAttributeInfo::AdjustValue(const CValue& cVal) const
{
	CValueTypeDescription* vt = GetValueTypeDescription();
	wxASSERT(vt);
	CValue retVal =
		vt->AdjustValue(cVal);
	delete vt;
	return retVal;
}

#include "frontend/windows/selectDataWnd.h"

CLASS_ID IAttributeInfo::GetDataType() const
{
	auto clsids = GetClsids();

	if (clsids.size() < 2) {
		auto foundedIt = clsids.begin();
		std::advance(foundedIt, 0);
		return *foundedIt;
	}

	CSelectDataTypeWnd* selectDataType =
		new CSelectDataTypeWnd(GetMetadata(), GetClsids());

	if (selectDataType != NULL) {
		CLASS_ID clsid = 0;
		if (selectDataType->ShowModal(clsid)) {
			return clsid;
		}
	}

	return 0;
}

CValueTypeDescription* IAttributeInfo::GetValueTypeDescription() const
{
	auto clsids = GetClsids();
	return new CValueTypeDescription(clsids,
		GetNumberQualifier(),
		GetDateQualifier(),
		GetStringQualifier()
	);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueQualifierNumber, "qualifierNumber", TEXT2CLSID("VL_QLFN"));
VALUE_REGISTER(CValueQualifierDate, "qualifierDate", TEXT2CLSID("VL_QLFD"));
VALUE_REGISTER(CValueQualifierString, "qualifierString", TEXT2CLSID("VL_QLFS"));