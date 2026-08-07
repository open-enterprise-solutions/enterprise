#ifndef __IB_JOB_MANAGER_H__
#define __IB_JOB_MANAGER_H__

// ibJobManager — the owner of work that runs OUTSIDE the thread that asked for it.
//
// It does not execute anything itself. It owns the SCHEDULE (what is due, and how
// often) and the SESSIONS jobs run under; execution is `ibSession::Submit`, which
// forwards to whatever worker pool that session answers with. The manager is
// therefore a bookkeeper sitting between a tick source and the worker pool.
//
// WHY THE TICK NEVER EXECUTES. Two independent reasons, both fatal if ignored:
//   1. The session registry's thread is fail-stop — ibSessionRegistry::Die()
//      terminates the process, because a dead registry means sys_session lies to
//      peer processes. Script must not run there.
//   2. ibSession::Current() resolves through THREAD BINDING. A tick thread is bound
//      to no session, so a job dispatched from it would find no runtime — or, worse,
//      a neighbouring session's. Only a pool worker binds correctly (it installs an
//      ibSessionScope before draining the queue).
// So Tick() decides "due" and hands the body to a session. Nothing else.
//
// WHY A SESSION PER JOB. A session owns exactly ONE connection
// (ibSession::Holder(); AcquireFreeConnection is deliberately not mirrored on it),
// and the pool dispatches per-session FIFO under a lease. Both properties are load-
// bearing here rather than incidental:
//   - one connection per job is what lets a job hand `session->Holder()` to engine
//     code that demands an explicit holder (see query/derivedStateBuilder.h);
//   - per-session FIFO means two runs of the SAME job can never overlap, which some
//     jobs need for correctness (the totals fold corrupts sums if two passes race on
//     one table) and which costs nothing to get this way;
//   - two jobs run in parallel because they are two sessions — never because one
//     session grew a second connection.
//
// WHY SESSIONS ARE BUILT AT Register(), NOT AT Tick(). Creating a session is
// Connect -> auth -> EnsureRoot -> CompileRoot, and Connect blocks up to its timeout.
// Doing that on a tick would freeze whichever thread ticks — on a file deployment
// that is the UI thread. Register() pays it once, up front; Tick() stays cheap enough
// to call every second.
//
// See docs/job-manager.md for the whole picture (the three job kinds, the
// cross-session value gate, identity, and what is deliberately not built yet).

#include "backend/backend.h"
#include "backend/appDataCtorToken.h"
#include "backend/guid.h"        // ibGuid — a job names its user by key, never by name
#include "backend/session/sessionHolder.h"
#include "backend/compiler/value.h"        // ibValue — the argument-array gate
#include "backend/job/jobSchedule.h"       // ibJobScheduleDescription — when a job is due
#include "backend/lock/lockHandle.h"       // ibLockHandle held per running job
#include "backend/lock/lockHolder.h"       // ibLockHolder — base for the job's claim identity

#include <wx/datetime.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <wx/string.h>

class ibSession;

// What a job actually does, and how it reports back.
//
// The session is handed in rather than resolved from ambient state: a job body
// typically needs `session->Holder()` to name the connection it runs on, and
// "the calling thread's connection" would silently borrow somebody else's.
//
// RETURN VALUE IS DOSAGE, not success. `true` means "this pass did its share and
// work remains" — the manager re-queues the job on the next tick instead of waiting
// out the full interval. `false` means "nothing left for now". This exists because
// the first tenant asks for it explicitly: a fold reads and writes the same database
// everyone else is using, so it must be paced rather than run to exhaustion, and
// stopping between units is always safe (query/derivedStateBuilder.h).
//
// Throwing is allowed. The exception is captured by the worker's promise and logged
// by the manager on the next tick; it does not escape into the tick source.
using ibJobBody = std::function<bool(ibSession* session)>;

// DOES THE RUN STAND ON ITS OWN, OR DOES IT RENT THE ONE THAT STARTED IT?
//
// One background run from the outside — one session kind, one list, one handle.
// Inside there are two behaviours, and the whole difference is whether the run can
// still be there when whoever asked for it is gone:
//
//   Standalone — copies the caller's identity, brings a runtime up, takes a row in
//                sys_session, and OUTLIVES the caller. That is what script's
//                RunBackground promises: start it and walk away.
//
//   Tenant     — has no existence apart from its parent. It installs no identity,
//                builds no runtime and takes no row, and it does not need to: it
//                reads on the parent's behalf, so it borrows the parent's access
//                policy (ibSession::GetAccessPolicy) instead of rebuilding one.
//                It creates a session for exactly ONE reason — a session owns a
//                connection, the parent's is busy with the parent's own work, and
//                a connection is the one thing that cannot be borrowed.
//
// The cost is the point. A standalone run pays Connect + InstallUser + a compile of
// every common module; that amortises over a report and is absurd per scrolled page.
// A tenant pays a Connect and a checkout.
enum class ibJobTenancy {
	Standalone,
	Tenant,
};

