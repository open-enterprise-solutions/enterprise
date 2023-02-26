////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "object.h"
#include "core/metadata/metadata.h"
#include "core/common/srcExplorer.h"
#include "core/compiler/systemObjects.h"
#include "core/metadata/singleClass.h"
#include "utils/stringUtils.h"

//***********************************************************************
//*								 metadata                               * 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectWrapperData, IMetaObject);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRegisterData, IMetaObjectWrapperData);

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordData, IMetaObjectWrapperData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataExt, IMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataRef, IMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataEnumRef, IMetaObjectRecordDataRef);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataMutableRef, IMetaObjectRecordDataRef);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataFolderMutableRef, IMetaObjectRecordDataMutableRef);

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
		if (id == metaObject->GetMetaID())
			return metaObject;
	}
	return NULL;
}

CValueForm* IMetaObjectWrapperData::GetGenericForm(const wxString& formName, IControlFrame* ownerControl, const CUniqueKey& formGuid)
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

	if (!formName.IsEmpty())
		CSystemObjects::Raise(_("Сommon form not found '") + formName + "'");

	return NULL;
}

CValueForm* IMetaObjectWrapperData::GetGenericForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;
	for (auto metaForm : GetGenericForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	if (defList == NULL)
		return NULL;

	return GetGenericForm(defList->GetName(),
		ownerControl, formGuid
	);
}

//***********************************************************************
//*                           IMetaObjectRecordData					    *
//***********************************************************************

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

CValueForm* IMetaObjectRecordData::GetObjectForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid)
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
	//get quick choice
	m_propertyQuickChoice->SetValue(dataReader.r_u8());

	//load default attributes:
	m_attributeReference->LoadMeta(dataReader);

	return IMetaObjectRecordData::LoadData(dataReader);
}

bool IMetaObjectRecordDataRef::SaveData(CMemoryWriter& dataWritter)
{
	//set quick choice
	dataWritter.w_u8(m_propertyQuickChoice->GetValueAsBoolean());

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
		m_attributeReference->SetDefaultMetatype(singleObject->GetTypeClass());
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
bool IMetaObjectRecordDataRef::ProcessChoice(IControlFrame* ownerValue, const meta_identifier_t& id, eSelectMode selMode)
{
	CValueForm* selectForm = IMetaObjectRecordDataRef::GetSelectForm(id, ownerValue);
	if (selectForm == NULL)
		return false;
	selectForm->ShowForm();
	return true;
}

CReferenceDataObject* IMetaObjectRecordDataRef::FindObjectValue(const Guid& guid)
{
	if (!guid.isValid())
		return NULL;
	return CReferenceDataObject::Create(this, guid);
}

CValueForm* IMetaObjectRecordDataRef::GetListForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid)
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

CValueForm* IMetaObjectRecordDataRef::GetSelectForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid)
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

//***********************************************************************
//*						IMetaObjectRecordDataEnumRef					*
//***********************************************************************

///////////////////////////////////////////////////////////////////////////////////////////////

IMetaObjectRecordDataEnumRef::IMetaObjectRecordDataEnumRef() : IMetaObjectRecordDataRef()
{
	m_attributeOrder = CMetaDefaultAttributeObject::CreateNumber(wxT("order"), _("Order"), wxEmptyString, 6, true);
	//set child/parent
	m_attributeOrder->SetParent(this);
	AddChild(m_attributeOrder);
}

IMetaObjectRecordDataEnumRef::~IMetaObjectRecordDataEnumRef()
{
	wxDELETE(m_attributeOrder);
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool IMetaObjectRecordDataEnumRef::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeOrder->LoadMeta(dataReader);
	return IMetaObjectRecordDataRef::LoadData(dataReader);
}

