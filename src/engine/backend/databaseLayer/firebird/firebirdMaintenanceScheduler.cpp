#include "firebirdMaintenanceScheduler.h"
#include "firebirdDatabaseLayer.h"

#include "backend/appData.h"
#include "backend/job/jobManager.h"
#include "backend/session/session.h"

// THE SCHEDULE SAYS WHEN THE WORK IS DUE — that is the whole point of having one. It used to say
// "ask every 60 seconds", with the real cadences (sweep every 6 h, backup/restore weekly) hidden in
// process-local statics inside the driver. Two clocks, and the one that survived a restart was the
// one that decided nothing: every new process read its statics as "never ran here", so a sweep
// fired on each re-login — which is why the job reappeared right after a restructure — while the
// shared clock in sys_job, keyed to the 60-second interval, happily allowed it.
//
// One cadence per job, declared here, is what makes sys_job the answer: it holds the last run per
// JOB NAME across every process on the base, so a restart changes nothing and a peer that already
// did the work is seen. Sweep and backup are separate jobs because they are separate cadences —
// folding them into one meant neither could be expressed.
static constexpr int kSweepEverySeconds  = 6 * 3600;        // cheap; several times a day is fine
static constexpr int kBackupWindowStart  = 2;               // 02:00 local
static constexpr int kBackupWindowEnd    = 5;               // 05:00 local
static constexpr int kBackupEveryDays    = 7;

void ibFirebirdMaintenanceJob::Register()
{
	ibJobManager* const jobs = ibApplicationData::GetJobManager();
	if (jobs == nullptr)
		return;

	// Platform origin on both: no user, therefore no RLS. Maintenance is about the database file,
	// not about anybody's data.
	{
		ibJobDescription sweep;
		sweep.m_name     = wxT("firebird.sweep");
		sweep.m_origin   = ibJobOrigin::Platform;
		sweep.m_body     = &ibFirebirdMaintenanceJob::RunSweep;
		sweep.m_schedule = ibJobScheduleDescription::EverySeconds(kSweepEverySeconds);
		(void)jobs->Register(std::move(sweep));
	}

	{
		// Heavy (~1 s/GB), so it is pinned to the quiet hours by the CALENDAR rather than by an if
		// inside the body: the schedule is what the manager consults before it starts anything, so
		// an out-of-window tick costs nothing at all — no session, no claim, no clock write.
		ibJobDescription backup;
		backup.m_name     = wxT("firebird.backup");
		backup.m_origin   = ibJobOrigin::Platform;
		backup.m_body     = &ibFirebirdMaintenanceJob::RunBackupRestore;
		backup.m_schedule = ibJobScheduleDescription::Nightly(kBackupWindowStart, kBackupWindowEnd);
		backup.m_schedule.m_intervalSeconds = kBackupEveryDays * 24 * 3600;
		(void)jobs->Register(std::move(backup));
	}
}

namespace {
// The Firebird driver behind this run's session, or null when the base is not Firebird (nothing to
// maintain then — every host registers these jobs, only a Firebird one has work).
//
// The CONNECTION comes from the job's own session, borrowed from the pool for the length of the
// call. That is why nothing is cached: the pool guarantees this connection is alive and ours while
// we hold it — the exact guarantee a remembered interface pointer could not give.
ibDatabaseLayerFirebird* DriverOf(ibSession* session, std::shared_ptr<ibDatabaseLayer>& keepAlive)
{
	if (session == nullptr)
		return nullptr;
	keepAlive = session->EnsureConnection();
	return dynamic_cast<ibDatabaseLayerFirebird*>(keepAlive.get());
}
} // namespace

// Both bodies are the same shape: the schedule already decided that this is due, so the body just
// does the pass. No "is it time yet" here — that question now has exactly one home, and it is the
// job's schedule plus the shared clock in sys_job.
//
// The CANCEL TOKEN travels for the same reason the connection does: it is alive for exactly as long
// as this call, and the pool raises it before it starts waiting for us. A Services API pass answers
// to nothing else — it is minutes of polling with no interpreter loop boundary in it, so without
// this the pool's Stop would wait it out. It is also what an administrator's cancel reaches.
bool ibFirebirdMaintenanceJob::RunSweep(ibSession* session)
{
	std::shared_ptr<ibDatabaseLayer> keepAlive;
	if (ibDatabaseLayerFirebird* const fb = DriverOf(session, keepAlive))
		fb->RunSweepNow(session->CancelFlag());
	return false;   // one pass does the whole thing — nothing to continue next tick
}

bool ibFirebirdMaintenanceJob::RunBackupRestore(ibSession* session)
{
	std::shared_ptr<ibDatabaseLayer> keepAlive;
	if (ibDatabaseLayerFirebird* const fb = DriverOf(session, keepAlive))
		fb->RunBackupRestoreNow(session->CancelFlag());
	return false;
}