// Who a job belongs to. Decides the KIND of session it runs under, which is what
// an administrator sees in Active Users — and the decision there differs: a
// configuration's job will come back on its interval whether or not this run is
// killed, while the platform's own housekeeping is safe to kill outright.
enum class ibJobOrigin {
	Platform,        // the engine's own — totals fold, maintenance
	Configuration,   // declared by the applied configuration
};

// A job's declaration. Registered by the platform at startup (system jobs) or,
// later, built from metadata for configuration-owned scheduled jobs.
struct BACKEND_API ibJobDescription {
	ibJobOrigin m_origin = ibJobOrigin::Platform;

	// Identity. Unique within the manager; Register rejects a duplicate rather than
	// replacing, so a double bootstrap is a visible error instead of a silent
	// second copy of a job that must not run twice.
	wxString   m_name;

	// THE STABLE KEY — what the base stores its settings and its shared clock under, and what the
	// cross-process claim names.
	//
	// It is NOT the name. A configuration's job is identified by its metaobject GUID — the identity
	// that survives a rename, an unload / reload, and a copy of the configuration onto another
	// base. Keying the row by the name would mean that renaming a job silently orphans its
	// settings and its last-run — the job then looks brand new and fires at once, and the row that
	// held "switched off" is still in the table pointing at nothing.
	//
	// A PLATFORM job has no metaobject, so it carries a FIXED guid minted once and written into
	// platformJobs.cpp — not its name. One type for every job's key means the table has one kind
	// of value in that column and no reader has to ask which sort of key it is looking at.
	ibGuid     m_key;

	ibJobBody  m_body;

	// WHO it runs AS — the difference that actually separates the three kinds.
	//
	//   Platform      — nobody. The session stays anonymous, which means no RLS
	//                   policy is built for it and the work sees everything. That
	//                   is REQUIRED, not lax: a totals fold that read through a
	//                   user's row filter would fold a subset and silently produce
	//                   wrong sums. Leave this empty for a platform job.
	//   Configuration — the user named HERE. A scheduled job belongs to whoever
	//                   declared it, and it must see exactly what that user sees:
	//                   same rights, same RLS, same result as if they had run it.
	//   Background    — not this field at all. A background run adopts the caller
	//                   (StartBackground reads the current session's user), because
	//                   it IS that caller's work, started by hand. A RENTED one
	//                   adopts nothing: it never acts as the user, it acts for the
	//                   session that already does — see ibJobTenancy.
	//
	// No password: `Login` is already split into AuthenticateUser (the check) and
	// InstallUser (the commit), and a job needs only the commit — nobody is typing
	// anything, and storing a service password to re-verify what the configuration
	// already declared would be a secret kept for no reason.
	ibGuid     m_runAsUser;

	// MAY TWO OF THESE RUN AT ONCE?
	//
	// Two shapes of scheduled job, differing in exactly this:
	//
	//   EXCLUSIVE (the default, and what a platform job is). One job, one record,
	//   one runner. Before starting, the run claims its own record in sys_lock; if
	//   anyone already holds it — this process, another client, another node — the
	//   run is Skipped and journalled as blocked. Some work is simply not correct
	//   twice at once: two totals folds racing on one table move the same figure
	//   twice, and one of them deletes a row the other still holds.
	//
	//   PARALLEL. The job is a procedure plus PARAMETERS, and several are meant to
	//   exist — the same work over a different set, period or node. Nothing is
	//   claimed, because there is no single record to claim: each registration is
	//   its own entry under its own name, and they are supposed to overlap.
	//
	// A background run is neither: it claims nothing, runs once and is gone, so
	// there is no record for a second one to collide with.
	bool m_exclusive = true;

	// WHEN it runs — interval plus calendar; see jobSchedule.h. The first run
	// happens on the first tick after Register that the calendar allows: jobs are
	// due immediately rather than one interval later, so a restart does not skip
	// a pass.
	ibJobScheduleDescription m_schedule;