bool IMetaObjectRecordDataEnumRef::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeOrder->SaveMeta(dataWritter);
	return IMetaObjectRecordDataRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool IMetaObjectRecordDataEnumRef::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordDataRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeOrder->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataEnumRef::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeOrder->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataRef::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordDataEnumRef::OnSaveMetaObject()
{
	if (!m_attributeOrder->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnSaveMetaObject();
}

bool IMetaObjectRecordDataEnumRef::OnDeleteMetaObject()
{
	if (!m_attributeOrder->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnDeleteMetaObject();
}

bool IMetaObjectRecordDataEnumRef::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeOrder->OnBeforeRunMetaObject(flags))
		return false;

	return IMetaObjectRecordDataRef::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRecordDataEnumRef::OnAfterRunMetaObject(int flags)
{
	return IMetaObjectRecordDataRef::OnAfterRunMetaObject(flags);
}

bool IMetaObjectRecordDataEnumRef::OnBeforeCloseMetaObject()
{
	return IMetaObjectRecordDataRef::OnBeforeCloseMetaObject();
}

bool IMetaObjectRecordDataEnumRef::OnAfterCloseMetaObject()
{
	if (!m_attributeOrder->OnAfterCloseMetaObject())
		return false;

	return IMetaObjectRecordDataRef::OnAfterCloseMetaObject();
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
	//set child/parent
	m_attributeDeletionMark->SetParent(this);
	AddChild(m_attributeDeletionMark);
}

IMetaObjectRecordDataMutableRef::~IMetaObjectRecordDataMutableRef()
{
	wxDELETE(m_attributeDeletionMark);
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

IRecordDataObjectRef* IMetaObjectRecordDataMutableRef::CreateObjectValue()
{
	IRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}

	return createdValue;
}

IRecordDataObjectRef* IMetaObjectRecordDataMutableRef::CreateObjectValue(const Guid& guid)
{
	IRecordDataObjectRef* createdValue = CreateObjectRefValue(guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

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

IRecordDataObjectRef* IMetaObjectRecordDataMutableRef::CopyObjectValue(const Guid& srcGuid)
{
	IRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(srcGuid)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordDataObject* IMetaObjectRecordDataMutableRef::CreateRecordDataObject()
{
	return CreateObjectValue();
}

//***********************************************************************
//*						IMetaObjectRecordDataFolderMutableRef			*
//***********************************************************************

IMetaObjectRecordDataFolderMutableRef::IMetaObjectRecordDataFolderMutableRef()
	: IMetaObjectRecordDataMutableRef()
{
	//create default attributes
	m_attributeCode = CMetaDefaultAttributeObject::CreateString(wxT("code"), _("Code"), wxEmptyString, 8, true, eItemMode::eItemMode_Folder_Item);
	//set child/parent
	m_attributeCode->SetParent(this);
	AddChild(m_attributeCode);

	m_attributeDescription = CMetaDefaultAttributeObject::CreateString(wxT("description"), _("Description"), wxEmptyString, 150, true, eItemMode::eItemMode_Folder_Item);
	//set child/parent
	m_attributeDescription->SetParent(this);
	AddChild(m_attributeDescription);

	m_attributeParent = CMetaDefaultAttributeObject::CreateEmptyType(wxT("parent"), _("Parent"), wxEmptyString, false, eItemMode::eItemMode_Folder_Item, eSelectMode::eSelectMode_Folders);
	//set child/parent
	m_attributeParent->SetParent(this);
	AddChild(m_attributeParent);

	m_attributeIsFolder = CMetaDefaultAttributeObject::CreateBoolean(wxT("isFolder"), _("Is folder"), wxEmptyString, eItemMode::eItemMode_Folder_Item);
	//set child/parent
	m_attributeIsFolder->SetParent(this);
	AddChild(m_attributeIsFolder);
}

IMetaObjectRecordDataFolderMutableRef::~IMetaObjectRecordDataFolderMutableRef()
{
	wxDELETE(m_attributeCode);
	wxDELETE(m_attributeDescription);
	wxDELETE(m_attributeParent);
	wxDELETE(m_attributeIsFolder);
}

////////////////////////////////////////////////////////////////////////////////////////////////

IRecordDataObjectFolderRef* IMetaObjectRecordDataFolderMutableRef::CreateObjectValue(eObjectMode mode)
{
	IRecordDataObjectFolderRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordDataObjectFolderRef* IMetaObjectRecordDataFolderMutableRef::CreateObjectValue(eObjectMode mode, const Guid& guid)
{
	IRecordDataObjectFolderRef* createdValue = CreateObjectRefValue(mode, guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordDataObjectFolderRef* IMetaObjectRecordDataFolderMutableRef::CreateObjectValue(eObjectMode mode, IRecordDataObjectRef* objSrc, bool generate)
{
	IRecordDataObjectFolderRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

IRecordDataObjectFolderRef* IMetaObjectRecordDataFolderMutableRef::CopyObjectValue(eObjectMode mode, const Guid& srcGuid)
{
	IRecordDataObjectFolderRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject(srcGuid)) {
		wxDELETE(createdValue);
		return NULL;
	}
	return createdValue;
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool IMetaObjectRecordDataFolderMutableRef::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeCode->LoadMeta(dataReader);
	m_attributeDescription->LoadMeta(dataReader);
	m_attributeParent->LoadMeta(dataReader);
	m_attributeIsFolder->LoadMeta(dataReader);

	return IMetaObjectRecordDataMutableRef::LoadData(dataReader);
}

bool IMetaObjectRecordDataFolderMutableRef::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeCode->SaveMeta(dataWritter);
	m_attributeDescription->SaveMeta(dataWritter);
	m_attributeParent->SaveMeta(dataWritter);
	m_attributeIsFolder->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectRecordDataMutableRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool IMetaObjectRecordDataFolderMutableRef::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeCode->OnCreateMetaObject(metaData) &&
		m_attributeDescription->OnCreateMetaObject(metaData) &&
		m_attributeParent->OnCreateMetaObject(metaData) &&
		m_attributeIsFolder->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataFolderMutableRef::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeCode->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeDescription->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeParent->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeIsFolder->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataMutableRef::OnLoadMetaObject(metaData);
}

bool IMetaObjectRecordDataFolderMutableRef::OnSaveMetaObject()
{
	if (!m_attributeCode->OnSaveMetaObject())
		return false;

	if (!m_attributeDescription->OnSaveMetaObject())
		return false;

	if (!m_attributeParent->OnSaveMetaObject())
		return false;

	if (!m_attributeIsFolder->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataMutableRef::OnSaveMetaObject();
}

bool IMetaObjectRecordDataFolderMutableRef::OnDeleteMetaObject()
{
	if (!m_attributeCode->OnDeleteMetaObject())
		return false;

	if (!m_attributeDescription->OnDeleteMetaObject())
		return false;

	if (!m_attributeParent->OnDeleteMetaObject())
		return false;

	if (!m_attributeIsFolder->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataMutableRef::OnDeleteMetaObject();
}

bool IMetaObjectRecordDataFolderMutableRef::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeCode->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeDescription->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeParent->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeIsFolder->OnBeforeRunMetaObject(flags))
		return false;

	if (!IMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	return true;
}

bool IMetaObjectRecordDataFolderMutableRef::OnAfterRunMetaObject(int flags)
{
	return IMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags);
}

bool IMetaObjectRecordDataFolderMutableRef::OnBeforeCloseMetaObject()
{
	return IMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject();
}

bool IMetaObjectRecordDataFolderMutableRef::OnAfterCloseMetaObject()
{
	if (!m_attributeCode->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeDescription->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeParent->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeIsFolder->OnAfterCloseMetaObject())
		return false;

	return IMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject();
}

//////////////////////////////////////////////////////////////////////

bool IMetaObjectRecordDataFolderMutableRef::ProcessChoice(IControlFrame* ownerValue, const meta_identifier_t& id, eSelectMode selMode)
{
	CValueForm* selectForm = NULL;
	if (selMode == eSelectMode::eSelectMode_Items || selMode == eSelectMode::eSelectMode_FoldersAndItems) {
		selectForm = IMetaObjectRecordDataFolderMutableRef::GetSelectForm(id, ownerValue);
	}
	else if (selMode == eSelectMode::eSelectMode_Folders) {
		selectForm = IMetaObjectRecordDataFolderMutableRef::GetFolderSelectForm(id, ownerValue);
	}
	if (selectForm == NULL)
		return false;
	selectForm->ShowForm();
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

CValueForm* IMetaObjectRecordDataFolderMutableRef::GetFolderForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetFolderForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

CValueForm* IMetaObjectRecordDataFolderMutableRef::GetFolderSelectForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetFolderSelectForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

//////////////////////////////////////////////////////////////////////

IRecordDataObjectRef* IMetaObjectRecordDataFolderMutableRef::CreateObjectRefValue(const Guid& objGuid)
{
	return CreateObjectRefValue(eObjectMode::OBJECT_ITEM, objGuid);
}

//***********************************************************************
//*                      IMetaObjectRegisterData						*
//***********************************************************************

IMetaObjectRegisterData::IMetaObjectRegisterData() : IMetaObjectWrapperData()
{
	//create default attributes
	m_attributeLineActive = CMetaDefaultAttributeObject::CreateBoolean(wxT("active"), _("Active"), wxEmptyString, false, true);
	m_attributePeriod = CMetaDefaultAttributeObject::CreateDate(wxT("period"), _("Period"), wxEmptyString, eDateFractions::eDateFractions_DateTime, true);
	m_attributeRecorder = CMetaDefaultAttributeObject::CreateEmptyType(wxT("recorder"), _("Recorder"), wxEmptyString);
	m_attributeLineNumber = CMetaDefaultAttributeObject::CreateNumber(wxT("lineNumber"), _("Line number"), wxEmptyString, 15, 0);

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
//***************************************************************************

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

#include "core/metadata/singleClass.h"

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

CValueForm* IMetaObjectRegisterData::GetListForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid)
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

#include "core/metadata/metaObjects/dimension/metaDimensionObject.h"
#include "core/metadata/metaObjects/resource/metaResourceObject.h"
#include "core/metadata/metaObjects/attribute/metaAttributeObject.h"

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

IMetaAttributeObject* IMetaObjectRegisterData::FindProp(const meta_identifier_t& id) const
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

IRecordManagerObject* IMetaObjectRegisterData::CopyRecordManagerObjectValue(const CUniquePairKey& uniqueKey)
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue(uniqueKey);
	if (createdValue && !createdValue->InitializeObject(NULL, true, true)) {
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
	m_methodHelper(new CMethodHelper()) {
}

IRecordDataObject::IRecordDataObject(const IRecordDataObject& source) :
	CValue(eValueTypes::TYPE_VALUE), IObjectValueInfo(wxNewUniqueGuid, true),
	m_methodHelper(new CMethodHelper()) {
}

IRecordDataObject::~IRecordDataObject() {
	wxDELETE(m_methodHelper);
}

CValueForm* IRecordDataObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return NULL;
	return CValueForm::FindFormByGuid(m_objGuid);
}

CLASS_ID IRecordDataObject::GetTypeClass() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle* clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enObject);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
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
		metaObject, GetTypeClass(),
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

#include "core/metadata/metaObjects/objects/tabularSection/tabularSection.h"

bool IRecordDataObject::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto foundedIt = m_objectValues.find(id);
	if (foundedIt != m_objectValues.end()) {
		const CValue& cTabularSection = foundedIt->second;
		return cTabularSection.ConvertToValue(tableValue);
	};

	tableValue = NULL;
	return false;
}

bool IRecordDataObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal)
{
	auto foundedIt = m_objectValues.find(id);
	wxASSERT(foundedIt != m_objectValues.end());
	if (foundedIt != m_objectValues.end()) {
		IMetaObjectRecordData* metaObjectValue = GetMetaObject();
		wxASSERT(metaObjectValue);
		IMetaAttributeObject* metaAttribute = wxDynamicCast(
			metaObjectValue->FindMetaObjectByID(id), IMetaAttributeObject
		);
		wxASSERT(metaAttribute);
		foundedIt->second = metaAttribute->AdjustValue(varMetaVal);
		return true;
	}
	return false;
}

bool IRecordDataObject::GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	auto foundedIt = m_objectValues.find(id);
	wxASSERT(foundedIt != m_objectValues.end());
	if (foundedIt != m_objectValues.end()) {
		pvarMetaVal = foundedIt->second;
		return true;
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

IValueTable* IRecordDataObject::GetTableByMetaID(const meta_identifier_t& id) const
{
	const CValue& cTable = GetValueByMetaID(id); IValueTable* retTable = NULL;
	if (cTable.ConvertToValue(retTable))
		return retTable;
	return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

#define thisObject wxT("thisObject")

void IRecordDataObject::PrepareEmptyObject()
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	m_objectValues.clear();
	//attrbutes can refValue 
	for (auto attribute : metaObject->GetGenericAttributes()) {
		m_objectValues.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
	}

	// table is collection values 
	for (auto table : metaObject->GetObjectTables()) {
		m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObject(this, table));
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void IRecordDataObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc("getFormObject", 2, "getFormObject(string, owner)");
	m_methodHelper->AppendFunc("getMetadata", "getMetadata()");

	m_methodHelper->AppendProp(thisObject,
		true, false, eThisObject, eSystem
	);

	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	//fill custom attributes 
	for (auto attributes : metaObject->GetGenericAttributes()) {
		if (attributes->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			attributes->GetName(),
			attributes->GetMetaID(),
			eProperty
		);
	}

	//fill custom tables 
	for (auto table : metaObject->GetObjectTables()) {
		if (table->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			table->GetName(),
			true,
			false,
			table->GetMetaID(),
			eTable
		);
	}

	if (m_procUnit != NULL) {
		byteCode_t* byteCode = m_procUnit->GetByteCode();
		for (auto exportFunction : byteCode->m_aExportFuncList) {
			m_methodHelper->AppendMethod(
				exportFunction.first,
				byteCode->GetNParams(exportFunction.second),
				byteCode->HasRetVal(exportFunction.second),
				exportFunction.second,
				eProcUnit
			);
		}
		for (auto exportVariable : byteCode->m_aExportVarList) {
			m_methodHelper->AppendProp(
				exportVariable.first,
				exportVariable.second,
				eProcUnit
			);
		}
	}
}

bool IRecordDataObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != NULL) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(
			m_methodHelper->GetPropData(lPropNum),
			varPropVal
		);
	}
	return false;
}

bool IRecordDataObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != NULL) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		return GetValueByMetaID(
			m_methodHelper->GetPropData(lPropNum), pvarPropVal
		);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum))
		{
		case eThisObject:
			pvarPropVal = GetValue();
			return true;
		}
	}
	return false;
}

