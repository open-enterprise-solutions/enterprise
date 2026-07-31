////////////////////////////////////////////////////////////////////////////
//	Description : BackgroundJob — the script handle on a background run
////////////////////////////////////////////////////////////////////////////

#include "valueBackgroundJob.h"

#include "backend/job/jobManager.h"   // ibBackgroundRun

namespace {

enum Func {
	enIsComplete = 0,
	enWait,
	enResult,
	enError,
	enActivity,
};

enum Proc {
	enCancel = 0,
};

} // namespace

// Order MUST match the enums above — the method number is the index into this
// table, so a swap here calls the wrong case below.
void ibValueBackgroundJob_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(wxT("IsComplete"), wxT("IsComplete()"));
	helper.AppendFunc(wxT("Wait"), 1, wxT("Wait(timeoutMs : number)"));
	helper.AppendFunc(wxT("Result"), wxT("Result()"));
	helper.AppendFunc(wxT("Error"), wxT("Error()"));
	helper.AppendFunc(wxT("Activity"), wxT("Activity()"));
	helper.AppendProc(wxT("Cancel"), wxT("Cancel()"));
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

wxString ibValueBackgroundJob::GetString() const
{
	if (m_run == nullptr)
		return wxT("BackgroundJob");
	if (!m_run->IsComplete())
		return m_run->Activity().IsEmpty() ? wxT("BackgroundJob: running")
		                                   : m_run->Activity();
	return m_run->Error().IsEmpty() ? wxT("BackgroundJob: done")
	                                : wxT("BackgroundJob: failed");
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

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// SYSTEM, not VALUE: script receives one from RunBackground and can never
// construct one — there is no such thing as a job that was not started.
SYSTEM_TYPE_REGISTER(ibValueBackgroundJob, "BackgroundJob", system_to_clsid("VL_BGJOB"));
