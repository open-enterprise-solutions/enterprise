////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — metaData
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJob.h"

#include "backend/appData.h"                       // appData->DesignerMode(), GetJobManager
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibCreateHierarchyList / ibCreateFolderList
#include "backend/userInfo.h"                        // ibUserInfo — the identity the job inherits
#include "backend/job/jobManager.h"                  // ibJobManager / ibJobDescription

//********************************************************************************************
//*                                      metaData                                            *
//********************************************************************************************

ibValueMetaObjectParameterizedJob::ibValueMetaObjectParameterizedJob()
	: ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	// The write hooks a reference object has — the object module is where "may this be saved"
	// lives, exactly as a catalog's.
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnDelete"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("Filling"), ibContentHelper::eProcedureHelper, { wxT("Source"), wxT("StandartProcessing") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnCopy"), ibContentHelper::eProcedureHelper, { wxT("Source") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("SetNewCode"), ibContentHelper::eProcedureHelper, { wxT("Prefix"), wxT("StandartProcessing") });

	// THE HANDLER — in the MANAGER module, taking the row's own reference. The name is deliberately
	// the same one a predefined job uses (where it takes nothing), so the two kinds are described,
	// documented and debugged as one thing.
	(*m_propertyManagerModule)->SetDefaultProcedure(wxT("JobProcessing"), ibContentHelper::eProcedureHelper, { wxT("Job") });
}

ibValueMetaObjectParameterizedJob::~ibValueMetaObjectParameterizedJob()
{
}

ibValueMetaObjectFormBase* ibValueMetaObjectParameterizedJob::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormObject && m_propertyDefFormObject->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormObject->GetValueAsInteger());
	else if (id == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormFolder->GetValueAsInteger());
	else if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	else if (id == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormSelect->GetValueAsInteger());
	else if (id == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormFolderSelect->GetValueAsInteger());

	return nullptr;
}

#include "parameterizedJobManager.h"

ibValueManagerDataObject* ibValueMetaObjectParameterizedJob::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectJob(this);
}

ibValueRecordDataObjectHierarchyRef* ibValueMetaObjectParameterizedJob::CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid) const
{
	ibValueRecordDataObjectParameterizedJob* pDataRef = nullptr;
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return new ibValueRecordDataObjectParameterizedJob(this, guid, mode);
	}
	else {
		pDataRef = new ibValueRecordDataObjectParameterizedJob(this, guid, mode);
	}

	return pDataRef;
}

ibSourceDataObject* ibValueMetaObjectParameterizedJob::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormObject:
		return CreateObjectValue(ibObjectMode::OBJECT_ITEM);
	case eFormFolder:
		return CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
	case eFormList:
		return ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription());
	case eFormSelect:
		return ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice);
	case eFormFolderSelect:
		return ibCreateFolderList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice);
	}

	return nullptr;
}

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectParameterizedJob::GetObjectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormObject,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_ITEM), formGuid);
}

ibBackendValueForm* ibValueMetaObjectParameterizedJob::GetFolderForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormFolder,
		ownerControl, CreateObjectValue(ibObjectMode::OBJECT_FOLDER), formGuid);
}

ibBackendValueForm* ibValueMetaObjectParameterizedJob::GetListForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormList,
		ownerControl, ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription()), formGuid);
}

ibBackendValueForm* ibValueMetaObjectParameterizedJob::GetSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormSelect,
		ownerControl, ibCreateHierarchyList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice), formGuid);
}

ibBackendValueForm* ibValueMetaObjectParameterizedJob::GetFolderSelectForm(const wxString& strFormName, ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(strFormName, eFormFolderSelect,
		ownerControl, ibCreateFolderList(GetQueryable(), GetDataIsFolder(), GetDataDescription(), ibDynamicListView_Choice), formGuid);
}
#pragma endregion

