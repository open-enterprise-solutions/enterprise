#include "firebirdLeaderMode.h"

#include "firebirdCommon.h"
#include "firebirdLease.h"
#include "core/diagnostics/journal.h"   // ibJournal — this TU does not pull in backend_core.h
#include "firebirdLocalServer.h"
#include "core/startProcess.h"   // ibHostName

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace {

// Process-wide singleton state. One orchestrator per process; one
// .fdb at a time. Multiple databases per process in leader-mode
// would require a registry keyed by path — not in scope yet.
struct LeaderModeState {
	std::mutex                       mu;
	std::unique_ptr<ibFirebirdLease> lease;
	ibString                         dbPath;
	ibFirebirdLeaderMode::Role       role        = ibFirebirdLeaderMode::Role::Standalone;
	ibString                         connectUrl;
	uint64_t                         lastGeneration = 0;

	std::atomic<bool>                stopRequested{false};
	std::thread                      heartbeatThread;
};

LeaderModeState& GS() {
	static LeaderModeState s;
	return s;
}

// Forward decl — `PromoteSelfToLeader` reuses the leader-init logic
// from `InitForDatabase` once a follower has won the lease.
void PromoteSelfToLeader(LeaderModeState* st);

// Heartbeat / generation-watch background thread. Runs every 5 s.
//   Leader: RefreshHeartbeat on lease.
//   Follower:
//     1. read lease state;
//     2. if generation has bumped → leader handoff happened, refresh
//        cached URL so driver's reconnect picks up the new leader;
//     3. if heartbeat is stale (>kHeartbeatStaleMs since last update),
//        attempt self-promote — try to acquire the exclusive lock.
//        On success we become the new leader; on failure (someone
//        else already promoted) we re-read the lease.
//
// I/O ops on the lease (ReadCurrentState, RefreshHeartbeat,
// TryAcquireExclusive, WriteSelfAsLeader) and any LocalServer spawn
// inside PromoteSelfToLeader happen OUTSIDE st->mu. SMB roundtrips
// and firebird.exe spawn can take seconds; holding the orchestrator
// mutex across them would block CurrentRole() / CurrentConnectUrl()
// calls from the UI thread.
void HeartbeatLoop(LeaderModeState* st) {
	using namespace std::chrono_literals;
	while (!st->stopRequested.load()) {
		std::this_thread::sleep_for(5s);
		if (st->stopRequested.load()) break;

		// Snapshot role + lease pointer under lock; do I/O outside.
		ibFirebirdLeaderMode::Role role;
		ibFirebirdLease* lease;
		uint64_t lastGeneration;
		ibString dbPath;
		{
			std::lock_guard<std::mutex> g(st->mu);
			if (!st->lease) continue;
			role           = st->role;
			lease          = st->lease.get();
			lastGeneration = st->lastGeneration;
			dbPath         = st->dbPath;
		}

		if (role == ibFirebirdLeaderMode::Role::Leader) {
			if (!lease->RefreshHeartbeat()) {
				ibFb::LogThreadSafe(ibT("ibFirebirdLeaderMode: heartbeat refresh ")
				                    ibT("failed; lease file may be on a ")
				                    ibT("disconnected share"));
			}
		} else if (role == ibFirebirdLeaderMode::Role::Follower) {
			const auto cur = lease->ReadCurrentState();
			if (!cur.valid) continue;

			if (cur.generation != lastGeneration) {
				// Leader handoff — connect URL is stale; refresh
				// cached URL so the driver's reconnect loop picks
				// up the new leader on next CurrentConnectUrl().
				ibFb::LogThreadSafe(ibString::Format(
					ibT("ibFirebirdLeaderMode: leader generation ")
					ibT("bumped %llu -> %llu; updating to %s:%u"),
					(unsigned long long)lastGeneration,
					(unsigned long long)cur.generation,
					cur.leaderHost,
					(unsigned)cur.leaderPort));

				const ibString hostForConnect =
					(cur.leaderHost == ibHostName())
						? ibString(ibT("localhost"))
						: cur.leaderHost;
				const ibString newUrl = (cur.leaderPort > 0)
					? ibString::Format(ibT("inet://%s:%u/%s"),
						hostForConnect, (unsigned)cur.leaderPort, dbPath)
					: ibString();

				std::lock_guard<std::mutex> g(st->mu);
				st->lastGeneration = cur.generation;
				st->connectUrl     = newUrl;
				continue;
			}

			// Two ways to detect "leader is gone":
			//   1. Graceful vacate signal — leader wrote pid = 0
			//      before releasing the lock (instant detection,
			//      no wait).
			//   2. Stale heartbeat — leader crashed / lost network /
			//      OS-killed, no chance to write the sentinel.
			//      Falls back to kHeartbeatStaleMs (20 s) wait.
			if (cur.leaderPid == 0) {
				ibFb::LogThreadSafe(ibT("ibFirebirdLeaderMode: leader vacated ")
				                    ibT("gracefully (pid sentinel); promoting"));
				PromoteSelfToLeader(st);
				continue;
			}

			const uint64_t now = ibFb::NowUnixMs();
			const uint64_t age = (now > cur.heartbeatUnixMs)
				? (now - cur.heartbeatUnixMs) : 0;
			if (age >= ibFirebirdLease::kHeartbeatStaleMs) {
				ibFb::LogThreadSafe(ibString::Format(
					ibT("ibFirebirdLeaderMode: leader heartbeat ")
					ibT("stale (%llu ms old); attempting self-promote"),
					(unsigned long long)age));
				PromoteSelfToLeader(st);
			}
		}
	}
}

// Promote this follower to leader. Caller does NOT hold `st->mu`
// (we do I/O — lease lock, server spawn — that must not block the
// orchestrator mutex). On success st->role flips to Leader and
// st->connectUrl points at our new local server (or local file
// path in degraded mode). On failure, stays Follower and re-reads
// the lease (someone else likely promoted first).
void PromoteSelfToLeader(LeaderModeState* st) {
	// Snapshot what we need under lock so the rest can run lock-free.
	ibFirebirdLease* lease;
	ibString dbPath;
	{
		std::lock_guard<std::mutex> g(st->mu);
		if (!st->lease) return;
		lease  = st->lease.get();
		dbPath = st->dbPath;
	}

	const auto acq = lease->TryAcquireExclusive();

	if (acq == ibFirebirdLease::AcquireResult::AnotherLeaderActive) {
		// Someone beat us. Re-read state and update cached URL.
		const auto cur = lease->ReadCurrentState();
		if (cur.valid && cur.leaderPort > 0) {
			const ibString hostForConnect =
				(cur.leaderHost == ibHostName())
					? ibString(ibT("localhost"))
					: cur.leaderHost;
			const ibString newUrl = ibString::Format(
				ibT("inet://%s:%u/%s"),
				hostForConnect, (unsigned)cur.leaderPort, dbPath);
			{
				std::lock_guard<std::mutex> g(st->mu);
				st->lastGeneration = cur.generation;
				st->connectUrl     = newUrl;
			}
			ibFb::LogThreadSafe(ibString::Format(
				ibT("ibFirebirdLeaderMode: another node promoted ")
				ibT("first (generation %llu); following %s:%u"),
				(unsigned long long)cur.generation,
				cur.leaderHost,
				(unsigned)cur.leaderPort));
		}
		return;
	}

	if (acq == ibFirebirdLease::AcquireResult::IOError) {
		ibFb::LogThreadSafe(ibT("ibFirebirdLeaderMode: I/O error during ")
		                    ibT("self-promote; will retry on next heartbeat"));
		return;
	}

	// AcquiredLock — we're the new leader. Same path as
	// InitForDatabase's leader branch. Both lease-write and server-
	// spawn run lock-free; the resulting role/URL get committed
	// under mu at the end.
	const int serverPort = ibFirebirdLocalServer::EnsureStarted();
	ibString newConnectUrl;
	if (serverPort == 0) {
		ibFb::LogThreadSafe(ibT("ibFirebirdLeaderMode: self-promote succeeded ")
		                    ibT("but local server can't start; running in ")
		                    ibT("degraded embedded-only mode (followers ")
		                    ibT("cannot connect)"));
		lease->WriteSelfAsLeader(ibHostName(), /*port=*/0);
		newConnectUrl = dbPath;
	} else {
		lease->WriteSelfAsLeader(ibHostName(), (uint16_t)serverPort);
		newConnectUrl = ibFirebirdLocalServer::MakeConnectUrl(dbPath);
	}

	// Re-read to capture the generation we just wrote, so future
	// follower-side handoff detection has the right baseline.
	const auto cur = lease->ReadCurrentState();

	{
		std::lock_guard<std::mutex> g(st->mu);
		st->role       = ibFirebirdLeaderMode::Role::Leader;
		st->connectUrl = newConnectUrl;
		if (cur.valid)
			st->lastGeneration = cur.generation;
	}

	ibFb::LogThreadSafe(ibString::Format(
		ibT("ibFirebirdLeaderMode: self-promoted to leader at %s"),
		newConnectUrl));
}

// Best-effort hostname for embedding in the lease as the leader's
// address. We don't try to discover the user-facing network IP;
// that would require iterating interfaces and applying heuristics
// (skip loopback, prefer non-virtual NICs, etc.). For LAN
// scenarios where the share is on a known subnet, the hostname is
// usually resolvable. Production deployments that need a specific
// IP can override via mesh.conf eventually.
ibString DiscoverHostName() {
	return ibHostName();
}

} // namespace

