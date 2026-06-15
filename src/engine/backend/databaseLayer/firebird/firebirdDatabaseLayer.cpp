#include "firebirdDatabaseLayer.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include "engine/ibase.h"

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
#include "firebirdInterface.h"
#endif

#include "firebirdPreparedStatement.h"
#include "firebirdResultSet.h"
#include "firebirdLeaderMode.h"
#include "firebirdLocalServer.h"
#include "firebirdMaintenanceScheduler.h"

#include <wx/file.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/regex.h>

// Firebird SQL dialect — owned by the driver. Static holds the definition (a
// test reads it without constructing the driver); virtual GetDialect() exposes
// it to L2. No central factory, no type-switch.
const ibDialectDictionary& ibDatabaseLayerFirebird::Dialect()
{
	static const ibDialectDictionary s_dialect = [] {
		ibDialectDictionary d;
		d.m_paramStyle  = ibParamStyle::QuestionMark;
		d.m_pagination  = ibPagination::FirstSkip;    // SELECT FIRST n SKIP m
		d.m_boolForm    = ibBoolForm::Smallint;       // no native boolean pre-FB3
		// UPDATE OR INSERT … MATCHING (pk) — no separate update body.
		d.m_upsertTemplate   = wxT("UPDATE OR INSERT INTO {table} ({columns}) VALUES ({values}) MATCHING ({keys})");
		d.m_upsertUpdateItem = wxEmptyString;
		d.m_features.m_window = true;                 // FB3+
		d.m_features.m_rollup = true;                 // GROUP BY ROLLUP(...) — FB5 (vendored: security5.fdb)
		// type map
		d.m_typeBoolean       = wxT("SMALLINT");
		d.m_typeDate          = wxT("TIMESTAMP");
		d.m_typeBlob          = wxT("BLOB");
		d.m_typeGuid          = wxT("CHAR(36)");
		// NUMERIC holds a wider range than DECIMAL (INT128-backed) — matches ibNumber.
		d.m_typeNumberPattern = wxT("NUMERIC(%d,%d)");
		d.m_rowLockSuffix     = wxT(" WITH LOCK");     // FB pessimistic row lock (not FOR UPDATE)
		d.m_rowLockNoWaitSuffix = wxEmptyString;       // FB has no FOR UPDATE NOWAIT — noWait rides the TX (isc_tpb_nowait)
		d.m_ddlCommitBeforeData = true;                // legacy isc_* API can't mix CREATE/ALTER + bound INSERT in one TX
		return d;
	}();
	return s_dialect;
}

const ibDialectDictionary& ibDatabaseLayerFirebird::GetDialect() const
{
	return Dialect();
}

// ctor()
ibDatabaseLayerFirebird::ibDatabaseLayerFirebird()
	: ibDatabaseLayer()
{
	m_pDatabase = 0;
	m_pTransaction = 0;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceFirebird();
	
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}

#endif

	m_strServer = wxT("");  // assume embedded database in this case
	m_strUser = wxT("sysdba");
	m_strPassword = wxT("masterkey");
	m_strDatabase = wxT("");
	m_strRole = wxEmptyString;
}

ibDatabaseLayerFirebird::ibDatabaseLayerFirebird(const wxString& strDatabase)
	: ibDatabaseLayer()
{
	m_pDatabase = 0;
	m_pTransaction = 0;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceFirebird();
	
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}

#endif

	m_strServer = wxT("");  // assume embedded database in this case
	m_strUser = wxT("sysdba");
	m_strPassword = wxT("masterkey");
	m_strRole = wxEmptyString;

	Open(strDatabase);
}

ibDatabaseLayerFirebird::ibDatabaseLayerFirebird(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: ibDatabaseLayer()
{
	m_pDatabase = 0;
	m_pTransaction = 0;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceFirebird();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}
#endif

	m_strServer = wxT("");  // assume embedded database in this case
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strRole = wxEmptyString;

	Open(strDatabase);
}

ibDatabaseLayerFirebird::ibDatabaseLayerFirebird(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: ibDatabaseLayer()
{
	m_pDatabase = 0;
	m_pTransaction = 0;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceFirebird();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}
#endif

	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strRole = wxEmptyString;

	Open(strDatabase);
}

ibDatabaseLayerFirebird::ibDatabaseLayerFirebird(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword, const wxString& strRole)
	: ibDatabaseLayer()
{
	m_pDatabase = 0;
	m_pTransaction = 0;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceFirebird();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}
#endif

	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strRole = strRole;

	Open(strDatabase);
}

ibDatabaseLayerFirebird::ibDatabaseLayerFirebird(const ibDatabaseLayerFirebird& src) 
{
	m_pDatabase = 0;
	m_pTransaction = 0;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceFirebird();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}
#endif

	m_strServer = src.m_strServer;
	m_strUser = src.m_strUser;
	m_strPassword = src.m_strPassword;
	m_strRole = src.m_strRole;

	Open(src.m_strDatabase);
}

// dtor()
ibDatabaseLayerFirebird::~ibDatabaseLayerFirebird()
{
	Close();
	ISC_STATUS_ARRAY* pStatus = (ISC_STATUS_ARRAY*)m_pStatus;
	wxDELETEA(pStatus);
	m_pStatus = NULL;
	wxDELETE(m_pInterface);
	m_pInterface = NULL;
}

// open database
bool ibDatabaseLayerFirebird::Open(const wxString& strDatabase)
{
	m_strDatabase = strDatabase;
	return Open();
}

bool ibDatabaseLayerFirebird::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strUser = strUser;
	m_strPassword = strPassword;

	return Open(strDatabase);
}

bool ibDatabaseLayerFirebird::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;

	return Open(strDatabase);
}