wxString ibValueMetaObjectParameterizedJob::GetDataPresentation(const ibValueDataObject* objValue) const
{
	static ibValue vDescription;
	if (objValue->GetValueByMetaID((*m_propertyAttributeDescription)->GetMetaID(), vDescription))
		return vDescription.GetString();
	return wxEmptyString;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectParameterizedJob::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	// The job's OWN attributes first — they are declared on this leaf, so this leaf is what gives
	// them their metadata. An attribute that never got it has no factory behind it, and the first
	// empty object built from this metatype asserts in CreateValueRef.
	if (!(*m_propertyAttributeActive)->OnCreateMetaObject(metaData, flags) ||
		!(*m_propertyAttributeSchedule)->OnCreateMetaObject(metaData, flags) ||
		!(*m_propertyAttributeLastRun)->OnCreateMetaObject(metaData, flags) ||
		!(*m_propertyAttributeNextRun)->OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectParameterizedJob::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeActive)->OnLoadMetaObject(metaData) ||
		!(*m_propertyAttributeSchedule)->OnLoadMetaObject(metaData) ||
		!(*m_propertyAttributeLastRun)->OnLoadMetaObject(metaData) ||
		!(*m_propertyAttributeNextRun)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData))
		return false;
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectParameterizedJob::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeActive)->OnSaveMetaObject(flags) ||
		!(*m_propertyAttributeSchedule)->OnSaveMetaObject(flags) ||
		!(*m_propertyAttributeLastRun)->OnSaveMetaObject(flags) ||
		!(*m_propertyAttributeNextRun)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags))
		return false;
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectParameterizedJob::OnDeleteMetaObject()
{
	// NOTHING IS FORGOTTEN HERE — see the predefined kind for why. This is the Designer's mark,
	// not the end of the job: the table is still there, its rows are still there, and the user may
	// yet close without saving. What actually ends a job is the RESTRUCTURING, which drops the
	// table in its own transaction; the orphaned records are swept at start-up, when the surviving
	// jobs are known.

	if (!(*m_propertyAttributeActive)->OnDeleteMetaObject() ||
		!(*m_propertyAttributeSchedule)->OnDeleteMetaObject() ||
		!(*m_propertyAttributeLastRun)->OnDeleteMetaObject() ||
		!(*m_propertyAttributeNextRun)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnDeleteMetaObject())
		return false;
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectParameterizedJob::OnReloadMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		ibValueRecordDataObjectParameterizedJob* pDataRef = nullptr;
		if (!cc->FindCompileModule(m_propertyObjectModule->GetMetaObject(), pDataRef))
			return true;
		return pDataRef->InitializeObject();
	}

	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectParameterizedJob::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeActive)->OnBeforeRunMetaObject(flags) ||
		!(*m_propertyAttributeSchedule)->OnBeforeRunMetaObject(flags) ||
		!(*m_propertyAttributeLastRun)->OnBeforeRunMetaObject(flags) ||
		!(*m_propertyAttributeNextRun)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeRunMetaObject(flags))
		return false;
	if (!(*m_propertyManagerModule)->OnBeforeRunMetaObject(flags))
		return false;

	registerSelection();

	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	// Parent points at this same list — the hierarchy's own reference type, resolved once the
	// reference ctor exists (registered by the record base above).
	const ibCtorMetaValueType* typeCtor =
		m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);

	if (typeCtor != nullptr && !(*m_propertyAttributeParent)->ContainType(typeCtor->GetClassType()))
		(*m_propertyAttributeParent)->SetDefaultMetaType(typeCtor->GetClassType());

	return true;
}

bool ibValueMetaObjectParameterizedJob::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeActive)->OnAfterRunMetaObject(flags) ||
		!(*m_propertyAttributeSchedule)->OnAfterRunMetaObject(flags) ||
		!(*m_propertyAttributeLastRun)->OnAfterRunMetaObject(flags) ||
		!(*m_propertyAttributeNextRun)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyObjectModule)->OnAfterRunMetaObject(flags))
		return false;
	if (!(*m_propertyManagerModule)->OnAfterRunMetaObject(flags))
		return false;

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
			return false;

		// The designer builds the compile value and stops — it must not declare a job.
		return cc->AddCompileModule(m_propertyObjectModule->GetMetaObject(),
			[this]() -> ibValue* { return CreateObjectValue(ibObjectMode::OBJECT_ITEM); });
	}

	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(flags))
		return false;

	// AFTER = resolve: the manager module is registered, the table exists, the reference ctor is
	// in the factory — everything the body reaches for is in place.
	return RegisterJobs();
}

bool ibValueMetaObjectParameterizedJob::OnBeforeCloseMetaObject()
{
	// Inversely on the way out — withdraw before the module it calls goes away.
	UnregisterJobs();

	if (!(*m_propertyAttributeActive)->OnBeforeCloseMetaObject() ||
		!(*m_propertyAttributeSchedule)->OnBeforeCloseMetaObject() ||
		!(*m_propertyAttributeLastRun)->OnBeforeCloseMetaObject() ||
		!(*m_propertyAttributeNextRun)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyObjectModule)->OnBeforeCloseMetaObject())
		return false;
	if (!(*m_propertyManagerModule)->OnBeforeCloseMetaObject())
		return false;

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject())
			return cc->RemoveCompileModule(m_propertyObjectModule->GetMetaObject());
		return false;
	}

	return ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectParameterizedJob::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeActive)->OnAfterCloseMetaObject() ||
		!(*m_propertyAttributeSchedule)->OnAfterCloseMetaObject() ||
		!(*m_propertyAttributeLastRun)->OnAfterCloseMetaObject() ||
		!(*m_propertyAttributeNextRun)->OnAfterCloseMetaObject())
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

void ibValueMetaObjectParameterizedJob::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject && m_propertyDefFormObject->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormObject->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormFolder->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormSelect->SetValue(metaForm->GetMetaID());
	else if (metaForm->GetTypeForm() == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormFolderSelect->SetValue(metaForm->GetMetaID());
}

void ibValueMetaObjectParameterizedJob::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormObject && m_propertyDefFormObject->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormObject->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormFolder && m_propertyDefFormFolder->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormFolder->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormSelect && m_propertyDefFormSelect->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormSelect->SetValue(wxNOT_FOUND);
	else if (metaForm->GetTypeForm() == eFormFolderSelect && m_propertyDefFormFolderSelect->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormFolderSelect->SetValue(wxNOT_FOUND);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectParameterizedJob, "ScheduledJob", g_metaParameterizedJobCLSID);