bool IRecordDataObject::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_methodHelper->GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return IModuleInfo::ExecuteProc(
			GetMethodName(lMethodNum), paParams, lSizeArray
		);
	}

	return false;
}

bool IRecordDataObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_methodHelper->GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return IModuleInfo::ExecuteFunc(
			GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
		);
	}

	switch (lMethodNum)
	{
	case eGetFormObject:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxEmptyString,
			lSizeArray > 1 ? paParams[1]->ConvertToType<IValueFrame>() : NULL
		);
		return true;
	case eGetMetadata:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}

//***********************************************************************
//*                        IRecordDataObjectExt							*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObjectExt, IRecordDataObject);

IRecordDataObjectExt::IRecordDataObjectExt(IMetaObjectRecordDataExt* metaObject) :
	IRecordDataObject(wxNewUniqueGuid, true), m_metaObject(metaObject)
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
	IRecordDataObject(objGuid.isValid() ? objGuid : Guid::newGuid(GUID_TIME_BASED), !objGuid.isValid()),
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

bool IRecordDataObjectRef::InitializeObject(const Guid& copyGuid)
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
		catch (const CTranslateError* err) {
			if (appData->EnterpriseMode()) {
				CSystemObjects::Raise(err->what());
			}
			return false;
		};
	}
	bool succes = true;
	if (!appData->DesignerMode()) {
		if (m_newObject && !copyGuid.isValid()) {
			PrepareEmptyObject();
		}
		else if (m_newObject && copyGuid.isValid()) {
			succes = ReadData(copyGuid);
			if (succes) {
				IMetaAttributeObject* codeAttribute = m_metaObject->GetAttributeForCode();
				wxASSERT(codeAttribute);
				m_objectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
			}
			m_objModified = true;
		}
		else {
			succes = ReadData();
		}
		if (!succes)
			return succes;
	}
	else {
		PrepareEmptyObject();
	}
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
			m_procUnit->CallFunction("OnCopy", source->GetValue());
		}
		else if (m_newObject && source == NULL) {
			succes = Filling();
		}
		else if (generate) {
			succes = Filling(reference->GetValue());
		}
	}
	if (reference != NULL)
		reference->DecrRef();
	//is Ok
	return succes;
}