	// IS IT SWITCHED ON? The declaration's schedule keeps existing either way — an inactive job is
	// still registered, still listed, and still runnable BY HAND (RunNow ignores this, as it
	// ignores the calendar). Only the tick reads it.
	//
	// Why the manager holds it rather than the metadata: switching a misbehaving job off at 3 a.m.
	// must not mean opening the Designer against a production base. The value therefore lives in
	// the database (sys_job), seeded from the declaration the first time the job is seen, and the
	// base wins from then on.
	bool m_active = true;

	// WHAT HAPPENS AFTER A FAILURE — retry, or wait out the schedule?
	//
	// Without this a job that failed because a network blinked waits its whole interval before
	// trying again, and for a nightly job that means tomorrow. Most real failures of unattended
	// work are transient, so a handful of quick attempts recovers what a schedule cannot.
	//
	// Deliberately NOT the same thing as the dosage answer: `true` from the body means "this pass
	// did its share, there is more to do", while a retry means "the pass did not happen at all".
	// Both re-queue the job ahead of its interval, and they must stay distinguishable — one is
	// progress, the other is a second chance.
	//
	// 0 = no retry (the default): a failure waits for the next scheduled moment, which is the right
	// answer for work that is not idempotent. The count is reset by a successful pass, so a job
	// failing once a week does not eventually exhaust its attempts and go quiet.
	int m_retryCount = 0;

	// Seconds between attempts. Only read when m_retryCount > 0. Small on purpose: a retry is for a
	// blink, and anything that needs minutes is better served by the schedule itself.
	int m_retryIntervalSeconds = 10;
};

// How the last run ended. Kept for SCHEDULED jobs, which repeat and therefore
// have a history worth looking at; a background job runs once and is gone, so
// its outcome goes to whoever started it, not into a schedule row.
enum class ibJobOutcome {
	Never,      // registered, not yet run
	Running,    // in flight now
	Succeeded,  // body returned
	Failed,     // body threw — see ibJobState::m_error
	Skipped,    // another process held the claim; it did the work instead
	// Somebody stopped it: an administrator kicked the session, or the process
	// is going down. Distinct from Failed on purpose — the job did nothing
	// wrong, and an administrator reading the list must be able to tell "this
	// broke" from "I stopped this". Treating a cancel as a failure would also
	// bury real failures in the noise of every ordinary shutdown.
	Cancelled,
};

// A job's observable state — what an administration list would show.
struct BACKEND_API ibJobState {
	wxString     m_name;
	// The KEY this job answers to — the metaobject's guid for a predefined job, the row's for a
	// parameterized one, a fixed literal for each of the engine's own. It is what sys_job, the
	// cross-process claim and every "run this one" are keyed on; the name is the caption.
	ibGuid       m_key;
	// IS IT ON THE SCHEDULE — a switched-off job stays registered (so it can still be run by hand)
	// and is simply never due, so "declared" and "running on its schedule" are two different
	// answers and a list that shows only the first tells half the truth.
	bool         m_active = true;
	ibJobOutcome m_outcome = ibJobOutcome::Never;
	// Wall clock, for display. The scheduling itself runs off a steady clock so a
	// system-clock adjustment cannot make a job fire twice or stall for hours;
	// these two are the human-readable projection of that, not its source.
	wxDateTime   m_lastRunAt;
	// COMPUTED, never stored: last run + interval. Empty when the job has not run
	// yet (it is due immediately) or when work remained from the previous pass
	// (due on the next tick). A stored "next run" would be one more thing that can
	// disagree with reality after a restart.
	wxDateTime   m_nextRunAt;
	wxString     m_error;   // set when m_outcome == Failed
	// The schedule as a sentence — "Every 10 minutes, 10:00-15:00, Tue". Built
	// from the same fields an editor shows, so a settings list and this list say
	// the same thing without either restating the rules.
	wxString     m_schedule;
};

// ONE background run, shared between the worker executing it and whoever is
// watching. Handed out behind a shared_ptr because those two have independent
// lifetimes: script may drop its handle and walk away ("start and forget"), or
// hold it and wait — and in the first case the run must still finish and clean
// up after itself.
//
// The session holder lives HERE, so the run owns its own session for exactly as
// long as it needs it. That is what lets a background job outlive the form, and
// even the session, that started it.
class BACKEND_API ibBackgroundRun {
public:
	bool     IsComplete() const;
	// Blocks until the run finishes or the timeout elapses. 0 = wait forever.
	// Returns IsComplete() — false means it timed out, not that it failed.
	bool     Wait(int milliseconds = 0);
	// The value the procedure returned. Empty until the run completes, and empty
	// afterwards if it threw — check Error() to tell those apart.
	ibValue  Result() const;
	wxString Error()  const;
	// Cooperative: raises the session's cancel flag; the interpreter unwinds at
	// its next loop boundary. A run blocked in a socket read will not notice.
	void     Cancel();

