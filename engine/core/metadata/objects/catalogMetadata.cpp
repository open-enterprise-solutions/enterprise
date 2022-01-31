////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metadata
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "list/objectList.h"
#include "metadata/metadata.h"
#include "metadata/moduleManager/moduleManager.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectCatalogValue, IMetaObjectRefValue);

//********************************************************************************************
//*                                      metadata                                            *
//********************************************************************************************

CMetaObjectCatalogValue::CMetaObjectCatalogValue() : IMetaObjectRefValue(),
//default form 
m_defaultFormObject(wxNOT_FOUND), m_defaultFormList(wxNOT_FOUND), m_defaultFormSelect(wxNOT_FOUND)
{
	PropertyContainer *categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_object", PropertyType::PT_OPTION, &CMetaObjectCatalogValue::GetFormObject);
	categoryForm->AddProperty("default_list", PropertyType::PT_OPTION, &CMetaObjectCatalogValue::GetFormList);
	categoryForm->AddProperty("default_select", PropertyType::PT_OPTION, &CMetaObjectCatalogValue::GetFormSelect);
	m_category->AddCategory(categoryForm);

	//create default attributes
	m_attributeCode = CMetaDefaultAttributeObject::CreateString(wxT("code"), _("Code"), wxEmptyString, 8, true);
	m_attributeCode->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeCode->SetParent(this);
	AddChild(m_attributeCode);

	m_attributeName = CMetaDefaultAttributeObject::CreateString(wxT("name"), _("Name"), wxEmptyString, 150, true);
	m_attributeName->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeName->SetParent(this);
	AddChild(m_attributeName);

	m_attributeGroup = CMetaDefaultAttributeObject::CreateBoolean(wxT("group"), _("Group"), wxEmptyString);
	m_attributeGroup->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeGroup->SetParent(this);
	AddChild(m_attributeGroup);

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

CMetaObjectCatalogValue::~CMetaObjectCatalogValue()
{
	wxDELETE(m_attributeCode);
	wxDELETE(m_attributeName);
	wxDELETE(m_attributeGroup);

	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaFormObject *CMetaObjectCatalogValue::GetDefaultFormByID(form_identifier_t id)
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

IDataObjectSource *CMetaObjectCatalogValue::CreateObjectData(IMetaFormObject *metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject: return CreateObjectRefValue(); break;
	case eFormList: return new CDataObjectList(this, metaObject->GetTypeForm()); break;
	case eFormSelect: return new CDataObjectList(this, metaObject->GetTypeForm(), true); break;
	}

	return NULL;
}

IDataObjectRefValue *CMetaObjectCatalogValue::CreateGroupObjectRefValue()
{
	return new CObjectCatalogGroupValue(this);
}

IDataObjectRefValue *CMetaObjectCatalogValue::CreateGroupObjectRefValue(const Guid & guid)
{
	return new CObjectCatalogGroupValue(this, guid);
}

#include "appData.h"

IDataObjectRefValue *CMetaObjectCatalogValue::CreateObjectRefValue()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	IDataObjectRefValue *pDataRef = NULL;

	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return new CObjectCatalogValue(this);
		}
	}
	else {
		pDataRef = new CObjectCatalogValue(this);
	}

	return pDataRef;
}

IDataObjectRefValue *CMetaObjectCatalogValue::CreateObjectRefValue(const Guid &guid)
{
	return new CObjectCatalogValue(this, guid);
}

CValueForm *CMetaObjectCatalogValue::CreateObjectValue(IMetaFormObject *metaForm)
{
	return metaForm->GenerateFormAndRun(
		NULL, CreateObjectData(metaForm)
	);
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm *CMetaObjectCatalogValue::GetObjectForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalogValue::eFormObject == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalogValue::eFormObject);
	}

	if (defList == NULL) {
		IDataObjectValue *objectData = CreateObjectRefValue();
		CValueForm *valueForm = new CValueForm;
		valueForm->InitializeForm(ownerControl, NULL,
			objectData, formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectCatalogValue::eFormObject);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateObjectRefValue(), formGuid
	);
}

CValueForm *CMetaObjectCatalogValue::GetListForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalogValue::eFormList == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalogValue::eFormList);
	}

	if (defList == NULL) {
		CValueForm *valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CDataObjectList(this, CMetaObjectCatalogValue::eFormList), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectCatalogValue::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(ownerControl,
		new CDataObjectList(this, defList->GetTypeForm())
	);
}

CValueForm *CMetaObjectCatalogValue::GetSelectForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalogValue::eFormSelect == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalogValue::eFormSelect);
	}

	if (defList == NULL) {
		CValueForm *valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CDataObjectList(this, CMetaObjectCatalogValue::eFormSelect, true), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectCatalogValue::eFormSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, new CDataObjectList(this, defList->GetTypeForm(), true), formGuid
	);
}

