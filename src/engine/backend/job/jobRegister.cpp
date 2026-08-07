////////////////////////////////////////////////////////////////////////////
//	Description : the job REGISTER — sys_job, the one record every job has
////////////////////////////////////////////////////////////////////////////

#include "jobManager.h"

#include "backend/appData.h"
// sys_job goes through the L2 door: it renders the dialect (upsert above all) so
// nothing here has to ask which driver it is on.
#include "backend/databaseLayer/databaseQueryBuilder.h"
// ibBackendQueryException — the write tells "no connection" from "the base said no" by its Kind.
#include "backend/databaseLayer/databaseLayerException.h"

#include <algorithm>

#include <wx/datetime.h>

// ---------------------------------------------------------------------------
// The SHARED clock — sys_job.
//
// The cross-process claim answers "is somebody running this RIGHT NOW". That is
// a different question from "has it already run recently", and only the second
// one stops N processes on one base from each running a job once per interval.
// Two clients open on a file base, two web servers, a compute server next to a
// desktop — without this they all keep private clocks and the job fires once per
// process. With it, whoever gets there first writes the time and the rest see it.
//
// Best-effort by design: a database that cannot answer must not stop a job from
// running, because the alternative — silently skipping housekeeping because a
// SELECT failed — is worse than running it twice.
// ---------------------------------------------------------------------------

wxDateTime ibJobManager::ReadSharedLastRun(const ibGuid& key)
{
	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.From(job_table)
			.Select({ wxT("lastRun") })
			.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("jobKey")), ibParam(0)))
			.Execute({ ibValue(key.str()) });

		// Columns are 1-based, and a NULL comes back as TYPE_NULL rather than as
		// a zero date — a job row that exists but never ran reads as "no opinion".
		if (rs.Next()) {
			const ibValue last = rs.GetValue(1);
			// Through wxLongLong, the way every other ms-to-wxDateTime site in the tree does
			// it. GetDate() hands back a wxLongLong_t (`long long`), and wxDateTime's
			// constructors take time_t / double / wxLongLong — on LP64 none of those is an
			// exact match for `long long`, so the implicit conversion is ambiguous. MSVC
			// happens to pick one; naming wxLongLong says which, on every platform.
			if (!last.IsNull() && !last.IsEmpty())
				return wxDateTime(wxLongLong(last.GetDate()));
		}
	}
	catch (...) {
		// An unreadable clock is no opinion, not a veto: skipping housekeeping
		// because a SELECT failed is worse than running it twice.
	}
	return wxInvalidDateTime;
}

void ibJobManager::WriteSharedLastRun(const ibGuid& key, const wxString& name, const wxDateTime& when)
{
	try {
		// UPDATE, deliberately — NOT an upsert.
		//
		// The register is the list of what RUNS on this base: a job takes its row when it is
		// switched on and loses it when it is switched off. A run BY HAND is not a change of that
		// state — "Execute" fires a job once whatever its schedule and whatever its switch say, and
		// it must not put a switched-off job back into the register through the back door. So the
		// stamp updates a row that is there and quietly does nothing when there is none.
		//
		// The name rides along because it is the display side of the record and may have been
		// improved since the row was written; the key never moves.
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpdate(job_table,
			{
				{ wxT("jobName"),  ibParam(0) },
				{ wxT("lastRun"),  ibParam(1) },
				{ wxT("computer"), ibParam(2) },
			},
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("jobKey")), ibParam(3))),
			{
				ibValue(name),
				ibValue(when),
				ibValue(appData != nullptr ? appData->GetComputerName() : wxString()),
				ibValue(key.str()),
			});
	}
	catch (...) {
		// A lost write means a peer may repeat the run. Tolerable; throwing here
		// would take the tick down instead.
	}
}

// ---------------------------------------------------------------------------
// The base-side SETTINGS — the schedule and the on/off switch, held in sys_job
// beside the shared clock.
//
// The declaration is the STARTING POINT: the first time a job is seen on a base,
// its metadata schedule seeds the row; from then on the row wins. That is what
// makes "switch this misbehaving job off" an action in the enterprise rather
// than an edit of the configuration on a production base — and it is also the
// only place the engine's OWN jobs can have their cadence changed, since there
// is no configuration behind them to edit.
// ---------------------------------------------------------------------------

ibJobManager::ibJobSettings ibJobManager::ReadSharedSettings(const ibGuid& key)
{
	ibJobSettings settings;
	settings.m_key = key;   // the record is about this job whether or not the base has heard of it

	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.From(job_table)
			.Select({ wxT("lastRun"), wxT("computer"), wxT("active"), wxT("schedule"), wxT("jobName") })
			.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("jobKey")), ibParam(0)))
			.Execute({ ibValue(key.str()) });

		if (rs.Next()) {
			settings.m_found = true;

			const ibValue name = rs.GetValue(5);
			if (!name.IsNull())
				settings.m_name = name.GetString();

			const ibValue last = rs.GetValue(1);
			if (!last.IsNull() && !last.IsEmpty())
				settings.m_lastRun = wxDateTime(wxLongLong(last.GetDate()));

			const ibValue computer = rs.GetValue(2);
			if (!computer.IsNull())
				settings.m_computer = computer.GetString();

			// An absent / NULL active reads as ON. A row written before the column existed must
			// not turn its job off — silence is not a refusal.
			const ibValue active = rs.GetValue(3);
			settings.m_active = active.IsNull() || active.IsEmpty() ? true : active.GetBoolean();

			// The schedule rides as opaque bytes — read through the blob door, not as a value: it
			// is a description, and its format belongs to ibJobScheduleDescriptionMemory alone.
			wxMemoryBuffer blob;
			rs.GetResultBlob(wxT("schedule"), blob);
			if (blob.GetDataLen() > 0)
				ibJobScheduleDescriptionMemory::ReadBuffer(blob.GetData(), blob.GetDataLen(), settings.m_schedule);
		}
	}
	catch (...) {
		// An unreadable settings row is NO OPINION, not a veto — the same rule the shared clock
		// follows. The declaration's own schedule then stands.
		settings.m_found = false;
	}
	return settings;
}

