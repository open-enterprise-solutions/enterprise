////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document metadata
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "list/objectList.h"
#include "metadata/metadata.h"
#include "metadata/moduleManager/moduleManager.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectDocumentValue, IMetaObjectRefValue);

//********************************************************************************************
//*                                      metadata                                            *
//********************************************************************************************

CMetaObjectDocumentValue::CMetaObjectDocumentValue() : IMetaObjectRefValue(),
m_defaultFormObject(wxNOT_FOUND), m_defaultFormList(wxNOT_FOUND), m_defaultFormSelect(wxNOT_FOUND)
{
	PropertyContainer *categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");

	categoryForm->AddProperty("default_object", PropertyType::PT_OPTION, &CMetaObjectDocumentValue::GetFormObject);
	categoryForm->AddProperty("default_list", PropertyType::PT_OPTION, &CMetaObjectDocumentValue::GetFormList);
	categoryForm->AddProperty("default_select", PropertyType::PT_OPTION, &CMetaObjectDocumentValue::GetFormSelect);

	m_category->AddCategory(categoryForm);

	//create default attributes
	m_attributeNumber = CMetaDefaultAttributeObject::CreateString(wxT("number"), _("Number"), wxEmptyString, 9, true);
	m_attributeNumber->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeNumber->SetParent(this);
	AddChild(m_attributeNumber);

	m_attributeDate = CMetaDefaultAttributeObject::CreateDate(wxT("date"), _("Date"), wxEmptyString, eDateFractions::eDateTime, true);
	m_attributeDate->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeDate->SetParent(this);
	AddChild(m_attributeDate);

	m_attributePosted = CMetaDefaultAttributeObject::CreateBoolean(wxT("posted"), _("Posted"), wxEmptyString);
	m_attributePosted->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributePosted->SetParent(this);
	AddChild(m_attributePosted);

	//create module
	m_moduleObject = new CMetaModuleObject(objectModule);
	m_moduleObject->SetClsid(g_metaModuleCLSID);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	//set default proc
	m_moduleObject->SetDefaultProcedure("beforeWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("beforeDelete", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onDelete", eContentHelper::eProcedureHelper, { "cancel" });

	m_moduleObject->SetDefaultProcedure("filling", eContentHelper::eProcedureHelper, { "source" });
	m_moduleObject->SetDefaultProcedure("onCopy", eContentHelper::eProcedureHelper, { "source" });

	m_moduleManager = new CMetaManagerModuleObject(managerModule);
	m_moduleManager->SetClsid(g_metaManagerCLSID);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectDocumentValue::~CMetaObjectDocumentValue()
{
	wxDELETE(m_attributeNumber);
	wxDELETE(m_attributeDate);
	wxDELETE(m_attributePosted);

	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaFormObject *CMetaObjectDocumentValue::GetDefaultFormByID(form_identifier_t id)
{
	if (id == eFormObject
		&& m_defaultFormObject != wxNOT_FOUND) {
		for (auto obj : GetObjectForms()) {
			if (m_defaultFormObject == obj->GetMetaID()) {
				return obj;
			}
		}
	}
	else if (id == eFormList
		&& m_defaultFormList != wxNOT_FOUND) {
		for (auto obj : GetObjectForms()) {
			if (m_defaultFormList == obj->GetMetaID()) {
				return obj;
			}
		}
	}
	else if (id == eFormSelect
		&& m_defaultFormSelect != wxNOT_FOUND) {
		for (auto obj : GetObjectForms()) {
			if (m_defaultFormSelect == obj->GetMetaID()) {
				return obj;
			}
		}
	}

	return NULL;
}

IDataObjectSource *CMetaObjectDocumentValue::CreateObjectData(IMetaFormObject *metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject: return CreateObjectRefValue(); break;
	case eFormList: return new CDataObjectList(this, metaObject->GetTypeForm()); break;
	case eFormSelect: return new CDataObjectList(this, metaObject->GetTypeForm(), true); break;
	}

	return NULL;
}

#include "appData.h"

IDataObjectRefValue *CMetaObjectDocumentValue::CreateObjectRefValue()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	IDataObjectRefValue *pDataRef = NULL;

	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef))
			return new CObjectDocumentValue(this);
	}
	else {
		pDataRef = new CObjectDocumentValue(this);
	}

	return pDataRef;
}

