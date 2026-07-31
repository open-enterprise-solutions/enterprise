#include "appData.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — DDL/DML + TableExists/GetColumns/IsOpen + typed row reads (no raw L1)
#include "backend/databaseLayer/connectionPool.h"

#include "backend/backend_exception.h"

#include "backend/session/sessionRegistry.h"


///////////////////////////////////////////////////////////////////////////////
//								ibApplicationData
///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::TableAlreadyCreated()
{
	ibDatabaseQueryBuilder q;
	return q.TableExists(user_table) &&
		q.TableExists(session_table);
}

///////////////////////////////////////////////////////////////////////////////

void ibApplicationData::CreateTableUser()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(user_table)) {
		// One CreateTable for every driver — the dialect TYPE-MAP renders binaryData's BLOB as
		// BYTEA on PostgreSQL and BLOB everywhere else, so the old per-driver fork is gone.
		q.Execute(ibCreateTable(user_table, {
			{ wxT("guid"),       ibTypeString(36),  /*notNull*/false, /*pk*/true,  wxEmptyString },
			{ wxT("name"),       ibTypeString(64),  /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("fullName"),   ibTypeString(128), /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("changed"),    ibTypeDate(),      /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("dataSize"),   ibTypeInteger(),   /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("binaryData"), ibTypeBlob(),      /*notNull*/true,  /*pk*/false, wxEmptyString },
		}));
		// The index rides with the just-created table, so the old "if not exists" was redundant
		// (and let Firebird choke on that syntax) — a plain CREATE INDEX is correct here.
		q.Execute(ibCreateIndex(user_table, wxT("user_index"), { wxT("guid"), wxT("name") }));
	}
}

void ibApplicationData::CreateTableSession()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(session_table)) {

		// session-registry extensions (2026-04-20) are nullable so legacy rows stay valid:
		//   pid             = owner process id (admin / kick / debugger attach)
		//   address         = "host:port" for web processes; "" for desktop
		//   currentActivity = last scripted/engine label ("idle", "running:OnStart", "reload", ...)
		//   exclusive       = 1 when this session holds process-wide monopoly mode; 0 otherwise.
		//                     Cluster-aware exclusive gate reads this column from peer rows to block
		//                     new Connects when another process is exclusive.
		q.Execute(ibCreateTable(session_table, {
			{ wxT("session"),         ibTypeString(36),  false, true,  wxEmptyString },
			{ wxT("userName"),        ibTypeString(64),  true,  false, wxEmptyString },
			{ wxT("application"),     ibTypeInteger(),   true,  false, wxEmptyString },
			{ wxT("started"),         ibTypeDate(),      true,  false, wxEmptyString },
			{ wxT("lastActive"),      ibTypeDate(),      true,  false, wxEmptyString },
			{ wxT("computer"),        ibTypeString(128), true,  false, wxEmptyString },
			{ wxT("pid"),             ibTypeInteger(),   false, false, wxEmptyString },
			{ wxT("address"),         ibTypeString(256), false, false, wxEmptyString },
			{ wxT("currentActivity"), ibTypeString(128), false, false, wxEmptyString },
			{ wxT("exclusive"),       ibTypeInteger(),   false, false, wxEmptyString },
		}));
		// Indexes ride with the just-created table — no "if not exists" (redundant here, and
		// unsupported by Firebird), so the per-driver fork collapses to three plain CREATE INDEX.
		q.Execute(ibCreateIndex(session_table, wxT("session_index_1"), { wxT("session"), wxT("userName") }));
		q.Execute(ibCreateIndex(session_table, wxT("session_index_2"), { wxT("session") }));
		q.Execute(ibCreateIndex(session_table, wxT("session_index_3"), { wxT("lastActive") }));
	}
}

void ibApplicationData::CreateTableEvent()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(event_table)) {
	}
}

