////////////////////////////////////////////////////////////////////////////
//	Description : ibJobManager — schedule + sessions for out-of-thread work
////////////////////////////////////////////////////////////////////////////

#include "jobManager.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"   // ibApplicationData::CreateSession<T> lives here
#include "backend/lock/lockManager.h"          // cross-process claim on a job name
#include "backend/logger/logger.h"            // a run leaves a row in the journal
#include "backend/backend_exception.h"
#include "backend/moduleManager/moduleManager.h"   // root module manager -> GetProcUnit
#include "backend/compiler/procUnit.h"             // CallAsFunc by name
#include "backend/session/workerPool.h"            // CancelSession for ibBackgroundRun::Cancel
// sys_job — the shared last-run clock. Through the L2 door: it renders the
// dialect (upsert above all) so nothing here has to ask which driver it is on.
#include "backend/databaseLayer/databaseQueryBuilder.h"

#include <algorithm>

#include <wx/datetime.h>
#include <wx/log.h>

namespace {

// HOW LONG A TENANT WAITS — for the registry to answer its Add, and for the pool to
// hand it a connection. Short, and short for one reason: a rented run is serving
// somebody who is already waiting, and who has something to show meanwhile (the rows
// already on screen). "Not now" is an answer they can act on; half a minute of
// patience is not. A standalone run keeps the generous defaults — nobody is watching
// it, and failing it early only means asking again.
constexpr std::chrono::seconds kTenantWait { 5 };

// WHAT A BACKGROUND RUN IS HANDED — one parcel rather than a capture list.
//
// The task needs eight things and outlives the call that built it, so they travel
// together and by shared_ptr: the closure copies one pointer, and what the run is
// given stays readable as a list of fields instead of a line of captures nobody can
// take in. Everything here is filled on the CALLER's thread, where the answers are
// still to hand.
struct ibBackgroundLaunch {
	std::shared_ptr<ibBackgroundRun>  m_run;
	ibSession*                        m_session  = nullptr;   // owned by m_run's holder
	ibSessionRegistry*                m_registry = nullptr;
	ibUserInfo                        m_initiator;            // empty for a tenant — it installs none
	ibJobManager::ibBackgroundBody    m_body;
	wxString                          m_activity;
	bool                              m_tenant   = false;
	// The session this run RENTS. Held for the run's lifetime rather than for its
	// use: a tenant reads through its parent's access policy, so the object that
	// policy lives on must still be there while the read is out. Empty otherwise.
	std::shared_ptr<ibSession>        m_parent;
};

// A finished future is one that is ready NOW. Anything else — still running, or
// never started — is not harvestable. Zero timeout so this never blocks a tick.
bool FutureReady(const std::future<void>& f)
{
	return f.valid()
		&& f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// The claim's owner identity is the JOB'S SESSION, not the process.
//
// That is what makes a crash survivable. A lock row is cleaned up when its owner
// is no longer among the live sessions — the registry sweeps stale sys_session
// rows by heartbeat, and the lock manager drops every row whose owner is not in
// that list. Owning the claim as a process-level identity instead would leave it
// behind forever when the process dies mid-run, and every peer would then skip
// that job for good.
//
// It also gives "I am still alive" for free: the session's row is heartbeated
// once a second by the registry for as long as the run holds the session, so a
// long job is visibly alive without inventing a second liveness channel.
class ibJobSessionLockHolder : public ibLockHolder {
public:
	explicit ibJobSessionLockHolder(const ibSession* session, const wxString& jobName)
		: m_guid(session->Identity().m_guid)
		, m_name(wxString::Format(wxT("job: %s"), jobName))
		, m_computer(session->Identity().m_computer)
	{
	}

	ibGuid   Identity()    const override { return m_guid; }
	wxString DisplayName() const override { return m_name; }
	wxString Computer()    const override { return m_computer; }

private:
	ibGuid   m_guid;
	wxString m_name;
	wxString m_computer;
};

} // namespace

int ibJobManager::FindNonTransferable(const std::vector<ibValue>& args)
{
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (!args[i].IsTransferable())
			return static_cast<int>(i);
	}
	return -1;
}

void ibJobManager::CheckTransferable(const std::vector<ibValue>& args)
{
	const int bad = FindNonTransferable(args);
	if (bad < 0)
		return;

	// Name the position AND the class: "argument 2" alone sends the author
	// counting, and the class is what tells them WHY — a form, an object, a record
	// set. The remedy follows from the class, so it is worth the lookup.
	ibBackendCoreException::Error(
		_("Passing a mutable value to a job is not allowed: argument %d (%s). Pass a reference or a plain value instead."),
		bad + 1,   // 1-based: script counts arguments from one
		args[static_cast<std::size_t>(bad)].GetClassName());
}

ibJobManager::ibJobManager(ib::AppDataCtorToken)
{
}

ibJobManager::~ibJobManager()
{
	Stop();
}

bool ibJobManager::IsRunning(const ibJobEntry& e)
{
	return e.m_future.valid() && !FutureReady(e.m_future);
}

