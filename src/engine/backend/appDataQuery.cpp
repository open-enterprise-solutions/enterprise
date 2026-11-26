#include "appData.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — DDL/DML + TableExists/GetColumns/IsOpen + typed row reads (no raw L1)
#include "backend/databaseLayer/connectionPool.h"

#include "backend/backend_exception.h"

#include "backend/session/sessionRegistry.h"
#include "core/diagnostics/journal.h"


///////////////////////////////////////////////////////////////////////////////
//								ibApplicationInstance
///////////////////////////////////////////////////////////////////////////////

bool ibApplicationInstance::TableAlreadyCreated()
{
	ibDatabaseQueryBuilder q;
	return q.TableExists(user_table) &&
		q.TableExists(session_table);
}

///////////////////////////////////////////////////////////////////////////////

void ibApplicationInstance::CreateTableUser()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(user_table)) {
		// One CreateTable for every driver — the dialect TYPE-MAP renders binaryData's BLOB as
		// BYTEA on PostgreSQL and BLOB everywhere else, so the old per-driver fork is gone.
		q.Execute(ibCreateTable(user_table, {
			{ ibT("guid"),       ibTypeString(36),  /*notNull*/false, /*pk*/true,  nullptr },
			{ ibT("name"),       ibTypeString(64),  /*notNull*/true,  /*pk*/false, nullptr },
			{ ibT("fullName"),   ibTypeString(128), /*notNull*/true,  /*pk*/false, nullptr },
			{ ibT("changed"),    ibTypeDate(),      /*notNull*/true,  /*pk*/false, nullptr },
			{ ibT("dataSize"),   ibTypeInteger(),   /*notNull*/true,  /*pk*/false, nullptr },
			{ ibT("binaryData"), ibTypeBlob(),      /*notNull*/true,  /*pk*/false, nullptr },
		}));
		// The index rides with the just-created table, so the old "if not exists" was redundant
		// (and let Firebird choke on that syntax) — a plain CREATE INDEX is correct here.
		q.Execute(ibCreateIndex(user_table, ibT("user_index"), { ibT("guid"), ibT("name") }));
	}
}

void ibApplicationInstance::CreateTableSession()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(session_table)) {

		// What the registry keeps beside who and when:
		//   pid             = owner process id (admin / kick / debugger attach)
		//   address         = "host:port" for web processes; "" for desktop
		//   currentActivity = last scripted/engine label ("idle", "running:OnStart", "reload", ...)
		//   kind            = ibSessionKind, the session's role (WebServer=5, WebClient=100; desktop kinds share
		//                     numeric values with ibRunMode) — distinct from `application`, the process's ibRunMode
		//   signal          = admin → registry control channel, picked up on the next JobCheckSignal tick and
		//                     cleared by the handler ("kick" is the first value)
		//   singleUser      = 1 when this session holds process-wide single-user mode; 0 otherwise.
		//                     Cluster-aware single-user gate reads this column from peer rows to block
		//                     new Connects when another process holds the mode.
		q.Execute(ibCreateTable(session_table, {
			{ ibT("session"),         ibTypeString(36),  false, true,  nullptr },
			{ ibT("userName"),        ibTypeString(64),  true,  false, nullptr },
			{ ibT("application"),     ibTypeInteger(),   true,  false, nullptr },
			{ ibT("started"),         ibTypeDate(),      true,  false, nullptr },
			{ ibT("lastActive"),      ibTypeDate(),      true,  false, nullptr },
			{ ibT("computer"),        ibTypeString(128), true,  false, nullptr },
			{ ibT("pid"),             ibTypeInteger(),   false, false, nullptr },
			{ ibT("address"),         ibTypeString(256), false, false, nullptr },
			{ ibT("currentActivity"), ibTypeString(ibSessionRegistry::kActivityWidth), false, false, nullptr },
			{ ibT("kind"),            ibTypeInteger(),   false, false, nullptr },
			{ ibT("signal"),          ibTypeString(32),  false, false, nullptr },
			// NOT NULL with a default: "not single-user" gets ONE spelling, decided by the column rather
			// than by whoever wrote the row. A nullable flag has two (NULL and 0) and they agree only
			// as long as every reader remembers to make them agree.
			{ ibT("singleUser"),       ibTypeInteger(),   true,  false, ibConst(ibValue(0)) },
			// tokenHash — SHA-256 hex of the thin client's bearer token. The token itself is never stored.
			// Null: a session with nothing to resume (every kind but a thin client that has logged in).
			{ ibT("tokenHash"),       ibTypeString(64),  false, false, nullptr },
		}));
		// Indexes ride with the just-created table — no "if not exists" (redundant here, and
		// unsupported by Firebird), so the per-driver fork collapses to three plain CREATE INDEX.
		q.Execute(ibCreateIndex(session_table, ibT("session_index_1"), { ibT("session"), ibT("userName") }));
		q.Execute(ibCreateIndex(session_table, ibT("session_index_2"), { ibT("session") }));
		q.Execute(ibCreateIndex(session_table, ibT("session_index_3"), { ibT("lastActive") }));
		q.Execute(ibCreateIndex(session_table, ibT("session_index_4"), { ibT("tokenHash") }));
	}
}

