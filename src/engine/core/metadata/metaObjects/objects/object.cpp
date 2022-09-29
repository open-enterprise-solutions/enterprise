////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "object.h"
#include "metadata/metadata.h"
#include "common/srcExplorer.h"
#include "compiler/systemObjects.h"
#include "metadata/singleMetaTypes.h"
#include "utils/stringUtils.h"

//***********************************************************************
//*								 metadata                               * 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectWrapperData, IMetaObject);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRegisterData, IMetaObjectWrapperData);

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordData, IMetaObjectWrapperData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataExt, IMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataRef, IMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataMutableRef, IMetaObjectRecordDataRef);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataGroupMutableRef, IMetaObjectRecordDataMutableRef);

/////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CMetaGroupAttributeObject, CMetaAttributeObject);
wxIMPLEMENT_DYNAMIC_CLASS(CMetaGroupTableObject, CMetaTableObject);

//***********************************************************************
//*							IMetaObjectWrapperData				        *
//***********************************************************************

IMetaTypeObjectValueSingle* IMetaObjectWrapperData::GetTypeObject(eMetaObjectType refType) const
{
	return m_metaData->GetTypeObject(this, refType);
}

meta_identifier_t IMetaObjectWrapperData::GetIdByGuid(const Guid& guid) const
{
	IMetaObject* metaObject = FindMetaObjectByID(guid);
	if (metaObject != NULL)
		return metaObject->GetMetaID();
	return wxNOT_FOUND;
}

Guid IMetaObjectWrapperData::GetGuidByID(const meta_identifier_t& id) const
{
	IMetaObject* metaObject = FindMetaObjectByID(id);
	if (metaObject != NULL)
		return metaObject->GetGuid();
	return wxNullGuid;
}

//////////////////////////////////////////////////////////////////////////////

IMetaObject* IMetaObjectWrapperData::FindMetaObjectByID(const meta_identifier_t& id) const
{
	return m_metaData->GetMetaObject(id,
		const_cast<IMetaObjectWrapperData*>(this));
}

IMetaObject* IMetaObjectWrapperData::FindMetaObjectByID(const Guid& guid) const
{
	return m_metaData->GetMetaObject(guid,
		const_cast<IMetaObjectWrapperData*>(this));
}

IMetaAttributeObject* IMetaObjectWrapperData::FindGenericAttribute(const meta_identifier_t& id) const
{
	for (auto metaObject : GetGenericAttributes()) {
		if (metaObject->GetMetaID() == id)
			return metaObject;
	}

	return NULL;
}

CValueForm* IMetaObjectWrapperData::GetGenericForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	if (!formName.IsEmpty()) {
		for (auto metaForm : GetGenericForms()) {
			if (StringUtils::CompareString(formName, metaForm->GetName())) {
				return metaForm->GenerateFormAndRun(ownerControl,
					NULL, formGuid
				);
			}
		}
	}

	CSystemObjects::Raise(_("Сommon form not found '") + formName + "'");
	return NULL;
}

CValueForm* IMetaObjectWrapperData::GetGenericForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetGenericForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	if (defList == NULL) {
		return NULL;
	}

	return GetGenericForm(defList->GetName(),
		ownerControl, formGuid
	);
}

//***********************************************************************
//*                           IMetaObjectRecordData					    *
//***********************************************************************
#include "compiler/methods.h"
#include "utils/fs/fs.h"