bool ibDatabaseLayerFirebird::Open()
{
	ResetErrorCodes();

	if (m_pInterface == NULL)
		return false;

	//wxCSConv conv(wxT("UTF-8"));
	//SetEncoding(&conv);

	// Leader-election orchestrator hook. UNC / SMB paths route
	// through `ibFirebirdLeaderMode::InitForDatabase` which decides
	// whether this process is leader (acquired the SMB byte-range
	// lock and hosts the database locally via a child `firebird.exe`
	// TCP listener) or follower (someone else is leader; attach to
	// them over TCP). Local paths bypass the orchestrator and
	// always go embedded — same behaviour as before leader-mode
	// landed.
	wxString strDatabaseUrl;
	if (m_strServer.IsEmpty()) {
		const auto lm = ibFirebirdLeaderMode::InitForDatabase(m_strDatabase);
		if (!lm.ok) {
			SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
			SetErrorMessage(lm.errorMessage);
			ThrowDatabaseException();
			return false;
		}
		if (lm.role == ibFirebirdLeaderMode::Role::Standalone) {
			// No lease — normal embedded attach by file path.
			strDatabaseUrl = m_strDatabase;
		} else {
			// Leader or follower — orchestrator hands back the right
			// attach URL (either `inet://localhost:<port>/<path>` for
			// the leader's own embedded-via-TCP path, or
			// `inet://leader-host:<port>/<path>` for followers).
			strDatabaseUrl = lm.connectUrl;
		}
	}
	else {
		strDatabaseUrl = m_strServer + wxT(":") + m_strDatabase;
	}

	wxCharBuffer urlBuffer = ConvertToUnicodeStream(strDatabaseUrl);
	unsigned int urlLength = GetEncodedStreamLength(strDatabaseUrl);

	std::string dpbBuffer;
	{
		dpbBuffer.push_back(isc_dpb_version1);
		dpbBuffer.push_back(isc_dpb_sql_dialect);
		dpbBuffer.push_back(1); // 1 byte long
		dpbBuffer.push_back(SQL_DIALECT_CURRENT);

		// page_size DPB: only honoured by isc_create_database (ignored on
		// attach). Encoded as big-endian 2 bytes per legacy DPB. FB 5
		// supports up to 32768; widen via uint32 before shifting so
		// 32768 doesn't sign-overflow.
		const uint32_t pageSize = (uint32_t)m_pageSize;
		dpbBuffer.push_back(isc_dpb_page_size);
		dpbBuffer.push_back(2);
		dpbBuffer.push_back((char)((pageSize >> 8) & 0xFF));
		dpbBuffer.push_back((char)(pageSize & 0xFF));

		// UTF8 character set:
		//   isc_dpb_set_db_charset — only honoured on CREATE DATABASE; sets
		//     the database default charset stored in the header page.
		//   isc_dpb_lc_ctype       — sets the *connection* charset; honoured
		//     on every attach. Without it FB falls back to NONE and returns
		//     raw bytes, which corrupts non-ASCII (Cyrillic, etc.) on read.
		// Both are pushed so a freshly-created DB has UTF8 as default and
		// every attach (create or existing) talks UTF8 to the engine.
		const char sCharset[] = "UTF8";
		dpbBuffer.push_back(isc_dpb_set_db_charset);
		dpbBuffer.push_back(sizeof(sCharset) - 1);
		dpbBuffer.append(sCharset);

		dpbBuffer.push_back(isc_dpb_lc_ctype);
		dpbBuffer.push_back(sizeof(sCharset) - 1);
		dpbBuffer.append(sCharset);

		// force_write = 0 — async writes. FB defaults a freshly CREATE'd
		// embedded DB to forced/synchronous writes (FlushFileBuffers per
		// dirty page); on Windows NTFS that's ~5–10× slower than async.
		// Async still uses careful-writes ordering, so a process crash
		// loses at most ~1s of pending writes — acceptable for OES
		// embedded dev/test. Existing DBs ignore this DPB on attach
		// (FB stores the flag in the header); flip with `gfix -w async`
		// or `ALTER DATABASE SET WRITE = ASYNC` when you want it on
		// pre-existing files.
		dpbBuffer.push_back(isc_dpb_force_write);
		dpbBuffer.push_back(1);
		dpbBuffer.push_back(0);

		// Session time zone = UTC. FB 4+ supports TIMESTAMP WITH TIME ZONE;
		// pinning the session to UTC means timestamps stored/returned over
		// this connection are always normalized to UTC regardless of the
		// client OS time zone. Plain TIMESTAMP columns are unaffected.
		// Avoids "the same row reads different times on different
		// machines" surprises.
		const char sTimeZone[] = "UTC";
		dpbBuffer.push_back((char)isc_dpb_session_time_zone);
		dpbBuffer.push_back(sizeof(sTimeZone) - 1);
		dpbBuffer.append(sTimeZone);

		// sweep_interval — how many transactions between automatic
		// sweep passes (background MVCC garbage collection that frees
		// pages occupied by dead row versions). FB default is 20000;
		// we tighten to 5000 so active-OLTP databases don't accumulate
		// as much dead-version bloat before sweep reclaims it. Sweep
		// runs in the background on the connection that triggers it
		// (typical impact: brief CPU spike, no DML pause). Encoded
		// as 4-byte little-endian per legacy DPB.
		{
			const uint32_t sweepInterval = 5000;
			dpbBuffer.push_back(isc_dpb_sweep_interval);
			dpbBuffer.push_back(4);
			dpbBuffer.push_back((char)(sweepInterval       & 0xFF));
			dpbBuffer.push_back((char)((sweepInterval >> 8 ) & 0xFF));
			dpbBuffer.push_back((char)((sweepInterval >> 16) & 0xFF));
			dpbBuffer.push_back((char)((sweepInterval >> 24) & 0xFF));
		}

		// num_buffers — per-attachment page-cache size in pages. Default
		// DefaultDbCachePages = 2048 (32 MB at 16K pages) is set in
		// firebird.conf; we set it via DPB too so the driver owns the
		// policy and firebird.conf becomes a fallback for engine-level
		// settings only (ServerMode = Classic for RDP coordination,
		// which has no DPB equivalent). Encoded as 4-byte little-endian
		// integer per the dpb_num_buffers convention.
		{
			const uint32_t numBuffers = 2048;
			dpbBuffer.push_back(isc_dpb_num_buffers);
			dpbBuffer.push_back(4);
			dpbBuffer.push_back((char)(numBuffers       & 0xFF));
			dpbBuffer.push_back((char)((numBuffers >> 8 ) & 0xFF));
			dpbBuffer.push_back((char)((numBuffers >> 16) & 0xFF));
			dpbBuffer.push_back((char)((numBuffers >> 24) & 0xFF));
		}

		// parallel_workers (FB 5) — per-attachment cap for the
		// operations FB 5 actually parallelises. ParallelWorkers in
		// firebird.conf is the global ceiling; this DPB value is the
		// upper bound for THIS connection (clamped to ≤
		// MaxParallelWorkers from conf). Encoded as 4-byte
		// little-endian integer.
		//
		// What FB 5 parallelises with this knob (the things OES
		// actually triggers):
		//   - Sweep — ibFirebirdMaintenance::RunSweep; faster finish
		//     ⇒ smaller window of page-cache thrash.
		//   - Backup / Restore — RunBackupRestoreCycle in the off-
		//     hours maintenance window.
		//   - CREATE INDEX / ALTER INDEX ACTIVE / index rebuild —
		//     designer deploys, data-import flows.
		//
		// What it does NOT parallelise in FB 5: regular SELECT (no
		// parallel scan yet — planned for FB 6), DML, OLTP in
		// general. OES's per-form OLTP-light query mix sees no
		// per-query latency change; the win is entirely in those
		// backend-task durations on the leader.
		//
		// Our vendored consts_pub.h is FB-4-era and stops at
		// isc_dpb_decfloat_traps (95); the parallel_workers tag was
		// added in FB 5. We're running against the FB 5.0.5 runtime,
		// so define it locally with the upstream value. The previous
		// `#ifdef isc_dpb_parallel_workers` guard silently skipped
		// the whole block because the symbol wasn't defined, leaving
		// per-attachment parallelism off. On a future header bump,
		// the duplicate `#define` will surface as a clear "macro
		// redefined" diagnostic at this site.
#ifndef isc_dpb_parallel_workers
#define isc_dpb_parallel_workers 100
#endif
		{
			const uint32_t parallelWorkers = 2;
			dpbBuffer.push_back((char)isc_dpb_parallel_workers);
			dpbBuffer.push_back(4);
			dpbBuffer.push_back((char)(parallelWorkers       & 0xFF));
			dpbBuffer.push_back((char)((parallelWorkers >> 8 ) & 0xFF));
			dpbBuffer.push_back((char)((parallelWorkers >> 16) & 0xFF));
			dpbBuffer.push_back((char)((parallelWorkers >> 24) & 0xFF));
		}

		// isc_dpb_utf8_filename is a FLAG (boolean) DPB tag — it tells
		// the engine "the database filename in the attach call is
		// encoded in UTF-8". It carries NO value (length byte = 0).
		//
		// Previously this stuffed the full URL as the tag's value,
		// which malformed the DPB. Non-UNC paths apparently survived
		// (engine ignored / skipped the bogus value), but UNC paths
		// (`\\host\share\db.fdb`) tripped a CreateFile attempt against
		// the bogus data and returned isc_io_error (335544344) before
		// the real attach completed. Server-side attach still appeared
		// to succeed (lock files were created in ProgramData), but
		// fbclient never saw a successful response.
		dpbBuffer.push_back(isc_dpb_utf8_filename);
		dpbBuffer.push_back(0);

		if (m_strUser.length() > 0)
		{
			int nUsernameLength = m_strUser.length();
			dpbBuffer.push_back(isc_dpb_user_name);
			dpbBuffer.push_back(nUsernameLength);
			dpbBuffer.append(m_strUser);
		}

		if (m_strPassword.length() > 0)
		{
			int nPasswordLength = m_strPassword.length();
			dpbBuffer.push_back(isc_dpb_password);
			dpbBuffer.push_back(nPasswordLength);
			dpbBuffer.append(m_strPassword);
		}

		if (m_strRole.length() > 0)
		{
			int nRoleLength = m_strRole.length();
			dpbBuffer.push_back(isc_dpb_sql_role_name);
			dpbBuffer.push_back(nRoleLength);
			dpbBuffer.append(m_strRole);
		}
	}

	// If the layer was previously opened, detach the old handle before
	// creating a new one — otherwise the existing isc_db_handle leaks
	// and the second isc_attach_database silently allocates a fresh
	// handle that callers won't see.
	if (m_pDatabase) {
		try { Close(); } catch (...) { /* best-effort — fall through */ }
	}
	m_pDatabase = 0;

	isc_db_handle pDatabase = m_pDatabase;

	int nReturn = 0;

	if (m_strServer.IsEmpty())
	{
		// Check existence by *file path*, not by `strDatabaseUrl`.
		// In leader-mode the URL is a TCP form like
		// `inet://localhost:<port>/\\host\share\db.fdb` — wxFile::Exists
		// against that always returns false, which used to silently
		// route us into the CREATE branch even when the DB already
		// existed on the share. FB then tried to CREATE over the URL
		// and surfaced isc_io_error (335544344). The actual file path
		// (m_strDatabase) is what should drive the create-vs-attach
		// decision.
		if (!wxFile::Exists(m_strDatabase)){

			wxFileName fileDatabase(m_strDatabase);
			// Mkdir(..., wxPATH_MKDIR_FULL) is the recursive variant —
			// wxMkDir on a multi-level path silently fails, leaving FB
			// to error out with "I/O error during open" when the
			// containing directory doesn't exist yet.
			wxFileName::Mkdir(fileDatabase.GetPath(), 0777, wxPATH_MKDIR_FULL);

			nReturn = m_pInterface->GetIscCreateDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus,
				(unsigned short)urlLength, (const char*)urlBuffer,
				&pDatabase,
				(unsigned short)dpbBuffer.length(), dpbBuffer.c_str(),
				(unsigned short)0);
		}
		else
		{
			nReturn = m_pInterface->GetIscAttachDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus,
				(unsigned short)urlLength, (const char*)urlBuffer,
				&pDatabase,
				(unsigned short)dpbBuffer.length(), dpbBuffer.c_str());
		}
	}
	else
	{
		nReturn = m_pInterface->GetIscAttachDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus, urlLength, (char*)(const char*)urlBuffer,
			&pDatabase,
			dpbBuffer.length(), dpbBuffer.c_str());
	}

	m_pDatabase = pDatabase;

	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();

		return false;
	}

	// Cache the URL the new isc_db_handle was attached against so
	// ReconnectIfLeaderChanged can later detect leader handoff and
	// reattach. Empty m_strServer = local/leader-mode path; remote
	// (`server:db`) bypasses leader-mode entirely.
	m_currentConnectUrl = strDatabaseUrl;

	wxLogDebug(wxT("ibDatabaseLayerFirebird: attached to %s"),
	           strDatabaseUrl);

	// Spin up the maintenance scheduler — ONLY for Standalone single-
	// process embedded. Leader-mode (our own spawned firebird.exe
	// holds the .fdb via TCP) cannot do gbak BR-cycle's atomic SWAP:
	// Windows rename fails with sharing violation, POSIX silently
	// re-points the inode leaving the running server on a stale
	// file. Followers must not touch the shared DB at all. Remote
	// `server:db` mode delegates maintenance to whoever owns the
	// remote server.
	if (m_strServer.IsEmpty()
	 && ibFirebirdLeaderMode::CurrentRole() == ibFirebirdLeaderMode::Role::Standalone) {
		ibFirebirdMaintenance::ServiceConnection conn;
		conn.username = m_strUser;
		conn.password = m_strPassword;
		// conn.server stays empty → service_mgr on local host
		ibFirebirdMaintenanceScheduler::Start(m_pInterface, m_strDatabase, conn);
	}

	return true;
}

