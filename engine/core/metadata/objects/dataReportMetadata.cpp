////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - metadata
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "metadata/metadata.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectReportValue, IMetaObjectValue)
wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectReportExternalValue, CMetaObjectReportValue)

//********************************************************************************************
//*                                      metadata                                            *
//********************************************************************************************

CMetaObjectReportValue::CMetaObjectReportValue() : IMetaObjectValue(),
m_defaultFormObject(wxNOT_FOUND), m_objMode(METAOBJECT_NORMAL)
{
	PropertyContainer *categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_object", PropertyType::PT_OPTION, &CMetaObjectReportValue::GetFormObject);
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

CMetaObjectReportValue::CMetaObjectReportValue(int objMode) : IMetaObjectValue(),
m_defaultFormObject(wxNOT_FOUND), m_objMode(objMode)
{
	PropertyContainer *categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_object", PropertyType::PT_OPTION, &CMetaObjectReportValue::GetFormObject);
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

CMetaObjectReportValue::~CMetaObjectReportValue()
{
	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaFormObject *CMetaObjectReportValue::GetDefaultFormByID(form_identifier_t id)
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

IDataObjectSource *CMetaObjectReportValue::CreateObjectData(IMetaFormObject *metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormReport: return CreateObjectValue(); break;
	}

	return NULL;
}

#include "appData.h"

IDataObjectValue *CMetaObjectReportValue::CreateObjectValue()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	IDataObjectValue *pDataRef = NULL;
	if (appData->DesignerMode()) {
		if (m_objMode == METAOBJECT_NORMAL) {
			if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef))
				return new CObjectReportValue(this);
		}
		else {
			return moduleManager->GetObjectValue();
		}
	}
	else {
		if (m_objMode == METAOBJECT_NORMAL) {
			pDataRef = new CObjectReportValue(this);
		}
		else {
			return moduleManager->GetObjectValue();
		}
	}

	return pDataRef;
}

CValueForm *CMetaObjectReportValue::CreateObjectValue(IMetaFormObject *metaForm)
{
	return metaForm->GenerateFormAndRun(
		NULL, CreateObjectData(metaForm)
	);
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm *CMetaObjectReportValue::GetObjectForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectReportValue::eFormReport == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				return defList->GenerateFormAndRun(ownerControl,
					CreateObjectValue()
				);
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectReportValue::eFormReport);
		if (defList) {
			return defList->GenerateFormAndRun(ownerControl, 
				CreateObjectValue(), formGuid
			);
		}
	}

	if (defList == NULL) {
		IDataObjectValue *objectData = CreateObjectValue();
		CValueForm *valueForm = new CValueForm;
		valueForm->InitializeForm(ownerControl, NULL,
			objectData, formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectReportValue::eFormReport);
		return valueForm;
	}

	return defList->GenerateFormAndRun();
}

OptionList *CMetaObjectReportValue::GetFormObject(Property *)
{
	OptionList *optlist = new OptionList;
	optlist->AddOption("<not selected>", wxNOT_FOUND);

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

bool CMetaObjectReportValue::LoadData(CMemoryReader &dataReader)
{
	//Load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	//Load default form 
	m_defaultFormObject = dataReader.r_u32();

	return IMetaObjectValue::LoadData(dataReader);
}

bool CMetaObjectReportValue::SaveData(CMemoryWriter &dataWritter)
{
	//Save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//Save default form 
	dataWritter.w_u32(m_defaultFormObject);

	return IMetaObjectValue::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool CMetaObjectReportValue::OnCreateMetaObject(IMetadata *metaData)
{
	if (!IMetaObjectValue::OnCreateMetaObject(metaData))
		return false;

	return (m_objMode == METAOBJECT_NORMAL ? m_moduleManager->OnCreateMetaObject(metaData) : true) && 
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectReportValue::OnLoadMetaObject(IMetadata *metaData)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
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

	return IMetaObjectValue::OnLoadMetaObject(metaData);
}

bool CMetaObjectReportValue::OnSaveMetaObject()
{
	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnSaveMetaObject())
			return false;
	}

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return IMetaObjectValue::OnSaveMetaObject();
}

bool CMetaObjectReportValue::OnDeleteMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnDeleteMetaObject())
			return false;
	}

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectValue::OnDeleteMetaObject();
}

bool CMetaObjectReportValue::OnReloadMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CObjectReportValue *pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return false;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

bool CMetaObjectReportValue::OnRunMetaObject(int flags)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnRunMetaObject(flags))
			return false;
	}

	if (!m_moduleObject->OnRunMetaObject(flags)) {
		return false;
	}

	if (appData->DesignerMode()) {
		if (moduleManager->AddCompileModule(m_moduleObject, CreateObjectValue())) {
			return IMetaObjectValue::OnRunMetaObject(flags);
		}
		return false;
	}

	return IMetaObjectValue::OnRunMetaObject(flags);
}

bool CMetaObjectReportValue::OnCloseMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (m_objMode == METAOBJECT_NORMAL) {
		if (!m_moduleManager->OnCloseMetaObject())
			return false;
	}

	if (!m_moduleObject->OnCloseMetaObject())
		return false;

	if (appData->DesignerMode()) {
		if (moduleManager->RemoveCompileModule(m_moduleObject)) {
			return IMetaObjectValue::OnCloseMetaObject();
		}
	}
	else {
		return IMetaObjectValue::OnCloseMetaObject();
	}

	return false;
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectReportValue::OnCreateMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectReportValue::eFormReport
		&& m_defaultFormObject == wxNOT_FOUND)
	{
		m_defaultFormObject = metaForm->GetMetaID();
	}
}

void CMetaObjectReportValue::OnRemoveMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectReportValue::eFormReport
		&& m_defaultFormObject == metaForm->GetMetaID())
	{
		m_defaultFormObject = wxNOT_FOUND;
	}
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectReportValue::ReadProperty()
{
	IMetaObjectValue::ReadProperty();

	IObjectBase::SetPropertyValue("default_object", m_defaultFormObject);
}

void CMetaObjectReportValue::SaveProperty()
{
	IMetaObjectValue::SaveProperty();

	IObjectBase::GetPropertyValue("default_object", m_defaultFormObject);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectReportValue, "metaReport", g_metaReportCLSID);
METADATA_REGISTER(CMetaObjectReportExternalValue, "metaExternalReport", g_metaExternalReportCLSID);