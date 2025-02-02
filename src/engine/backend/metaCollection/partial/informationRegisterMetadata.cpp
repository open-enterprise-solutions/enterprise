#include "informationRegister.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

#define objectModule wxT("recordSetModule")
#define managerModule wxT("managerModule")

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectInformationRegister::CMetaObjectRecordManager, IMetaObject);
wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectInformationRegister, IMetaObjectRegisterData);

/////////////////////////////////////////////////////////////////////////

CMetaObjectInformationRegister::CMetaObjectInformationRegister() : IMetaObjectRegisterData(),
m_metaRecordManager(new CMetaObjectRecordManager())
{
	//create module
	m_moduleObject = new CMetaObjectModule(objectModule);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	//set default proc
	m_moduleObject->SetDefaultProcedure("beforeWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onWrite", eContentHelper::eProcedureHelper, { "cancel" });

	m_moduleManager = new CMetaObjectManagerModule(managerModule);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectInformationRegister::~CMetaObjectInformationRegister()
{
	wxDELETE(m_metaRecordManager);

	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaObjectForm* CMetaObjectInformationRegister::GetDefaultFormByID(const form_identifier_t& id)
{
	if (id == eFormRecord && m_propertyDefFormRecord->GetValueAsInteger() != wxNOT_FOUND) {
		for (auto& obj : GetObjectForms()) {
			if (m_propertyDefFormRecord->GetValueAsInteger() == obj->GetMetaID()) {
				return obj;
			}
		}
	}
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		for (auto& obj : GetObjectForms()) {
			if (m_propertyDefFormList->GetValueAsInteger() == obj->GetMetaID()) {
				return obj;
			}
		}
	}

	return nullptr;
}

IBackendValueForm* CMetaObjectInformationRegister::GetRecordForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniquePairKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectInformationRegister::eFormRecord == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectInformationRegister::eFormRecord);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			CreateRecordManagerObjectValue(), formGuid
		);
		valueForm->BuildForm(CMetaObjectInformationRegister::eFormRecord);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateRecordManagerObjectValue(), formGuid
	);

	return nullptr;
}

IBackendValueForm* CMetaObjectInformationRegister::GetListForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectInformationRegister::eFormList == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectInformationRegister::eFormList);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			m_metaData->CreateAndConvertObjectValueRef<CListRegisterObject>(this, CMetaObjectInformationRegister::eFormList), formGuid
		);
		valueForm->BuildForm(CMetaObjectInformationRegister::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, m_metaData->CreateAndConvertObjectValueRef<CListRegisterObject>(this, defList->GetTypeForm()), formGuid
	);

	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////

OptionList* CMetaObjectInformationRegister::GetFormRecord(PropertyOption*)
{
	OptionList* optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormRecord == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList* CMetaObjectInformationRegister::GetFormList(PropertyOption*)
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

//***************************************************************************
//*								form record								    *
//***************************************************************************

IBackendValueForm* CMetaObjectInformationRegister::GetRecordForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
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

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool CMetaObjectInformationRegister::LoadData(CMemoryReader& dataReader)
{
	//load default form 
	m_propertyDefFormRecord->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	//load data 
	m_propertyWriteMode->SetValue(dataReader.r_u16());
	m_propertyPeriodicity->SetValue(dataReader.r_u16());

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	return IMetaObjectRegisterData::LoadData(dataReader);
}

bool CMetaObjectInformationRegister::SaveData(CMemoryWriter& dataWritter)
{
	//save default form 
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormRecord->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));

	//save data
	dataWritter.w_u16(m_propertyWriteMode->GetValueAsInteger());
	dataWritter.w_u16(m_propertyPeriodicity->GetValueAsInteger());

	//Save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectRegisterData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool CMetaObjectInformationRegister::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRegisterData::OnCreateMetaObject(metaData))
		return false;

	return m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectInformationRegister::OnLoadMetaObject(IMetaData* metaData)
{
	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool CMetaObjectInformationRegister::OnSaveMetaObject()
{
	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	if (GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		if (!(m_attributeRecorder->GetClsidCount() > 0))
			return false;
	}
#endif

	return IMetaObjectRegisterData::OnSaveMetaObject();
}

bool CMetaObjectInformationRegister::OnDeleteMetaObject()
{
	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectRegisterData::OnDeleteMetaObject();
}

bool CMetaObjectInformationRegister::OnReloadMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		CRecordSetObjectInformationRegister* recordSet = nullptr;
		if (moduleManager->FindCompileModule(m_moduleObject, recordSet)) {
			if (!recordSet->InitializeObject())
				return false;
		}

		CRecordManagerObjectInformationRegister* recordManager = nullptr;
		if (moduleManager->FindCompileModule(m_metaRecordManager, recordManager)) {
			if (!recordManager->InitializeObject())
				return false;
		}
	}

	return true;
}

