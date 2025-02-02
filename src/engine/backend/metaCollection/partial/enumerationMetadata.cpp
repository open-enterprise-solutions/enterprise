////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration metaData
////////////////////////////////////////////////////////////////////////////

#include "enumeration.h"
#include "backend/metaData.h"
#include "list/objectList.h"

#define managerModule wxT("managerModule")

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectEnumeration, IMetaObjectRecordDataEnumRef)

//********************************************************************************************

#include "databaseLayer/databaseLayer.h"
#include "backend/appData.h"

//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

CMetaObjectEnumeration::CMetaObjectEnumeration() : IMetaObjectRecordDataEnumRef()
{
	m_propertyQuickChoice->SetValue(true);

	m_moduleManager = new CMetaObjectManagerModule(managerModule);

	//set child/parent
	m_moduleManager->SetParent(this);
	AddChild(m_moduleManager);
}

CMetaObjectEnumeration::~CMetaObjectEnumeration()
{
	wxDELETE(m_moduleManager);
}

CMetaObjectForm* CMetaObjectEnumeration::GetDefaultFormByID(const form_identifier_t& id)
{
	if (id == eFormList
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

ISourceDataObject* CMetaObjectEnumeration::CreateObjectData(IMetaObjectForm* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList: return
		new CListDataObjectEnumRef(this, metaObject->GetTypeForm()); break;
	case eFormSelect: return
		new CListDataObjectEnumRef(this, metaObject->GetTypeForm(), true); 
		break;
	}

	return nullptr;
}



IBackendValueForm* CMetaObjectEnumeration::GetListForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectEnumeration::eFormList == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectEnumeration::eFormList);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			new CListDataObjectEnumRef(this, CMetaObjectEnumeration::eFormList), formGuid
		);
		valueForm->BuildForm(CMetaObjectEnumeration::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(ownerControl,
		new CListDataObjectEnumRef(this, defList->GetTypeForm()), formGuid
	);
}

IBackendValueForm* CMetaObjectEnumeration::GetSelectForm(const wxString& formName, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaObjectForm* defList = nullptr;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectEnumeration::eFormSelect == metaForm->GetTypeForm()
				&& stringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectEnumeration::eFormSelect);
	}

	if (defList == nullptr) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(ownerControl, nullptr,
			new CListDataObjectEnumRef(this, CMetaObjectEnumeration::eFormSelect, true), formGuid
		);
		valueForm->BuildForm(CMetaObjectEnumeration::eFormSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(ownerControl,
		new CListDataObjectEnumRef(this, defList->GetTypeForm(), true), formGuid
	);
}

OptionList* CMetaObjectEnumeration::GetFormList(PropertyOption*)
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

OptionList* CMetaObjectEnumeration::GetFormSelect(PropertyOption*)
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

wxString CMetaObjectEnumeration::GetDataPresentation(const IObjectValueInfo* objValue) const
{
	for (auto &obj : GetObjectEnums()) {
		if (objValue->GetGuid() == obj->GetGuid()) {
			return obj->GetSynonym();
		}
	}
	return wxEmptyString;
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool CMetaObjectEnumeration::LoadData(CMemoryReader& dataReader)
{
	//Load object module
	m_moduleManager->LoadMeta(dataReader);

	//save default form 
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	return IMetaObjectRecordDataEnumRef::LoadData(dataReader);
}

bool CMetaObjectEnumeration::SaveData(CMemoryWriter& dataWritter)
{
	//Save object module
	m_moduleManager->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()));

	//create or update table:
	return IMetaObjectRecordDataEnumRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool CMetaObjectEnumeration::OnCreateMetaObject(IMetaData* metaData)
{
	if (!IMetaObjectRecordDataEnumRef::OnCreateMetaObject(metaData))
		return false;

	return m_moduleManager->OnCreateMetaObject(metaData);
}

bool CMetaObjectEnumeration::OnLoadMetaObject(IMetaData* metaData)
{
	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataEnumRef::OnLoadMetaObject(metaData);
}

bool CMetaObjectEnumeration::OnSaveMetaObject()
{
	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataEnumRef::OnSaveMetaObject();
}

bool CMetaObjectEnumeration::OnDeleteMetaObject()
{
	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataEnumRef::OnDeleteMetaObject();
}

bool CMetaObjectEnumeration::OnReloadMetaObject()
{
	return true;
}

bool CMetaObjectEnumeration::OnBeforeRunMetaObject(int flags)
{
	if (!m_moduleManager->OnBeforeRunMetaObject(flags))
		return false;

	return IMetaObjectRecordDataEnumRef::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectEnumeration::OnAfterCloseMetaObject()
{
	if (!m_moduleManager->OnAfterCloseMetaObject())
		return false;

	return IMetaObjectRecordDataEnumRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectEnumeration::OnCreateFormObject(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectEnumeration::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectEnumeration::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
}

void CMetaObjectEnumeration::OnRemoveMetaForm(IMetaObjectForm* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectEnumeration::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == CMetaObjectEnumeration::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
}

std::vector<IMetaObjectAttribute*> CMetaObjectEnumeration::GetDefaultAttributes() const
{
	std::vector<IMetaObjectAttribute*> attributes;
	attributes.push_back(m_attributeReference);
	return attributes;
}

std::vector<IMetaObjectAttribute*> CMetaObjectEnumeration::GetSearchedAttributes() const
{
	return std::vector<IMetaObjectAttribute*>();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectEnumeration, "enumeration", g_metaEnumerationCLSID);