#ifndef __APP_DATA_H__
#define __APP_DATA_H__

#include "backend/appDataCtorToken.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "backend/backend_core.h"

#define appData				(ibApplicationData::Get())

#define appDataCreateFile(mode,database,locale) (ibApplicationData::CreateFileAppDataEnv(mode,database,locale))
#define appDataCreateServer(mode,server,port,user,password,database,locale) (ibApplicationData::CreateServerAppDataEnv(mode,server,port,user,password,database,locale))

#define appDataDestroy()	(ibApplicationData::DestroyAppDataEnv())

#define db_query  (ibApplicationData::GetDatabaseLayer())

// Queryable-source factory — the L4 query engine resolves a source namespace
// (Catalog / Document / a plugin / an external DB) to a queryable through it.
// POINTER (nullptr pre-appData / post-appData), like GetLockManager; null-check it.
#define query_sources (ibApplicationData::GetQueryableFactory())

// Audit + trace logger. One per process, lifetime managed by
// ibApplicationData. Resolves to nullptr before Init / after Destroy.
#define ibLog     (ibApplicationData::GetLogger())

enum ibRunMode {
	eLAUNCHER_MODE = 1,		// for create db, only backmode
	eDESIGNER_MODE = 2,		// backmode + frontmode
	eENTERPRISE_MODE = 3,	// backmode + frontmode (thick client)
	eSERVICE_MODE = 4,		// only backmode
	eWEB_ENTERPRISE_MODE = 5	// backmode + wfrontend (web — wes process)
};

enum ibDatabaseMode {
	eFILE,
	eSERVER,

	eNONE = 1000
};

//////////////////////////////////////////////////////////////////
#define _app_start_default_flag 0x0000
#define _app_start_create_debug_server_flag 0x0080
//////////////////////////////////////////////////////////////////

class BACKEND_API ibDatabaseLayer;
class BACKEND_API ibSession;
class BACKEND_API ibHelpService;  // defined in backend/help/helpService.h
enum class ibSessionKind : int;   // defined in backend/session/session.h

// ibSessionSnapshot — cluster-wide sys_session snapshot — moved to
// backend/session/sessionSnapshot.h. Producer is ibSessionRegistry's
// JobRefreshSnapshot; consumers (designer Active Users dialog) read
// it through ibApplicationData::GetSessionRegistry()->GetClusterSnapshot().

#pragma region config
struct ibApplicationDataConfigInfo {

	bool IsSetLocale() const { return !m_strLocale.IsEmpty(); }

	//Locale info 
	wxString m_strLocale;
};
#pragma endregion 

#pragma region user
// ibUserInfo lives in backend/userInfo.h so ibSession can reach it
// without pulling all of appData.h. Carries the full record plus
// nested Brief projection for table-wide listings (replaces the
// historical ibApplicationDataShortUserInfo).
#include "backend/userInfo.h"
#pragma endregion

// This class is a singleton class.
class BACKEND_API ibApplicationData {

	ibApplicationData(ibRunMode runMode);

public:

	virtual ~ibApplicationData();
	static ibApplicationData* Get() { return s_instance; }


	///////////////////////////////////////////////////////////////////////////
	static bool CreateAppDataEnv(ibRunMode runMode = ibRunMode::eENTERPRISE_MODE);
	///////////////////////////////////////////////////////////////////////////

	static bool CreateFileAppDataEnv(ibRunMode runMode, const wxString& strDirDatabase, const wxString& strLocale = wxT(""));
	static bool CreateServerAppDataEnv(ibRunMode runMode, const wxString& strServer, const wxString& strPort,
		const wxString& strUser = wxT(""), const wxString& strPassword = wxT(""), const wxString& strDatabase = wxT(""), const wxString& strLocale = wxT(""));

	static bool SetLocaleAppDataEnv(const wxString& strLocale = wxT(""));
	static bool DestroyAppDataEnv();
	///////////////////////////////////////////////////////////////////////////

	// Initialize application
	bool InitLocale(const wxString& locale = wxT(""));

