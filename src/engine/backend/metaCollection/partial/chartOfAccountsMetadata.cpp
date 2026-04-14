////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of accounts metaData
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueMetaObjectChartOfAccounts, ibValueMetaObjectRecordDataHierarchyMutableRef);

ibValueMetaObjectChartOfAccounts::ibValueMetaObjectChartOfAccounts() : ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("OnDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("Filling"), ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("OnCopy"), ibContentHelper::eProcedureHelper, { wxT("Source") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("SetNewCode"), ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });
}

ibValueMetaObjectChartOfAccounts::~ibValueMetaObjectChartOfAccounts()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectChartOfAccounts::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormObject && m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND) return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	else if (id == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() != wxNOT_FOUND) return FindFormObjectByFilter(m_propertyDefFormFolder->GetValueAsInteger());
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	else if (id == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() != wxNOT_FOUND) return FindFormObjectByFilter(m_propertyDefFormSelect->GetValueAsInteger());
	else if (id == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() != wxNOT_FOUND) return FindFormObjectByFilter(m_propertyDefFormFolderSelect->GetValueAsInteger());
	return nullptr;
}

#include "chartOfAccountsManager.h"

ibValueManagerDataObject* ibValueMetaObjectChartOfAccounts::CreateManagerDataObjectValue()
{
	return ibValue::CreateAndPrepareValueRef<ibValueManagerDataObjectChartOfAccounts>(this);
}

#include "backend/appData.h"

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectChartOfAccounts::CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid)
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	ibValueRecordDataObjectChartOfAccounts* pDataRef = nullptr;
	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_propertyModuleObject->GetMetaObject(), pDataRef))
			return ibValue::CreateAndPrepareValueRef<ibValueRecordDataObjectChartOfAccounts>(this, guid, mode);
	}
	else {
		pDataRef = ibValue::CreateAndPrepareValueRef<ibValueRecordDataObjectChartOfAccounts>(this, guid, mode);
	}
	return pDataRef;
}

ibSourceDataObject* ibValueMetaObjectChartOfAccounts::CreateSourceObject(ibValueMetaObjectFormBase* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject: return CreateObjectValue(ibObjectMode::OBJECT_ITEM);
	case eFormFolder: return CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
	case eFormList: return ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER);
	case eFormSelect: return ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER, true);
	case eFormFolderSelect: return ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true);
	}
	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(strFormName, eFormObject, ownerControl, CreateObjectValue(ibObjectMode::OBJECT_ITEM), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetFolderForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(strFormName, eFormFolder, ownerControl, CreateObjectValue(ibObjectMode::OBJECT_FOLDER), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(strFormName, eFormList, ownerControl,
		ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, eFormList, ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(strFormName, eFormSelect, ownerControl,
		ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, eFormSelect, ibValueModelTreeDataObjectFolderRef::LIST_ITEM, true), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetFolderSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(strFormName, eFormFolderSelect, ownerControl,
		ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, eFormFolderSelect, ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true), formGuid);
}
#pragma endregion

wxString ibValueMetaObjectChartOfAccounts::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDescription;
	if (objValue->GetValueByMetaID((*m_propertyAttributeDescription)->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

bool ibValueMetaObjectChartOfAccounts::LoadData(ibReaderMemory& dataReader)
{
	(*m_propertyModuleObject)->LoadMeta(dataReader);
	(*m_propertyModuleManager)->LoadMeta(dataReader);
	m_propertyDefFormObject->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormFolder->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormFolderSelect->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	//load default attributes:
	(*m_propertyAttributeAccountType)->LoadMeta(dataReader);
	(*m_propertyAttributeOffBalance)->LoadMeta(dataReader);
	(*m_propertyAttributeQuantitative)->LoadMeta(dataReader);
	(*m_propertyAttributeCurrency)->LoadMeta(dataReader);
	(*m_propertyAttributeMaxSubcontoCount)->LoadMeta(dataReader);

	(*m_propertySubcontoKindsTable)->LoadMeta(dataReader);

	if (!m_propertyChartOfCharacteristicTypes->LoadData(dataReader))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::LoadData(dataReader);
}

bool ibValueMetaObjectChartOfAccounts::SaveData(ibWriterMemory& dataWritter)
{
	(*m_propertyModuleObject)->SaveMeta(dataWritter);
	(*m_propertyModuleManager)->SaveMeta(dataWritter);
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormFolderSelect->GetValueAsInteger()));

	//save default attributes:
	(*m_propertyAttributeAccountType)->SaveMeta(dataWritter);
	(*m_propertyAttributeOffBalance)->SaveMeta(dataWritter);
	(*m_propertyAttributeQuantitative)->SaveMeta(dataWritter);
	(*m_propertyAttributeCurrency)->SaveMeta(dataWritter);
	(*m_propertyAttributeMaxSubcontoCount)->SaveMeta(dataWritter);

	(*m_propertySubcontoKindsTable)->SaveMeta(dataWritter);

	if (!m_propertyChartOfCharacteristicTypes->SaveData(dataWritter))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::SaveData(dataWritter);
}

bool ibValueMetaObjectChartOfAccounts::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(metaData, flags)) return false;

	return (*m_propertyAttributeAccountType)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeOffBalance)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeQuantitative)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeCurrency)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeMaxSubcontoCount)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertySubcontoKindsTable)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyModuleObject)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyModuleManager)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectChartOfAccounts::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeAccountType)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeCurrency)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertySubcontoKindsTable)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyModuleObject)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyModuleManager)->OnLoadMetaObject(metaData)) return false;
	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectChartOfAccounts::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeAccountType)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeCurrency)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertySubcontoKindsTable)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyModuleObject)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyModuleManager)->OnSaveMetaObject(flags)) return false;
	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectChartOfAccounts::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeAccountType)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeOffBalance)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeQuantitative)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeCurrency)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnDeleteMetaObject()) return false;
	if (!(*m_propertySubcontoKindsTable)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyModuleObject)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyModuleManager)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectChartOfAccounts::OnReloadMetaObject()
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (appData->DesignerMode()) {
		ibValueRecordDataObjectChartOfAccounts* pDataRef = nullptr;
		if (!moduleManager->FindCompileModule(m_propertyModuleObject->GetMetaObject(), pDataRef)) return true;
		return pDataRef->InitializeObject();
	}
	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectChartOfAccounts::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeAccountType)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeCurrency)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertySubcontoKindsTable)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleObject)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleManager)->OnBeforeRunMetaObject(flags)) return false;
	registerSelection();
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(flags)) return false;
	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	if (typeCtor != nullptr && !(*m_propertyAttributeParent)->ContainType(typeCtor->GetClassType()))
		(*m_propertyAttributeParent)->SetDefaultMetaType(typeCtor->GetClassType());
	return true;
}

