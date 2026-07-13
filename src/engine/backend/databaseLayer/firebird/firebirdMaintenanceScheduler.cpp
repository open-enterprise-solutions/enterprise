#include "firebirdMaintenanceScheduler.h"
#include "firebirdCommon.h"

#include <wx/log.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace {

// Process-wide singleton state. One scheduler per process — matches
// the leader-mode constraint (one .fdb in leader-mode per process).
struct SchedulerState {
	std::mutex                       mu;
	wxString                         dbPath;
	std::shared_ptr<ibInterfaceFirebird> iface;   // shared: outlives the pooled donor connection (no dangling worker)
	ibFirebirdMaintenance::ServiceConnection conn{};

	std::atomic<bool>                stopRequested{false};
	std::atomic<bool>                running{false};
	std::thread                      worker;

	// Worker-exit signal — set true by the worker right before its
	// thread function returns. Replaces the std::async-based timed
	// join in Stop() which was unsafe at DLL-detach time (TPP teardown
	// kills CreateThreadpoolWork, which std::async ultimately calls;
	// atexit handler in DllMain detach hit INVALID_PARAMETER c000000d).
	std::mutex                       doneMu;
	std::condition_variable          doneCv;
	bool                             workerDone = false;

	// Wall-clock UNIX ms timestamps for "last successful". 0 means
	// "never run since process start". Persistence to disk would
	// elevate this to "never run on this DB" instead.
	std::atomic<uint64_t>            lastSweepMs{0};
	std::atomic<uint64_t>            lastBackupRestoreMs{0};
};

SchedulerState& GS() {
	static SchedulerState s;
	return s;
}

using ibFb::NowUnixMs;

// True iff the current local-time hour-of-day falls within the
// configured maintenance window. The window is "hour-of-day in
// [start, end)" — e.g. 02:00–05:00 means hour ∈ {2, 3, 4}. Wrapping
// windows (e.g. 22:00–02:00 next day) are NOT supported here; this
// is fine for the default off-hours slot, which never crosses
// midnight.
bool IsInMaintenanceWindow() {
	const std::time_t now = std::time(nullptr);
	std::tm local{};
#if defined(_MSC_VER)
	localtime_s(&local, &now);
#else
	std::tm* p = std::localtime(&now);
	if (p) local = *p;
#endif
	return local.tm_hour >= ibFirebirdMaintenanceScheduler::kMaintenanceWindowStartHour
	    && local.tm_hour <  ibFirebirdMaintenanceScheduler::kMaintenanceWindowEndHour;
}

// Compute "is X overdue?" against a last-run timestamp.
// `lastMs == 0` → never run, treat as overdue immediately. The
// "never run" case starts BR-cycle immediately on process boot,
// which is NOT what we want for BR (the user would see a multi-
// minute pause right at app launch). Caller for BR layers the
// maintenance-window check on top — BR only runs in the window,
// so first-boot-during-the-day still defers until the next 02:00.
// Sweep, being cheap, runs immediately when "never run".
bool IsOverdue(uint64_t lastMs, int intervalSeconds) {
	if (lastMs == 0) return true;
	const uint64_t nowMs = NowUnixMs();
	const uint64_t ageMs = (nowMs > lastMs) ? (nowMs - lastMs) : 0;
	return ageMs >= static_cast<uint64_t>(intervalSeconds) * 1000ULL;
}