CLASS_ID IRecordDataObjectRef::GetTypeClass() const
{
	return IRecordDataObject::GetTypeClass();
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
		m_metaObject, GetTypeClass(),
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
	auto foundedIt = m_objectValues.find(id);
	if (foundedIt != m_objectValues.end()) {
		const CValue& cTabularSection = foundedIt->second;
		return cTabularSection.ConvertToValue(tableValue);
	};
	tableValue = NULL;
	return false;
}

void IRecordDataObjectRef::Modify(bool mod)
{
	CValueForm* const foundedForm = CValueForm::FindFormByGuid(m_objGuid);
	if (foundedForm != NULL)
		foundedForm->Modify(mod);
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

bool IRecordDataObjectRef::SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal)
{
	if (varMetaVal != IRecordDataObject::GetValueByMetaID(id)) {
		if (IRecordDataObject::SetValueByMetaID(id, varMetaVal)) {
			IRecordDataObjectRef::Modify(true);
			return true;
		}
		return false;
	}
	return true;
}

bool IRecordDataObjectRef::GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	return IRecordDataObject::GetValueByMetaID(id, pvarMetaVal);
}

IRecordDataObjectRef* IRecordDataObjectRef::CopyObjectValue()
{
	return m_metaObject->CreateObjectValue(this);
}

void IRecordDataObjectRef::PrepareEmptyObject()
{
	m_objectValues.clear();
	//attrbutes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		m_objectValues.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
	}
	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObjectRef(this, table));
	}
	m_objModified = true;
}

void IRecordDataObjectRef::PrepareEmptyObject(const IRecordDataObjectRef* source)
{
	m_objectValues.clear();
	IMetaAttributeObject* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_objectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (attribute != codeAttribute) {
			source->GetValueByMetaID(attribute->GetMetaID(), m_objectValues[attribute->GetMetaID()]);
		}
	}
	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(table->GetMetaID())))
			m_objectValues.insert_or_assign(table->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
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
//*                        IRecordDataObjectFolderRef					*           
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordDataObjectFolderRef, IRecordDataObjectRef);