void ibApplicationInstance::CreateTableEvent()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(event_table)) {
	}
}

// sys_job — the SHARED clock for scheduled jobs. One row per job name,
// carrying when it last ran as every process on this base sees it.
//
// Why it exists: the cross-process claim (the lock file, Job.<name>) answers
// "is somebody running it RIGHT NOW", which is not the same question as
// "has it already run recently". Without a shared last-run, two clients
// open on one file base each keep their own in-memory clock and the job
// fires once per process per interval — twice the work, and for anything
// that is not idempotent, twice the effect.
//
// Deliberately minimal. No history, no status, no next-run: those are
// per-process observations (ibJobState) and belong in memory. What has to
// be shared is exactly the one fact that decides whether to start.
void ibApplicationInstance::CreateTableJob()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(job_table)) {

		q.Execute(ibCreateTable(job_table, {
			// PRIMARY KEY — the job's GUID, not its name. An application's job carries its
			// metaobject's guid (which survives a rename, an unload / reload and a copy onto
			// another base); a platform job carries a fixed one minted once in platformJobs.cpp.
			// One kind of value in the column, so no reader has to ask which sort of key it is
			// looking at — and keying by the display name would orphan a renamed job's settings
			// and its clock, leaving a row that still says "switched off" pointing at nothing.
			// Native UUID on PostgreSQL, CHAR(36) elsewhere — the dialect TYPE-MAP picks.
			{ ibT("jobKey"),   ibTypeGuid(),      false, true,  nullptr },
			{ ibT("jobName"),  ibTypeString(128), true,  false, nullptr },   // display name, for a person reading the table
			// ⚠⚠ THE THIRD FLAG IS `m_notNull`, NOT "nullable" — and the columns below were once
			// written as though it were the latter. lastRun and computer describe something a job
			// that has NEVER RUN does not have, so they must ACCEPT NULL: there is no clock reading
			// before the first run.
			//
			// Declared NOT NULL, the seed INSERT that Register() writes for a job it has never seen
			// died on the first one: "validation error for column SYS_JOB.LASTRUN, value *** null
			// ***". Which is to say enterprise.exe could not open a database at all — the flags said
			// the opposite of what the comments beside them promised.
			{ ibT("lastRun"),  ibTypeDate(),      false, false, nullptr },   // wall clock, shared across processes
			{ ibT("computer"), ibTypeString(128), false, false, nullptr },   // who ran it last, for diagnostics
			// The two SETTINGS a base holds — what the enterprise may change without opening the
			// Studio. They are here and not in the metadata because switching a misbehaving job
			// off at 3 a.m. must not mean editing the application on a production base; and for
			// the engine's OWN jobs there is no application to edit at all.
			{ ibT("active"),   ibTypeBoolean(),   false, false, nullptr },   // stated by every write of the settings
			{ ibT("schedule"), ibTypeBlob(),      false, false, nullptr },   // ibJobScheduleDescriptionMemory blob
		}));
	}
}