// close database
bool ibDatabaseLayerFirebird::Close()
{
	// NOTE: maintenance scheduler is intentionally NOT stopped here.
	// ibDatabaseLayerFirebird instances are cloned per pool slot, and
	// the pool open/closes connections constantly — stopping the
	// scheduler on every Close would kill it the first time any
	// connection returns to the pool. Scheduler is a process-wide
	// singleton tied to leader-mode lifetime (Started lazily on first
	// Open in leader/standalone role; stopped via atexit hook
	// registered in MaintenanceScheduler::Start itself).

	CloseResultSets();
	CloseStatements();

	if (m_pDatabase)
	{
		// Roll back any TX still open on this handle so isc_detach_database
		// doesn't reject the disconnect with "active transactions". Errors
		// are intentionally ignored here — we're tearing down the
		// connection and the rollback is best-effort.
		if (m_pTransaction)
		{
			isc_tr_handle pTransaction = m_pTransaction;
			m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
			m_pTransaction = 0;
		}

		isc_db_handle pDatabase = m_pDatabase;
		int nReturn = m_pInterface->GetIscDetachDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase);
		m_pDatabase = 0;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
			return false;
		}
	}

	return true;
}

bool ibDatabaseLayerFirebird::IsOpen()
{
	return (m_pDatabase != 0);
}