#include "backend/objCtor.h"

bool CMetaObjectInformationRegister::OnBeforeRunMetaObject(int flags)
{
	if (!m_moduleManager->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleObject->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	return IMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectInformationRegister::OnAfterRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {

			if (!moduleManager->AddCompileModule(m_metaRecordManager, CreateRecordManagerObjectValue()))
				return false;

			if (!moduleManager->AddCompileModule(m_moduleObject, CreateRecordSetObjectValue()))
				return false;

			return true;
		}
	}

	return IMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool CMetaObjectInformationRegister::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRegisterData::OnBeforeCloseMetaObject()) {

			if (!moduleManager->RemoveCompileModule(m_metaRecordManager))
				return false;

			if (!moduleManager->RemoveCompileModule(m_moduleObject))
				return false;

			return true;
		}
	}

	return IMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool CMetaObjectInformationRegister::OnAfterCloseMetaObject()
{
	if (!m_moduleManager->OnAfterCloseMetaObject())
		return false;

	if (!m_moduleObject->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return IMetaObjectRegisterData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectInformationRegister::OnCreateFormObject(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormRecord
		&& m_propertyDefFormRecord->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormRecord->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

void CMetaObjectInformationRegister::OnRemoveMetaForm(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormRecord
		&& m_propertyDefFormRecord->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormRecord->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

std::vector<IMetaObjectAttribute*> CMetaObjectInformationRegister::GetDefaultAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;

	if (GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		attributes.push_back(m_attributeLineActive);
	}

	if (GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		attributes.push_back(m_attributePeriod);
	}

	if (GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		attributes.push_back(m_attributeRecorder);
		attributes.push_back(m_attributeLineNumber);
	}

	return attributes;
}

std::vector<IMetaObjectAttribute*> CMetaObjectInformationRegister::GetGenericDimensions() const
{
	std::vector<IMetaObjectAttribute*> attributes;

	if (GetWriteRegisterMode() != eWriteRegisterMode::eSubordinateRecorder) {
		if (GetPeriodicity() != ePeriodicity::eNonPeriodic) {
			attributes.push_back(m_attributePeriod);
		}
		for (auto& obj : GetObjectDimensions()) {
			attributes.push_back(obj);
		}
	}
	else {
		attributes.push_back(m_attributeRecorder);
	}

	return attributes;
}

ISourceDataObject* CMetaObjectInformationRegister::CreateObjectData(IMetaObjectForm* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormRecord: 
		return CreateRecordManagerObjectValue();
	case eFormList: 
		return m_metaData->CreateAndConvertObjectValueRef<CListRegisterObject>(this, metaObject->GetTypeForm());
	}

	return nullptr;
}

IRecordSetObject* CMetaObjectInformationRegister::CreateRecordSetObjectRegValue(const CUniquePairKey& uniqueKey)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		IRecordSetObject* pDataRef = nullptr;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return m_metaData->CreateAndConvertObjectValueRef<CRecordSetObjectInformationRegister>(this, uniqueKey);
		}
		return pDataRef;
	}

	return m_metaData->CreateAndConvertObjectValueRef<CRecordSetObjectInformationRegister>(this, uniqueKey);
}

IRecordManagerObject* CMetaObjectInformationRegister::CreateRecordManagerObjectRegValue(const CUniquePairKey& uniqueKey)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		IRecordManagerObject* pDataRef = nullptr;
		if (!moduleManager->FindCompileModule(m_metaRecordManager, pDataRef)) {
			return m_metaData->CreateAndConvertObjectValueRef<CRecordManagerObjectInformationRegister>(this, uniqueKey);
		}
		return pDataRef;
	}
	return m_metaData->CreateAndConvertObjectValueRef<CRecordManagerObjectInformationRegister>(this, uniqueKey);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(CMetaObjectInformationRegister::CMetaObjectRecordManager, "recordManager", string_to_clsid("MT_RCMG"));
METADATA_TYPE_REGISTER(CMetaObjectInformationRegister, "informationRegister", g_metaInformationRegisterCLSID);