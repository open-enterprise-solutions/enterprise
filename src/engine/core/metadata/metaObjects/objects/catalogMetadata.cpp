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

///////////////////////////////////////////////////////////////////////////////////////////////

wxString wxVariantOwnerData::MakeString() const
{
	wxString description;
	for (auto clsid : m_ownerData) {
		IMetaObjectRecordDataRef* record =
			dynamic_cast<IMetaObjectRecordDataRef*>(m_metaData->GetMetaObject(clsid));
		if (record == NULL || !record->IsAllowed())
			continue;
		if (description.IsEmpty()) {
			description = record->GetName();
		}
		else {
			description = description +
				", " + record->GetName();
		}
	}
	return description;
}

///////////////////////////////////////////////////////////////////////////////////////////////

#include "metadata/metadata.h"

bool CMetaObjectCatalog::ownerData_t::LoadData(CMemoryReader& dataReader)
{
	unsigned int count = dataReader.r_u32();

	for (unsigned int i = 0; i < count; i++) {
		meta_identifier_t record_id = dataReader.r_u32();
		m_ownerData.insert(record_id);
	}

	return true;
}

bool CMetaObjectCatalog::ownerData_t::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_u32(m_ownerData.size());

	for (auto record_id : m_ownerData) {
		dataWritter.w_u32(record_id);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

#include "metadata/metadata.h"

bool CMetaObjectCatalog::ownerData_t::LoadFromVariant(const wxVariant& variant)
{
	wxVariantOwnerData* list =
		dynamic_cast<wxVariantOwnerData*>(variant.GetData());

	if (list == NULL)
		return false;

	m_ownerData.clear();

	for (unsigned int idx = 0; idx < list->GetCount(); idx++) {
		m_ownerData.insert(
			list->GetById(idx)
		);
	}

	return true;
}

void CMetaObjectCatalog::ownerData_t::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantOwnerData* list = new wxVariantOwnerData(metaData);

	for (auto clsid : m_ownerData) {
		list->AppendRecord(clsid);
	}

	variant = list;
}

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectCatalog, IMetaObjectRecordDataMutableRef);

//********************************************************************************************
//*                                      metadata                                            *
//********************************************************************************************

CMetaObjectCatalog::CMetaObjectCatalog() : IMetaObjectRecordDataMutableRef(),
//default form 
m_defaultFormObject(wxNOT_FOUND), m_defaultFormList(wxNOT_FOUND), m_defaultFormSelect(wxNOT_FOUND)
{
	PropertyContainer* categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_object", PropertyType::PT_OPTION, &CMetaObjectCatalog::GetFormObject);
	categoryForm->AddProperty("default_list", PropertyType::PT_OPTION, &CMetaObjectCatalog::GetFormList);
	categoryForm->AddProperty("default_select", PropertyType::PT_OPTION, &CMetaObjectCatalog::GetFormSelect);
	m_category->AddCategory(categoryForm);

	PropertyContainer* categoryData = IObjectBase::CreatePropertyContainer("Data");

	categoryData->AddProperty("owners", PropertyType::PT_OWNER_SELECT);

	m_category->AddCategory(categoryData);

	//create default attributes
	m_attributeCode = CMetaDefaultAttributeObject::CreateString(wxT("code"), _("Code"), wxEmptyString, 8, true);
	m_attributeCode->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeCode->SetParent(this);
	AddChild(m_attributeCode);

	m_attributeDescription = CMetaDefaultAttributeObject::CreateString(wxT("description"), _("Description"), wxEmptyString, 150, true);
	m_attributeDescription->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeDescription->SetParent(this);
	AddChild(m_attributeDescription);

	m_attributeOwner = CMetaDefaultAttributeObject::CreateEmptyType(wxT("owner"), _("Owner"), wxEmptyString, true);
	m_attributeOwner->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeOwner->SetParent(this);
	AddChild(m_attributeOwner);

	m_attributeParent = CMetaDefaultAttributeObject::CreateEmptyType(wxT("parent"), _("Parent"), wxEmptyString);
	m_attributeParent->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeParent->SetParent(this);
	AddChild(m_attributeParent);

	m_attributeIsFolder = CMetaDefaultAttributeObject::CreateBoolean(wxT("isFolder"), _("Is folder"), wxEmptyString);
	m_attributeIsFolder->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeIsFolder->SetParent(this);
	AddChild(m_attributeIsFolder);

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

CMetaObjectCatalog::~CMetaObjectCatalog()
{
	wxDELETE(m_attributeCode);
	wxDELETE(m_attributeDescription);
	wxDELETE(m_attributeOwner);
	wxDELETE(m_attributeParent);
	wxDELETE(m_attributeIsFolder);

	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaFormObject* CMetaObjectCatalog::GetDefaultFormByID(const form_identifier_t& id)
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

ISourceDataObject* CMetaObjectCatalog::CreateObjectData(IMetaFormObject* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject: return CreateObjectRefValue(); break;
	case eFormList: return new CListDataObjectRef(this, metaObject->GetTypeForm()); break;
	case eFormSelect: return new CListDataObjectRef(this, metaObject->GetTypeForm(), true); break;
	}

	return NULL;
}

IRecordDataObjectRef* CMetaObjectCatalog::CreateGroupObjectRefValue()
{
	return new CObjectCatalogGroup(this);
}

IRecordDataObjectRef* CMetaObjectCatalog::CreateGroupObjectRefValue(const Guid& guid)
{
	return new CObjectCatalogGroup(this, guid);
}

#include "appData.h"

IRecordDataObjectRef* CMetaObjectCatalog::CreateObjectRefValue()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	IRecordDataObjectRef* pDataRef = NULL;

	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return new CObjectCatalog(this);
		}
	}
	else {
		pDataRef = new CObjectCatalog(this);
	}

	return pDataRef;
}

