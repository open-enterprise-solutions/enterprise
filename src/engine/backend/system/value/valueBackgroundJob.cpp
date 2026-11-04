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
	enWaitForExecutionCompletion,
};

enum Prop {
	enState = 0,
	enBegin,
	enEnd,
	enErrorInfo,
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
	helper.AppendFunc(ibT("WaitForExecutionCompletion"), 1, ibT("WaitForExecutionCompletion(timeout : number)"));

	helper.AppendProp(ibT("State"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Begin"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("End"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("ErrorInfo"), true, false, wxNOT_FOUND);
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
	const ibString description = m_run->Label().m_description;
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
	case enWaitForExecutionCompletion:
	{
		// The timeout is in SECONDS, as everywhere a person says how long to wait; none (or 0) waits for
		// the end. A run that is still going when it elapses is not an error — State says Active, and the
		// caller decides. The answer is the job itself, so a call can be read straight on.
		const long seconds = lSizeArray > 0 && !paParams[0]->IsEmpty() ? paParams[0]->GetInteger() : 0;
		if (seconds < 0)
			ibBackendCoreException::Error(_("Background job: the timeout cannot be negative (%ld seconds)"), seconds);
		m_run->Wait(seconds > 0 ? static_cast<int>(seconds * 1000) : 0);
		pvarRetValue = this;
		return true;
	}
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
	case enErrorInfo:
		// What the run died of, with the module and the line when its code said so; empty when it did not
		// fail. A cancel answers here too, as it does in Error().
		pvarPropVal = m_run->Error();
		return true;
	case enKey:
		pvarPropVal = m_run->Label().m_key;
		return true;
	case enDescription:
		pvarPropVal = m_run->Label().m_description;
		return true;
	case enMethodName:
		pvarPropVal = m_run->Label().m_method;
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
	enExecute = 0,
	enGetBackgroundJobs,
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

// ONE FILTER KEY against one run. Begin and End are bounds — begun at or after, ended at or before — the
// rest are equal-to. A key that names nothing is refused with its name: a filter that quietly ignored a
// misspelt key would answer with every job there is.
bool Matches(const ibBackgroundRun& run, const ibString& key, const ibValue& wanted)
{
	if (stringUtils::IsSameAs(key, ibT("UUID")))
		return run.UUID() == UUIDOf(wanted, ibT("GetBackgroundJobs"));
	if (stringUtils::IsSameAs(key, ibT("Key")))
		return run.Label().m_key == wanted.GetString();
	if (stringUtils::IsSameAs(key, ibT("Description")))
		return run.Label().m_description == wanted.GetString();
	if (stringUtils::IsSameAs(key, ibT("MethodName")))
		return stringUtils::IsSameAs(run.Label().m_method, wanted.GetString());
	if (stringUtils::IsSameAs(key, ibT("State")))
		return StateValue(run.State()) == wanted;
	if (stringUtils::IsSameAs(key, ibT("Begin")))
		return run.Begin() >= wanted.GetDate();
	if (stringUtils::IsSameAs(key, ibT("End"))) {
		const ibDateTime end = run.End();
		return !end.IsEmpty() && end <= wanted.GetDate();
	}
	ibBackendCoreException::Error(
		_("GetBackgroundJobs: the filter has no field '%s' (UUID, Key, Description, MethodName, State, Begin, End)"), key);
	return false;
}

} // namespace

void ibValueBackgroundJobsManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(ibT("Execute"), 4, ibT("Execute(methodName : string, parameters : Array, key : string, description : string)"));
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
	case enExecute:
	{
		// Starting work from a watch expression would be a side effect of LOOKING at something — the
		// same guard RunBackground has.
		if (ibBackendException::IsEvalMode()) {
			pvarRetValue = wxEmptyValue;
			return true;
		}
		if (lSizeArray < 1 || paParams[0]->IsEmpty())
			ibBackendCoreException::Error(_("Background job: no procedure name given"));

		ibBackgroundLabel label;
		label.m_method = paParams[0]->GetString();
		if (lSizeArray > 2) label.m_key = paParams[2]->GetString();
		if (lSizeArray > 3) label.m_description = paParams[3]->GetString();

		// The parameters are an Array, passed to the method positionally. Anything else non-empty is the
		// caller's mistake and named — read as "no parameters" it would start the method with the wrong
		// signature and fail deep inside it.
		std::vector<ibValue> args;
		if (lSizeArray > 1 && !paParams[1]->IsEmpty()) {
			ibValueArray* array = nullptr;
			if (!paParams[1]->ConvertToValue(array) || array == nullptr)
				ibBackendCoreException::Error(_("Background job: the parameters of '%s' must be an Array"), label.m_method);
			args = array->Values();
		}

		pvarRetValue = new ibValueBackgroundJob(RunningJobManager()->StartBackground(args, label));
		return true;
	}
	case enGetBackgroundJobs:
	{
		ibValueStructure* filter = nullptr;
		if (lSizeArray > 0 && !paParams[0]->IsEmpty()
			&& (!paParams[0]->ConvertToValue(filter) || filter == nullptr))
			ibBackendCoreException::Error(_("GetBackgroundJobs: the filter must be a Structure"));

		ibValuePtr<ibValueArray> found(new ibValueArray());
		for (const std::shared_ptr<ibBackgroundRun>& run : RunningJobManager()->BackgroundRuns()) {
			bool match = true;
			if (filter != nullptr)
				for (const ibValueContainer::ibContainerEntry& entry : filter->Entries())
					if (!Matches(*run, entry.key.GetString(), entry.value)) { match = false; break; }
			if (match)
				found->Add(ibValue(new ibValueBackgroundJob(run)));
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
			RunningJobManager()->FindBackgroundRun(UUIDOf(*paParams[0], ibT("FindByUUID")));
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
