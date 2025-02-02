////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "object.h"
#include "backend/metaData.h"
#include "backend/srcExplorer.h"
#include "backend/systemManager/systemManager.h"
#include "backend/objCtor.h"


#include "backend/metaCollection/partial/reference/reference.h"

//***********************************************************************
//*								 metaData                               * 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectGenericData, IMetaObject);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRegisterData, IMetaObjectGenericData);

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordData, IMetaObjectGenericData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataExt, IMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataRef, IMetaObjectRecordData);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataEnumRef, IMetaObjectRecordDataRef);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataMutableRef, IMetaObjectRecordDataRef);
wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectRecordDataFolderMutableRef, IMetaObjectRecordDataMutableRef);

//***********************************************************************
//*							IMetaObjectGenericData				        *
//***********************************************************************

IMetaValueTypeCtor* IMetaObjectGenericData::GetTypeCtor(eCtorMetaType refType) const
{
	return m_metaData->GetTypeCtor(this, refType);
}

meta_identifier_t IMetaObjectGenericData::GetIdByGuid(const Guid& guid) const
{
	IMetaObject* metaObject = FindMetaObjectByID(guid);
	if (metaObject != nullptr)
		return metaObject->GetMetaID();
	return wxNOT_FOUND;
}

Guid IMetaObjectGenericData::GetGuidByID(const meta_identifier_t& id) const
{
	IMetaObject* metaObject = FindMetaObjectByID(id);
	if (metaObject != nullptr)
		return metaObject->GetGuid();
	return wxNullGuid;
}

//////////////////////////////////////////////////////////////////////////////

IMetaObject* IMetaObjectGenericData::FindMetaObjectByID(const meta_identifier_t& id) const
{
	return m_metaData->GetMetaObject(id,
		const_cast<IMetaObjectGenericData*>(this));
}

IMetaObject* IMetaObjectGenericData::FindMetaObjectByID(const Guid& guid) const
{
	return m_metaData->GetMetaObject(guid,
		const_cast<IMetaObjectGenericData*>(this));
}

IMetaObjectAttribute* IMetaObjectGenericData::FindGenericAttribute(const meta_identifier_t& id) const
{
	for (auto metaObject : GetGenericAttributes()) {
		if (id == metaObject->GetMetaID())
			return metaObject;
	}
	return nullptr;
}

IBackendValueForm* IMetaObjectGenericData::GetGenericForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	if (!formName.IsEmpty()) {
		for (auto metaForm : GetGenericForms()) {
			if (stringUtils::CompareString(formName, metaForm->GetName())) {
				return metaForm->GenerateFormAndRun(ownerControl,
					nullptr, formGuid
				);
			}
		}
	}

	if (!formName.IsEmpty())
		CSystemFunction::Raise(_("Сommon form not found '") + formName + "'");

	return nullptr;
}

IBackendValueForm* IMetaObjectGenericData::GetGenericForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;
	for (auto metaForm : GetGenericForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	if (defList == nullptr)
		return nullptr;

	return GetGenericForm(defList->GetName(),
		ownerControl, formGuid
	);
}

//***********************************************************************
//*                           IMetaObjectRecordData					    *
//***********************************************************************

#include "backend/fileSystem/fs.h"

bool IMetaObjectRecordData::OnLoadMetaObject(IMetaData* metaData)
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
	if (GetClassType() == g_metaExternalDataProcessorCLSID)
		registerExternalObject();
	else if (GetClassType() == g_metaExternalReportCLSID)
		registerExternalObject();
	else if (GetClassType() != g_metaEnumerationCLSID)
		registerObject();

	if (GetClassType() == g_metaExternalDataProcessorCLSID)
		registerExternalManager();
	else if (GetClassType() == g_metaExternalReportCLSID)
		registerExternalManager();
	else
		registerManager();

	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectRecordData::OnAfterCloseMetaObject()
{
	if (GetClassType() != g_metaEnumerationCLSID)
		unregisterObject();

	unregisterManager();
	return IMetaObject::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IBackendValueForm* IMetaObjectRecordData::GetObjectForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

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

std::vector<IMetaObjectAttribute*> IMetaObjectRecordData::GetObjectAttributes() const
{
	return CMetaVector<IMetaObjectAttribute>(this, g_metaAttributeCLSID);
}

std::vector<CMetaObjectForm*> IMetaObjectRecordData::GetObjectForms() const
{
	return CMetaVector<CMetaObjectForm>(this, g_metaFormCLSID);
}

std::vector<CMetaObjectGrid*> IMetaObjectRecordData::GetObjectTemplates() const
{
	return CMetaVector<CMetaObjectGrid>(this, g_metaTemplateCLSID);
}

std::vector<CMetaObjectTable*> IMetaObjectRecordData::GetObjectTables() const
{
	return CMetaVector<CMetaObjectTable>(this, g_metaTableCLSID);
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
			return nullptr;
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
			return nullptr;
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
	m_attributeReference = CMetaObjectAttributeDefault::CreateSpecialType("reference", _("Reference"), wxEmptyString, CValue::GetIDByVT(eValueTypes::TYPE_EMPTY));
	//set child/parent
	m_attributeReference->SetParent(this);
	AddChild(m_attributeReference);
}

IMetaObjectRecordDataRef::~IMetaObjectRecordDataRef()
{
	wxDELETE(m_attributeReference);
}

//***************************************************************************
//*                       Save & load metaData                              *
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

bool IMetaObjectRecordDataRef::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRecordData::OnCreateMetaObject(metaData))
		return false;

	return m_attributeReference->OnCreateMetaObject(metaData);
}

#include "backend/appData.h"
#include "databaseLayer/databaseLayer.h"

