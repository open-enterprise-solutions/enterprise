////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — the row object
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJob.h"

#include "backend/appData.h"
#include "backend/metaData.h"
#include "backend/session/session.h"
#include "backend/job/jobManager.h"   // the shared last-run lives in sys_job, for every kind of job
#include "backend/logger/logger.h"    // a run by hand reports itself, exactly as a scheduled one does
#include "reference/reference.h"

//*********************************************************************************************
//*                                     Object value                                          *
//*********************************************************************************************

ibValueRecordDataObjectParameterizedJob::ibValueRecordDataObjectParameterizedJob(
	const ibValueMetaObjectParameterizedJob* metaObject, const ibGuid& objGuid, ibObjectMode objMode)
	: ibValueRecordDataObjectHierarchyRef(metaObject, objGuid, objMode)
{
	m_members.Bind(this, &ibValueRecordDataObjectParameterizedJob::FillMethods);
}

ibValueRecordDataObjectParameterizedJob::ibValueRecordDataObjectParameterizedJob(const ibValueRecordDataObjectParameterizedJob& source)
	: ibValueRecordDataObjectHierarchyRef(source)
{
	m_members.Bind(this, &ibValueRecordDataObjectParameterizedJob::FillMethods);
}

ibFormID ibValueRecordDataObjectParameterizedJob::GetCurrentObjectFormID() const
{
	return m_objMode == ibObjectMode::OBJECT_ITEM
		? ibValueMetaObjectParameterizedJob::eFormObject
		: ibValueMetaObjectParameterizedJob::eFormFolder;
}

const ibSourceExplorer* ibValueRecordDataObjectParameterizedJob::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(), false);

	ibValueMetaObjectParameterizedJob* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		m_sourceExplorer.AppendColumn(metaRef->GetDataCode(), false);
		m_sourceExplorer.AppendColumn(metaRef->GetDataDescription()->GetQueryColumn());
		m_sourceExplorer.AppendColumn(metaRef->GetDataParent()->GetQueryColumn());
		// The job's own four. They are ordinary columns of the row, which is exactly why the list,
		// the filters and any report over "which jobs fail most" cost this subsystem nothing.
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			m_sourceExplorer.AppendColumn(metaRef->GetDataActive()->GetQueryColumn());
			m_sourceExplorer.AppendColumn(metaRef->GetDataSchedule()->GetQueryColumn());
			m_sourceExplorer.AppendColumn(metaRef->GetDataLastRun()->GetQueryColumn());
			m_sourceExplorer.AppendColumn(metaRef->GetDataNextRun()->GetQueryColumn());
		}
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		const ibItemMode attrUse = object->GetItemMode();
		const bool wanted = (m_objMode == ibObjectMode::OBJECT_ITEM)
			? (attrUse == ibItemMode::ibItemMode_Item || attrUse == ibItemMode::ibItemMode_Folder_Item)
			: (attrUse == ibItemMode::ibItemMode_Folder || attrUse == ibItemMode::ibItemMode_Folder_Item);
		if (wanted && !m_metaObject->IsDataReference(object->GetMetaID()))
			m_sourceExplorer.AppendColumn(object->GetQueryColumn());
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		const ibItemMode tableUse = object->GetTableUse();
		const bool wanted = (m_objMode == ibObjectMode::OBJECT_ITEM)
			? (tableUse == ibItemMode::ibItemMode_Item || tableUse == ibItemMode::ibItemMode_Folder_Item)
			: (tableUse == ibItemMode::ibItemMode_Folder || tableUse == ibItemMode::ibItemMode_Folder_Item);
		if (wanted && object != nullptr && !object->IsDeleted()) {
			ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
			for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject())
				tblNode.AppendColumn(tblCol->GetQueryColumn());
		}
	}

	return &m_sourceExplorer;
}

//***********************************************************************************************
//*                                    Write & execute                                          *
//***********************************************************************************************

bool ibValueRecordDataObjectParameterizedJob::WriteObject()
{
	if (!ibValueRecordDataObjectHierarchyRef::WriteObject())
		return false;

	// THE ROW ANNOUNCES ITSELF. A written row is the only thing that knows its own schedule and
	// its own switch, so it is what tells the manager — a fresh one appears on the schedule, an
	// edited one replaces its entry, a switched-off one leaves it. A FOLDER carries none of these
	// requisites and is never a job.
	const ibValueMetaObjectParameterizedJob* metaJob = nullptr;
	if (m_objMode == ibObjectMode::OBJECT_ITEM
		&& m_metaObject->ConvertToValue(metaJob) && metaJob != nullptr) {

		ibValue activeValue, scheduleValue, description;
		GetValueByMetaID(metaJob->GetDataActive()->GetMetaID(), activeValue);
		GetValueByMetaID(metaJob->GetDataSchedule()->GetMetaID(), scheduleValue);
		GetValueByMetaID(metaJob->GetDataDescription()->GetMetaID(), description);

		ibValueSchedule* schedule = nullptr;
		if (scheduleValue.ConvertToValue(schedule) && schedule != nullptr) {
			metaJob->RegisterRow(m_objGuid, activeValue.GetBoolean(),
				schedule->GetSchedule(), description.GetString());
		}
		else {
			// No schedule on the row — nothing says when it should run, so it is not on the
			// schedule at all. Withdrawn rather than left with a stale entry.
			metaJob->UnregisterRow(m_objGuid);
		}
	}

	return true;
}

