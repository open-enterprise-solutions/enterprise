////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — rows as jobs (register, run, stamp)
////////////////////////////////////////////////////////////////////////////

// The seam between a metatype and the schedule: a ROW is a job, so every row declares itself
// to ibJobManager, runs through the manager module, and stamps its run in the shared register.
#include "parameterizedJob.h"

#include "backend/appData.h"                       // appData->DesignerMode(), GetJobManager
#include "backend/metaData.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/userInfo.h"                        // ibUserInfo — the identity the job inherits
#include "backend/job/jobManager.h"                  // ibJobManager / ibJobDescription

//***********************************************************************
//*                    NextRun — the moved computation                  *
//***********************************************************************

wxDateTime ibValueMetaObjectParameterizedJob::ComputeNextRun(const ibJobScheduleDescription& schedule, const wxDateTime& lastRun)
{
	// NOT simply lastRun + interval: a schedule is interval AND calendar joined by AND, so the
	// interval says "no earlier than" and the calendar moves that to the next permitted window and
	// day. Without the second half an hourly job with a 02:00–05:00 window would be scheduled for
	// 14:00. This is deliberately the SAME formula the manager uses for a job as a whole, so a row
	// inherits "a time of day means NOT BEFORE" rather than inventing a stricter reading.
	const wxDateTime countFrom = lastRun.IsValid()
		? lastRun + wxTimeSpan::Seconds(schedule.m_intervalSeconds > 0 ? schedule.m_intervalSeconds : 0)
		: wxDateTime::Now();

	return ibJobScheduleRules::NextAllowedAfter(schedule, countFrom);
}

//***********************************************************************
//*                 Registration with the job manager                   *
//***********************************************************************

#include "backend/query/dataQueryBuilder.h"   // the L3 door — the due-row selection
#include "backend/query/columnLayout.h"        // ibFieldSuffix / ibColumnRole — the field-name authority
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — the last-run stamp goes as a field write
#include "reference/reference.h"              // ibValueReferenceDataObject — the one argument a job takes

namespace {

// THE BODY OF ONE ROW — what the manager runs when that row's own schedule says it is due.
//
// One entry per active row, so there is no table walk here and no "who else is due": the manager
// already answered that by picking this entry. The row is loaded once, the handler is called with
// its reference, and the run is stamped on the object that was loaded — the same two steps the
// card's button performs, in the same order.
bool RunParameterizedRowBody(ibSession* session, const ibValueMetaObjectParameterizedJob* metaJob,
	const ibGuid& rowGuid)
{
	// EVERY refusal below THROWS rather than returning quietly. A returned false means "nothing
	// left to do" — the dosage answer — so using it for "I could not run at all" would make a
	// broken job indistinguishable from a finished one: Succeeded in the list, a line in the
	// journal, and nothing executed.
	if (session == nullptr || metaJob == nullptr)
		ibBackendCoreException::Error(_("the job has no session"));
	if (!rowGuid.isValid())
		ibBackendCoreException::Error(_("the job row is missing"));

	// LOAD ONCE, run, stamp — on the object that was loaded. A second copy would move the row's
	// data version behind this one's back, and the next write against it would be refused as
	// "changed by another user".
	ibValuePtr<ibValueRecordDataObjectHierarchyRef> object(
		metaJob->CreateObjectValue(ibObjectMode::OBJECT_ITEM, rowGuid));

	if (object == nullptr)
		ibBackendCoreException::Error(_("the job row could not be read"));

	// STILL ACTIVE? The row may have been switched off between the tick's decision and now, and a
	// switched-off row must not run on a schedule. Its entry is withdrawn on the next write; this
	// is the guard for the window in between.
	ibValue activeValue;
	object->GetValueByMetaID(metaJob->GetDataActive()->GetMetaID(), activeValue);
	if (!activeValue.GetBoolean())
		return false;

	// A throw travels: the worker's promise captures it, the manager journals the run as failed.
	metaJob->RunHandler(rowGuid);
	metaJob->StampLastRun(rowGuid);

	// One row, one pass — there is nothing left over to pace. The next run is this entry's own
	// schedule, computed from the LastRun just written.
	return false;
}

} // namespace
#include "backend/session/session.h"   // ibSession::GetManagerModule — the runtime the handler lives in