bool ibJobManager::IsDue(const ibJobEntry& e, std::chrono::steady_clock::time_point now)
{
	// TWO independent questions, joined by AND — that composition is what makes
	// "10:00–15:00 on Tuesdays in March" expressible without a grammar: the
	// calendar answers WHETHER THIS MOMENT QUALIFIES, the interval answers
	// WHETHER ENOUGH TIME HAS PASSED. Drop the calendar and the job runs at any
	// hour; drop the interval and it re-runs on every tick for the whole window.
	// ONE QUESTION: HAS THE DUE MOMENT ARRIVED? Not "does this instant qualify" — that reading is
	// what let a missed hour vanish. A schedule is a promise: the moment it names either is still
	// ahead (wait) or is already behind us (run now, late). Nothing about being late cancels the run.
	//
	// The due moment is computed, not remembered: take the point we last ran from, add the interval
	// the schedule demands between runs, then ask the calendar for the first moment at or after that
	// which it allows. A time of day therefore reads as NOT BEFORE rather than ONLY AT — start the
	// base at 07:00 with a 02:00 job whose night has already passed and it runs at 07:00, because
	// 02:00 is behind us; start it at 10:00 with nothing missed and it waits for 02:00, because that
	// is ahead. Same formula answers both.
	//
	// FIRST RUN has no last-run to count from, so it counts from when we started watching — and it
	// skips the interval, because an interval is the gap BETWEEN runs and there was no previous one.
	// Without that a weekly backup could never start at all: no desktop client lives a week, so the
	// first gap never elapsed and the most carefully scheduled job was the one that never fired.
	// What protects a plain cadence ("every six hours", no time of day) from firing on every launch
	// is that its calendar allows any moment, so the interval from registration is the whole answer.
	const ibJobScheduleDescription& sched = e.m_desc.m_schedule;
	wxDateTime countFrom;
	if (e.m_everRun) {
		countFrom = e.m_lastRunAt.IsValid() ? e.m_lastRunAt : e.m_registeredAtWall;
		countFrom += wxTimeSpan::Seconds(sched.m_intervalSeconds);
	}
	else {
		countFrom = e.m_registeredAtWall;
		if (sched.m_startMinute < 0 && sched.m_endMinute < 0)
			countFrom += wxTimeSpan::Seconds(sched.m_intervalSeconds);
	}

	const wxDateTime dueAt = ibJobScheduleRules::NextAllowedAfter(sched, countFrom);
	if (!dueAt.IsValid())
		return false;   // the calendar names no moment at all — a schedule that can never run
	if (dueAt.IsLaterThan(wxDateTime::Now()))
		return false;   // still ahead

	// The due moment has arrived. One guard left: a WALL clock decided that, and a wall clock can be
	// moved. The steady clock is what makes a run twice in one second impossible regardless — unless
	// the previous pass paced itself and asked to continue, which is a deliberate "again, now".
	if (e.m_workRemains)
		return true;
	if (!e.m_everRun)
		return true;
	return (now - e.m_lastRun) >= std::chrono::seconds(sched.m_intervalSeconds);
}

void ibJobManager::HarvestFinished(ibJobEntry& e)
{
	if (!FutureReady(e.m_future))
		return;

	// The VERDICT was decided on the worker, at the moment the run ended (see
	// Launch). Nothing is caught here — there is nothing left to catch, because
	// the task never let anything escape. This only copies the answer across.
	if (e.m_result != nullptr && e.m_result->m_done.load(std::memory_order_acquire)) {
		const bool skipped = e.m_result->m_skipped.load(std::memory_order_acquire);
		const bool ok      = e.m_result->m_ok.load(std::memory_order_acquire);
		e.m_outcome     = skipped ? ibJobOutcome::Skipped
		                : ok      ? ibJobOutcome::Succeeded
		                          : ibJobOutcome::Failed;
		e.m_workRemains = ok && e.m_result->m_more.load(std::memory_order_acquire);

		std::lock_guard<std::mutex> lk(e.m_result->m_mtx);
		e.m_error = e.m_result->m_error;
		if (!ok)
			wxLogDebug(wxT("job '%s' failed: %s"), e.m_desc.m_name, e.m_error);
	}

	// Drain the future so a stray exception (one thrown by the POOL rather than by
	// the body — a stopped pool rejecting the task) cannot surface out of the
	// future's destructor.
	try { e.m_future.get(); } catch (...) {}

	// THE SESSION GOES NOW — the run is over, so its session has no reason to
	// exist. Done here rather than inside the task because Teardown waits behind
	// the session's own queue, and this thread is not on it.
	if (e.m_runSession) {
		e.m_runSession->Reset();
		e.m_runSession.reset();
	}

	e.m_future = std::future<void>();
	e.m_result.reset();
}

// ---------------------------------------------------------------------------
// The SHARED clock — sys_job.
//
// The cross-process claim answers "is somebody running this RIGHT NOW". That is
// a different question from "has it already run recently", and only the second
// one stops N processes on one base from each running a job once per interval.
// Two clients open on a file base, two web servers, a compute server next to a
// desktop — without this they all keep private clocks and the job fires once per
// process. With it, whoever gets there first writes the time and the rest see it.
//
// Best-effort by design: a database that cannot answer must not stop a job from
// running, because the alternative — silently skipping housekeeping because a
// SELECT failed — is worse than running it twice.
// ---------------------------------------------------------------------------

wxDateTime ibJobManager::ReadSharedLastRun(const wxString& name)
{
	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.From(job_table)
			.Select({ wxT("lastRun") })
			.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("jobName")), ibParam(0)))
			.Execute({ ibValue(name) });

		// Columns are 1-based, and a NULL comes back as TYPE_NULL rather than as
		// a zero date — a job row that exists but never ran reads as "no opinion".
		if (rs.Next()) {
			const ibValue last = rs.GetValue(1);
			// Through wxLongLong, the way every other ms-to-wxDateTime site in the tree does
			// it. GetDate() hands back a wxLongLong_t (`long long`), and wxDateTime's
			// constructors take time_t / double / wxLongLong — on LP64 none of those is an
			// exact match for `long long`, so the implicit conversion is ambiguous. MSVC
			// happens to pick one; naming wxLongLong says which, on every platform.
			if (!last.IsNull() && !last.IsEmpty())
				return wxDateTime(wxLongLong(last.GetDate()));
		}
	}
	catch (...) {
		// An unreadable clock is no opinion, not a veto: skipping housekeeping
		// because a SELECT failed is worse than running it twice.
	}
	return wxInvalidDateTime;
}