// transaction support
void ibDatabaseLayerFirebird::DoBeginTransaction(const ibTxOptions& opts)
{
	ResetErrorCodes();

	// Single reconnect-on-leader-handoff checkpoint per TX boundary.
	// Reattach to the current leader URL if it has changed since our
	// last Open. Cheap on the hot path (URL string compare) when no
	// handoff happened. If reconnect fires, m_pDatabase is the fresh
	// handle below.
	ReconnectIfLeaderChanged();

	if (!m_pDatabase)
		return;

	// TPB layouts (read-committed / no-rec-version, the OES default).
	// Wait vs nowait drives whether SELECT ... WITH LOCK contention
	// blocks or surfaces immediately as a lock-conflict exception —
	// non-blocking acquires (ibTxOptions::noWait) use the latter.
	//
	//   ISOLATION_READ_UNCOMMITTED         = version3, write, wait,   read_committed, rec_version
	//   ISOLATION_READ_COMMITTED           = version3, write, wait,   read_committed, no_rec_version
	//   ISOLATION_REPEATABLE_READ          = version3, write, wait,   concurrency
	//   ISOLATION_SERIALIZABLE             = version3, write, wait,   consistency
	//   ISOLATION_READ_COMMITTED_READ_ONLY = version3, read,  wait,   read_committed, no_rec_version
	//
	// wait-mode TPB also pins a 30-second lock timeout: without it the
	// caller blocks indefinitely on contention (UI hangs, daemons stall).
	// 30 s is long enough that legitimate brief contention resolves and
	// short enough that a stuck peer surfaces as an exception. nowait
	// mode skips the timeout knob — the engine fails immediately anyway.
	// Encoding per FB legacy TPB: tag, length=4, little-endian int32 secs.
	static const std::string isc_tpb_waitMode = {
		isc_tpb_version3, isc_tpb_write, isc_tpb_wait, isc_tpb_read_committed, isc_tpb_no_rec_version,
		(char)isc_tpb_lock_timeout, 4, 30, 0, 0, 0
	};
	static const std::string isc_tpb_nowaitMode = {
		isc_tpb_version3, isc_tpb_write, isc_tpb_nowait, isc_tpb_read_committed, isc_tpb_no_rec_version
	};
	// Read-only mode: read + read_committed + read_consistency (FB 4+).
	// read_consistency makes every statement in this TX see a stable
	// snapshot of committed data without holding the snapshot for the
	// entire TX (vs isc_tpb_concurrency which does). No write-intent
	// locks are acquired, so SELECT-heavy paths don't conflict with
	// concurrent writers. Lock timeout is irrelevant for a read-only TX
	// but kept for symmetry — engine ignores it when no locks taken.
	static const std::string isc_tpb_readOnlyMode = {
		isc_tpb_version3, isc_tpb_read, isc_tpb_wait, isc_tpb_read_committed, (char)isc_tpb_read_consistency,
		(char)isc_tpb_lock_timeout, 4, 30, 0, 0, 0
	};
	const std::string& isc_tpb =
		opts.noWait   ? isc_tpb_nowaitMode :
		opts.readOnly ? isc_tpb_readOnlyMode :
		                isc_tpb_waitMode;

	isc_db_handle pDatabase = m_pDatabase;
	isc_tr_handle pTransaction = 0;

	int nReturn = m_pInterface->GetIscStartTransaction()(
		*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction, 1, &pDatabase,
		isc_tpb.size(), isc_tpb.c_str());

	m_pDatabase = pDatabase;

	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
		return;
	}

	m_pTransaction = pTransaction;
}