bool ibValueMetaObjectParameterizedJob::RunHandler(const ibGuid& objGuid) const
{
	// ONE VERB, four initiators — the tick, the list command, the card's button and script. None
	// of them brings a mechanism of its own, so this is the only place that knows how a job's
	// handler is actually called. What differs is only WHO stamps the row afterwards; see below.
	const ibValueMetaObjectCommonModule* jobModule = GetManagerModule();
	if (jobModule == nullptr)
		ibBackendCoreException::Error(_("the job module is missing"));

	// THE SAME LOOKUP THE MANAGER VALUE USES (commonObject.cpp, ibValueManagerDataObject::
	// CallAsProc): EditModuleManagerFor, not the session's runtime root.
	//
	// Not a nicety — the two are different managers. The root holds the configuration's COMMON
	// modules; a manager module belongs to its metaobject and is registered where that metaobject's
	// metadata lives, which in the Designer is a compile-cache manager and has no runtime root at
	// all. Asking the root found nothing, so a job "ran" and did nothing at all.
	ibValueModuleManager* const mm = ibSession::EditModuleManagerFor(GetMetaData());
	if (mm == nullptr)
		ibBackendCoreException::Error(_("the session has no runtime"));

	ibValueModuleManager::ibValueModuleUnit* const unit = mm->FindCommonModule(jobModule);
	if (unit == nullptr)
		ibBackendCoreException::Error(_("the job module is not registered in this session"));

	// The ONLY argument is the job itself. Everything it needs it reads off its own row, on its own
	// side, under its own rights — which is also what the value gate requires: a loaded object does
	// not cross a session boundary, a reference always does.
	ibValuePtr<ibValueReferenceDataObject> reference(ibValueReferenceDataObject::Create(this, objGuid));
	ibValue jobReference(reference);

	// A throw travels: the worker's promise captures it, the manager journals the run as failed.
	// Swallowing it here would make a broken job look like one that did nothing.
	unit->ExecAsProc(wxT("JobProcessing"), jobReference);
	return true;
}

void ibValueMetaObjectParameterizedJob::StampLastRun(const ibGuid& objGuid) const
{
	if (!objGuid.isValid())
		return;

	// AFTER THE RUN — record it where EVERY job records it: sys_job, through the manager. That
	// table already holds the shared clock of the predefined jobs and of the engine's own, keyed
	// by the job's guid, and a parameterized row is a job like any other — so there is one place
	// that answers "when did this last run", not one per kind.
	//
	// It is also why the stamp is NOT an object write. Three things fall out of that for free:
	//
	//   * A JOB SESSION HAS NO UI. Writing an object asks whether it is open on a form and should
	//     be refreshed, and that question routes through the frame — which a scheduled run does not
	//     have, and must not reach for.
	//   * A CARD IS NOT INVALIDATED BY ITS OWN RUN. An object write bumps the row's data version,
	//     so an open card would be told its data "was changed by another user" — by itself.
	//   * RUNNING IS NOT EDITING. Execute is its own right; a stamp through the object write would
	//     silently require Write as well.
	ibJobManager::WriteSharedLastRun(objGuid, GetRowJobName(objGuid), wxDateTime::Now());
}

bool ibValueMetaObjectParameterizedJob::RunJobByReference(const ibGuid& objGuid) const
{
	// The NO-OPEN-CARD path — the tick, the list command, script. There is no loaded object here,
	// so one is loaded for the stamp, and it is the only holder of that row's version.
	//
	// ⚠ The card's button must NOT come through here. It holds its own loaded object, and stamping
	// a SECOND one bumps the row's data version behind the first one's back: the next Save from
	// that card is refused as "changed by another user" — by itself, one second ago. The card
	// therefore runs the handler and stamps ITSELF (ibValueRecordDataObjectParameterizedJob::
	// ExecuteJob), which is the same two steps against the object already in hand.
	if (!RunHandler(objGuid))
		return false;

	ibValuePtr<ibValueRecordDataObjectHierarchyRef> object(CreateObjectValue(ibObjectMode::OBJECT_ITEM, objGuid));
	StampLastRun(objGuid);
	return true;
}

wxString ibValueMetaObjectParameterizedJob::GetRowJobName(const ibGuid& objGuid) const
{
	// Name reads for a person, guid keeps it unique. Both, because this string is what the journal
	// line and the Active Users row show: "Exchange" alone would not say WHICH exchange, and a bare
	// guid would not say what it is.
	return GetJobName() + wxT(".") + objGuid.str();
}