	// ------------------------------------------------------------------
	// Phased startup — split of Connect(). Apps compose:
	//
	//   CreateSession()     → ticket visible in sys_session, registry
	//                         policies fire (DesignerExclusive etc.).
	//   Authenticate(u, p)  → ticket.Attach with creds (+ dialog
	//                         fallback on fail).
	//   LoadMetadata(flags) → compile descriptors.
	//   frame->Initialize(session)  → bind + runtime (by session kind).
	//   frame->Show()       → AllowRun fires BeforeStart veto.
	//
	// A failed Authenticate keeps the ticket visible (user sees
	// "login in progress" in admin) until it's explicitly dropped.
	// Retry loops can call Authenticate again without re-creating the
	// session.
	// ------------------------------------------------------------------

	// Phase 1: registry session — anonymous Connect (no creds yet).
	// Row inserted in sys_session immediately so admin / policies see
	// "someone is logging in". DesignerExclusivePolicy (etc.) fire
	// here before any auth. Returns session pointer on success;
	// nullptr if registry Connect failed (policy veto, row-lock dup).
	ibSession* CreateSession();

	// Typed overload of CreateSession. The caller (enterprise/mainApp.cpp,
	// designer/mainApp.cpp, web code) picks the concrete derived session
	// class (ibEnterpriseSession, ibDesignerSession, ibWebClientSession, …).
	// Template bodies live in backend/session/sessionRegistry.h (callers
	// that instantiate the typed overload include it) so the registry's
	// CreateSessionWithFactory is visible at instantiation.
	// Flow:
	//   1. m_sessionRegistry->CreateSessionWithFactory runs
	//      EnsureStartedForCreateSession + Connect(req) under a factory
	//      that builds SessionT instead of the plain base.
	//   2. OnCreateSession() fires on the main (caller) thread for UI-side
	//      setup — GUI sessions create their wx frame here.
	//   3. On OnCreateSession returning false, the session is Close()d so
	//      the registry ticket is dropped and CreateSession returns nullptr.
	template<class SessionT>
	SessionT* CreateSession();

	// Per-tab variant for the wes web frontend. Caller supplies the cookie
	// guid (used as sys_session.session PK + the registry's session id —
	// one identifier across cookie / SessionManager / sys_session row) and
	// the listener address ("host:port", surfaced in admin UI). Kind is
	// fixed at WebClient (per-tab), independent of process run mode.
	// Server() is auto-populated by the registry — it tracks the most
	// recent WebServer-kind session in the process and attaches subsequent
	// non-server sessions to it. Single-session apps never register a
	// WebServer session so their Server() stays null.
	template<class SessionT>
	SessionT* CreateSession(const wxString& presetGuid,
	                        const wxString& address);

private:
	// Register the session-lifecycle event listeners that drive
	// metadata + per-session runtime bring-up/teardown. Called once
	// from the ctor — listeners stay live for the appData's lifetime.
	void WireSessionEvents();
public:

#pragma region config
	// Read setting from file
	void ReadEngineConfig();
#pragma endregion

	// Returns the connection the current thread should use for
	// database work. Resolution order:
	//   1. Thread-local active-TX connection (pool-tracked) — while a
	//      transaction is open, every db_query access on this thread
	//      must land on the same conn.
	//   2. Thread-local current connection set by the innermost live
	//      ibConnectionScope on this thread.
	//   3. Process-wide m_db — the legacy single shared connection
	//      held directly by ibApplicationData. Unchanged fallback
	//      for code that hasn't been migrated to ibConnectionScope.
	//
	// This is what the `db_query` macro expands to. Defined out-of-
	// line in appData.cpp; the TL slots themselves live on
	// ibConnectionPool (see connectionPool.h).
	static std::shared_ptr<ibDatabaseLayer> GetDatabaseLayer();

	// The L4 query-engine source factory (query/queryableFactory.h) — OWNED by appData,
	// mirroring GetLockManager (nullptr pre-appData / post-appData; no static of its own).
	// Its descriptor CONTENTS are registered on metadata open and dropped on close; the
	// factory object lives with appData. Reached via the `query_sources` macro. Present
	// even with no metadata (external sources still resolve).
	static class ibQueryableFactory* GetQueryableFactory() {
		return s_instance != nullptr ? s_instance->m_queryableFactory.get() : nullptr;
	}