bool IMetaObjectRecordDataRef::OnLoadMetaObject(IMetaData* metaData)
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

	IMetaValueTypeCtor* typeCtor = m_metaData->GetTypeCtor(this, eCtorMetaType::eCtorMetaType_Reference);

	if (typeCtor != nullptr) {
		m_attributeReference->SetDefaultMetatype(typeCtor->GetClassType());
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

	if (m_attributeReference != nullptr) {
		m_attributeReference->SetDefaultMetatype(CValue::GetIDByVT(eValueTypes::TYPE_EMPTY));
	}

	unregisterReference();
	unregisteRefList();

	return IMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

//process choice 
bool IMetaObjectRecordDataRef::ProcessChoice(IBackendControlFrame* ownerValue, const meta_identifier_t& id, eSelectMode selMode)
{
	IBackendValueForm* selectForm = IMetaObjectRecordDataRef::GetSelectForm(id, ownerValue);
	if (selectForm == nullptr)
		return false;
	selectForm->ShowForm();
	return true;
}

CReferenceDataObject* IMetaObjectRecordDataRef::FindObjectValue(const Guid& guid)
{
	if (!guid.isValid())
		return nullptr;
	return CReferenceDataObject::Create(this, guid);
}

IBackendValueForm* IMetaObjectRecordDataRef::GetListForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetListForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

IBackendValueForm* IMetaObjectRecordDataRef::GetSelectForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

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

std::vector<IMetaObjectAttribute*> IMetaObjectRecordDataRef::GetGenericAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;
	for (auto& obj : GetDefaultAttributes()) {
		attributes.push_back(obj);
	}
	for (auto& obj : GetObjectAttributes()) {
		attributes.push_back(obj);
	}
	return attributes;
}

std::vector<CMetaObjectEnum*> IMetaObjectRecordDataRef::GetObjectEnums() const
{
	return CMetaVector<CMetaObjectEnum>(this, g_metaEnumCLSID);
}

//***********************************************************************
//*						IMetaObjectRecordDataEnumRef					*
//***********************************************************************

///////////////////////////////////////////////////////////////////////////////////////////////

IMetaObjectRecordDataEnumRef::IMetaObjectRecordDataEnumRef() : IMetaObjectRecordDataRef()
{
	m_attributeOrder = CMetaObjectAttributeDefault::CreateNumber(wxT("order"), _("Order"), wxEmptyString, 6, true);
	//set child/parent
	m_attributeOrder->SetParent(this);
	AddChild(m_attributeOrder);
}

IMetaObjectRecordDataEnumRef::~IMetaObjectRecordDataEnumRef()
{
	wxDELETE(m_attributeOrder);
}

//***************************************************************************
//*                       Save & load metaData                              *
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

bool IMetaObjectRecordDataEnumRef::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRecordDataRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeOrder->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataEnumRef::OnLoadMetaObject(IMetaData* metaData)
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
	if (list == nullptr)
		return false;
	m_data.clear();
	for (unsigned int idx = 0; idx < list->GetCount(); idx++) {
		m_data.insert(
			list->GetByIdx(idx)
		);
	}

	return true;
}

void IMetaObjectRecordDataMutableRef::genData_t::SaveToVariant(wxVariant& variant, IMetaData* metaData) const
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
	m_attributeDeletionMark = CMetaObjectAttributeDefault::CreateBoolean("deletionMark", _("DeletionMark"), wxEmptyString);
	//set child/parent
	m_attributeDeletionMark->SetParent(this);
	AddChild(m_attributeDeletionMark);
}

IMetaObjectRecordDataMutableRef::~IMetaObjectRecordDataMutableRef()
{
	wxDELETE(m_attributeDeletionMark);
}

//***************************************************************************
//*                       Save & load metaData                              *
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

bool IMetaObjectRecordDataMutableRef::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRecordDataRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeDeletionMark->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataMutableRef::OnLoadMetaObject(IMetaData* metaData)
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
		return nullptr;
	}

	return createdValue;
}