bool ibValueMetaObjectParameterizedJob::RegisterRow(const ibGuid& objGuid, bool active,
	const ibJobScheduleDescription& schedule, const wxString& presentation) const
{
	// The Designer never runs a configuration's jobs. It edits them.
	if (appData == nullptr || appData->DesignerMode())
		return true;

	ibJobManager* const manager = ibApplicationData::GetJobManager();
	if (manager == nullptr)
		return true;   // launcher / pre-bootstrap — nothing to declare against

	// THE RECORD IS WRITTEN EITHER WAY — the register lists every job there is, with a flag saying
	// whether it is on. Only a DELETED row loses its record (the row object drops it on delete).
	//
	// That is what makes the dates behave the same for everybody: a switched-off job that somebody
	// runs by hand still has somewhere to stamp its run, so it still shows when it last ran and
	// when it would run next — which is exactly what one looks at before switching it back on.
	ibJobManager::ibJobSettings record;
	record.m_key = objGuid;
	record.m_name = GetRowJobName(objGuid);
	record.m_active = active && IsUsed();
	record.m_schedule = schedule;
	if (ibJobManager::WriteSharedSettings(record) == ibJobManager::ibWriteOutcome::Refused) {
		// The register did not take the setting. Say so — a switch that did not stick is exactly
		// the failure a person must not discover by watching a job they turned off keep running.
		//
		// NoBase is not that failure and is not reported: this runs at bring-up, where a row
		// declared before the connection is up has been asked nothing. Logging it would train
		// whoever reads the log to ignore the line that matters.
		wxLogDebug(wxT("job row '%s': the register refused the settings write"), presentation);
	}

	// … and the row is declared ALIVE whatever its switch says, so the orphan sweep at start-up
	// does not mistake "switched off" for "no longer exists".
	manager->NoteLiveKey(objGuid);

	// EVERY row is REGISTERED, switched on or not — and the switch is what the tick reads.
	//
	// That is the difference between "not due" and "not there". A registered-but-inactive job is
	// never picked up by the schedule (ibJobManager::IsDue answers false on m_active), and yet it
	// is a job the manager knows: RunNow finds it, gives it a session of its own and runs it once,
	// out of turn, ignoring both the calendar and the switch. Withdrawing it instead would leave
	// "Execute" on a switched-off row with nothing to ask — which is exactly what it did.
	//
	// ALREADY REGISTERED → update IN PLACE, never unregister-then-register. Not an optimisation:
	// this very call happens INSIDE a run (the body stamps its last run, the stamp writes the row,
	// the write lands here), so unregistering would make the run wait for its own future.
	if (manager->ApplySettings(objGuid, record.m_active, schedule))
		return true;

	ibJobDescription desc;
	desc.m_origin = ibJobOrigin::Configuration;
	desc.m_name = GetRowJobName(objGuid);
	// THE ROW IS THE OWNER OF ITS OWN SWITCH — say so, rather than leaving the description's
	// default (true) and trusting Register to overwrite it from the register a moment later.
	//
	// That trust is what let a switched-off row RUN. Register reads sys_job and adopts what it
	// finds, but only when it finds a record: a settings write that did not land (a busy base, a
	// row this process has not written yet) left the declaration saying "active" — and the seed
	// branch then WROTE that back, making the register agree with the mistake. For a predefined or
	// a platform job the register really is the owner (there is no row to ask), which is why the
	// rule is stated here, on the side that has one, instead of being changed there for everybody.
	desc.m_active = record.m_active;
	// The ROW's guid is the key: its settings and its shared clock belong to the row, and the row
	// survives every rename above it.
	desc.m_key = objGuid;
	desc.m_schedule = schedule;
	desc.m_retryCount = m_propertyRetryCount->GetValueAsInteger();
	desc.m_retryIntervalSeconds = m_propertyRetryInterval->GetValueAsInteger();

	// EXCLUSIVE — and the emphasis is on WHAT is exclusive: the ROW, not the metaobject. Two
	// different rows are two different jobs and run side by side, which is the ordinary case;
	// the same row must never run twice at the same moment, and in a file-mode base "at the same
	// moment" spans MACHINES. Every copy of the application that opened that file registers the
	// same rows off the same table and ticks its own schedule, so without the claim two desks
	// would run one exchange together. The claim is keyed on the row's guid (`Job.<key>`), so it
	// says exactly that and nothing wider: whoever takes it runs, the rest read the shared clock
	// and record a skip.
	//
	// In a server base the same mechanism costs nothing and says the same thing — there simply
	// happens to be one process holding the schedule.
	desc.m_exclusive = true;

	// WHO IT RUNS AS — whoever opened the configuration, when there is such a user. No user is a
	// legitimate state, not a refusal: no identity means no row filter, so the job sees everything,
	// which is what most unattended work wants.
	const ibUserInfo& user = appData->GetUserInfo();
	if (user.IsOk())
		desc.m_runAsUser = ibGuid(user.m_strUserGuid);

	// The metaobject outlives every run of it (the manager is torn down before the metadata is),
	// so capturing `this` is safe — and the withdrawal on close closes the window regardless.
	const ibValueMetaObjectParameterizedJob* metaJob = this;
	const ibGuid rowGuid = objGuid;
	desc.m_body = [metaJob, rowGuid](ibSession* session) {
		return RunParameterizedRowBody(session, metaJob, rowGuid);
	};

	if (!manager->Register(std::move(desc))) {
		// Register logs the reason itself. A row that could not be declared must not stop a
		// configuration from opening — the failure belongs in the log, not in the user's face.
		wxLogDebug(wxT("job row '%s' was not registered"), presentation);
		return true;
	}

	// THE CARD HAS THE LAST WORD — and this line is why a switched-off row stopped running only
	// after somebody re-saved it.
	//
	// Register adopts whatever sys_job holds for this key, which is right for a predefined or a
	// platform job: they have no row, so the register IS where their switch and their schedule
	// live. A parameterized row has a card, and the card is the owner — so a record left over from
	// when the row was still switched on (or carrying a schedule since edited) must not decide
	// what the schedule does at start-up. Re-saving the row worked because that path goes through
	// ApplySettings, which states the row's own opinion; the start-up census did not, and the
	// register won. Now both do.
	manager->ApplySettings(objGuid, record.m_active, schedule);

	return true;
}