IRecordDataObjectFolderRef::IRecordDataObjectFolderRef(IMetaObjectRecordDataFolderMutableRef* metaObject, const Guid& objGuid, eObjectMode objMode)
	: IRecordDataObjectRef(metaObject, objGuid), m_objMode(objMode)
{
}

IRecordDataObjectFolderRef::IRecordDataObjectFolderRef(const IRecordDataObjectFolderRef& source)
	: IRecordDataObjectRef(source), m_objMode(source.m_objMode)
{
}

IRecordDataObjectFolderRef::~IRecordDataObjectFolderRef()
{
}

CSourceExplorer IRecordDataObjectFolderRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
		false
	);
	IMetaAttributeObject* metaAttribute = m_metaObject->GetAttributeForCode();
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		eItemMode attrUse = attribute->GetItemMode();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (attrUse == eItemMode::eItemMode_Item
				|| attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute, attribute != metaAttribute);
				}
			}
		}
		else {
			if (attrUse == eItemMode::eItemMode_Folder ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
					srcHelper.AppendSource(attribute, attribute != metaAttribute);
				}
			}
		}
	}

	for (auto table : m_metaObject->GetObjectTables()) {
		eItemMode tableUse = table->GetTableUse();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (tableUse == eItemMode::eItemMode_Item
				|| tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
		else {
			if (tableUse == eItemMode::eItemMode_Folder ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(table);
			}
		}
	}

	return srcHelper;
}

bool IRecordDataObjectFolderRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto foundedIt = m_objectValues.find(id);
	if (foundedIt != m_objectValues.end()) {
		const CValue& cTabularSection = foundedIt->second;
		return cTabularSection.ConvertToValue(tableValue);
	};

	tableValue = NULL;
	return false;
}

IRecordDataObjectRef* IRecordDataObjectFolderRef::CopyObjectValue()
{
	return ((IMetaObjectRecordDataFolderMutableRef*)m_metaObject)->CreateObjectValue(m_objMode, this);
}

bool IRecordDataObjectFolderRef::SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal)
{
	const CValue& cOldValue = IRecordDataObjectRef::GetValueByMetaID(id);
	if (cOldValue.GetType() == TYPE_NULL)
		return false;
	if (GetMetaObject()->IsDataParent(id) && 
		varMetaVal == GetReference()) {
		return false;
	}

	return IRecordDataObjectRef::SetValueByMetaID(id, varMetaVal);
}

bool IRecordDataObjectFolderRef::GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	return IRecordDataObjectRef::GetValueByMetaID(id, pvarMetaVal);
}

void IRecordDataObjectFolderRef::PrepareEmptyObject()
{
	m_objectValues.clear();
	//attrbutes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		eItemMode attrUse = attribute->GetItemMode();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (attrUse == eItemMode::eItemMode_Item ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
			}
			else {
				m_objectValues.insert_or_assign(attribute->GetMetaID(), eValueTypes::TYPE_NULL);
			}
		}
		else {
			if (attrUse == eItemMode::eItemMode_Folder ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
			}
			else {
				m_objectValues.insert_or_assign(attribute->GetMetaID(), eValueTypes::TYPE_NULL);
			}
		}
	}
	IMetaObjectRecordDataFolderMutableRef* metaFolder = GetMetaObject();
	wxASSERT(metaFolder);
	if (m_objMode == eObjectMode::OBJECT_ITEM) {
		m_objectValues.insert_or_assign(*metaFolder->GetDataIsFolder(), false);
	}
	else if (m_objMode == eObjectMode::OBJECT_FOLDER) {
		m_objectValues.insert_or_assign(*metaFolder->GetDataIsFolder(), true);
	}
	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		eItemMode tableUse = table->GetTableUse();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (tableUse == eItemMode::eItemMode_Item ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObjectRef(this, table));
			}
			else {
				m_objectValues.insert_or_assign(table->GetMetaID(), eValueTypes::TYPE_NULL);
			}
		}
		else {
			if (tableUse == eItemMode::eItemMode_Folder ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObjectRef(this, table));
			}
			else {
				m_objectValues.insert_or_assign(table->GetMetaID(), eValueTypes::TYPE_NULL);
			}
		}
	}
	m_objModified = true;
}