// sys_settings — ONE ROW PER SAVED SETTING: a packed runtime value under an address of
// category + object + name + user. What a person arranged on a form or on a list stops
// dying with the window that arranged it.
//
// ⚠ THE PRIMARY KEY IS A HASH OF THE ADDRESS, not the address itself, and that is a fact
// about the DDL renderer rather than about settings: PRIMARY KEY is spelled per COLUMN, so
// four key columns would emit four of them and no driver would take the CREATE — keyHash for the lookup, the readable parts
// beside it. They also carry the "everything this user saved for that object" query,
// which is what the index is for.
//
// The payload's format is NOT this table's business: binaryData is a NODE written through
// ibBinaryProvider, and it changes when what it carries changes without a word of DDL.
// Deliberately no "version" column for the same reason — the node format carries its own,
// and a second one here would be a version of nothing.
//
// 🛑 AND WHEN THIS SCHEMA CHANGES, IT MAY NOT BE DROPPED AND REBUILT. sys_job and
// sys_bytecode_cache could be, and be right: a job's settings re-seed from its declaration and a
// cache recomputes, so nothing a person did is in them. THIS table is the
// opposite — every row is something somebody arranged and asked to keep, and it exists nowhere
// else. A schema change here is ADD COLUMN (nullable, read as absent by an older row), never a
// DROP. That is not an alpha-vs-release distinction; it is what the table holds.
void ibApplicationInstance::CreateTableSettings()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(settings_table)) {

		q.Execute(ibCreateTable(settings_table, {
			{ ibT("entryKey"),   ibTypeString(64),  false, true,  nullptr },   // SHA-256 hex of the four parts below
			{ ibT("category"),   ibTypeInteger(),   true,  false, nullptr },   // ibSettingsCategory, BY NUMBER
			{ ibT("objectKey"),  ibTypeString(128), true,  false, nullptr },   // metaobject guid, or a name of the caller's own
			// NOT NULL with a default: "this object has only one setting" and "this row is
			// everybody's" get ONE spelling each, decided by the column rather than by whoever
			// wrote the row. A nullable key column has two (NULL and ''), and they agree only as
			// long as every reader remembers to make them agree — while the hash above already
			// took the empty string as its input.
			{ ibT("settingKey"), ibTypeString(128), true,  false, ibConst(ibValue(ibEmptyString)) },       // which setting of that object; '' = the only one
			{ ibT("userKey"),    ibTypeString(36),  true,  false, ibConst(ibValue(ibEmptyString)) },       // whose; '' = shared by everybody
			{ ibT("changed"),    ibTypeDate(),      true,  false, nullptr },
			{ ibT("dataSize"),   ibTypeInteger(),   true,  false, nullptr },
			{ ibT("binaryData"), ibTypeBlob(),      true,  false, nullptr },   // the packed value; BYTEA on PostgreSQL via the TYPE-MAP
		}));
		// The readable address, for "everything this user saved about that object" — the query an
		// administrator's "reset this person's settings" and a form's own cleanup both make. The
		// index rides with the just-created table, so no "if not exists" (which Firebird rejects).
		q.Execute(ibCreateIndex(settings_table, ibT("settings_index_1"),
			{ ibT("category"), ibT("objectKey"), ibT("userKey") }));
	}
}

// sys_file — the sessions' temporary files (ibTempStorage). One row per PART of a file: part 0 is its header
// (the name it came under), the content follows in parts — a file is never one blob, so a large one passes
// through without being held whole anywhere.
//
// The key is the part's address, file + number, in ONE column — PRIMARY KEY is spelled per column (see
// sys_settings above); the two halves stand beside it for the reads. The owner is on every part, as the lock file
// carries it on every lock: the session-end cascade and the sweep delete by it.
//
// Nothing here outlives its session, so a schema change may drop and rebuild it.
void ibApplicationInstance::CreateTableFile()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(file_table)) {

		q.Execute(ibCreateTable(file_table, {
			{ ibT("partKey"),     ibTypeString(48),  false, true,  nullptr },   // fileKey:partIndex
			{ ibT("fileKey"),     ibTypeString(36),  true,  false, nullptr },   // the file's id
			{ ibT("partIndex"),   ibTypeInteger(),   true,  false, nullptr },   // 0 the header, then 1, 2, … in the order written
			{ ibT("sessionGuid"), ibTypeString(36),  true,  false, nullptr },   // the owner
			{ ibT("fileName"),    ibTypeString(260), false, false, nullptr },   // the header's: the name the file came under
			{ ibT("changed"),     ibTypeDate(),      true,  false, nullptr },
			{ ibT("dataSize"),    ibTypeInteger(),   true,  false, nullptr },
			{ ibT("binaryData"),  ibTypeBlob(),      false, false, nullptr },   // a content part's bytes; the header has none
		}));
		// A file's parts in order; and the owner's, for the session-end cascade.
		q.Execute(ibCreateIndex(file_table, ibT("file_index_1"), { ibT("fileKey"), ibT("partIndex") }));
		q.Execute(ibCreateIndex(file_table, ibT("file_index_2"), { ibT("sessionGuid") }));
	}
}

