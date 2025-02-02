////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metaData
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

//********************************************************************************************
//*										 metaData											 * 
//********************************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectCatalog, IMetaObjectRecordDataFolderMutableRef);

//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

CMetaObjectCatalog::CMetaObjectCatalog() : IMetaObjectRecordDataFolderMutableRef()
{
	m_attributeOwner = CMetaObjectAttributeDefault::CreateEmptyType(wxT("owner"), _("Owner"), wxEmptyString, true, eItemMode::eItemMode_Folder_Item);
	//set child/parent
	m_attributeOwner->SetParent(this);
	AddChild(m_attributeOwner);

	//create module
	m_moduleObject = new CMetaObjectModule(objectModule);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	//set default proc
	m_moduleObject->SetDefaultProcedure("beforeWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("beforeDelete", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onDelete", eContentHelper::eProcedureHelper, { "cancel" });

	m_moduleObject->SetDefaultProcedure("filling", eContentHelper::eProcedureHelper, { "source", "standartProcessing" });
	m_moduleObject->SetDefaultProcedure("onCopy", eContentHelper::eProcedureHelper, { "source" });

	m_moduleManager = new CMetaObjectManagerModule(managerModule);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectCatalog::~CMetaObjectCatalog()
{
	wxDELETE(m_attributeOwner);

	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaObjectForm* CMetaObjectCatalog::GetDefaultFormByID(const form_identifier_t& id)
{
	if (id == eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND) {
		for (auto& obj : GetObjectForms()) {
			if (m_propertyDefFormObject->GetValueAsInteger() == obj->GetMetaID()) {
				return obj;
			}
		}
	}
	else if (id == eFormList
		&& m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		for (auto& obj : GetObjectForms()) {
			if (m_propertyDefFormList->GetValueAsInteger() == obj->GetMetaID()) {
				return obj;
			}
		}
	}
	else if (id == eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() != wxNOT_FOUND) {
		for (auto& obj : GetObjectForms()) {
			if (m_propertyDefFormSelect->GetValueAsInteger() == obj->GetMetaID()) {
				return obj;
			}
		}
	}

	return nullptr;
}

ISourceDataObject* CMetaObjectCatalog::CreateObjectData(IMetaObjectForm* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject:
		return CreateObjectValue(eObjectMode::OBJECT_ITEM);
	case eFormGroup:
		return CreateObjectValue(eObjectMode::OBJECT_FOLDER);
	case eFormList:
		return m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), CTreeDataObjectFolderRef::LIST_ITEM_FOLDER);
	case eFormSelect:
		return m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), CTreeDataObjectFolderRef::LIST_ITEM_FOLDER, true);
	case eFormFolderSelect:
		return m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), CTreeDataObjectFolderRef::LIST_FOLDER, true);
	}

	return nullptr;
}

#include "backend/appData.h"

IRecordDataObjectFolderRef* CMetaObjectCatalog::CreateObjectRefValue(eObjectMode mode, const Guid& guid)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	CRecordDataObjectCatalog* pDataRef = nullptr;
	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return m_metaData->CreateAndConvertObjectValueRef<CRecordDataObjectCatalog>(this, guid, mode);
		}
	}
	else {
		pDataRef = m_metaData->CreateAndConvertObjectValueRef<CRecordDataObjectCatalog>(this, guid, mode);
	}

	return pDataRef;
}



IBackendValueForm* CMetaObjectCatalog::GetObjectForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormObject == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormObject);
	}

	if (defList == nullptr) {
		IRecordDataObject* objectData = CreateObjectValue(eObjectMode::OBJECT_ITEM);
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			objectData, formGuid
		);
		valueForm->BuildForm(CMetaObjectCatalog::eFormObject);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateObjectValue(eObjectMode::OBJECT_ITEM), formGuid
	);
}

IBackendValueForm* CMetaObjectCatalog::GetFolderForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormGroup == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormGroup);
	}

	if (defList == nullptr) {
		IRecordDataObject* objectData = CreateObjectValue(eObjectMode::OBJECT_FOLDER);
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			objectData, formGuid
		);
		valueForm->BuildForm(CMetaObjectCatalog::eFormGroup);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateObjectValue(eObjectMode::OBJECT_FOLDER), formGuid
	);
}

