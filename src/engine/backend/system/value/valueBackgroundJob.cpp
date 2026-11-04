////////////////////////////////////////////////////////////////////////////
//	Description : BackgroundJob — the script handle on a background run
////////////////////////////////////////////////////////////////////////////

#include "valueBackgroundJob.h"

#include "backend/job/jobManager.h"   // ibBackgroundRun
#include "backend/appData.h"          // ibApplicationInstance::GetJobManager
#include "backend/backend_exception.h"
#include "backend/system/systemManagerEnum.h"   // BackgroundJobState
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueGuid.h"
#include "backend/system/value/valueMap.h"      // the filter Structure

#include <algorithm>

namespace {

// ONE NUMBERING for functions and procedures: the member table numbers its methods in the order they are
// appended, whichever kind they are, and that number is what CallAsFunc and CallAsProc receive. Cancel had
// its own enum starting at 0, so `job.Cancel()` asked for method 5 and was answered by nothing.
enum Method {
	enIsComplete = 0,
	enWait,
	enResult,
	enError,
	enActivity,
	enCancel,
};

enum Prop {
	enState = 0,
	enBegin,
	enEnd,
	enKey,
	enDescription,
	enMethodName,
	enUUID,
};

// The member of BackgroundJobState a run stands at.
ibValue StateValue(ibBackgroundJobState state)
{
	return ibValue::CreateEnumObject<ibValueEnumBackgroundJobState>(state);
}

} // namespace

// Order MUST match the enums above — the method number is the index into this
// table, so a swap here calls the wrong case below.
void ibValueBackgroundJob_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(ibT("IsComplete"), ibT("IsComplete()"));
	helper.AppendFunc(ibT("Wait"), 1, ibT("Wait(timeoutMs : number)"));
	helper.AppendFunc(ibT("Result"), ibT("Result()"));
	helper.AppendFunc(ibT("Error"), ibT("Error()"));
	helper.AppendFunc(ibT("Activity"), ibT("Activity()"));
	helper.AppendProc(ibT("Cancel"), ibT("Cancel()"));

	helper.AppendProp(ibT("State"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Begin"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("End"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Key"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Description"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("MethodName"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("UUID"), true, false, wxNOT_FOUND);
}

ibValueBackgroundJob::ibValueBackgroundJob()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true)
{
}

ibValueBackgroundJob::ibValueBackgroundJob(std::shared_ptr<ibBackgroundRun> run)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_run(std::move(run))
{
}

ibValueBackgroundJob::~ibValueBackgroundJob()
{
	// Nothing to tear down: dropping the handle deliberately does NOT stop the
	// run. The run holds its own session and releases it when it finishes, which
	// is what makes "start and forget" work.
}

ibString ibValueBackgroundJob::GetString() const
{
	if (m_run == nullptr)
		return ibT("BackgroundJob");
	const ibString description = m_run->Request().m_description;
	if (!m_run->IsComplete()) {
		if (!description.IsEmpty())
			return description;
		return m_run->Activity().IsEmpty() ? ibString(ibT("BackgroundJob: running"))
		                                   : m_run->Activity();
	}
	if (!description.IsEmpty())
		return description;
	return m_run->Error().IsEmpty() ? ibT("BackgroundJob: done")
	                                : ibT("BackgroundJob: failed");
}

bool ibValueBackgroundJob::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                      ibValue** paParams, const long lSizeArray)
{
	if (m_run == nullptr)
		return false;

	switch (lMethodNum) {
	case enIsComplete:
		pvarRetValue = m_run->IsComplete();
		return true;
	case enWait:
		// 0 (or no argument) waits forever — a script that asks to wait means it.
		pvarRetValue = m_run->Wait(lSizeArray > 0 ? paParams[0]->GetInteger() : 0);
		return true;
	case enResult:
		pvarRetValue = m_run->Result();
		return true;
	case enError:
		pvarRetValue = m_run->Error();
		return true;
	case enActivity:
		pvarRetValue = m_run->Activity();
		return true;
	}
	return false;
}

bool ibValueBackgroundJob::CallAsProc(const long lMethodNum, ibValue** /*paParams*/,
                                      const long /*lSizeArray*/)
{
	if (m_run == nullptr)
		return false;

	switch (lMethodNum) {
	case enCancel:
		m_run->Cancel();
		return true;
	}
	return false;
}

bool ibValueBackgroundJob::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (m_run == nullptr)
		return false;

	switch (lPropNum) {
	case enState:
		pvarPropVal = StateValue(m_run->State());
		return true;
	case enBegin:
		pvarPropVal = ibValue(m_run->Begin());
		return true;
	case enEnd:
		pvarPropVal = ibValue(m_run->End());   // a run in flight: the empty date
		return true;
	case enKey:
		pvarPropVal = m_run->Request().m_key;
		return true;
	case enDescription:
		pvarPropVal = m_run->Request().m_description;
		return true;
	case enMethodName:
		pvarPropVal = m_run->Request().m_method;
		return true;
	case enUUID:
		pvarPropVal = new ibValueGuid(m_run->UUID());
		return true;
	}
	return false;
}

//**********************************************************************
//*                    BackgroundJobs — the manager                    *
//**********************************************************************