void RunMaintenanceCycle(SchedulerState* st) {
	// Snapshot params under lock — the loop runs without holding
	// the mutex so Stop() can flip stopRequested without blocking.
	wxString dbPath;
	std::shared_ptr<ibInterfaceFirebird> iface;   // a local strong ref pins the interface alive for the WHOLE cycle
	ibFirebirdMaintenance::ServiceConnection conn{};
	{
		std::lock_guard<std::mutex> g(st->mu);
		dbPath = st->dbPath;
		iface  = st->iface;
		conn   = st->conn;
	}
	if (dbPath.IsEmpty() || !iface) return;

	// Sweep — cheap, can run any time of day.
	if (IsOverdue(st->lastSweepMs.load(),
	              ibFirebirdMaintenanceScheduler::kSweepIntervalSeconds)) {
		ibFb::LogThreadSafe(wxString::Format(
			wxT("ibFirebirdMaintenanceScheduler: running sweep on %s"),
			dbPath));
		const auto status = ibFirebirdMaintenance::RunSweep(
			iface.get(), dbPath, conn, &st->stopRequested);
		if (status == ibFirebirdMaintenance::Status::Ok) {
			st->lastSweepMs.store(NowUnixMs());
		} else {
			ibFb::LogThreadSafe(wxString::Format(
				wxT("ibFirebirdMaintenanceScheduler: sweep failed: %s"),
				ibFirebirdMaintenance::StatusToString(status)));
		}
	}

	// Backup/Restore — heavy, requires no other connections + off-
	// hours window. The "no other connections" precondition is the
	// caller's responsibility per the API contract; we don't
	// enforce it here. In leader-mode the leader would quiesce
	// followers before invoking; in standalone single-process mode
	// the user's session is the only consumer.
	if (IsOverdue(st->lastBackupRestoreMs.load(),
	              ibFirebirdMaintenanceScheduler::kBackupRestoreIntervalSeconds)
	 && IsInMaintenanceWindow()) {
		ibFb::LogThreadSafe(wxString::Format(
			wxT("ibFirebirdMaintenanceScheduler: running BR cycle on %s"),
			dbPath));
		const auto status = ibFirebirdMaintenance::RunBackupRestoreCycle(
			iface.get(), dbPath, conn, &st->stopRequested);
		if (status == ibFirebirdMaintenance::Status::Ok) {
			st->lastBackupRestoreMs.store(NowUnixMs());
		} else {
			ibFb::LogThreadSafe(wxString::Format(
				wxT("ibFirebirdMaintenanceScheduler: BR cycle failed: %s"),
				ibFirebirdMaintenance::StatusToString(status)));
		}
	}
}

void WorkerLoop(SchedulerState* st) {
	using namespace std::chrono;
	// Short sleep slices so Stop() doesn't wait up to kTickSeconds
	// for the worker to notice — break each tick into 1 s slices
	// and re-check the stop flag.
	const int sliceSeconds = 1;
	while (!st->stopRequested.load()) {
		for (int i = 0; i < ibFirebirdMaintenanceScheduler::kTickSeconds; ++i) {
			if (st->stopRequested.load()) break;
			std::this_thread::sleep_for(seconds(sliceSeconds));
		}
		if (st->stopRequested.load()) break;

		try {
			RunMaintenanceCycle(st);
		} catch (...) {
			// Don't let an exception kill the scheduler thread.
			// Surface and continue — the next tick will retry.
			ibFb::LogThreadSafe(wxT("ibFirebirdMaintenanceScheduler: tick ")
			                    wxT("threw; continuing"));
		}
	}

	// Signal Stop()'s timed wait that we're done. Has to happen
	// AFTER the loop exits — Stop polls workerDone via doneCv.
	{
		std::lock_guard<std::mutex> g(st->doneMu);
		st->workerDone = true;
	}
	st->doneCv.notify_all();
}

} // namespace