void ibJobManager::WriteSharedLastRun(const wxString& name, const wxDateTime& when)
{
	try {
		// UPSERT, not UPDATE-then-INSERT. The dialects spell it differently —
		// Firebird MATCHING, PostgreSQL / SQLite ON CONFLICT, MySQL's implicit
		// key — and closing that difference is exactly what this level is for.
		// Writing the fallback by hand here would have put a dialect question
		// into a scheduler.
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpsert(job_table,
			{
				{ wxT("jobName"),  ibParam(0) },
				{ wxT("lastRun"),  ibParam(1) },
				{ wxT("computer"), ibParam(2) },
			},
			{ wxT("jobName") }),
			{
				ibValue(name),
				ibValue(when),
				ibValue(appData != nullptr ? appData->GetComputerName() : wxString()),
			});
	}
	catch (...) {
		// A lost write means a peer may repeat the run. Tolerable; throwing here
		// would take the tick down instead.
	}
}

ibJobManager::ibJobEntry* ibJobManager::Find(const wxString& name)
{
	for (auto& e : m_entries)
		if (e->m_desc.m_name == name)
			return e.get();
	return nullptr;
}

bool ibJobManager::Launch(ibJobEntry& e)
{
	// THE TICK'S WHOLE JOB: decide that this is due, hand it over, return. It
	// opens nothing, connects to nothing and waits for nothing — a Connect can
	// take seconds, and the tick has the rest of the schedule to look after.
	auto result = std::make_shared<ibJobEntry::ibRunResult>();
	const ibJobDescription desc = e.m_desc;   // copy: the entry may move while this runs

	// THE SESSION IS THE MANAGER'S TO CREATE and the worker's to run under: it
	// goes in as the Submit argument, the way every other task's does. The pool
	// keeps one entry point and one meaning for it — a task never asks whose
	// session it is on, it is simply run in the order the manager chose.
	//
	// One session PER RUN, released when the run ends. Not kept between runs: a
	// session owns a pooled connection, and a job that works for ten seconds every
	// six hours would otherwise hold that connection for the whole six hours while
	// interactive sessions queue on Checkout.
	//
	// Carried through a shared_ptr because the task holds it alive: ibSessionHolder
	// is move-only while the pool's Task is a std::function, which must be
	// copyable. The holder dies with the closure, and the session with it — no
	// teardown step for anyone to forget.
	// ASK THE SHARED CLOCK FIRST — before a session exists.
	//
	// "Never ran" is remembered in memory, so every process start thinks every job
	// is due. Without this, opening the application always fires the whole
	// schedule: two sessions appear, do nothing, and go — which is exactly what it
	// looked like. The shared clock is what actually knows, and asking it here
	// means a job that ran ten minutes ago in another process never even opens a
	// session now.
	//
	// This is the CHEAP check (one indexed SELECT). The authoritative one still
	// happens later under the claim, where it is race-free — two processes can
	// pass this test at the same instant, and only one will pass that one.
	// Exclusive jobs only: a parameterised one has an entry per instance, so there
	// is no shared "when did THIS last run" that means anything.
	if (!e.m_workRemains && e.m_desc.m_exclusive) {
		const wxDateTime sharedLast = ReadSharedLastRun(e.m_desc.m_name);
		if (sharedLast.IsValid()) {
			const wxTimeSpan since    = wxDateTime::Now() - sharedLast;
			const wxTimeSpan interval = wxTimeSpan::Seconds(e.m_desc.m_schedule.m_intervalSeconds);
			if (since < interval) {
				// Somebody already did it. Adopt their time as ours so the local
				// clock stops asking the database on every tick.
				e.m_everRun   = true;
				e.m_lastRun   = std::chrono::steady_clock::now();
				e.m_lastRunAt = sharedLast;
				e.m_outcome   = ibJobOutcome::Skipped;
				return false;
			}
		}
	}

	auto runSession = std::make_shared<ibSessionHolder>(OpenRunSession(e.m_desc));
	if (!*runSession)
		return false;   // no session — try again on the next tick

	ibSession* const session = runSession->Get();

	e.m_future = session->Submit([session, runSession, desc, result]() {
		// 2. THE CLAIM, owned by this session so it dies with it — including if
		//    the process does. Refused means a peer is already running the job:
		//    not an error, just not ours this time.
		ibLockHandle claim;
		// Only an EXCLUSIVE job claims its record. A parameterised one is meant to
		// have several instances running at once, so there is nothing to claim and
		// nothing to be blocked by — see ibJobDescription::m_exclusive.
		if (ibLockManager* const locks = desc.m_exclusive
			? ibApplicationData::GetLockManager() : nullptr) {
			try {
				std::vector<ibLockItem> items;
				items.push_back(ibLockItem::ForNamespace(wxT("Job.") + desc.m_name,
				                                          ibLockMode::Exclusive));
				ibJobSessionLockHolder owner(session, desc.m_name);
				claim = locks->Acquire(items, {}, &owner);
			}
			catch (const ibBackendException&) {
				// SOMEBODY ELSE HOLDS IT. Not an error and not a retry — the work
				// is being done, just not by us. A job that must not run twice
				// (the totals fold corrupts sums if two passes race on one table)
				// is protected by exactly this, the same way two sessions never
				// share one lease.
				//
				// Logged, because "it did not run here" is a question an
				// administrator will otherwise ask the wrong way round.
				if (ibLogger* const log = ibApplicationData::GetLogger()) {
					log->Audit(wxT("job"), wxT("blocked"),
					           wxString::Format(_("Job '%s' is already running elsewhere"),
					                            desc.m_name));
				}
				result->m_skipped.store(true, std::memory_order_release);
				result->m_done.store(true, std::memory_order_release);
				return;
			}
		}

		// 3. THE SHARED CLOCK. The claim proved nobody is running it right now,
		//    which says nothing about the process that ran it two minutes ago and
		//    already let go. Without this every process on the base runs the job
		//    once per interval instead of the base running it once.
		//
		//    Exclusive jobs only: a parameterised job has one record per instance,
		//    so there is no shared "when did THIS last run" to consult.
		const wxDateTime sharedLast = desc.m_exclusive ? ReadSharedLastRun(desc.m_name)
		                                              : wxInvalidDateTime;
		if (sharedLast.IsValid()) {
			// Compare SPANS, never a span converted to long. A row that exists but
			// has never actually run comes back as a zero date, and the distance
			// from year zero to now is some 63 billion seconds — wxLongLong::ToLong
			// asserts on that, which under Debug is an int 3 and takes the process
			// down. It also took the run with it, which is why these sessions
			// looked like they were hanging: the task died here and never reached
			// the line that releases the session.
			const wxTimeSpan since    = wxDateTime::Now() - sharedLast;
			const wxTimeSpan interval = wxTimeSpan::Seconds(desc.m_schedule.m_intervalSeconds);
			if (since < interval) {
				result->m_skipped.store(true, std::memory_order_release);
				result->m_done.store(true, std::memory_order_release);
				claim.Release();
				return;
			}
		}

		// Stamped BEFORE the run, not after: a peer ticking while this is in
		// flight must see the job as taken. The claim covers that window too, but
		// the stamp is what survives this process letting go — including by dying.
		if (desc.m_exclusive)
			WriteSharedLastRun(desc.m_name, wxDateTime::Now());

		// 4. THE BODY, wrapped. Visible in Active Users while it lasts.
		session->SetActivity(wxString::Format(wxT("job: %s"), desc.m_name));
		try {
			result->m_more.store(desc.m_body(session), std::memory_order_release);
			result->m_ok.store(true, std::memory_order_release);
		}
		catch (const ibBackendException& err) {
			std::lock_guard<std::mutex> lk(result->m_mtx);
			result->m_error = err.GetErrorDescription();
		}
		catch (...) {
			std::lock_guard<std::mutex> lk(result->m_mtx);
			result->m_error = _("unknown exception");
		}
		session->SetActivity(wxEmptyString);

		// 5. INTO THE JOURNAL, so a run leaves a trace an administrator can read
		//    afterwards. A scheduled job runs unattended by definition — without a
		//    row here, "did it run last night, and how did it go?" has no answer
		//    short of watching Active Users at the right moment.
		if (ibLogger* const log = ibApplicationData::GetLogger()) {
			const bool ok = result->m_ok.load(std::memory_order_acquire);
			if (ok) {
				log->Audit(wxT("job"), wxT("finished"),
				           wxString::Format(_("Job '%s' completed"), desc.m_name));
			}
			else {
				std::lock_guard<std::mutex> lk(result->m_mtx);
				log->Audit(wxT("job"), wxT("failed"),
				           wxString::Format(_("Job '%s' failed: %s"), desc.m_name, result->m_error));
			}
		}

		// 6. THE VERDICT, published LAST so a reader that sees it done finds the
		//    answer already there.
		result->m_done.store(true, std::memory_order_release);

		// The session is NOT released here: Teardown queues an empty task on this
		// very session and waits for it, which from inside one of its own tasks is
		// waiting behind oneself. The tick releases it in HarvestFinished instead —
		// one tick later at the outside, and off this session's worker.
		claim.Release();
	});

	e.m_runSession = runSession;   // released by HarvestFinished, off the worker
	e.m_result    = std::move(result);
	e.m_everRun   = true;
	e.m_lastRun   = std::chrono::steady_clock::now();
	e.m_lastRunAt = wxDateTime::Now();
	e.m_outcome   = ibJobOutcome::Running;
	e.m_error.clear();
	// Cleared here rather than after: the pass is under way, so "work remained
	// from the PREVIOUS pass" is no longer the reason to be due.
	e.m_workRemains = false;
	return true;
}