bool IMetaObjectRecordData::OnLoadMetaObject(IMetadata* metaData)
{
	return IMetaObject::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordData::OnSaveMetaObject()
{
	return IMetaObject::OnSaveMetaObject();
}

bool IMetaObjectRecordData::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

bool IMetaObjectRecordData::OnBeforeRunMetaObject(int flags)
{
	if (m_metaClsid != g_metaEnumerationCLSID) {
		registerObject();
	}

	registerManager();
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRecordData::OnAfterCloseMetaObject()
{
	if (m_metaClsid != g_metaEnumerationCLSID) {
		unregisterObject();
	}

	unregisterManager();
	return IMetaObject::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CValueForm* IMetaObjectRecordData::GetObjectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetObjectForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//***********************************************************************
//*								 ARRAY									*
//***********************************************************************

std::vector<IMetaAttributeObject*> IMetaObjectRecordData::GetGenericAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	for (auto attribute : GetObjectAttributes()) {
		attributes.push_back(attribute);
	}

	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRecordData::GetGenericForms() const
{
	return GetObjectForms();
}

std::vector<IMetaAttributeObject*> IMetaObjectRecordData::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;
	for (auto metaObject : m_metaObjects) {
		CMetaAttributeObject* metaAttribute = NULL;
		if (metaObject->ConvertToValue(metaAttribute)) {
			attributes.push_back(metaAttribute);
		}
	}
	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRecordData::GetObjectForms() const
{
	std::vector<CMetaFormObject*> forms;
	for (auto metaObject : m_metaObjects) {
		CMetaFormObject* metaForm = NULL;
		if (metaObject->ConvertToValue(metaForm)) {
			forms.push_back(metaForm);
		}
	}

	return forms;
}

std::vector<CMetaGridObject*> IMetaObjectRecordData::GetObjectTemplates() const
{
	std::vector<CMetaGridObject*> templates;
	for (auto metaObject : m_metaObjects) {
		CMetaGridObject* metaTemplate = NULL;
		if (metaObject->ConvertToValue(metaTemplate)) {
			templates.push_back(metaTemplate);
		}
	}
	return templates;
}

std::vector<CMetaTableObject*> IMetaObjectRecordData::GetObjectTables() const
{
	std::vector<CMetaTableObject*> tables;
	for (auto metaObject : m_metaObjects) {
		CMetaTableObject* metaTable = NULL;
		if (metaObject->ConvertToValue(metaTable)) {
			tables.push_back(metaTable);
		}
	}
	return tables;
}

//***********************************************************************
//*						IMetaObjectRecordDataExt					    *
//***********************************************************************

IMetaObjectRecordDataExt::IMetaObjectRecordDataExt(int objMode) :
	IMetaObjectRecordData(), m_objMode(objMode)
{
}

IRecordDataObjectExt* IMetaObjectRecordDataExt::CreateObjectValue()
{
	IRecordDataObjectExt* createdValue = CreateObjectExtValue();
	if (m_objMode == METAOBJECT_NORMAL) {
		if (createdValue && !createdValue->InitializeObject()) {
			wxDELETE(createdValue);
			return NULL;
		}
	}
	return createdValue;
}

IRecordDataObjectExt* IMetaObjectRecordDataExt::CreateObjectValue(IRecordDataObjectExt* objSrc)
{
	IRecordDataObjectExt* createdValue = CreateObjectExtValue();
	if (m_objMode == METAOBJECT_NORMAL) {
		if (createdValue && !createdValue->InitializeObject(objSrc)) {
			wxDELETE(createdValue);
			return NULL;
		}
	}
	return createdValue;
}

IRecordDataObject* IMetaObjectRecordDataExt::CreateRecordDataObject()
{
	return CreateObjectValue();
}

//***********************************************************************
//*						IMetaObjectRecordDataRef					    *
//***********************************************************************

IMetaObjectRecordDataRef::IMetaObjectRecordDataRef() : IMetaObjectRecordData()
{
	m_attributeReference = CMetaDefaultAttributeObject::CreateSpecialType("reference", _("Reference"), wxEmptyString, CValue::GetIDByVT(eValueTypes::TYPE_EMPTY));
	m_attributeReference->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeReference->SetParent(this);
	AddChild(m_attributeReference);
}

IMetaObjectRecordDataRef::~IMetaObjectRecordDataRef()
{
	wxDELETE(m_attributeReference);
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool IMetaObjectRecordDataRef::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeReference->LoadMeta(dataReader);

	return IMetaObjectRecordData::LoadData(dataReader);
}

bool IMetaObjectRecordDataRef::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeReference->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectRecordData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool IMetaObjectRecordDataRef::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordData::OnCreateMetaObject(metaData))
		return false;

	return m_attributeReference->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataRef::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeReference->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordData::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordDataRef::OnSaveMetaObject()
{
	if (!m_attributeReference->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordData::OnSaveMetaObject();
}

bool IMetaObjectRecordDataRef::OnDeleteMetaObject()
{
	if (!m_attributeReference->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordData::OnDeleteMetaObject();
}

bool IMetaObjectRecordDataRef::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeReference->OnBeforeRunMetaObject(flags))
		return false;

	registerReference();
	registerRefList();

	if (!IMetaObjectRecordData::OnBeforeRunMetaObject(flags))
		return false;

	IMetaTypeObjectValueSingle* singleObject = m_metaData->GetTypeObject(this, eMetaObjectType::enReference);

	if (singleObject != NULL) {
		m_attributeReference->SetDefaultMetatype(singleObject->GetClassType());
	}

	return true;
}

bool IMetaObjectRecordDataRef::OnAfterRunMetaObject(int flags)
{
	return IMetaObjectRecordData::OnAfterRunMetaObject(flags);
}

bool IMetaObjectRecordDataRef::OnBeforeCloseMetaObject()
{
	return IMetaObjectRecordData::OnBeforeCloseMetaObject();
}

bool IMetaObjectRecordDataRef::OnAfterCloseMetaObject()
{
	if (!m_attributeReference->OnAfterCloseMetaObject())
		return false;

	if (m_attributeReference != NULL) {
		m_attributeReference->SetDefaultMetatype(CValue::GetIDByVT(eValueTypes::TYPE_EMPTY));
	}

	unregisterReference();
	unregisteRefList();

	return IMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

#include "frontend/visualView/controls/form.h" 

//process choice 
bool IMetaObjectRecordDataRef::ProcessChoice(IValueFrame* ownerValue, const meta_identifier_t& id)
{
	CValueForm* selectForm = IMetaObjectRecordDataRef::GetSelectForm(id, ownerValue);
	if (selectForm == NULL)
		return false;
	selectForm->ShowForm();
	return true;
}

bool IMetaObjectRecordDataRef::ProcessListChoice(IValueFrame* ownerValue, const meta_identifier_t& id)
{
	return true;
}

CReferenceDataObject* IMetaObjectRecordDataRef::FindObjectValue(const Guid& guid)
{
	return CReferenceDataObject::Create(this, guid);
}

CValueForm* IMetaObjectRecordDataRef::GetListForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetListForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

CValueForm* IMetaObjectRecordDataRef::GetSelectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetSelectForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//***********************************************************************
//*                            ARRAY									*
//***********************************************************************

std::vector<IMetaAttributeObject*> IMetaObjectRecordDataRef::GetGenericAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	for (auto attribute : GetDefaultAttributes()) {
		attributes.push_back(attribute);
	}

	for (auto attribute : GetObjectAttributes()) {
		attributes.push_back(attribute);
	}

	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRecordDataRef::GetGenericForms() const
{
	return GetObjectForms();
}

std::vector<CMetaEnumerationObject*> IMetaObjectRecordDataRef::GetObjectEnums() const
{
	std::vector<CMetaEnumerationObject*> enumerations;
	for (auto metaObject : m_metaObjects) {
		CMetaEnumerationObject* enum_ = NULL;
		if (metaObject->ConvertToValue(enum_)) {
			enumerations.push_back(enum_);
		}
	}
	return enumerations;
}

IRecordDataObjectRef* IMetaObjectRecordDataRef::CreateObjectValue()
{
	IRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}

	return createdValue;
}

IRecordDataObjectRef* IMetaObjectRecordDataRef::CreateObjectValue(const Guid& guid)
{
	IRecordDataObjectRef* createdValue = CreateObjectRefValue(guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}

	return createdValue;
}

IRecordDataObject* IMetaObjectRecordDataRef::CreateRecordDataObject()
{
	return CreateObjectValue();
}

//***********************************************************************
//*						IMetaObjectRecordDataMutableRef					*
//***********************************************************************

bool IMetaObjectRecordDataMutableRef::genData_t::LoadData(CMemoryReader& dataReader)
{
	unsigned int count = dataReader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		meta_identifier_t record_id = dataReader.r_u32();
		m_data.insert(record_id);
	}
	return true;
}

bool IMetaObjectRecordDataMutableRef::genData_t::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_u32(m_data.size());
	for (auto record_id : m_data) {
		dataWritter.w_u32(record_id);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

#include "objectVariant.h"

bool IMetaObjectRecordDataMutableRef::genData_t::LoadFromVariant(const wxVariant& variant)
{
	wxVariantGenerationData* list =
		dynamic_cast<wxVariantGenerationData*>(variant.GetData());
	if (list == NULL)
		return false;
	m_data.clear();
	for (unsigned int idx = 0; idx < list->GetCount(); idx++) {
		m_data.insert(
			list->GetByIdx(idx)
		);
	}

	return true;
}

void IMetaObjectRecordDataMutableRef::genData_t::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantGenerationData* list = new wxVariantGenerationData(metaData);
	for (auto clsid : m_data) {
		list->SetMetatype(clsid);
	}
	variant = list;
}

///////////////////////////////////////////////////////////////////////////////////////////////

IMetaObjectRecordDataMutableRef::IMetaObjectRecordDataMutableRef() : IMetaObjectRecordDataRef()
{
	m_attributeDeletionMark = CMetaDefaultAttributeObject::CreateBoolean("deletionMark", _("DeletionMark"), wxEmptyString);
	m_attributeDeletionMark->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeDeletionMark->SetParent(this);
	AddChild(m_attributeDeletionMark);
}

IMetaObjectRecordDataMutableRef::~IMetaObjectRecordDataMutableRef()
{
	wxDELETE(m_attributeDeletionMark);
}

///////////////////////////////////////////////////////////////////////////////////////////////

IRecordDataObjectRef* IMetaObjectRecordDataGroupMutableRef::CreateGroupObjectValue()
{
	IRecordDataObjectRef* createdValue = CreateGroupObjectRefValue();
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}

	return createdValue;
}

IRecordDataObjectRef* IMetaObjectRecordDataGroupMutableRef::CreateGroupObjectValue(const Guid& guid)
{
	IRecordDataObjectRef* createdValue = CreateGroupObjectRefValue(guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}

	return createdValue;
}

IRecordDataObjectRef* IMetaObjectRecordDataGroupMutableRef::CreateGroupObjectValue(IRecordDataObjectRef* objSrc, bool generate)
{
	if (objSrc == NULL)
		return NULL;
	IRecordDataObjectRef* createdValue = CreateGroupObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

CValueForm* IMetaObjectRecordDataGroupMutableRef::GetGroupForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetGroupForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

CValueForm* IMetaObjectRecordDataGroupMutableRef::GetGroupSelectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetGroupSelectForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool IMetaObjectRecordDataMutableRef::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeDeletionMark->LoadMeta(dataReader);

	if (!IMetaObjectRecordDataRef::LoadData(dataReader))
		return false;

	return m_genData.LoadData(dataReader);
}

