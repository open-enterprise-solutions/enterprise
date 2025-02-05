////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document metaData
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectDocument, IMetaObjectRecordDataMutableRef);

//********************************************************************************************

class CListDocumentDataObjectRef : public CListDataObjectRef {
public:
	CListDocumentDataObjectRef(CMetaObjectDocument* metaObject = nullptr, const form_identifier_t& formType = wxNOT_FOUND, bool choiceMode = false) :
		CListDataObjectRef(metaObject, formType, choiceMode)
	{
		IListDataObject::AppendSort(metaObject->GetDocumentNumber(), true, false);
		IListDataObject::AppendSort(metaObject->GetDocumentDate(), true, true);
		IListDataObject::AppendSort(metaObject->GetDataReference(), true, true, true);
	}
};

//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

CMetaObjectDocument::CMetaObjectDocument() : IMetaObjectRecordDataMutableRef()
{
	//create default attributes
	m_attributeNumber = CMetaObjectAttributeDefault::CreateString(wxT("number"), _("Number"), wxEmptyString, 11, true);
	//set child/parent
	m_attributeNumber->SetParent(this);
	AddChild(m_attributeNumber);

	m_attributeDate = CMetaObjectAttributeDefault::CreateDate(wxT("date"), _("Date"), wxEmptyString, eDateFractions::eDateFractions_DateTime, true);
	//set child/parent
	m_attributeDate->SetParent(this);
	AddChild(m_attributeDate);

	m_attributePosted = CMetaObjectAttributeDefault::CreateBoolean(wxT("posted"), _("Posted"), wxEmptyString);
	//set child/parent
	m_attributePosted->SetParent(this);
	AddChild(m_attributePosted);

	//create module
	m_moduleObject = new CMetaObjectModule(objectModule);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	//set default proc
	m_moduleObject->SetDefaultProcedure("beforeWrite", eContentHelper::eProcedureHelper, { "cancel", "writeMode", "postingMode" });
	m_moduleObject->SetDefaultProcedure("onWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("beforeDelete", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onDelete", eContentHelper::eProcedureHelper, { "cancel" });

	m_moduleObject->SetDefaultProcedure("posting", eContentHelper::eProcedureHelper, { "cancel", "postingMode" });
	m_moduleObject->SetDefaultProcedure("undoPosting", eContentHelper::eProcedureHelper, { "cancel" });

	m_moduleObject->SetDefaultProcedure("filling", eContentHelper::eProcedureHelper, { "source", "standartProcessing" });
	m_moduleObject->SetDefaultProcedure("onCopy", eContentHelper::eProcedureHelper, { "source" });

	m_moduleManager = new CMetaObjectManagerModule(managerModule);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectDocument::~CMetaObjectDocument()
{
	wxDELETE(m_attributeNumber);
	wxDELETE(m_attributeDate);
	wxDELETE(m_attributePosted);

	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaObjectForm* CMetaObjectDocument::GetDefaultFormByID(const form_identifier_t& id)
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

ISourceDataObject* CMetaObjectDocument::CreateObjectData(IMetaObjectForm* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject: return CreateObjectValue(); break;
	case eFormList:
		return m_metaData->CreateAndConvertObjectValueRef<CListDocumentDataObjectRef>(this, metaObject->GetTypeForm());
		break;
	case eFormSelect:
		return m_metaData->CreateAndConvertObjectValueRef<CListDocumentDataObjectRef>(this, metaObject->GetTypeForm(), true);
		break;
	}

	return nullptr;
}

#include "backend/appData.h"

IRecordDataObjectRef* CMetaObjectDocument::CreateObjectRefValue(const Guid& objGuid)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	CRecordDataObjectDocument* pDataRef = nullptr;
	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef))
			return m_metaData->CreateAndConvertObjectValueRef<CRecordDataObjectDocument>(this, objGuid);
	}
	else {
		pDataRef = m_metaData->CreateAndConvertObjectValueRef<CRecordDataObjectDocument>(this, objGuid);
	}

	return pDataRef;
}