bool ibValueMetaObjectParameterizedJob::UnregisterRow(const ibGuid& objGuid, bool forgetState) const
{
	if (appData == nullptr || appData->DesignerMode())
		return true;

	if (ibJobManager* const manager = ibApplicationData::GetJobManager())
		manager->Unregister(GetRowJobName(objGuid));

	// A row that was DELETED takes its shared record with it — otherwise sys_job keeps the
	// settings and the clock of a job that no longer exists. Merely switching one off does not:
	// its "when did it last run" is exactly what one wants to still see, and to come back to when
	// it is switched on again.
	if (forgetState)
		ibJobManager::ForgetSharedState(objGuid);

	return true;
}

bool ibValueMetaObjectParameterizedJob::RegisterJobs()
{
	// The Designer never runs a configuration's jobs. It edits them.
	if (appData == nullptr || appData->DesignerMode())
		return true;
	if (ibApplicationData::GetJobManager() == nullptr)
		return true;

	const ibBackendQueryable* queryable = GetQueryable();
	if (queryable == nullptr)
		return true;

	// ONE WALK, at start-up: EVERY row declares itself, active or not. The active ones go onto the
	// schedule; the rest only refresh their record and are noted as existing, so their last run
	// survives being switched off and the orphan sweep does not take them for deleted.
	//
	// After this the table is not read again on a tick — each active row is an entry with its own
	// schedule, and a row written later re-registers itself from its own Write.
	try {
		ibDataQueryBuilder query;
		query.From(queryable)
			.Where(GetDataIsFolder(), ibValue(false));   // a folder is not a job

		ibReadPageRequest page;
		page.m_count = 0;   // all of them — this is the start-up census, not a paged read

		ibDataQueryResult selection = query.Execute(page);
		while (selection.Next()) {

			// The identity column by NAME, and the guid from the reference itself — see the same read in
			// catalogManager_impl.cpp for what the two guesses on this line used to cost.
			const ibValueReferenceDataObject* const rowReference =
				selection.GetValue(GetDataReference()).ConvertToType<ibValueReferenceDataObject>();
			if (rowReference == nullptr)
				continue;
			const ibGuid rowGuid = rowReference->GetGuid().GetGuid();
			if (!rowGuid.isValid())
				continue;

			const ibValue activeValue = selection.GetValue(GetDataActive());
			const ibValue scheduleValue = selection.GetValue(GetDataSchedule());
			const ibValue description = selection.GetValue(GetDataDescription());

			ibValueSchedule* schedule = nullptr;
			if (!scheduleValue.ConvertToValue(schedule) || schedule == nullptr)
				continue;   // no schedule on the row — nothing says when it should run

			RegisterRow(rowGuid, activeValue.GetBoolean(), schedule->GetSchedule(), description.GetString());
		}
	}
	catch (const ibBackendException& err) {
		// A table that cannot be read at start-up must not stop the configuration from opening —
		// the rows are still there and re-register themselves as they are written.
		wxLogDebug(wxT("scheduled job '%s': rows were not registered: %s"),
			GetJobName(), err.GetErrorDescription());
	}

	return true;
}

bool ibValueMetaObjectParameterizedJob::UnregisterJobs()
{
	if (appData == nullptr || appData->DesignerMode())
		return true;

	ibJobManager* const manager = ibApplicationData::GetJobManager();
	if (manager == nullptr)
		return true;

	// Withdraw by PREFIX — the rows were registered as "<JobName>.<rowGuid>", and by the time a
	// configuration closes the table may no longer be readable. Asking the manager what it holds
	// is both cheaper and safe.
	manager->UnregisterByPrefix(GetJobName() + wxT("."));
	return true;
}

