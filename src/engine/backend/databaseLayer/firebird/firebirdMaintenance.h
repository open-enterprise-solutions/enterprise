#ifndef __FIREBIRD_MAINTENANCE_H__
#define __FIREBIRD_MAINTENANCE_H__

// Database maintenance operations via the Firebird Services API.
//
// Two primary operations:
//
//   - Backup + Restore cycle (`RunBackupRestoreCycle`): the only
//     way to physically shrink an `.fdb` file. Active OLTP leaves
//     MVCC version chains scattered across pages; sweep frees them
//     for reuse inside the file but never returns space to the OS.
//     gbak -B / gbak -R is the canonical "shrink + defrag" workflow.
//
//   - Sweep (`RunSweep`): forces immediate background sweep instead
//     of waiting for the SweepInterval threshold. Faster sweep =
//     less MVCC bloat between BR cycles.
//
// Implementation goes through `isc_service_attach` →
// `isc_service_start` (action SPB) → `isc_service_query` poll loop
// → `isc_service_detach`. No child process spawn, no extra
// vendored binaries — uses functions already inside fbclient.dll.
//
// Calling these blocks the calling thread; intended to be invoked
// from a background scheduler (caller responsibility) so the UI
// stays responsive. Typical schedule: `RunBackupRestoreCycle`
// weekly during off-hours, `RunSweep` every 6-12 hours.

#include "backend/backend.h"

#include <wx/string.h>

class ibInterfaceFirebird;

class BACKEND_API ibFirebirdMaintenance {
public:
	// Connection info for Services API attachment. Same credentials
	// as a normal `isc_attach_database` — typically SYSDBA/masterkey
	// for embedded / single-instance deployments.
	struct ServiceConnection {
		wxString server;     // empty = service_mgr on local host
		wxString username;   // default SYSDBA
		wxString password;   // default masterkey
	};

	enum class Status {
		Ok,
		ServicesApiUnavailable,   // fbclient.dll missing isc_service_* symbols
		AttachFailed,             // service_mgr attach rejected (auth, etc.)
		StartFailed,              // operation didn't start (bad SPB, etc.)
		QueryFailed,              // mid-operation query/wait failed
		Timeout,                  // operation didn't complete in budget
		RestoreFailed,            // restore returned non-zero — temp file kept for forensics
		IOError                   // local file ops (rename, delete) failed
	};

	// Forced sweep — picks up MVCC dead version chains immediately
	// instead of waiting for FB's internal `SweepInterval` threshold.
	// Synchronous: blocks the calling thread until sweep completes.
	// Typical duration on a 5 GB database with light bloat: 5-30 s.
	static Status RunSweep(
		ibInterfaceFirebird* iface,
		const wxString& databasePath,
		const ServiceConnection& conn);

	// Backup + Restore cycle. Runs gbak -B to a temp `.fbk` file,
	// then gbak -R into a temp `.fdb` file, then atomic-renames the
	// new file over the original. The original is renamed to `.bak`
	// for one-cycle rollback insurance, then deleted on success.
	//
	// **Caller responsibility — STRICT**: no other process (including
	// our own spawned firebird.exe leader) may have the database
	// open during the SWAP step. Windows rename fails with sharing
	// violation; POSIX rename silently re-points the inode and
	// leaves other holders working on the old file (state divergence
	// + data loss). Safe scenarios:
	//   - Single-process embedded mode (Standalone), no other
	//     OES process attached.
	//   - Operator-driven maintenance window where the OES cluster
	//     is fully shut down.
	// NOT safe in leader-mode while followers are connected — the
	// leader's spawned firebird.exe holds the .fdb open. The
	// scheduler must gate BR-cycle to Standalone-only.
	//
	// Synchronous. Typical duration: ~1 s per GB on modern SSD,
	// dominated by gbak's sequential scan + reconstruct passes.
	// File size reduction: 30-60 % depending on prior bloat.
	//
	// Note on UNC paths: the temp .fbk + .new.fdb land in the same
	// directory as the source .fdb — for shared-folder deployments
	// this means a gigabytes-over-SMB roundtrip. Prefer running BR
	// after copying the .fdb to local disk for the maintenance run.
	static Status RunBackupRestoreCycle(
		ibInterfaceFirebird* iface,
		const wxString& databasePath,
		const ServiceConnection& conn);

	// Human-readable error string for a status.
	static wxString StatusToString(Status s);
};

#endif // __FIREBIRD_MAINTENANCE_H__