bool IMetaObjectRecordDataMutableRef::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeDeletionMark->SaveMeta(dataWritter);

	//create or update table:
	if (!IMetaObjectRecordDataRef::SaveData(dataWritter))
		return false;

	return m_genData.SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool IMetaObjectRecordDataMutableRef::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordDataRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeDeletionMark->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataMutableRef::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeDeletionMark->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataRef::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordDataMutableRef::OnSaveMetaObject()
{
	if (!m_attributeDeletionMark->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnSaveMetaObject();
}

bool IMetaObjectRecordDataMutableRef::OnDeleteMetaObject()
{
	if (!m_attributeDeletionMark->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnDeleteMetaObject();
}

bool IMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeDeletionMark->OnBeforeRunMetaObject(flags))
		return false;

	return IMetaObjectRecordDataRef::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(int flags)
{
	return IMetaObjectRecordDataRef::OnAfterRunMetaObject(flags);
}

bool IMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject()
{
	return IMetaObjectRecordDataRef::OnBeforeCloseMetaObject();
}

bool IMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject()
{
	if (!m_attributeDeletionMark->OnAfterCloseMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////

IRecordDataObjectRef* IMetaObjectRecordDataMutableRef::CreateObjectValue(IRecordDataObjectRef* objSrc, bool generate)
{
	if (objSrc == NULL)
		return NULL;
	IRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

//***********************************************************************
//*                      IMetaObjectRegisterData						*
//***********************************************************************

IMetaObjectRegisterData::IMetaObjectRegisterData() : IMetaObjectWrapperData()
{
	//create default attributes
	m_attributeLineActive = CMetaDefaultAttributeObject::CreateBoolean(wxT("active"), _("Active"), wxEmptyString, false, true);
	m_attributeLineActive->SetClsid(g_metaDefaultAttributeCLSID);
	m_attributePeriod = CMetaDefaultAttributeObject::CreateDate(wxT("period"), _("Period"), wxEmptyString, eDateFractions::eDateFractions_DateTime, true);
	m_attributePeriod->SetClsid(g_metaDefaultAttributeCLSID);
	m_attributeRecorder = CMetaDefaultAttributeObject::CreateEmptyType(wxT("recorder"), _("Recorder"), wxEmptyString);
	m_attributeRecorder->SetClsid(g_metaDefaultAttributeCLSID);
	m_attributeLineNumber = CMetaDefaultAttributeObject::CreateNumber(wxT("lineNumber"), _("Line number"), wxEmptyString, 15, 0);
	m_attributeLineNumber->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeLineActive->SetParent(this);
	AddChild(m_attributeLineActive);
	m_attributePeriod->SetParent(this);
	AddChild(m_attributePeriod);
	m_attributeRecorder->SetParent(this);
	AddChild(m_attributeRecorder);
	m_attributeLineNumber->SetParent(this);
	AddChild(m_attributeLineNumber);
}

IMetaObjectRegisterData::~IMetaObjectRegisterData()
{
	wxDELETE(m_attributeLineActive);
	wxDELETE(m_attributePeriod);
	wxDELETE(m_attributeRecorder);
	wxDELETE(m_attributeLineNumber);
}

//***************************************************************************
//*                       Save & load metadata                              *
//*************23121994qwe@M_	**************************************************************

bool IMetaObjectRegisterData::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeLineActive->LoadMeta(dataReader);
	m_attributePeriod->LoadMeta(dataReader);
	m_attributeRecorder->LoadMeta(dataReader);
	m_attributeLineNumber->LoadMeta(dataReader);

	return IMetaObjectWrapperData::LoadData(dataReader);
}

bool IMetaObjectRegisterData::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeLineActive->SaveMeta(dataWritter);
	m_attributePeriod->SaveMeta(dataWritter);
	m_attributeRecorder->SaveMeta(dataWritter);
	m_attributeLineNumber->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectWrapperData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool IMetaObjectRegisterData::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObject::OnCreateMetaObject(metaData))
		return false;

	return m_attributeLineActive->OnCreateMetaObject(metaData) &&
		m_attributePeriod->OnCreateMetaObject(metaData) &&
		m_attributeRecorder->OnCreateMetaObject(metaData) &&
		m_attributeLineNumber->OnCreateMetaObject(metaData);
}

bool IMetaObjectRegisterData::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeLineActive->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributePeriod->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeRecorder->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeLineNumber->OnLoadMetaObject(metaData))
		return false;

	return IMetaObject::OnLoadMetaObject(metaData);
}

bool IMetaObjectRegisterData::OnSaveMetaObject()
{
	if (!m_attributeLineActive->OnSaveMetaObject())
		return false;

	if (!m_attributePeriod->OnSaveMetaObject())
		return false;

	if (!m_attributeRecorder->OnSaveMetaObject())
		return false;

	if (!m_attributeLineNumber->OnSaveMetaObject())
		return false;

	return IMetaObject::OnSaveMetaObject();
}

bool IMetaObjectRegisterData::OnDeleteMetaObject()
{
	if (!m_attributeLineActive->OnDeleteMetaObject())
		return false;

	if (!m_attributePeriod->OnDeleteMetaObject())
		return false;

	if (!m_attributeRecorder->OnDeleteMetaObject())
		return false;

	if (!m_attributeLineNumber->OnDeleteMetaObject())
		return false;

	return IMetaObject::OnDeleteMetaObject();
}

#include "metadata/singleMetaTypes.h"

bool IMetaObjectRegisterData::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeLineActive->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributePeriod->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeRecorder->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeLineNumber->OnBeforeRunMetaObject(flags))
		return false;

	registerManager();
	registerRegList();
	registerRecordKey();
	registerRecordSet();

	if (HasRecordManager()) {
		registerRecordManager();
	}

	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRegisterData::OnAfterCloseMetaObject()
{
	if (!m_attributeLineActive->OnAfterCloseMetaObject())
		return false;

	if (!m_attributePeriod->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeRecorder->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeLineNumber->OnAfterCloseMetaObject())
		return false;

	unregisterManager();
	unregisterRegList();
	unregisterRecordKey();
	unregisterRecordSet();

	if (HasRecordManager()) {
		unregisterRecordManager();
	}

	return IMetaObject::OnAfterCloseMetaObject();
}

CValueForm* IMetaObjectRegisterData::GetListForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetListForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//***********************************************************************
//*								ARRAY									*
//***********************************************************************

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetGenericAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	for (auto attribute : GetDefaultAttributes()) {
		attributes.push_back(attribute);
	}

	for (auto dimension : GetObjectDimensions()) {
		attributes.push_back(dimension);
	}

	for (auto resource : GetObjectResources()) {
		attributes.push_back(resource);
	}

	for (auto attribute : GetObjectAttributes()) {
		attributes.push_back(attribute);
	}

	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRegisterData::GetGenericForms() const
{
	return GetObjectForms();
}

#include "metadata/metaObjects/dimension/metaDimensionObject.h"
#include "metadata/metaObjects/resource/metaResourceObject.h"
#include "metadata/metaObjects/attribute/metaAttributeObject.h"

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetObjectDimensions() const
{
	std::vector<IMetaAttributeObject*> dimensions;
	for (auto metaObject : m_metaObjects) {
		CMetaDimensionObject* dimension = NULL;
		if (metaObject->ConvertToValue(dimension)) {
			dimensions.push_back(dimension);
		}
	}
	return dimensions;
}

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetObjectResources() const
{
	std::vector<IMetaAttributeObject*> resources;
	for (auto metaObject : m_metaObjects) {
		CMetaResourceObject* resource = NULL;
		if (metaObject->ConvertToValue(resource)) {
			resources.push_back(resource);
		}
	}
	return resources;
}

std::vector<IMetaAttributeObject*> IMetaObjectRegisterData::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;
	for (auto metaObject : m_metaObjects) {
		CMetaAttributeObject* attribute = NULL;
		if (metaObject->ConvertToValue(attribute) &&
			attribute->GetClsid() == g_metaAttributeCLSID) {
			attributes.push_back(attribute);
		}
	}
	return attributes;
}