IRecordDataObjectRef* IMetaObjectRecordDataMutableRef::CreateObjectValue(const Guid& guid)
{
	IRecordDataObjectRef* createdValue = CreateObjectRefValue(guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordDataObjectRef* IMetaObjectRecordDataMutableRef::CreateObjectValue(IRecordDataObjectRef* objSrc, bool generate)
{
	if (objSrc == nullptr)
		return nullptr;
	IRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordDataObjectRef* IMetaObjectRecordDataMutableRef::CopyObjectValue(const Guid& srcGuid)
{
	IRecordDataObjectRef* createdValue = CreateObjectRefValue();
	if (createdValue && !createdValue->InitializeObject(srcGuid)) {
		wxDELETE(createdValue);
		return nullptr;
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
	m_attributeCode = CMetaObjectAttributeDefault::CreateString(wxT("code"), _("Code"), wxEmptyString, 8, true, eItemMode::eItemMode_Folder_Item);
	//set child/parent
	m_attributeCode->SetParent(this);
	AddChild(m_attributeCode);

	m_attributeDescription = CMetaObjectAttributeDefault::CreateString(wxT("description"), _("Description"), wxEmptyString, 150, true, eItemMode::eItemMode_Folder_Item);
	//set child/parent
	m_attributeDescription->SetParent(this);
	AddChild(m_attributeDescription);

	m_attributeParent = CMetaObjectAttributeDefault::CreateEmptyType(wxT("parent"), _("Parent"), wxEmptyString, false, eItemMode::eItemMode_Folder_Item, eSelectMode::eSelectMode_Folders);
	//set child/parent
	m_attributeParent->SetParent(this);
	AddChild(m_attributeParent);

	m_attributeIsFolder = CMetaObjectAttributeDefault::CreateBoolean(wxT("isFolder"), _("Is folder"), wxEmptyString, eItemMode::eItemMode_Folder_Item);
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
		return nullptr;
	}
	return createdValue;
}

IRecordDataObjectFolderRef* IMetaObjectRecordDataFolderMutableRef::CreateObjectValue(eObjectMode mode, const Guid& guid)
{
	IRecordDataObjectFolderRef* createdValue = CreateObjectRefValue(mode, guid);
	if (createdValue && !createdValue->InitializeObject()) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordDataObjectFolderRef* IMetaObjectRecordDataFolderMutableRef::CreateObjectValue(eObjectMode mode, IRecordDataObjectRef* objSrc, bool generate)
{
	IRecordDataObjectFolderRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject(objSrc, generate)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordDataObjectFolderRef* IMetaObjectRecordDataFolderMutableRef::CopyObjectValue(eObjectMode mode, const Guid& srcGuid)
{
	IRecordDataObjectFolderRef* createdValue = CreateObjectRefValue(mode);
	if (createdValue && !createdValue->InitializeObject(srcGuid)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

//***************************************************************************
//*                       Save & load metaData                              *
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

bool IMetaObjectRecordDataFolderMutableRef::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeCode->OnCreateMetaObject(metaData) &&
		m_attributeDescription->OnCreateMetaObject(metaData) &&
		m_attributeParent->OnCreateMetaObject(metaData) &&
		m_attributeIsFolder->OnCreateMetaObject(metaData);
}

bool IMetaObjectRecordDataFolderMutableRef::OnLoadMetaObject(IMetaData* metaData)
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

bool IMetaObjectRecordDataFolderMutableRef::ProcessChoice(IBackendControlFrame* ownerValue, const meta_identifier_t& id, eSelectMode selMode)
{
	IBackendValueForm* selectForm = nullptr;
	if (selMode == eSelectMode::eSelectMode_Items || selMode == eSelectMode::eSelectMode_FoldersAndItems) {
		selectForm = IMetaObjectRecordDataFolderMutableRef::GetSelectForm(id, ownerValue);
	}
	else if (selMode == eSelectMode::eSelectMode_Folders) {
		selectForm = IMetaObjectRecordDataFolderMutableRef::GetFolderSelectForm(id, ownerValue);
	}
	if (selectForm == nullptr)
		return false;
	selectForm->ShowForm();
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

IBackendValueForm* IMetaObjectRecordDataFolderMutableRef::GetFolderForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	for (auto metaForm : GetObjectForms()) {
		if (id == metaForm->GetMetaID()) {
			defList = metaForm; break;
		}
	}

	return GetFolderForm(defList ? defList->GetName() : wxEmptyString,
		ownerControl, formGuid
	);
}

IBackendValueForm* IMetaObjectRecordDataFolderMutableRef::GetFolderSelectForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

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

IMetaObjectRegisterData::IMetaObjectRegisterData() : IMetaObjectGenericData()
{
	//create default attributes
	m_attributeLineActive = CMetaObjectAttributeDefault::CreateBoolean(wxT("active"), _("Active"), wxEmptyString, false, true);
	m_attributePeriod = CMetaObjectAttributeDefault::CreateDate(wxT("period"), _("Period"), wxEmptyString, eDateFractions::eDateFractions_DateTime, true);
	m_attributeRecorder = CMetaObjectAttributeDefault::CreateEmptyType(wxT("recorder"), _("Recorder"), wxEmptyString);
	m_attributeLineNumber = CMetaObjectAttributeDefault::CreateNumber(wxT("lineNumber"), _("Line number"), wxEmptyString, 15, 0);

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
//*                       Save & load metaData                              *
//***************************************************************************

bool IMetaObjectRegisterData::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeLineActive->LoadMeta(dataReader);
	m_attributePeriod->LoadMeta(dataReader);
	m_attributeRecorder->LoadMeta(dataReader);
	m_attributeLineNumber->LoadMeta(dataReader);

	return IMetaObjectGenericData::LoadData(dataReader);
}

bool IMetaObjectRegisterData::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeLineActive->SaveMeta(dataWritter);
	m_attributePeriod->SaveMeta(dataWritter);
	m_attributeRecorder->SaveMeta(dataWritter);
	m_attributeLineNumber->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectGenericData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool IMetaObjectRegisterData::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObject::OnCreateMetaObject(metaData))
		return false;

	return m_attributeLineActive->OnCreateMetaObject(metaData) &&
		m_attributePeriod->OnCreateMetaObject(metaData) &&
		m_attributeRecorder->OnCreateMetaObject(metaData) &&
		m_attributeLineNumber->OnCreateMetaObject(metaData);
}

bool IMetaObjectRegisterData::OnLoadMetaObject(IMetaData* metaData)
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
	registerRecordManager();

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
	unregisterRecordManager();

	return IMetaObject::OnAfterCloseMetaObject();
}

IBackendValueForm* IMetaObjectRegisterData::GetListForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

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

std::vector<IMetaObjectAttribute*> IMetaObjectRegisterData::GetGenericAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;

	for (auto& obj : GetDefaultAttributes()) {
		attributes.push_back(obj);
	}

	for (auto& obj : GetObjectDimensions()) {
		attributes.push_back(obj);
	}

	for (auto& obj : GetObjectResources()) {
		attributes.push_back(obj);
	}

	for (auto& obj : GetObjectAttributes()) {
		attributes.push_back(obj);
	}

	return attributes;
}

#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/resource/metaResourceObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

std::vector<IMetaObjectAttribute*> IMetaObjectRegisterData::GetObjectDimensions() const
{
	return CMetaVector<IMetaObjectAttribute, CMetaObjectDimension>(this, g_metaDimensionCLSID);
}

std::vector<IMetaObjectAttribute*> IMetaObjectRegisterData::GetObjectResources() const
{
	return CMetaVector<IMetaObjectAttribute, CMetaObjectResource>(this, g_metaResourceCLSID);
}

std::vector<IMetaObjectAttribute*> IMetaObjectRegisterData::GetObjectAttributes() const
{
	return CMetaVector<IMetaObjectAttribute>(this, g_metaAttributeCLSID);
}

std::vector<CMetaObjectForm*> IMetaObjectRegisterData::GetObjectForms() const
{
	return CMetaVector<CMetaObjectForm>(this, g_metaFormCLSID);
}

std::vector<CMetaObjectGrid*> IMetaObjectRegisterData::GetObjectTemplates() const
{
	return CMetaVector<CMetaObjectGrid>(this, g_metaTemplateCLSID);
}

///////////////////////////////////////////////////////////////////////

IMetaObjectAttribute* IMetaObjectRegisterData::FindProp(const meta_identifier_t& id) const
{
	for (auto metaObject : m_listMetaObject) {
		if (
			metaObject->GetClassType() == g_metaDefaultAttributeCLSID ||
			metaObject->GetClassType() == g_metaAttributeCLSID ||
			metaObject->GetClassType() == g_metaDimensionCLSID ||
			metaObject->GetClassType() == g_metaResourceCLSID ||

			metaObject->GetMetaID() == id) {
			return metaObject->ConvertToType<IMetaObjectAttribute>();
		}
	}

	return nullptr;
}

IRecordSetObject* IMetaObjectRegisterData::CreateRecordSetObjectValue(bool needInitialize)
{
	IRecordSetObject* createdValue = CreateRecordSetObjectRegValue();
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(nullptr, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordSetObject* IMetaObjectRegisterData::CreateRecordSetObjectValue(const CUniquePairKey& uniqueKey, bool needInitialize)
{
	IRecordSetObject* createdValue = CreateRecordSetObjectRegValue(uniqueKey);
	if (!needInitialize)
		return createdValue;
	if (createdValue && !createdValue->InitializeObject(nullptr, false)) {
		wxDELETE(createdValue);
		return nullptr;
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
		return nullptr;
	}
	return createdValue;
}

IRecordManagerObject* IMetaObjectRegisterData::CreateRecordManagerObjectValue()
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(nullptr, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordManagerObject* IMetaObjectRegisterData::CreateRecordManagerObjectValue(const CUniquePairKey& uniqueKey)
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue(uniqueKey);
	if (createdValue && !createdValue->InitializeObject(nullptr, false)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordManagerObject* IMetaObjectRegisterData::CreateRecordManagerObjectValue(IRecordManagerObject* source)
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue();
	if (createdValue && !createdValue->InitializeObject(source, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

IRecordManagerObject* IMetaObjectRegisterData::CopyRecordManagerObjectValue(const CUniquePairKey& uniqueKey)
{
	IRecordManagerObject* createdValue = CreateRecordManagerObjectRegValue(uniqueKey);
	if (createdValue && !createdValue->InitializeObject(nullptr, true, true)) {
		wxDELETE(createdValue);
		return nullptr;
	}
	return createdValue;
}

//***********************************************************************
//*                        ISourceDataObject							*
//***********************************************************************

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

IBackendValueForm* IRecordDataObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;
	return IBackendValueForm::FindFormByUniqueKey(m_objGuid);
}

class_identifier_t IRecordDataObject::GetClassType() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaValueTypeCtor* clsFactory =
		metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordDataObject::GetClassName() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaValueTypeCtor* clsFactory =
		metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordDataObject::GetString() const
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaValueTypeCtor* clsFactory =
		metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Object);
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

	for (auto& obj : metaObject->GetGenericAttributes()) {
		srcHelper.AppendSource(obj);
	}

	for (auto& obj : metaObject->GetObjectTables()) {
		srcHelper.AppendSource(obj);
	}

	return srcHelper;
}

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

bool IRecordDataObject::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto& it = m_objectValues.find(id);
	if (it != m_objectValues.end()) {
		const CValue& cTabularSection = it->second;
		return cTabularSection.ConvertToValue(tableValue);
	};

	tableValue = nullptr;
	return false;
}

bool IRecordDataObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal)
{
	auto& it = m_objectValues.find(id);
	wxASSERT(it != m_objectValues.end());
	if (it != m_objectValues.end()) {
		IMetaObjectRecordData* metaObjectValue = GetMetaObject();
		wxASSERT(metaObjectValue);
		IMetaObjectAttribute* metaAttribute = wxDynamicCast(
			metaObjectValue->FindMetaObjectByID(id), IMetaObjectAttribute
		);
		wxASSERT(metaAttribute);
		it->second = metaAttribute->AdjustValue(varMetaVal);
		return true;
	}
	return false;
}

bool IRecordDataObject::GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	auto& it = m_objectValues.find(id);
	wxASSERT(it != m_objectValues.end());
	if (it != m_objectValues.end()) {
		pvarMetaVal = it->second;
		return true;
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

IValueTable* IRecordDataObject::GetTableByMetaID(const meta_identifier_t& id) const
{
	const CValue& cTable = GetValueByMetaID(id); IValueTable* retTable = nullptr;
	if (cTable.ConvertToValue(retTable))
		return retTable;
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

#define thisObject wxT("thisObject")

void IRecordDataObject::PrepareEmptyObject()
{
	IMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);

	m_objectValues.clear();

	//attrbutes can refValue 
	for (auto& obj : metaObject->GetGenericAttributes()) {
		m_objectValues.insert_or_assign(obj->GetMetaID(), obj->CreateValue());
	}

	// table is collection values 
	for (auto& obj : metaObject->GetObjectTables()) {
		m_objectValues.insert_or_assign(obj->GetMetaID(), metaObject->GetMetaData()->CreateObjectValueRef<CTabularSectionDataObject>(this, obj));
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
	for (auto& obj : metaObject->GetGenericAttributes()) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			obj->GetMetaID(),
			eProperty
		);
	}

	//fill custom tables 
	for (auto& obj : metaObject->GetObjectTables()) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			true,
			false,
			obj->GetMetaID(),
			eTable
		);
	}

	if (m_procUnit != nullptr) {
		CByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
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
}

bool IRecordDataObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
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
		if (m_procUnit != nullptr) {
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
			lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr
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
			IMetaData* metaData = m_metaObject->GetMetaData();
			if (!metaData->CloseConfiguration(forceCloseFlag)) {
				wxASSERT_MSG(false, "m_moduleManager->CloseConfiguration() == false");
			}
			wxDELETE(metaData);
		}
	}
}