// ---------------------------------------------------------------------------
// ibBackgroundRun — the handle on one background run
// ---------------------------------------------------------------------------

bool ibBackgroundRun::IsComplete() const
{
	return m_done.load(std::memory_order_acquire);
}

bool ibBackgroundRun::Wait(int milliseconds)
{
	// The future is written once, before the handle is published, so reading it
	// here needs no lock; waiting on it under one would block every other reader.
	if (!m_future.valid())
		return IsComplete();

	if (milliseconds <= 0) {
		m_future.wait();
		return true;
	}
	return m_future.wait_for(std::chrono::milliseconds(milliseconds))
		== std::future_status::ready;
}

ibValue ibBackgroundRun::Result() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_result;
}

wxString ibBackgroundRun::Error() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_error;
}

wxString ibBackgroundRun::Activity() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_activity;
}

void ibBackgroundRun::Cancel()
{
	// Cooperative — raises the flag and wakes the worker; the interpreter throws
	// ibBackendInterruptException at its next loop boundary and the task unwinds.
	ibSession* const session = m_holder.Get();
	if (session == nullptr)
		return;
	if (ibWorkerPool* const pool = session->GetWorkerPool())
		pool->CancelSession(session);
}

// ---------------------------------------------------------------------------

std::shared_ptr<ibBackgroundRun> ibJobManager::StartBackground(const wxString& procedureName,
                                                               const std::vector<ibValue>& args)
{
	// Gate first, before anything is created: a bad argument is the caller's
	// mistake and they are still on the stack to hear about it.
	CheckTransferable(args);

	if (procedureName.IsEmpty())
		ibBackendCoreException::Error(_("Background job: no procedure name given"));

	// Everything a background run IS — its own session, the initiator's identity,
	// the error capture, the handle — belongs to the general form below. What is
	// specific to a call BY NAME is only this: resolve ModuleName.MethodName
	// against the session's runtime and call it. So that is all this body does.
	const std::vector<ibValue> argsCopy = args;
	return StartBackground(
		[procedureName, argsCopy](ibSession* session) -> ibValue {
			// Resolve ModuleName.MethodName against the session's root module. The
			// name is REQUIRED to carry its module: a background call is made by
			// name from another session, so "which module did the author mean" has
			// to be in the name rather than in whatever happened to be in scope
			// where it was written.
			ibValueModuleManagerRuntimeConfiguration* const mm = session->GetManagerModule();
			if (mm == nullptr)
				ibBackendCoreException::Error(_("Background job: the session has no runtime"));

			std::shared_ptr<ibProcUnit> unit = mm->GetProcUnit();
			if (!unit)
				ibBackendCoreException::Error(_("Background job: the session has no runtime"));

			const int dot = procedureName.Find('.', /*fromEnd*/ true);
			if (dot == wxNOT_FOUND) {
				ibBackendCoreException::Error(
					_("Background job: '%s' must name the module — ModuleName.MethodName"),
					procedureName);
			}
			const wxString moduleName = procedureName.Left(dot);
			const wxString methodName = procedureName.Mid(dot + 1);

			ibValue moduleValue;
			if (!unit->GetPropVal(moduleName, moduleValue) || moduleValue.IsEmpty())
				ibBackendCoreException::Error(_("Background job: common module '%s' not found"), moduleName);

			// Only PUBLIC methods are on a module value's surface at all — that is
			// what a module value exposes ("exports are the whole surface",
			// moduleManager.h, where Export is the bytecode-side name for the
			// Public modifier). So this lookup enforces the visibility rule
			// without a second check: a Private method is simply not there.
			const long methodNum = moduleValue.FindMethod(methodName);
			if (methodNum == wxNOT_FOUND) {
				ibBackendCoreException::Error(
					_("Background job: '%s' has no public method '%s'"), moduleName, methodName);
			}

			std::vector<ibValue> callArgs = argsCopy;
			std::vector<ibValue*> ptrs;
			ptrs.reserve(callArgs.size() + 1);
			for (auto& v : callArgs) ptrs.push_back(&v);
			ptrs.push_back(nullptr);   // trailing null keeps a 0-arg array valid

			// Called as a FUNCTION: a procedure simply leaves the result empty, so
			// one path serves both and the caller's Result() answers honestly
			// either way.
			ibValue result;
			moduleValue.CallAsFunc(methodNum, result, ptrs.data(),
			                       static_cast<long>(callArgs.size()));
			return result;
		},
		wxString::Format(wxT("background: %s"), procedureName));
}