std::vector<CMetaFormObject*> IMetaObjectRegisterData::GetObjectForms() const
{
	std::vector<CMetaFormObject*> forms;
	for (auto metaObject : m_metaObjects) {
		CMetaFormObject* metaForm = NULL;
		if (metaObject->ConvertToValue(metaForm)) {
			forms.push_back(metaForm);
		}
	}
	return forms;
}

std::vector<CMetaGridObject*> IMetaObjectRegisterData::GetObjectTemplates() const
{
	std::vector<CMetaGridObject*> templates;
	for (auto metaObject : m_metaObjects) {
		CMetaGridObject* metaTemplate = NULL;
		if (metaObject->ConvertToValue(metaTemplate)) {
			templates.push_back(metaTemplate);
		}
	}
	return templates;
}

///////////////////////////////////////////////////////////////////////

IMetaAttributeObject* IMetaObjectRegisterData::FindAttribute(const meta_identifier_t& id) const
{
	for (auto metaObject : m_metaObjects) {
		if (
			metaObject->GetClsid() == g_metaDefaultAttributeCLSID ||
			metaObject->GetClsid() == g_metaAttributeCLSID ||
			metaObject->GetClsid() == g_metaDimensionCLSID ||
			metaObject->GetClsid() == g_metaResourceCLSID ||

			metaObject->GetMetaID() == id) {
			return dynamic_cast<IMetaAttributeObject*>(metaObject);
		}
	}

	return NULL;
}

IRecordSetObject* IMetaObjectRegisterData::CreateRecordSetObjectValue(bool needInitialize)
{
	IRecordSetObject* createdValue = CreateRecordSetObjectRegValue();
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(NULL, true)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordSetObject* IMetaObjectRegisterData::CreateRecordSetObjectValue(const CUniquePairKey& uniqueKey, bool needInitialize)
{
	IRecordSetObject* createdValue = CreateRecordSetObjectRegValue(uniqueKey);
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(NULL, false)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordSetObject* IMetaObjectRegisterData::CreateRecordSetObjectValue(IRecordSetObject* source, bool needInitialize)
{
	IRecordSetObject* createdValue = CreateRecordSetObjectRegValue();
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(source, true)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordManagerObject* IMetaObjectRegisterData::CreateRecordManagerObjectValue()
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(NULL, true)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordManagerObject* IMetaObjectRegisterData::CreateRecordManagerObjectValue(const CUniquePairKey& uniqueKey)
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue(uniqueKey);
	if (createdValue && !createdValue->InitializeObject(NULL, false)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordManagerObject* IMetaObjectRegisterData::CreateRecordManagerObjectValue(IRecordManagerObject* source)
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(source, true)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

//***********************************************************************
//*                        ISourceDataObject							*
//***********************************************************************

#include "frontend/visualView/controls/form.h"

//***********************************************************************
//*                        IRecordDataObject							*
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObject, CValue);

IRecordDataObject::IRecordDataObject(const Guid& objGuid, bool newObject) :
	CValue(eValueTypes::TYPE_VALUE), IObjectValueInfo(objGuid, newObject),
	m_methods(new CMethods()) {
}

IRecordDataObject::IRecordDataObject(const IRecordDataObject& source) :
	CValue(eValueTypes::TYPE_VALUE), IObjectValueInfo(wxNewGuid, true),
	m_methods(new CMethods()) {
}

IRecordDataObject::~IRecordDataObject() {
	wxDELETE(m_methods);
}

CValueForm* IRecordDataObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return NULL;
	return CValueForm::FindFormByGuid(m_objGuid);
}

CLASS_ID IRecordDataObject::GetClassType() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle* clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordDataObject::GetTypeString() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle* clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordDataObject::GetString() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle* clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

CSourceExplorer IRecordDataObject::GetSourceExplorer() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();

	CSourceExplorer srcHelper(
		metaObject, GetClassType(),
		false
	);

	for (auto attribute : metaObject->GetGenericAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	for (auto table : metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
	}

	return srcHelper;
}

#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"

bool IRecordDataObject::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](ITabularSectionDataObject* vts) {
		CMetaTableObject* metaObject = vts->GetMetaObject();
		wxASSERT(metaObject);
		return id == metaObject->GetMetaID();
		});

	if (foundedIt == m_aObjectTables.end())
		return false;

	tableValue = *foundedIt;
	return true;
}

#include "compiler/valueTypeDescription.h"

void IRecordDataObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		IMetaObjectRecordData* metaObjectValue = GetMetaObject();
		wxASSERT(metaObjectValue);
		IMetaAttributeObject* metaAttribute = wxDynamicCast(
			metaObjectValue->FindMetaObjectByID(id), IMetaAttributeObject
		);
		wxASSERT(metaAttribute);
		foundedIt->second = metaAttribute->AdjustValue(cVal);
	}
}

CValue IRecordDataObject::GetValueByMetaID(const meta_identifier_t& id) const
{
	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		return foundedIt->second;
	}

	return CValue();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void IRecordDataObject::PrepareEmptyObject()
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	m_aObjectValues.clear();
	//attrbutes can refValue 
	for (auto attribute : metaObject->GetGenericAttributes()) {
		m_aObjectValues[attribute->GetMetaID()] = attribute->CreateValue();
	}
	m_aObjectTables.clear();
	// table is collection values 
	for (auto table : metaObject->GetObjectTables()) {
		CTabularSectionDataObject* tableSection = new CTabularSectionDataObject(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}
}

//***********************************************************************
//*                        IRecordDataObjectExt							*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObjectExt, IRecordDataObject);

IRecordDataObjectExt::IRecordDataObjectExt(IMetaObjectRecordDataExt* metaObject) :
	IRecordDataObject(wxNewGuid, true), m_metaObject(metaObject)
{
}

IRecordDataObjectExt::IRecordDataObjectExt(const IRecordDataObjectExt& source) :
	IRecordDataObject(source), m_metaObject(source.m_metaObject)
{
}

IRecordDataObjectExt::~IRecordDataObjectExt()
{
	if (m_metaObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
		if (!appData->DesignerMode()) {
			delete m_metaObject->GetMetadata();
		}
	}
}

bool IRecordDataObjectExt::InitializeObject()
{
	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		IMetadata* metaData = m_metaObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		if (!m_compileModule) {
			m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
			m_compileModule->SetParent(moduleManager->GetCompileModule());
			m_compileModule->AddContextVariable(thisObject, this);
		}

		if (appData->EnterpriseMode()) {
			try {
				m_compileModule->Compile();
			}
			catch (const CTranslateError* err)
			{
				if (appData->EnterpriseMode()) {
					CSystemObjects::Raise(err->what());
				}

				return false;
			};

			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(moduleManager->GetProcUnit());
		}
	}

	PrepareEmptyObject();

	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		if (appData->EnterpriseMode()) {
			m_procUnit->Execute(m_compileModule->m_cByteCode, true);
		}
	}

	//is Ok
	return true;
}

bool IRecordDataObjectExt::InitializeObject(IRecordDataObjectExt* source)
{
	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		IMetadata* metaData = m_metaObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		if (!m_compileModule) {
			m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
			m_compileModule->SetParent(moduleManager->GetCompileModule());
			m_compileModule->AddContextVariable(thisObject, this);
		}

		if (appData->EnterpriseMode()) {
			try {
				m_compileModule->Compile();
			}
			catch (const CTranslateError* err)
			{
				if (appData->EnterpriseMode()) {
					CSystemObjects::Raise(err->what());
				}

				return false;
			};

			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(moduleManager->GetProcUnit());
		}
	}

	PrepareEmptyObject();

	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		if (appData->EnterpriseMode()) {
			m_procUnit->Execute(m_compileModule->m_cByteCode, true);
		}
	}

	//is Ok
	return true;
}