bool IRecordDataObjectExt::InitializeObject()
{
	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		IMetaData* metaData = m_metaObject->GetMetaData();
		wxASSERT(metaData);
		IModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		if (!m_compileModule) {
			m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
			m_compileModule->SetParent(moduleManager->GetCompileModule());
			m_compileModule->AddContextVariable(thisObject, this);
		}

		if (!appData->DesignerMode()) {
			try {
				m_compileModule->Compile();
			}
			catch (const CBackendException* err)
			{
				if (!appData->DesignerMode()) {
					CSystemFunction::Raise(err->what());
				}

				return false;
			};

			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(moduleManager->GetProcUnit());
		}
	}

	PrepareEmptyObject();

	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		if (!appData->DesignerMode()) {
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
	}

	PrepareNames();
	//is Ok
	return true;
}

bool IRecordDataObjectExt::InitializeObject(IRecordDataObjectExt* source)
{
	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		IMetaData* metaData = m_metaObject->GetMetaData();
		wxASSERT(metaData);
		IModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		if (m_compileModule == nullptr) {
			m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
			m_compileModule->SetParent(moduleManager->GetCompileModule());
			m_compileModule->AddContextVariable(thisObject, this);
		}

		if (!appData->DesignerMode()) {
			try {
				m_compileModule->Compile();
			}
			catch (const CBackendException* err) {
				if (!appData->DesignerMode()) {
					CSystemFunction::Raise(err->what());
				}

				return false;
			};

			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(moduleManager->GetProcUnit());
		}
	}

	PrepareEmptyObject();

	if (m_metaObject->GetObjectMode() == METAOBJECT_NORMAL) {
		if (!appData->DesignerMode()) {
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
	}

	PrepareNames();
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
	m_reference_impl(nullptr), m_codeGenerator(nullptr),
	m_objModified(false)
{
	if (m_metaObject != nullptr) {
		IMetaObjectAttribute* attributeCode = m_metaObject->GetAttributeForCode();
		if (attributeCode != nullptr) {
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
	m_reference_impl(nullptr), m_codeGenerator(nullptr),
	m_objModified(false)
{
	if (m_metaObject != nullptr) {
		IMetaObjectAttribute* attributeCode = m_metaObject->GetAttributeForCode();
		if (attributeCode != nullptr) {
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

bool IRecordDataObjectRef::InitializeObject(const Guid& copyGuid)
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (m_compileModule == nullptr) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}
	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const CBackendException* err) {
			if (!appData->DesignerMode()) {
				CSystemFunction::Raise(err->what());
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
				IMetaObjectAttribute* codeAttribute = m_metaObject->GetAttributeForCode();
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
		wxASSERT(m_procUnit == nullptr);
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode);
		if (m_newObject) {
			succes = Filling();
		}
	}

	PrepareNames();
	//is Ok
	return succes;
}

bool IRecordDataObjectRef::InitializeObject(IRecordDataObjectRef* source, bool generate)
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (m_compileModule == nullptr) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}
	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const CBackendException* err) {
			if (!appData->DesignerMode()) {
				CSystemFunction::Raise(err->what());
			}

			return false;
		};
	}

	CReferenceDataObject* reference = source ?
		source->GetReference() : nullptr;

	if (reference != nullptr)
		reference->IncrRef();

	if (!generate && source != nullptr)
		PrepareEmptyObject(source);
	else
		PrepareEmptyObject();

	bool succes = true;
	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode);
		if (m_newObject && source != nullptr && !generate) {
			m_procUnit->CallAsProc("OnCopy", source->GetValue());
		}
		else if (m_newObject && source == nullptr) {
			succes = Filling();
		}
		else if (generate) {
			succes = Filling(reference->GetValue());
		}
	}
	if (reference != nullptr)
		reference->DecrRef();

	PrepareNames();
	//is Ok
	return succes;
}