void ibDatabaseLayerFirebird::DoCommit()
{
	ResetErrorCodes();

	if (!m_pDatabase || !m_pTransaction)
		return;

	isc_tr_handle pTransaction = m_pTransaction;
	int nReturn = m_pInterface->GetIscCommitTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
	// Whether the commit succeeded or not, FB invalidates the handle —
	// drop our copy so a stray DoRollBack / DoCommit can't double-free.
	m_pTransaction = 0;
	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
	}
}

void ibDatabaseLayerFirebird::DoRollBack()
{
	ResetErrorCodes();

	if (!m_pDatabase || !m_pTransaction)
		return;

	isc_tr_handle pTransaction = m_pTransaction;
	int nReturn = m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
	m_pTransaction = 0;
	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
	}
}

// IsActiveTransaction inherits the base-class default (m_txDepth > 0) —
// the counter on the base is the source of truth and matches the
// drivers that don't have a native handle to probe.


// query database
int ibDatabaseLayerFirebird::DoRunQuery(const wxString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();
	// Proactive leader-handoff check: any caller (TX-bound or direct
	// auto-commit) gets a self-healed connection before the query is
	// dispatched. Hot path is a single cached-string compare against
	// the leader-mode URL — no-op when nothing changed. If a handoff
	// happened mid-TX, this surfaces as an exception, which is exactly
	// what we want — caller must rollback and retry. See header.
	ReconnectIfLeaderChanged();
	if (m_pDatabase != 0)
	{
		wxCharBuffer sqlDebugBuffer = ConvertToUnicodeStream(strQuery);
#ifdef DEBUG
		wxLogDebug(wxT("Running query: \"%s\"\n"), (const char*)sqlDebugBuffer);
#endif // !DEBUG
		wxArrayString QueryArray;
		if (bParseQuery)
			QueryArray = ParseQueries(strQuery);
		else
			QueryArray.push_back(strQuery);

		wxArrayString::iterator start = QueryArray.begin();
		wxArrayString::iterator stop = QueryArray.end();

		long rows = 1;
		if (QueryArray.size() > 0)
		{
			bool bQuickieTransaction = false;

			if (m_pTransaction == 0)
			{
				// If there's no transaction is progress, run this as a quick one-timer transaction
				bQuickieTransaction = true;
			}

			if (bQuickieTransaction)
			{
				BeginTransaction();
				if (GetErrorCode() != DATABASE_LAYER_OK)
				{
					wxLogError(wxT("Unable to start transaction"));
					ThrowDatabaseException();
					return DATABASE_LAYER_QUERY_RESULT_ERROR;
				}
			}

			while (start != stop)
			{
				wxCharBuffer sqlBuffer = ConvertToUnicodeStream(*start);
				isc_db_handle pDatabase = m_pDatabase;
				isc_tr_handle pTransaction = m_pTransaction;
				int nReturn = m_pInterface->GetIscDsqlExecuteImmediate()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase, &pTransaction, GetEncodedStreamLength(*start), (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, NULL);
				m_pDatabase = pDatabase;
				m_pTransaction = pTransaction;
				if (nReturn != 0)
				{
					InterpretErrorCodes();
					// Roll back the in-progress TX. When we own it
					// (bQuickieTransaction), go through the public
					// RollBack so the base class's m_txDepth counter
					// drops to 0 and the pool's TX-pin clears — without
					// this, a failed DDL leaves IsActiveTransaction()
					// true forever and trips checks like
					// OnBeforeSaveDatabase. When the caller owns the
					// TX (bQuickieTransaction == false), driver-level
					// rollback only — caller's own depth is theirs to
					// resolve.
					if (bQuickieTransaction) {
						RollBack();
					} else {
						isc_tr_handle pTr = m_pTransaction;
						m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTr);
						m_pTransaction = 0;
					}

					ThrowDatabaseException();
					return DATABASE_LAYER_QUERY_RESULT_ERROR;
				}
				start++;
			}

			if (bQuickieTransaction)
			{
				Commit();
				if (GetErrorCode() != DATABASE_LAYER_OK)
				{
					ThrowDatabaseException();
					return DATABASE_LAYER_QUERY_RESULT_ERROR;
				}
			}
		}

		return rows;
	}
	else
	{
		wxLogError(wxT("Database handle is NULL"));
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}
}