IRecordDataObjectExt* IRecordDataObjectExt::CopyObjectValue()
{
	return m_metaObject->CreateObjectValue(this);
}

//***********************************************************************
//*                        IRecordDataObjectRef							*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObjectRef, IRecordDataObject);

IRecordDataObjectRef::IRecordDataObjectRef(IMetaObjectRecordDataMutableRef* metaObject, const Guid& objGuid) :
	IRecordDataObject(objGuid.isValid() ? objGuid : wxNewGuid, !objGuid.isValid()),
	m_metaObject(metaObject),
	m_reference_impl(NULL), m_codeGenerator(NULL),
	m_objModified(false)
{
	if (m_metaObject != NULL) {
		IMetaAttributeObject* attributeCode = m_metaObject->GetAttributeForCode();
		if (attributeCode != NULL) {
			m_codeGenerator = new CCodeGenerator(
				m_metaObject, attributeCode
			);
		}
		m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
	}
}

IRecordDataObjectRef::IRecordDataObjectRef(const IRecordDataObjectRef& src) :
	IRecordDataObject(src),
	m_metaObject(src.m_metaObject),
	m_reference_impl(NULL), m_codeGenerator(NULL),
	m_objModified(false)
{
	if (m_metaObject != NULL) {
		IMetaAttributeObject* attributeCode = m_metaObject->GetAttributeForCode();
		if (attributeCode != NULL) {
			m_codeGenerator = new CCodeGenerator(
				m_metaObject, attributeCode
			);
		}
		m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
	}
}

IRecordDataObjectRef::~IRecordDataObjectRef()
{
	wxDELETE(m_codeGenerator);
	wxDELETE(m_reference_impl);
}

#include "appData.h"

bool IRecordDataObjectRef::InitializeObject()
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_compileModule == NULL) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}

	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const CTranslateError* err)
		{
			if (appData->EnterpriseMode()) {
				CSystemObjects::Raise(err->what());
			}

			return false;
		};
	}

	if (!appData->DesignerMode()) {
		if (!ReadData()) {
			PrepareEmptyObject();
		}
	}
	else {
		PrepareEmptyObject();
	}

	bool succes = true;

	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == NULL);
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
		if (m_newObject)
			succes = Filling();
	}

	//is Ok
	return succes;
}

bool IRecordDataObjectRef::InitializeObject(IRecordDataObjectRef* source, bool generate)
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_compileModule == NULL) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}

	if (appData->EnterpriseMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const CTranslateError* err) {
			if (appData->EnterpriseMode()) {
				CSystemObjects::Raise(err->what());
			}

			return false;
		};
	}

	CReferenceDataObject* reference = source ?
		source->GetReference() : NULL;

	if (reference != NULL)
		reference->IncrRef();

	if (!generate && source != NULL)
		PrepareEmptyObject(source);
	else
		PrepareEmptyObject();

	bool succes = true;

	if (appData->EnterpriseMode()) {
		wxASSERT(m_procUnit == NULL);
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
		if (m_newObject && source != NULL && !generate) {
			m_procUnit->CallFunction("OnCopy", CValue(source));
		}
		else if (m_newObject && source == NULL) {
			succes = Filling();
		}
		else if (generate) {
			succes = Filling(CValue(reference));
		}
	}

	if (reference != NULL)
		reference->DecrRef();

	//is Ok
	return succes;
}

CLASS_ID IRecordDataObjectRef::GetClassType() const
{
	return IRecordDataObject::GetClassType();
}

wxString IRecordDataObjectRef::GetTypeString() const
{
	return IRecordDataObject::GetTypeString();
}

wxString IRecordDataObjectRef::GetString() const
{
	return m_metaObject->GetDescription(this);
}

CSourceExplorer IRecordDataObjectRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	IMetaAttributeObject* metaAttribute = m_metaObject->GetAttributeForCode();

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
			srcHelper.AppendSource(attribute, attribute != metaAttribute);
		}
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		srcHelper.AppendSource(table);
	}

	return srcHelper;
}

bool IRecordDataObjectRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](ITabularSectionDataObject* vts) {
		CMetaTableObject* metaObject = vts->GetMetaObject();
		wxASSERT(metaObject);
		return id == metaObject->GetMetaID();
		});

	if (foundedIt == m_aObjectTables.end())
		return false;

	tableValue = *foundedIt;
	return true;
}

void IRecordDataObjectRef::Modify(bool mod)
{
	CValueForm* foundedForm = CValueForm::FindFormByGuid(m_objGuid);

	if (foundedForm != NULL) {
		foundedForm->Modify(mod);
	}

	m_objModified = mod;
}

#include "frontend/windows/generationWnd.h"

bool IRecordDataObjectRef::Generate()
{
	if (m_newObject)
		return false;
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	CGenerationWnd* selectDataType =
		new CGenerationWnd(metaData, m_metaObject->GetGenMetaId());
	meta_identifier_t sel_id = 0;
	if (selectDataType->ShowModal(sel_id)) {
		IMetaObjectRecordDataMutableRef* meta = NULL;
		if (metaData->GetMetaObject(meta, sel_id)) {
			IRecordDataObjectRef* obj = meta->CreateObjectValue(this, true);
			if (obj != NULL) {
				obj->ShowFormValue();
				return true;
			}
		}
	}
	return false;
}

bool IRecordDataObjectRef::Filling(CValue& cValue) const
{
	CValue standartProcessing = true;
	if (m_procUnit != NULL) {
		m_procUnit->CallFunction("Filling", cValue, standartProcessing);
	}
	return standartProcessing.GetBoolean();
}

void IRecordDataObjectRef::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	IRecordDataObject::SetValueByMetaID(id, cVal);
	IRecordDataObjectRef::Modify(true);
}

CValue IRecordDataObjectRef::GetValueByMetaID(const meta_identifier_t& id) const
{
	return IRecordDataObject::GetValueByMetaID(id);
}

IRecordDataObjectRef* IRecordDataObjectRef::CopyObjectValue()
{
	return m_metaObject->CreateObjectValue(this);
}

void IRecordDataObjectRef::PrepareEmptyObject()
{
	m_aObjectValues.clear();
	//attrbutes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		m_aObjectValues[attribute->GetMetaID()] = attribute->CreateValue();
	}
	m_aObjectTables.clear();
	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}

	m_objModified = true;
}

void IRecordDataObjectRef::PrepareEmptyObject(const IRecordDataObjectRef* source)
{
	m_aObjectValues.clear();
	IMetaAttributeObject* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_aObjectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (attribute != codeAttribute) {
			m_aObjectValues[attribute->GetMetaID()] = source->GetValueByMetaID(attribute->GetMetaID());
		}
	}
	m_aObjectTables.clear();
	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
		tableSection->LoadDataFromTable(source->GetTableByMetaID(table->GetMetaID()));
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}

	m_objModified = true;
}

CReferenceDataObject* IRecordDataObjectRef::GetReference() const
{
	if (m_newObject) {
		return CReferenceDataObject::Create(m_metaObject);
	}

	return CReferenceDataObject::Create(m_metaObject, m_objGuid);
}

//***********************************************************************
//*                        IRecordDataObjectGroupRef					*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObjectGroupRef, IRecordDataObjectRef);

IRecordDataObjectGroupRef::IRecordDataObjectGroupRef(IMetaObjectRecordDataGroupMutableRef* metaObject, const Guid& objGuid, int objMode)
	: IRecordDataObjectRef(metaObject, objGuid), m_objMode(objMode)
{
}