class_identifier_t IRecordDataObjectRef::GetClassType() const
{
	return IRecordDataObject::GetClassType();
}

wxString IRecordDataObjectRef::GetClassName() const
{
	return IRecordDataObject::GetClassName();
}

wxString IRecordDataObjectRef::GetString() const
{
	return m_metaObject->GetDataPresentation(this);
}

CSourceExplorer IRecordDataObjectRef::GetSourceExplorer() const
{
	CSourceExplorer srcHelper(
		m_metaObject, GetClassType(),
		false
	);

	IMetaObjectAttribute* metaAttribute = m_metaObject->GetAttributeForCode();

	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (!m_metaObject->IsDataReference(obj->GetMetaID())) {
			srcHelper.AppendSource(obj, obj != metaAttribute);
		}
	}

	for (auto& obj : m_metaObject->GetObjectTables()) {
		srcHelper.AppendSource(obj);
	}

	return srcHelper;
}

bool IRecordDataObjectRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto& it = m_objectValues.find(id);
	if (it != m_objectValues.end()) {
		const CValue& cTabularSection = it->second;
		return cTabularSection.ConvertToValue(tableValue);
	};
	tableValue = nullptr;
	return false;
}

void IRecordDataObjectRef::Modify(bool mod)
{
	IBackendValueForm* const foundedForm = IBackendValueForm::FindFormByUniqueKey(m_objGuid);
	if (foundedForm != nullptr)
		foundedForm->Modify(mod);
	m_objModified = mod;
}

bool IRecordDataObjectRef::Generate()
{
	if (m_newObject)
		return false;

	IBackendValueForm* const foundedForm = IBackendValueForm::FindFormByUniqueKey(m_objGuid);
	if (foundedForm != nullptr) {
		return foundedForm->GenerateForm(this);
	}

	return false;
}

bool IRecordDataObjectRef::Filling(CValue& cValue) const
{
	CValue standartProcessing = true;
	if (m_procUnit != nullptr) {
		m_procUnit->CallAsProc("Filling", cValue, standartProcessing);
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
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		m_objectValues.insert_or_assign(obj->GetMetaID(), obj->CreateValue());
	}
	// table is collection values 
	for (auto& obj : m_metaObject->GetObjectTables()) {
		m_objectValues.insert_or_assign(obj->GetMetaID(), m_metaObject->GetMetaData()->CreateAndConvertObjectValueRef<CTabularSectionDataObjectRef>(this, obj));
	}
	m_objModified = true;
}

