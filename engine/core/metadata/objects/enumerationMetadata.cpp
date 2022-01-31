////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration metadata
////////////////////////////////////////////////////////////////////////////

#include "enumeration.h"
#include "metadata/metadata.h"
#include "list/objectList.h"

#define managerModule wxT("managerModule")

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectEnumerationValue, IMetaObjectRefValue)

//********************************************************************************************
//*                                      metadata                                            *
//********************************************************************************************

CMetaObjectEnumerationValue::CMetaObjectEnumerationValue() : IMetaObjectRefValue(),
m_defaultFormList(wxNOT_FOUND), m_defaultFormSelect(wxNOT_FOUND)
{
	PropertyContainer *categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_list", PropertyType::PT_OPTION, &CMetaObjectEnumerationValue::GetFormList);
	categoryForm->AddProperty("default_select", PropertyType::PT_OPTION, &CMetaObjectEnumerationValue::GetFormSelect);

	m_category->AddCategory(categoryForm);

	m_moduleManager = new CMetaManagerModuleObject(managerModule);
	m_moduleManager->SetClsid(g_metaManagerCLSID);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectEnumerationValue::~CMetaObjectEnumerationValue()
{
	wxDELETE(m_moduleManager);
}

CMetaFormObject *CMetaObjectEnumerationValue::GetDefaultFormByID(form_identifier_t id)
{
	if (id == eFormList
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

IDataObjectSource *CMetaObjectEnumerationValue::CreateObjectData(IMetaFormObject *metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList: return new CDataObjectList(this, metaObject->GetTypeForm()); break;
	case eFormSelect: return new CDataObjectList(this, metaObject->GetTypeForm(), true); break;
	}

	return NULL;
}

CValueForm *CMetaObjectEnumerationValue::CreateObjectValue(IMetaFormObject *metaForm)
{
	return metaForm->GenerateFormAndRun(
		NULL, CreateObjectData(metaForm)
	);
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm *CMetaObjectEnumerationValue::GetObjectForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	return NULL;
}

CValueForm *CMetaObjectEnumerationValue::GetListForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectEnumerationValue::eFormList == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectEnumerationValue::eFormList);
	}

	if (defList == NULL) {
		CValueForm *valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CDataObjectList(this, CMetaObjectEnumerationValue::eFormList), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectEnumerationValue::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(ownerControl,
		new CDataObjectList(this, defList->GetTypeForm()), formGuid
	);
}

CValueForm *CMetaObjectEnumerationValue::GetSelectForm(const wxString &formName, IValueFrame *ownerControl, const Guid &formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectEnumerationValue::eFormSelect == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectEnumerationValue::eFormSelect);
	}

	if (defList == NULL) {
		CValueForm *valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CDataObjectList(this, CMetaObjectEnumerationValue::eFormSelect, true), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectEnumerationValue::eFormSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(ownerControl,
		new CDataObjectList(this, defList->GetTypeForm(), true), formGuid
	);
}

OptionList *CMetaObjectEnumerationValue::GetFormList(Property *)
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

OptionList *CMetaObjectEnumerationValue::GetFormSelect(Property *)
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

wxString CMetaObjectEnumerationValue::GetDescription(const IObjectValueInfo *objValue) const
{
	for (auto eEnum : GetObjectEnums()) {
		if (objValue->GetGuid() == eEnum->GetGuid()) {
			return eEnum->GetSynonym();
		}
	}

	return wxEmptyString;
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool CMetaObjectEnumerationValue::LoadData(CMemoryReader &dataReader)
{
	//Load object module
	m_moduleManager->LoadMeta(dataReader);

	//save default form 
	m_defaultFormList = dataReader.r_u32();
	m_defaultFormSelect = dataReader.r_u32();

	return IMetaObjectRefValue::LoadData(dataReader);
}

bool CMetaObjectEnumerationValue::SaveData(CMemoryWriter &dataWritter)
{
	//Save object module
	m_moduleManager->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_u32(m_defaultFormList);
	dataWritter.w_u32(m_defaultFormSelect);

	//create or update table:
	return IMetaObjectRefValue::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool CMetaObjectEnumerationValue::OnCreateMetaObject(IMetadata *metaData)
{
	if (!IMetaObjectRefValue::OnCreateMetaObject(metaData))
		return false;

	return m_moduleManager->OnCreateMetaObject(metaData);
}

bool CMetaObjectEnumerationValue::OnLoadMetaObject(IMetadata *metaData)
{
	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRefValue::OnLoadMetaObject(metaData);
}

bool CMetaObjectEnumerationValue::OnSaveMetaObject()
{
	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	return IMetaObjectRefValue::OnSaveMetaObject();
}

bool CMetaObjectEnumerationValue::OnDeleteMetaObject()
{
	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	return IMetaObjectRefValue::OnDeleteMetaObject();
}

bool CMetaObjectEnumerationValue::OnReloadMetaObject()
{
	return true;
}

bool CMetaObjectEnumerationValue::OnRunMetaObject(int flags)
{
	if (!m_moduleManager->OnRunMetaObject(flags))
		return false;

	return IMetaObjectRefValue::OnRunMetaObject(flags);
}

bool CMetaObjectEnumerationValue::OnCloseMetaObject()
{
	if (!m_moduleManager->OnCloseMetaObject())
		return false;

	return IMetaObjectRefValue::OnCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectEnumerationValue::OnCreateMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectEnumerationValue::eFormList
		&& m_defaultFormList == wxNOT_FOUND)
	{
		m_defaultFormList = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectEnumerationValue::eFormSelect
		&& m_defaultFormSelect == wxNOT_FOUND)
	{
		m_defaultFormSelect = metaForm->GetMetaID();
	}
}

void CMetaObjectEnumerationValue::OnRemoveMetaForm(IMetaFormObject *metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectEnumerationValue::eFormList
		&& m_defaultFormList == metaForm->GetMetaID())
	{
		m_defaultFormList = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectEnumerationValue::eFormSelect
		&& m_defaultFormSelect == metaForm->GetMetaID())
	{
		m_defaultFormSelect = wxNOT_FOUND;
	}
}

std::vector<IMetaAttributeObject*> CMetaObjectEnumerationValue::GetSearchedAttributes() const
{
	std::vector<IMetaAttributeObject *> attributes;
	return attributes;
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectEnumerationValue::ReadProperty()
{
	IMetaObjectValue::ReadProperty();

	IObjectBase::SetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::SetPropertyValue("default_select", m_defaultFormSelect);
}

void CMetaObjectEnumerationValue::SaveProperty()
{
	IMetaObjectValue::SaveProperty();

	IObjectBase::GetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::GetPropertyValue("default_select", m_defaultFormSelect);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectEnumerationValue, "metaEnumeration", g_metaEnumerationCLSID);