ibDatabaseResultSet* ibDatabaseLayerFirebird::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();
	// Self-heal after leader handoff before the SELECT — see
	// DoRunQuery for the rationale. Cheap when no handoff happened.
	ReconnectIfLeaderChanged();
	if (m_pDatabase != 0)
	{
		wxCharBuffer sqlDebugBuffer = ConvertToUnicodeStream(strQuery);
#if DEBUG
		wxLogDebug(wxT("Running query: \"%s\""), (const char*)sqlDebugBuffer);
#endif
		wxArrayString QueryArray = ParseQueries(strQuery);

		if (QueryArray.size() > 0)
		{
			bool bQuickieTransaction = false;

			if (m_pTransaction == 0)
			{
				// If there's no transaction is progress, run this as a quick one-timer transaction
				bQuickieTransaction = true;
			}

			if (QueryArray.size() > 1)
			{
				if (bQuickieTransaction)
				{
					BeginTransaction();
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						wxLogError(wxT("Unable to start transaction"));
						ThrowDatabaseException();
						return NULL;
					}
				}

				// Assume that only the last statement in the array returns the result set
				for (unsigned int i = 0; i < QueryArray.size() - 1; i++)
				{
					DoRunQuery(QueryArray[i], false);
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						// Inner DoRunQuery failure — when we own the
						// outer TX (bQuickieTransaction), drop the
						// depth counter via public RollBack so a
						// subsequent caller doesn't see a phantom
						// active transaction. Symmetric to the fix in
						// DoRunQuery's per-statement error path.
						if (bQuickieTransaction) RollBack();
						ThrowDatabaseException();
						return NULL;
					}
				}

				// Now commit all the previous queries before calling the query that returns a result set
				if (bQuickieTransaction)
				{
					Commit();
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						ThrowDatabaseException();
						return NULL;
					}
				}
			} // End check if there are more than one query in the array

			isc_tr_handle pQueryTransaction = NULL;
			bool bManageTransaction = false;
			if (bQuickieTransaction)
			{
				bManageTransaction = true;

				std::string tpbBuffer;
				{
					//tpbBuffer.push_back(isc_tpb_lock_timeout);
					//tpbBuffer.push_back(60);
				}

				isc_db_handle pDatabase = m_pDatabase;
				int nReturn = m_pInterface->GetIscStartTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, 1, &pDatabase, tpbBuffer.size(), tpbBuffer.data());
				m_pDatabase = pDatabase;
				if (nReturn != 0)
				{
					InterpretErrorCodes();
					ThrowDatabaseException();
				}
			}
			else
			{
				pQueryTransaction = m_pTransaction;
			}

			isc_stmt_handle pStatement = NULL;
			isc_db_handle pDatabase = m_pDatabase;
			int nReturn = m_pInterface->GetIscDsqlAllocateStatement()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase, &pStatement);
			m_pDatabase = pDatabase;
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				ThrowDatabaseException();
				return NULL;
			}

			wxCharBuffer sqlBuffer = ConvertToUnicodeStream(QueryArray[QueryArray.size() - 1]);
			nReturn = m_pInterface->GetIscDsqlPrepare()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, &pStatement, 0, (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, NULL);
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				ThrowDatabaseException();
				return NULL;
			}

			//--------------------------------------------------------------

			XSQLDA* pOutputSqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(1));
			pOutputSqlda->sqln = 1;
			pOutputSqlda->version = SQLDA_VERSION1;

			// Make sure that we have enough space allocated for the result set
			nReturn = m_pInterface->GetIscDsqlDescribe()(*(ISC_STATUS_ARRAY*)m_pStatus, &pStatement, SQL_DIALECT_CURRENT, pOutputSqlda);
			if (nReturn != 0)
			{
				free(pOutputSqlda);
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				ThrowDatabaseException();
				return NULL;
			}

			if (pOutputSqlda->sqld > pOutputSqlda->sqln)
			{
				int nColumns = pOutputSqlda->sqld;
				free(pOutputSqlda);
				pOutputSqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(nColumns));
				pOutputSqlda->sqln = nColumns;
				pOutputSqlda->version = SQLDA_VERSION1;
				nReturn = m_pInterface->GetIscDsqlDescribe()(*(ISC_STATUS_ARRAY*)m_pStatus, &pStatement, SQL_DIALECT_CURRENT, pOutputSqlda);
				if (nReturn != 0)
				{
					free(pOutputSqlda);
					InterpretErrorCodes();

					// Manually try to rollback the transaction rather than calling the member RollBack function
					//  so that we can ignore the error messages
					m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

					ThrowDatabaseException();
					return NULL;
				}
			}

			// Create the result set object
			ibDatabaseResultSetFirebird* pResultSet = new ibDatabaseResultSetFirebird(m_pInterface, m_pDatabase, pQueryTransaction, pStatement, pOutputSqlda, true, bManageTransaction);
			pResultSet->SetEncoding(GetEncoding());
			if (pResultSet->GetErrorCode() != DATABASE_LAYER_OK)
			{
				SetErrorCode(pResultSet->GetErrorCode());
				SetErrorMessage(pResultSet->GetErrorMessage());

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				// Swallow any throw from the result-set dtor — we are
				// already on the error path; the original isc_dsql_*
				// failure is what we want the caller to see, not a
				// secondary cleanup exception.
				try { delete pResultSet; } catch (const ibBackendException&) {}

				ThrowDatabaseException();
			}

			// Now execute the SQL
			nReturn = m_pInterface->GetIscDsqlExecute()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, &pStatement, SQL_DIALECT_CURRENT, NULL);
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				// Swallow any throw from the result-set dtor — the
				// isc_dsql_execute failure above is the user-visible
				// error; a secondary cleanup exception would mask it.
				try { delete pResultSet; } catch (const ibBackendException&) {}

				ThrowDatabaseException();
				return NULL;
			}

			//--------------------------------------------------------------

			LogResultSetForCleanup(pResultSet);
			return pResultSet;
		}
		else
			return NULL;
	}
	else
	{
		wxLogError(wxT("Database handle is NULL"));
		return NULL;
	}
}

