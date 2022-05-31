#include "informationRegister.h"
#include "list/objectList.h"
#include "metadata/metadata.h"
#include "metadata/moduleManager/moduleManager.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectInformationRegister, IMetaObjectRegisterData);

/////////////////////////////////////////////////////////////////////////

CMetaObjectInformationRegister::CMetaObjectInformationRegister() : IMetaObjectRegisterData(),
m_defaultFormRecord(wxNOT_FOUND), m_defaultFormList(wxNOT_FOUND),
m_writeMode(eWriteRegisterMode::eIndependent), m_periodicity(ePeriodicity::eNonPeriodic),
m_metaRecordManager(new CMetaObjectRecordManager())
{
	PropertyContainer* categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_record", PropertyType::PT_OPTION, &CMetaObjectInformationRegister::GetFormRecord);
	categoryForm->AddProperty("default_list", PropertyType::PT_OPTION, &CMetaObjectInformationRegister::GetFormList);
	m_category->AddCategory(categoryForm);

	PropertyContainer* categoryData = IObjectBase::CreatePropertyContainer("Data");
	categoryData->AddProperty("periodicity", PropertyType::PT_OPTION, &CMetaObjectInformationRegister::GetPeriodicity);
	categoryData->AddProperty("write_mode", PropertyType::PT_OPTION, &CMetaObjectInformationRegister::GetWriteMode);
	m_category->AddCategory(categoryData);

	//create module
	m_moduleObject = new CMetaModuleObject(objectModule);
	m_moduleObject->SetClsid(g_metaModuleCLSID);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	//set default proc
	m_moduleObject->SetDefaultProcedure("beforeWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onWrite", eContentHelper::eProcedureHelper, { "cancel" });

	m_moduleManager = new CMetaManagerModuleObject(managerModule);
	m_moduleManager->SetClsid(g_metaManagerCLSID);

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

CMetaFormObject* CMetaObjectInformationRegister::GetDefaultFormByID(const form_identifier_t& id)
{
	if (id == eFormRecord && m_defaultFormRecord != wxNOT_FOUND) {
		for (auto obj : GetObjectForms()) {
			if (m_defaultFormRecord == obj->GetMetaID()) {
				return obj;
			}
		}
	}
	else if (id == eFormList && m_defaultFormList != wxNOT_FOUND) {
		for (auto obj : GetObjectForms()) {
			if (m_defaultFormList == obj->GetMetaID()) {
				return obj;
			}
		}
	}

	return NULL;
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm* CMetaObjectInformationRegister::GetRecordForm(const wxString& formName, IValueFrame* ownerControl, const CUniquePairKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectInformationRegister::eFormRecord == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectInformationRegister::eFormRecord);
	}

	if (defList == NULL) {
		CValueForm* valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			CreateRecordManagerValue(), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectInformationRegister::eFormRecord);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateRecordManagerValue(), formGuid
	);

	return NULL;
}

CValueForm* CMetaObjectInformationRegister::GetListForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectInformationRegister::eFormList == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectInformationRegister::eFormList);
	}

	if (defList == NULL) {
		CValueForm* valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CListRegisterObject(this, CMetaObjectInformationRegister::eFormList), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectInformationRegister::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, new CListRegisterObject(this, defList->GetTypeForm()), formGuid
	);

	return NULL;
}

/////////////////////////////////////////////////////////////////////////////

OptionList* CMetaObjectInformationRegister::GetFormRecord(Property*)
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

OptionList* CMetaObjectInformationRegister::GetFormList(Property*)
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

CValueForm* CMetaObjectInformationRegister::GetRecordForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid)
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

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool CMetaObjectInformationRegister::LoadData(CMemoryReader& dataReader)
{
	//load default form 
	m_defaultFormRecord = dataReader.r_u32();
	m_defaultFormList = dataReader.r_u32();

	//load data 
	m_writeMode = (eWriteRegisterMode)dataReader.r_u16();
	m_periodicity = (ePeriodicity)dataReader.r_u16();

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	return IMetaObjectRegisterData::LoadData(dataReader);
}

bool CMetaObjectInformationRegister::SaveData(CMemoryWriter& dataWritter)
{
	//save default form 
	dataWritter.w_u32(m_defaultFormRecord);
	dataWritter.w_u32(m_defaultFormList);

	//save data
	dataWritter.w_u16(m_writeMode);
	dataWritter.w_u16(m_periodicity);

	//Save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//create or update table:
	return IMetaObjectRegisterData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool CMetaObjectInformationRegister::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRegisterData::OnCreateMetaObject(metaData))
		return false;

	return m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectInformationRegister::OnLoadMetaObject(IMetadata* metaData)
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