std::shared_ptr<ibBackgroundRun> ibJobManager::StartBackground(ibBackgroundBody body,
                                                               const wxString& activity,
                                                               ibJobTenancy tenancy)
{
	if (!body)
		ibBackendCoreException::Error(_("Background job: nothing to run"));

	ibSessionRegistry* const registry = ibApplicationData::GetSessionRegistry();
	if (registry == nullptr || appData == nullptr)
		ibBackendCoreException::Error(_("Background job: the application is not running"));

	auto launch = std::make_shared<ibBackgroundLaunch>();
	launch->m_registry = registry;
	launch->m_body     = std::move(body);
	launch->m_activity = activity;
	launch->m_tenant   = (tenancy == ibJobTenancy::Tenant);
	const bool tenant  = launch->m_tenant;

	// THE PARENT — the session this run rents, read here while it is still the
	// current one. Held for the run's whole life, because a tenant borrows its
	// access policy: the object the policy lives on must not be dismantled
	// underneath a read that is using it. A run with no parent is not a tenant at
	// all, so this refuses rather than quietly running unrented.
	if (tenant) {
		ibSession* const current = ibSession::Current();
		if (current == nullptr)
			ibBackendCoreException::Error(_("Background job: a rented run needs the session that starts it"));

		// ANY HOST WILL DO. A tenant is not a property of how the process was
		// started — it is a connection plus a policy, borrowed for the length of one
		// read. A list is a list whether it is opened in the Designer, in the thick
		// client or in a browser tab; the run is used up, brings the data back and
		// ends, and nothing about it needs to know which of those it was.
		launch->m_parent = current->shared_from_this();
	}

	// WHOSE identity this run adopts — read here, on the caller's thread, while
	// the caller's session is the current one. Inside the task it would resolve to
	// the job's own session, which has none yet. A tenant installs none: it does
	// not act as the user, it acts FOR the session that already does.
	if (!tenant)
		launch->m_initiator = appData->GetUserInfo();

	auto run = std::make_shared<ibBackgroundRun>();
	if (tenant) {
		// MINTED, NOT REGISTERED. The registry's Add exists to make a session
		// visible and governable — a row, the policy chain, the lookup index — and a
		// tenant wants none of that: it lives for one portion and answers to nobody
		// but the caller. Add is also not free. It is a handshake with the consumer
		// thread plus, inside ProcessAdd, a cluster-snapshot refresh (a SELECT over
		// sys_session), and paying that per scrolled page ON THE THREAD THAT ASKED
		// is felt as the window hanging.
		//
		// Everything the run actually needs it gets by construction: a worker queue
		// (the pool keys on the session pointer and creates one on first Submit), a
		// connection of its own (taken just below), and a teardown that gives the
		// connection back (~ibSession) without troubling the registry (Teardown
		// reads m_listed).
		auto minted = std::make_shared<ibSession>(wxString(wxNewUniqueGuid),
		                                          ibSessionKind::BackgroundJob);
		minted->SetUnlisted();
		run->m_holder = ibSessionHolder(std::move(minted));
	}
	else {
		run->m_holder = registry->CreateSessionOfKind(
			appData->GetAppMode(), appData->GetComputerName(),
			ibSessionKind::BackgroundJob,
			&ib_detail::MakeSessionFactory<ibSession>);
	}

	if (!run->m_holder)
		ibBackendCoreException::Error(_("Background job: session could not be created"));

	ibSession* const session = run->m_holder.Get();
	launch->m_run     = run;
	launch->m_session = session;

	if (tenant) {
		// THE LANDLORD. Server() is already "the session that hosts this one" —
		// what a web client's is to it, this run's parent is to this run — and
		// hanging the tenancy there is what lets GetAccessPolicy find the borrowed
		// policy without anyone threading it through. Set AFTER the session is
		// registered: the registry stamps its own answer during Add (the process's
		// web server, where there is one), and the parent is the truer one here.
		session->SetServer(launch->m_parent.get());

		// THE ONE THING THAT CANNOT BE RENTED. A session owns exactly one
		// connection and the parent's is busy with the parent's own work, so this
		// run takes its own — HERE, on the caller's thread, so a pool with nothing
		// free is an exception the caller can act on (keep the rows it has, read
		// inline, say so) instead of a failure discovered by nobody on a worker.
		// Bounded for the same reason: a form waiting on one portion needs an
		// answer, not a half-minute of patience it never asked for.
		session->Holder()->EnsureConnection(kTenantWait);
	}

	// Everything below runs ON THE WORKER, with the session already bound by the
	// pool's ibSessionScope. That binding is why the identity install, the compile
	// and the call all happen here rather than at submission: ibSession::Current(),
	// GetPUState(), the lambda runtime AND the query layer's connection + access
	// policy (ibDatabaseQueryBuilder reads both off the current session) all
	// resolve through it. That last one is why a background read sees exactly what
	// the initiator sees, RLS included, without this layer arranging anything.
	run->m_future = session->Submit([launch]() {
		const ibBackgroundLaunch&              l   = *launch;
		const std::shared_ptr<ibBackgroundRun> run = l.m_run;
		try {
			// 1. Adopt the initiator's identity. No password: Login is already
			//    split into AuthenticateUser (the check) and InstallUser (the
			//    commit), and only the commit is wanted here — the caller was
			//    authenticated, and this run is theirs.
			//
			// 2. Bring the runtime up. EnsureRoot + CompileRoot hang off the
			//    Authenticated notification, and without them the session has no
			//    root module and nothing to execute.
			//
			// A TENANT DOES NEITHER, and that is the whole saving. It runs no script,
			// so a compile of every common module would buy nothing; it acts for the
			// parent rather than as the user, so an identity of its own would be a
			// second answer to a question already answered next door. What RLS needs
			// — the policy — it borrows through Server() (ibSession::GetAccessPolicy).
			if (!l.m_tenant) {
				if (l.m_initiator.IsOk())
					l.m_registry->InstallUser(l.m_session, l.m_initiator, wxEmptyString);
				l.m_registry->NotifyAuthenticated(l.m_session);
			}

			{
				std::lock_guard<std::mutex> lk(run->m_mtx);
				run->m_activity = l.m_activity;
			}
			// Only into the row that exists. A tenant has none (it is unlisted), so
			// this would be a registry request per scrolled page for a line nobody
			// can read; its handle carries the text instead.
			if (!l.m_tenant)
				l.m_session->SetActivity(l.m_activity);

			// 3. The work itself. Whatever it is — a configuration's procedure
			//    resolved by name, or an engine read — it is a function of the
			//    session, and this layer neither knows nor needs to know which.
			const ibValue result = l.m_body(l.m_session);

			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_result = result;
		}
		catch (const ibBackendException& err) {
			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = err.GetErrorDescription();
		}
		catch (...) {
			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = _("unknown exception");
		}

		{
			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_activity.clear();
		}
		// Published LAST, and only after the result / error are stored: a watcher
		// that sees IsComplete() must find everything already there.
		run->m_done.store(true, std::memory_order_release);
	});

	// A SUBMIT THAT WAS NOT ACCEPTED MUST NOT READ AS "IN FLIGHT". A stopped pool
	// rejects by setting an exception on the future and never running the task. The
	// caller has by then raised its own "a read is out" state, and would wait for a
	// delivery that can never come — the spinner spins forever and the list refuses
	// every later portion, which is a wedged control rather than a slow one. If the
	// future came back already resolved with an exception, say so out loud: the
	// caller reads a refusal as "not now" and does the work itself.
	//
	// A ready future with no exception is the ordinary inline completion (the GUI
	// pool, or a reentrant submit) — consuming it here is harmless, since a watcher
	// asks IsComplete() and the result is already stored.
	if (run->m_future.valid()
	    && run->m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		try {
			run->m_future.get();
		}
		catch (...) {
			ibBackendCoreException::Error(_("Background job: the worker pool did not take the run"));
		}
	}

	// Watched from here on: when this manager stops, the run stops with it.
	// Expired entries are swept on the next tick, so this list never grows past
	// what is actually in flight.
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		m_background.push_back(run);
	}

	return run;
}

