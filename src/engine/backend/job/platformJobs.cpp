////////////////////////////////////////////////////////////////////////////
//	Description : the platform's own scheduled jobs — which, and how often
////////////////////////////////////////////////////////////////////////////

#include "platformJobs.h"
#include "jobManager.h"

#include "backend/appData.h"
#include "backend/metaData.h"
#include "backend/metadataApplication.h"
#include "backend/session/session.h"

#include "backend/query/provider/database/structure/schemaSnapshot.h"      // ibSchemaSnapshot + ContributeTables
#include "backend/query/provider/database/structure/derivedStateBuilder.h" // ibDerivedState::MaintainTotals
#include "backend/logger/logger.h"                                         // ibLog->Warn — a disagreement is said where people read

namespace {

// ---------------------------------------------------------------------------
// totals.fold — re-pack split totals shards back into one row per key.
//
// A split register trades write contention for read width: one logical key
// occupies N rows forever, and every read of it sums N rows forever. The fold
// removes that tax where it stops earning — a closed period nobody writes to
// any more folds to a single row and stays there, while the current period
// stays spread, which is where the split is doing its job.
//
// Why this is safe to run unattended, and why the manager needs nothing extra
// for it (docs/private/register-totals-strategy.md § 6a):
//   - the DATABASE folds, one slice (a unit and a period) per MERGE — nothing is
//     read into the process, whatever the table holds;
//   - a pass folds the period that closed LAST in each unit, never the history
//     behind it, so its cost does not grow with the base;
//   - one SNAPSHOT transaction per slice that never waits: a row a posting
//     changed or holds is a conflict, the slice rolls back and folds next pass,
//     so the posting never waits longer than one slice;
//   - that period is DERIVED from the unit and the clock, never stored — there
//     is no "how far did we get" state to keep, and none to get out of step;
//   - not running is safe: shards read exactly right at any distribution, so a
//     missed pass costs a slightly wider read and nothing else.
//
// Two folds on one table meet as a conflict too, so they need no lock of their
// own; this is ONE job walking every table anyway, and the worker pool
// dispatches a session's tasks strictly FIFO under a lease.
bool FoldTotals(ibSession* session)
{
	if (session == nullptr || activeMetaData == nullptr)
		return false;

	// The schema comes from the caller, not from the floor — L3-4 is
	// metadata-blind by construction, and for a background job "the active
	// application" has no ambient answer (a web process holds many sessions,
	// a cluster many nodes). Here the answer is unambiguous: THIS process's open
	// application, snapshotted at the moment the pass starts.
	const ibSchemaSnapshot snapshot = activeMetaData->BuildSchemaSnapshot();
	if (snapshot.Tables().empty())
		return false;   // no application open — nothing to fold, not an error

	// The holder names WHOSE connection this runs on. It comes from the job's own
	// session — falling back to "the calling thread's" would silently borrow
	// somebody else's (derivedStateBuilder.h).
	//
	// ⭐⭐ VERIFY, THEN FOLD — MaintainTotals, the call written for this job. The job used to
	// call the fold alone, so the check of the last period against the movements had no
	// caller at all: totals that had drifted from the movements stayed wrong with nothing
	// anywhere saying so. That is not hypothetical — a rebuild that filed every movement as
	// an expense was found by hand on 2026-09-15, and this check is what would have reported
	// it on the next pass. A disagreement is written to the registration journal, where the
	// person responsible reads; the Studio's recompute rebuilds the table.
	const ibDerivedState::ibTotalsMaintenance done = ibDerivedState::MaintainTotals(snapshot, session->Holder());
	if (done.m_failed) {
		ibJournalInfo(ibT("job"),ibT("totals fold failed"));
		return false;
	}
	if (done.m_mismatched > 0)
		ibLog->Warn(ibT("totals"), ibT("verify"), ibString::Format(
			ibT("%d key(s) of the last period in the register totals disagree with the movements - ")
			ibT("recompute the totals in the Studio to rebuild them"), done.m_mismatched));

	// MaintainTotals walks every table in one pass, so there is never a remainder to
	// report. If it ever grows a chunked form, THIS is the line that turns into
	// "work remains" and the manager re-queues on the next tick without waiting
	// out the interval — the dosage contract exists for exactly that.
	return false;
}

} // namespace

void ibRegisterPlatformJobs(ibApplicationInstance* const applicationInstance)
{
	ibJobManager* const manager = ibApplicationInstance::GetJobManager(applicationInstance);
	if (manager == nullptr)
		return;   // launcher / pre-bootstrap — no schedule to populate

	// (The pool the jobs run ON is the process's — ibApplicationHost makes it with the process, for every
	//  host that opens a base. Without one ibSession::Submit would run inline on the manager's own tick
	//  thread, which would then EXECUTE a job instead of dispatching it and stall the whole schedule.)

	ibJobDescription fold;
	fold.m_name = ibT("totals.fold");
	// The engine's own jobs have no metaobject, so their key is a guid MINTED ONCE and written
	// here. A literal rather than a hash of the name: the key must not move when the name is
	// improved, and a constant in the source is the only kind of stability nothing can derive away.
	fold.m_key  = ibGuid(ibT("6d1a8f30-0000-4a00-9e00-000000000001"));
	fold.m_body = &FoldTotals;
	// Six hours, and no day window. Folding is cheap next to a rebuild (it reads
	// the totals table, never the movements) and explicitly safe while people
	// work, so confining it to the night would only make the read tax last
	// longer for no gain. Contrast the Firebird backup/restore cycle, which IS
	// heavy and therefore does declare a 02:00-05:00 window — declared inside the
	// scheduler itself, not here.
	fold.m_schedule = ibJobScheduleDescription::EverySeconds(6 * 3600);

	if (!manager->Register(fold)) {
		// Logged inside Register with the reason. Startup continues: housekeeping
		// that could not register is a slightly wider read, not a broken process.
		ibJournalInfo(ibT("job"),ibT("platform job 'totals.fold' was not registered"));
	}

	// (The Firebird housekeeping jobs — firebird.sweep / firebird.backup — are NOT declared here.
	// They are registered by the startup sequence (ibFirebirdMaintenanceScheduler::Register from
	// appData.cpp), i.e. when a Standalone Firebird is
	// actually open — the only moment at which that work is possible at all.
	// Declaring it here too would be a second place deciding the same thing; the
	// manager refuses a duplicate name anyway, so the two would silently race to
	// be first.)

	// The schedule is DECLARED here and starts running with the base's first session (ibApplicationInstance's
	// OnFirstConnect): every host that opens a database gets it — a file base's program, a server — without
	// arranging a timer of its own, and nobody comes into the base ahead of the one who opened it, a job due at
	// start included.
}