	// Process-wide connection pool — the sole owner of every live
	// ibDatabaseLayer. Pool holds the master (opened at Init) as
	// m_source and lazily clones up to maxSize on demand. Init/
	// Shutdown are driven by CreateFile/Server AppDataEnv and
	// DestroyAppDataEnv respectively. Borrowed pointer — lifetime
	// tied to ibApplicationData; never null while s_instance is alive.
	static class ibConnectionPool* GetConnectionPool() {
		return s_instance != nullptr ? s_instance->m_connectionPool.get() : nullptr;
	}

#pragma region execute
	// RunApplication overloads spawn any OES bin (enterprise / designer /
	// daemon / codeRunner / wenterprise-server). Connection flags are
	// emitted in unified `--flag=value` form which every bin's parser
	// accepts.
	// useManifest=true switches to wenterprise-server's bind-handshake
	// protocol: --port=0 --manifest=<tempfile>, poll manifest for the
	// real URL and open the default browser. Returns pid.
	long RunApplication(const wxString& strAppName,
		bool searchDebug = true,
		bool useManifest = false) const;
	long RunApplication(const wxString& strAppName,
		const wxString& strUserName,
		const wxString& strUserPassword,
		bool searchDebug = true,
		bool useManifest = false) const;

	// Append `--port=0 --manifest=<tempfile>` to cmd, spawn detached,
	// poll the manifest for up to 5s and open the default browser at the
	// reported URL. Returns pid on spawn-ok, 0 on failure. Exposed for
	// callers that build the cmd from external connection data (e.g.
	// launcher picks from a saved IB list, not the appData singleton).
	static long SpawnWebServerWithManifest(wxString cmd, bool searchDebug = false);
#pragma endregion

	ibRunMode GetAppMode() const { return m_runMode; }

	bool LauncherMode() const { return m_runMode == ibRunMode::eLAUNCHER_MODE; }
	bool DesignerMode() const { return m_runMode == ibRunMode::eDESIGNER_MODE; }
	bool EnterpriseMode() const {
		return m_runMode == ibRunMode::eENTERPRISE_MODE
			|| m_runMode == ibRunMode::eWEB_ENTERPRISE_MODE;
	}
	bool WebEnterpriseMode() const { return m_runMode == ibRunMode::eWEB_ENTERPRISE_MODE; }
	bool ServiceMode() const { return m_runMode == ibRunMode::eSERVICE_MODE; }

	inline wxString GetRunModeDescr(const ibRunMode& mode) const {
		switch (mode)
		{
		case eLAUNCHER_MODE:
			return wxEmptyString;
		case eDESIGNER_MODE:
			return _("Designer");
		case eENTERPRISE_MODE:
			return _("Thick client (GUI)");
		case eWEB_ENTERPRISE_MODE:
			return _("Web server");
		case eSERVICE_MODE:
			return _("Daemon");
		}
		return wxEmptyString;
	}

	inline wxString GetRunModeDescr() const { return GetRunModeDescr(m_runMode); }

	ibDatabaseMode GetDatabaseMode() const { return m_dbMode; }

	inline wxString GetDatabaseModeDescr(const ibDatabaseMode& mode) const {
		switch (mode)
		{
		case eNONE:
			return wxEmptyString;
		case eFILE:
			return _("File");
		case eSERVER:
			return _("Server");
		}
		return wxEmptyString;
	}

	inline wxString GetDatabaseModeDescr() const { return GetDatabaseModeDescr(m_dbMode); }

	// Verify credentials and install the resolved user onto the current
	// ibSessionScope's ibSession. Single entry point used by both the GUI
	// login dialog (main-thread, scope = the desktop session) and the
	// registry's ProcessAttach (registry-thread, scope = the target tab
	// session). Returns true on successful verification — `outInfo` is
	// filled in that case; the install side runs only when outInfo.IsOk()
	// (open-access mode passes verification with empty info — no install).
	// Returns false on bad creds; outInfo is left untouched.
	bool Login(const wxString& strUserName,
	           const wxString& strUserPassword,
	           ibUserInfo& outInfo);

	// Pure credential check used by Login above. Looks up the user, verifies
	// the password (PBKDF2 with silent MD5→PBKDF2 upgrade via NeedsRehash),
	// fills `outInfo` on success, and does NOT mutate any session state.
	// Returns true for open-access mode too (empty sys_user populating +
	// any creds → pass with outInfo.IsOk()==false). Safe to call from the
	// registry thread without a ibSessionScope. Exposed as a building block
	// so registry's ProcessAttach can short-circuit on bad creds before
	// pinning a session scope.
	bool AuthenticateUser(const wxString& strUserName,
	                      const wxString& strUserPassword,
	                      ibUserInfo& outInfo);