bool ibJobManager::Register(ibJobDescription desc)
{
	if (desc.m_name.IsEmpty() || !desc.m_body) {
		wxLogDebug(wxT("job registration rejected: incomplete description ('%s')"), desc.m_name);
		return false;
	}
	// A schedule that can never match is refused HERE rather than left to be
	// debugged as silence — an empty window, an inverted validity range or a
	// non-positive interval all produce a job that simply never runs.
	if (!desc.m_schedule.IsValid()) {
		wxLogDebug(wxT("job '%s' rejected: the schedule can never match"), desc.m_name);
		return false;
	}

	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_stopped)
			return false;
		if (Find(desc.m_name) != nullptr) {
			// Refuse rather than replace: a duplicate name means a double bootstrap,
			// and quietly keeping one copy would hide it until something runs twice.
			wxLogDebug(wxT("job '%s' is already registered"), desc.m_name);
			return false;
		}
		if (m_entries.size() >= m_maxJobs) {
			wxLogDebug(wxT("job '%s' rejected: at the %zu-job cap"), desc.m_name, m_maxJobs);
			return false;
		}
	}

	// DECLARATION ONLY — no session is built here. Registering has to be callable
	// from wherever the platform's job list lives (ibApplicationData, right after
	// the database opens), and at that moment there is no metadata, no user and
	// no reason to spend a Connect on a job that may not come due for six hours.
	// The session is materialised on first launch instead; see EnsureSession.
	std::lock_guard<std::mutex> lk(m_mtx);
	if (m_stopped || Find(desc.m_name) != nullptr || m_entries.size() >= m_maxJobs)
		return false;

	auto entry = std::make_unique<ibJobEntry>();
	entry->m_desc = std::move(desc);
	m_entries.push_back(std::move(entry));
	return true;
}