IBackendValueForm* CMetaObjectDocument::GetObjectForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectDocument::eFormObject == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectDocument::eFormObject);
	}

	if (defList == nullptr) {
		IRecordDataObject* objectData = CreateObjectValue();
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			objectData, formGuid
		);
		valueForm->BuildForm(CMetaObjectDocument::eFormObject);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateObjectValue(), formGuid
	);
}

IBackendValueForm* CMetaObjectDocument::GetListForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectDocument::eFormList == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectDocument::eFormList);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			m_metaData->CreateAndConvertObjectValueRef <CListDocumentDataObjectRef>(this, CMetaObjectDocument::eFormList), formGuid
		);
		valueForm->BuildForm(CMetaObjectDocument::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, m_metaData->CreateAndConvertObjectValueRef<CListDocumentDataObjectRef>(this, defList->GetTypeForm()), formGuid
	);
}

IBackendValueForm* CMetaObjectDocument::GetSelectForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectDocument::eFormSelect == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectDocument::eFormSelect);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			m_metaData->CreateAndConvertObjectValueRef<CListDocumentDataObjectRef>(this, CMetaObjectDocument::eFormSelect, true), formGuid
		);
		valueForm->BuildForm(CMetaObjectDocument::eFormSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, m_metaData->CreateAndConvertObjectValueRef<CListDocumentDataObjectRef>(this, defList->GetTypeForm()), formGuid
	);
}

OptionList* CMetaObjectDocument::GetFormObject(PropertyOption*)
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

OptionList* CMetaObjectDocument::GetFormList(PropertyOption*)
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

OptionList* CMetaObjectDocument::GetFormSelect(PropertyOption*)
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

wxString CMetaObjectDocument::GetDataPresentation(const IObjectValueInfo* objValue) const
{
	CValue vDate, vNumber;
	if (!objValue->GetValueByMetaID(m_attributeDate->GetMetaID(), vDate))
		return wxEmptyString;
	if (!objValue->GetValueByMetaID(m_attributeNumber->GetMetaID(), vNumber))
		return wxEmptyString;
	return GetSynonym() << wxT(" ") << vNumber.GetString() << wxT(" ") << vDate.GetString();
}

std::vector<IMetaObjectAttribute*> CMetaObjectDocument::GetDefaultAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;
	attributes.push_back(m_attributeNumber);
	attributes.push_back(m_attributeDate);
	attributes.push_back(m_attributePosted);
	attributes.push_back(m_attributeReference);
	return attributes;
}

std::vector<IMetaObjectAttribute*> CMetaObjectDocument::GetSearchedAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;

	attributes.push_back(m_attributeNumber);
	attributes.push_back(m_attributeDate);

	return attributes;
}

//////////////////////////////////////////////////////////////////////////////

bool CMetaObjectDocument::LoadFromVariant(const wxVariant& variant)
{
	std::set<meta_identifier_t> prevRecords = m_recordData.m_data;
	if (m_recordData.LoadFromVariant(variant)) {
		std::set<meta_identifier_t> records = m_recordData.m_data;
		for (auto record : prevRecords) {
			auto& it = records.find(record);
			if (it != records.end())
				continue;
			IMetaObjectRegisterData* registerData = wxDynamicCast(
				m_metaData->GetMetaObject(record), IMetaObjectRegisterData
			);
			if (registerData != nullptr) {
				CMetaObjectAttributeDefault* infoRecorder = registerData->GetRegisterRecorder();
				infoRecorder->ClearMetatype(
					m_attributeReference->GetClsids()
				);
			}
		}
		for (auto record : records) {
			auto& it = prevRecords.find(record);
			if (it != prevRecords.end())
				continue;
			IMetaObjectRegisterData* registerData = wxDynamicCast(
				m_metaData->GetMetaObject(record), IMetaObjectRegisterData
			);
			if (registerData != nullptr) {
				CMetaObjectAttributeDefault* infoRecorder = registerData->GetRegisterRecorder();
				infoRecorder->SetMetatype(
					m_attributeReference->GetClsids()
				);
			}
		}
		return true;
	}
	return false;
}