// sys_lock — long-held pessimistic-lock coordination table (see
// docs/record-locks.md "Planned upgrade path"). One row per held
// lock. ibLockManager INSERTs on Acquire, DELETEs on Release / on
// session end / on zombie sweep. Index on (namespace, keyHash) drives
// the per-acquire conflict-check; index on sessionGuid drives the
// session-end cascade.
void ibApplicationData::CreateTableLock()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(lock_table)) {

		q.Execute(ibCreateTable(lock_table, {
			{ wxT("lockGuid"),    ibTypeString(36),   false, true,  wxEmptyString },
			{ wxT("sessionGuid"), ibTypeString(36),   true,  false, wxEmptyString },   // owner identity (session.GUID or custom holder)
			{ wxT("namespace"),   ibTypeString(128),  true,  false, wxEmptyString },   // e.g. "Catalog.Products"
			{ wxT("keyHash"),     ibTypeString(64),   true,  false, wxEmptyString },   // SHA-256 hex of canonical key bytes
			{ wxT("keyData"),     ibTypeString(1024), false, false, wxEmptyString },   // canonical human-readable key for conflict messages
			{ wxT("lockMode"),    ibTypeInteger(),    true,  false, wxEmptyString },   // 0=Shared, 1=Exclusive
			{ wxT("acquiredAt"),  ibTypeDate(),       true,  false, wxEmptyString },
			{ wxT("userName"),    ibTypeString(128),  false, false, wxEmptyString },   // holder's display name (snapshot at acquire)
			{ wxT("computer"),    ibTypeString(128),  false, false, wxEmptyString },
		}));
		// Index on (namespace, keyHash) drives the per-acquire conflict-check; index on
		// sessionGuid drives the session-end cascade. No per-driver fork — see CreateTableSession.
		q.Execute(ibCreateIndex(lock_table, wxT("lock_index_1"), { wxT("namespace"), wxT("keyHash") }));
		q.Execute(ibCreateIndex(lock_table, wxT("lock_index_2"), { wxT("sessionGuid") }));
	}
}

// sys_job — the SHARED clock for scheduled jobs. One row per job name,
// carrying when it last ran as every process on this base sees it.
//
// Why it exists: the cross-process claim (sys_lock, Job.<name>) answers
// "is somebody running it RIGHT NOW", which is not the same question as
// "has it already run recently". Without a shared last-run, two clients
// open on one file base each keep their own in-memory clock and the job
// fires once per process per interval — twice the work, and for anything
// that is not idempotent, twice the effect.
//
// Deliberately minimal. No history, no status, no next-run: those are
// per-process observations (ibJobState) and belong in memory. What has to
// be shared is exactly the one fact that decides whether to start.
void ibApplicationData::CreateTableJob()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(job_table)) {

		q.Execute(ibCreateTable(job_table, {
			{ wxT("jobName"),  ibTypeString(128), false, true,  wxEmptyString },   // primary key — the job's registered name
			{ wxT("lastRun"),  ibTypeDate(),      true,  false, wxEmptyString },   // wall clock, shared across processes
			{ wxT("computer"), ibTypeString(128), false, false, wxEmptyString },   // who ran it last, for diagnostics
		}));
	}
}

// Additive column migration for sys_session. Existing databases created
// before the session-registry refactor (pid / address / currentActivity
// added 2026-04-20) are transparently upgraded on startup — registry
// writes / reads assume these columns exist, so an old schema would
// trip INSERT and snapshot SELECT otherwise. Columns are nullable, so
// legacy rows stay valid until the next heartbeat rewrite.
void ibApplicationData::MigrateTableSession()
{
	ibDatabaseQueryBuilder qi;
	if (!qi.TableExists(session_table))
		return;

	wxArrayString cols = qi.GetColumns(session_table);
	auto has = [&](const wxString& col) {
		for (std::size_t i = 0; i < cols.GetCount(); ++i)
			if (cols[i].CmpNoCase(col) == 0) return true;
		return false;
	};

	// Each ADD is best-effort and independent — a fresh builder per column so one driver's
	// rejection (caught below) never poisons the next column's statement.
	auto addColumn = [&](const wxString& name, ibColumnType type) {
		try { ibDatabaseQueryBuilder q; q.Execute(ibAddColumn(session_table, { name, type, false, false, wxEmptyString })); }
		catch (...) { /* swallowed: best-effort migration; driver may not support this DDL */ }
	};

	if (!has(wxT("pid")))             addColumn(wxT("pid"),             ibTypeInteger());
	if (!has(wxT("address")))         addColumn(wxT("address"),         ibTypeString(256));
	if (!has(wxT("currentActivity"))) addColumn(wxT("currentActivity"), ibTypeString(128));
	// kind — ibSessionKind session-level role (WebServer=5, WebClient=100; desktop kinds share
	// numeric values with ibRunMode). Distinct from `application` (process-level ibRunMode).
	if (!has(wxT("kind")))            addColumn(wxT("kind"),            ibTypeInteger());
	// signal — admin → registry control channel; picked up on the next JobCheckSignal tick, then
	// cleared by the handler. "kick" is the first supported value (reload / refresh may follow).
	if (!has(wxT("signal")))          addColumn(wxT("signal"),          ibTypeString(32));
	// exclusive — process-wide monopoly marker; cluster-aware gate in ProcessAdd /
	// ProcessSetExclusive reads peer rows to detect another process holding it.
	if (!has(wxT("exclusive")))       addColumn(wxT("exclusive"),       ibTypeInteger());
}