	// Set by the worker as the run proceeds; also written to
	// sys_session.currentActivity, so the same text shows in Active Users.
	wxString Activity() const;

private:
	friend class ibJobManager;

	mutable std::mutex m_mtx;
	ibSessionHolder    m_holder;
	std::future<void>  m_future;
	ibValue            m_result;
	wxString           m_error;
	wxString           m_activity;
	std::atomic<bool>  m_done { false };
};

class BACKEND_API ibJobManager {
public:
	// Construction restricted to ibApplicationData via the token gate — same
	// pattern as the connection pool, lock manager and session registry.
	explicit ibJobManager(ib::AppDataCtorToken);
	~ibJobManager();

	ibJobManager(const ibJobManager&)            = delete;
	ibJobManager& operator=(const ibJobManager&) = delete;

	// DECLARE a job. Returns false when the name is already taken, the description
	// is unusable (no body, non-positive interval), or the cap is reached — a
	// refusal rather than a deferral, so the caller learns about it at bootstrap
	// instead of discovering silence at tick time.
	//
	// Cheap and side-effect-free: no session, no database, no metadata. That is
	// deliberate, because the platform's own list is registered from
	// ibApplicationData the moment the database opens — long before there is a
	// user, a configuration or a reason to spend a Connect on a job that may not
	// come due for six hours. The session is built on first launch instead, so a
	// job that never becomes due never costs anything.
	bool Register(ibJobDescription desc);

	// Drop a job and release its session. Returns false when the name is unknown.
	// A run already in flight is NOT cancelled — the entry waits for it, so the
	// session stays alive exactly as long as the work using it.
	bool Unregister(const wxString& name);

	// Drop every job whose name starts with `prefix`; returns how many went. The parameterized
	// scheduled job registers one entry PER ROW under "<JobName>.<rowGuid>", and when its
	// configuration closes it has to withdraw all of them without re-reading a table it may no
	// longer be able to read. Same waiting rule as Unregister: a run in flight is not cancelled.
	std::size_t UnregisterByPrefix(const wxString& prefix);

	// Start the manager's OWN tick thread. Idempotent.
	//
	// The thread does one thing: ask what is due and hand it to a worker. It never
	// executes a job — that is not a style choice, it is what keeps the two
	// invariants that matter. Script must not run on a thread whose death is
	// fatal, and ibSession::Current() resolves by THREAD BINDING, which only a
	// pool worker sets up.
	//
	// Owning the thread here rather than borrowing a host's timer is what makes
	// the schedule host-independent: a desktop client, a web server, a compute
	// server and a daemon all get the same cadence without any of them arranging
	// it. It also keeps the tick off the UI thread, where a first-run session
	// creation (Connect, up to its timeout) would have been felt.
	void Start();

	// THE TICK. Launches every job that is due and not already running; returns how
	// many were launched. Returns immediately — the futures stay with the entries,
	// they are never waited on here.
	//
	// Public and callable from anywhere: the thread above drives it, and script's
	// RunScheduledJobs() forces a round. An over-eager tick finds nothing due and
	// costs a mutex plus a clock read.
	int Tick();

	// Start `procedureName` on a session of its own, right now, and return a handle
	// to the run. This is the BACKGROUND half of the manager: no schedule, an
	// initiator, and a result somebody may be waiting for.
	//
	// THE NAME IS `ModuleName.MethodName` — a PUBLIC method of a common module.
	// Required in that form, not merely accepted: the call is made by name from
	// another session, so which module the author meant has to be in the name
	// rather than in whatever was in scope where it was written. Visibility needs
	// no separate check — a module value exposes only its public methods, so a
	// private one is simply not found.
	//
	// The session is fresh and its own — that is what keeps the caller's session
	// responsive. Putting the work on the caller's session instead would queue it
	// behind that session's lease, and the user's next action behind the work.
	//
	// IDENTITY IS INHERITED from whoever calls: the run adopts the current
	// session's user, so it sees exactly what that user sees (RLS included). No
	// password is involved — `Login` is already split into a credential check and
	// an install, and this uses only the second half. Naming a DIFFERENT user is
	// deliberately not offered: it would be an escalation door, deferred and
	// unattended, and it can be added later behind a role check without changing
	// this signature.
	//
	// `args` is passed to the procedure positionally. Every element must clear the
	// gate below; a mutable value throws HERE, in front of the caller.
	//
	// The returned handle may be dropped: the run finishes regardless and tears its
	// session down when it does.
	std::shared_ptr<ibBackgroundRun> StartBackground(const wxString& procedureName,
	                                                 const std::vector<ibValue>& args = {});

