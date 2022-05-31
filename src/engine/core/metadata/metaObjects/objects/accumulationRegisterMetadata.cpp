#include "accumulationRegister.h"
#include "list/objectList.h"
#include "metadata/metadata.h"
#include "metadata/moduleManager/moduleManager.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueEnumAccumulationRegisterRecordType, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectAccumulationRegister, IMetaObjectRegisterData);

/////////////////////////////////////////////////////////////////////////

CValue CValueEnumAccumulationRegisterRecordType::CreateDefEnumValue()
{
	CValue retValue = new CValueEnumAccumulationRegisterRecordType(eRecordType::eExpense);

	if (retValue.Init()) {
		IEnumerationWrapper* enumVal = NULL;
		if (retValue.ConvertToValue(enumVal)) {
			return enumVal->GetEnumVariantValue();
		}
	}

	return retValue;
}

/////////////////////////////////////////////////////////////////////////

CMetaObjectAccumulationRegister::CMetaObjectAccumulationRegister() : IMetaObjectRegisterData(),
m_defaultFormList(wxNOT_FOUND),
m_registerType(eRegisterType::eBalances)
{
	PropertyContainer* categoryForm = IObjectBase::CreatePropertyContainer("DefaultForms");
	categoryForm->AddProperty("default_list", PropertyType::PT_OPTION, &CMetaObjectAccumulationRegister::GetFormList);
	m_category->AddCategory(categoryForm);

	PropertyContainer* categoryData = IObjectBase::CreatePropertyContainer("Data");
	categoryData->AddProperty("register_type", PropertyType::PT_OPTION, &CMetaObjectAccumulationRegister::GetRegisterType);
	m_category->AddCategory(categoryData);

	//create default attributes
	m_attributeRecordType = CMetaDefaultAttributeObject::CreateSpecialType(wxT("recordType"), _("Record type"), wxEmptyString, g_enumRecordTypeCLSID, false, CValueEnumAccumulationRegisterRecordType::CreateDefEnumValue());
	m_attributeRecordType->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_attributeRecordType->SetParent(this);
	AddChild(m_attributeRecordType);

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

CMetaObjectAccumulationRegister::~CMetaObjectAccumulationRegister()
{
	wxDELETE(m_attributeRecordType);

	wxDELETE(m_moduleObject);
	wxDELETE(m_moduleManager);
}

CMetaFormObject* CMetaObjectAccumulationRegister::GetDefaultFormByID(const form_identifier_t& id)
{
	if (id == eFormList && m_defaultFormList != wxNOT_FOUND) {
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

CValueForm* CMetaObjectAccumulationRegister::GetListForm(const wxString& formName, IValueFrame* ownerControl, const CUniqueKey& formGuid)
{
	CMetaFormObject* defList = NULL;

	if (!formName.IsEmpty()) {
		for (auto metaForm : GetObjectForms()) {
			if (CMetaObjectAccumulationRegister::eFormList == metaForm->GetTypeForm()
				&& StringUtils::CompareString(formName, metaForm->GetName())) {
				defList = metaForm; break;
			}
		}
		wxASSERT(defList);
	}
	else {
		defList = GetDefaultFormByID(CMetaObjectAccumulationRegister::eFormList);
	}

	if (defList == NULL) {
		CValueForm* valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, NULL,
			new CListRegisterObject(this, CMetaObjectAccumulationRegister::eFormList), formGuid
		);
		valueForm->ReadProperty();
		valueForm->BuildForm(CMetaObjectAccumulationRegister::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, new CListRegisterObject(this, defList->GetTypeForm()), formGuid
	);

	return NULL;
}

/////////////////////////////////////////////////////////////////////////////

OptionList* CMetaObjectAccumulationRegister::GetFormList(Property*)
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
//*                       Save & load metadata                              *
//***************************************************************************

bool CMetaObjectAccumulationRegister::LoadData(CMemoryReader& dataReader)
{
	//load default attributes:
	m_attributeRecordType->LoadMeta(dataReader);

	//load default form 
	m_defaultFormList = dataReader.r_u32();

	//load data 
	m_registerType = (eRegisterType)dataReader.r_u16();

	//load object module
	m_moduleObject->LoadMeta(dataReader);
	m_moduleManager->LoadMeta(dataReader);

	return IMetaObjectRegisterData::LoadData(dataReader);
}

bool CMetaObjectAccumulationRegister::SaveData(CMemoryWriter& dataWritter)
{
	//save default attributes:
	m_attributeRecordType->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_u32(m_defaultFormList);

	//save data
	dataWritter.w_u16(m_registerType);

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

bool CMetaObjectAccumulationRegister::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObjectRegisterData::OnCreateMetaObject(metaData))
		return false;

	return m_attributeRecordType->OnCreateMetaObject(metaData) &&
		m_moduleManager->OnCreateMetaObject(metaData) &&
		m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectAccumulationRegister::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_attributeRecordType->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleManager->OnLoadMetaObject(metaData))
		return false;

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return IMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool CMetaObjectAccumulationRegister::OnSaveMetaObject()
{
	if (!m_attributeRecordType->OnSaveMetaObject())
		return false;

	if (!m_moduleManager->OnSaveMetaObject())
		return false;

	if (!m_moduleObject->OnSaveMetaObject())
		return false;

#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
	if (!(m_attributeRecorder->GetCount() > 0))
		return false;
#endif 

	return IMetaObjectRegisterData::OnSaveMetaObject();
}

bool CMetaObjectAccumulationRegister::OnDeleteMetaObject()
{
	if (!m_attributeRecordType->OnDeleteMetaObject())
		return false;

	if (!m_moduleManager->OnDeleteMetaObject())
		return false;

	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return IMetaObjectRegisterData::OnDeleteMetaObject();
}

bool CMetaObjectAccumulationRegister::OnReloadMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		CRecordSetAccumulationRegister* recordSet = NULL;
		if (moduleManager->FindCompileModule(m_moduleObject, recordSet)) {
			if (!recordSet->InitializeObject())
				return false;
		}
	}

	return true;
}