IRecordDataObjectRef* CMetaObjectCatalog::CreateObjectRefValue(const Guid& guid)
{
	return new CObjectCatalog(this, guid);
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm* CMetaObjectCatalog::GetObjectForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormObject == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormObject);
	}

	if (defList == NULL) {
		IRecordDataObject* objectData = CreateObjectRefValue();
		CValueForm* valueForm = new CValueForm;
		valueForm->InitializeForm(ownerControl, NULL,
			objectData, formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectCatalog::eFormObject);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, CreateObjectRefValue(), formGuid
	);
}

CValueForm* CMetaObjectCatalog::GetListForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormList == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormList);
	}

	if (defList == NULL) {
		CValueForm* valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CListDataObjectRef(this, CMetaObjectCatalog::eFormList), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectCatalog::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(ownerControl,
		new CListDataObjectRef(this, defList->GetTypeForm())
	);
}

CValueForm* CMetaObjectCatalog::GetSelectForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectCatalog::eFormSelect == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectCatalog::eFormSelect);
	}

	if (defList == NULL) {
		CValueForm* valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CListDataObjectRef(this, CMetaObjectCatalog::eFormSelect, true), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectCatalog::eFormSelect);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, new CListDataObjectRef(this, defList->GetTypeForm(), true), formGuid
	);
}

OptionList* CMetaObjectCatalog::GetFormObject(Property*)
{
	OptionList* optlist = new OptionList();
	optlist->AddOption(_("<not selected>"), wxNOT_FOUND);

	for (auto formObject : GetObjectForms())
	{
		if (eFormObject == formObject->GetTypeForm()) {
			optlist->AddOption(formObject->GetName(), formObject->GetMetaID());
		}
	}

	return optlist;
}

OptionList* CMetaObjectCatalog::GetFormList(Property*)
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

OptionList* CMetaObjectCatalog::GetFormSelect(Property*)
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

wxString CMetaObjectCatalog::GetDescription(const IObjectValueInfo* objValue) const
{
	CValue vName = objValue->GetValueByMetaID(m_attributeDescription->GetMetaID());

	wxString decr;
	decr << vName.GetString();
	return decr;
}

std::vector<IMetaAttributeObject*> CMetaObjectCatalog::GetDefaultAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	attributes.push_back(m_attributeCode);
	attributes.push_back(m_attributeDescription);
	attributes.push_back(m_attributeOwner);
	attributes.push_back(m_attributeParent);
	attributes.push_back(m_attributeIsFolder);
	attributes.push_back(m_attributeReference);

	return attributes;
}

std::vector<IMetaAttributeObject*> CMetaObjectCatalog::GetSearchedAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;

	attributes.push_back(m_attributeCode);
	attributes.push_back(m_attributeDescription);

	return attributes;
}