bool ibValueRecordDataObjectParameterizedJob::DeleteObject()
{
	const ibValueMetaObjectParameterizedJob* metaJob = nullptr;
	if (m_metaObject->ConvertToValue(metaJob) && metaJob != nullptr)
		metaJob->UnregisterRow(m_objGuid, /*forgetState*/ true);   // the row goes, its record goes

	return ibValueRecordDataObjectHierarchyRef::DeleteObject();
}

bool ibValueRecordDataObjectParameterizedJob::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	const ibValueMetaObjectParameterizedJob* metaJob = nullptr;

	// LAST RUN and NEXT RUN are GENERATED, never stored in the row.
	//
	// The last run lives in sys_job — the one table every kind of job records itself in, keyed by
	// guid — so the row does not keep a second copy that could disagree with it. The next run is a
	// pure function of (schedule, last run), computed when asked, so it cannot go stale after a
	// restart, a clock change or a schedule edit.
	//
	// A FOLDER has neither (attribute usage = Items), so it answers the empty date — which is also
	// what its card shows: a group is not a job.
	if (m_metaObject->ConvertToValue(metaJob) && metaJob != nullptr
		&& (id == metaJob->GetDataNextRun()->GetMetaID()
			|| id == metaJob->GetDataLastRun()->GetMetaID())) {

		if (m_objMode != ibObjectMode::OBJECT_ITEM) {
			pvarMetaVal = ibValue(ibValueTypes::TYPE_DATE);
			return true;
		}

		const wxDateTime lastRun = ibJobManager::ReadSharedLastRun(m_objGuid);

		if (id == metaJob->GetDataLastRun()->GetMetaID()) {
			pvarMetaVal = lastRun.IsValid() ? ibValue(lastRun) : ibValue(ibValueTypes::TYPE_DATE);
			return true;
		}

		ibValue scheduleValue;
		ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(metaJob->GetDataSchedule()->GetMetaID(), scheduleValue);

		ibValueSchedule* schedule = nullptr;
		if (!scheduleValue.ConvertToValue(schedule) || schedule == nullptr) {
			pvarMetaVal = ibValue(ibValueTypes::TYPE_DATE);
			return true;
		}

		const wxDateTime nextRun = ibValueMetaObjectParameterizedJob::ComputeNextRun(schedule->GetSchedule(), lastRun);

		// An INVALID answer means the calendar names no moment within a year — a schedule naming,
		// say, February 31st. It reads as the empty date, which is a thing to SEE on the card
		// rather than a job that quietly never runs.
		pvarMetaVal = nextRun.IsValid() ? ibValue(nextRun) : ibValue(ibValueTypes::TYPE_DATE);
		return true;
	}

	return ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(id, pvarMetaVal);
}