IRecordDataObjectGroupRef::IRecordDataObjectGroupRef(const IRecordDataObjectGroupRef& source)
	: IRecordDataObjectRef(source), m_objMode(source.m_objMode)
{
}

IRecordDataObjectGroupRef::~IRecordDataObjectGroupRef()
{
}

bool IRecordDataObjectGroupRef::InitializeObject()
{
	if (IRecordDataObjectRef::InitializeObject()) {
		IMetaObjectRecordDataGroupMutableRef* metaObject = GetMetaObject();
		return true;
	}

	return false;
}

bool IRecordDataObjectGroupRef::InitializeObject(IRecordDataObjectRef* source, bool generate)
{
	if (IRecordDataObjectRef::InitializeObject(source, generate)) {
		IMetaObjectRecordDataGroupMutableRef* metaObject = GetMetaObject();
		return true;
	}

	return false;
}

CSourceExplorer IRecordDataObjectGroupRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);
	IMetaAttributeObject* metaAttribute = m_metaObject->GetAttributeForCode();
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CMetaGroupAttributeObject* metaAttr = NULL; eUseItem attrUse = eUseItem::eUseItem_Folder_Item;
		if (attribute->ConvertToValue(metaAttr)) {
			attrUse = metaAttr->GetAttrUse();
		}
		if (m_objMode == OBJECT_NORMAL) {
			if (attrUse == eUseItem::eUseItem_Item
				|| attrUse == eUseItem::eUseItem_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute, attribute != metaAttribute);
				}
			}
		}
		else {
			if (attrUse == eUseItem::eUseItem_Folder ||
				attrUse == eUseItem::eUseItem_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute, attribute != metaAttribute);
				}
			}
		}
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		CMetaGroupTableObject* metaTable = NULL; eUseItem tableUse = eUseItem::eUseItem_Folder_Item;
		if (table->ConvertToValue(metaTable)) {
			tableUse = metaTable->GetTableUse();
		}
		if (m_objMode == OBJECT_NORMAL) {
			if (tableUse == eUseItem::eUseItem_Item
				|| tableUse == eUseItem::eUseItem_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
		else {
			if (tableUse == eUseItem::eUseItem_Folder ||
				tableUse == eUseItem::eUseItem_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
	}

	return srcHelper;
}

bool IRecordDataObjectGroupRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto foundedIt = std::find_if(m_aObjectTables.begin(), m_aObjectTables.end(), [id](ITabularSectionDataObject* vts) {
		CMetaTableObject* metaObject = vts->GetMetaObject();
		wxASSERT(metaObject);
		return id == metaObject->GetMetaID();
		});

	if (foundedIt == m_aObjectTables.end())
		return false;

	tableValue = *foundedIt;
	return true;
}

void IRecordDataObjectGroupRef::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	const CValue& cOldValue = IRecordDataObjectRef::GetValueByMetaID(id);
	if (cOldValue.GetType() != TYPE_NULL)
		IRecordDataObjectRef::SetValueByMetaID(id, cVal);
}

CValue IRecordDataObjectGroupRef::GetValueByMetaID(const meta_identifier_t& id) const
{
	return IRecordDataObjectRef::GetValueByMetaID(id);
}

void IRecordDataObjectGroupRef::PrepareEmptyObject()
{
	m_aObjectValues.clear();

	//attrbutes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CMetaGroupAttributeObject* metaAttr = NULL; eUseItem attrUse = eUseItem::eUseItem_Folder_Item;
		if (attribute->ConvertToValue(metaAttr)) {
			attrUse = metaAttr->GetAttrUse();
		}
		if (m_objMode == OBJECT_NORMAL) {
			if (attrUse == eUseItem::eUseItem_Item
				|| attrUse == eUseItem::eUseItem_Folder_Item) {
				m_aObjectValues[attribute->GetMetaID()] = attribute->CreateValue();
			}
			else {
				m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_NULL;
			}
		}
		else {
			if (attrUse == eUseItem::eUseItem_Folder ||
				attrUse == eUseItem::eUseItem_Folder_Item) {
				m_aObjectValues[attribute->GetMetaID()] = attribute->CreateValue();
			}
			else {
				m_aObjectValues[attribute->GetMetaID()] = eValueTypes::TYPE_NULL;
			}
		}
	}

	m_aObjectTables.clear();

	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CMetaGroupTableObject* metaTable = NULL; eUseItem tableUse = eUseItem::eUseItem_Folder_Item;
		if (table->ConvertToValue(metaTable)) {
			tableUse = metaTable->GetTableUse();
		}
		if (m_objMode == OBJECT_NORMAL) {
			if (tableUse == eUseItem::eUseItem_Item
				|| tableUse == eUseItem::eUseItem_Folder_Item) {
				CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
				m_aObjectValues[table->GetMetaID()] = tableSection;
				m_aObjectTables.push_back(tableSection);
			}
			else {
				m_aObjectValues[table->GetMetaID()] = eValueTypes::TYPE_NULL;
			}
		}
		else {
			if (tableUse == eUseItem::eUseItem_Folder ||
				tableUse == eUseItem::eUseItem_Folder_Item) {
				CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
				m_aObjectValues[table->GetMetaID()] = tableSection;
				m_aObjectTables.push_back(tableSection);
			}
			else {
				m_aObjectValues[table->GetMetaID()] = eValueTypes::TYPE_NULL;
			}
		}
	}

	m_objModified = true;
}

void IRecordDataObjectGroupRef::PrepareEmptyObject(const IRecordDataObjectRef* source)
{
	m_aObjectValues.clear();
	IMetaAttributeObject* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_aObjectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CMetaGroupAttributeObject* metaAttr = NULL; eUseItem attrUse = eUseItem::eUseItem_Folder_Item;
		if (attribute->ConvertToValue(metaAttr)) {
			attrUse = metaAttr->GetAttrUse();
		}
		if (attribute != codeAttribute) {
			m_aObjectValues[attribute->GetMetaID()] = source->GetValueByMetaID(attribute->GetMetaID());
		}
	}
	m_aObjectTables.clear();
	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CMetaGroupTableObject* metaTable = NULL; eUseItem tableUse = eUseItem::eUseItem_Folder_Item;
		if (table->ConvertToValue(metaTable)) {
			tableUse = metaTable->GetTableUse();
		}
		CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
		tableSection->LoadDataFromTable(source->GetTableByMetaID(table->GetMetaID()));
		m_aObjectValues[table->GetMetaID()] = tableSection;
		m_aObjectTables.push_back(tableSection);
	}

	m_objModified = true;
}

//***********************************************************************
//*						     metadata									* 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordSetObject, IValueTable);
wxIMPLEMENT_ABSTRACT_CLASS(IRecordManagerObject, CValue);

wxIMPLEMENT_ABSTRACT_CLASS(CRecordKeyObject, CValue);

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterKeyValue, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue, CValue);

//***********************************************************************
//*                      Record key & set								*
//***********************************************************************

//////////////////////////////////////////////////////////////////////
//						  CRecordKeyObject							//
//////////////////////////////////////////////////////////////////////

CRecordKeyObject::CRecordKeyObject(IMetaObjectRegisterData* metaObject) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methods(new CMethods())
{
}

CRecordKeyObject::~CRecordKeyObject()
{
	wxDELETE(m_methods);
}

bool CRecordKeyObject::IsEmpty() const
{
	for (auto value : m_aKeyValues) {
		const CValue& cValue = value.second;
		if (!cValue.IsEmpty())
			return false;
	}

	return true;
}

CLASS_ID CRecordKeyObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CRecordKeyObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CRecordKeyObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////
//						  IRecordManagerObject						//
//////////////////////////////////////////////////////////////////////

