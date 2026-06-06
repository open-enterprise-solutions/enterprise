////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metaData
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "list/objectList.h"
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"

//********************************************************************************************
//*										 metaData											 * 
//********************************************************************************************


//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectCatalog::ibValueMetaObjectCatalog() : ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	// Common 6 hooks — m_propertyObjectModule is declared on the leaf,
	// not on MutableRef base, so each leaf ctor registers them locally.
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"),  ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"),      ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnDelete"),     ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("Filling"),      ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnCopy"),       ibContentHelper::eProcedureHelper, { wxT("Source") });
	// Leaf-specific: code-generation hook.
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("SetNewCode"), ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });
}

ibValueMetaObjectCatalog::~ibValueMetaObjectCatalog()
{
	//wxDELETE((*m_propertyAttributeOwner));
}

ibValueMetaObjectFormBase* ibValueMetaObjectCatalog::GetDefaultFormByID(const ibFormID& id) const
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

#include "catalogManager.h"

ibValueManagerDataObject* ibValueMetaObjectCatalog::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectCatalog(this);
}

#include "backend/appData.h"

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectCatalog::CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid) const
{
	ibValueRecordDataObjectCatalog* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return new ibValueRecordDataObjectCatalog(this, guid, mode);
		}
	}
	else {
		pDataRef = new ibValueRecordDataObjectCatalog(this, guid, mode);
	}

	return pDataRef;
}

ibSourceDataObject* ibValueMetaObjectCatalog::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject:
		return CreateObjectValue(ibObjectMode::OBJECT_ITEM);
	case eFormFolder:
		return CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
	case eFormList:
		return new ibValueModelTreeDataObjectFolderRef(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER);
	case eFormSelect:
		return new ibValueModelTreeDataObjectFolderRef(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER, true);
	case eFormFolderSelect:
		return new ibValueModelTreeDataObjectFolderRef(this, metaObject->GetTypeForm(), ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true);
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectCatalog::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCatalog::eFormObject,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_ITEM),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectCatalog::GetFolderForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCatalog::eFormFolder,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_FOLDER),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectCatalog::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCatalog::eFormList,
		ownerControl, new ibValueModelTreeDataObjectFolderRef(this, ibValueMetaObjectCatalog::eFormList, ibValueModelTreeDataObjectFolderRef::LIST_ITEM_FOLDER),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectCatalog::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCatalog::eFormSelect,
		ownerControl, new ibValueModelTreeDataObjectFolderRef(this, ibValueMetaObjectCatalog::eFormSelect, ibValueModelTreeDataObjectFolderRef::LIST_ITEM, true),
		formGuid
	);
}

ibBackendValueForm* ibValueMetaObjectCatalog::GetFolderSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectCatalog::eFormFolderSelect,
		ownerControl, new ibValueModelTreeDataObjectFolderRef(this, ibValueMetaObjectCatalog::eFormSelect, ibValueModelTreeDataObjectFolderRef::LIST_FOLDER, true),
		formGuid
	);
}
#pragma endregion

wxString ibValueMetaObjectCatalog::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDescription;
	if (objValue->GetValueByMetaID((*m_propertyAttributeDescription)->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectCatalog::LoadData(ibReaderMemory& dataReader)
{
	//load default attributes:
	(*m_propertyAttributeOwner)->LoadMeta(dataReader);

	//Load object module
	(*m_propertyObjectModule)->LoadMeta(dataReader);
	(*m_propertyManagerModule)->LoadMeta(dataReader);

	//load default form 
	m_propertyDefFormObject->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormFolder->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormList->SetValue(GetIdByGuid(dataReader.r_stringZ()));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(dataReader.r_stringZ()));

	if (!m_propertyOwner->LoadData(dataReader))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::LoadData(dataReader);
}

bool ibValueMetaObjectCatalog::SaveData(ibWriterMemory& dataWritter)
{
	//save default attributes:
	(*m_propertyAttributeOwner)->SaveMeta(dataWritter);

	//Save object module
	(*m_propertyObjectModule)->SaveMeta(dataWritter);
	(*m_propertyManagerModule)->SaveMeta(dataWritter);

	//save default form 
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormList->GetValueAsInteger()));
	dataWritter.w_stringZ(GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()));

	if (!m_propertyOwner->SaveData(dataWritter))
		return false;

	//create or update table:
	return ibValueMetaObjectRecordDataHierarchyMutableRef::SaveData(dataWritter);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectCatalog::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeOwner)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCatalog::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeOwner)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCatalog::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeOwner)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectCatalog::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeOwner)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectCatalog::OnReloadMetaObject()
{

	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectCatalog* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef)) {
			return true;
		}
		return pDataRef->InitializeObject();
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectCatalog::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeOwner)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
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

bool ibValueMetaObjectCatalog::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeOwner)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;


	const ibMetaDescription& metaDesc = m_propertyOwner->GetValueAsMetaDesc(); ibTypeDescription typeDesc;
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* catalog = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (catalog != nullptr) {
			const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(catalog, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
			wxASSERT(so);
			typeDesc.AppendMetaType(so->GetClassType());
		}
	}

	(*m_propertyAttributeOwner)->SetDefaultMetaType(typeDesc);

	if ((*m_propertyAttributeOwner)->GetClsidCount() > 0)
		(*m_propertyAttributeOwner)->ClearFlag(metaDisableFlag);
	else
		(*m_propertyAttributeOwner)->SetFlag(metaDisableFlag);

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(), CreateObjectValue(ibObjectMode::OBJECT_ITEM));

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCatalog::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeOwner)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;


	const ibMetaDescription& metaDesc = m_propertyOwner->GetValueAsMetaDesc();
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* catalog = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
		if (catalog != nullptr) {
			const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(catalog, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
			wxASSERT(so);
			(*m_propertyAttributeOwner)->GetTypeDesc().ClearMetaType(so->GetClassType());
		}
	}

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			return cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());

		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCatalog::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeOwner)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyManagerModule)->OnAfterCloseMetaObject())
		return false;

	unregisterSelection();

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

void ibValueMetaObjectCatalog::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormFolder
		&& m_propertyDefFormFolder->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == wxNOT_FOUND)
	{
		m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
	}
}

void ibValueMetaObjectCatalog::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormObject
		&& m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormObject->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormFolder
		&& m_propertyDefFormFolder->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolder->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormList
		&& m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormSelect
		&& m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormSelect->SetValue(wxNOT_FOUND);
	}
	else if (metaForm->GetTypeForm() == ibValueMetaObjectCatalog::eFormFolderSelect
		&& m_propertyDefFormFolderSelect->GetValueAsInteger() == metaForm->GetMetaID())
	{
		m_propertyDefFormFolderSelect->SetValue(wxNOT_FOUND);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectCatalog, "Catalog", g_metaCatalogCLSID);