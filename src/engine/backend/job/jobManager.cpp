////////////////////////////////////////////////////////////////////////////
//	Description : ibJobManager — schedule + sessions for out-of-thread work
////////////////////////////////////////////////////////////////////////////

#include "jobManager.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"   // a run opens a session of its own
#include "backend/lock/lockManager.h"          // cross-process claim on a job's key
#include "backend/logger/logger.h"             // a run leaves a row in the journal
#include "backend/backend_exception.h"

#include <algorithm>

#include <wx/datetime.h>
#include <wx/log.h>

namespace {

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
	// SWITCHED OFF is not "not due yet" — it is "not on the schedule at all". The entry stays
	// registered and stays in the list, and RunNow still runs it by hand: turning a job off must
	// not also take away the ability to fire it once and watch what happens.
	if (!e.m_desc.m_active)
		return false;

	const ibJobScheduleDescription& sched = e.m_desc.m_schedule;
	wxDateTime countFrom;
	if (e.m_retryAt.IsValid()) {
		// A RETRY counts from its own moment, not from the interval — the previous pass did not
		// happen, so there is no gap between runs to space out. The calendar below still applies:
		// a second chance must not sneak the job into an hour its window forbids.
		countFrom = e.m_retryAt;
	}
	else if (e.m_everRun) {
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

void ibJobManager::HarvestFinished(ibJobEntry& e, std::vector<std::shared_ptr<ibSessionHolder>>& release)
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

		// RETRY BOOKKEEPING, decided here because this is where the verdict lands.
		//
		//   succeeded — the streak is over: refill the allowance and drop any pending attempt.
		//   failed    — spend one attempt if any are left, and put the job ahead of its schedule.
		//   skipped   — a peer ran it; that is not our failure and costs no attempt.
		//
		// Note it is the ATTEMPT that is scheduled, not the failure that is remembered: once the
		// attempts run out the job simply returns to its ordinary schedule, still marked Failed for
		// whoever reads the list.
		if (e.m_outcome == ibJobOutcome::Succeeded) {
			e.m_retriesLeft = e.m_desc.m_retryCount;
			e.m_retryAt     = wxDateTime();
		}
		else if (e.m_outcome == ibJobOutcome::Failed && e.m_retriesLeft > 0) {
			--e.m_retriesLeft;
			const int wait = e.m_desc.m_retryIntervalSeconds > 0 ? e.m_desc.m_retryIntervalSeconds : 1;
			e.m_retryAt = wxDateTime::Now() + wxTimeSpan::Seconds(wait);
		}
		else {
			e.m_retryAt = wxDateTime();
		}

		std::lock_guard<std::mutex> lk(e.m_result->m_mtx);
		e.m_error = e.m_result->m_error;
		if (!ok)
			ibJournalInfo(wxT("job"),wxT("job '%s' failed: %s"), e.m_desc.m_name, e.m_error);
	}

	// Drain the future so a stray exception (one thrown by the POOL rather than by
	// the body — a stopped pool rejecting the task) cannot surface out of the
	// future's destructor.
	try { e.m_future.get(); } catch (...) {}

	// THE SESSION GOES — the run is over, so it has no reason to exist. Not done here, though:
	// releasing a holder runs Teardown, which waits behind the session's own queue, and this is
	// called with the manager's mutex held. HANDED OUT instead, for the caller to drop once the
	// lock is gone (Tick). The entry lets go of it now, so nothing else can reach it.
	if (e.m_runSession) {
		release.push_back(std::move(e.m_runSession));
		e.m_runSession.reset();
	}

	e.m_future = std::future<void>();
	e.m_result.reset();
}

bool ibJobManager::ApplySettings(const ibGuid& key, bool active, const ibJobScheduleDescription& schedule)
{
	std::lock_guard<std::mutex> lock(m_mtx);

	ibJobEntry* entry = nullptr;
	for (auto& e : m_entries)
		if (KeyOf(e->m_desc) == key) { entry = e.get(); break; }

	if (entry == nullptr)
		return false;

	// Updated IN PLACE. Unregister + register would drop the entry's session, which a run in
	// flight is using, and would reset the registration moment — turning "I changed the schedule"
	// into "the job forgot it ever ran".
	entry->m_desc.m_active = active;
	entry->m_desc.m_schedule = schedule;
	return true;
}