bool ibValueRecordDataObjectParameterizedJob::ExecuteJob()
{
	const ibValueMetaObjectParameterizedJob* metaJob = nullptr;
	if (!m_metaObject->ConvertToValue(metaJob) || metaJob == nullptr)
		return false;


	// A folder is not a job: it groups them. Refusing loudly beats running a handler with a
	// reference that carries none of the requisites it will read.
	if (m_objMode != ibObjectMode::OBJECT_ITEM)
		ibBackendCoreException::Error(_("a group cannot be executed"));

	if (!metaJob->AccessRight_Execute())
		ibBackendCoreException::Error(_("insufficient rights to execute the job"));

	// QUEUED, NOT RUN HERE. "Execute" asks the manager to run this job now; it does not execute
	// the handler on the caller's thread.
	//
	// Three things follow, and each of them is the reason:
	//   * A job's work belongs on a JOB SESSION — its own connection, its own identity, its own
	//     row in Active Users, its own line in the journal. Run inline it would borrow the
	//     window's session and appear nowhere.
	//   * A window must not freeze for the length of an exchange. Inline, a ten-minute job is a
	//     ten-minute frozen card.
	//   * A CLIENT MAY NOT BE WHERE THE WORK RUNS. A thin client has no scheduler and no
	//     database; asking the manager is a request that reaches whoever does, while calling the
	//     handler in place could only ever work on a fat client over a file base.
	ibJobManager* const manager = ibApplicationData::GetJobManager();
	if (manager == nullptr)
		ibBackendCoreException::Error(_("scheduled jobs are not available in this context"));

	// A BACKGROUND RUN — always, whatever the switch says.
	//
	// "Execute" is not the schedule doing something early: it is a one-off run of this row, asked
	// for by a person. So it goes where one-off work goes — a background run on a session of its
	// own, started and forgotten. Active or not makes no difference: the schedule was never
	// consulted.
	//
	// The two steps inside are the same ones the scheduled body performs, in the same order, so a
	// run by hand and a run by calendar leave the row in identical states.
	const ibValueMetaObjectParameterizedJob* const meta = metaJob;
	const ibGuid rowGuid = m_objGuid;
	const wxString jobName = metaJob->GetRowJobName(m_objGuid);

	auto run = manager->StartBackground(
		[meta, rowGuid, jobName](ibSession*) -> ibValue {
			// WHAT FAILS HERE MUST BE VISIBLE. A background run keeps its error inside its own
			// handle, and nobody is holding this one — started and forgotten is the whole point.
			// Without this line a broken handler is indistinguishable from a job that ran and did
			// nothing, which is the one failure mode a scheduled job must never have.
			try {
				meta->RunHandler(rowGuid);
				meta->StampLastRun(rowGuid);

				if (ibLogger* const log = ibApplicationData::GetLogger())
					log->Audit(wxT("job"), wxT("finished"),
						wxString::Format(_("Job '%s' finished"), jobName));
			}
			catch (const ibBackendException& err) {
				if (ibLogger* const log = ibApplicationData::GetLogger())
					log->Audit(wxT("job"), wxT("failed"),
						wxString::Format(_("Job '%s' failed: %s"), jobName, err.GetErrorDescription()));
				throw;
			}

			return ibValue();
		},
		wxString::Format(wxT("job: %s"), jobName));

	return run != nullptr;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

enum Func {
	enIsNew = 0,
	enCopy,
	enFill,
	enWrite,
	enDelete,
	enModified,
	enExecute,
	enGetForm,
	enGetTemplate,
	enGetMetadata,
	enLock,
	enUnlock
};

void ibValueRecordDataObjectParameterizedJob::FillMethods(ibMemberTable& helper) const
{
	// Order is load-bearing — CallAsFunc switches on the method index (enIsNew = 0 …).
	helper.AppendFunc(wxT("IsNew"), wxT("IsNew()"));
	helper.AppendFunc(wxT("Copy"), wxT("Copy()"));
	helper.AppendFunc(wxT("Fill"), 1, wxT("Fill(object)"));
	helper.AppendFunc(wxT("Write"), wxT("Write()"));
	helper.AppendFunc(wxT("Delete"), wxT("Delete()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	// The second verb — next to Write, which is the whole shape of a parameterized job.
	helper.AppendFunc(wxT("Execute"), wxT("Execute()"));
	helper.AppendFunc(wxT("GetFormObject"), 3, wxT("GetFormObject(name : string, owner : any , id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	helper.AppendProc(wxT("Lock"), wxT("Lock()"));
	helper.AppendProc(wxT("Unlock"), wxT("Unlock()"));
}

bool ibValueRecordDataObjectParameterizedJob::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr)
			return m_procUnit->SetPropVal(GetPropName(lPropNum), varPropVal);
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(m_members.GetPropData(lPropNum), varPropVal);
	}

	return false;
}

bool ibValueRecordDataObjectParameterizedJob::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr)
			return m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal);
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (m_metaObject->IsDataReference(lPropData)) {
			pvarPropVal = GetReference();
			return true;
		}
		return GetValueByMetaID(lPropData, pvarPropVal);
	}
	return false;
}

bool ibValueRecordDataObjectParameterizedJob::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enIsNew:
		pvarRetValue = m_newObject;
		return true;
	case enCopy:
		pvarRetValue = CopyObject();
		return true;
	case enFill:
		FillObject(*paParams[0]);
		return true;
	case enWrite:
		WriteObject();
		return true;
	case enDelete:
		DeleteObject();
		return true;
	case enModified:
		pvarRetValue = m_objModified;
		return true;
	case enExecute:
		pvarRetValue = ExecuteJob();
		return true;
	case Func::enGetForm:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case Func::enGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case Func::enGetMetadata:
		pvarRetValue = m_metaObject;
		return true;
	case Func::enLock:
		TryAcquireFormLock();
		return true;
	case Func::enUnlock:
		ReleaseFormLock();
		return true;
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}