	// Commit side of Login. Writes the resolved user onto the current
	// ibSessionScope's ibSession. `rawPassword` is cached for Designer
	// "Start debugging" re-attach — pass the plain-text the caller received;
	// pass empty to skip the raw-password cache (e.g. token-based flows).
	// No-op when no session is scoped — the caller is in a pre-auth path
	// that has no business installing a user. Most callers should go
	// through Login above instead of invoking this directly.
	void InstallUser(const ibUserInfo& info,
	                 const wxString& rawPassword);

	// Process-wide exclusive (monopoly) mode — true when any session
	// currently holds it. Facade over ibSessionRegistry; out-of-line so
	// this header doesn't have to pull sessionRegistry.h.
	bool ExclusiveMode() const;

	// User-identity accessors — read from the current thread's `ibSession`
	// (per-cookie on web, main user session on desktop). Without an active
	// ibSessionScope the readers see an empty/unauthenticated state — used
	// only by pre-auth bootstrap and standalone tools (codeRunner).
	const wxString& GetUserName()     const;
	const wxString& GetUserPassword() const;
	const ibUserInfo& GetUserInfo() const;

	wxString GetComputerName() const { return m_strComputer; }

	wxString GetLocale() const { return m_locale.GetCanonicalName(); }

#pragma region session

	// Cluster-wide sys_session snapshot — readers go through
	// ibApplicationData::GetSessionRegistry()->GetClusterSnapshot()
	// directly (the accessor returns nullptr pre-appData / post-appData;
	// callers MUST null-check). Snapshot type ibSessionSnapshot lives
	// in backend/session/sessionSnapshot.h.

	class ibPluginManager* GetPluginManager() const { return m_pluginManager.get(); }

#pragma endregion

	const std::vector<ibUserInfo::ibUserRole>& GetUserRoleArray() const;

#pragma region language

	ibGuid   GetUserLanguageGuid() const;
	wxString GetUserLanguageCode() const;

#pragma endregion

	// Process-level force-exit machinery (m_forceExit, ForceExit,
	// IsForceExit, SetProcessExitHook) was removed — force-exit is now
	// per-session. ibSession::Close(true) / RequestForceExit set the
	// session's flag; OnForceExit (overridden on ibGUISession) dispatches
	// the per-kind action (wxTheApp::Exit on desktop GUI; per-tab
	// shutdown on web; no-op on headless). The interpreter loop checks
	// the session's flag, not a global one.
	//
	// wes-specific concern: when the wes process needs to shut down on
	// signal (Ctrl+C, console close), main.cpp wires that path
	// directly — no longer through this class.

	// User-record DB I/O lives on ibUserInfo as static factories
	// (see backend/userInfo.h). ibApplicationData is no longer the
	// gateway to sys_user — call sites use ibUserInfo::Read /
	// ibUserInfo::Save directly.

#pragma region database

	bool LoadDatabase(const wxString& strFullPath);
	bool SaveDatabase(const wxString& strFullPath);

	bool ClearDatabase();

#pragma endregion 

	wxString GetDatabaseDescription();

private:

	// Buffer-level user-table export / import. Per-record serialization
	// lives on ibUserInfo (Serialize / Deserialize); these methods just
	// drive the iteration over the sys_user table.
	bool LoadUserInfoFromBuffer(wxMemoryBuffer& buffer);
	bool SaveUserInfoToBuffer(wxMemoryBuffer& buffer) const;

	wxString ComputeMd5() const;
	wxString ComputeMd5(const wxString& userPassword) const;

	static bool TableAlreadyCreated();
	static void CreateTableUser();
	static void CreateTableSession();
	static void CreateTableEvent();
	static void CreateTableLock();
	static void MigrateTableSession();
	// Additive — creates sys_bytecode_cache if missing. Runs in any
	// runMode after the existing-tables gate, so DBs initialised before
	// AOT cache landed pick the table up on next open. Independent
	// table; not part of TableAlreadyCreated()'s init-contract.
	static void MigrateTableBytecodeCache();

	static bool ClearTableUser();

private:

	static ibApplicationData* s_instance;

	ibRunMode m_runMode;
	wxString m_strComputer;

#pragma region config
	ibApplicationDataConfigInfo m_configInfo;
#pragma endregion