void IRecordDataObjectRef::PrepareEmptyObject(const IRecordDataObjectRef* source)
{
	m_objectValues.clear();
	IMetaObjectAttribute* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_objectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (obj != codeAttribute) {
			source->GetValueByMetaID(obj->GetMetaID(), m_objectValues[obj->GetMetaID()]);
		}
	}
	// table is collection values 
	for (auto& obj : m_metaObject->GetObjectTables()) {
		CTabularSectionDataObjectRef* tableSection = m_metaObject->GetMetaData()->CreateAndConvertObjectValueRef<CTabularSectionDataObjectRef>(this, obj);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(obj->GetMetaID())))
			m_objectValues.insert_or_assign(obj->GetMetaID(), tableSection);
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
		m_metaObject, GetClassType(),
		false
	);
	IMetaObjectAttribute* metaAttribute = m_metaObject->GetAttributeForCode();
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		eItemMode attrUse = obj->GetItemMode();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (attrUse == eItemMode::eItemMode_Item
				|| attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(obj->GetMetaID())) {
					srcHelper.AppendSource(obj, obj != metaAttribute);
				}
			}
		}
		else {
			if (attrUse == eItemMode::eItemMode_Folder ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(obj->GetMetaID())) {
					srcHelper.AppendSource(obj, obj != metaAttribute);
				}
			}
		}
	}

	for (auto& obj : m_metaObject->GetObjectTables()) {
		eItemMode tableUse = obj->GetTableUse();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (tableUse == eItemMode::eItemMode_Item
				|| tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(obj);
			}
		}
		else {
			if (tableUse == eItemMode::eItemMode_Folder ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				srcHelper.AppendSource(obj);
			}
		}
	}

	return srcHelper;
}

bool IRecordDataObjectFolderRef::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	auto& it = m_objectValues.find(id);
	if (it != m_objectValues.end()) {
		const CValue& cTabularSection = it->second;
		return cTabularSection.ConvertToValue(tableValue);
	};

	tableValue = nullptr;
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
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		eItemMode attrUse = obj->GetItemMode();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (attrUse == eItemMode::eItemMode_Item ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(obj->GetMetaID(), obj->CreateValue());
			}
			else {
				m_objectValues.insert_or_assign(obj->GetMetaID(), eValueTypes::TYPE_NULL);
			}
		}
		else {
			if (attrUse == eItemMode::eItemMode_Folder ||
				attrUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(obj->GetMetaID(), obj->CreateValue());
			}
			else {
				m_objectValues.insert_or_assign(obj->GetMetaID(), eValueTypes::TYPE_NULL);
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
	for (auto& obj : m_metaObject->GetObjectTables()) {
		eItemMode tableUse = obj->GetTableUse();
		if (m_objMode == eObjectMode::OBJECT_ITEM) {
			if (tableUse == eItemMode::eItemMode_Item ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(obj->GetMetaID(), m_metaObject->GetMetaData()->CreateAndConvertObjectValueRef<CTabularSectionDataObjectRef>(this, obj));
			}
			else {
				m_objectValues.insert_or_assign(obj->GetMetaID(), eValueTypes::TYPE_NULL);
			}
		}
		else {
			if (tableUse == eItemMode::eItemMode_Folder ||
				tableUse == eItemMode::eItemMode_Folder_Item) {
				m_objectValues.insert_or_assign(obj->GetMetaID(), m_metaObject->GetMetaData()->CreateAndConvertObjectValueRef<CTabularSectionDataObjectRef>(this, obj));
			}
			else {
				m_objectValues.insert_or_assign(obj->GetMetaID(), eValueTypes::TYPE_NULL);
			}
		}
	}
	m_objModified = true;
}

void IRecordDataObjectFolderRef::PrepareEmptyObject(const IRecordDataObjectRef* source)
{
	m_objectValues.clear();
	IMetaObjectAttribute* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_objectValues[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		CMetaObjectAttribute* metaAttr = nullptr; eItemMode attrUse = eItemMode::eItemMode_Folder_Item;
		if (obj->ConvertToValue(metaAttr)) {
			attrUse = metaAttr->GetItemMode();
		}
		if (obj != codeAttribute && !m_metaObject->IsDataReference(obj->GetMetaID())) {
			source->GetValueByMetaID(obj->GetMetaID(), m_objectValues[obj->GetMetaID()]);
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
	for (auto& obj : m_metaObject->GetObjectTables()) {
		CMetaObjectTable* metaTable = nullptr; eItemMode tableUse = eItemMode::eItemMode_Folder_Item;
		if (obj->ConvertToValue(metaTable))
			tableUse = metaTable->GetTableUse();
		CTabularSectionDataObjectRef* tableSection = m_metaObject->GetMetaData()->CreateAndConvertObjectValueRef <CTabularSectionDataObjectRef>(this, obj);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(obj->GetMetaID())))
			m_objectValues.insert_or_assign(obj->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
	}
	m_objModified = true;
}

//***********************************************************************
//*						     metaData									* 
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IRecordSetObject, IValueTable);
wxIMPLEMENT_ABSTRACT_CLASS(IRecordManagerObject, CValue);

wxIMPLEMENT_ABSTRACT_CLASS(CRecordKeyObject, CValue);

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetObjectRegisterKeyValue, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue, CValue);

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

class_identifier_t CRecordKeyObject::GetClassType() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CRecordKeyObject::GetClassName() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CRecordKeyObject::GetString() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordKey);
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
	if (!m_recordSet->InitializeObject(source ? source->GetRecordSet() : nullptr, newRecord))
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

	PrepareNames();
	//is Ok
	return true;
}

IRecordManagerObject* IRecordManagerObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordManagerObjectValue(this);
}

IRecordManagerObject::IRecordManagerObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(metaObject), m_methodHelper(new CMethodHelper()), m_recordSet(m_metaObject->CreateRecordSetObjectValue(uniqueKey, false)), m_recordLine(nullptr),
m_objGuid(uniqueKey)
{
}

IRecordManagerObject::IRecordManagerObject(const IRecordManagerObject& source) : CValue(eValueTypes::TYPE_VALUE),
m_metaObject(source.m_metaObject), m_methodHelper(new CMethodHelper()), m_recordSet(m_metaObject->CreateRecordSetObjectValue(source.m_recordSet, false)), m_recordLine(nullptr),
m_objGuid(source.m_metaObject)
{
}

IRecordManagerObject::~IRecordManagerObject()
{
	wxDELETE(m_methodHelper);

	wxDELETE(m_recordSet);
	wxDELETE(m_recordLine);
}

IBackendValueForm* IRecordManagerObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;
	if (m_recordSet->m_selected)
		return IBackendValueForm::FindFormByUniqueKey(m_objGuid);
	return nullptr;
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

	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		srcHelper.AppendSource(obj);
	}

	return srcHelper;
}

