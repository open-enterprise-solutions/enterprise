#include "firebirdMaintenanceScheduler.h"
#include "firebirdDatabaseLayer.h"

#include "backend/appData.h"
#include "backend/job/jobManager.h"
#include "backend/session/session.h"

// How often the job asks whether anything is due. Not a workload: a tick with
// nothing due is two timestamp comparisons inside the driver.
static constexpr int kAskEverySeconds = 60;

void ibFirebirdMaintenanceJob::Register()
{
	ibJobManager* const jobs = ibApplicationData::GetJobManager();
	if (jobs == nullptr)
		return;

	ibJobDescription desc;
	desc.m_name = wxT("firebird.maintenance");
	// Platform: no user, therefore no RLS. Maintenance is about the database
	// file, not about anybody's data.
	desc.m_origin   = ibJobOrigin::Platform;
	desc.m_body     = &ibFirebirdMaintenanceJob::Run;
	desc.m_schedule = ibJobSchedule::EverySeconds(kAskEverySeconds);

	(void)jobs->Register(std::move(desc));
}

bool ibFirebirdMaintenanceJob::Run(ibSession* session)
{
	if (session == nullptr)
		return false;

	// The CONNECTION comes from the job's own session, borrowed from the pool for
	// the length of this call. That is why nothing is cached anywhere: the pool
	// guarantees this connection is alive and ours while we hold it — the exact
	// guarantee a remembered interface pointer could not give.
	std::shared_ptr<ibDatabaseLayer> db = session->EnsureConnection();
	ibDatabaseLayerFirebird* const fb = dynamic_cast<ibDatabaseLayerFirebird*>(db.get());
	if (fb == nullptr)
		return false;   // not a Firebird connection — nothing to maintain

	// The work itself belongs to the driver: the interface, the path and the
	// service credentials are already there and valid for as long as this
	// connection is checked out.
	// The CANCEL TOKEN comes from the same session, for the same reason the
	// connection does: it is alive for exactly as long as this call, and the
	// pool raises it before it starts waiting for us. A Services API pass
	// answers to nothing else — it is minutes of polling with no interpreter
	// loop boundary in it, so without this the pool's Stop waits it out.
	fb->RunDueMaintenance(session->CancelFlag());

	// One pass does everything that is due — nothing to continue on the next tick.
	return false;
}