IBackendValueForm* CMetaObjectCatalog::GetListForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormList == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormList);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, CMetaObjectCatalog::eFormList, CTreeDataObjectFolderRef::LIST_ITEM_FOLDER), formGuid
		);
		valueForm->BuildForm(CMetaObjectCatalog::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(ownerControl,
		m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, defList->GetTypeForm(), CTreeDataObjectFolderRef::LIST_ITEM_FOLDER)
	);
}

IBackendValueForm* CMetaObjectCatalog::GetSelectForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormSelect == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormSelect);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, CMetaObjectCatalog::eFormSelect, CTreeDataObjectFolderRef::LIST_ITEM, true), formGuid
		);
		valueForm->BuildForm(CMetaObjectCatalog::eFormSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, defList->GetTypeForm(), CTreeDataObjectFolderRef::LIST_ITEM, true), formGuid
	);
}

IBackendValueForm* CMetaObjectCatalog::GetFolderSelectForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormSelect == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormFolderSelect);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, CMetaObjectCatalog::eFormFolderSelect, CTreeDataObjectFolderRef::LIST_FOLDER, true), formGuid
		);
		valueForm->BuildForm(CMetaObjectCatalog::eFormFolderSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, m_metaData->CreateAndConvertObjectValueRef<CTreeDataObjectFolderRef>(this, defList->GetTypeForm(), CTreeDataObjectFolderRef::LIST_FOLDER, true), formGuid
	);
}