void CMetaObjectDocument::SaveToVariant(wxVariant& variant, IMetaData* metaData) const
{
	m_recordData.SaveToVariant(variant, metaData);
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool CMetaObjectDocument::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeNumber->LoadMeta(dataReader);
	m_attributeDate->LoadMeta(dataReader);
	m_attributePosted->LoadMeta(dataReader);

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	//load default form 
	m_propertyDefFormObject->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	if (!m_recordData.LoadData(dataReader))
		return false;

	return IMetaObjectRecordDataMutableRef::LoadData(dataReader);
}

bool CMetaObjectDocument::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeNumber->SaveMeta(dataWritter);
	m_attributeDate->SaveMeta(dataWritter);
	m_attributePosted->SaveMeta(dataWritter);

	//Save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()));

	if (!m_recordData.SaveData(dataWritter))
		return false;

	//create or update table:
	return IMetaObjectRecordDataMutableRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool CMetaObjectDocument::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeNumber->OnCreateMetaObject(metaData) &&
		m_attributeDate->OnCreateMetaObject(metaData) &&
		m_attributePosted->OnCreateMetaObject(metaData) &&
		m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectDocument::OnLoadMetaObject(IMetaData* metaData)
{
	if (!m_attributeNumber->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeDate->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributePosted->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataMutableRef::OnLoadMetaObject(metaData);
}

bool CMetaObjectDocument::OnSaveMetaObject()
{
	if (!m_attributeNumber->OnSaveMetaObject())
		return false;

	if (!m_attributeDate->OnSaveMetaObject())
		return false;

	if (!m_attributePosted->OnSaveMetaObject())
		return false;

	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataMutableRef::OnSaveMetaObject();
}

bool CMetaObjectDocument::OnDeleteMetaObject()
{
	if (!m_attributeNumber->OnDeleteMetaObject())
		return false;

	if (!m_attributeDate->OnDeleteMetaObject())
		return false;

	if (!m_attributePosted->OnDeleteMetaObject())
		return false;

	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataMutableRef::OnDeleteMetaObject();
}

bool CMetaObjectDocument::OnReloadMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CRecordDataObjectDocument* pDataRef = nullptr;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return true;
		}

		if (pDataRef->InitializeObject()) {
			pDataRef->UpdateRecordSet();
			return true;
		}

		return false;
	}

	return true;
}

#include "backend/objCtor.h"

bool CMetaObjectDocument::OnBeforeRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_attributeNumber->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeDate->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributePosted->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleManager->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleObject->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	return IMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectDocument::OnAfterRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags)) {
			return moduleManager->AddCompileModule(m_moduleObject, CreateObjectValue());
		}

		return false;
	}

	return IMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags);
}

bool CMetaObjectDocument::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject()) {
			return moduleManager->RemoveCompileModule(m_moduleObject);
		}

		return false;
	}

	return IMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject();
}

bool CMetaObjectDocument::OnAfterCloseMetaObject()
{
	if (!m_attributePosted->OnAfterCloseMetaObject())
		return false;

	if (!m_attributePosted->OnAfterCloseMetaObject())
		return false;

	if (!m_attributePosted->OnAfterCloseMetaObject())
		return false;

	if (!m_moduleManager->OnAfterCloseMetaObject())
		return false;

	if (!m_moduleObject->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return IMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectDocument::OnCreateFormObject(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectDocument::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocument::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocument::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
}

void CMetaObjectDocument::OnRemoveMetaForm(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectDocument::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocument::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocument::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectDocument, "document", g_metaDocumentCLSID);
SYSTEM_TYPE_REGISTER(CRecordDataObjectDocument::CRecorderRegisterDocument, "recordRegister", string_to_clsid("VL_RECR"));