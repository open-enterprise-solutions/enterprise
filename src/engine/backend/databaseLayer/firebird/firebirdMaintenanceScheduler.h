#ifndef __FIREBIRD_MAINTENANCE_SCHEDULER_H__
#define __FIREBIRD_MAINTENANCE_SCHEDULER_H__

// Firebird's housekeeping as a system job. TWO functions, and nothing else:
//
//   Register() — put it on the schedule. Called from firebirdDatabaseLayer::Open,
//                and only in Standalone role: under leader mode our own spawned
//                firebird.exe holds the .fdb over TCP, and the backup/restore
//                cycle's atomic rename would hit a sharing violation on Windows
//                or silently re-point the inode on POSIX.
//
//   Run()      — do whatever is due. This is the job's body; the manager calls it
//                on a worker, on a session of its own, wrapped so a failure
//                becomes the job's status.
//
// Everything else lives inside: when each operation is due, the night window for
// the heavy one, and the last-run clocks. None of it is anyone else's business,
// and none of it belongs in a header.
//
// No thread, no start/stop, no cached connection. The work borrows a live
// connection from the session it is handed — an earlier shape cached the driver
// interface in a process-wide singleton and then had to prove the cache never
// outlived the driver. It did not: EIP=0xdddddddd in WaitForServiceCompletion,
// 2026-05-26 and -05-29. Borrowing makes the question disappear.

#include "backend/backend.h"

class ibSession;

class BACKEND_API ibFirebirdMaintenanceJob {
public:
	// Put maintenance on the schedule. Idempotent — the manager refuses a
	// duplicate name, so re-opening a database does not stack up jobs.
	static void Register();

	// The job's body: run what is due, report whether anything ran.
	//
	// Blocking — the Services API calls return only when the operation finishes.
	// That is exactly why this is a job: it runs on a worker, never on a thread
	// anybody is waiting on.
	static bool Run(ibSession* session);
};

#endif // __FIREBIRD_MAINTENANCE_SCHEDULER_H__