wxString ibJobManager::FindNameByKey(const ibGuid& key) const
{
	std::lock_guard<std::mutex> lock(m_mtx);
	for (const auto& e : m_entries)
		if (KeyOf(e->m_desc) == key)
			return e->m_desc.m_name;
	return wxString();
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
		const wxDateTime sharedLast = ReadSharedLastRun(KeyOf(e.m_desc));
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
				// `.str()` spelled out, and the literal wrapped: a bare `wxT("Job.") + guid` is a
				// wchar_t array plus a class, which MSVC resolves through ibGuid's conversion and
				// GCC refuses outright (portability.md — the compilers disagree about which
				// implicit conversions a built-in operator may reach for).
				items.push_back(ibLockItem::ForNamespace(wxString(wxT("Job.")) + KeyOf(desc).str(),
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
		const wxDateTime sharedLast = desc.m_exclusive ? ReadSharedLastRun(KeyOf(desc))
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
			WriteSharedLastRun(KeyOf(desc), desc.m_name, wxDateTime::Now());

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

	// The pending attempt has been taken — a new failure will schedule the next one. Cleared HERE
	// rather than on the verdict, so a retry that is still waiting survives an intervening tick.
	e.m_retryAt = wxDateTime();

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

bool ibJobManager::Register(ibJobDescription desc)
{
	if (desc.m_name.IsEmpty() || !desc.m_body) {
		ibJournalInfo(wxT("job"),wxT("job registration rejected: incomplete description ('%s')"), desc.m_name);
		return false;
	}
	// A schedule that can never match is refused HERE rather than left to be
	// debugged as silence — an empty window, an inverted validity range or a
	// non-positive interval all produce a job that simply never runs.
	if (!desc.m_schedule.IsValid()) {
		ibJournalInfo(wxT("job"),wxT("job '%s' rejected: the schedule can never match"), desc.m_name);
		return false;
	}

	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_stopped)
			return false;
		if (Find(desc.m_name) != nullptr) {
			// Refuse rather than replace: a duplicate name means a double bootstrap,
			// and quietly keeping one copy would hide it until something runs twice.
			ibJournalInfo(wxT("job"),wxT("job '%s' is already registered"), desc.m_name);
			return false;
		}
	}

	// THE DECLARATION SEEDS, THE BASE HOLDS. A job's schedule and its on/off switch live in
	// sys_job so they can be changed from the enterprise; the declaration is only where a base
	// that has never seen this job gets its starting values from.
	//
	// Done here, OUTSIDE the lock — it is a database round trip, and the tick shares this mutex
	// with it (a mutex a hot path shares with a slow one is the bug, not the symptom).
	//
	// ⚠ A schedule changed in the Designer therefore does NOT reach a base that already has the
	// row. Correct for a default, surprising in practice — the job list needs a "reset to the
	// configuration's value" action, or the first support call is about exactly this.
	//
	// ASKED, NOT ASSUMED: the row is found BY THE KEY, so a job that has none has no row to read
	// and none to seed — the register is keyed by guid, and a name is a caption. Every job the
	// platform declares carries one (a minted literal for the engine's own, the metaobject's guid
	// for a configuration's, the row's own for a parameterized one), so this skips nothing real;
	// what it stops is asking the base a question that has no subject.
	if (KeyOf(desc).isValid()) {
		const ibJobSettings stored = ReadSharedSettings(KeyOf(desc));
		if (stored.m_found) {
			desc.m_active = stored.m_active;
			// An empty stored schedule (a row written by an older build) is not an opinion —
			// keeping the declaration's is better than scheduling nothing.
			if (stored.m_schedule.IsValid())
				desc.m_schedule = stored.m_schedule;
		}
		else {
			ibJobSettings seed;
			seed.m_key = KeyOf(desc);
			seed.m_name = desc.m_name;
			seed.m_active = desc.m_active;
			seed.m_schedule = desc.m_schedule;
			// ⚠⚠ THE RESULT IS NOT DISCARDED. A base whose sys_job cannot be written is a base
			// where no job's settings survive a restart, and where the very first thing the
			// platform does on opening it has silently failed.
			//
			// It cost a whole evening: an INSERT rejected by a NOT NULL column threw, was caught
			// inside the write, returned false HERE, and startup carried on as if nothing had
			// happened — so the person running it saw a program that closed with no message, and
			// the actual Firebird sentence ("validation error for column SYS_JOB.LASTRUN") existed
			// only in a debugger.
			//
			// Raising puts that sentence in front of them: every ibBackendException records its own
			// description, and the startup path drains the whole chain into one dialog. So this adds
			// the ONE fact the chain is missing — WHICH job the platform choked on.
			//
			// ⚠ But ONLY when the base had its say. Registering must stay possible with no database
			// at all (see the note below, and JobManager.RegisterNeedsNoApplicationData): the seed
			// is what a base that has never seen this job starts from, not a precondition for
			// declaring one. Raising on NoBase turned "there is nothing to seed yet" into a failed
			// bring-up — which is the same conflation this used to make in the other direction.
			if (WriteSharedSettings(seed) == ibWriteOutcome::Refused)
				ibBackendCoreException::Error(
					_("the scheduled job '%s' could not be recorded in the base"), desc.m_name);
		}
	}

	// DECLARATION ONLY — no session is built here. Registering has to be callable
	// from wherever the platform's job list lives (ibApplicationData, right after
	// the database opens), and at that moment there is no metadata, no user and
	// no reason to spend a Connect on a job that may not come due for six hours.
	// The session is materialised on first launch instead; see EnsureSession.
	//
	// The seed above is the one thing that DOES touch the base, and it is deliberately
	// optional: with no connection it is skipped and the declaration stands, so a job
	// can be declared before — or entirely without — a database.
	std::lock_guard<std::mutex> lk(m_mtx);
	if (m_stopped || Find(desc.m_name) != nullptr)
		return false;

	auto entry = std::make_unique<ibJobEntry>();
	entry->m_desc = std::move(desc);
	// The allowance starts full — a job declared today has spent no attempts.
	entry->m_retriesLeft = entry->m_desc.m_retryCount;
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
		ibJournalInfo(wxT("job"),wxT("job '%s': session could not be created"), desc.m_name);
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
	ibSessionRegistry* const registry = ibApplicationData::GetSessionRegistry();

	if (desc.m_runAsUser.isValid()) {
		// BY GUID, never by name: a user can be renamed, and a schedule that
		// resolved by name would then either stop running or — worse — find
		// somebody else who now holds that name.
		const ibUserInfo info = ibUserInfo::Read(desc.m_runAsUser);
		if (!info.IsOk() || registry == nullptr) {
			// Named a user who is not there: refuse rather than run the job
			// anonymously. Falling back to full rights is how a scheduled job ends
			// up seeing more than its author ever could.
			ibJournalInfo(wxT("job"),wxT("job '%s': user %s not found - the job will not run"),
			           desc.m_name, desc.m_runAsUser.str());
			return ibSessionHolder();
		}
		// Both before anything runs: install the identity, then let the
		// Authenticated notification build the root module and the lambda runtime
		// a script job needs.
		registry->InstallUser(holder.Get(), info, wxEmptyString);
		registry->NotifyAuthenticated(holder.Get());
	}
	else if (desc.m_origin == ibJobOrigin::Configuration && registry != nullptr) {
		// NO USER NAMED — and that is a legitimate state, not an error: nobody's job runs with the
		// FULL view, because no identity means no RLS policy is built for it. Same reading as a
		// platform job, arrived at from the other side.
		//
		// It still needs a RUNTIME, though, and that is the whole reason this branch exists. A
		// configuration's body is SCRIPT, and the root module is built on the Authenticated
		// notification — so an anonymous job that skipped this call had nothing to call into and
		// failed every tick with "the session has no runtime". Notify without installing anyone:
		// authenticated as nobody is exactly what is meant here.
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
		std::unique_lock<std::mutex> lk(m_mtx);
		for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
			if ((*it)->m_desc.m_name != name)
				continue;
			found = true;

			// WAIT OUT A LAUNCH IN PROGRESS. A tick marks an entry m_inFlight and then works on it
			// with the lock released (Tick explains why it must). Erasing it now would destroy the
			// object that launch is still writing into.
			ibJobEntry* const entry = it->get();
			m_inFlightCv.wait(lk, [this, entry] { return m_stopped || !entry->m_inFlight; });

			// The vector may have been re-shuffled while we waited; find it again by name.
			it = std::find_if(m_entries.begin(), m_entries.end(),
			                  [&name](const std::unique_ptr<ibJobEntry>& slot) { return slot->m_desc.m_name == name; });
			if (it == m_entries.end())
				break;   // somebody else removed it while we waited — still "found"

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

std::size_t ibJobManager::UnregisterByPrefix(const wxString& prefix)
{
	if (prefix.IsEmpty())
		return 0;   // an empty prefix matches everything, which is never what a caller means

	// Collect the NAMES first, then drop them one by one through the single-name path — which is
	// the one that knows how to wait out a launch in flight. Doing the erase here as well would
	// mean two places that must both get that waiting right.
	std::vector<wxString> names;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (const auto& slot : m_entries)
			if (slot->m_desc.m_name.StartsWith(prefix))
				names.push_back(slot->m_desc.m_name);
	}

	std::size_t removed = 0;
	for (const wxString& name : names)
		if (Unregister(name))
			++removed;

	return removed;
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
			ibJournalInfo(wxT("job"),wxT("job tick failed: %s"), err.GetErrorDescription());
		}
		catch (...) {
			ibJournalInfo(wxT("job"),wxT("job tick failed with an unknown exception"));
		}
	}
}