// Bring up sys_bytecode_cache. Independent of the user/session/event
// triple — runs in any runMode after the existing-tables gate, so DBs
// initialised before AOT cache landed pick the table up on the next
// open of any process. Idempotent; second call is a no-op once the
// table exists.
//
// Per-driver column types: PostgreSQL uses BYTEA for the binary blob,
// every other driver (Firebird embedded, SQLite, MySQL, ODBC) takes
// plain BLOB. Firebird's BLOB SUB_TYPE 0 is implicit when no sub-type
// is named.
void ibApplicationData::MigrateTableBytecodeCache()
{
	ibDatabaseQueryBuilder q;
	if (q.TableExists(bytecode_cache_table))
		return;

	try {
		// bc_blob's BLOB renders as BYTEA on PostgreSQL and BLOB on every other driver
		// (Firebird embedded, SQLite, MySQL, ODBC) via the dialect TYPE-MAP — no fork here.
		q.Execute(ibCreateTable(bytecode_cache_table, {
			{ wxT("descriptor_id"),    ibTypeString(36), false, true,  wxEmptyString },
			{ wxT("bytecode_version"), ibTypeString(36), true,  false, wxEmptyString },
			{ wxT("bc_blob"),          ibTypeBlob(),     true,  false, wxEmptyString },
		}));
	}
	catch (...) {
		// Best-effort — DDL failure leaves Save / Load in their
		// "no table" no-op branch; runtime falls back to compile path.
	}
}

bool ibApplicationData::ClearTableUser()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(user_table))
		return false;

	q.Execute(ibDelete(user_table));   // no WHERE = all rows
	return true;
}

///////////////////////////////////////////////////////////////////////////////

#include "fileSystem/fs.h"

// User-record DB I/O moved onto ibUserInfo as static factories; see
// backend/userInfo.{h,cpp}. ibApplicationData no longer mediates the
// sys_user round-trip — call sites use ibUserInfo::Read / Save / Serialize
// / Deserialize directly.

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::LoadUserInfoFromBuffer(wxMemoryBuffer& buffer)
{
	ibReaderMemory reader = buffer;

	ibReaderMemory* prevReaderMemory = nullptr; u64 meta_id = 0;

	while (!reader.eof()) {

		ibReaderMemory* readerMemory = reader.open_chunk_iterator(meta_id, &*prevReaderMemory);
		if (!readerMemory)
			break;

		ibUserInfo::Save(ibUserInfo::Deserialize(*readerMemory));

		prevReaderMemory = readerMemory;
	};

	return true;
}

bool ibApplicationData::SaveUserInfoToBuffer(wxMemoryBuffer& buffer) const
{
	// SELECT guid FROM sys_user
	try {
		ibDatabaseQueryBuilder q;
		ibQueryIR ir(ibProject(ibScan(user_table), { { ibCol(wxT("guid")), wxEmptyString } }));
		ibQueryResult result = q.ExecuteIR(ir);

		ibWriterMemory writer; unsigned int idx = 0;

		while (result.Next()) {
			ibWriterMemory userWriter;
			ibUserInfo::Read(ibGuid(result.GetResultString(wxT("guid"))))
				.Serialize(userWriter);
			writer.w_chunk(idx++, userWriter.buffer());
		}

		buffer = writer.buffer();
	}
	catch (...) { return false; }
	return true;
}

///////////////////////////////////////////////////////////////////////////////

// HasAllowedUser / GetAllowedUser moved onto ibUserInfo as
// ibUserInfo::HasAny / ibUserInfo::ListAll — sys_user table-wide
// queries belong with the type, not on the singleton.

// -----------------------------------------------------------------------
// Phased session lifecycle — apps compose CreateSession (or the typed
// CreateSession<T>) with holder->Open() so the anonymous row stays
// visible during login-retry loops. There is no one-shot
// "Connect/StartSession" anymore; a failed Open keeps the row in
// sys_session until the caller drops the holder, and dropping it is
// what removes the row.
// -----------------------------------------------------------------------

ibSessionHolder ibApplicationData::CreateSession()
{
	// Default-factory passthrough — registry builds a plain ibSession.
	// Used by codeRunner / daemon / headless callers and by the wes
	// process's own system session bring-up. GUI apps go through the
	// typed CreateSession<T>() template overload (defined in
	// sessionRegistry.h after the registry class).
	return m_sessionRegistry->CreateSessionWithFactory(m_runMode, m_strComputer, {});
}