ibFirebirdLeaderMode::Status ibFirebirdLeaderMode::InitForDatabase(const ibString& dbPath) {
	auto& st = GS();

	// First Init in this process — register atexit to ensure Shutdown
	// runs before static destructors. Without it, GS().heartbeatThread's
	// destructor terminates the process if the thread is still joinable
	// (no one explicitly Shutdown()'d). Once-flag ensures only one
	// atexit registration even across many pool-clone Init calls.
	static std::once_flag s_atexitOnce;
	std::call_once(s_atexitOnce, []() {
		std::atexit([](){ ibFirebirdLeaderMode::Shutdown(); });
	});

	std::lock_guard<std::mutex> g(st.mu);

	// Idempotent — same dbPath returns the cached status.
	if (!st.dbPath.IsEmpty() && st.dbPath == dbPath) {
		Status s;
		s.role         = st.role;
		s.connectUrl   = st.connectUrl;
		s.ok           = true;
		return s;
	}

	// Multi-database in one process: not supported. Caller should
	// shut down the existing orchestrator before opening another DB.
	if (!st.dbPath.IsEmpty() && st.dbPath != dbPath) {
		Status s;
		s.errorMessage = ibString::Format(
			ibT("Leader mode already active for %s; cannot init for %s"),
			st.dbPath, dbPath);
		return s;
	}

	// Activation gate is purely path-based: leader-mode kicks in only
	// for UNC / SMB paths (`\\host\share\...` or `//host/share/...`),
	// which inherently imply multi-process coordination over a shared
	// folder. Local paths (`C:\data\db.fdb`, `/var/lib/db.fdb`) are
	// always plain embedded single-process — same as the driver behaved
	// before leader-mode landed.
	//
	// Why path-based, not lease-file-based: a `.lease` file travels
	// with the `.fdb` when the share folder is copied to a local disk
	// for inspection / backup. If lease presence drove activation,
	// every such copy would spuriously spawn firebird.exe + write the
	// lease — surprising and slow. Path inspection is the only signal
	// that survives the copy.
	const bool isUncPath = dbPath.StartsWith(ibT("\\\\"))
	                    || dbPath.StartsWith(ibT("//"));
	if (!isUncPath) {
		// Debugger-only log — surfacing "I'm running standalone" in the
		// user's Output Window adds startup noise without anything they
		// can act on.
		ibFb::LogThreadSafe(ibString::Format(
			ibT("ibFirebirdLeaderMode: local path %s - running ")
			ibT("STANDALONE (single-process embedded)"),
			dbPath));
		Status s;
		s.role = Role::Standalone;
		s.ok   = true;
		return s;
	}

	const ibString leasePath = dbPath + ibT(".lease");

	st.dbPath = dbPath;
	st.lease  = std::make_unique<ibFirebirdLease>(dbPath);

	// Leader-preference heuristic: parse the `\\HOST\share\...` (or
	// `//HOST/share/...`) prefix of dbPath and compare against this
	// machine's hostname. If we ARE the host that physically owns
	// the share, we want to be leader — our writes are local, every
	// other peer's are over SMB. This single rule handles ~80% of
	// real deployments (small office with a designated "main PC"
	// that holds the share; mixed peer-to-peer where the share
	// owner naturally becomes leader). The remaining 20% (admin
	// wants a specific non-host-owner as leader) is the future
	// `enterprise.conf` opt-in — not implemented yet.
	//
	// Mechanism: bias the startup race. If we're NOT preferred,
	// sleep briefly before TryAcquireExclusive so the preferred
	// peer wins the lock when both start within the bias window.
	// Once the race has settled (one process is already leader),
	// the bias is irrelevant — late joiners follow regardless of
	// preference. Live preemption of an existing non-preferred
	// leader is a separate (heavier) mechanism deferred to a
	// future iteration; admin restart of the leader process is the
	// current workaround.
	bool preferLeader = false;
	{
		ibString rest = dbPath.Mid(2);   // strip "\\" / "//"
		const size_t slash = rest.find_first_of(ibT("\\/"));
		const ibString uncHost = slash != ibString::npos ? rest.Left(slash) : rest;
		preferLeader = uncHost.IsSameAs(ibHostName(), /*caseSensitive*/ false);
		ibFb::LogThreadSafe(ibString::Format(
			ibT("ibFirebirdLeaderMode: dbPath host '%s' vs local '%s' - ")
			ibT("preferLeader=%d"),
			uncHost, ibHostName(), (int)preferLeader));
	}
	if (!preferLeader) {
		// 150 ms head start for preferred peers. Short enough that
		// non-preferred startup latency stays imperceptible; long
		// enough that a preferred process woken within the same
		// scheduler quantum wins. Both processes still go through
		// TryAcquireExclusive — the bias just shifts who arrives
		// first to the OS lock.
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
	}

	auto acq = st.lease->TryAcquireExclusive();
	if (acq == ibFirebirdLease::AcquireResult::IOError) {
		st.lease.reset();
		st.dbPath.Clear();
		Status s;
		s.errorMessage = ibT("Cannot open lease file (I/O error). ")
		                 ibT("Check share availability and permissions.");
		return s;
	}

	if (acq == ibFirebirdLease::AcquireResult::AcquiredLock) {
		// Leader role. Spawn local FB server so we have a TCP
		// listener; remote peers connect through it. Embedded FB
		// inside this OES process attaches via localhost too (the
		// shared address space concern from Phase 6).
		const int serverPort = ibFirebirdLocalServer::EnsureStarted();
		if (serverPort == 0) {
			// firebird.exe missing or spawn failed — degrade to
			// embedded-only. Write port=0 into lease so followers
			// know they can't connect; surface a warning.
			ibJournalWarning(ibT("db.firebird"),ibT("ibFirebirdLeaderMode: leader role acquired ")
			             ibT("but local server can't start (firebird.exe ")
			             ibT("missing from _fb/?). Falling back to embedded ")
			             ibT("attach; remote followers will fail to connect."));
			st.lease->WriteSelfAsLeader(DiscoverHostName(), /*port=*/0);
			st.role = Role::Leader;
			st.connectUrl = dbPath;  // local file path — embedded attach
		} else {
			st.lease->WriteSelfAsLeader(DiscoverHostName(),
			                            (uint16_t)serverPort);
			st.role = Role::Leader;
			st.connectUrl = ibFirebirdLocalServer::MakeConnectUrl(dbPath);
		}
		// Initialise lastGeneration to the just-written value so the
		// heartbeat thread's first tick doesn't spuriously detect a
		// "handoff" 0 → 1.
		{
			const auto cur = st.lease->ReadCurrentState();
			if (cur.valid)
				st.lastGeneration = cur.generation;
		}
	} else {
		// AnotherLeaderActive — read current state, become follower.
		auto cur = st.lease->ReadCurrentState();
		if (!cur.valid) {
			// Lock held but file contents bogus (zero-sized fresh-
			// create state, partial write, etc.). Wait a moment and
			// retry once — the actual leader may be in the middle of
			// initial write. Sleep under mu briefly: 500 ms is short
			// enough that blocking CurrentRole() / CurrentConnectUrl()
			// callers during init is acceptable; an unlock + relock
			// would race with concurrent shutdown.
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			cur = st.lease->ReadCurrentState();
		}
		if (!cur.valid || cur.leaderPort == 0) {
			st.lease.reset();
			st.dbPath.Clear();
			Status s;
			s.errorMessage = cur.valid
				? ibT("Leader holds the lease but reports no TCP port - ")
				  ibT("leader is in degraded embedded-only mode and cannot ")
				  ibT("accept follower connections.")
				: ibT("Lease file unreadable or empty after retry.");
			return s;
		}
		st.role            = Role::Follower;
		st.lastGeneration  = cur.generation;
		// If the leader is on the same machine as us, prefer
		// `localhost` over the hostname — avoids name-resolution
		// failures (DNS / WSD / firewall) that bite when a process
		// tries to connect to its own machine by hostname. The
		// in-process leader path already uses `localhost`; this
		// keeps the follower-on-same-host path symmetric.
		const ibString hostForConnect =
			(cur.leaderHost == ibHostName())
				? ibString(ibT("localhost"))
				: cur.leaderHost;
		st.connectUrl      = ibString::Format(
			ibT("inet://%s:%u/%s"),
			hostForConnect,
			(unsigned)cur.leaderPort,
			dbPath);
	}

	// Start the heartbeat thread for either role.
	st.stopRequested.store(false);
	st.heartbeatThread = std::thread(HeartbeatLoop, &st);

	const ibString roleStr =
		(st.role == ibFirebirdLeaderMode::Role::Leader)   ? ibT("LEADER")   :
		(st.role == ibFirebirdLeaderMode::Role::Follower) ? ibT("FOLLOWER") :
		                                                    ibT("STANDALONE");
	// Debugger-only — startup banner for postmortem; not user-actionable.
	ibFb::LogThreadSafe(ibString::Format(
		ibT("ibFirebirdLeaderMode: role=%s connect=%s ")
		ibT("(lease=%s, heartbeat thread started)"),
		roleStr, st.connectUrl, leasePath));

	Status s;
	s.role       = st.role;
	s.connectUrl = st.connectUrl;
	s.ok         = true;
	return s;
}