int ibJobManager::Tick()
{
	const auto now = std::chrono::steady_clock::now();

	// DECIDING IS CHEAP, RUNNING IS NOT — and only the deciding may hold the lock.
	//
	// m_mtx also guards Register, and Register is NOT a bootstrap-only call: the Firebird driver
	// declares its maintenance job from Open(), so EVERY connection taken out of the pool goes
	// through it. Opening a form compiles a module, which loads bytecode, which builds a query
	// builder, which checks out a connection — and there the form met this mutex.
	//
	// Held across the whole loop, the way this used to be, that mutex covered a session teardown
	// (which waits behind the session's own queue, up to seconds) and a session create (which waits
	// on the registry thread). With a job on a short interval it was almost never free, and the
	// application froze on it while the schedule looked perfectly healthy.
	//
	// So: under the lock, decide and mark. Outside it, do the work.
	std::vector<ibJobEntry*>                      toLaunch;
	std::vector<std::shared_ptr<ibSessionHolder>> toRelease;   // released below, off the lock
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_stopped)
			return 0;

		// A FINISHED BACKGROUND RUN GIVES ITS SESSION BACK HERE — from the tick, never from
		// its own worker, for the reason spelled out on m_runSession: Teardown waits behind
		// the session's own queue, and a task of that session dropping the last reference
		// would be waiting behind itself. Holding the run this far also keeps its
		// sys_session row alive past the moment the work ended, which is what makes a
		// two-millisecond background job visible in Active Users at all — the cluster
		// snapshot is taken once a second and would otherwise never catch one.
		//
		// The handle the caller was given (if it kept one) stays valid: only the session
		// goes, and a finished run has nothing left to do with it.
		for (auto it = m_background.begin(); it != m_background.end(); ) {
			const std::shared_ptr<ibBackgroundRun>& run = *it;
			if (!run || run->m_done.load(std::memory_order_acquire)) {
				if (run) {
					std::lock_guard<std::mutex> runLk(run->m_mtx);
					toRelease.push_back(std::make_shared<ibSessionHolder>(std::move(run->m_holder)));
				}
				it = m_background.erase(it);
			}
			else {
				++it;
			}
		}

		// HOW MANY MAY RUN AT ONCE — the cap is about RESOURCES, not about how many jobs a
		// configuration is allowed to declare. Every run holds a session and a session holds a
		// pooled connection, so a hundred and fifty parameterized rows must not start together;
		// they may certainly all be REGISTERED, and each comes due on its own schedule.
		std::size_t running = 0;
		for (const auto& slot : m_entries)
			if (slot->m_inFlight || IsRunning(*slot))
				++running;

		for (auto& slot : m_entries) {
			ibJobEntry& e = *slot;

			// A job the previous tick is still launching outside the lock: its fields are being
			// written by that tick, so nothing here may touch it.
			if (e.m_inFlight)
				continue;

			HarvestFinished(e, toRelease);
			if (IsRunning(e))
				continue;
			if (!IsDue(e, now))
				continue;

			// At the cap: leave it due and take it on a later tick. Not an error and not a skip —
			// nothing about the job changes, it simply waits its turn.
			if (running >= m_maxJobs)
				break;
			++running;

			// Claim it for this tick. Unregister and Stop wait for the flag to clear, so the entry
			// cannot go away underneath the launch below.
			e.m_inFlight = true;
			toLaunch.push_back(&e);
		}
	}

	// The two expensive things, both without the lock. Releasing a holder tears its session down;
	// launching creates one.
	toRelease.clear();

	int started = 0;
	for (ibJobEntry* e : toLaunch) {
		bool ok = false;
		try { ok = Launch(*e); }
		catch (...) { /* a launch failure is this job's problem, not the schedule's */ }

		{
			std::lock_guard<std::mutex> lk(m_mtx);
			e->m_inFlight = false;
		}
		m_inFlightCv.notify_all();
		if (ok)
			++started;
	}
	return started;
}