ibPreparedStatement* ibDatabaseLayerFirebird::DoPrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();
	// Self-heal after leader handoff so the statement is built against
	// the new firebird.exe handle, not the dead one — see DoRunQuery
	// for the rationale.
	ReconnectIfLeaderChanged();

	ibPreparedStatementFirebird* pStatement = ibPreparedStatementFirebird::CreateStatement(m_pInterface, m_pDatabase, m_pTransaction, strQuery, GetEncoding());
	if (pStatement && (pStatement->GetErrorCode() != DATABASE_LAYER_OK))
	{
		SetErrorCode(pStatement->GetErrorCode());
		SetErrorMessage(pStatement->GetErrorMessage());
		wxDELETE(pStatement); // This sets the pointer to NULL after deleting it

		ThrowDatabaseException();
		return NULL;
	}

	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool ibDatabaseLayerFirebird::TableExists(const wxString& table)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = NULL;
	ibDatabaseResultSet* pResult = NULL;
	try {
		wxString tableUpperCase = table.Upper();
		wxString query = wxT("SELECT COUNT(*) FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NULL AND RDB$RELATION_NAME=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, tableUpperCase);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				if (pResult->Next())
				{
					if (pResult->GetResultInt(1) != 0)
					{
						bReturn = true;
					}
				}
			}
		}

		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		if (pStatement != NULL)
		{
			CloseStatement(pStatement);
			pStatement = NULL;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open resources before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != NULL) {
			CloseResultSet(pResult);
			pResult = NULL;
		}
		if (pStatement != NULL) {
			CloseStatement(pStatement);
			pStatement = NULL;
		}
		throw;
	}

	return bReturn;
}

bool ibDatabaseLayerFirebird::ViewExists(const wxString& view)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = NULL;
	ibDatabaseResultSet* pResult = NULL;
	try {
		wxString viewUpperCase = view.Upper();
		wxString query = wxT("SELECT COUNT(*) FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NOT NULL AND RDB$RELATION_NAME=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, viewUpperCase);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				if (pResult->Next())
				{
					if (pResult->GetResultInt(1) != 0)
					{
						bReturn = true;
					}
				}
			}
		}

		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		if (pStatement != NULL)
		{
			CloseStatement(pStatement);
			pStatement = NULL;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open resources before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != NULL) {
			CloseResultSet(pResult);
			pResult = NULL;
		}
		if (pStatement != NULL) {
			CloseStatement(pStatement);
			pStatement = NULL;
		}
		throw;
	}

	return bReturn;
}

wxArrayString ibDatabaseLayerFirebird::GetTables()
{
	wxArrayString returnArray;

	ibDatabaseResultSet* pResult = NULL;
	try {
		wxString query = wxT("SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NULL");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1).Trim());
		}

		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != NULL) {
			CloseResultSet(pResult);
			pResult = NULL;
		}
		throw;
	}

	return returnArray;
}

wxArrayString ibDatabaseLayerFirebird::GetViews()
{
	wxArrayString returnArray;

	ibDatabaseResultSet* pResult = NULL;
	try {
		wxString query = wxT("SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NOT NULL");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1).Trim());
		}

		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != NULL) {
			CloseResultSet(pResult);
			pResult = NULL;
		}
		throw;
	}

	return returnArray;
}

wxArrayString ibDatabaseLayerFirebird::GetColumns(const wxString& table)
{
	// Initialize variables
	wxArrayString returnArray;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = NULL;
	ibDatabaseResultSet* pResult = NULL;
	try {
		wxString tableUpperCase = table.Upper();
		wxString query = wxT("SELECT RDB$FIELD_NAME FROM RDB$RELATION_FIELDS WHERE RDB$RELATION_NAME=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, tableUpperCase);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				while (pResult->Next())
				{
					returnArray.Add(pResult->GetResultString(1).Trim());
				}
			}
		}


		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		if (pStatement != NULL)
		{
			CloseStatement(pStatement);
			pStatement = NULL;
		}
	}
	catch (const ibBackendException&) {
		// Close any still-open resources before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != NULL) {
			CloseResultSet(pResult);
			pResult = NULL;
		}
		if (pStatement != NULL) {
			CloseStatement(pStatement);
			pStatement = NULL;
		}
		throw;
	}

	return returnArray;
}

int ibDatabaseLayerFirebird::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Firebird database error code values to ibDatabaseLayer values
	// For now though, we'll just return the original error code
	return nCode;
}

ibBackendDatabaseException::Kind ibDatabaseLayerFirebird::ClassifyDatabaseError(int nativeCode) const
{
	// Firebird stashes the primary `isc_*` gds code into m_nErrorCode
	// via SetErrorCode(...) at every error path. The full status
	// vector (isc_status[]) carries detail, but the primary code is
	// what callers branch on.
	//
	// Symbolic names from interbase/ibase.h — we use the raw integer
	// values to avoid pulling the FB headers into this classifier.
	// They are part of FB's stable ABI (isc_*. macros are in iberror.h
	// and don't change between minor versions).
	using Kind = ibBackendDatabaseException::Kind;

	switch (nativeCode) {
		// --- ConnectionLost ---
		case 335544721: // isc_network_error
		case 335544722: // isc_net_connect_err
		case 335544723: // isc_net_connect_listen_err
		case 335544724: // isc_net_event_connect_err
		case 335544725: // isc_net_event_listen_err
		case 335544726: // isc_net_read_err
		case 335544727: // isc_net_write_err
		case 335544741: // isc_server_misconfigured
		case 335544856: // isc_att_shutdown
			return Kind::ConnectionLost;

		// --- Deadlock / lock conflict ---
		case 335544336: // isc_deadlock
		case 335544345: // isc_lock_conflict
		case 335544510: // isc_lock_timeout — but FB labels this as a
		                // *conflict* (the wait actually expired); we
		                // surface as Timeout below.
			return Kind::Deadlock;

		// --- Timeout ---
		case 335544855: // isc_cancelled (statement cancelled, often via timeout)
			return Kind::Timeout;

		// --- Constraint violations ---
		case 335544349: // isc_no_dup
		case 335544466: // isc_foreign_key
		case 335544347: // isc_not_valid (CHECK violation)
		case 335544665: // isc_unique_key_violation
		case 335544558: // isc_check_constraint
			return Kind::Constraint;

		// --- Syntax / DSQL parse errors ---
		case 335544343: // isc_dsql_error
		case 336397208: // isc_dsql_command_err
		case 336397210: // isc_dsql_token_unk_err
		case 336003075: // isc_dsql_relation_err
		case 336003085: // isc_dsql_field_err
			return Kind::Syntax;

		default:
			return Kind::Unknown;
	}
}

