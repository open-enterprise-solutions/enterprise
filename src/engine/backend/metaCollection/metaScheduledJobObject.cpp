////////////////////////////////////////////////////////////////////////////
//	Description : predefined scheduled job — the configuration's unattended work
////////////////////////////////////////////////////////////////////////////

#include "metaScheduledJobObject.h"

#include "backend/metaData.h"
#include "backend/appData.h"                       // appData->DesignerMode(), GetJobManager
#include "backend/userInfo.h"                      // ibUserInfo — the identity a job inherits
#include "backend/job/jobManager.h"                // ibJobManager / ibJobDescription
#include "backend/moduleManager/moduleManager.h"   // ibValueModuleManagerRuntimeConfiguration
#include "backend/session/session.h"               // ibSession::GetManagerModule

//***********************************************************************
//*                          Scheduled job object                       *
//***********************************************************************

ibValueMetaObjectScheduledJob::ibValueMetaObjectScheduledJob(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
	// One handler shape, shared with the parameterized kind — which passes the job's own reference
	// as the single argument. Keeping the NAME identical is what lets the two kinds be described,
	// documented and debugged as one thing.
	(*m_propertyJobModule)->SetDefaultProcedure(wxT("JobProcessing"), ibContentHelper::eProcedureHelper);
}

//***********************************************************************
//*                    THE BODY — what the manager runs                 *
//***********************************************************************

namespace {

// Run the job's handler on the session the manager handed us.
//
// The module is a MANAGER module, so by the time a job runs it is already registered and compiled
// with the session's own modules — this only has to find its unit and call into it. Nothing is
// compiled per run, and nothing is passed in: a predefined job carries no parameters, because
// there is only ever one of it.
//
// Returns the DOSAGE answer, not success (jobManager.h): false = nothing left for now. A predefined
// job runs to completion in one pass; when a handler ever needs pacing, this is the line that turns
// into "work remains" and the manager re-queues on the next tick.
bool RunScheduledJobBody(ibSession* session, const ibValueMetaObjectScheduledJob* metaJob)
{
	if (session == nullptr || metaJob == nullptr)
		return false;

	// EVERY refusal below THROWS rather than returning quietly. A returned false means "nothing
	// left to do" — the dosage answer — so using it for "I could not run at all" makes a broken
	// job indistinguishable from a job that finished its work: Succeeded in the list, a line in
	// the journal, and nothing actually executed. That is the worst failure mode a scheduled job
	// can have, because it looks exactly like the feature working.
	// The messages carry NO job name: the manager's journal line already opens with
	// "Job '<name>' failed:", and repeating it there produced an entry long enough to be elided in
	// the middle — losing exactly the part that says what went wrong.
	const ibValueMetaObjectManagerModule* jobModule = metaJob->GetJobModule();
	if (jobModule == nullptr)
		ibBackendCoreException::Error(_("the job module is missing"));

	// The session's runtime root. A scheduled session is authenticated and therefore builds one, so
	// a null here means the run has no runtime at all and there is nothing to call into.
	ibValueModuleManagerRuntimeConfiguration* const mm = session->GetManagerModule();
	if (mm == nullptr)
		ibBackendCoreException::Error(_("the session has no runtime"));

	ibValueModuleManager::ibValueModuleUnit* const unit = mm->FindCommonModule(jobModule);
	if (unit == nullptr)
		ibBackendCoreException::Error(_("the job module is not registered in this session"));

	// Throwing is allowed and expected to travel: the worker's promise captures it, the manager
	// logs it and the run is journalled as failed. Swallowing it here would make a broken job look
	// like a job that did nothing.
	unit->ExecAsProc(wxT("JobProcessing"));
	return false;
}

} // namespace

//***********************************************************************
//*                 Registration with the job manager                   *
//***********************************************************************

