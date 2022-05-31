#include "attributeControl.h"
#include "metadata/metaObjects/objects/baseObject.h"
#include "metadata/singleMetaTypes.h"
#include "metadata/metadata.h"
#include "form.h"

void wxVariantSourceAttributeData::UpdateSourceAttribute()
{
	if (m_metaData != NULL) {
		std::set<CLASS_ID> clsids;
		for (auto clsid : m_clsids) {
			if (!m_metaData->IsRegisterObject(clsid)) {
				clsids.insert(clsid);
			}
		}
		for (auto clsid : clsids) {
			m_clsids.erase(clsid);
		}
	}
}

void wxVariantSourceAttributeData::DoSetFromMetaId(const meta_identifier_t& id)
{
	if (m_metaData != NULL && id != wxNOT_FOUND) {
		ISourceDataObject* srcData = m_formData->GetSourceObject();
		if (srcData != NULL) {
			IMetaObjectWrapperData* metaObject = srcData->GetMetaObject();
			if (metaObject != NULL && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				IAttributeWrapper::SetDefaultMetatype(srcData->GetClassType());
				return;
			}
		}
	}

	wxVariantAttributeData::DoSetFromMetaId(id);
}

wxString wxVariantSourceData::MakeString() const
{
	if (m_srcId == wxNOT_FOUND) {
		return _("<not selected>");
	}

	if (m_metaData != NULL) {
		IMetaObject* metaObject = m_metaData->GetMetaObject(m_srcId);
		if (metaObject != NULL &&
			!metaObject->IsAllowed()) {
			return _("<not selected>");
		}
		return metaObject->GetName();
	}

	return _("<not selected>");
}

////////////////////////////////////////////////////////////////////////////

bool IAttributeControl::LoadData(CMemoryReader& dataReader)
{
	m_dataSource = dataReader.r_s32(); m_clsids.clear();
	unsigned int count = dataReader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		CLASS_ID clsid = dataReader.r_u64();
		m_clsids.insert(clsid);
	}

	dataReader.r(&m_metaDescription, sizeof(metaDescription_t));
	return true;
}

bool IAttributeControl::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_s32(m_dataSource);
	dataWritter.w_u32(m_clsids.size());
	for (auto clsid : m_clsids) {
		dataWritter.w_u64(clsid);
	}

	dataWritter.w(&m_metaDescription, sizeof(metaDescription_t));
	return true;
}

////////////////////////////////////////////////////////////////////////////

bool IAttributeControl::LoadFromVariant(const wxVariant& variant)
{
	wxVariantSourceData* srcData =
		dynamic_cast<wxVariantSourceData*>(variant.GetData());

	if (srcData == NULL)
		return false;

	wxVariantAttributeData* attrData = srcData->GetAttributeData();

	if (attrData == NULL)
		return false;

	m_dataSource = srcData->GetSourceId();

	SetDefaultMetatype(
		attrData->GetClsids(), attrData->GetDescription()
	);

	return true;
}

void IAttributeControl::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantSourceData* srcData = new wxVariantSourceData(metaData, GetOwnerForm(), m_dataSource);
	wxVariantAttributeData* attrData = srcData->GetAttributeData();
	metaDescription_t& metaDescription = attrData->GetDescription();

	metaDescription = m_metaDescription;

	if (m_dataSource == wxNOT_FOUND) {
		for (auto ñlsid : m_clsids) {
			attrData->AppendRecord(ñlsid);
		}
	}

	variant = srcData;
}

////////////////////////////////////////////////////////////////////////////

IMetaObject* IAttributeControl::GetMetaSource() const
{
	if (m_dataSource != wxNOT_FOUND && GetSourceObject()) {
		ISourceDataObject* srcObject = GetSourceObject();
		if (srcObject) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			return objMetaValue->FindMetaObjectByID(m_dataSource);
		}
	}

	return NULL;
}

IMetaObject* IAttributeControl::GetMetaObjectById(const CLASS_ID& clsid) const
{
	if (clsid == 0)
		return NULL;

	IMetadata* metaData = GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* singleValue = metaData->GetTypeObject(clsid);
	if (singleValue != NULL) {
		return singleValue->GetMetaObject();
	}
	return NULL;
}

void IAttributeControl::ResetSource()
{
	if (m_dataSource != wxNOT_FOUND) {
		wxASSERT(m_clsids.size() > 0);
		m_dataSource = wxNOT_FOUND;
	}
}

IMetaObjectWrapperData* IAttributeControl::GetMetaObject() const
{
	ISourceDataObject* sourceObject = GetSourceObject();
	return sourceObject ?
		sourceObject->GetMetaObject() : NULL;
}

CValue IAttributeControl::CreateValue() const
{
	return IAttributeControl::CreateValueRef();
}

CValue* IAttributeControl::CreateValueRef() const
{
	if (m_dataSource != wxNOT_FOUND && GetSourceObject()) {
		IMetaObjectWrapperData* metaObjectValue =
			IAttributeControl::GetMetaObject();
		IMetaAttributeObject* metaObject = NULL;
		if (metaObjectValue != NULL) {
			metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
			);
		}
		if (metaObject != NULL) {
			return metaObject->CreateValueRef();
		}
	}

	return IAttributeInfo::CreateValueRef();
}

CLASS_ID IAttributeControl::GetDataType() const
{
	if (m_dataSource != wxNOT_FOUND && GetSourceObject()) {
		IMetaObjectWrapperData* metaObjectValue =
			IAttributeControl::GetMetaObject();
		IMetaAttributeObject* metaObject = NULL;
		if (metaObjectValue != NULL) {
			metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
			);
		}
		if (metaObject != NULL) {
			return metaObject->GetDataType();
		}
	}

	return IAttributeInfo::GetDataType();
}