//wxString ibDatabaseLayerFirebird::TranslateErrorCodeToString(ibInterfaceFirebird* pInterface, int nCode, ISC_STATUS_ARRAY status)
wxString ibDatabaseLayerFirebird::TranslateErrorCodeToString(ibInterfaceFirebird* pInterface, int nCode, void* status)
{
	char szError[512];
	wxString strReturn;

	if (nCode > -901) // Error codes less than -900 indicate that it wasn't a SQL error but an ibase system error
	{
		long* pVector = (long*)status;
		pInterface->GetFbInterpret()(szError, 512, (const ISC_STATUS**)&pVector);

		strReturn = wxString::Format(wxT("%s\n"), szError);
		while (pInterface->GetFbInterpret()(szError, 512, (const ISC_STATUS**)&pVector))
		{
			strReturn += wxString::Format(wxT("%s\n"), szError);
		}

		pInterface->GetIscSqlInterprete()(nCode, szError, sizeof(szError));
		strReturn += wxString::Format(wxT("%s\n"), szError);
	}
	else
	{
		pInterface->GetIscSqlInterprete()(nCode, szError, sizeof(szError));
		wxCharBuffer systemEncoding = wxLocale::GetSystemEncodingName().mb_str();
		strReturn = ibDatabaseStringConverter::ConvertFromUnicodeStream(szError, (const char*)systemEncoding);
	}

	return strReturn;
}

void ibDatabaseLayerFirebird::InterpretErrorCodes()
{
	//wxLogDebug(wxT("ibDatabaseLayerFirebird::InterpretErrorCodes()"));

	long nSqlCode = m_pInterface->GetIscSqlcode()(*(ISC_STATUS_ARRAY*)m_pStatus);
	SetErrorMessage(ibDatabaseLayerFirebird::TranslateErrorCodeToString(m_pInterface, nSqlCode, *(ISC_STATUS_ARRAY*)m_pStatus));
	if (nSqlCode < -900)  // Error codes less than -900 indicate that it wasn't a SQL error but an ibase system error
	{
		SetErrorCode(ibDatabaseLayerFirebird::TranslateErrorCode(*((ISC_STATUS_ARRAY*)m_pStatus)[1]));
	}
	else
	{
		SetErrorCode(ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode));
	}
}

bool ibDatabaseLayerFirebird::ReconnectIfStale()
{
	// Public hook on the base — forwards to our private leader-mode
	// reconnect logic. Callers from outside the FB driver use this
	// generic API; the FB-specific routing stays encapsulated.
	return ReconnectIfLeaderChanged();
}

bool ibDatabaseLayerFirebird::ReconnectIfLeaderChanged()
{
	// Remote `server:db` mode has no leader-mode involvement — caller
	// configured a fixed FB server, not a shared-file path through the
	// orchestrator. Skip.
	if (!m_strServer.IsEmpty())
		return false;

	const wxString currentLeaderUrl = ibFirebirdLeaderMode::CurrentConnectUrl();

	// Empty = leader-mode never initialised (no UNC path → standalone
	// path) or shut down. Nothing to reconnect against.
	if (currentLeaderUrl.IsEmpty())
		return false;

	// URL still matches cache = no handoff happened since our last
	// attach. Hot path; this is the common case on every
	// BeginTransaction call.
	if (currentLeaderUrl == m_currentConnectUrl)
		return false;

	// Hard fail if caller is mid-transaction. Reconnect tears down
	// the FB handle; any uncommitted work on the old leader is
	// lost. Surfacing this as a clean exception is much safer than
	// silently re-pointing the handle and letting the caller commit
	// a half-statement-tx onto the new leader. Caller is expected
	// to catch, rollback their logical TX, and retry from scratch.
	if (m_pTransaction != 0) {
		wxLogError(wxT("ibDatabaseLayerFirebird: leader handoff during ")
		           wxT("active transaction (was %s, now %s) — caller must ")
		           wxT("rollback and retry"),
		           m_currentConnectUrl, currentLeaderUrl);
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Leader handoff during active transaction; "
		                    "transaction state lost. Retry."));
		ThrowDatabaseException();
		return false;
	}

	// Per-pool-clone log — every clone in ibConnectionPool::m_entries
	// hits this when the leader URL changes, so a 20-clone pool would
	// spam 20 identical "handoff detected" lines per cluster event.
	// Cluster-level handoff is already logged once by
	// ibFirebirdLeaderMode's heartbeat thread. Debug only.
	wxLogDebug(wxT("ibDatabaseLayerFirebird: leader handoff detected ")
	           wxT("(was %s, now %s); reconnecting"),
	           m_currentConnectUrl, currentLeaderUrl);

	// Tear down the existing handle — best-effort. If the underlying
	// TCP socket is already dead (leader process gone), Close will
	// fail; we ignore that and march on to the fresh Open. CloseResultSets
	// and CloseStatements inside Close invalidate any caller-held
	// result-set / prepared statement so subsequent use surfaces as
	// "handle invalid" rather than silently reading from the old
	// connection.
	try { Close(); } catch (...) { /* best-effort */ }
	m_pDatabase = 0;
	m_pTransaction = 0;
	m_currentConnectUrl.Clear();

	// Open() re-runs InitForDatabase + attach against whatever URL
	// the leader-mode singleton currently reports. m_strDatabase is
	// unchanged, so leader-mode's per-dbPath cache resolves the same
	// orchestrator state. Catch any exception from Open — caller
	// invoked us from DoBeginTransaction which doesn't expect reconnect
	// to throw; surface as logged failure instead.
	bool opened = false;
	try { opened = Open(); } catch (...) { opened = false; }
	if (!opened) {
		wxLogError(wxT("ibDatabaseLayerFirebird: reconnect against new ")
		           wxT("leader URL %s failed"), currentLeaderUrl);
		return false;
	}
	return true;
}

bool ibDatabaseLayerFirebird::IsAvailable()
{
	bool bAvailable = false;
	ibInterfaceFirebird* pInterface = new ibInterfaceFirebird();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}


