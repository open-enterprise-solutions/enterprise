////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register metaData
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "backend/serialize/dataBuilder.h"
#include "chartOfAccounts.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"


ibValueMetaObjectAccountingRegister::ibValueMetaObjectAccountingRegister() : ibValueMetaObjectRegisterData()
{
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
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
ibBackendValueForm* ibValueMetaObjectAccountingRegister::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormList, ownerControl,
		ibCreateList(GetQueryable(), GetRegisterPeriod()), formGuid);   // migrated onto the universal dynamic list
}
#pragma endregion

bool ibValueMetaObjectAccountingRegister::ReadData(const ibDataNode& node)
{
	m_propertyAttributeRecordType->ReadNodeValue(node.GetProperty(m_propertyAttributeRecordType->GetName()));
	m_propertyAttributeAccount->ReadNodeValue(node.GetProperty(m_propertyAttributeAccount->GetName()));
	m_propertyAttributeSubconto1->ReadNodeValue(node.GetProperty(m_propertyAttributeSubconto1->GetName()));
	m_propertyAttributeSubconto2->ReadNodeValue(node.GetProperty(m_propertyAttributeSubconto2->GetName()));
	m_propertyAttributeSubconto3->ReadNodeValue(node.GetProperty(m_propertyAttributeSubconto3->GetName()));

	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));

	m_propertyChartOfAccounts->ReadNodeValue(node.GetProperty(m_propertyChartOfAccounts->GetName()));

	m_propertyObjectModule->ReadNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	return ibValueMetaObjectRegisterData::ReadData(node);
}

bool ibValueMetaObjectAccountingRegister::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeRecordType->GetName(), m_propertyAttributeRecordType->GetNodeValue());
	node.SetProperty(m_propertyAttributeAccount->GetName(), m_propertyAttributeAccount->GetNodeValue());
	node.SetProperty(m_propertyAttributeSubconto1->GetName(), m_propertyAttributeSubconto1->GetNodeValue());
	node.SetProperty(m_propertyAttributeSubconto2->GetName(), m_propertyAttributeSubconto2->GetNodeValue());
	node.SetProperty(m_propertyAttributeSubconto3->GetName(), m_propertyAttributeSubconto3->GetNodeValue());

	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());

	node.SetProperty(m_propertyChartOfAccounts->GetName(), m_propertyChartOfAccounts->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
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
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAccountingRegister::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeRecordType)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeAccount)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeSubconto1)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeSubconto2)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeSubconto3)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectAccountingRegister::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeRecordType)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeAccount)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto1)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto2)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeSubconto3)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectAccountingRegister::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeRecordType)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeAccount)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto1)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto2)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeSubconto3)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

bool ibValueMetaObjectAccountingRegister::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObjectAccountingRegister* recordSet = nullptr;
		if (cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), recordSet)) {
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
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags)) return false;
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
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags)) return false;


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

	// Set Subconto1/2/3 types from ChartOfCharacteristicTypes linked to the Chart of Accounts
	// Find the Chart of Accounts and get its ChartOfCharacteristicTypes binding
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* chartOfAccounts = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (chartOfAccounts != nullptr) {
			// Cast to ibValueMetaObjectChartOfAccounts to access ChartOfCharacteristicTypes binding
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
					// Set all subconto fields to accept ChartOfCharacteristicTypes reference types
					if (subcontoTypeDesc.GetClsidCount() > 0) {
						(*m_propertyAttributeSubconto1)->SetDefaultMetaType(subcontoTypeDesc);
						(*m_propertyAttributeSubconto2)->SetDefaultMetaType(subcontoTypeDesc);
						(*m_propertyAttributeSubconto3)->SetDefaultMetaType(subcontoTypeDesc);
					}
				}
			}
		}
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags)) {
			if (!cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateRecordSetObjectValue(); })) return false;
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
	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject()) return false;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()) {
			cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());
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
	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject()) return false;
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

ibValueManagerDataObject* ibValueMetaObjectAccountingRegister::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectAccountingRegister(this);
}

ibValueRecordSetObject* ibValueMetaObjectAccountingRegister::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordSetObject* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return new ibValueRecordSetObjectAccountingRegister(this, uniqueKey);
		return pDataRef;
	}
	return new ibValueRecordSetObjectAccountingRegister(this, uniqueKey);
}

ibSourceDataObject* ibValueMetaObjectAccountingRegister::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm()) {
	case eFormList: return ibCreateList(GetQueryable(), GetRegisterPeriod());   // migrated onto the universal dynamic list
	}
	return nullptr;
}

METADATA_TYPE_REGISTER(ibValueMetaObjectAccountingRegister, "AccountingRegister", g_metaAccountingRegisterCLSID);