void IRecordManagerObject::CreateEmptyKey()
{
	m_recordSet->CreateEmptyKey();
}

bool IRecordManagerObject::InitializeObject(const IRecordManagerObject* source, bool newRecord)
{
	if (!m_recordSet->InitializeObject(source ? source->GetRecordSet() : NULL, newRecord))
		return false;

	if (!appData->DesignerMode()) {
		if (!newRecord && !ReadData()) {
			PrepareEmptyObject(source);
		}
		else if (newRecord) {
			PrepareEmptyObject(source);
		}
	}
	else {
		PrepareEmptyObject(source);
	}

	//is Ok
	return true;
}

IRecordManagerObject* IRecordManagerObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordManagerObjectValue(this);
}

IRecordManagerObject::IRecordManagerObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methods(new CMethods()), m_recordSet(m_metaObject->CreateRecordSetObjectValue(uniqueKey, false)), m_recordLine(NULL),
m_objGuid(uniqueKey)
{
}

IRecordManagerObject::IRecordManagerObject(const IRecordManagerObject& source) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(source.m_metaObject), m_methods(new CMethods()), m_recordSet(m_metaObject->CreateRecordSetObjectValue(source.m_recordSet, false)), m_recordLine(NULL),
m_objGuid(source.m_metaObject)
{
}

IRecordManagerObject::~IRecordManagerObject()
{
	wxDELETE(m_recordSet);
	wxDELETE(m_methods);
}

CValueForm* IRecordManagerObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return NULL;
	return CValueForm::FindFormByGuid(m_objGuid);
}

bool IRecordManagerObject::IsEmpty() const
{
	return m_recordSet->IsEmpty();
}

CSourceExplorer IRecordManagerObject::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		srcHelper.AppendSource(attribute);
	}

	return srcHelper;
}

bool IRecordManagerObject::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	return false;
}

void IRecordManagerObject::Modify(bool mod)
{
	CValueForm* foundedForm = CValueForm::FindFormByGuid(m_objGuid);

	if (foundedForm != NULL) {
		foundedForm->Modify(mod);
	}

	m_recordSet->Modify(mod);
}

bool IRecordManagerObject::IsModified() const
{
	return m_recordSet->IsModified();
}

void IRecordManagerObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	m_recordLine->SetValueByMetaID(id, cVal);
	IRecordManagerObject::Modify(true);
}

CValue IRecordManagerObject::GetValueByMetaID(const meta_identifier_t& id) const
{
	return m_recordLine->GetValueByMetaID(id);
}

CLASS_ID IRecordManagerObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordManagerObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordManagerObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////

void IRecordManagerObject::PrepareEmptyObject(const IRecordManagerObject* source)
{
	if (source == NULL) {
		m_recordLine = new IRecordSetObject::CRecordSetRegisterReturnLine(
			m_recordSet,
			m_recordSet->AppenRow()
		);
	}
	else if (source != NULL) {
		m_recordLine = m_recordSet->GetRowAt(0);
	}

	m_recordSet->Modify(true);
}

//////////////////////////////////////////////////////////////////////
//						  IRecordSetObject							//
//////////////////////////////////////////////////////////////////////

#include "metadata/singleMetaTypes.h"

void IRecordSetObject::CreateEmptyKey()
{
	m_aKeyValues.clear();

	for (auto attributes : m_metaObject->GetGenericDimensions()) {
		m_aKeyValues.insert_or_assign(attributes->GetMetaID(), attributes->CreateValue());
	}
}

bool IRecordSetObject::InitializeObject(const IRecordSetObject* source, bool newRecord)
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_compileModule == NULL) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}

	if (appData->EnterpriseMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const CTranslateError* err) {
			if (appData->EnterpriseMode()) {
				CSystemObjects::Raise(err->what());
			}

			return false;
		};
	}

	if (source != NULL) {
		m_aObjectValues = source->m_aObjectValues;
	}

	if (!appData->DesignerMode()) {
		if (!newRecord) {
			ReadData();
		}
	}

	if (appData->EnterpriseMode()) {

		wxASSERT(m_procUnit == NULL);

		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	//is Ok
	return true;
}

///////////////////////////////////////////////////////////////////////////////////

bool IRecordSetObject::FindKeyValue(const meta_identifier_t& id) const
{
	return m_aKeyValues.find(id) != m_aKeyValues.end();
}

void IRecordSetObject::SetKeyValue(const meta_identifier_t& id, const CValue& cValue)
{
	IMetaAttributeObject* metaAttribute =
		m_metaObject->FindGenericAttribute(id);
	wxASSERT(metaAttribute);
	m_aKeyValues.insert_or_assign(
		id, metaAttribute != NULL ? metaAttribute->AdjustValue(cValue) : cValue
	);
}

CValue IRecordSetObject::GetKeyValue(const meta_identifier_t& id) const
{
	return m_aKeyValues.at(id);
}

void IRecordSetObject::EraseKeyValue(const meta_identifier_t& id)
{
	m_aKeyValues.erase(id);
}

IRecordSetObject* IRecordSetObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordSetObjectValue(this);
}

///////////////////////////////////////////////////////////////////////////////////

IRecordSetObject::IRecordSetObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey) : IValueTable(),
m_methods(new CMethods()),
m_metaObject(metaObject), m_aKeyValues(uniqueKey.IsOk() ? uniqueKey : metaObject), m_objModified(false), m_selected(false)
{
	m_dataColumnCollection = new CRecordSetRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();
}

IRecordSetObject::IRecordSetObject(const IRecordSetObject& source) : IValueTable(),
m_methods(new CMethods()),
m_metaObject(source.m_metaObject), m_aKeyValues(source.m_aKeyValues), m_aObjectValues(source.m_aObjectValues), m_objModified(true), m_selected(false)
{
	m_dataColumnCollection = new CRecordSetRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();
}

IRecordSetObject::~IRecordSetObject()
{
	wxDELETE(m_methods);

	if (m_dataColumnCollection != NULL) {
		m_dataColumnCollection->DecrRef();
	}

	if (m_recordSetKeyValue != NULL) {
		m_recordSetKeyValue->DecrRef();
	}
}

CValue IRecordSetObject::GetAt(const CValue& cKey)
{
	unsigned int index = cKey.ToUInt();

	if (index >= m_aObjectValues.size() && !appData->DesignerMode())
		CTranslateError::Error(_("Index outside array bounds"));

	return new CRecordSetRegisterReturnLine(this, index);
}

CLASS_ID IRecordSetObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordSetObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordSetObject::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

#include "compiler/valueTable.h"

bool IRecordSetObject::LoadDataFromTable(IValueTable* srcTable)
{
	IValueTableColumnCollection* colData = srcTable->GetColumns();

	if (colData == NULL)
		return false;

	wxArrayString columnsName;

	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueTableColumnCollection::IValueTableColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_dataColumnCollection->GetColumnByName(colInfo->GetColumnName()) != NULL) {
			columnsName.push_back(colInfo->GetColumnName());
		}
	}

	unsigned int rowsCount = srcTable->GetCount();

	for (unsigned int row = 0; row < rowsCount; row++) {
		IValueTableReturnLine* retLine = srcTable->GetRowAt(row);
		IValueTableReturnLine* newRetLine = new CRecordSetRegisterReturnLine(this, AppenRow());
		newRetLine->PrepareNames();
		for (auto colName : columnsName) {
			newRetLine->SetAttribute(colName, retLine->GetAttribute(colName));
		}
		wxDELETE(newRetLine);
	}

	return true;
}