#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
	if (m_writeMode == eWriteRegisterMode::eSubordinateRecorder) {
		if (!(m_attributeRecorder->GetCount() > 0))
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

		CRecordSetInformationRegister* recordSet = NULL;
		if (moduleManager->FindCompileModule(m_moduleObject, recordSet)) {
			if (!recordSet->InitializeObject())
				return false;
		}

		CRecordManagerInformationRegister* recordManager = NULL;
		if (moduleManager->FindCompileModule(m_metaRecordManager, recordManager)) {
			if (!recordManager->InitializeObject())
				return false;
		}
	}

	return true;
}

#include "metadata/singleMetaTypes.h"

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

			if (!moduleManager->AddCompileModule(m_metaRecordManager, CreateRecordManagerValue()))
				return false;

			if (!moduleManager->AddCompileModule(m_moduleObject, CreateRecordSetValue()))
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

void CMetaObjectInformationRegister::OnCreateMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormRecord
		&& m_defaultFormRecord == wxNOT_FOUND)
	{
		m_defaultFormRecord = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormList
		&& m_defaultFormList == wxNOT_FOUND)
	{
		m_defaultFormList = metaForm->GetMetaID();
	}
}

void CMetaObjectInformationRegister::OnRemoveMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormRecord
		&& m_defaultFormRecord == metaForm->GetMetaID())
	{
		m_defaultFormList = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectInformationRegister::eFormList
		&& m_defaultFormList == metaForm->GetMetaID())
	{
		m_defaultFormList = wxNOT_FOUND;
	}
}

std::vector<IMetaAttributeObject*> CMetaObjectInformationRegister::GetDefaultAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	if (m_writeMode == eWriteRegisterMode::eSubordinateRecorder) {
		attributes.push_back(m_attributeLineActive);
	}

	if (m_periodicity != ePeriodicity::eNonPeriodic ||
		m_writeMode == eWriteRegisterMode::eSubordinateRecorder) {
		attributes.push_back(m_attributePeriod);
	}

	if (m_writeMode == eWriteRegisterMode::eSubordinateRecorder) {
		attributes.push_back(m_attributeRecorder);
		attributes.push_back(m_attributeLineNumber);
	}

	return attributes;
}

std::vector<IMetaAttributeObject*> CMetaObjectInformationRegister::GetGenericDimensions() const
{
	std::vector<IMetaAttributeObject*> attributes;

	if (m_writeMode != eWriteRegisterMode::eSubordinateRecorder) {
		if (m_periodicity != ePeriodicity::eNonPeriodic) {
			attributes.push_back(m_attributePeriod);
		}
		for (auto dimension : GetObjectDimensions()) {
			attributes.push_back(dimension);
		}
	}
	else {
		attributes.push_back(m_attributeRecorder);
	}

	return attributes;
}

ISourceDataObject* CMetaObjectInformationRegister::CreateObjectData(IMetaFormObject* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormRecord: return CreateRecordManagerValue();
	case eFormList: return new CListRegisterObject(this, metaObject->GetTypeForm());
	}

	return NULL;
}

IRecordSetObject* CMetaObjectInformationRegister::CreateRecordSet()
{
	return new CRecordSetInformationRegister(this);
}

IRecordSetObject* CMetaObjectInformationRegister::CreateRecordSet(const CUniquePairKey& uniqueKey)
{
	return new CRecordSetInformationRegister(this, uniqueKey);
}

IRecordManagerObject* CMetaObjectInformationRegister::CreateRecordManager()
{
	return new CRecordManagerInformationRegister(this);
}

IRecordManagerObject* CMetaObjectInformationRegister::CreateRecordManager(const CUniquePairKey& uniqueKey)
{
	return new CRecordManagerInformationRegister(this, uniqueKey);
}

/////////////////////////////////////////////////////////////////////////

IRecordSetObject* CMetaObjectInformationRegister::CreateRecordSetValue()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		IRecordSetObject* pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return CreateRecordSet();
		}
		return pDataRef;
	}
	return CreateRecordSet();
}

IRecordManagerObject* CMetaObjectInformationRegister::CreateRecordManagerValue()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		IRecordManagerObject* pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_metaRecordManager, pDataRef)) {
			return CreateRecordManager();
		}
		return pDataRef;
	}
	return CreateRecordManager();
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectInformationRegister::ReadProperty()
{
	IMetaObjectRegisterData::ReadProperty();

	IObjectBase::SetPropertyValue("default_record", m_defaultFormRecord);
	IObjectBase::SetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::SetPropertyValue("write_mode", m_writeMode);
	IObjectBase::SetPropertyValue("periodicity", m_periodicity);
}

void CMetaObjectInformationRegister::SaveProperty()
{
	IMetaObjectRegisterData::SaveProperty();

	IObjectBase::GetPropertyValue("default_record", m_defaultFormRecord);
	IObjectBase::GetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::GetPropertyValue("write_mode", m_writeMode, true);
	IObjectBase::GetPropertyValue("periodicity", m_periodicity, true);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectInformationRegister, "informationRegister", g_metaInformationRegisterCLSID);