ibSessionHolder ibJobManager::OpenRunSession(const ibJobDescription& desc)
{
	if (ibApplicationData::GetSessionRegistry() == nullptr || appData == nullptr)
		return ibSessionHolder();

	// The session's KIND follows the job's origin, so Active Users shows what is
	// actually running rather than another anonymous Enterprise row.
	const ibSessionKind kind = desc.m_origin == ibJobOrigin::Platform
		? ibSessionKind::SystemJob
		: ibSessionKind::ScheduledJob;

	ibSessionHolder holder = ibApplicationData::GetSessionRegistry()->CreateSessionOfKind(
		appData->GetAppMode(), appData->GetComputerName(), kind,
		&ib_detail::MakeSessionFactory<ibSession>);

	if (!holder) {
		wxLogDebug(wxT("job '%s': session could not be created"), desc.m_name);
		return ibSessionHolder();
	}

	// WHOSE session this is.
	//
	// A platform job names nobody and stays anonymous — no user, therefore no RLS
	// policy, therefore the full view of the data. For the totals fold that is the
	// requirement rather than a convenience: reading through somebody's row filter
	// would fold a subset and write wrong sums.
	//
	// A configuration's job names its user, and gets exactly that user's rights —
	// same RLS, same result as if they had run it themselves. The identity is
	// installed without a password: `Login` is already split into the check and the
	// commit, and only the commit applies here.
	if (desc.m_runAsUser.isValid()) {
		ibSessionRegistry* const registry = ibApplicationData::GetSessionRegistry();
		// BY GUID, never by name: a user can be renamed, and a schedule that
		// resolved by name would then either stop running or — worse — find
		// somebody else who now holds that name.
		const ibUserInfo info = ibUserInfo::Read(desc.m_runAsUser);
		if (!info.IsOk() || registry == nullptr) {
			// Named a user who is not there: refuse rather than run the job
			// anonymously. Falling back to full rights is how a scheduled job ends
			// up seeing more than its author ever could.
			wxLogDebug(wxT("job '%s': user %s not found — the job will not run"),
			           desc.m_name, desc.m_runAsUser.str());
			return ibSessionHolder();
		}
		// Both before anything runs: install the identity, then let the
		// Authenticated notification build the root module and the lambda runtime
		// a script job needs.
		registry->InstallUser(holder.Get(), info, wxEmptyString);
		registry->NotifyAuthenticated(holder.Get());
	}

	return holder;
}

bool ibJobManager::Unregister(const wxString& name)
{
	std::future<void> pending;
	ibSessionHolder   holder;
	bool              found = false;

	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
			if ((*it)->m_desc.m_name != name)
				continue;
			found = true;
			// Move the future and the holder OUT, then wait outside the lock: the
			// running body may call back into the manager, and the session must not
			// die before its task finishes with it.
			pending = std::move((*it)->m_future);
						m_entries.erase(it);
			break;
		}
	}

	// The answer is whether the JOB was there, not whether it had a session. A
	// declared job that has not come due yet holds no session at all — reading the
	// holder as the verdict reported "no such job" for one just removed.
	if (!found)
		return false;

	if (pending.valid()) {
		try { pending.get(); } catch (...) { /* logged by whoever harvests; here it is teardown */ }
	}
	return true;   // holder dies here — the session goes with it
}

void ibJobManager::Start()
{
	bool expected = false;
	if (!m_tickRunning.compare_exchange_strong(expected, true))
		return;   // already ticking

	m_tickStop.store(false);
	m_thread = std::thread(&ibJobManager::ThreadBody, this);
}