bool IRecordManagerObject::GetModel(IValueModel*& tableValue, const meta_identifier_t& id)
{
	return false;
}

void IRecordManagerObject::Modify(bool mod)
{
	IBackendValueForm* const foundedForm = IBackendValueForm::FindFormByUniqueKey(m_objGuid);
	if (foundedForm != nullptr)
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

class_identifier_t IRecordManagerObject::GetClassType() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordManagerObject::GetClassName() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordManagerObject::GetString() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////

void IRecordManagerObject::PrepareEmptyObject(const IRecordManagerObject* source)
{
	if (source == nullptr) {
		m_recordLine = new IRecordSetObject::CRecordSetObjectRegisterReturnLine(
			m_recordSet,
			m_recordSet->GetItem(
				m_recordSet->AppendRow()
			)
		);
	}
	else if (source != nullptr) {
		m_recordLine = m_recordSet->GetRowAt(
			m_recordSet->GetItem(0)
		);
	}

	m_recordSet->Modify(true);
}

//////////////////////////////////////////////////////////////////////
//						  IRecordSetObject							//
//////////////////////////////////////////////////////////////////////

void IRecordSetObject::CreateEmptyKey()
{
	m_keyValues.clear();
	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		m_keyValues.insert_or_assign(
			obj->GetMetaID(), obj->CreateValue()
		);
	}
}

bool IRecordSetObject::InitializeObject(const IRecordSetObject* source, bool newRecord)
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (m_compileModule == nullptr) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
		m_compileModule->AddContextVariable(thisObject, this);
	}
	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const CBackendException* err) {
			if (!appData->DesignerMode()) {
				CSystemFunction::Raise(err->what());
			}
			return false;
		};
	}
	if (source != nullptr) {
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
	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		m_procUnit->Execute(m_compileModule->m_cByteCode);
	}

	PrepareNames();
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
	IMetaObjectAttribute* metaAttribute =
		m_metaObject->FindGenericAttribute(id);
	wxASSERT(metaAttribute);
	m_keyValues.insert_or_assign(
		id, metaAttribute != nullptr ? metaAttribute->AdjustValue(cValue) : cValue
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
	m_dataColumnCollection = new CRecordSetObjectRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetObjectRegisterKeyValue(this);
	m_recordSetKeyValue->IncrRef();
}

IRecordSetObject::IRecordSetObject(const IRecordSetObject& source) : IValueTable(),
m_methodHelper(new CMethodHelper()),
m_metaObject(source.m_metaObject), m_keyValues(source.m_keyValues), m_objModified(true), m_selected(false)
{
	m_dataColumnCollection = new CRecordSetObjectRegisterColumnCollection(this);
	m_dataColumnCollection->IncrRef();
	m_recordSetKeyValue = new CRecordSetObjectRegisterKeyValue(this);
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

	if (m_dataColumnCollection != nullptr) {
		m_dataColumnCollection->DecrRef();
	}

	if (m_recordSetKeyValue != nullptr) {
		m_recordSetKeyValue->DecrRef();
	}
}

bool IRecordSetObject::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		CBackendException::Error("Array index out of bounds");
		return false;
	}
	pvarValue = new CRecordSetObjectRegisterReturnLine(this, GetItem(index));
	return true;
}

class_identifier_t IRecordSetObject::GetClassType() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString IRecordSetObject::GetClassName() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IRecordSetObject::GetString() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

#include "backend/compiler/value/valueTable.h"

bool IRecordSetObject::LoadDataFromTable(IValueTable* srcTable)
{
	IValueModelColumnCollection* colData = srcTable->GetColumnCollection();

	if (colData == nullptr)
		return false;
	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_dataColumnCollection->GetColumnByName(colInfo->GetColumnName()) != nullptr) {
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
	CValueTable* valueTable = CValue::CreateAndConvertObjectRef<CValueTable>();

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
	if (node != nullptr) {
		IMetaObjectAttribute* metaAttribute = m_metaObject->FindGenericAttribute(id);
		wxASSERT(metaAttribute);
		return node->SetValue(
			id, metaAttribute->AdjustValue(varMetaVal), true
		);
	}
	return false;
}

bool IRecordSetObject::GetValueByMetaID(const wxDataViewItem& item, const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	IMetaObjectAttribute* metaAttribute = m_metaObject->FindProp(id);
	wxASSERT(metaAttribute);
	if (appData->DesignerMode()) {
		pvarMetaVal = metaAttribute->CreateValue();
		return true;
	}
	wxValueTableRow* node = GetViewData<wxValueTableRow>(item);
	if (node == nullptr)
		return false;
	return node->GetValue(id, pvarMetaVal);
}

//////////////////////////////////////////////////////////////////////
//					CRecordSetObjectRegisterColumnCollection				//
//////////////////////////////////////////////////////////////////////



wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetObjectRegisterColumnCollection, IValueTable::IValueModelColumnCollection);

IRecordSetObject::CRecordSetObjectRegisterColumnCollection::CRecordSetObjectRegisterColumnCollection() : IValueModelColumnCollection(), m_methodHelper(nullptr), m_ownerTable(nullptr)
{
}

