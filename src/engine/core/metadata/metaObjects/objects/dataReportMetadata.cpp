////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - metadata
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "metadata/metadata.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectReport, IMetaObjectRecordData)
wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectReportExternal, CMetaObjectReport)

//********************************************************************************************
//*                                      metadata                                            *
//********************************************************************************************

CMetaObjectReport::CMetaObjectReport() : IMetaObjectRecordData(),
m_defaultFormObject(wxNOT_FOUND), m_objMode(METAOBJECT_NORMAL)
{
	PropertyContainer* categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_object", PropertyType::PT_OPTION, &CMetaObjectReport::GetFormObject);
	m_category->AddCategory(categoryForm);

	//create module
	m_moduleObject = new CMetaModuleObject(objectModule);
	m_moduleObject->SetClsid(g_metaModuleCLSID);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	m_moduleManager = new CMetaManagerModuleObject(managerModule);
	m_moduleManager->SetClsid(g_metaManagerCLSID);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectReport::CMetaObjectReport(int objMode) : IMetaObjectRecordData(),
m_defaultFormObject(wxNOT_FOUND), m_objMode(objMode)
{
	PropertyContainer* categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_object", PropertyType::PT_OPTION, &CMetaObjectReport::GetFormObject);
	m_category->AddCategory(categoryForm);

	//create module
	m_moduleObject = new CMetaModuleObject(objectModule);
	m_moduleObject->SetClsid(g_metaModuleCLSID);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	m_moduleManager = new CMetaManagerModuleObject(managerModule);
	m_moduleManager->SetClsid(g_metaManagerCLSID);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectReport::~CMetaObjectReport()
{
	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaFormObject* CMetaObjectReport::GetDefaultFormByID(const form_identifier_t &id)
{
	if (id == eFormReport
		&& m_defaultFormObject != wxNOT_FOUND) {
		for (auto obj : GetObjectForms()) {
			if (m_defaultFormObject == obj->GetMetaID()) {
				return obj;
			}
		}
	}

	return NULL;
}

ISourceDataObject* CMetaObjectReport::CreateObjectData(IMetaFormObject* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormReport: return CreateObjectValue(); break;
	}

	return NULL;
}

#include "appData.h"

IRecordDataObject* CMetaObjectReport::CreateObjectValue()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	IRecordDataObject* pDataRef = NULL;
	if (appData->DesignerMode()) {
		if (m_objMode == METAOBJECT_NORMAL) {
			if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef))
				return new CObjectReport(this);
		}
		else {
			return moduleManager->GetObjectValue();
		}
	}
	else {
		if (m_objMode == METAOBJECT_NORMAL) {
			pDataRef = new CObjectReport(this);
		}
		else {
			return moduleManager->GetObjectValue();
		}
	}

	return pDataRef;
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm* CMetaObjectReport::GetObjectForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectReport::eFormReport == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				return defList->GenerateFormAndRun(ownerControl,
					CreateObjectValue()
				);
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectReport::eFormReport);
		if (defList) {
			return defList->GenerateFormAndRun(ownerControl,
				CreateObjectValue(), formGuid
			);
		}
	}

	if (defList == NULL) {
		IRecordDataObject* objectData = CreateObjectValue();
		CValueForm* valueForm = new CValueForm;
		valueForm->InitializeForm(ownerControl, NULL,
			objectData, formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectReport::eFormReport);
		return valueForm;
	}

	return defList->GenerateFormAndRun();
}

OptionList* CMetaObjectReport::GetFormObject(Property*)
{
	OptionList* optlist = new OptionList;
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormReport == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool CMetaObjectReport::LoadData(CMemoryReader& dataReader)
{
	//Load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	//Load default form 
	m_defaultFormObject = dataReader.r_u32();

	return IMetaObjectRecordData::LoadData(dataReader);
}

bool CMetaObjectReport::SaveData(CMemoryWriter& dataWritter)
{
	//Save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//Save default form 
	dataWritter.w_u32(m_defaultFormObject);

	return IMetaObjectRecordData::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool CMetaObjectReport::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordData::OnCreateMetaObject(metaData))
		return false;

	return (m_objMode == METAOBJECT_NORMAL ? m_moduleManager->OnCreateMetaObject(metaData) : true) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectReport::OnLoadMetaObject(IMetadata* metaData)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_objMode == METAOBJECT_NORMAL) {

		if (!m_moduleManager->OnLoadMetaObject(metaData))
			return false;

		m_moduleObject->SetMetadata(m_metaData);

		if (!m_moduleObject->OnLoadMetaObject(metaData))
			return false;
	}
	else {

		m_moduleObject->SetMetadata(m_metaData);

		if (!m_moduleObject->OnLoadMetaObject(metaData))
			return false;
	}

	return IMetaObjectRecordData::OnLoadMetaObject(metaData);
}

bool CMetaObjectReport::OnSaveMetaObject()
{
	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnSaveMetaObject())
			return false;
	}

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordData::OnSaveMetaObject();
}

bool CMetaObjectReport::OnDeleteMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnDeleteMetaObject())
			return false;
	}

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordData::OnDeleteMetaObject();
}

bool CMetaObjectReport::OnReloadMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CObjectReport* pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

bool CMetaObjectReport::OnBeforeRunMetaObject(int flags)
{
	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnBeforeRunMetaObject(flags)) {
			return false;
		}
	}

	if (!m_moduleObject->OnBeforeRunMetaObject(flags)) {
		return false;
	}

	return IMetaObjectRecordData::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectReport::OnAfterRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		if (IMetaObjectRecordData::OnAfterRunMetaObject(flags))
			return moduleManager->AddCompileModule(m_moduleObject, CreateObjectValue());
		return false;
	}

	return IMetaObjectRecordData::OnAfterRunMetaObject(flags);
}

bool CMetaObjectReport::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		if (IMetaObjectRecordData::OnBeforeCloseMetaObject())
			return moduleManager->RemoveCompileModule(m_moduleObject);
		return false;
	}

	return IMetaObjectRecordData::OnBeforeCloseMetaObject();
}

bool CMetaObjectReport::OnAfterCloseMetaObject()
{
	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnAfterCloseMetaObject())
			return false;
	}

	if (!m_moduleObject->OnAfterCloseMetaObject()) {
		return false;
	}

	return IMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectReport::OnCreateMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectReport::eFormReport
		&& m_defaultFormObject == wxNOT_FOUND)
	{
		m_defaultFormObject = metaForm->GetMetaID();
	}
}

void CMetaObjectReport::OnRemoveMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectReport::eFormReport
		&& m_defaultFormObject == metaForm->GetMetaID())
	{
		m_defaultFormObject = wxNOT_FOUND;
	}
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectReport::ReadProperty()
{
	IMetaObjectRecordData::ReadProperty();

	IObjectBase::SetPropertyValue("default_object", m_defaultFormObject);
}

void CMetaObjectReport::SaveProperty()
{
	IMetaObjectRecordData::SaveProperty();

	IObjectBase::GetPropertyValue("default_object", m_defaultFormObject);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectReport, "report", g_metaReportCLSID);
METADATA_REGISTER(CMetaObjectReportExternal, "externalReport", g_metaExternalReportCLSID);