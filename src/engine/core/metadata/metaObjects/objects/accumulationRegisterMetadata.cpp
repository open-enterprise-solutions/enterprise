#include "accumulationRegister.h"
#include "list/objectList.h"
#include "core/metadata/metadata.h"
#include "core/metadata/moduleManager/moduleManager.h"

#define objectModule wxT("objectModule")
#define managerModule wxT("managerModule")

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectAccumulationRegister, IMetaObjectRegisterData);

/////////////////////////////////////////////////////////////////////////

CMetaObjectAccumulationRegister::CMetaObjectAccumulationRegister() : IMetaObjectRegisterData()
{
	//create default attributes
	m_attributeRecordType = CMetaDefaultAttributeObject::CreateSpecialType(wxT("recordType"), _("Record type"), wxEmptyString, g_enumRecordTypeCLSID, false, CValueEnumAccumulationRegisterRecordType::CreateDefEnumValue());
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
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		for (auto obj : GetObjectForms()) {
			if (m_propertyDefFormList->GetValueAsInteger() == obj->GetMetaID()) {
				return obj;
			}
		}
	}

	return NULL;
}

#include "frontend/visualView/controls/form.h"
#include "utils/stringUtils.h"

CValueForm* CMetaObjectAccumulationRegister::GetListForm(const wxString& formName, IControlFrame* ownerControl, const CUniqueKey& formGuid)
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
		CValueForm* valueForm = new CValueForm(ownerControl, NULL,
			new CListRegisterObject(this, CMetaObjectAccumulationRegister::eFormList), formGuid
		);
		valueForm->BuildForm(CMetaObjectAccumulationRegister::eFormList);
		return valueForm;
	}

	return defList->GenerateFormAndRun(
		ownerControl, new CListRegisterObject(this, defList->GetTypeForm()), formGuid
	);

	return NULL;
}

/////////////////////////////////////////////////////////////////////////////

OptionList* CMetaObjectAccumulationRegister::GetFormList(PropertyOption*)
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
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	//load data 
	m_propertyRegisterType->SetValue(dataReader.r_u16());

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
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));

	//save data
	dataWritter.w_u16(m_propertyRegisterType->GetValueAsInteger());

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

#include "core/metadata/singleClass.h"

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

			if (!moduleManager->AddCompileModule(m_moduleObject, CreateRecordSetObjectValue()))
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
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

void CMetaObjectAccumulationRegister::OnRemoveMetaForm(IMetaFormObject* metaForm)
{
	if (metaForm->GetTypeForm() == CMetaObjectAccumulationRegister::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
}

std::vector<IMetaAttributeObject*> CMetaObjectAccumulationRegister::GetDefaultAttributes() const
{
	std::vector<IMetaAttributeObject*> attributes;
	attributes.push_back(m_attributeLineActive);
	if (GetRegisterType() == eRegisterType::eBalances) {
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
	case eFormList:
		return new CListRegisterObject(this, metaObject->GetTypeForm());
	}

	return NULL;
}

IRecordSetObject* CMetaObjectAccumulationRegister::CreateRecordSetObjectRegValue(const CUniquePairKey& uniqueKey)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (appData->DesignerMode()) {
		IRecordSetObject* pDataRef = NULL;
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef)) {
			return new CRecordSetAccumulationRegister(this, uniqueKey);
		}
		return pDataRef;
	}
	return new CRecordSetAccumulationRegister(this, uniqueKey);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObjectAccumulationRegister, "accumulationRegister", g_metaAccumulationRegisterCLSID);