void ibJobManager::ThreadBody()
{
	// One second is a granularity, not a workload: a tick with nothing due is a
	// mutex and a clock read. Anything coarser would make a job declared "every
	// 5 seconds" mean something else.
	constexpr auto kInterval = std::chrono::seconds(1);

	while (!m_tickStop.load(std::memory_order_acquire)) {
		{
			// Sleep on the CV rather than sleep_for, so Stop() wakes us at once
			// instead of after up to a second — teardown should not have to wait
			// out a tick.
			std::unique_lock<std::mutex> lk(m_tickMtx);
			m_tickCv.wait_for(lk, kInterval,
				[this] { return m_tickStop.load(std::memory_order_acquire); });
		}
		if (m_tickStop.load(std::memory_order_acquire))
			break;

		// Nothing may escape. Unlike the session registry's thread, this one is
		// not fail-stop — a job that throws is a job's problem — but a thread that
		// dies here would leave the schedule silently frozen, which reads exactly
		// like "nothing was due".
		try {
			(void)Tick();
		}
		catch (const ibBackendException& err) {
			wxLogDebug(wxT("job tick failed: %s"), err.GetErrorDescription());
		}
		catch (...) {
			wxLogDebug(wxT("job tick failed with an unknown exception"));
		}
	}
}

int ibJobManager::Tick()
{
	const auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lk(m_mtx);
	if (m_stopped)
		return 0;

	// Forget background runs that have finished — they are watched only so Stop()
	// can reach them, and a finished one has nothing left to reach.
	m_background.erase(
		std::remove_if(m_background.begin(), m_background.end(),
		               [](const std::weak_ptr<ibBackgroundRun>& w) { return w.expired(); }),
		m_background.end());

	int started = 0;
	for (auto& slot : m_entries) {
		ibJobEntry& e = *slot;

		HarvestFinished(e);
		if (IsRunning(e))
			continue;
		if (!IsDue(e, now))
			continue;
		if (Launch(e))
			++started;
	}
	return started;
}

bool ibJobManager::RunNow(const wxString& name)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	if (m_stopped)
		return false;

	ibJobEntry* const e = Find(name);
	if (e == nullptr)
		return false;

	HarvestFinished(*e);
	if (IsRunning(*e))
		return false;   // already going — a manual run does not queue behind it

	return Launch(*e);
}

void ibJobManager::SetMaxJobs(std::size_t n)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	m_maxJobs = n;
}

std::size_t ibJobManager::GetMaxJobs() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_maxJobs;
}

std::size_t ibJobManager::Count() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_entries.size();
}

std::vector<ibJobState> ibJobManager::Snapshot() const
{
	std::lock_guard<std::mutex> lk(m_mtx);

	std::vector<ibJobState> out;
	out.reserve(m_entries.size());
	for (const auto& slot : m_entries) {
		const ibJobEntry& e = *slot;

		ibJobState state;
		state.m_name      = e.m_desc.m_name;
		state.m_outcome   = e.m_outcome;
		state.m_lastRunAt = e.m_lastRunAt;
		state.m_error     = e.m_error;

		state.m_schedule = ibJobScheduleRules::Describe(e.m_desc.m_schedule);

		// Next run is DERIVED, not stored, and derived through the CALENDAR —
		// "last run + interval" alone would promise 03:00 for a job that only
		// runs on Tuesdays. Left empty when the job is due right now (never ran,
		// or work remained), so an empty cell reads as "on the next tick" rather
		// than as a missing value.
		if (e.m_everRun && !e.m_workRemains && e.m_lastRunAt.IsValid()) {
			const wxDateTime earliest =
				e.m_lastRunAt + wxTimeSpan::Seconds(e.m_desc.m_schedule.m_intervalSeconds);
			state.m_nextRunAt = ibJobScheduleRules::NextAllowedAfter(e.m_desc.m_schedule, earliest);
		}

		out.push_back(std::move(state));
	}
	return out;
}

std::size_t ibJobManager::RunningCount() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	std::size_t running = 0;
	for (const auto& e : m_entries)
		if (IsRunning(*e)) ++running;
	return running;
}

void ibJobManager::Stop()
{
	// Stop ticking FIRST, and join before touching the entries: a tick in flight
	// is reading m_entries and may be launching. Waking it through the CV means
	// this returns in microseconds rather than after the tick interval.
	if (m_tickRunning.exchange(false)) {
		m_tickStop.store(true, std::memory_order_release);
		m_tickCv.notify_all();
		if (m_thread.joinable())
			m_thread.join();
	}

	// EVERYTHING THIS MANAGER STARTED GOES WITH IT — background runs included.
	// They are not on the schedule and nobody may be holding their value, but
	// they are on sessions this process owns, against a database about to be torn
	// down. Leaving one running would be work nobody is watching, on a session
	// nobody will close.
	//
	// Cancel first, then wait: cancellation is cooperative (the interpreter
	// unwinds at its next loop boundary), so asking everyone before waiting on
	// anyone means the waits overlap instead of queueing.
	{
		std::vector<std::shared_ptr<ibBackgroundRun>> alive;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			for (auto& w : m_background)
				if (auto run = w.lock()) alive.push_back(std::move(run));
			m_background.clear();
		}
		for (auto& run : alive) run->Cancel();
		for (auto& run : alive) run->Wait();
	}

	std::vector<std::unique_ptr<ibJobEntry>> doomed;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_stopped)
			return;
		m_stopped = true;
		doomed.swap(m_entries);
	}

	// Wait outside the lock. Each run owns its own session inside its task, so
	// waiting for the task IS waiting for the session to be let go — there is no
	// holder here to reset.
	for (auto& e : doomed) {
		if (e->m_future.valid()) {
			try { e->m_future.get(); } catch (...) { /* shutdown — nothing left to tell */ }
		}
	}
}