void ibFirebirdMaintenanceScheduler::Start(
	std::shared_ptr<ibInterfaceFirebird> iface,
	const wxString& dbPath,
	const ibFirebirdMaintenance::ServiceConnection& conn)
{
	if (!iface || dbPath.IsEmpty()) return;

	auto& st = GS();

	// First-ever Start in this process — register atexit so we Stop
	// once at clean shutdown. Driver instances live in a pool and
	// Open/Close repeatedly; relying on per-instance Close to Stop
	// would kill the scheduler the first time any pool slot recycles.
	// Once-flag ensures we register only one atexit handler even
	// across many Start invocations.
	static std::once_flag s_atexitOnce;
	std::call_once(s_atexitOnce, []() {
		std::atexit([](){ ibFirebirdMaintenanceScheduler::Stop(); });
	});

	// Already running for this path = idempotent no-op.
	{
		std::lock_guard<std::mutex> g(st.mu);
		if (st.running.load() && st.dbPath == dbPath) {
			// Refresh the credentials / interface pointer in case the
			// driver re-opened with new auth; same path keeps timestamps.
			st.iface = iface;
			st.conn  = conn;
			return;
		}
	}

	// Different path or not running → ensure clean state.
	Stop();

	// Spawn worker INSIDE the mutex + before flipping `running` to
	// true. Otherwise a concurrent Stop() could see running=true,
	// try to join the not-yet-constructed `worker`, succeed with a
	// no-op join (joinable() returns false), clear running, then
	// our std::thread construction below proceeds → orphaned
	// thread + state inconsistency.
	{
		std::lock_guard<std::mutex> g(st.mu);
		st.dbPath = dbPath;
		st.iface  = iface;
		st.conn   = conn;
		st.stopRequested.store(false);
		{
			std::lock_guard<std::mutex> dg(st.doneMu);
			st.workerDone = false;
		}
		st.worker = std::thread(WorkerLoop, &st);
		st.running.store(true);
	}

	// Debugger-only startup banner — see firebirdLeaderMode for the
	// same "no Output Window noise" rationale.
	ibFb::LogThreadSafe(wxString::Format(
		wxT("ibFirebirdMaintenanceScheduler: started for %s ")
		wxT("(sweep every %d s, BR every %d s, window %02d:00-%02d:00)"),
		dbPath, kSweepIntervalSeconds, kBackupRestoreIntervalSeconds,
		kMaintenanceWindowStartHour, kMaintenanceWindowEndHour));
}

void ibFirebirdMaintenanceScheduler::Stop()
{
	auto& st = GS();

	if (!st.running.load()) return;

	st.stopRequested.store(true);

	// Self-stop guard — if the worker thread itself called Stop()
	// (defensive: not a current code path, but cheap to protect),
	// joining ourselves would deadlock. Detach instead.
	if (st.worker.joinable()) {
		if (st.worker.get_id() == std::this_thread::get_id()) {
			st.worker.detach();
		} else {
			// Full join — NO detach fallback.
			//
			// History: previous version waited 5 s for the worker and
			// then detach()ed if it hadn't exited. That left the
			// detached thread alive while main proceeded to tear down
			// the FB driver pool; the detached worker kept calling
			// iface->GetIscServiceQuery() on freed memory →
			// EIP=0xdddddddd in WaitForServiceCompletion (see crash
			// dump 2026-05-26).
			//
			// stopRequested now propagates as a cancelToken into
			// RunSweep / RunBackupRestoreCycle / WaitForServiceCompletion;
			// the worker checks it on every iteration of the service-
			// query poll loop, so it exits in <=200 ms (one poll tick)
			// no matter where in maintenance it sits. The grace wait
			// below is purely a "log if anomalous" instrument — we
			// still join after, never detach.
			constexpr int kAnomalyLogSeconds = 30;
			using namespace std::chrono;
			std::unique_lock<std::mutex> lk(st.doneMu);
			const bool done = st.doneCv.wait_for(lk, seconds(kAnomalyLogSeconds),
				[&st]{ return st.workerDone; });
			lk.unlock();
			if (!done) {
				ibFb::LogThreadSafe(wxString::Format(
					wxT("ibFirebirdMaintenanceScheduler: worker did not ")
					wxT("acknowledge cancel within %d s — still joining ")
					wxT("(cancel token may not be plumbed through some ")
					wxT("blocking call)"),
					kAnomalyLogSeconds));
			}
			if (st.worker.joinable())
				st.worker.join();
		}
	}

	std::lock_guard<std::mutex> g(st.mu);
	st.running.store(false);
	st.dbPath.Clear();
	st.iface.reset();   // release the scheduler's shared ref — frees the interface iff the driver already released
	st.conn  = ibFirebirdMaintenance::ServiceConnection{};
}

wxString ibFirebirdMaintenanceScheduler::CurrentDbPath()
{
	auto& st = GS();
	std::lock_guard<std::mutex> g(st.mu);
	return st.dbPath;
}

uint64_t ibFirebirdMaintenanceScheduler::LastSweepUnixMs()
{
	return GS().lastSweepMs.load();
}

uint64_t ibFirebirdMaintenanceScheduler::LastBackupRestoreUnixMs()
{
	return GS().lastBackupRestoreMs.load();
}
