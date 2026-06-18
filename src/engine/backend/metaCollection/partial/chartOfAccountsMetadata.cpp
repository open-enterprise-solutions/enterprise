////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of accounts metaData
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"
#include "backend/serialize/dataBuilder.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"


ibValueMetaObjectChartOfAccounts::ibValueMetaObjectChartOfAccounts() : ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"),  ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"),      ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnDelete"),     ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("Filling"),      ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnCopy"),       ibContentHelper::eProcedureHelper, { wxT("Source") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("SetNewCode"),   ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });
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

ibValueManagerDataObject* ibValueMetaObjectChartOfAccounts::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectChartOfAccounts(this);
}

#include "backend/appData.h"

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectChartOfAccounts::CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid) const
{
	ibValueRecordDataObjectChartOfAccounts* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return new ibValueRecordDataObjectChartOfAccounts(this, guid, mode);
	}
	else {
		pDataRef = new ibValueRecordDataObjectChartOfAccounts(this, guid, mode);
	}
	return pDataRef;
}

ibSourceDataObject* ibValueMetaObjectChartOfAccounts::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject: return CreateObjectValue(ibObjectMode::OBJECT_ITEM);
	case eFormFolder: return CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
	case eFormList: return new ibValueModelTreeDataObjectFolderRef(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER);
	case eFormSelect: return new ibValueModelTreeDataObjectFolderRef(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER, true);
	case eFormFolderSelect: return new ibValueModelTreeDataObjectFolderRef(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true);
	}
	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, eFormObject, ownerControl, CreateObjectValue(ibObjectMode::OBJECT_ITEM), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetFolderForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, eFormFolder, ownerControl, CreateObjectValue(ibObjectMode::OBJECT_FOLDER), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, eFormList, ownerControl,
		new ibValueModelTreeDataObjectFolderRef(this, eFormList, ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, eFormSelect, ownerControl,
		new ibValueModelTreeDataObjectFolderRef(this, eFormSelect, ibValueModelTreeDataObjectFolderRef::LIST_ITEM, true), formGuid);
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetFolderSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, eFormFolderSelect, ownerControl,
		new ibValueModelTreeDataObjectFolderRef(this, eFormFolderSelect, ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true), formGuid);
}
#pragma endregion

wxString ibValueMetaObjectChartOfAccounts::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDescription;
	if (objValue->GetValueByMetaID((*m_propertyAttributeDescription)->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

bool ibValueMetaObjectChartOfAccounts::WriteData(ibDataNode& node)
{
	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolder->GetName(), GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormSelect->GetName(), GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolderSelect->GetName(), GetGuidByID(m_propertyDefFormFolderSelect->GetValueAsInteger()).str());

	node.SetProperty(m_propertyAttributeAccountType->GetName(), m_propertyAttributeAccountType->GetNodeValue());
	node.SetProperty(m_propertyAttributeOffBalance->GetName(), m_propertyAttributeOffBalance->GetNodeValue());
	node.SetProperty(m_propertyAttributeQuantitative->GetName(), m_propertyAttributeQuantitative->GetNodeValue());
	node.SetProperty(m_propertyAttributeCurrency->GetName(), m_propertyAttributeCurrency->GetNodeValue());
	node.SetProperty(m_propertyAttributeMaxSubcontoCount->GetName(), m_propertyAttributeMaxSubcontoCount->GetNodeValue());

	node.SetProperty(m_propertySubcontoKindsTable->GetName(), m_propertySubcontoKindsTable->GetNodeValue());

	node.SetProperty(m_propertyChartOfCharacteristicTypes->GetName(), m_propertyChartOfCharacteristicTypes->GetNodeValue());

	return ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData(node);
}

bool ibValueMetaObjectChartOfAccounts::ReadData(const ibDataNode& node)
{
	m_propertyObjectModule->ReadNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyDefFormObject->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormObject->GetName())));
	m_propertyDefFormFolder->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormFolder->GetName())));
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormSelect->GetName())));
	m_propertyDefFormFolderSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormFolderSelect->GetName())));

	m_propertyAttributeAccountType->ReadNodeValue(node.GetProperty(m_propertyAttributeAccountType->GetName()));
	m_propertyAttributeOffBalance->ReadNodeValue(node.GetProperty(m_propertyAttributeOffBalance->GetName()));
	m_propertyAttributeQuantitative->ReadNodeValue(node.GetProperty(m_propertyAttributeQuantitative->GetName()));
	m_propertyAttributeCurrency->ReadNodeValue(node.GetProperty(m_propertyAttributeCurrency->GetName()));
	m_propertyAttributeMaxSubcontoCount->ReadNodeValue(node.GetProperty(m_propertyAttributeMaxSubcontoCount->GetName()));

	m_propertySubcontoKindsTable->ReadNodeValue(node.GetProperty(m_propertySubcontoKindsTable->GetName()));

	m_propertyChartOfCharacteristicTypes->ReadNodeValue(node.GetProperty(m_propertyChartOfCharacteristicTypes->GetName()));

	return ibValueMetaObjectRecordDataHierarchyMutableRef::ReadData(node);
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
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectChartOfAccounts::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeAccountType)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeCurrency)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeMaxSubcontoCount)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertySubcontoKindsTable)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData)) return false;
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
	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags)) return false;
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
	if (!(*m_propertyObjectModule)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject()) return false;
	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectChartOfAccounts::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectChartOfAccounts* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) return true;
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
	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags)) return false;
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
	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags)) return false;


	// Set SubcontoKind column type from РџР’РҐ binding
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

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), CreateObjectValue(ibObjectMode::OBJECT_ITEM));
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
	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject()) return false;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			return cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());
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
	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject()) return false;
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