// sys_bytecode_cache — the compiled modules, by descriptor. bc_blob's BLOB renders as BYTEA on PostgreSQL and BLOB on
// every other driver (Firebird embedded, SQLite, ODBC) via the dialect TYPE-MAP — no fork here.
void ibApplicationInstance::CreateTableBytecodeCache()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(bytecode_cache_table)) {
		q.Execute(ibCreateTable(bytecode_cache_table, {
			{ ibT("descriptor_id"),    ibTypeString(36), false, true,  nullptr },
			{ ibT("bytecode_version"), ibTypeString(36), true,  false, nullptr },
			// WHAT THE BYTECODE WAS COMPILED AGAINST — the application's own digest, recomputed on
			// every save. It is part of the LOOKUP, not a field somebody has to remember to compare:
			// a row written under an earlier application is simply not found. See byteCodeCache.h.
			{ ibT("application_md5"),  ibTypeString(32), true,  false, nullptr },
			{ ibT("bc_blob"),          ibTypeBlob(),     true,  false, nullptr },
		}));
	}
}

bool ibApplicationInstance::ClearTableUser()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(user_table))
		return false;

	q.Execute(ibDelete(user_table));   // no WHERE = all rows
	return true;
}

///////////////////////////////////////////////////////////////////////////////

#include "core/memory/memoryStream.h"

// User-record DB I/O moved onto ibUserInfo as static factories; see
// backend/userInfo.{h,cpp}. ibApplicationInstance no longer mediates the
// sys_user round-trip — call sites use ibUserInfo::Read / Save / Serialize
// / Deserialize directly.

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationInstance::LoadUserInfoFromBuffer(ibMemoryBuffer& buffer)
{
	ibReaderMemory reader = buffer;

	ibReaderMemory* prevReaderMemory = nullptr; std::uint64_t meta_id = 0;

	while (!reader.eof()) {

		ibReaderMemory* readerMemory = reader.open_chunk_iterator(meta_id, &*prevReaderMemory);
		if (!readerMemory)
			break;

		ibUserInfo::Save(ibUserInfo::Deserialize(*readerMemory));

		prevReaderMemory = readerMemory;
	};

	return true;
}

bool ibApplicationInstance::SaveUserInfoToBuffer(ibMemoryBuffer& buffer) const
{
	// SELECT guid FROM sys_user
	try {
		ibDatabaseQueryBuilder q;
		ibQueryIR ir(ibProject(ibScan(user_table), { { ibCol(ibT("guid")), ibEmptyString } }));
		ibQueryResult result = q.ExecuteIR(ir);

		ibWriterMemory writer; unsigned int idx = 0;

		while (result.Next()) {
			ibWriterMemory userWriter;
			ibUserInfo::Read(ibGuid(result.GetResultString(ibT("guid"))))
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

ibSessionHolder ibApplicationInstance::CreateSession(ibSessionKind kind)
{
	// Default-factory passthrough — registry builds a plain ibSession.
	// Used by coderunner / appserver / headless callers and by the wes
	// process's own system session bring-up. GUI apps go through the
	// typed CreateSession<T>() template overload (defined in
	// sessionRegistry.h after the registry class).
	return m_sessionRegistry->CreateSessionOfKind(m_runMode, m_strComputer, kind, {});
}

