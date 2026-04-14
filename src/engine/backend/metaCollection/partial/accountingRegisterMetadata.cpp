////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register metaData
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "chartOfAccounts.h"
#include "list/objectList.h"
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueMetaObjectAccountingRegister, ibValueMetaObjectRegisterData);

ibValueMetaObjectAccountingRegister::ibValueMetaObjectAccountingRegister() : ibValueMetaObjectRegisterData()
{
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectAccountingRegister::~ibValueMetaObjectAccountingRegister()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectAccountingRegister::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectAccountingRegister::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormList, ownerControl,
		ibValue::CreateAndPrepareValueRef<ibValueListRegisterObject>(this, eFormList), formGuid);
}
#pragma endregion

bool ibValueMetaObjectAccountingRegister::LoadData(ibReaderMemory& dataReader)
{
	(*m_propertyAttributeRecordType)->LoadMeta(dataReader);
	(*m_propertyAttributeAccount)->LoadMeta(dataReader);
	(*m_propertyAttributeSubconto1)->LoadMeta(dataReader);
	(*m_propertyAttributeSubconto2)->LoadMeta(dataReader);
	(*m_propertyAttributeSubconto3)->LoadMeta(dataReader);
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	if (!m_propertyChartOfAccounts->LoadData(dataReader))
		return false;

	(*m_propertyModuleObject)->LoadMeta(dataReader);
	(*m_propertyModuleManager)->LoadMeta(dataReader);
	return ibValueMetaObjectRegisterData::LoadData(dataReader);
}

bool ibValueMetaObjectAccountingRegister::SaveData(ibWriterMemory& dataWritter)
{
	(*m_propertyAttributeRecordType)->SaveMeta(dataWritter);
	(*m_propertyAttributeAccount)->SaveMeta(dataWritter);
	(*m_propertyAttributeSubconto1)->SaveMeta(dataWritter);
	(*m_propertyAttributeSubconto2)->SaveMeta(dataWritter);
	(*m_propertyAttributeSubconto3)->SaveMeta(dataWritter);
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));

	if (!m_propertyChartOfAccounts->SaveData(dataWritter))
		return false;

	(*m_propertyModuleObject)->SaveMeta(dataWritter);
	(*m_propertyModuleManager)->SaveMeta(dataWritter);
	return ibValueMetaObjectRegisterData::SaveData(dataWritter);
}

#include "backend/appData.h"

bool ibValueMetaObjectAccountingRegister::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags)) return false;
	return (*m_propertyAttributeRecordType)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeAccount)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeSubconto1)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeSubconto2)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeSubconto3)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyModuleManager)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyModuleObject)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAccountingRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeRecordType)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeAccount)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeSubconto1)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeSubconto2)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeSubconto3)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyModuleManager)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyModuleObject)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectAccountingRegister::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto1)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto2)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto3)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyModuleManager)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyModuleObject)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectAccountingRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto1)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto2)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto3)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyModuleManager)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyModuleObject)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