void IRecordDataObjectFolderRef::PrepareEmptyObject(const IRecordDataObjectRef* source)
{
	m_objectValues.clear();
	IMetaAttributeObject* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_objectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CMetaAttributeObject* metaAttr = NULL; eItemMode attrUse = eItemMode::eItemMode_Folder_Item;
		if (attribute->ConvertToValue(metaAttr)) {
			attrUse = metaAttr->GetItemMode();
		}
		if (attribute != codeAttribute) {
			source->GetValueByMetaID(attribute->GetMetaID(), m_objectValues[attribute->GetMetaID()]);
		}
	}
	IMetaObjectRecordDataFolderMutableRef* metaFolder = GetMetaObject();
	wxASSERT(metaFolder);
	if (m_objMode == eObjectMode::OBJECT_ITEM) {
		m_objectValues.insert_or_assign(*metaFolder->GetDataIsFolder(), false);
	}
	else if (m_objMode == eObjectMode::OBJECT_FOLDER) {
		m_objectValues.insert_or_assign(*metaFolder->GetDataIsFolder(), true);
	}
	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		CMetaTableObject* metaTable = NULL; eItemMode tableUse = eItemMode::eItemMode_Folder_Item;
		if (table->ConvertToValue(metaTable))
			tableUse = metaTable->GetTableUse();

		CTabularSectionDataObjectRef* tableSection = new CTabularSectionDataObjectRef(this, table);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(table->GetMetaID())))
			m_objectValues.insert_or_assign(table->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
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
m_metaObject(metaObject), m_methodHelper(new CMethodHelper())
{
}

CRecordKeyObject::~CRecordKeyObject()
{
	wxDELETE(m_methodHelper);
}

bool CRecordKeyObject::IsEmpty() const
{
	for (auto value : m_keyValues) {
		const CValue& cValue = value.second;
		if (!cValue.IsEmpty())
			return false;
	}

	return true;
}

CLASS_ID CRecordKeyObject::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
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

bool IRecordManagerObject::InitializeObject(const IRecordManagerObject* source, bool newRecord, bool copyObject)
{
	if (!m_recordSet->InitializeObject(source ? source->GetRecordSet() : NULL, newRecord))
		return false;

	if (!appData->DesignerMode()) {
		if (copyObject && ReadData()) {
			m_recordSet->m_selected = false; // is new 
			m_recordSet->Modify(true); // and modify
		}
		else if (!newRecord && !copyObject && !ReadData()) {
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
m_metaObject(metaObject), m_methodHelper(new CMethodHelper()), m_recordSet(m_metaObject->CreateRecordSetObjectValue(uniqueKey, false)), m_recordLine(NULL),
m_objGuid(uniqueKey)
{
}

IRecordManagerObject::IRecordManagerObject(const IRecordManagerObject& source) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(source.m_metaObject), m_methodHelper(new CMethodHelper()), m_recordSet(m_metaObject->CreateRecordSetObjectValue(source.m_recordSet, false)), m_recordLine(NULL),
m_objGuid(source.m_metaObject)
{
}

IRecordManagerObject::~IRecordManagerObject()
{
	wxDELETE(m_methodHelper);

	wxDELETE(m_recordSet);
	wxDELETE(m_recordLine);
}

CValueForm* IRecordManagerObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return NULL;
	if (m_recordSet->m_selected)
		return CValueForm::FindFormByGuid(m_objGuid);
	return NULL;
}

bool IRecordManagerObject::IsEmpty() const
{
	return m_recordSet->IsEmpty();
}

CSourceExplorer IRecordManagerObject::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetTypeClass(),
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
	CValueForm* const foundedForm = CValueForm::FindFormByGuid(m_objGuid);
	if (foundedForm != NULL)
		foundedForm->Modify(mod);
	m_recordSet->Modify(mod);
}

bool IRecordManagerObject::IsModified() const
{
	return m_recordSet->IsModified();
}

bool IRecordManagerObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal)
{
	if (varMetaVal != IRecordManagerObject::GetValueByMetaID(id)) {
		bool result = m_recordLine->SetValueByMetaID(id, varMetaVal);
		IRecordManagerObject::Modify(true);
		return result;
	}
	return true;
}

bool IRecordManagerObject::GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	return m_recordLine->GetValueByMetaID(id, pvarMetaVal);
}

CLASS_ID IRecordManagerObject::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
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
			m_recordSet->GetItem(
				m_recordSet->AppendRow()
			)
		);
	}
	else if (source != NULL) {
		m_recordLine = m_recordSet->GetRowAt(
			m_recordSet->GetItem(0)
		);
	}

	m_recordSet->Modify(true);
}

//////////////////////////////////////////////////////////////////////
//						  IRecordSetObject							//
//////////////////////////////////////////////////////////////////////

#include "core/metadata/singleClass.h"

void IRecordSetObject::CreateEmptyKey()
{
	m_keyValues.clear();
	for (auto attributes : m_metaObject->GetGenericDimensions()) {
		m_keyValues.insert_or_assign(
			attributes->GetMetaID(),
			attributes->CreateValue()
		);
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
		for (long row = 0; row < source->GetRowCount(); row++) {
			wxValueTableRow* node = source->GetViewData<wxValueTableRow>(source->GetItem(row));
			wxASSERT(node);
			IValueTable::Append(new wxValueTableRow(*node), false);
		}
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
	return m_keyValues.find(id) != m_keyValues.end();
}

void IRecordSetObject::SetKeyValue(const meta_identifier_t& id, const CValue& cValue)
{
	IMetaAttributeObject* metaAttribute =
		m_metaObject->FindGenericAttribute(id);
	wxASSERT(metaAttribute);
	m_keyValues.insert_or_assign(
		id, metaAttribute != NULL ? metaAttribute->AdjustValue(cValue) : cValue
	);
}

CValue IRecordSetObject::GetKeyValue(const meta_identifier_t& id) const
{
	return m_keyValues.at(id);
}

void IRecordSetObject::EraseKeyValue(const meta_identifier_t& id)
{
	m_keyValues.erase(id);
}

IRecordSetObject* IRecordSetObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordSetObjectValue(this);
}

///////////////////////////////////////////////////////////////////////////////////

IRecordSetObject::IRecordSetObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey) : IValueTable(),
m_methodHelper(new CMethodHelper()),
m_metaObject(metaObject), m_keyValues(uniqueKey.IsOk() ? uniqueKey : metaObject), m_objModified(false), m_selected(false)
{
	m_dataColumnCollection = new CRecordSetRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();
}

IRecordSetObject::IRecordSetObject(const IRecordSetObject& source) : IValueTable(),
m_methodHelper(new CMethodHelper()),
m_metaObject(source.m_metaObject), m_keyValues(source.m_keyValues), m_objModified(true), m_selected(false)
{
	m_dataColumnCollection = new CRecordSetRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();

	for (long row = 0; row < source.GetRowCount(); row++) {
		wxValueTableRow* node = source.GetViewData<wxValueTableRow>(source.GetItem(row));
		wxASSERT(node);
		IValueTable::Append(new wxValueTableRow(*node), false);
	}
}

IRecordSetObject::~IRecordSetObject()
{
	wxDELETE(m_methodHelper);

	if (m_dataColumnCollection != NULL) {
		m_dataColumnCollection->DecrRef();
	}

	if (m_recordSetKeyValue != NULL) {
		m_recordSetKeyValue->DecrRef();
	}
}