bool ibValueMetaObjectScheduledJob::RegisterJob()
{
	// The Designer never runs a configuration's jobs. It edits them.
	if (appData == nullptr || appData->DesignerMode())
		return true;

	ibJobManager* const manager = ibApplicationData::GetJobManager();
	if (manager == nullptr)
		return true;   // launcher / pre-bootstrap — nothing to declare against

	if (!IsUsed())
		return true;   // declared but switched off: visible in the tree, absent from the schedule

	ibJobDescription desc;
	desc.m_origin   = ibJobOrigin::Configuration;
	desc.m_name     = GetJobName();
	desc.m_schedule = GetSchedule();
	desc.m_retryCount = GetRetryCount();
	desc.m_retryIntervalSeconds = GetRetryInterval();

	// WHO IT RUNS AS. Whoever opened the configuration, when there is such a user — the same rule a
	// background run follows when it adopts its caller, and it keeps rights and RLS honest: the job
	// sees exactly what that user sees.
	//
	// NO user is a legitimate state, not a refusal: an open-access base has nobody to inherit, and
	// the job then runs with the full view because no identity means no row filter is built. The
	// runtime it needs is arranged by the manager either way (OpenRunSession).
	//
	// Until the metaobject carries its own "run as" property, this is the whole policy.
	const ibUserInfo& user = appData->GetUserInfo();
	if (user.IsOk())
		desc.m_runAsUser = ibGuid(user.m_strUserGuid);

	// The metaobject outlives every run of it (the manager is torn down before the metadata is),
	// so capturing `this` is safe — and UnregisterJob on close closes the window regardless.
	const ibValueMetaObjectScheduledJob* metaJob = this;
	desc.m_body = [metaJob](ibSession* session) { return RunScheduledJobBody(session, metaJob); };

	if (!manager->Register(std::move(desc))) {
		// Register logs the reason itself. A job that could not be declared must not stop a
		// configuration from opening — the failure belongs in the log, not in the user's face.
		wxLogDebug(wxT("scheduled job '%s' was not registered"), GetJobName());
		return true;
	}

	return true;
}

bool ibValueMetaObjectScheduledJob::UnregisterJob()
{
	if (appData == nullptr || appData->DesignerMode())
		return true;

	if (ibJobManager* const manager = ibApplicationData::GetJobManager())
		manager->Unregister(GetJobName());

	return true;
}

bool ibValueMetaObjectScheduledJob::RunNow() const
{
	if (ibJobManager* const manager = ibApplicationData::GetJobManager())
		return manager->RunNow(GetJobName());
	return false;
}

//***********************************************************************
//*        lifecycle — the module first, our registration after         *
//***********************************************************************

bool ibValueMetaObjectScheduledJob::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	return (*m_propertyJobModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectScheduledJob::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyJobModule)->OnLoadMetaObject(metaData))
		return false;
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectScheduledJob::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyJobModule)->OnSaveMetaObject(flags))
		return false;
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectScheduledJob::OnDeleteMetaObject()
{
	if (!(*m_propertyJobModule)->OnDeleteMetaObject())
		return false;
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectScheduledJob::OnBeforeRunMetaObject(int flags)
{
	// BEFORE = register. The module puts itself into the module storage here; the job cannot be
	// declared yet, because its body resolves that very module.
	if (!(*m_propertyJobModule)->OnBeforeRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectScheduledJob::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyJobModule)->OnAfterRunMetaObject(flags))
		return false;
	if (!ibValueMetaObject::OnAfterRunMetaObject(flags))
		return false;

	// AFTER = resolve: everything the body will need is in place.
	return RegisterJob();
}

bool ibValueMetaObjectScheduledJob::OnBeforeCloseMetaObject()
{
	// Inversely on the way out — withdraw before the module it calls goes away.
	UnregisterJob();

	if (!(*m_propertyJobModule)->OnBeforeCloseMetaObject())
		return false;
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectScheduledJob::OnAfterCloseMetaObject()
{
	if (!(*m_propertyJobModule)->OnAfterCloseMetaObject())
		return false;
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          load & save from DB                        *
//***********************************************************************

bool ibValueMetaObjectScheduledJob::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyJobModule->ReadNodeValue(node.GetProperty(m_propertyJobModule->GetName()));
	m_propertyUse->ReadNodeValue(node.GetProperty(m_propertyUse->GetName()));
	m_propertySchedule->ReadNodeValue(node.GetProperty(m_propertySchedule->GetName()));
	m_propertyRetryCount->ReadNodeValue(node.GetProperty(m_propertyRetryCount->GetName()));
	m_propertyRetryInterval->ReadNodeValue(node.GetProperty(m_propertyRetryInterval->GetName()));
	return true;
}

bool ibValueMetaObjectScheduledJob::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyJobModule->GetName(), m_propertyJobModule->GetNodeValue());
	node.SetProperty(m_propertyUse->GetName(),           m_propertyUse->GetNodeValue());
	node.SetProperty(m_propertySchedule->GetName(),      m_propertySchedule->GetNodeValue());
	node.SetProperty(m_propertyRetryCount->GetName(),    m_propertyRetryCount->GetNodeValue());
	node.SetProperty(m_propertyRetryInterval->GetName(), m_propertyRetryInterval->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectScheduledJob, "ScheduledJob", g_metaScheduledJobCLSID);