IDataObjectRefValue *CMetaObjectDocumentValue::CreateObjectRefValue(const Guid &guid)
{
	return new CObjectDocumentValue(this, guid);
}

CValueForm *CMetaObjectDocumentValue::CreateObjectValue(IMetaFormObject *metaForm)
{
	return metaForm->GenerateFormAndRun(
		NULL, CreateObjectData(metaForm)
	);
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm *CMetaObjectDocumentValue::GetObjectForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectDocumentValue::eFormObject == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectDocumentValue::eFormObject);
	}

	if (defList == NULL) {
		IDataObjectValue *objectData = CreateObjectRefValue();
		CValueForm *valueForm = new CValueForm;
		valueForm->InitializeForm(ownerControl, NULL,
			objectData, formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectDocumentValue::eFormObject);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateObjectRefValue(), formGuid
	);
}

CValueForm *CMetaObjectDocumentValue::GetListForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectDocumentValue::eFormList == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectDocumentValue::eFormList);
	}

	if (defList == NULL) {
		CValueForm *valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CDataObjectList(this, CMetaObjectDocumentValue::eFormList), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectDocumentValue::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, new CDataObjectList(this, defList->GetTypeForm()), formGuid
	);
}

CValueForm *CMetaObjectDocumentValue::GetSelectForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectDocumentValue::eFormSelect == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectDocumentValue::eFormSelect);
	}

	if (defList == NULL) {
		CValueForm *valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CDataObjectList(this, CMetaObjectDocumentValue::eFormSelect, true), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectDocumentValue::eFormSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, new CDataObjectList(this, defList->GetTypeForm()), formGuid
	);
}