IRecordSetObject::CRecordSetObjectRegisterColumnCollection::CRecordSetObjectRegisterColumnCollection(IRecordSetObject* ownerTable) : IValueModelColumnCollection(), m_methodHelper(new CMethodHelper()), m_ownerTable(ownerTable)
{
	IMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	wxASSERT(metaObject);

	for (auto& obj : metaObject->GetGenericAttributes()) {
		CValueRecordSetRegisterColumnInfo* columnInfo = CValue::CreateAndConvertObjectValueRef<CValueRecordSetRegisterColumnInfo>(obj);
		m_columnInfo.insert_or_assign(obj->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

IRecordSetObject::CRecordSetObjectRegisterColumnCollection::~CRecordSetObjectRegisterColumnCollection()
{
	for (auto& colInfo : m_columnInfo) {
		CValueRecordSetRegisterColumnInfo* columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methodHelper);
}

bool IRecordSetObject::CRecordSetObjectRegisterColumnCollection::SetAt(const CValue& varKeyValue, const CValue& varValue)//индекс массива должен начинаться с 0
{
	return false;
}

bool IRecordSetObject::CRecordSetObjectRegisterColumnCollection::GetAt(const CValue& varKeyValue, CValue& pvarValue) //индекс массива должен начинаться с 0
{
	unsigned int index = varKeyValue.GetUInteger();
	if ((index < 0 || index >= m_columnInfo.size() && !appData->DesignerMode())) {
		CBackendException::Error("Index goes beyond array");
		return false;
	}

	auto it = m_columnInfo.begin();
	std::advance(it, index);
	pvarValue = it->second;
	return true;
}

//////////////////////////////////////////////////////////////////////
//					CValueRecordSetRegisterColumnInfo               //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetObjectRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, IValueTable::IValueModelColumnCollection::IValueModelColumnInfo);

IRecordSetObject::CRecordSetObjectRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo() :
	IValueModelColumnInfo(), m_metaAttribute(nullptr)
{
}

IRecordSetObject::CRecordSetObjectRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::CValueRecordSetRegisterColumnInfo(IMetaObjectAttribute* metaAttribute) :
	IValueModelColumnInfo(), m_metaAttribute(metaAttribute)
{
}

IRecordSetObject::CRecordSetObjectRegisterColumnCollection::CValueRecordSetRegisterColumnInfo::~CValueRecordSetRegisterColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//					 CRecordSetObjectRegisterReturnLine					//
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IRecordSetObject::CRecordSetObjectRegisterReturnLine, IValueTable::IValueModelReturnLine);

IRecordSetObject::CRecordSetObjectRegisterReturnLine::CRecordSetObjectRegisterReturnLine(IRecordSetObject* ownerTable, const wxDataViewItem& line)
	: IValueModelReturnLine(line), m_ownerTable(ownerTable), m_methodHelper(new CMethodHelper())
{
}

IRecordSetObject::CRecordSetObjectRegisterReturnLine::~CRecordSetObjectRegisterReturnLine()
{
	wxDELETE(m_methodHelper);
}

void IRecordSetObject::CRecordSetObjectRegisterReturnLine::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	IMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	for (auto& obj : metaObject->GetGenericAttributes()) {
		m_methodHelper->AppendProp(
			obj->GetName(),
			obj->GetMetaID()
		);
	}
}

//////////////////////////////////////////////////////////////////////
//				       CRecordSetObjectRegisterKeyValue					//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyValue(IRecordSetObject* recordSet) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_recordSet(recordSet)
{
}

IRecordSetObject::CRecordSetObjectRegisterKeyValue::~CRecordSetObjectRegisterKeyValue()
{
	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////
//						CRecordSetObjectRegisterKeyDescriptionValue		//
//////////////////////////////////////////////////////////////////////

IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue::CRecordSetObjectRegisterKeyDescriptionValue(IRecordSetObject* recordSet, const meta_identifier_t& id) : CValue(eValueTypes::TYPE_VALUE),
m_methodHelper(new CMethodHelper()), m_recordSet(recordSet), m_metaId(id)
{
}

IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue::~CRecordSetObjectRegisterKeyDescriptionValue()
{
	wxDELETE(m_methodHelper);
}

//////////////////////////////////////////////////////////////////////////////////////

long IRecordSetObject::AppendRow(unsigned int before)
{
	wxValueTableRow* rowData = new wxValueTableRow();

	IMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaData* metaData = metaObject->GetMetaData();
	for (auto& obj : metaObject->GetGenericAttributes()) {
		rowData->AppendTableValue(obj->GetMetaID(), obj->CreateValue());
	}

	if (before > 0)
		return IValueTable::Insert(rowData, before, !CBackendException::IsEvalMode());

	return IValueTable::Append(rowData, !CBackendException::IsEvalMode());
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

bool IRecordSetObject::CRecordSetObjectRegisterReturnLine::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND)
		return SetValueByMetaID(id, varPropVal);
	return false;
}

bool IRecordSetObject::CRecordSetObjectRegisterReturnLine::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		return GetValueByMetaID(id, pvarPropVal);
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////

bool IRecordSetObject::CRecordSetObjectRegisterKeyValue::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool IRecordSetObject::CRecordSetObjectRegisterKeyValue::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		pvarPropVal = new CRecordSetObjectRegisterKeyDescriptionValue(m_recordSet, id);
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
	m_methodHelper->AppendFunc("metaData", "metaData()");

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			obj->GetMetaID()
		);
	}

}

//////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetObjectRegisterKeyValue::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	IMetaObjectRegisterData* metaRegister = m_recordSet->GetMetaObject();
	for (auto& obj : metaRegister->GetGenericDimensions()) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			obj->GetMetaID()
		);
	}
}

//////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue::PrepareNames() const
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

bool IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	IMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);
	IMetaObjectAttribute* metaAttribute = metaObject->FindGenericAttribute(m_metaId);
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

bool IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	IMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);
	IMetaObjectAttribute* metaAttribute = metaObject->FindGenericAttribute(m_metaId);
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

bool IRecordSetObject::CRecordSetObjectRegisterKeyValue::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	return false;
}

//////////////////////////////////////////////////////////////

bool IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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

SYSTEM_TYPE_REGISTER(IRecordSetObject::CRecordSetObjectRegisterColumnCollection, "recordSetRegisterColumn", string_to_clsid("VL_RSCL"));
SYSTEM_TYPE_REGISTER(IRecordSetObject::CRecordSetObjectRegisterColumnCollection::CValueRecordSetRegisterColumnInfo, "recordSetRegisterColumnInfo", string_to_clsid("VL_RSCI"));
SYSTEM_TYPE_REGISTER(IRecordSetObject::CRecordSetObjectRegisterReturnLine, "recordSetRegisterRow", string_to_clsid("VL_RSCR"));

SYSTEM_TYPE_REGISTER(IRecordSetObject::CRecordSetObjectRegisterKeyValue, "recordSetRegisterKey", string_to_clsid("VL_RSCK"));
SYSTEM_TYPE_REGISTER(IRecordSetObject::CRecordSetObjectRegisterKeyValue::CRecordSetObjectRegisterKeyDescriptionValue, "recordSetRegisterKeyDescription", string_to_clsid("VL_RDVL"));