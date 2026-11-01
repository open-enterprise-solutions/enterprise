#ifndef _VALUE_BACKGROUND_JOB_H__
#define _VALUE_BACKGROUND_JOB_H__

// The script-visible handle on a background run.
//
// VENDED, never created: a job comes into being by being STARTED, so
// `New BackgroundJob()` would have nothing to refer to. That is what
// SYSTEM_TYPE_REGISTER means (docs/private/script-value-types.md § 1) — the ctor
// registry returns nullptr for it and only RunBackground(), BackgroundJobs and a
// scheduled job's LastJob hand one out.
//
// It is a thin skin over ibBackgroundRun, which is shared with the worker
// executing the run. Dropping the value does NOT cancel anything: the run owns
// its own session and finishes on its own. That is the "start and forget" case,
// and it is the default rather than an option.

#include "backend/compiler/value.h"

#include <memory>

class ibBackgroundRun;

// Type-invariant contributor — free function (external linkage) so it can be a
// template non-type arg in the base clause below.
void ibValueBackgroundJob_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueBackgroundJob :
	public ibValueStaticMembers<&ibValueBackgroundJob_BindNames> {
public:

	ibValueBackgroundJob();
	explicit ibValueBackgroundJob(std::shared_ptr<ibBackgroundRun> run);
	virtual ~ibValueBackgroundJob();

	virtual ibString GetString() const override;
	virtual bool     IsEmpty()   const override { return m_run == nullptr; }

	// The run is shared state, not a session-bound object — the whole point of it
	// is to be readable from wherever the caller happens to be. Handing it on to
	// another job is pointless but harmless, so the transfer gate lets it through
	// (the inherited default answers true).

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// State, Begin, End, ErrorInfo and the label — FACTS about the run, all read-only.
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	const std::shared_ptr<ibBackgroundRun>& GetRun() const { return m_run; }

private:
	std::shared_ptr<ibBackgroundRun> m_run;
};

void ibValueBackgroundJobsManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// BackgroundJobs — the global manager of background work: start one under a key and a description, list
// them, find one by its UUID. Every one of them is the run RunBackground starts (ibJobManager::
// StartBackground), so a job started here and one started there are the same thing, listed alike.
//
//   Job = BackgroundJobs.Execute("Exchange.Upload", Parameters, "upload", "Upload to the warehouse");
//   Jobs = BackgroundJobs.GetBackgroundJobs(New Structure("Key, State", "upload", BackgroundJobState.Active));
//
// VENDED like the job itself: there is one, and it is reached by its global name.
class BACKEND_API ibValueBackgroundJobsManager :
	public ibValueStaticMembers<&ibValueBackgroundJobsManager_BindNames> {
public:
	ibValueBackgroundJobsManager();

	virtual ibString GetString() const override { return ibT("BackgroundJobsManager"); }

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
};

#endif // !_VALUE_BACKGROUND_JOB_H__
