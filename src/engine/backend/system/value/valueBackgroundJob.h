#ifndef _VALUE_BACKGROUND_JOB_H__
#define _VALUE_BACKGROUND_JOB_H__

// The script-visible handle on a background run.
//
// VENDED, never created: a job comes into being by being STARTED, so
// `New BackgroundJob()` would have nothing to refer to. That is what
// SYSTEM_TYPE_REGISTER means (docs/script-value-types.md § 1) — the ctor
// registry returns nullptr for it and only RunBackground() hands one out.
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

	virtual wxString GetString() const override;
	virtual bool     IsEmpty()   const override { return m_run == nullptr; }

	// The run is shared state, not a session-bound object — the whole point of it
	// is to be readable from wherever the caller happens to be. Handing it on to
	// another job is pointless but harmless, so the transfer gate lets it through
	// (the inherited default answers true).

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

private:
	std::shared_ptr<ibBackgroundRun> m_run;
};

#endif // !_VALUE_BACKGROUND_JOB_H__
