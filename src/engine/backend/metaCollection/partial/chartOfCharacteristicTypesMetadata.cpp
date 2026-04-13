////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of characteristic types metaData
////////////////////////////////////////////////////////////////////////////

#include "chartOfCharacteristicTypes.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

//********************************************************************************************
//*										 metaData											 *
//********************************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueMetaObjectChartOfCharacteristicTypes, ibValueMetaObjectRecordDataHierarchyMutableRef);

//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectChartOfCharacteristicTypes::ibValueMetaObjectChartOfCharacteristicTypes() : ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	//set default proc
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("OnDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });

	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("Filling"), ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("OnCopy"), ibContentHelper::eProcedureHelper, { wxT("Source") });

	(*m_propertyModuleObject)->SetDefaultProcedure(wxT("SetNewCode"), ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });
}

ibValueMetaObjectChartOfCharacteristicTypes::~ibValueMetaObjectChartOfCharacteristicTypes()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectChartOfCharacteristicTypes::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormObject && m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	}
	else if (id == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormFolder->GetValueAsInteger());
	}
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	}
	else if (id == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormSelect->GetValueAsInteger());
	}
	else if (id == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() != wxNOT_FOUND) {
		return FindFormObjectByFilter(m_propertyDefFormFolderSelect->GetValueAsInteger());
	}

	return nullptr;
}

#include "chartOfCharacteristicTypesManager.h"

ibValueManagerDataObject* ibValueMetaObjectChartOfCharacteristicTypes::CreateManagerDataObjectValue()
{
	return ibValue::CreateAndPrepareValueRef<ibValueManagerDataObjectChartOfCharacteristicTypes>(this);
}

#include "backend/appData.h"

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectChartOfCharacteristicTypes::CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid)
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	ibValueRecordDataObjectChartOfCharacteristicTypes* pDataRef = nullptr;
	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_propertyModuleObject->GetMetaObject(), pDataRef)) {
			return ibValue::CreateAndPrepareValueRef<ibValueRecordDataObjectChartOfCharacteristicTypes>(this, guid, mode);
		}
	}
	else {
		pDataRef = ibValue::CreateAndPrepareValueRef<ibValueRecordDataObjectChartOfCharacteristicTypes>(this, guid, mode);
	}

	return pDataRef;
}

ibSourceDataObject* ibValueMetaObjectChartOfCharacteristicTypes::CreateSourceObject(ibValueMetaObjectFormBase* metaObject)
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject:
		return CreateObjectValue(ibObjectMode::OBJECT_ITEM);
	case eFormFolder:
		return CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
	case eFormList:
		return ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER);
	case eFormSelect:
		return ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER, true);
	case eFormFolderSelect:
		return ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true);
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormObject,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_ITEM),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetFolderForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormFolder,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_FOLDER),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormList,
		ownerControl, ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, ibValueMetaObjectChartOfCharacteristicTypes::eFormList, ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormSelect,
		ownerControl, ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, ibValueMetaObjectChartOfCharacteristicTypes::eFormSelect, ibValueModelTreeDataObjectFolderRef::LIST_ITEM, true),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectChartOfCharacteristicTypes::GetFolderSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid)
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectChartOfCharacteristicTypes::eFormFolderSelect,
		ownerControl, ibValue::CreateAndPrepareValueRef<ibValueModelTreeDataObjectFolderRef>(this, ibValueMetaObjectChartOfCharacteristicTypes::eFormSelect, ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true),
		formGuid
	);
}
#pragma endregion

wxString ibValueMetaObjectChartOfCharacteristicTypes::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDescription;
	if (objValue->GetValueByMetaID((*m_propertyAttributeDescription)->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectChartOfCharacteristicTypes::LoadData(ibReaderMemory& dataReader)
{
	//Load object module
	(*m_propertyModuleObject)->LoadMeta(dataReader);
	(*m_propertyModuleManager)->LoadMeta(dataReader);

	//load default form
	m_propertyDefFormObject->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormFolder->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	return ibValueMetaObjectRecordDataHierarchyMutableRef::LoadData(dataReader);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::SaveData(ibWriterMemory& dataWritter)
{
	//Save object module
	(*m_propertyModuleObject)->SaveMeta(dataWritter);
	(*m_propertyModuleManager)->SaveMeta(dataWritter);

	//save default form
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()));

	//create or update table:
	return ibValueMetaObjectRecordDataHierarchyMutableRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectChartOfCharacteristicTypes::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyModuleObject)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyModuleManager)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyModuleObject)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyModuleManager)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyModuleObject)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyModuleManager)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnDeleteMetaObject()
{
	if (!(*m_propertyModuleObject)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyModuleManager)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnReloadMetaObject()
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		ibValueRecordDataObjectChartOfCharacteristicTypes* pDataRef = nullptr;
		if (!moduleManager->FindCompileModule(m_propertyModuleObject->GetMetaObject(), pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectChartOfCharacteristicTypes::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyModuleObject)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyModuleManager)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	const ibCtorMetaValueType* typeCtor =
		m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);

	if (typeCtor != nullptr && !(*m_propertyAttributeParent)->ContainType(typeCtor->GetClassType())) {
		(*m_propertyAttributeParent)->SetDefaultMetaType(typeCtor->GetClassType());
	}

	return true;
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyModuleObject)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyModuleManager)->OnAfterRunMetaObject(flags))
		return false;

	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return moduleManager->AddCompileModule(m_propertyModuleObject->GetMetaObject(), CreateObjectValue(ibObjectMode::OBJECT_ITEM));

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyModuleObject)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyModuleManager)->OnBeforeCloseMetaObject())
		return false;

	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			return moduleManager->RemoveCompileModule(m_propertyModuleObject->GetMetaObject());

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnAfterCloseMetaObject()
{
	if (!(*m_propertyModuleObject)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyModuleManager)->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectChartOfCharacteristicTypes::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormFolder
		&& m_propertyDefFormFolder->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectChartOfCharacteristicTypes::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormFolder
		&& m_propertyDefFormFolder->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectChartOfCharacteristicTypes, "ChartOfCharacteristicTypes", g_metaChartOfCharacteristicTypesCLSID);