ibJobManager::ibWriteOutcome ibJobManager::WriteSharedSettings(const ibJobSettings& settings)
{
	if (!settings.IsOk())
		return ibWriteOutcome::Refused;   // no key, no record — there is nothing this row would be about

	try {
		wxMemoryBuffer blob;
		ibJobScheduleDescriptionMemory::WriteBuffer(blob, settings.m_schedule);

		// UPSERT for the same reason the clock uses one: the dialects spell it differently, and
		// closing that difference is what this level is for.
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpsert(job_table,
			{
				{ wxT("jobKey"),   ibParam(0) },
				{ wxT("jobName"),  ibParam(1) },
				{ wxT("active"),   ibParam(2) },
				// Opaque bytes, bound as a blob constant — L2 never interprets them, which is
				// exactly right for a description whose format lives one level up.
				{ wxT("schedule"), ibConstBlob(blob.GetData(), blob.GetDataLen()) },
			},
			{ wxT("jobKey") }),
			{
				ibValue(settings.m_key.str()),
				ibValue(settings.m_name),
				ibValue(settings.m_active),
			});

		return ibWriteOutcome::Written;
	}
	catch (const ibBackendQueryException& err) {
		// NO BASE IS NOT A REFUSAL, and this is the whole reason the outcome has three values.
		// L2 already knows the difference — it throws NoConnection when the holder has nothing to
		// give — and that knowledge died in a catch-all one line further down. A declaration made
		// before a database is open (a unit test, the platform's own list at bring-up) has not been
		// refused anything; it has not been asked yet.
		if (err.GetKind() == ibBackendQueryException::Kind::NoConnection)
			return ibWriteOutcome::NoBase;

		// Any other query-tier fault is ours and the row did not land — a refusal.
		return ibWriteOutcome::Refused;
	}
	catch (...) {
		// A lost settings write is a setting that did not stick. Reported rather than raised — the
		// caller decides whether that matters (a seed can be retried on the next registration; a
		// deliberate "switch this off" is something a person should hear about).
		return ibWriteOutcome::Refused;
	}
}

void ibJobManager::ForgetSharedState(const ibGuid& key)
{
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(job_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("jobKey")), ibParam(0))),
			{ ibValue(key.str()) });
	}
	catch (...) {
		// A row left behind is a stale settings record for a job that no longer exists — untidy,
		// never harmful (nothing reads it without a live registration), and certainly not worth
		// taking the caller down for.
	}
}

void ibJobManager::NoteLiveKey(const ibGuid& key)
{
	if (!key.isValid())
		return;

	std::lock_guard<std::mutex> lk(m_mtx);
	if (std::find(m_liveKeys.begin(), m_liveKeys.end(), key) == m_liveKeys.end())
		m_liveKeys.push_back(key);
}

std::size_t ibJobManager::PurgeSharedState(const std::vector<ibGuid>& alsoLive)
{
	// WHO IS ALIVE — everything registered right now, everything NOTED as existing but not
	// scheduled (a switched-off row), plus whatever the caller adds.
	std::vector<wxString> live;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (const auto& slot : m_entries)
			if (KeyOf(slot->m_desc).isValid())
				live.push_back(KeyOf(slot->m_desc).str());
		for (const ibGuid& key : m_liveKeys)
			live.push_back(key.str());
	}
	for (const ibGuid& key : alsoLive)
		if (key.isValid())
			live.push_back(key.str());

	// An EMPTY live set means we know nothing yet — a base opened before any job declared itself.
	// Deleting everything then would wipe the clocks of jobs that are about to register, so the
	// sweep declines rather than guesses.
	if (live.empty())
		return 0;

	try {
		// Read first, delete by key: a NOT IN over a long list is the kind of statement that hits
		// a driver's parameter limit, and the row count here is jobs, not documents.
		std::vector<wxString> orphans;
		{
			ibDatabaseQueryBuilder q;
			ibQueryResult rs = q.From(job_table).Select({ wxT("jobKey") }).Execute();
			while (rs.Next()) {
				const wxString key = rs.GetValue(1).GetString();
				if (std::find(live.begin(), live.end(), key) == live.end())
					orphans.push_back(key);
			}
		}

		for (const wxString& key : orphans) {
			ibDatabaseQueryBuilder q;
			q.Execute(ibDelete(job_table,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("jobKey")), ibParam(0))),
				{ ibValue(key) });
		}

		return orphans.size();
	}
	catch (...) {
		// A sweep that could not run leaves stale rows — untidy, never harmful: nothing reads a
		// record without a live registration to read it for.
		return 0;
	}
}