IValueTable* IRecordSetObject::SaveDataToTable() const
{
	CValueTable* valueTable = new CValueTable;
	IValueTableColumnCollection* colData = valueTable->GetColumns();

	for (unsigned int idx = 0; idx < m_dataColumnCollection->GetColumnCount() - 1; idx++) {
		IValueTableColumnCollection::IValueTableColumnInfo* colInfo = m_dataColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		IValueTableColumnCollection::IValueTableColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnTypes(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}

	valueTable->PrepareNames();

	for (auto row : m_aObjectValues) {
		CValueTable::CValueTableReturnLine* retLine = valueTable->AddRow();
		wxASSERT(retLine);
		for (auto cols : row) {
			retLine->SetValueByMetaID(cols.first, cols.second);
		}
	}

	return valueTable;
}

bool IRecordSetObject::IsEmpty() const
{
	return m_selected;
}

void IRecordSetObject::Modify(bool mod)
{
	m_objModified = mod;
}

bool IRecordSetObject::IsModified() const
{
	return m_objModified;
}

void IRecordSetObject::SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal)
{
	IMetaAttributeObject* metaAttribute = m_metaObject->FindGenericAttribute(id);
	wxASSERT(metaAttribute);

	m_aObjectValues[line][id] = metaAttribute->AdjustValue(cVal);
	IRecordSetObject::Cleared();
}

CValue IRecordSetObject::GetValueByMetaID(long line, const meta_identifier_t& id) const
{
	IMetaAttributeObject* metaAttribute = m_metaObject->FindAttribute(id);
	wxASSERT(metaAttribute);

	if (appData->DesignerMode()) {
		return metaAttribute->CreateValue();
	}

	return m_aObjectValues[line].at(id);
}

//////////////////////////////////////////////////////////////////////
//					CRecordSetRegisterColumnCollection				//
//////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterColumnCollection, IValueTable::IValueTableColumnCollection);

IRecordSetObject::CRecordSetRegisterColumnCollection::CRecordSetRegisterColumnCollection() : IValueTableColumnCollection(), m_methods(NULL), m_ownerTable(NULL)
{
}

IRecordSetObject::CRecordSetRegisterColumnCollection::CRecordSetRegisterColumnCollection(IRecordSetObject* ownerTable) : IValueTableColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable)
{
	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	wxASSERT(metaObject);

	for (auto attributes : metaObject->GetGenericAttributes()) {
		CValueRecordSetRegisterColumnInfo* columnInfo = new CValueRecordSetRegisterColumnInfo(attributes);
		m_aColumnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

IRecordSetObject::CRecordSetRegisterColumnCollection::~CRecordSetRegisterColumnCollection()
{
	for (auto& colInfo : m_aColumnInfo) {
		CValueRecordSetRegisterColumnInfo* columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methods);
}

void IRecordSetObject::CRecordSetRegisterColumnCollection::SetAt(const CValue& cKey, CValue& cVal)//индекс массива должен начинаться с 0
{
}

CValue IRecordSetObject::CRecordSetRegisterColumnCollection::GetAt(const CValue& cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_aColumnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_aColumnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
}

//////////////////////////////////////////////////////////////////////
//					CValueRecordSetRegisterColumnInfo               //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, IValueTable::IValueTableColumnCollection::IValueTableColumnInfo);

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo() : IValueTableColumnInfo(), m_metaAttribute(NULL) {}

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo(IMetaAttributeObject* metaAttribute) : IValueTableColumnInfo(), m_metaAttribute(metaAttribute) {}

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::~CValueRecordSetRegisterColumnInfo() {  }

//////////////////////////////////////////////////////////////////////
//					 CRecordSetRegisterReturnLine					//
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterReturnLine, IValueTable::IValueTableReturnLine);

IRecordSetObject::CRecordSetRegisterReturnLine::CRecordSetRegisterReturnLine()
	: IValueTableReturnLine(), m_methods(new CMethods())
{
}

IRecordSetObject::CRecordSetRegisterReturnLine::CRecordSetRegisterReturnLine(IRecordSetObject* ownerTable, int line)
	: IValueTableReturnLine(), m_ownerTable(ownerTable), m_lineTable(line), m_methods(new CMethods())
{
}

IRecordSetObject::CRecordSetRegisterReturnLine::~CRecordSetRegisterReturnLine()
{
	wxDELETE(m_methods);
}

void IRecordSetObject::CRecordSetRegisterReturnLine::PrepareNames() const
{
	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();

	std::vector<SEng> aAttributes;

	for (auto attribute : metaObject->GetGenericAttributes())
	{
		SEng aAttribute;
		aAttribute.sName = attribute->GetName();
		aAttribute.iName = attribute->GetMetaID();
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void IRecordSetObject::CRecordSetRegisterReturnLine::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
	m_ownerTable->SetValueByMetaID(m_lineTable, id, cVal);
}

CValue IRecordSetObject::CRecordSetRegisterReturnLine::GetValueByMetaID(const meta_identifier_t& id) const
{
	return m_ownerTable->GetValueByMetaID(m_lineTable, id);
}

//////////////////////////////////////////////////////////////////////
//				       CRecordSetRegisterKeyValue					//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyValue(IRecordSetObject* recordSet) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_recordSet(recordSet)
{
}

IRecordSetObject::CRecordSetRegisterKeyValue::~CRecordSetRegisterKeyValue()
{
	wxDELETE(m_methods);
}

//////////////////////////////////////////////////////////////////////
//						CRecordSetRegisterKeyDescriptionValue		//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::CRecordSetRegisterKeyDescriptionValue(IRecordSetObject* recordSet, const meta_identifier_t& id) : CValue(eValueTypes::TYPE_VALUE),
m_methods(new CMethods()), m_recordSet(recordSet), m_metaId(id)
{
}

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::~CRecordSetRegisterKeyDescriptionValue()
{
	wxDELETE(m_methods);
}

//////////////////////////////////////////////////////////////////////////////////////

bool CMetaGroupAttributeObject::LoadData(CMemoryReader& reader)
{
	if (!CMetaAttributeObject::LoadData(reader))
		return false;

	m_propertyUse->SetValue(reader.r_u16());
	return true;
}

bool CMetaGroupAttributeObject::SaveData(CMemoryWriter& writer)
{
	if (!CMetaAttributeObject::SaveData(writer))
		return false;

	writer.w_u16(m_propertyUse->GetValueAsInteger());
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////

bool CMetaGroupTableObject::LoadData(CMemoryReader& reader)
{
	if (!CMetaTableObject::LoadData(reader))
		return false;

	m_propertyUse->SetValue(reader.r_u16());
	return true;
}

bool CMetaGroupTableObject::SaveData(CMemoryWriter& writer)
{
	if (!CMetaTableObject::SaveData(writer))
		return false;

	writer.w_u16(m_propertyUse->GetValueAsInteger());
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

METADATA_REGISTER(CMetaGroupAttributeObject, "groupAttribute", g_metaGroupAttributeCLSID);
METADATA_REGISTER(CMetaGroupTableObject, "groupTable", g_metaGroupTableCLSID);

SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterColumnCollection, "recordSetRegisterColumn", CRecordSetRegisterColumnCollection, TEXT2CLSID("VL_RSCL"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, "recordSetRegisterColumnInfo", CValueRecordSetRegisterColumnInfo, TEXT2CLSID("VL_RSCI"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterReturnLine, "recordSetRegisterRow", CRecordSetRegisterReturnLine, TEXT2CLSID("VL_RSCR"));

SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterKeyValue, "recordSetRegisterKey", CRecordSetRegisterKeyValue, TEXT2CLSID("VL_RSCK"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue, "recordSetRegisterKeyDescription", CRecordSetRegisterKeyDescriptionValue, TEXT2CLSID("VL_RDVL"));