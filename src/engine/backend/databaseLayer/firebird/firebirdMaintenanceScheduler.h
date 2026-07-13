#ifndef __FIREBIRD_MAINTENANCE_SCHEDULER_H__
#define __FIREBIRD_MAINTENANCE_SCHEDULER_H__

// Background scheduler that periodically invokes
// `ibFirebirdMaintenance::RunSweep` and `RunBackupRestoreCycle`. The
// maintenance ops themselves are synchronous (Services API calls
// block until completion); this class owns the cadence + the
// background thread that calls them so the UI stays responsive.
//
// Schedule policy (defaults, see `kXxx` constants below):
//   - Sweep:          every 6 hours
//   - Backup/Restore: every 7 days, only during the maintenance
//                     window (02:00–05:00 local time by default).
//                     Outside the window the next tick re-checks.
//
// State is per-process and per-dbPath. Calling `Start` a second
// time with the same path is idempotent; with a different path,
// the previous scheduler is stopped first.
//
// Lifecycle wiring:
//   - Started by `firebirdDatabaseLayer::Open` ONLY when the
//     leader-mode role is Standalone. Leader-mode role would mean
//     our own spawned firebird.exe holds the .fdb open via TCP —
//     BR-cycle's atomic-rename SWAP step would fail with sharing
//     violation on Windows, or silently re-point the inode on
//     POSIX leaving the running server on a stale file. Followers
//     don't touch the shared DB at all. Standalone single-process
//     embedded is the only safe scheduler scope.
//   - Stopped deterministically in ibApplicationData teardown (step 0
//     of ~ibApplicationData). The atexit hook registered in Start()
//     remains as an idempotent backstop. Per-driver Close does NOT Stop —
//     pool checkouts/checkins would constantly kill the scheduler.
//   - The FB interface is held by SHARED ownership (std::shared_ptr), so
//     the worker never dangles: a pooled donor connection can be reaped /
//     destroyed while the scheduler keeps the fbclient function table
//     alive. Correctness no longer depends on teardown ORDERING — the
//     interface simply outlives every caller that might touch it.
//
// First-cut policy keeps clocks in memory — a process restart
// resets the "last run" timestamps. Persistence to a
// `<dbPath>.maint` sidecar is the next refinement.

#include "backend/backend.h"
#include "backend/databaseLayer/firebird/firebirdMaintenance.h"

#include <wx/string.h>

#include <memory>   // std::shared_ptr — the scheduler co-owns the FB interface

class ibInterfaceFirebird;

class BACKEND_API ibFirebirdMaintenanceScheduler {
public:
	// Default cadence — tuned for the FB driver's sweet-spot (3-30
	// users, 100 GB cap). Sweep is cheap enough to run several times
	// a day; BR cycle is heavy (~1 s/GB) and should hit off-hours.
	static constexpr int kSweepIntervalSeconds        = 6 * 3600;          // 6 h
	static constexpr int kBackupRestoreIntervalSeconds = 7 * 24 * 3600;    // 7 days
	static constexpr int kMaintenanceWindowStartHour  = 2;                 // 02:00 local
	static constexpr int kMaintenanceWindowEndHour    = 5;                 // 05:00 local

	// Tick granularity. The scheduler loop wakes every `kTickSeconds`
	// and re-evaluates whether sweep / BR is due. Short tick keeps
	// shutdown responsive (Stop() joins within ~kTickSeconds);
	// long tick saves CPU. 60 s is a fair compromise.
	static constexpr int kTickSeconds                 = 60;

	// Start the scheduler for `dbPath` against the given FB interface
	// + service-credentials. Idempotent — second call with same path
	// is a no-op; different path stops the prior scheduler first.
	// Safe to call from `firebirdDatabaseLayer::Open` regardless of
	// whether the scheduler already runs.
	//
	// `iface` is taken by SHARED ownership: the scheduler co-owns the
	// fbclient interface, so the worker's calls stay valid even after the
	// pooled connection that donated it is reaped / destroyed. This — not
	// teardown ordering — is what prevents the use-after-free.
	static void Start(
		std::shared_ptr<ibInterfaceFirebird> iface,
		const wxString& dbPath,
		const ibFirebirdMaintenance::ServiceConnection& conn);

	// Stop the scheduler (if any) and join the background thread, then
	// release the scheduler's interface reference. Safe to call multiple
	// times. Called from `firebirdDatabaseLayer::Close` and at process exit.
	static void Stop();

	// Currently-scheduled dbPath, or empty when not running.
	// For diagnostics / "Maintenance status" UI panels.
	static wxString CurrentDbPath();

	// Wall-clock UNIX ms timestamps of the last successful operation.
	// 0 when the operation has not run yet (since process start).
	static uint64_t LastSweepUnixMs();
	static uint64_t LastBackupRestoreUnixMs();
};

#endif // __FIREBIRD_MAINTENANCE_SCHEDULER_H__