	// Subsystem ownership — order below is THE teardown contract.
	// C++ destroys members in reverse declaration order, so the last
	// field declared dies first. We use that to encode dependencies
	// without any explicit `reset()` in ~ibApplicationData; the dtor
	// only calls the business-level hooks (Stop / UnloadAll / OnDestroy
	// / Shutdown) and then lets default destruction unwind in the
	// safe order.
	//
	// Destruction order (top of stack = destroyed first):
	//   1. m_activeMetaData   — OnDestroy already ran above; its
	//                            polymorphic dtor (Storage→Configuration→
	//                            File→Base) needs db_query for some
	//                            paths, so it goes BEFORE pool / registry.
	//   2. m_sessionRegistry  — Stop() already drained workers; dtor
	//                            cleans up the session vector. Session
	//                            destructors may still want pool.
	//   3. m_logger           — own SQLite handle, no external deps;
	//                            placed here so its writer thread joins
	//                            while everything below is still alive
	//                            for the rare audit-on-shutdown row.
	//   4. m_lockManager      — no external deps.
	//   5. m_pluginManager    — UnloadAll already called Destroy on
	//                            each plugin while host was live; dtor
	//                            just clears the vector.
	//   6. m_connectionPool   — last to die. Holds the master DB layer
	//                            that every other subsystem might want
	//                            during their own destruction.
	//
	// Declared in REVERSE of the above (first declared = last destroyed):

	// Connection pool — the sole owner of every ibDatabaseLayer in
	// the process. Master (opened at Init) plus lazy clones up to
	// maxSize. `db_query` / GetDatabaseLayer resolve through the
	// pool; ibApplicationData keeps no direct DB handle of its own.
	std::unique_ptr<class ibConnectionPool> m_connectionPool;

	// Plugin manager — registry of loaded backend plugins. UnloadAll
	// is called explicitly in ~ibApplicationData so each plugin sees
	// Destroy() while the host is still up.
	std::unique_ptr<class ibPluginManager> m_pluginManager;

	// Long-held pessimistic lock coordinator (sys_lock table). No
	// external dependencies on teardown.
	std::unique_ptr<class ibLockManager> m_lockManager;

	// L4 query-engine source factory (descriptors of how to create queryables).
	// No external deps on teardown; its descriptor contents follow the metadata
	// open/close lifecycle, the object itself lives with appData.
	std::unique_ptr<class ibQueryableFactory> m_queryableFactory;

	// Audit + trace logger. Owns its own SQLite handle (not the pool's).
	// Built after the DB + connection pool are live; declared here so
	// it's destroyed before the registry — its writer thread joins in
	// the dtor and rare "log on close" rows still land before flush.
	std::unique_ptr<class ibLogger> m_logger;

	// Session manager (registry). Owned here — created in ctor, destroyed
	// in dtor. Single coordinator pattern: appData owns the registry,
	// the connection pool, the lock manager, plugin manager — everything
	// process-wide lives only for the duration of appData. No subsystem
	// has its own static `Instance()`; readers go through the static
	// accessors below, which return nullptr pre-appData and post-appData
	// (no AV, no hidden assert in Release).
	std::unique_ptr<class ibSessionRegistry> m_sessionRegistry;

	// Syntax-helper corpus owner. Same ownership shape as logger /
	// lockManager — lives with appData. Lazy-built in InitLocale once
	// the platform locale is settled; null before that.
	std::unique_ptr<class ibHelpService> m_helpService;

	// Active configuration metadata. Polymorphic — concrete subclass
	// (`ibMetaDataConfiguration` for runtime modes,
	// `ibMetaDataConfigurationStorage` for designer) chosen by the
	// fabric `CreateActiveMetaData` based on runMode. nullptr in
	// launcher / codeRunner (no DB-backed metadata). Declared last so
	// reverse-order destruction kills it first — OnDestroy ran already
	// in the dtor above, dtor itself wraps up.
	std::unique_ptr<class ibMetaDataConfigurationBase> m_activeMetaData;

public:
	// Static accessor — returns nullptr when no appData is alive.
	// Mirrors GetConnectionPool's shape. Callers MUST null-check; the
	// signature documents the precondition that this can come up empty.
	// Hot path: backend code that already has an `appData` pointer in
	// scope can short-circuit to `appData->m_sessionRegistry.get()`
	// through the private field — but the public surface is one entry.
	static class ibSessionRegistry* GetSessionRegistry() {
		return s_instance != nullptr ? s_instance->m_sessionRegistry.get() : nullptr;
	}