void ibFirebirdLeaderMode::Shutdown() {
	auto& st = GS();

	// Signal stop, then move thread handle out under the mutex so a
	// concurrent InitForDatabase can't construct a new heartbeat
	// thread while we're probing the old one.
	st.stopRequested.store(true);

	std::thread threadToJoin;
	{
		std::lock_guard<std::mutex> g(st.mu);
		threadToJoin = std::move(st.heartbeatThread);
	}

	// Join outside the mutex — the thread itself acquires st.mu in
	// its loop, so holding mu while joining would deadlock.
	// Self-stop guard: if Shutdown() is somehow called from inside
	// the heartbeat thread itself, joining ourselves would deadlock.
	if (threadToJoin.joinable()) {
		if (threadToJoin.get_id() == std::this_thread::get_id()) {
			threadToJoin.detach();
		} else {
			threadToJoin.join();
		}
	}

	std::lock_guard<std::mutex> g(st.mu);
	if (st.lease) {
		// If we were leader, write the "vacating" sentinel
		// (leaderPid = 0) before releasing the byte-range lock.
		// Followers see the sentinel on their next 5 s tick and
		// self-promote immediately, skipping the 20 s staleness
		// wait. Best-effort: failure to write just means slower
		// (timeout-driven) handoff.
		if (st.role == Role::Leader)
			st.lease->WriteVacating();
		st.lease->Release();
		st.lease.reset();
	}
	st.dbPath.Clear();
	st.connectUrl.Clear();
	st.role = Role::Standalone;
	st.lastGeneration = 0;
}

ibFirebirdLeaderMode::Role ibFirebirdLeaderMode::CurrentRole() {
	auto& st = GS();
	std::lock_guard<std::mutex> g(st.mu);
	return st.role;
}

ibString ibFirebirdLeaderMode::CurrentConnectUrl() {
	auto& st = GS();
	std::lock_guard<std::mutex> g(st.mu);
	return st.connectUrl;
}