bool IRecordSetObject::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		CTranslateError::Error("Array index out of bounds");
		return false;
	}
	pvarValue = new CRecordSetRegisterReturnLine(this, GetItem(index));
	return true;
}

CLASS_ID IRecordSetObject::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enRecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
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

#include "core/compiler/valueTable.h"

bool IRecordSetObject::LoadDataFromTable(IValueTable* srcTable)
{
	IValueModelColumnCollection* colData = srcTable->GetColumnCollection();

	if (colData == NULL)
		return false;
	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_dataColumnCollection->GetColumnByName(colInfo->GetColumnName()) != NULL) {
			columnName.push_back(colInfo->GetColumnName());
		}
	}
	unsigned int rowCount = srcTable->GetRowCount();
	for (unsigned int row = 0; row < rowCount; row++) {
		const wxDataViewItem& srcItem = srcTable->GetItem(row);
		const wxDataViewItem& dstItem = GetItem(AppendRow());
		for (auto colName : columnName) {
			CValue cRetValue;
			if (srcTable->GetValueByMetaID(srcItem, srcTable->GetColumnIDByName(colName), cRetValue)) {
				const meta_identifier_t& id = GetColumnIDByName(colName);
				if (id != wxNOT_FOUND) SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return true;
}

IValueTable* IRecordSetObject::SaveDataToTable() const
{
	CValueTable* valueTable = new CValueTable;
	IValueModelColumnCollection* colData = valueTable->GetColumnCollection();
	for (unsigned int idx = 0; idx < m_dataColumnCollection->GetColumnCount() - 1; idx++) {
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = m_dataColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		IValueModelColumnCollection::IValueModelColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnType(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}
	valueTable->PrepareNames();
	for (long row = 0; row < GetRowCount(); row++) {
		const wxDataViewItem& srcItem = GetItem(row);
		const wxDataViewItem& dstItem = valueTable->GetItem(valueTable->AppendRow());
		for (unsigned int col = 0; col < colData->GetColumnCount(); col++) {
			CValue cRetValue;
			IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colData->GetColumnInfo(col);
			wxASSERT(colInfo);
			if (GetValueByMetaID(srcItem, colInfo->GetColumnID(), cRetValue)) {
				const meta_identifier_t& id = GetColumnIDByName(colInfo->GetColumnName());
				if (id != wxNOT_FOUND) valueTable->SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return valueTable;
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

bool IRecordSetObject::SetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, const CValue& varMetaVal)
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
	if (node != NULL) {
		IMetaAttributeObject* metaAttribute = m_metaObject->FindGenericAttribute(id);
		wxASSERT(metaAttribute);
		return node->SetValue(
			id, metaAttribute->AdjustValue(varMetaVal), true
		);
	}
	return false;
}

bool IRecordSetObject::GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	IMetaAttributeObject* metaAttribute = m_metaObject->FindProp(id);
	wxASSERT(metaAttribute);
	if (appData->DesignerMode()) {
		pvarMetaVal = metaAttribute->CreateValue();
		return true;
	}
	wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
	if (node == NULL)
		return false;
	return node->GetValue(id, pvarMetaVal);
}

//////////////////////////////////////////////////////////////////////
//					CRecordSetRegisterColumnCollection				//
//////////////////////////////////////////////////////////////////////



wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterColumnCollection, IValueTable::IValueModelColumnCollection);

IRecordSetObject::CRecordSetRegisterColumnCollection::CRecordSetRegisterColumnCollection() : IValueModelColumnCollection(), m_methodHelper(NULL), m_ownerTable(NULL)
{
}

IRecordSetObject::CRecordSetRegisterColumnCollection::CRecordSetRegisterColumnCollection(IRecordSetObject* ownerTable) : IValueModelColumnCollection(), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable)
{
	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	wxASSERT(metaObject);

	for (auto attributes : metaObject->GetGenericAttributes()) {
		CValueRecordSetRegisterColumnInfo* columnInfo = new CValueRecordSetRegisterColumnInfo(attributes);
		m_columnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

IRecordSetObject::CRecordSetRegisterColumnCollection::~CRecordSetRegisterColumnCollection()
{
	for (auto& colInfo : m_columnInfo) {
		CValueRecordSetRegisterColumnInfo* columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methodHelper);
}

bool IRecordSetObject::CRecordSetRegisterColumnCollection::SetAt(const CValue& varKeyValue, const CValue& varValue)//индекс массива должен начинаться с 0
{
	return false;
}

bool IRecordSetObject::CRecordSetRegisterColumnCollection::GetAt(const CValue& varKeyValue, CValue& pvarValue) //индекс массива должен начинаться с 0
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode())) {
		CTranslateError::Error("Index goes beyond array");
		return false;
	}

	auto itFounded = m_columnInfo.begin();
	std::advance(itFounded, index);
	pvarValue = itFounded->second;
	return true;
}

//////////////////////////////////////////////////////////////////////
//					CValueRecordSetRegisterColumnInfo               //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, IValueTable::IValueModelColumnCollection::IValueModelColumnInfo);

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo() :
	IValueModelColumnInfo(), m_metaAttribute(NULL)
{
}

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo(IMetaAttributeObject* metaAttribute) :
	IValueModelColumnInfo(), m_metaAttribute(metaAttribute)
{
}

IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::~CValueRecordSetRegisterColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//					 CRecordSetRegisterReturnLine					//
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetRegisterReturnLine, IValueTable::IValueModelReturnLine);

IRecordSetObject::CRecordSetRegisterReturnLine::CRecordSetRegisterReturnLine(IRecordSetObject* ownerTable, const wxDataViewItem& line)
	: IValueModelReturnLine(line), m_ownerTable(ownerTable), m_methodHelper(new CMethodHelper())
{
}