//////////////////////////////////////////////////////////////////////////////

#include "metadata/singleMetaTypes.h"

bool CMetaObjectCatalog::LoadFromVariant(const wxVariant& variant)
{
	std::set<meta_identifier_t> prevRecords = m_ownerData.GetData();

	if (m_ownerData.LoadFromVariant(variant)) {

		std::set<meta_identifier_t> records = m_ownerData.GetData();

		for (auto record : prevRecords) {

			auto foundedIt = records.find(record);
			if (foundedIt != records.end())
				continue;

			CMetaObjectCatalog* catalog = wxDynamicCast(
				m_metaData->GetMetaObject(record), CMetaObjectCatalog
			);

			if (catalog != NULL) {
				IMetaTypeObjectValueSingle* so =
					m_metaData->GetTypeObject(catalog, eMetaObjectType::enReference);
				wxASSERT(so);
				m_attributeOwner->ClearMetatype(
					so->GetClassType()
				);
			}
		}

		for (auto record : records) {

			auto foundedIt = prevRecords.find(record);
			if (foundedIt != prevRecords.end())
				continue;

			CMetaObjectCatalog* catalog = wxDynamicCast(
				m_metaData->GetMetaObject(record), CMetaObjectCatalog
			);

			if (catalog != NULL) {
				IMetaTypeObjectValueSingle* so =
					m_metaData->GetTypeObject(catalog, eMetaObjectType::enReference);
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

void CMetaObjectCatalog::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	m_ownerData.SaveToVariant(variant, metaData);
}

//***************************************************************************
//*                       Save & load metadata                              *
//***************************************************************************

bool CMetaObjectCatalog::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeCode->LoadMeta(dataReader);
	m_attributeDescription->LoadMeta(dataReader);
	m_attributeOwner->LoadMeta(dataReader);
	m_attributeParent->LoadMeta(dataReader);
	m_attributeIsFolder->LoadMeta(dataReader);

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	//load default form 
	m_defaultFormObject = dataReader.r_u32();
	m_defaultFormList = dataReader.r_u32();
	m_defaultFormSelect = dataReader.r_u32();

	if (!m_ownerData.LoadData(dataReader))
		return false;

	return IMetaObjectRecordDataMutableRef::LoadData(dataReader);
}

bool CMetaObjectCatalog::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeCode->SaveMeta(dataWritter);
	m_attributeDescription->SaveMeta(dataWritter);
	m_attributeOwner->SaveMeta(dataWritter);
	m_attributeParent->SaveMeta(dataWritter);
	m_attributeIsFolder->SaveMeta(dataWritter);

	//save object module
	m_moduleObject->SaveMeta(dataWritter);
	m_moduleManager->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_u32(m_defaultFormObject);
	dataWritter.w_u32(m_defaultFormList);
	dataWritter.w_u32(m_defaultFormSelect);

	if (!m_ownerData.SaveData(dataWritter))
		return false;

	//create or update table:
	return IMetaObjectRecordDataMutableRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectCatalog::ReadProperty()
{
	IMetaObjectRecordData::ReadProperty();

	IObjectBase::SetPropertyValue("default_object", m_defaultFormObject);
	IObjectBase::SetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::SetPropertyValue("default_select", m_defaultFormSelect);

	CMetaObjectCatalog::SaveToVariant(
		GetPropertyAsVariant("owners"), m_metaData
	);
}

void CMetaObjectCatalog::SaveProperty()
{
	IMetaObjectRecordData::SaveProperty();

	IObjectBase::GetPropertyValue("default_object", m_defaultFormObject);
	IObjectBase::GetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::GetPropertyValue("default_select", m_defaultFormSelect);

	CMetaObjectCatalog::LoadFromVariant(
		GetPropertyAsVariant("owners")
	);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool CMetaObjectCatalog::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData))
		return false;

	return m_attributeCode->OnCreateMetaObject(metaData) &&
		m_attributeDescription->OnCreateMetaObject(metaData) &&
		m_attributeOwner->OnCreateMetaObject(metaData) &&
		m_attributeParent->OnCreateMetaObject(metaData) &&
		m_attributeIsFolder->OnCreateMetaObject(metaData) &&
		m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectCatalog::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeCode->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeDescription->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeOwner->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeParent->OnLoadMetaObject(metaData))
		return false;

	if (!m_attributeIsFolder->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRecordDataMutableRef::OnLoadMetaObject(metaData);
}

