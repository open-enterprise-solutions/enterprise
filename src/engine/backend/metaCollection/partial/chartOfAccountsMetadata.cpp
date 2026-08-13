////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of accounts metaData
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the standard list migrates onto the universal dynamic list
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message — the message pane, not a dialog


ibValueMetaObjectChartOfAccounts::ibValueMetaObjectChartOfAccounts() : ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	// AN ACCOUNT RECORDS WHICH ACCOUNT IT SITS UNDER, and that record is not a tree. There is no
	// separate container kind — every node is an account — but the platform navigates nothing here:
	// the list is flat, and whoever wants the structure asks for it in a query or a grouping. Stated
	// here rather than left to the user, because it is not a preference: it is what a chart of
	// accounts IS. (A catalog keeps the default, folders and items.)
	SetHierarchyType(ibHierarchyType::eSubordination);

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
	case eFormList: return ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription());   // migrated onto the universal dynamic list (hierarchy via queryable)
	case eFormSelect: return ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice);   // select front-driven — choice mode
	case eFormFolderSelect: return ibCreateFolderList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice);   // folder-select = choice + IsFolder = true
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
		ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription()), formGuid);   // migrated onto the universal dynamic list (hierarchy via queryable)
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, eFormSelect, ownerControl,
		ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice), formGuid);   // select front-driven — choice mode
}

ibBackendValueForm* ibValueMetaObjectChartOfAccounts::GetFolderSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return CreateAndBuildForm(strFormName, eFormFolderSelect, ownerControl,
		ibCreateFolderList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice), formGuid);   // folder-select = choice + IsFolder = true
}
#pragma endregion

wxString ibValueMetaObjectChartOfAccounts::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDescription;
	if (objValue->GetValueByMetaID((*m_propertyAttributeDescription)->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

bool ibValueMetaObjectChartOfAccounts::WriteData(ibDataNode& node) const
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
	node.SetProperty(m_propertyMaxAccountDimensionCount->GetName(), m_propertyMaxAccountDimensionCount->GetNodeValue());

	node.SetProperty(m_propertyAccountDimensionKindsTable->GetName(), m_propertyAccountDimensionKindsTable->GetNodeValue());

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
	m_propertyMaxAccountDimensionCount->ReadNodeValue(node.GetProperty(m_propertyMaxAccountDimensionCount->GetName()));

	m_propertyAccountDimensionKindsTable->ReadNodeValue(node.GetProperty(m_propertyAccountDimensionKindsTable->GetName()));

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
		(*m_propertyAccountDimensionKindsTable)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectChartOfAccounts::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeAccountType)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributeCurrency)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAccountDimensionKindsTable)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData)) return false;
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(metaData))
		return false;
	// The binding is READ by now, so the kind column can take its type here rather than waiting for the
	// configuration to be run. Between load and run the metadata otherwise described a column the table
	// does not have (and the save, which happens in between, believed it).
	ApplyAccountDimensionKindType();
	return true;
}


bool ibValueMetaObjectChartOfAccounts::OnSaveMetaObject(int flags)
{
	// A CHART OF ACCOUNTS WITHOUT A CHART OF CHARACTERISTIC TYPES IS NOT A HALF-CONFIGURED CHART, IT IS A
	// CONTRADICTION: its analytics kinds table exists to hold ELEMENTS OF THAT CHART, and with no chart the
	// column has no type, so the section is a set of rows that can name nothing. Refused here, where a data
	// processor or an import cannot walk around it.
	// REPORTED, NOT THROWN. A metadata rule the user has not satisfied yet belongs in the message pane
	// under the editor, the way an enumeration with no values reports itself — a modal box for something
	// found while saving interrupts the work instead of describing it. The save still refuses.
	if (m_propertyChartOfCharacteristicTypes->GetValueAsMetaDesc().GetTypeCount() == 0) {
		ibValueSystemFunction::Message(
			wxString::Format(_("%s: a chart of characteristic types is required — the account dimension kinds are elements of it"), GetName()),
			ibStatusMessage::ibStatusMessage_Error);
		return false;
	}


	// Re-apply before the schema is computed: whatever the user has just picked is what the columns must
	// describe, and the snapshot is taken off these metaobjects.
	ApplyAccountDimensionKindType();

	if (!(*m_propertyAttributeAccountType)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributeCurrency)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAccountDimensionKindsTable)->OnSaveMetaObject(flags)) return false;
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
	if (!(*m_propertyAccountDimensionKindsTable)->OnDeleteMetaObject()) return false;
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
	if (!(*m_propertyAccountDimensionKindsTable)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags)) return false;
	registerSelection();
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(flags)) return false;
	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	if (typeCtor != nullptr && !(*m_propertyAttributeParent)->ContainType(typeCtor->GetClassType()))
		(*m_propertyAttributeParent)->SetDefaultMetaType(typeCtor->GetClassType());
	return true;
}

// THE KIND COLUMN'S TYPE COMES FROM THE BINDING, and must be applied wherever the binding can have
// arrived: on load, when the user picks the chart, and on run. It used to be applied at RUN only, so
// between picking a chart and the next configuration run the metadata said one thing and the schema
// another — the apply then tried to ALTER away a reference slot the table had never grown
// ("column FLD…_RTREF does not exist").
//
// A KIND is an ELEMENT of that chart, so a reference type is right here — and is exactly what must
// NOT be used for the VALUE slots on the register (those take the chart's composition).
void ibValueMetaObjectChartOfAccounts::ApplyAccountDimensionKindType()
{
	ibValueMetaObjectAccountDimensionKindsTable* kindsTable = m_propertyAccountDimensionKindsTable->GetMetaObject();
	if (kindsTable == nullptr || m_metaData == nullptr)
		return;

	ibValueMetaObjectAttributeBase* kindAttr = kindsTable->GetAccountDimensionKind();
	if (kindAttr == nullptr)
		return;

	const ibMetaDescription& metaDesc = m_propertyChartOfCharacteristicTypes->GetValueAsMetaDesc();
	ibTypeDescription typeDesc;
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* chartOfCharTypes = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (chartOfCharTypes != nullptr) {
			const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(chartOfCharTypes, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
			wxASSERT(so);
			typeDesc.AppendMetaType(so->GetClassType());
		}
	}

	// A BINDING THAT NAMES SOMETHING NOT LOADED YET IS NOT AN EMPTY BINDING. Load order is nobody's
	// promise: the chart of accounts may be read before the chart of characteristic types it points at,
	// and resolving to nothing there would CLEAR the column — the exact drop this method exists to
	// prevent. So an unresolved binding leaves the column alone; the next call (run, or the user's own
	// pick) resolves it. Only a genuinely empty binding clears, and that state cannot be saved.
	if (typeDesc.GetClsidCount() == 0 && metaDesc.GetTypeCount() > 0)
		return;

	kindAttr->GetTypeDesc().SetDefaultMetaType(typeDesc);
}

bool ibValueMetaObjectChartOfAccounts::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeAccountType)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeOffBalance)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeQuantitative)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAttributeCurrency)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyAccountDimensionKindsTable)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags)) return false;
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags)) return false;


	ApplyAccountDimensionKindType();

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), [this]() -> ibValue* { return CreateObjectValue(ibObjectMode::OBJECT_ITEM); });
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
	if (!(*m_propertyAccountDimensionKindsTable)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject()) return false;
	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject()) return false;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			{ cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject()); return true; }
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
	if (!(*m_propertyAccountDimensionKindsTable)->OnAfterCloseMetaObject()) return false;
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