	// Returns nullptr when no appData is alive. Replaces the historical
	// `ibLockManager::Instance()` Meyers singleton — same nullable shape
	// as the other subsystem accessors.
	static class ibLockManager* GetLockManager() {
		return s_instance != nullptr ? s_instance->m_lockManager.get() : nullptr;
	}

	// Syntax-helper corpus service. Null before InitLocale runs (the
	// service is constructed lazily there, once the platform locale is
	// settled) and after DestroyAppDataEnv. Callers MUST null-check.
	static class ibHelpService* GetHelpService() {
		return s_instance != nullptr ? s_instance->m_helpService.get() : nullptr;
	}

	// Active configuration metadata accessor. nullptr in launcher /
	// codeRunner; nullptr before CreateActiveMetaData fires for the
	// first time. The legacy `activeMetaData` macro redirects to
	// `appEnv::ActiveMetaData()` which calls this.
	static class ibMetaDataConfigurationBase* GetActiveMetaData() {
		return s_instance != nullptr ? s_instance->m_activeMetaData.get() : nullptr;
	}

	// Fabric — pick subclass by runMode and stash it in m_activeMetaData.
	// Returns false if construction or OnInitialize failed; nullptr modes
	// (launcher) return true with no-op so callers can branch uniformly.
	// Replaces the historical `ibMetaDataConfigurationBase::Initialize`
	// static. The `metaDataCreate(mode, f)` macro routes here.
	static bool CreateActiveMetaData(enum ibRunMode mode, int flags);

	// Symmetric tear-down — fires OnDestroy via the polymorphic dtor
	// chain when the unique_ptr resets, drops the ptr. Idempotent (no-op
	// when already null). No public macro for this one — the legacy
	// `metaDataDestroy()` was retired; ~ibApplicationData calls this on
	// shutdown, and external callers go through the function directly.
	static bool DestroyActiveMetaData();

	// Audit + trace logger. Created during CreateFile/Server AppDataEnv
	// once the DB is open + the connection pool is initialised; destroyed
	// at the top of ~ibApplicationData before the registry stops, so
	// teardown writes still find a live sink. Returns nullptr if logger
	// initialisation failed (disk full, no write permission on log dir);
	// callers must null-check.
	static class ibLogger* GetLogger() {
		return s_instance != nullptr ? s_instance->m_logger.get() : nullptr;
	}

private:

	// Build the absolute path for the .olg directory:
	//   - file-mode   → <m_strFile>/oeslog
	//   - server-mode → <wxStandardPaths::GetUserLocalDataDir>/OES/<db>/logs
	// Called from CreateFile/Server AppDataEnv after m_dbMode is set.
	wxString ResolveLogDir() const;

	// Stand up m_logger using ResolveLogDir(). No-op for LAUNCHER mode
	// (no session / no audit surface). Failures are swallowed — the
	// process must keep running even if the log dir is not writable.
	void CreateLogger();

	bool m_connected_to_db = false;
	bool m_created_metadata = false;
	bool m_run_metadata = false;

public:
	// Flags stashed before Authenticate fires the OnFirstConnect
	// listener — listener can't take a flags argument (callback shape is
	// fixed) so the flags ride here instead. Apps that need non-default
	// flags (Enterprise's debug-server flag) write directly before
	// CreateSession + Authenticate.
	int  m_loadMetadataFlags = _app_start_default_flag;
private:

	ibDatabaseMode m_dbMode;

	// FILE ENTRY
	wxString m_strFile;

	// SERVER ENTRY
	wxString m_strServer;
	wxString m_strPort;
	wxString m_strDatabase;

	wxString m_strUser;
	wxString m_strPassword;

	// LOCALE
	wxLocale m_locale;
	int m_locale_lang;
};

///////////////////////////////////////////////////////////////////////////////
#define user_table				wxT("sys_user")
#define session_table			wxT("sys_session")
#define sequence_table			wxT("sys_sequence")
#define event_table				wxT("sys_event")
#define bytecode_cache_table	wxT("sys_bytecode_cache")
#define lock_table				wxT("sys_lock")
///////////////////////////////////////////////////////////////////////////////

#endif
