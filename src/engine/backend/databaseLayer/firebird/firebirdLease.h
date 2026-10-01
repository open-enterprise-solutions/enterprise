#ifndef __FIREBIRD_LEASE_H__
#define __FIREBIRD_LEASE_H__

// Leadership lease — sidecar file with OS byte-range lock for
// distributed leader election on a file share.
//
// Use case. A small office (3-30 users) puts `office.fdb` on a
// shared LAN folder. Direct multi-client access to `.fdb` over SMB
// is a known antipattern — Firebird can't coordinate cache, OAT,
// page locks across clients. The lease mechanism puts ONE OES
// process in charge of the file (opens `.fdb` locally on its
// machine via embedded FB) and lets the rest connect to it via
// TCP. Only the lock sidecar lives on the share; all FB data I/O
// stays local to the leader.
//
// Why this works while raw shared-fdb doesn't:
//   - The byte-range lock on `.lease` is the only thing that
//     crosses SMB. SMB supports advisory byte-range locks across
//     clients (same primitive used by Windows file-mode RDBMS
//     coordination, just we apply it once for leadership rather
//     than per-page).
//   - Only the leader's `fbclient.dll` touches the `.fdb` file.
//     From FB's perspective the database is opened by a single
//     client (the leader's OES process); no multi-client cache
//     coherence issues to resolve.
//   - On leader crash, SMB releases the lock automatically (~30 s
//     typical), and the next claimant takes over. Followers see
//     the generation bump and reconnect.
//
// Sidecar file format (binary, fixed layout — no parser surprises):
//
//   offset  size  field
//   0       4     magic = 'O','E','S','L'
//   4       4     version (currently 1)
//   8       8     generation — bumped on every handoff (uint64)
//   16      8     heartbeat_unix_ms — refreshed every 5 s
//   24      4     leader_pid
//   28      2     leader_port  (TCP port the leader's FB listener uses)
//   30      64    leader_host  (ASCII hostname, zero-padded)
//   94      4     (reserved — flags / future)
//   ----   ----
//   98 bytes total
//
// Field positions confirmed by the in-file static_assert. Struct
// layout is packed but only int + char fields, no platform
// divergence.
//
// Sentinel-byte lock at offset 200 (well past the content range) is
// the leader-election primitive — see kLockSentinelOffset below.

#include "backend/backend.h"

#include <wx/string.h>

#include <cstdint>

#ifdef __WXMSW__
#include <windows.h>
#endif

class BACKEND_API ibFirebirdLease {
public:
	// Outcome of an acquisition attempt — drives the role decision
	// in the leader-election driver: AcquiredLock → become leader,
	// AnotherLeaderActive → become follower, error → bail.
	enum class AcquireResult {
		AcquiredLock,         // we won the lock; we're the leader
		AnotherLeaderActive,  // someone else holds the lock
		IOError               // file can't be opened/read/locked
	};

	// Snapshot of the current lease file contents — read by
	// followers to discover where to connect, by all sides to
	// detect handoff via generation change.
	struct State {
		uint32_t version          = 0;
		uint64_t generation       = 0;
		uint64_t heartbeatUnixMs  = 0;
		uint32_t leaderPid        = 0;
		uint16_t leaderPort       = 0;
		wxString leaderHost;
		bool     valid            = false;  // false if file unreadable / wrong magic
	};

	// `dbPath` is the `.fdb` path; the lease file lives next to it
	// at `<dbPath>.lease`.
	explicit ibFirebirdLease(const wxString& dbPath);
	~ibFirebirdLease();

	ibFirebirdLease(const ibFirebirdLease&) = delete;
	ibFirebirdLease& operator=(const ibFirebirdLease&) = delete;

	// Open the lease file (creates it if missing) and try to acquire
	// the OS-level exclusive byte-range lock. Non-blocking — fails
	// immediately if another holder. On success, this object holds
	// the lock until `Release` / dtor.
	AcquireResult TryAcquireExclusive();

	// Write our identity into the lease file as the current leader.
	// Bumps the generation counter so existing followers detect the
	// change. Must hold the lock (caller has just succeeded in
	// `TryAcquireExclusive`). Returns true on write success.
	bool WriteSelfAsLeader(const wxString& host, uint16_t port);

	// Update the heartbeat timestamp in place. Call periodically
	// (suggested every 5 s) so followers can detect stale leaders
	// when SMB lock release is slow.
	bool RefreshHeartbeat();

	// Graceful "I'm leaving" signal — writes leaderPid = 0 (sentinel,
	// real OS pids are never 0) while still holding the lock, so
	// followers' next heartbeat tick sees the sentinel and can
	// self-promote immediately without waiting out the staleness
	// timeout. Called from Shutdown() before the byte-range lock
	// release. Must hold the lock.
	bool WriteVacating();

	// Read current state without acquiring the lock. Used by
	// followers to learn where the leader is and watch for
	// generation changes.
	State ReadCurrentState();

	// Drop the lock + close the file. Called explicitly on
	// graceful shutdown; dtor is a backstop.
	void Release();

	// True iff this instance currently holds the lock.
	bool HoldsLock() const { return m_holdsLock; }

	// Heartbeat staleness threshold — a leader that hasn't updated
	// the heartbeat in this long is considered dead even if the
	// OS lock hasn't released yet (SMB sometimes lags on dirty
	// disconnect). Followers force-acquire on stale heartbeat.
	// 20 s = 4 missed heartbeat ticks at the 5 s heartbeat cadence;
	// short enough for users to notice ~30 s recovery (20 s stale
	// detection + spawn time), wide enough not to false-positive
	// on a transient SMB write blip. Graceful leader shutdown is
	// signalled via `leaderPid == 0` in the lease (instant promote,
	// no wait — see `WriteVacating`).
	static constexpr uint64_t kHeartbeatStaleMs = 20 * 1000;

	// Sentinel-byte offset for the byte-range lock. Placed past the
	// content range (98 bytes) so follower ReadFile calls aren't
	// blocked by Windows' ERROR_LOCK_VIOLATION semantics on the
	// locked region. Any value > 98 works; 200 chosen for clear
	// separation. Single source of truth — TryAcquireExclusive +
	// Release reference this directly.
	static constexpr unsigned long kLockSentinelOffset = 200;

private:
	wxString m_dbPath;
	wxString m_leasePath;
	bool     m_holdsLock = false;

#ifdef __WXMSW__
	HANDLE   m_hFile = INVALID_HANDLE_VALUE;
#else
	int      m_fd = -1;
#endif
};

#endif // __FIREBIRD_LEASE_H__