bool CMetaObjectCatalog::OnSaveMetaObject()
{
	if (!m_attributeCode->OnSaveMetaObject())
		return false;

	if (!m_attributeDescription->OnSaveMetaObject())
		return false;

	if (!m_attributeOwner->OnSaveMetaObject())
		return false;

	if (!m_attributeParent->OnSaveMetaObject())
		return false;

	if (!m_attributeIsFolder->OnSaveMetaObject())
		return false;

	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return IMetaObjectRecordDataMutableRef::OnSaveMetaObject();
}

bool CMetaObjectCatalog::OnDeleteMetaObject()
{
	if (!m_attributeCode->OnDeleteMetaObject())
		return false;

	if (!m_attributeDescription->OnDeleteMetaObject())
		return false;

	if (!m_attributeOwner->OnDeleteMetaObject())
		return false;

	if (!m_attributeParent->OnDeleteMetaObject())
		return false;

	if (!m_attributeIsFolder->OnDeleteMetaObject())
		return false;

	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectRecordDataMutableRef::OnDeleteMetaObject();
}

bool CMetaObjectCatalog::OnReloadMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CObjectCatalog* pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

#include "metadata/singleMetaTypes.h"

bool CMetaObjectCatalog::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeCode->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeDescription->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeOwner->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeParent->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_attributeIsFolder->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleManager->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleObject->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	if (!IMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	IMetaTypeObjectValueSingle* singleObject =
		m_metaData->GetTypeObject(this, eMetaObjectType::enReference);

	if (singleObject != NULL && !m_attributeParent->ContainType(singleObject->GetClassType())) {
		m_attributeParent->SetDefaultMetatype(singleObject->GetClassType());
	}

	return true;
}

bool CMetaObjectCatalog::OnAfterRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags))
			return moduleManager->AddCompileModule(m_moduleObject, CreateObjectRefValue());

		return false;
	}

	return IMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags);
}

bool CMetaObjectCatalog::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject())
			return moduleManager->RemoveCompileModule(m_moduleObject);

		return false;
	}

	return IMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject();
}

bool CMetaObjectCatalog::OnAfterCloseMetaObject()
{
	if (!m_attributeCode->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeDescription->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeOwner->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeParent->OnAfterCloseMetaObject())
		return false;

	if (!m_attributeIsFolder->OnAfterCloseMetaObject())
		return false;

	if (!m_moduleManager->OnAfterCloseMetaObject())
		return false;

	if (!m_moduleObject->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	/////////////////////////////////////////////////////////////

	for (auto record : m_ownerData.GetData()) {

		CMetaObjectCatalog* catalog = wxDynamicCast(
			m_metaData->GetMetaObject(record), CMetaObjectCatalog
		);

		if (catalog != NULL) {
			IMetaTypeObjectValueSingle *so = 
				m_metaData->GetTypeObject(catalog, eMetaObjectType::enReference);
			wxASSERT(so); 
			m_attributeOwner->ClearMetatype(
				so->GetClassType()
			);
		}
	}

	/////////////////////////////////////////////////////////////

	return IMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void CMetaObjectCatalog::OnCreateMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormObject
		&& m_defaultFormObject == wxNOT_FOUND)
	{
		m_defaultFormObject = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormList
		&& m_defaultFormList == wxNOT_FOUND)
	{
		m_defaultFormList = metaForm->GetMetaID();
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormSelect
		&& m_defaultFormSelect == wxNOT_FOUND)
	{
		m_defaultFormSelect = metaForm->GetMetaID();
	}
}

void CMetaObjectCatalog::OnRemoveMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormObject
		&& m_defaultFormObject == metaForm->GetMetaID())
	{
		m_defaultFormObject = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormList
		&& m_defaultFormList == metaForm->GetMetaID())
	{
		m_defaultFormList = wxNOT_FOUND;
	}
	else if (metaForm->GetTypeForm() == CMetaObjectCatalog::eFormSelect
		&& m_defaultFormSelect == metaForm->GetMetaID())
	{
		m_defaultFormSelect = wxNOT_FOUND;
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectCatalog, "catalog", g_metaCatalogCLSID);