OptionList *CMetaObjectDocumentValue::GetFormObject(Property *)
{
	OptionList *optlist = new OptionList();
	optlist->AddOption("<not selected>", wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormObject == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList *CMetaObjectDocumentValue::GetFormList(Property *)
{
	OptionList *optlist = new OptionList();
	optlist->AddOption("<not selected>", wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormList == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList *CMetaObjectDocumentValue::GetFormSelect(Property *)
{
	OptionList *optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormSelect == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

wxString CMetaObjectDocumentValue::GetDescription(const IObjectValueInfo *objValue) const
{
	CValue vDate = objValue->GetValueByMetaID(m_attributeDate->GetMetaID());
	CValue vNumber = objValue->GetValueByMetaID(m_attributeNumber->GetMetaID());

	wxString decr;
	decr << m_metaName << wxT(" ") << vNumber.GetString() << wxT(" ") << vDate.GetString();
	return decr;
}

std::vector<IMetaAttributeObject *> CMetaObjectDocumentValue::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject *> attributes;

	attributes.push_back(m_attributeNumber);
	attributes.push_back(m_attributeDate);

	for (auto metaObject : m_aMetaObjects)
	{
		if (metaObject->GetClsid() == g_metaAttributeCLSID)
			attributes.push_back(dynamic_cast<CMetaAttributeObject *>(metaObject));
	}

	attributes.push_back(m_attributePosted);
	return attributes;
}

std::vector<IMetaAttributeObject*> CMetaObjectDocumentValue::GetSearchedAttributes() const
{
	std::vector<IMetaAttributeObject *> attributes;
	attributes.push_back(m_attributeNumber);
	attributes.push_back(m_attributeDate);
	return attributes;
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool CMetaObjectDocumentValue::LoadData(CMemoryReader &dataReader)
{
	//load default attributes:
	m_attributeNumber->LoadMeta(dataReader);
	m_attributeDate->LoadMeta(dataReader);
	m_attributePosted->LoadMeta(dataReader);

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	m_defaultFormObject = dataReader.r_u32();
	m_defaultFormList = dataReader.r_u32();
	m_defaultFormSelect = dataReader.r_u32();

	return IMetaObjectRefValue::LoadData(dataReader);
}

bool CMetaObjectDocumentValue::SaveData(CMemoryWriter &dataWritter)
{
	//save default attributes:
	m_attributeNumber->SaveMeta(dataWritter);
	m_attributeDate->SaveMeta(dataWritter);
	m_attributePosted->SaveMeta(dataWritter);

	//Save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_u32(m_defaultFormObject);
	dataWritter.w_u32(m_defaultFormList);
	dataWritter.w_u32(m_defaultFormSelect);

	//create or update table:
	return IMetaObjectRefValue::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool CMetaObjectDocumentValue::OnCreateMetaObject(IMetadata *metaData)
{
	if (!IMetaObjectRefValue::OnCreateMetaObject(metaData))
		return false;

	return m_attributeNumber->OnCreateMetaObject(metaData) &&
		m_attributeDate->OnCreateMetaObject(metaData) &&
		m_attributePosted->OnCreateMetaObject(metaData) &&
		m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectDocumentValue::OnLoadMetaObject(IMetadata *metaData)
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

	return IMetaObjectRefValue::OnLoadMetaObject(metaData);
}

bool CMetaObjectDocumentValue::OnSaveMetaObject()
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

	return IMetaObjectRefValue::OnSaveMetaObject();
}

bool CMetaObjectDocumentValue::OnDeleteMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

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

	return IMetaObjectRefValue::OnDeleteMetaObject();
}

bool CMetaObjectDocumentValue::OnReloadMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CObjectDocumentValue *pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

#include "metadata/singleMetaTypes.h"

bool CMetaObjectDocumentValue::OnRunMetaObject(int flags)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_attributePosted->OnRunMetaObject(flags))
		return false;

	if (!m_attributePosted->OnRunMetaObject(flags))
		return false;

	if (!m_attributePosted->OnRunMetaObject(flags))
		return false;

	if (!m_moduleManager->OnRunMetaObject(flags))
		return false;

	if (!m_moduleObject->OnRunMetaObject(flags))
		return false;

	registerSelection();

	if (appData->DesignerMode()) {
		if (moduleManager->AddCompileModule(m_moduleObject, CreateObjectRefValue())) {
			return IMetaObjectRefValue::OnRunMetaObject(flags);
		}
		return false;
	}

	return IMetaObjectRefValue::OnRunMetaObject(flags);
}

bool CMetaObjectDocumentValue::OnCloseMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_attributePosted->OnCloseMetaObject())
		return false;

	if (!m_attributePosted->OnCloseMetaObject())
		return false;

	if (!m_attributePosted->OnCloseMetaObject())
		return false;

	if (!m_moduleManager->OnCloseMetaObject())
		return false;

	if (!m_moduleObject->OnCloseMetaObject())
		return false;

	unregisterSelection();

	if (appData->DesignerMode()) {
		if (moduleManager->RemoveCompileModule(m_moduleObject)) {
			return IMetaObjectRefValue::OnCloseMetaObject();
		}
	}

	return IMetaObjectRefValue::OnCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectDocumentValue::OnCreateMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectDocumentValue::eFormObject
		&& m_defaultFormObject == wxNOT_FOUND)
	{
		m_defaultFormObject = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocumentValue::eFormList
		&& m_defaultFormList == wxNOT_FOUND)
	{
		m_defaultFormList = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocumentValue::eFormSelect
		&& m_defaultFormSelect == wxNOT_FOUND)
	{
		m_defaultFormSelect = metaForm->GetMetaID();
	}
}

void CMetaObjectDocumentValue::OnRemoveMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectDocumentValue::eFormObject
		&& m_defaultFormObject == metaForm->GetMetaID())
	{
		m_defaultFormObject = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocumentValue::eFormList
		&& m_defaultFormList == metaForm->GetMetaID())
	{
		m_defaultFormList = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectDocumentValue::eFormSelect
		&& m_defaultFormSelect == metaForm->GetMetaID())
	{
		m_defaultFormSelect = wxNOT_FOUND;
	}
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectDocumentValue::ReadProperty()
{
	IMetaObjectValue::ReadProperty();

	IObjectBase::SetPropertyValue("default_object", m_defaultFormObject);
	IObjectBase::SetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::SetPropertyValue("default_select", m_defaultFormSelect);
}

void CMetaObjectDocumentValue::SaveProperty()
{
	IMetaObjectValue::SaveProperty();

	IObjectBase::GetPropertyValue("default_object", m_defaultFormObject);
	IObjectBase::GetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::GetPropertyValue("default_select", m_defaultFormSelect);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectDocumentValue, "metaDocument", g_metaDocumentCLSID);