OptionList *CMetaObjectCatalogValue::GetFormObject(Property *)
{
	OptionList *optlist = new OptionList();
	optlist->AddOption("<not selected>", wxNOT_FOUND);

	for (auto formObject : GetObjectForms())
	{
		if (eFormObject == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList *CMetaObjectCatalogValue::GetFormList(Property *)
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

OptionList *CMetaObjectCatalogValue::GetFormSelect(Property *)
{
	OptionList *optlist = new OptionList();
	optlist->AddOption("<not selected>", wxNOT_FOUND);

	for (auto formObject : GetObjectForms()) {
		if (eFormSelect == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

wxString CMetaObjectCatalogValue::GetDescription(const IObjectValueInfo *objValue) const
{
	CValue vName = objValue->GetValueByMetaID(m_attributeName->GetMetaID());

	wxString decr;
	decr << vName.GetString();
	return decr;
}

std::vector<IMetaAttributeObject *> CMetaObjectCatalogValue::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject *> attributes;

	attributes.push_back(m_attributeCode);
	attributes.push_back(m_attributeName);

	for (auto metaObject : m_aMetaObjects)
	{
		if (metaObject->GetClsid() == g_metaAttributeCLSID)
			attributes.push_back(dynamic_cast<CMetaAttributeObject *>(metaObject));
	}

	attributes.push_back(m_attributeGroup);

	return attributes;
}

std::vector<IMetaAttributeObject*> CMetaObjectCatalogValue::GetSearchedAttributes() const
{
	std::vector<IMetaAttributeObject *> attributes;

	attributes.push_back(m_attributeCode);
	attributes.push_back(m_attributeName);

	return attributes;

}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool CMetaObjectCatalogValue::LoadData(CMemoryReader &dataReader)
{
	//load default attributes:
	m_attributeCode->LoadMeta(dataReader);
	m_attributeName->LoadMeta(dataReader);
	m_attributeGroup->LoadMeta(dataReader);

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	//load default form 
	m_defaultFormObject = dataReader.r_u32();
	m_defaultFormList = dataReader.r_u32();
	m_defaultFormSelect = dataReader.r_u32();

	return IMetaObjectRefValue::LoadData(dataReader);
}

bool CMetaObjectCatalogValue::SaveData(CMemoryWriter &dataWritter)
{
	//save default attributes:
	m_attributeCode->SaveMeta(dataWritter);
	m_attributeName->SaveMeta(dataWritter);
	m_attributeGroup->SaveMeta(dataWritter);

	//save object module
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
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectCatalogValue::ReadProperty()
{
	IMetaObjectValue::ReadProperty();

	IObjectBase::SetPropertyValue("default_object", m_defaultFormObject);
	IObjectBase::SetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::SetPropertyValue("default_select", m_defaultFormSelect);
}

void CMetaObjectCatalogValue::SaveProperty()
{
	IMetaObjectValue::SaveProperty();

	IObjectBase::GetPropertyValue("default_object", m_defaultFormObject);
	IObjectBase::GetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::GetPropertyValue("default_select", m_defaultFormSelect);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool CMetaObjectCatalogValue::OnCreateMetaObject(IMetadata *metaData)
{
	if (!IMetaObjectRefValue::OnCreateMetaObject(metaData))
		return false;

	return m_attributeCode->OnCreateMetaObject(metaData) &&
		m_attributeName->OnCreateMetaObject(metaData) &&
		m_attributeGroup->OnCreateMetaObject(metaData) &&
		m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectCatalogValue::OnLoadMetaObject(IMetadata *metaData)
{
	if (!m_attributeCode->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeName->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeGroup->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRefValue::OnLoadMetaObject(metaData);
}

bool CMetaObjectCatalogValue::OnSaveMetaObject()
{
	if (!m_attributeCode->OnSaveMetaObject())
		return false;

	if (!m_attributeName->OnSaveMetaObject())
		return false;

	if (!m_attributeGroup->OnSaveMetaObject())
		return false;

	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return IMetaObjectRefValue::OnSaveMetaObject();
}

bool CMetaObjectCatalogValue::OnDeleteMetaObject()
{
	if (!m_attributeCode->OnDeleteMetaObject())
		return false;

	if (!m_attributeName->OnDeleteMetaObject())
		return false;

	if (!m_attributeGroup->OnDeleteMetaObject())
		return false;

	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectRefValue::OnDeleteMetaObject();
}

bool CMetaObjectCatalogValue::OnReloadMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CObjectCatalogValue *pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

#include "metadata/singleMetaTypes.h"

bool CMetaObjectCatalogValue::OnRunMetaObject(int flags)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_attributeCode->OnRunMetaObject(flags))
		return false;

	if (!m_attributeName->OnRunMetaObject(flags))
		return false;

	if (!m_attributeGroup->OnRunMetaObject(flags))
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

bool CMetaObjectCatalogValue::OnCloseMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_attributeCode->OnCloseMetaObject())
		return false;

	if (!m_attributeName->OnCloseMetaObject())
		return false;

	if (!m_attributeGroup->OnCloseMetaObject())
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

void CMetaObjectCatalogValue::OnCreateMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectCatalogValue::eFormObject
		&& m_defaultFormObject == wxNOT_FOUND)
	{
		m_defaultFormObject = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalogValue::eFormList
		&& m_defaultFormList == wxNOT_FOUND)
	{
		m_defaultFormList = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalogValue::eFormSelect
		&& m_defaultFormSelect == wxNOT_FOUND)
	{
		m_defaultFormSelect = metaForm->GetMetaID();
	}
}

void CMetaObjectCatalogValue::OnRemoveMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectCatalogValue::eFormObject
		&& m_defaultFormObject == metaForm->GetMetaID())
	{
		m_defaultFormObject = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalogValue::eFormList
		&& m_defaultFormList == metaForm->GetMetaID())
	{
		m_defaultFormList = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalogValue::eFormSelect
		&& m_defaultFormSelect == metaForm->GetMetaID())
	{
		m_defaultFormSelect = wxNOT_FOUND;
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectCatalogValue, "metaCatalog", g_metaCatalogCLSID);