#include "metadata/singleMetaTypes.h"

bool CMetaObjectAccumulationRegister::OnBeforeRunMetaObject(int flags)
{
	if (!m_attributeRecordType->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleManager->OnBeforeRunMetaObject(flags))
		return false;

	if (!m_moduleObject->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	return IMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectAccumulationRegister::OnAfterRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {

			if (!moduleManager->AddCompileModule(m_moduleObject, CreateRecordSetValue()))
				return false;

			return true;
		}
	}

	return IMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool CMetaObjectAccumulationRegister::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (IMetaObjectRegisterData::OnBeforeCloseMetaObject()) {

			if (!moduleManager->RemoveCompileModule(m_moduleObject))
				return false;

			return true;
		}
	}

	return IMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool CMetaObjectAccumulationRegister::OnAfterCloseMetaObject()
{
	if (!m_attributeRecordType->OnAfterCloseMetaObject())
		return false;

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

void CMetaObjectAccumulationRegister::OnCreateMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectAccumulationRegister::eFormList
		&& m_defaultFormList == wxNOT_FOUND)
	{
		m_defaultFormList = metaForm->GetMetaID();
	}
}

void CMetaObjectAccumulationRegister::OnRemoveMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectAccumulationRegister::eFormList
		&& m_defaultFormList == metaForm->GetMetaID())
	{
		m_defaultFormList = wxNOT_FOUND;
	}
}

std::vector<IMetaAttributeObject*> CMetaObjectAccumulationRegister::GetDefaultAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;
	attributes.push_back(m_attributeLineActive);
	if (m_registerType == eRegisterType::eBalances) {
		attributes.push_back(m_attributeRecordType);
	}
	attributes.push_back(m_attributePeriod);
	attributes.push_back(m_attributeRecorder);
	attributes.push_back(m_attributeLineNumber);
	return attributes;
}

std::vector<IMetaAttributeObject*> CMetaObjectAccumulationRegister::GetGenericDimensions() const
{
	std::vector<IMetaAttributeObject*> attributes;
	attributes.push_back(m_attributeRecorder);
	return attributes;
}

ISourceDataObject* CMetaObjectAccumulationRegister::CreateObjectData(IMetaFormObject* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList: return new CListRegisterObject(this, metaObject->GetTypeForm());
	}

	return NULL;
}

IRecordSetObject* CMetaObjectAccumulationRegister::CreateRecordSet()
{
	return new CRecordSetAccumulationRegister(this);
}

IRecordSetObject* CMetaObjectAccumulationRegister::CreateRecordSet(const CUniquePairKey& uniqueKey)
{
	return new CRecordSetAccumulationRegister(this, uniqueKey);
}

/////////////////////////////////////////////////////////////////////////

IRecordSetObject* CMetaObjectAccumulationRegister::CreateRecordSetValue()
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

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObjectAccumulationRegister::ReadProperty()
{
	IMetaObjectRegisterData::ReadProperty();

	IObjectBase::SetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::SetPropertyValue("register_type", m_registerType);
}

void CMetaObjectAccumulationRegister::SaveProperty()
{
	IMetaObjectRegisterData::SaveProperty();

	IObjectBase::GetPropertyValue("default_list", m_defaultFormList);
	IObjectBase::GetPropertyValue("register_type", m_registerType, true);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectAccumulationRegister, "accumulationRegister", g_metaAccumulationRegisterCLSID);
//add new enumeration
ENUM_REGISTER(CValueEnumAccumulationRegisterRecordType, "accumulationRecordType", g_enumRecordTypeCLSID);