	// THE SAME RUN, described in C++ instead of by name.
	//
	// Everything that makes a background run what it is — its own session, the
	// initiator's identity (and therefore their RLS), the try/catch that turns a
	// throw into a reported error, the handle somebody may watch or drop — is
	// identical whether the work is a configuration's procedure or the engine's
	// own reading. Only the payload differs, so this is the one implementation and
	// the named-procedure form above is a body built over it.
	//
	// This is what a list or a report loads through: the read runs here, and the
	// result is handed back by Submitting to the session that ASKED — captured by
	// the caller before it starts, never resolved inside, where the current session
	// is this run's own.
	//
	// `activity` is the sentence shown in Active Users and in the run's Activity()
	// while it lasts. Required, because a background session with no activity text
	// is a row an administrator cannot account for. (A TENANT has no row and so
	// nothing to account for; the text still names the run in its own handle.)
	//
	// `tenancy` decides which of the two behaviours above this run gets. A read
	// that serves a form is a Tenant: it must be cheap, and it has no meaning once
	// the form that asked is gone. Everything script starts is Standalone.
	//
	// A TENANT TAKES ITS CONNECTION HERE, on the calling thread, before the run is
	// submitted — so "the pool has nothing free" is an exception in front of the
	// caller, who can still read the old rows or fall back, rather than a failure
	// discovered by nobody on a worker.
	using ibBackgroundBody = std::function<ibValue(ibSession* session)>;
	std::shared_ptr<ibBackgroundRun> StartBackground(ibBackgroundBody body,
	                                                 const wxString& activity,
	                                                 ibJobTenancy tenancy = ibJobTenancy::Standalone);

	// Run one job now, ignoring interval and window. Returns false when the name is
	// unknown or that job is already running.
	//
	// Not a debugging convenience — without it a job can only be exercised by
	// waiting out its schedule, which makes "is the job wrong or is the manager
	// wrong?" unanswerable when a fresh tenant misbehaves.
	bool RunNow(const wxString& name);

	// Cap on jobs registered at once. Each holds a session, and a session holds a
	// connection out of ibConnectionPool — whose Checkout BLOCKS once maxSize is
	// reached. Unbounded jobs would therefore stall interactive work on a connection
	// wait. The cap belongs here because the connection pool cannot know who matters
	// more. Set before registering; lowering it does not evict what already exists.
	void        SetMaxJobs(std::size_t n);
	std::size_t GetMaxJobs() const;

	// Diagnostics: registered count, and how many are running right now.
	std::size_t Count()        const;
	std::size_t RunningCount() const;

	// Every job's state, for an administration list: what ran, when, how it went,
	// and when it is next expected. A snapshot by value — the caller reads it
	// without holding anything of the manager's.
	std::vector<ibJobState> Snapshot() const;

	// THE GATE for a job's argument array. Every value must answer yes to
	// ibValue::IsTransferable() — the rule lives on the types, not here, so this
	// layer never grows a list of what it knows about.
	//
	// Checked at SUBMISSION, never at first touch inside the job: in the
	// background there is no caller left to raise the error to. Returns the index
	// of the first value that refused, or -1 when the whole array may travel.
	static int FindNonTransferable(const std::vector<class ibValue>& args);

	// Same check, but THROWS ibBackendCoreException naming the offending argument
	// and its class. This is what a submission path calls: passing a mutable value
	// to a job is a programming error in the configuration, and the author has to
	// see it — at the call, on their own stack, with the script's try/except as
	// the natural handling point. Returning a quiet false would leave a job that
	// either never runs or runs on values it must not touch, and nothing said why.
	static void CheckTransferable(const std::vector<class ibValue>& args);

	// Release every job and its session. Waits for in-flight runs so no worker is
	// left holding a session the manager is dropping. Idempotent; called from
	// ~ibApplicationData before the session registry goes down.
	void Stop();

private:
	struct ibJobEntry {
		ibJobDescription                      m_desc;
		std::future<void>                     m_future;
		std::chrono::steady_clock::time_point m_lastRun {};
		// When this job entered the schedule. A job that has never run counts its
		// first interval FROM HERE — see IsDue.
		std::chrono::steady_clock::time_point m_registeredAt = std::chrono::steady_clock::now();
		// The same moment on the WALL clock — the calendar speaks in local time, so a question like
		// "was there a 02:00 between when we started watching and now?" cannot be asked on a steady
		// clock. Used only for the first-run decision; every later one measures on the steady clock,
		// where moving the system time cannot make a job fire twice.
		wxDateTime                            m_registeredAtWall = wxDateTime::Now();
		bool                                  m_everRun     = false;
		// Set from the body's return value: the previous pass left work behind, so
		// the interval is skipped and the job is due on the next tick.
		bool                                  m_workRemains = false;

