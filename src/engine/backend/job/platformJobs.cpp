////////////////////////////////////////////////////////////////////////////
//	Description : the platform's own scheduled jobs — which, and how often
////////////////////////////////////////////////////////////////////////////

#include "platformJobs.h"
#include "jobManager.h"

#include "backend/appData.h"
#include "backend/metaData.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"        // installing the pool the jobs run on
#include "backend/session/workerPoolHeadless.h"

#include "backend/query/schemaSnapshot.h"        // ibSchemaSnapshot + ContributeTables
#include "backend/query/derivedStateBuilder.h"   // ibDerivedState::CollapseAll

#include <wx/log.h>

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
// for it (docs/register-totals-strategy.md § 6a):
//   - figures MOVE by arithmetic (col = col + delta, computed by the DB), so a
//     posting landing mid-fold composes with the adjustment instead of being
//     overwritten by it;
//   - a drained row is deleted only when provably empty;
//   - one transaction per KEY, so a row lock lives three statements;
//   - the frontier is DERIVED, never stored — there is no "how far did we get"
//     state to keep, and therefore none to get out of step;
//   - not running is safe: shards read exactly right at any distribution, so a
//     missed pass costs a slightly wider read and nothing else.
//
// The one hazard is TWO folds on the SAME table at once — both move the same
// figure, one drains a row to zero and deletes it while the other still holds
// its value, and the key's total grows. Serialising is free here: this is ONE
// job walking every table, and the worker pool dispatches a session's tasks
// strictly FIFO under a lease, so a second pass cannot start while the first
// is running. Parallel-by-table would need a session per table and a lock per
// table; it buys little, since the contention is rows rather than CPU.
bool FoldTotals(ibSession* session)
{
	if (session == nullptr || activeMetaData == nullptr)
		return false;

	// The schema comes from the caller, not from the floor — L3-4 is
	// metadata-blind by construction, and for a background job "the active
	// configuration" has no ambient answer (a web process holds many sessions,
	// a cluster many nodes). Here the answer is unambiguous: THIS process's open
	// configuration, snapshotted at the moment the pass starts.
	const ibSchemaSnapshot snapshot = activeMetaData->BuildSchemaSnapshot();
	if (snapshot.Tables().empty())
		return false;   // no configuration open — nothing to fold, not an error

	// The holder names WHOSE connection this runs on. It comes from the job's own
	// session — falling back to "the calling thread's" would silently borrow
	// somebody else's (derivedStateBuilder.h).
	const int folded = ibDerivedState::CollapseAll(snapshot, session->Holder());
	if (folded < 0) {
		wxLogDebug(wxT("totals fold failed"));
		return false;
	}

	// CollapseAll walks every table in one pass, so there is never a remainder to
	// report. If it ever grows a chunked form, THIS is the line that turns into
	// "work remains" and the manager re-queues on the next tick without waiting
	// out the interval — the dosage contract exists for exactly that.
	return false;
}

} // namespace

void ibRegisterPlatformJobs()
{
	ibJobManager* const manager = ibApplicationData::GetJobManager();
	if (manager == nullptr)
		return;   // launcher / pre-bootstrap — no schedule to populate

	// A pool to run them ON, before anything is declared.
	//
	// The registry sizes its own pool from the run mode: headless hosts get one,
	// GUI hosts get none — which was right while a desktop process held exactly
	// one session, and stops being right the moment a scheduled session exists.
	// Without a pool ibSession::Submit falls back to running inline, i.e. on
	// whichever thread asked — here the manager's own tick thread. The tick would
	// then EXECUTE the job instead of dispatching it, and the whole schedule would
	// stall for the duration of every run.
	//
	// Installed here rather than in each host's main, for the same reason the job
	// list is: whoever opens a database needs it, and none of them should have to
	// remember. A host that already has a pool (wes) keeps the one it has.
	//
	// Two workers: enough that one long job cannot stall another, small enough
	// that background work never crowds out interactive sessions. The real ceiling
	// is elsewhere — a session owns ONE connection, so concurrency is bounded by
	// the connection pool (32), not by this number.
	if (ibSessionRegistry* const registry = ibApplicationData::GetSessionRegistry()) {
		if (registry->GetWorkerPool() == nullptr)
			registry->SetWorkerPool(std::make_unique<ibWorkerPoolHeadless>(2));
	}

	ibJobDescription fold;
	fold.m_name = wxT("totals.fold");
	fold.m_body = &FoldTotals;
	// Six hours, and no day window. Folding is cheap next to a rebuild (it reads
	// the totals table, never the movements) and explicitly safe while people
	// work, so confining it to the night would only make the read tax last
	// longer for no gain. Contrast the Firebird backup/restore cycle, which IS
	// heavy and therefore does declare a 02:00-05:00 window — declared inside the
	// scheduler itself, not here.
	fold.m_schedule = ibJobSchedule::EverySeconds(6 * 3600);

	if (!manager->Register(fold)) {
		// Logged inside Register with the reason. Startup continues: housekeeping
		// that could not register is a slightly wider read, not a broken process.
		wxLogDebug(wxT("platform job 'totals.fold' was not registered"));
	}

	// (firebird.maintenance is NOT declared here. It registers itself from
	// ibFirebirdMaintenanceJob::Arm, i.e. when a Standalone Firebird is
	// actually open — the only moment at which that work is possible at all.
	// Declaring it here too would be a second place deciding the same thing; the
	// manager refuses a duplicate name anyway, so the two would silently race to
	// be first.)

	// The schedule starts running here, once there is something to run. Every
	// host that opens a database gets it — desktop client, web server, compute
	// server, daemon — without arranging a timer of its own. A thin client never
	// reaches this code because it never opens a database, which is exactly why
	// it neither schedules nor executes anything.
	manager->Start();
}