namespace {

enum ManagerMethod {
	enGetBackgroundJobs = 0,
	enFindByUUID,
};

ibJobManager* RunningJobManager()
{
	ibJobManager* const manager = ibApplicationInstance::GetJobManager();
	if (manager == nullptr)
		ibBackendCoreException::Error(_("Background job: the application is not running"));
	return manager;
}

// A UUID as a script hands it over: a UUID value, or its text. Anything else is named, not read as "none".
ibGuid UUIDOf(const ibValue& value, const ibString& verb)
{
	ibValueGuid* uuid = nullptr;
	if (value.ConvertToValue(uuid) && uuid != nullptr)
		return *uuid;
	ibGuid parsed;
	if (value.GetType() == ibValueTypes::TYPE_STRING && ibGuid::parse(value.GetString(), parsed))
		return parsed;
	ibBackendCoreException::Error(_("%s: '%s' is not a UUID"), verb, value.GetString());
	return ibGuid();
}

// ONE FILTER FIELD — one of BackgroundJob's own properties, named as a script names it and found in the
// job's member table, so the names live in one place (ibValueBackgroundJob_BindNames). A field that names no
// property is refused with its name: a filter that quietly ignored a misspelt field would answer with every
// job there is.
struct ibRunFilterField {
	long    m_prop = wxNOT_FOUND;
	ibValue m_wanted;
};

// …against one job, by the property's number. Begin and End are bounds — begun at or after, ended at or
// before — and the UUID is taken as a UUID or its text; every other property is equal-to.
bool Matches(ibValueBackgroundJob& job, const ibRunFilterField& field)
{
	const ibBackgroundRun& run = *job.GetRun();
	switch (field.m_prop) {
	case enUUID:
		return run.UUID() == UUIDOf(field.m_wanted, ibT("GetBackgroundJobs"));
	case enBegin:
		return run.Begin() >= field.m_wanted.GetDate();
	case enEnd: {
		const ibDateTime end = run.End();
		return !end.IsEmpty() && end <= field.m_wanted.GetDate();
	}
	}
	ibValue held;
	return job.GetPropVal(field.m_prop, held) && held == field.m_wanted;
}

} // namespace

void ibValueBackgroundJobsManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(ibT("GetBackgroundJobs"), 1, ibT("GetBackgroundJobs(filter : Structure)"));
	helper.AppendFunc(ibT("FindByUUID"), 1, ibT("FindByUUID(uuid : UUID)"));
}

ibValueBackgroundJobsManager::ibValueBackgroundJobsManager()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true)
{
}

bool ibValueBackgroundJobsManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                              ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enGetBackgroundJobs:
	{
		ibValueStructure* filter = nullptr;
		if (lSizeArray > 0 && !paParams[0]->IsEmpty()
			&& (!paParams[0]->ConvertToValue(filter) || filter == nullptr))
			ibBackendCoreException::Error(_("GetBackgroundJobs: the filter must be a Structure"));

		// The fields are found ONCE, by name, among the job's properties; every run is then compared by number.
		std::vector<ibRunFilterField> fields;
		if (filter != nullptr) {
			const ibValuePtr<ibValueBackgroundJob> probe(new ibValueBackgroundJob());
			for (const ibMapEntry& entry : filter->Values()) {
				const ibString name = entry.key.GetString();
				const long prop = probe->FindProp(name);
				if (prop == wxNOT_FOUND)
					ibBackendCoreException::Error(_("GetBackgroundJobs: a background job has no property '%s' to filter by"), name);
				if (prop == enUUID)
					UUIDOf(entry.value, ibT("GetBackgroundJobs"));   // refused here, whether or not any job is held
				fields.push_back({ prop, entry.value });
			}
		}

		ibValuePtr<ibValueArray> found(new ibValueArray());
		for (const std::shared_ptr<ibBackgroundRun>& run : RunningJobManager()->GetBackgroundJobs()) {
			const ibValuePtr<ibValueBackgroundJob> job(new ibValueBackgroundJob(run));
			if (std::all_of(fields.begin(), fields.end(), [&job](const ibRunFilterField& f) { return Matches(*job, f); }))
				found->Add(ibValue(job));
		}
		pvarRetValue = found;
		return true;
	}
	case enFindByUUID:
	{
		if (lSizeArray < 1)
			ibBackendCoreException::Error(_("FindByUUID: no UUID given"));
		// Not found is an answer — Undefined — not an error: a job that ended a long time ago is let go.
		const std::shared_ptr<ibBackgroundRun> run =
			RunningJobManager()->FindBackgroundJob(UUIDOf(*paParams[0], ibT("FindByUUID")));
		pvarRetValue = run != nullptr ? ibValue(new ibValueBackgroundJob(run)) : ibValue();
		return true;
	}
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// SYSTEM, not VALUE: script receives one from RunBackground and can never
// construct one — there is no such thing as a job that was not started.
SYSTEM_TYPE_REGISTER(ibValueBackgroundJob, "BackgroundJob", system_to_clsid("VL_BGJOB"));
// The same: there is one, under its global name.
SYSTEM_TYPE_REGISTER(ibValueBackgroundJobsManager, "BackgroundJobsManager", system_to_clsid("MG_BGJOB"));