		// RETRY STATE (ibJobDescription::m_retryCount). m_retriesLeft is the allowance remaining for
		// the CURRENT streak of failures — refilled by a successful pass, so a job that fails once a
		// week never runs out. m_retryAt is when the next attempt is due; invalid means the job is on
		// its ordinary schedule and nothing is being retried.
		int                                   m_retriesLeft = 0;
		wxDateTime                            m_retryAt;

		// A TICK IS WORKING ON THIS ENTRY OUTSIDE THE LOCK — launching it, which creates a session
		// and can wait on the registry. Set under m_mtx before the lock is dropped and cleared under
		// it afterwards; Unregister and Stop wait for it, so an entry can never be destroyed while a
		// launch is still writing to it. (Deciding what to run must not hold the mutex that Register
		// needs — every pooled connection goes through Register, so holding it froze the UI.)
		bool                                  m_inFlight = false;
		// WHAT THE RUN ANSWERED, filled ON THE WORKER as the run ends — not at the
		// next tick. The worker knows the outcome at the moment it happens; making
		// the schedule discover it later would leave a finished job reading as
		// still Running until something else came round.
		//
		// Shared with the submitted task, which outlives the Launch call.
		struct ibRunResult {
			std::atomic<bool> m_done    { false };   // published LAST
			std::atomic<bool> m_ok      { false };   // false = the body threw
			std::atomic<bool> m_more    { false };   // the dosage answer
			// The run never reached the body: a peer held the claim, or the shared
			// clock said it had just run elsewhere. Not a failure — it simply was
			// not ours this time.
			std::atomic<bool> m_skipped { false };
			std::mutex        m_mtx;                 // guards m_error only
			wxString          m_error;
		};
		std::shared_ptr<ibRunResult>          m_result;
		// The session this run is on. Kept here rather than only inside the task so
		// the manager can release it from the TICK: ibSession::Teardown cancels and
		// then queues an empty task on the session's own queue and waits for it, and
		// doing that from inside a task of that same session means waiting behind
		// oneself. Released in HarvestFinished, off the worker.
		std::shared_ptr<ibSessionHolder>      m_runSession;

		// Observable history — projected out by Snapshot(). Scheduling itself does
		// NOT read these: it runs off m_lastRun (steady clock) so that moving the
		// system clock cannot make a job fire twice or stall until the date comes
		// back around.
		ibJobOutcome                          m_outcome = ibJobOutcome::Never;
		wxDateTime                            m_lastRunAt;
		wxString                              m_error;
	};

	// Is a previous run still going? A default-constructed future is "never ran".
	static bool IsRunning(const ibJobEntry& e);

	// Due = never ran, or work remained, or the interval elapsed — and, if a window
	// is declared, the local hour is inside it.
	static bool IsDue(const ibJobEntry& e, std::chrono::steady_clock::time_point now);

	// Drain a finished future so the exception it may hold is logged rather than
	// swallowed by the future's destructor. No-op while the run is still going.
	//
	// The run's SESSION is handed to `release` rather than dropped here: dropping it tears the
	// session down, which waits, and this runs under m_mtx. The caller releases it once the lock
	// is gone.
	static void HarvestFinished(ibJobEntry& e, std::vector<std::shared_ptr<ibSessionHolder>>& release);

	// Open the session a run happens on. One per run: created when the run starts,
	// released when it ends, so a job that works for ten seconds every six hours does
	// not hold a pooled connection for the other six.
	ibSessionHolder OpenRunSession(const ibJobDescription& desc);

public:

	// ONE JOB'S RECORD IN THE REGISTER — the whole row of sys_job, for every kind of job there is.
	//
	// A platform job, a predefined one and one ROW of a parameterized one are all described by
	// exactly this: who it is (the guid), what it is called, whether it is on, when it is due, and
	// when it last ran. That is why there is one struct and not one per kind — the register is the
	// list of what actually runs on this base, and its entries are the same shape whatever
	// declared them.
	struct ibJobSettings {
		ibGuid                   m_key;         // identity — the metaobject's guid, or a row's own
		wxString                 m_name;        // display name, so the table reads for a person
		bool                     m_active = true;
		ibJobScheduleDescription m_schedule;
		wxDateTime               m_lastRun;      // read-only for a caller — the shared clock
		wxString                 m_computer;     // who ran it last, for diagnostics
		bool                     m_found = false;   // was there a row at all? (seed-on-first-sight)