OptionList* CMetaObjectCatalog::GetFormObject(PropertyOption*)
{
	OptionList* optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormObject == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList* CMetaObjectCatalog::GetFormFolder(PropertyOption*)
{
	OptionList* optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormGroup == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList* CMetaObjectCatalog::GetFormList(PropertyOption*)
{
	OptionList* optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormList == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList* CMetaObjectCatalog::GetFormSelect(PropertyOption*)
{
	OptionList* optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormSelect == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList* CMetaObjectCatalog::GetFormFolderSelect(PropertyOption*)
{
	OptionList* optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormFolderSelect == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

wxString CMetaObjectCatalog::GetDataPresentation(const IObjectValueInfo* objValue) const
{
	CValue vDescription;
	if (objValue->GetValueByMetaID(m_attributeDescription->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

std::vector<IMetaObjectAttribute*> CMetaObjectCatalog::GetDefaultAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;

	attributes.push_back(m_attributeCode);
	attributes.push_back(m_attributeDescription);
	attributes.push_back(m_attributeOwner);
	attributes.push_back(m_attributeParent);
	attributes.push_back(m_attributeIsFolder);
	attributes.push_back(m_attributeReference);

	return attributes;
}

std::vector<IMetaObjectAttribute*> CMetaObjectCatalog::GetSearchedAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;

	attributes.push_back(m_attributeCode);
	attributes.push_back(m_attributeDescription);

	return attributes;
}

//////////////////////////////////////////////////////////////////////////////

#include "backend/objCtor.h"

bool CMetaObjectCatalog::LoadFromVariant(const wxVariant& variant)
{
	std::set<meta_identifier_t> prevRecords = m_ownerData.m_data;
	if (m_ownerData.LoadFromVariant(variant)) {
		std::set<meta_identifier_t> records = m_ownerData.m_data;
		for (auto record : prevRecords) {
			auto& it = records.find(record);
			if (it != records.end())
				continue;
			CMetaObjectCatalog* catalog = wxDynamicCast(
				m_metaData->GetMetaObject(record), CMetaObjectCatalog
			);
			if (catalog != nullptr) {
				IMetaValueTypeCtor* so =
					m_metaData->GetTypeCtor(catalog, eCtorMetaType::eCtorMetaType_Reference);
				wxASSERT(so);
				m_attributeOwner->ClearMetatype(
					so->GetClassType()
				);
			}
		}
		for (auto record : records) {
			auto& it = prevRecords.find(record);
			if (it != prevRecords.end())
				continue;
			CMetaObjectCatalog* catalog = wxDynamicCast(
				m_metaData->GetMetaObject(record), CMetaObjectCatalog
			);
			if (catalog != nullptr) {
				IMetaValueTypeCtor* so =
					m_metaData->GetTypeCtor(catalog, eCtorMetaType::eCtorMetaType_Reference);
				wxASSERT(so);
				m_attributeOwner->SetMetatype(
					so->GetClassType()
				);
			}
		}

		return true;
	}

	return false;
}

void CMetaObjectCatalog::SaveToVariant(wxVariant& variant, IMetaData* metaData) const
{
	m_ownerData.SaveToVariant(variant, metaData);
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool CMetaObjectCatalog::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeOwner->LoadMeta(dataReader);

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	//load default form 
	m_propertyDefFormObject->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormFolder->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	if (!m_ownerData.LoadData(dataReader))
		return false;

	return IMetaObjectRecordDataFolderMutableRef::LoadData(dataReader);
}

bool CMetaObjectCatalog::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeOwner->SaveMeta(dataWritter);

	//save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()));

	if (!m_ownerData.SaveData(dataWritter))
		return false;

	//create or update table:
	return IMetaObjectRecordDataFolderMutableRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool CMetaObjectCatalog::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRecordDataFolderMutableRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeOwner->OnCreateMetaObject(metaData) &&
		m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectCatalog::OnLoadMetaObject(IMetaData* metaData)
{
	if (!m_attributeOwner->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataFolderMutableRef::OnLoadMetaObject(metaData);
}

bool CMetaObjectCatalog::OnSaveMetaObject()
{
	if (!m_attributeOwner->OnSaveMetaObject())
		return false;

	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataFolderMutableRef::OnSaveMetaObject();
}

bool CMetaObjectCatalog::OnDeleteMetaObject()
{
	if (!m_attributeOwner->OnDeleteMetaObject())
		return false;

	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataFolderMutableRef::OnDeleteMetaObject();
}

bool CMetaObjectCatalog::OnReloadMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CRecordDataObjectCatalog* pDataRef = nullptr;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

bool CMetaObjectCatalog::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeOwner->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleManager->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleObject->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	if (!IMetaObjectRecordDataFolderMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	IMetaValueTypeCtor* typeCtor =
		m_metaData->GetTypeCtor(this, eCtorMetaType::eCtorMetaType_Reference);

	if (typeCtor != nullptr && !m_attributeParent->ContainType(typeCtor->GetClassType())) {
		m_attributeParent->SetDefaultMetatype(typeCtor->GetClassType());
	}

	return true;
}

bool CMetaObjectCatalog::OnAfterRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRecordDataFolderMutableRef::OnAfterRunMetaObject(flags))
			return moduleManager->AddCompileModule(m_moduleObject, CreateObjectValue(eObjectMode::OBJECT_ITEM));

		return false;
	}

	return IMetaObjectRecordDataFolderMutableRef::OnAfterRunMetaObject(flags);
}

bool CMetaObjectCatalog::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRecordDataFolderMutableRef::OnBeforeCloseMetaObject())
			return moduleManager->RemoveCompileModule(m_moduleObject);

		return false;
	}

	return IMetaObjectRecordDataFolderMutableRef::OnBeforeCloseMetaObject();
}

bool CMetaObjectCatalog::OnAfterCloseMetaObject()
{
	if (!m_attributeOwner->OnAfterCloseMetaObject())
		return false;

	if (!m_moduleManager->OnAfterCloseMetaObject())
		return false;

	if (!m_moduleObject->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return IMetaObjectRecordDataFolderMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectCatalog::OnCreateFormObject(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormGroup
		&& m_propertyDefFormFolder->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
	}
}

void CMetaObjectCatalog::OnRemoveMetaForm(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormGroup
		&& m_propertyDefFormFolder->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectCatalog, "catalog", g_metaCatalogCLSID);