bool ibJobManager::RunNow(const wxString& name)
{
	// DECIDE UNDER THE LOCK, RUN OUTSIDE IT — the same division Tick makes, and for the same
	// reason: Launch creates a session, and creating one waits on the registry and takes a
	// pooled connection, and taking a connection goes through the Firebird driver's Open(),
	// which declares its maintenance job — which calls Register, which wants THIS mutex. Held
	// across the launch, the manual run either froze the window on the registry or waited on
	// a mutex it was itself holding.
	std::vector<std::shared_ptr<ibSessionHolder>> toRelease;

	ibJobEntry* e = nullptr;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_stopped)
			return false;

		e = Find(name);
		if (e == nullptr)
			return false;

		if (e->m_inFlight)
			return false;   // a tick is launching it right now

		HarvestFinished(*e, toRelease);
		if (IsRunning(*e))
			return false;   // already going — a manual run does not queue behind it

		// CLAIM IT for this call. Unregister and Stop wait for the flag to clear, so the entry
		// cannot be destroyed underneath the launch below — the same contract the tick relies on.
		e->m_inFlight = true;
	}

	// Both expensive things off the lock: releasing a holder tears its session down,
	// launching creates one.
	toRelease.clear();

	bool ok = false;
	try { ok = Launch(*e); }
	catch (...) { /* a launch failure is this job's problem, not the caller's */ }

	{
		std::lock_guard<std::mutex> lk(m_mtx);
		e->m_inFlight = false;
	}
	m_inFlightCv.notify_all();

	return ok;
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
		state.m_key       = KeyOf(e.m_desc);
		state.m_active    = e.m_desc.m_active;
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
			alive.swap(m_background);
		}
		for (auto& run : alive) run->Cancel();
		for (auto& run : alive) run->Wait();
	}

	// The tick thread is joined by now, so no entry can still be m_inFlight — nothing to wait for
	// here. The notify is for anyone parked in Unregister: m_stopped releases them.
	std::vector<std::unique_ptr<ibJobEntry>> doomed;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_stopped)
			return;
		m_stopped = true;
		doomed.swap(m_entries);
	}
	m_inFlightCv.notify_all();

	// Wait outside the lock. Each run owns its own session inside its task, so
	// waiting for the task IS waiting for the session to be let go — there is no
	// holder here to reset.
	for (auto& e : doomed) {
		if (e->m_future.valid()) {
			try { e->m_future.get(); } catch (...) { /* shutdown — nothing left to tell */ }
		}
	}
}