		bool IsOk() const { return m_key.isValid(); }
	};

	// WHAT BECAME OF A SETTINGS WRITE — three answers, because a bool cannot carry two facts.
	//
	// "It did not take" used to mean both "there is no base to write to" and "the base looked at
	// this row and said no", and every caller had to pick ONE behaviour for both. Whichever it
	// picked was wrong half the time: silence hid a rejected INSERT for a whole evening, and
	// raising took down every job declared before a database was open. The two are different
	// events and now say so.
	enum class ibWriteOutcome {
		Written,   // the row is in the base
		NoBase,    // no connection to write through — nobody has been told anything yet
		Refused,   // the base HAD its say and the row did not land: a real failure
	};

	// Read / write the base-side settings of a job by name. Public because the JOB SETTINGS VALUE
	// (script's PredefinedJobs.<Name>) is exactly a view on this row: one door, so the scheduler
	// and the card cannot hold different opinions about whether a job is on.
	// Both keyed by the job's STABLE key, never by its name — a renamed job must keep its settings
	// and its clock, and an orphaned row that still says "switched off" is exactly what keying by
	// the name produces.
	//
	// A read has no such split: an unreadable row is NO OPINION either way, and the declaration's
	// own values stand. Only a write can be refused.
	static ibJobSettings   ReadSharedSettings(const ibGuid& key);
	static ibWriteOutcome  WriteSharedSettings(const ibJobSettings& settings);

	// FORGET a job entirely — drop its row from sys_job. For a job that no longer exists: a
	// parameterized row that was deleted takes its settings and its clock with it, so a later row
	// that happens to be created is genuinely new and does not inherit a stranger's "switched off"
	// or "ran a minute ago".
	static void ForgetSharedState(const ibGuid& key);

	// "THIS JOB EXISTS, it is simply not on the schedule" — a switched-off row, a job whose Use
	// flag is clear. They keep their record (and with it their last run, which is what one looks
	// at before switching something back on), so the sweep below must not take them for orphans.
	// Noted by whoever wrote the record; forgotten when the manager stops.
	void NoteLiveKey(const ibGuid& key);

	// SWEEP THE ORPHANS — drop every sys_job row whose key is neither a registered job's, nor
	// noted live, nor in `alsoLive`, and report how many went.
	//
	// This is the ONLY place records are cleaned up in bulk, and it is deliberately not the
	// Designer's delete: that event is a MARK on a metaobject, and the user may still close
	// without saving. A job ceases to exist at the RESTRUCTURING, which drops its table in its own
	// transaction — and the honest way to notice is afterwards, at start-up, when what survived is
	// exactly what registered.
	//
	// `alsoLive` is what is alive but NOT registered: a row switched off, a job whose Use flag is
	// clear. They keep their settings and their clock — switching something off must not erase
	// when it last ran, which is precisely what one looks at before switching it back on.
	std::size_t PurgeSharedState(const std::vector<ibGuid>& alsoLive = {});

	// THE SHARED CLOCK, public alongside the settings above — sys_job is the ONE record every kind
	// of job keeps, and a parameterized ROW is a job like any other: it stamps its run here and
	// reads its last run from here, instead of keeping a second copy in its own table that could
	// disagree. Keyed by the job's guid; the display name rides along so the table stays readable.
	//
	// Best-effort by design: a database that cannot answer must not stop a job, since silently
	// skipping work is worse than repeating it.
	static wxDateTime ReadSharedLastRun(const ibGuid& key);
	static void       WriteSharedLastRun(const ibGuid& key, const wxString& name, const wxDateTime& when);

	// Apply settings to a REGISTERED job, at once — no re-registration, because the entry holds a
	// session and dropping it would end a run in flight. Returns false when the key is unknown.
	bool ApplySettings(const ibGuid& key, bool active, const ibJobScheduleDescription& schedule);

	// The job registered under this key, by NAME — what a settings value needs to run one by hand
	// (RunNow is keyed by name, because that is the manager's own identity for an entry).
	wxString FindNameByKey(const ibGuid& key) const;

private:

	// (ReadSharedLastRun / WriteSharedLastRun are PUBLIC, declared with the settings pair above:
	//  the claim answers "nobody is running it now", the clock answers "nobody ran it recently",
	//  and only the two together make N processes on one base behave like one scheduler.)