bool ibValueMetaObjectAccountingRegister::OnReloadMetaObject()
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (appData->DesignerMode()) {
		ibValueRecordSetObjectAccountingRegister* recordSet = nullptr;
		if (moduleManager->FindCompileModule(m_propertyModuleObject->GetMetaObject(), recordSet)) {
			if (!recordSet->InitializeObject()) return false;
		}
	}
	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectAccountingRegister::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto1)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto2)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto3)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleManager)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleObject)->OnBeforeRunMetaObject(flags)) return false;
	registerSelection();
	return ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectAccountingRegister::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto1)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto2)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto3)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleManager)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleObject)->OnAfterRunMetaObject(flags)) return false;

	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	// Set Account field type from Chart of Accounts binding
	const ibMetaDescription& metaDesc = m_propertyChartOfAccounts->GetValueAsMetaDesc();
	ibTypeDescription typeDesc;
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* chartOfAccounts = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (chartOfAccounts != nullptr) {
			const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(chartOfAccounts, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
			wxASSERT(so);
			typeDesc.AppendMetaType(so->GetClassType());
		}
	}
	(*m_propertyAttributeAccount)->SetDefaultMetaType(typeDesc);

	if ((*m_propertyAttributeAccount)->GetClsidCount() > 0)
		(*m_propertyAttributeAccount)->ClearFlag(metaDisableFlag);
	else
		(*m_propertyAttributeAccount)->SetFlag(metaDisableFlag);

	// Set Subconto1/2/3 types from ПВХ linked to the Chart of Accounts
	// Find the Chart of Accounts and get its ПВХ binding
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* chartOfAccounts = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (chartOfAccounts != nullptr) {
			// Cast to ibValueMetaObjectChartOfAccounts to access ПВХ binding
			const ibValueMetaObjectChartOfAccounts* chartOfAccountsObj = nullptr;
			if (chartOfAccounts->ConvertToValue(chartOfAccountsObj) && chartOfAccountsObj != nullptr) {
				ibPropertyChartOfCharacteristicTypes* pvhBinding = chartOfAccountsObj->GetChartOfCharacteristicTypes();
				if (pvhBinding != nullptr) {
					const ibMetaDescription& pvhDesc = pvhBinding->GetValueAsMetaDesc();
					ibTypeDescription subcontoTypeDesc;
					for (unsigned int pvhIdx = 0; pvhIdx < pvhDesc.GetTypeCount(); pvhIdx++) {
						const ibValueMetaObject* pvh = m_metaData->FindAnyObjectByFilter(pvhDesc.GetByIdx(pvhIdx));
						if (pvh != nullptr) {
							const ibCtorMetaValueType* pvhCtor = m_metaData->GetTypeCtor(pvh, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
							if (pvhCtor != nullptr)
								subcontoTypeDesc.AppendMetaType(pvhCtor->GetClassType());
						}
					}
					// Set all subconto fields to accept ПВХ reference types
					if (subcontoTypeDesc.GetClsidCount() > 0) {
						(*m_propertyAttributeSubconto1)->SetDefaultMetaType(subcontoTypeDesc);
						(*m_propertyAttributeSubconto2)->SetDefaultMetaType(subcontoTypeDesc);
						(*m_propertyAttributeSubconto3)->SetDefaultMetaType(subcontoTypeDesc);
					}
				}
			}
		}
	}

	if (appData->DesignerMode()) {
		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {
			if (!moduleManager->AddCompileModule(m_propertyModuleObject->GetMetaObject(), CreateRecordSetObjectValue())) return false;
			return true;
		}
	}
	return ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectAccountingRegister::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto1)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto2)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto3)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyModuleManager)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyModuleObject)->OnBeforeCloseMetaObject()) return false;
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (appData->DesignerMode()) {
		if (ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()) {
			if (!moduleManager->RemoveCompileModule(m_propertyModuleObject->GetMetaObject())) return false;
			return true;
		}
	}
	return ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectAccountingRegister::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto1)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto2)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto3)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyModuleManager)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyModuleObject)->OnAfterCloseMetaObject()) return false;
	unregisterSelection();
	return ibValueMetaObjectRegisterData::OnAfterCloseMetaObject();
}

void ibValueMetaObjectAccountingRegister::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
}

void ibValueMetaObjectAccountingRegister::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
}

#include "accountingRegisterManager.h"

ibValueManagerDataObject* ibValueMetaObjectAccountingRegister::CreateManagerDataObjectValue()
{
	return ibValue::CreateAndPrepareValueRef<ibValueManagerDataObjectAccountingRegister>(this);
}

ibValueRecordSetObject* ibValueMetaObjectAccountingRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey)
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (appData->DesignerMode()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!moduleManager->FindCompileModule(m_propertyModuleObject->GetMetaObject(), pDataRef))
			return ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectAccountingRegister>(this, uniqueKey);
		return pDataRef;
	}
	return ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectAccountingRegister>(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectAccountingRegister::CreateSourceObject(ibValueMetaObjectFormBase* metaObject)
{
	switch (metaObject->GetTypeForm()) {
	case eFormList: return ibValue::CreateAndPrepareValueRef<ibValueListRegisterObject>(this, metaObject->GetTypeForm());
	}
	return nullptr;
}

bool ibValueMetaObjectAccountingRegister::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	return CreateAndUpdateRegisterTableDB(srcMetaData, srcMetaObject, flags);
}

bool ibValueMetaObjectAccountingRegister::CreateAndUpdateRegisterTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	// Delegates to the base class which handles all predefined attributes
	// (LineActive, Period, RecordType, Account, Subconto1-3, Recorder, LineNumber)
	// via FillArrayObjectByPredefinedAttribute, plus user-defined dimensions and resources.
	return ibValueMetaObjectRegisterData::CreateAndUpdateTableDB(srcMetaData, srcMetaObject, flags);
}

METADATA_TYPE_REGISTER(ibValueMetaObjectAccountingRegister, "AccountingRegister", g_metaAccountingRegisterCLSID);