bool ibValueMetaObjectChartOfAccounts::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeAccountType)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeCurrency)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertySubcontoKindsTable)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleObject)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyModuleManager)->OnAfterRunMetaObject(flags)) return false;

	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	// Set SubcontoKind column type from ПВХ binding
	const ibMetaDescription& metaDesc = m_propertyChartOfCharacteristicTypes->GetValueAsMetaDesc();
	if (m_propertySubcontoKindsTable->GetMetaObject() != nullptr && metaDesc.GetTypeCount() > 0) {
		ibTypeDescription typeDesc;
		for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
			const ibValueMetaObject* chartOfCharTypes = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
			if (chartOfCharTypes != nullptr) {
				const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(chartOfCharTypes, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
				wxASSERT(so);
				typeDesc.AppendMetaType(so->GetClassType());
			}
		}
		// Update SubcontoKind column type in predefined table
		ibValueMetaObjectAttributeBase* kindAttr = (*m_propertySubcontoKindsTable)->GetSubcontoKind();
		if (kindAttr != nullptr) {
			kindAttr->GetTypeDesc().SetDefaultMetaType(typeDesc);
		}
		// Prevent deletion of predefined tabular section
		(*m_propertySubcontoKindsTable)->SetFlag(metaDisableFlag);
	}

	if (appData->DesignerMode()) {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return moduleManager->AddCompileModule(m_propertyModuleObject->GetMetaObject(), CreateObjectValue(ibObjectMode::OBJECT_ITEM));
		return false;
	}
	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectChartOfAccounts::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeAccountType)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeOffBalance)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeQuantitative)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeCurrency)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertySubcontoKindsTable)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyModuleObject)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyModuleManager)->OnBeforeCloseMetaObject()) return false;
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (appData->DesignerMode()) {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			return moduleManager->RemoveCompileModule(m_propertyModuleObject->GetMetaObject());
		return false;
	}
	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectChartOfAccounts::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeAccountType)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeOffBalance)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeQuantitative)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeCurrency)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertySubcontoKindsTable)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyModuleObject)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyModuleManager)->OnAfterCloseMetaObject()) return false;
	unregisterSelection();
	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterCloseMetaObject();
}

void ibValueMetaObjectChartOfAccounts::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject && m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND) m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() == wxNOT_FOUND) m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND) m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND) m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() == wxNOT_FOUND) m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
}

void ibValueMetaObjectChartOfAccounts::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject && m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID()) m_propertyDefFormObject->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() == metaForm->GetMetaID()) m_propertyDefFormFolder->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID()) m_propertyDefFormList->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID()) m_propertyDefFormSelect->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() == metaForm->GetMetaID()) m_propertyDefFormFolderSelect->SetValue(wxNOT_FOUND);
}

METADATA_TYPE_REGISTER(ibValueMetaObjectChartOfAccounts, "ChartOfAccounts", g_metaChartOfAccountsCLSID);