	// The key a job's shared row lives under. One place, so a rename can never reach the storage.
	static ibGuid KeyOf(const ibJobDescription& desc) { return desc.m_key; }

	// Submit the body. Caller holds m_mtx; the submit itself does not block.
	bool Launch(ibJobEntry& e);

	ibJobEntry* Find(const wxString& name);

	mutable std::mutex                        m_mtx;
	std::vector<std::unique_ptr<ibJobEntry>>  m_entries;

	// HOW MANY RUN AT ONCE — not how many may be declared.
	//
	// Every run holds a session and a session holds one pooled connection, so this is sized
	// against ibConnectionPool's maxSize of 32: a handful of concurrent runs leaves the great
	// majority of connections for sessions that have users behind them. Hosts that know better
	// call SetMaxJobs.
	//
	// Registration is NOT capped, and that is what makes a parameterized job affordable: each of
	// its active rows registers itself, so a hundred and fifty exchanges are a hundred and fifty
	// entries — each asleep until its own schedule says otherwise, and a sleeping job costs two
	// timestamp comparisons per tick. What the cap does is stop them from all waking together;
	// the ones over the line stay due and are taken on a later tick.
	std::size_t                               m_maxJobs = 4;

	bool                                      m_stopped = false;

	// The manager's own tick. Sleeps a second, asks what is due, submits it —
	// nothing else. Guarded so an escaped exception cannot kill the schedule: the
	// thread logs and keeps ticking, because a manager that silently stopped
	// looking is indistinguishable from one with nothing to do.
	void ThreadBody();

	// Background runs this manager started — held until they finish, then let go
	// on a tick. The caller may or may not keep the handle it was given (a run
	// started and forgotten is the ordinary case), so this list is what guarantees
	// the run reaches its end and gives its session back in an orderly way.
	//
	// Why hold rather than watch: the session must be released OFF ITS OWN WORKER.
	// ibSession::Teardown queues an empty task on that session's queue and waits
	// for it, so dropping the last reference from inside the run's own task means
	// waiting behind oneself — the same reason the scheduled path keeps
	// m_runSession and releases it in HarvestFinished. It also means a run that
	// took two milliseconds still has a sys_session row when the next cluster
	// snapshot is taken, so a background job SHOWS in Active Users instead of
	// vanishing between refreshes.
	//
	// And when the manager goes down, everything it started goes with it: a run
	// outliving its manager would be work nobody is watching, on a session nobody
	// will close, against a database that is being torn down — Stop() cancels them
	// and waits.
	std::vector<std::shared_ptr<ibBackgroundRun>> m_background;

	// Keys that EXIST but are not registered — switched-off rows and jobs whose Use flag is clear.
	// Kept only so the orphan sweep can tell "nobody declared this" from "declared, switched off";
	// nothing schedules from this list.
	std::vector<ibGuid>                       m_liveKeys;

	// Signalled when a tick finishes launching an entry and clears its m_inFlight. Unregister and
	// Stop wait on it, so they never destroy an entry a launch is still writing to.
	std::condition_variable                   m_inFlightCv;

	std::thread                               m_thread;
	std::condition_variable                   m_tickCv;
	std::mutex                                m_tickMtx;
	std::atomic<bool>                         m_tickStop { false };
	std::atomic<bool>                         m_tickRunning { false };

	// HOW N PROCESSES ON ONE BASE BEHAVE LIKE ONE SCHEDULER.
	//
	// The worker pool's per-session lease rules out two runs of a job inside THIS
	// process, and says nothing about a second one. A file base can have several
	// clients open on the same folder; a server base, any number. Some jobs
	// survive being run twice (the totals fold only wastes work) and some do not
	// (two folds racing on one table corrupt the sum), so the manager does not
	// leave it to the tenant. Two mechanisms, answering two different questions:
	//
	//   sys_lock, `Job.<name>`  — "is somebody running it RIGHT NOW?"
	//   sys_job.lastRun         — "has it already run recently?"
	//
	// Neither alone is enough. Without the claim, two processes start the same job
	// at the same instant. Without the shared last-run, each process keeps its own
	// clock and the job fires once per process per interval.
	//
	// The claim's owner is the JOB'S SESSION, not this object — see
	// ibJobSessionLockHolder in the .cpp. That is what makes a crash survivable
	// (the claim is swept with the session) and what gives liveness for free (the
	// registry heartbeats the session's row while the run holds it).
};

#endif // !__IB_JOB_MANAGER_H__