IRecordSetObject::CRecordSetRegisterReturnLine::~CRecordSetRegisterReturnLine()
{
	wxDELETE(m_methodHelper);
}

void IRecordSetObject::CRecordSetRegisterReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	IMetaObjectWrapperData* metaObject = m_ownerTable->GetMetaObject();
	for (auto attribute : metaObject->GetGenericAttributes()) {
		m_methodHelper->AppendProp(
			attribute->GetName(),
			attribute->GetMetaID()
		);
	}
}

//////////////////////////////////////////////////////////////////////
//				       CRecordSetRegisterKeyValue					//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyValue(IRecordSetObject* recordSet) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_recordSet(recordSet)
{
}

IRecordSetObject::CRecordSetRegisterKeyValue::~CRecordSetRegisterKeyValue()
{
	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////
//						CRecordSetRegisterKeyDescriptionValue		//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::CRecordSetRegisterKeyDescriptionValue(IRecordSetObject* recordSet, const meta_identifier_t& id) : CValue(eValueTypes::TYPE_VALUE),
m_methodHelper(new CMethodHelper()), m_recordSet(recordSet), m_metaId(id)
{
}

IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::~CRecordSetRegisterKeyDescriptionValue()
{
	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////////////////////

long IRecordSetObject::AppendRow(unsigned int before)
{
	valueArray_t valueRow;

	IMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetadata* metaData = metaObject->GetMetadata();
	for (auto attribute : metaObject->GetGenericAttributes()) {
		valueRow.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
	}

	if (before > 0)
		return IValueTable::Insert(
			new wxValueTableRow(valueRow), before, !CTranslateError::IsSimpleMode());

	return IValueTable::Append(
		new wxValueTableRow(valueRow), !CTranslateError::IsSimpleMode()
	);
}

enum Func {
	enAdd = 0,
	enCount,
	enClear,
	enLoad,
	enUnload,
	enWrite,
	enModified,
	enRead,
	enSelected,
	enGetMetadata,
};

enum {
	enEmpty,
	enMetadata,
};

enum {
	enSet,
};

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

bool CRecordKeyObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CRecordKeyObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////

bool IRecordSetObject::CRecordSetRegisterReturnLine::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND)
		return SetValueByMetaID(id, varPropVal);
	return false;
}

bool IRecordSetObject::CRecordSetRegisterReturnLine::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		return GetValueByMetaID(id, pvarPropVal);
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////

bool IRecordSetObject::CRecordSetRegisterKeyValue::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool IRecordSetObject::CRecordSetRegisterKeyValue::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		pvarPropVal = new CRecordSetRegisterKeyDescriptionValue(m_recordSet, id);
		return true;
	}
	return false;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void CRecordKeyObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("isEmpty", "isEmpty()");
	m_methodHelper->AppendFunc("metadata", "metadata()");

	for (auto dimension : m_metaObject->GetGenericDimensions()) {
		if (dimension->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			dimension->GetName(),
			dimension->GetMetaID()
		);
	}

}

//////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetRegisterKeyValue::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	IMetaObjectRegisterData* metaRegister = m_recordSet->GetMetaObject();
	for (auto dimension : metaRegister->GetGenericDimensions()) {
		if (dimension->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			dimension->GetName(),
			dimension->GetMetaID()
		);
	}
}

//////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc(wxT("set"), 1, "set(value)");
	m_methodHelper->AppendProp(wxT("value"), m_metaId);
	m_methodHelper->AppendProp(wxT("use"));
}

enum Prop {
	eValue,
	eUse
};

bool IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	IMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);
	IMetaAttributeObject* metaAttribute = metaObject->FindGenericAttribute(m_metaId);
	wxASSERT(metaAttribute);

	switch (lPropNum) {
	case eValue:
		m_recordSet->SetKeyValue(m_metaId, varPropVal);
		return true;
	case eUse:
		if (varPropVal.GetBoolean())
			m_recordSet->SetKeyValue(m_metaId, metaAttribute->CreateValue());
		else
			m_recordSet->EraseKeyValue(m_metaId);
		return true;
	}

	return false;
}

bool IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	IMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);
	IMetaAttributeObject* metaAttribute = metaObject->FindGenericAttribute(m_metaId);
	wxASSERT(metaAttribute);

	switch (lPropNum) {
	case eValue:
		if (m_recordSet->FindKeyValue(m_metaId))
			pvarPropVal = m_recordSet->GetKeyValue(m_metaId);
		pvarPropVal = metaAttribute->CreateValue();
		return true;
	case eUse:
		pvarPropVal = m_recordSet->FindKeyValue(m_metaId);
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////

#include "core/metadata/metadata.h"

bool CRecordKeyObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enEmpty:
		pvarRetValue = IsEmpty();
		return true;
	case enMetadata:
		pvarRetValue = m_metaObject;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////

bool IRecordSetObject::CRecordSetRegisterKeyValue::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	return false;
}

//////////////////////////////////////////////////////////////

bool IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enSet:
		m_recordSet->SetKeyValue(m_metaId, paParams[0]);
		return true;
	}
	return false;
}


//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterColumnCollection, "recordSetRegisterColumn", TEXT2CLSID("VL_RSCL"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, "recordSetRegisterColumnInfo", TEXT2CLSID("VL_RSCI"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterReturnLine, "recordSetRegisterRow", TEXT2CLSID("VL_RSCR"));

SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterKeyValue, "recordSetRegisterKey", TEXT2CLSID("VL_RSCK"));
SO_VALUE_REGISTER(IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue, "recordSetRegisterKeyDescription", TEXT2CLSID("VL_RDVL"));