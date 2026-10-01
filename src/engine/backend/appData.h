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
// Session ownership is part of appData's public surface: CreateSession
// hands out an ibSessionHolder. Safe to include here — sessionHolder.h
// only forward-declares ibSession, so it does not pull session.h (which
// includes this header) back in.
#include "backend/session/sessionHolder.h"

#define appData				(ibApplicationInstance::Get())

#define appDataDestroy()	(ibApplicationInstance::DestroyAppDataEnv())

#define db_query  (ibApplicationInstance::GetDatabaseLayer())

// Queryable-source factory — the L4 query engine resolves a source namespace
// (Catalog / Document / a plugin / an external DB) to a queryable through it.
// POINTER (nullptr pre-appData / post-appData), like GetLockManager; null-check it.
#define query_sources (ibApplicationInstance::GetQueryableFactory())

// Audit + trace logger. One per process, lifetime managed by
// ibApplicationInstance. Resolves to nullptr before Init / after Destroy.
#define ibLog     (ibApplicationInstance::GetLogger())

enum ibRunMode {
	eLAUNCHER_MODE = 1,		// for create db, only backmode
	eDESIGNER_MODE = 2,		// backmode + frontmode
	eRUNTIME_MODE = 3,	// backmode + frontmode (thick client)
	eSERVICE_MODE = 4,		// only backmode
	eWEB_RUNTIME_MODE = 5	// backmode + wfrontend (web — wes process)
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
// it through ibApplicationInstance::GetSessionRegistry()->GetClusterSnapshot().

#pragma region user
// ibUserInfo lives in backend/userInfo.h so ibSession can reach it
// without pulling all of appData.h. Carries the full record plus
// nested Brief projection for table-wide listings (replaces the
// historical ibApplicationDataShortUserInfo).
#include "backend/userInfo.h"
#pragma endregion

// ⭐ WHAT OPENING A BASE NEEDS — a shape rather than a growing argument list (Max, 2026-10-01): the opener
// fills what it knows and the rest keeps its default; a new field is one edit, not one per caller. A base's
// own settings are not here: they live in its folder, in infobase.conf (the connections to its DBMS), and the
// process's in backend.conf.
struct BACKEND_API ibInstanceRequest {
	ibRunMode m_runMode = eRUNTIME_MODE;
	wxString  m_name;        // the base's name in the journal; empty — its directory's (file), its database's (server)
	wxString  m_locale;      // empty — the process's
};

struct BACKEND_API ibFileInstanceRequest : ibInstanceRequest {
	wxString  m_directory;   // the base's folder — sys.fdb, its journal and its infobase.conf in it
};

struct BACKEND_API ibServerInstanceRequest : ibInstanceRequest {
	wxString  m_server;
	wxString  m_port;
	wxString  m_user;        // the DBMS's
	wxString  m_password;
	wxString  m_database;
	// The folder this base keeps its local files in — its journal and its infobase.conf — when the opener keeps one
	// (a server keeps each base in a folder of its own). Empty — the user's local data, as before.
	wxString  m_dirLocal;
};

// ONE BASE. A process holds one (every host of today) or several (the application server); the set and what belongs
// to the process rather than to a base live in ibApplicationHost (appHost.h). docs/private/multi-base-process.md
class BACKEND_API ibApplicationInstance {

	// host — the process that holds it (ibApplicationHost): the top of the chain session → registry →
	// base → host.
	ibApplicationInstance(class ibApplicationHost* host, ibRunMode runMode);

	// The one road a base comes up by once its database is open, a file base and a server base alike (the
	// pool, the tables, the locale, the journal, the jobs): the base it now is, or null — refused or thrown, it
	// is closed again. `folder` keeps its infobase.conf.
	static ibApplicationInstance* Open(std::unique_ptr<ibApplicationInstance> opening,
		std::shared_ptr<class ibDatabaseLayer> db, const wxString& folder, const wxString& locale);

	// Its teardown, in order — run by its owner while it is still listed (ibApplicationHost::Close), before it
	// is freed. Idempotent: the destructor runs it again over the empty shell.
	void Close();
	friend class ibApplicationHost;

public:

	class ibApplicationHost* GetHost() const { return m_host; }

	virtual ~ibApplicationInstance();

	// THE CURRENT BASE — THROUGH THE SESSION: the base of the session this thread works for (ibSession::Current),
	// else the base the thread is bound to (ibApplicationInstanceScope — the thread that opened it, a base's own
	// service thread). There is no global one. Null while the process holds no base. A thread that has neither:
	// an exception — or null when it is not `required` (a destructor, a teardown path: they must not throw).
	static ibApplicationInstance* Get(bool required = true);


	///////////////////////////////////////////////////////////////////////////
	static bool CreateAppDataEnv(ibRunMode runMode = ibRunMode::eRUNTIME_MODE);
	///////////////////////////////////////////////////////////////////////////

	// Open a base and ADD it to the process's set — answering the base itself, or null when it did not open (a
	// base that fails half-way closes itself, never the others). A host that opens once holds a set of one.
	static ibApplicationInstance* CreateFileAppDataEnv(const ibFileInstanceRequest& request);
	static ibApplicationInstance* CreateServerAppDataEnv(const ibServerInstanceRequest& request);

	static bool SetLocaleAppDataEnv(const wxString& strLocale = wxT(""));

	// Every base this process holds, newest first, and the process with them.
	static bool DestroyAppDataEnv();
	// …or one of them — the process and the other bases stay.
	static bool DestroyAppDataEnv(ibApplicationInstance* applicationInstance);
	///////////////////////////////////////////////////////////////////////////

	// ------------------------------------------------------------------
	// Phased startup — split of Connect(). Apps compose:
	//
	//   CreateSession()     → holder in hand, row visible in sys_session,
	//                         registry policies fire (DesignerExclusive).
	//   holder->Open(u, p)  → Attach with creds (+ dialog fallback).
	//   new Frame(std::move(holder))  → the window takes ownership.
	//   frame->Show()       → runtime start + the window's own AllowRun.
	//
	// A failed Open keeps the row visible (user sees "login in progress"
	// in admin) until the holder is dropped — and dropping it is what
	// removes the row. Retry loops call Open again on the same holder.
	// ------------------------------------------------------------------

	// Phase 1: registry session — anonymous Connect (no creds yet).
	// Row inserted in sys_session immediately so admin / policies see
	// "someone is logging in". DesignerExclusivePolicy (etc.) fire
	// here before any auth.
	//
	// Returns OWNERSHIP, not a pointer. Nothing in this codebase hands out
	// a bare live session: whoever calls this holds the session's life in
	// its hands until it moves the holder into a real owner (a frame), and
	// an empty holder is the failure case (policy veto, duplicate id).
	ibSessionHolder CreateSession();

	// Typed overload of CreateSession. The caller (enterprise/mainApp.cpp,
	// designer/mainApp.cpp, web code) picks the concrete derived session
	// class (ibGUISession on desktop, ibWebClientSession per web tab, …).
	// Template bodies live in backend/session/sessionRegistry.h (callers
	// that instantiate the typed overload include it) so the registry's
	// CreateSessionWithFactory is visible at instantiation.
	// Flow:
	//   1. m_sessionRegistry->CreateSessionWithFactory runs
	//      EnsureStartedForCreateSession + Connect(req) under a factory
	//      that builds SessionT instead of the plain base.
	//   2. The holder comes back to the caller, which moves it into the
	//      window it builds. An empty holder means Connect failed.
	template<class SessionT>
	ibSessionHolder CreateSession();

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
	ibSessionHolder CreateSession(const wxString& presetGuid,
	                              const wxString& address);

private:
	// Register the session-lifecycle event listeners that drive
	// metadata + per-session runtime bring-up/teardown. Called once
	// from the ctor — listeners stay live for the appData's lifetime.
	void WireSessionEvents();
public:

	// Returns the connection the current thread should use for
	// database work. Resolution order:
	//   1. Thread-local active-TX connection (pool-tracked) — while a
	//      transaction is open, every db_query access on this thread
	//      must land on the same conn.
	//   2. Thread-local current connection set by the innermost live
	//      ibConnectionScope on this thread.
	//   3. Process-wide m_db — the legacy single shared connection
	//      held directly by ibApplicationInstance. Unchanged fallback
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
	static class ibQueryableFactory* GetQueryableFactory() { return GetQueryableFactory(Get()); }
	static class ibQueryableFactory* GetQueryableFactory(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_queryableFactory.get() : nullptr;
	}

	// Process-wide connection pool — the sole owner of every live
	// ibDatabaseLayer. Pool holds the master (opened at Init) as
	// m_source and lazily clones up to maxSize on demand. Init/
	// Shutdown are driven by CreateFile/Server AppDataEnv and
	// DestroyAppDataEnv respectively. Borrowed pointer — lifetime
	// tied to ibApplicationInstance; never null while the base is alive.
	static class ibConnectionPool* GetConnectionPool() { return GetConnectionPool(Get()); }
	static class ibConnectionPool* GetConnectionPool(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_connectionPool.get() : nullptr;
	}

#pragma region execute
	// RunApplication overloads spawn any OES bin (enterprise / designer /
	// appserver / codeRunner / wenterprise-server). Connection flags are
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
		return m_runMode == ibRunMode::eRUNTIME_MODE
			|| m_runMode == ibRunMode::eWEB_RUNTIME_MODE;
	}
	bool WebEnterpriseMode() const { return m_runMode == ibRunMode::eWEB_RUNTIME_MODE; }
	bool ServiceMode() const { return m_runMode == ibRunMode::eSERVICE_MODE; }

	inline wxString GetRunModeDescr(const ibRunMode& mode) const {
		switch (mode)
		{
		case eLAUNCHER_MODE:
			return wxEmptyString;
		case eDESIGNER_MODE:
			return _("Designer");
		case eRUNTIME_MODE:
			return _("Thick client (GUI)");
		case eWEB_RUNTIME_MODE:
			return _("Web server");
		case eSERVICE_MODE:
			return _("Application server");
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

	// The base's name, as its opening request gave it (ibInstanceRequest::m_name) — else the directory of a
	// file base or the name of a server one. Read by the journal.
	wxString GetInstanceName() const { return m_strInstance; }
	const wxString& GetFile() const { return m_strFile; } // file-mode config/db path (VCS working copy root)

	// The platform locale — the PROCESS's (ibApplicationHost), asked here so its readers need not know that.
	wxString GetLocale() const;

#pragma region session

	// Cluster-wide sys_session snapshot — readers go through
	// ibApplicationInstance::GetSessionRegistry()->GetClusterSnapshot()
	// directly (the accessor returns nullptr pre-appData / post-appData;
	// callers MUST null-check). Snapshot type ibSessionSnapshot lives
	// in backend/session/sessionSnapshot.h.

	// Plugins are loaded once for the PROCESS (ibApplicationHost); asked here so its readers need not know that.
	class ibPluginManager* GetPluginManager() const;

#pragma endregion

	// The user's roles, each carrying WHAT IT IS: the id and how to combine it
	// (ibRoleCompositionMode). Both travel together in the sys_user row, so nothing here resolves
	// anything — the membership already says how it is to be compared.
	const std::vector<ibUserInfo::ibUserRole>& GetUserRoleArray() const;

#pragma region language

	wxString GetUserLanguageCode() const;

#pragma endregion

	// Process-level force-exit machinery (m_forceExit, ForceExit,
	// IsForceExit, SetProcessExitHook) was removed — force-exit is now
	// per-session. ibSession::Close(true) sets the session's flag and
	// closes its window without asking; each kind knows what its window
	// is (desktop main frame, web tab, nothing at all on headless). The
	// interpreter loop checks the session's flag, not a global one.
	//
	// wes-specific concern: when the wes process needs to shut down on
	// signal (Ctrl+C, console close), main.cpp wires that path
	// directly — no longer through this class.

	// User-record DB I/O lives on ibUserInfo as static factories
	// (see backend/userInfo.h). ibApplicationInstance is no longer the
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
	// Additive — creates sys_job if missing. The shared last-run clock for
	// scheduled jobs; independent table, not part of TableAlreadyCreated()'s
	// init contract, so existing databases pick it up on next open.
	static void CreateTableJob();
	// Additive — gives sys_job its settings columns (active / schedule) on a base created before
	// jobs had any. Nullable, and a NULL active reads as ON: silence must never switch a job off.
	static void MigrateTableJob();
	static void MigrateTableSession();
	// Additive — creates sys_settings if missing. One row per saved setting,
	// addressed by category + object + name + user. Independent table, not part
	// of TableAlreadyCreated()'s init contract, so existing databases pick it up
	// on next open. See backend/settings/settingsStorage.h.
	static void CreateTableSettings();
	// Additive — creates sys_bytecode_cache if missing. Runs in any
	// runMode after the existing-tables gate, so DBs initialised before
	// AOT cache landed pick the table up on next open. Independent
	// table; not part of TableAlreadyCreated()'s init-contract.
	static void MigrateTableBytecodeCache();

	static bool ClearTableUser();

private:

	class ibApplicationHost* const m_host;

	ibRunMode m_runMode;
	wxString m_strComputer;

	// The name a person calls this base by — written before every journal line about it. Declared before the
	// subsystems below so it dies after them: they still write lines while they are destroyed, and those lines
	// ask for it.
	wxString m_strInstance;

	// Subsystem ownership — order below is THE teardown contract.
	// C++ destroys members in reverse declaration order, so the last
	// field declared dies first; Close() calls the business-level hooks
	// (Stop / UnloadAll / OnDestroy / Shutdown) and then releases the
	// fields itself in that same order, each one null before it dies.
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
	//   5. m_connectionPool   — last to die. Holds the master DB layer
	//                            that every other subsystem might want
	//                            during their own destruction.
	//
	// Declared in REVERSE of the above (first declared = last destroyed):

	// Connection pool — the sole owner of every ibDatabaseLayer in
	// the process. Master (opened at Init) plus lazy clones up to
	// maxSize. `db_query` / GetDatabaseLayer resolve through the
	// pool; ibApplicationInstance keeps no direct DB handle of its own.
	std::unique_ptr<class ibConnectionPool> m_connectionPool;

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
	// the connection pool, the lock manager — everything of this base
	// lives only for the duration of the base. No subsystem
	// has its own static `Instance()`; readers go through the static
	// accessors below, which return nullptr pre-appData and post-appData
	// (no AV, no hidden assert in Release).
	std::unique_ptr<class ibSessionRegistry> m_sessionRegistry;

	// Scheduled + background work. Declared AFTER the registry on purpose:
	// destruction runs in reverse declaration order, and the manager holds
	// session holders whose release goes through the registry — so it must
	// die while the registry is still up. Its own Stop() waits for in-flight
	// runs first, so no worker is left holding a session being dropped.
	std::unique_ptr<class ibJobManager> m_jobManager;

	// The MCP server — the platform answering a machine caller. Declared next to
	// the job manager and for the same reason: it holds a listening thread that
	// works IN THE NAME OF a session, so it must be gone while the registry that
	// owns sessions is still up. Its Stop() joins the thread, so nothing is left
	// mid-exchange against a configuration being torn down.
	std::unique_ptr<class ibMcpServer> m_mcpServer;

	// Saved settings (sys_settings). No external deps on teardown — every call
	// opens its own builder — so its position among the others is free; it stands
	// here because it is read at the same moments jobs are: while a base is open.
	std::unique_ptr<class ibSettingsStorage> m_settingsStorage;

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
	//
	// ⭐ EVERY ACCESSOR HAS TWO FORMS — the CURRENT base's (through the session: Get) and the base NAMED as an
	// argument, for a subsystem that knows its owner and must not ask which base is current: the registry,
	// the job manager, the MCP server, the metadata and the debugger it owns (multi-base-process.md).
	static class ibSessionRegistry* GetSessionRegistry() { return GetSessionRegistry(Get()); }
	static class ibSessionRegistry* GetSessionRegistry(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_sessionRegistry.get() : nullptr;
	}

	// Returns nullptr when no appData is alive. Replaces the historical
	// `ibLockManager::Instance()` Meyers singleton — same nullable shape
	// as the other subsystem accessors.
	static class ibLockManager* GetLockManager() { return GetLockManager(Get()); }
	static class ibLockManager* GetLockManager(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_lockManager.get() : nullptr;
	}

	// Syntax-helper corpus service — the PROCESS's (ibApplicationHost). Null
	// before the locale is settled (the service is constructed there) and
	// after DestroyAppDataEnv. Callers MUST null-check.
	static class ibHelpService* GetHelpService();

	// Active configuration metadata accessor. nullptr in launcher /
	// codeRunner; nullptr before CreateActiveMetaData fires for the
	// first time. The legacy `activeMetaData` macro redirects to
	// `appEnv::ActiveMetaData()` which calls this.
	static class ibMetaDataConfigurationBase* GetActiveMetaData() { return GetActiveMetaData(Get()); }
	static class ibMetaDataConfigurationBase* GetActiveMetaData(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_activeMetaData.get() : nullptr;
	}

	// Fabric — pick subclass by runMode and stash it in m_activeMetaData.
	// Returns false if construction or OnInitialize failed; nullptr modes
	// (launcher) return true with no-op so callers can branch uniformly.
	// Replaces the historical `ibMetaDataConfigurationBase::Initialize`
	// static. The `metaDataCreate(mode, f)` macro routes here.
	static bool CreateActiveMetaData(enum ibRunMode mode, int flags);
	// …on the application data named rather than the current one.
	static bool CreateActiveMetaData(ibApplicationInstance* applicationInstance, enum ibRunMode mode, int flags);
	// (Its tear-down is the base's own Close: OnDestroy, then the field released.)

	// Audit + trace logger. Created during CreateFile/Server AppDataEnv
	// once the DB is open + the connection pool is initialised; destroyed
	// at the top of ~ibApplicationInstance before the registry stops, so
	// teardown writes still find a live sink. Returns nullptr if logger
	// initialisation failed (disk full, no write permission on log dir);
	// callers must null-check.
	static class ibLogger* GetLogger() { return GetLogger(Get()); }
	static class ibLogger* GetLogger(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_logger.get() : nullptr;
	}

	// (The technology journal is deliberately NOT here. It lives ABOVE application data — opened by
	// ibCrashGuard::Install, before a database is chosen or a session made — and `ibJournalInfo(...)` is
	// declared in backend_core.h, so every file already has it. An accessor here would be a
	// middleman that adds nothing and suggests an ownership that does not exist.
	// See backend/diagnostics/journal.h.)

	// Job manager — the schedule and the sessions behind scheduled /
	// background work. Same nullptr-before-and-after contract as the
	// accessors above. See backend/job/jobManager.h and docs/private/job-manager.md.
	static class ibJobManager* GetJobManager() { return GetJobManager(Get()); }
	static class ibJobManager* GetJobManager(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_jobManager.get() : nullptr;
	}

	// The MCP server. Same nullptr-before-and-after contract as the accessors
	// above; it exists from startup but LISTENS only once somebody starts it in
	// the name of a session. See backend/mcp/mcpServer.h.
	static class ibMcpServer* GetMcpServer() { return GetMcpServer(Get()); }
	static class ibMcpServer* GetMcpServer(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_mcpServer.get() : nullptr;
	}

	// Saved settings — the two doors a caller wants are Save / Restore on the
	// storage itself. Same nullptr-before-and-after contract as the accessors
	// above. See backend/settings/settingsStorage.h.
	static class ibSettingsStorage* GetSettingsStorage() { return GetSettingsStorage(Get()); }
	static class ibSettingsStorage* GetSettingsStorage(const ibApplicationInstance* applicationInstance) {
		return applicationInstance != nullptr ? applicationInstance->m_settingsStorage.get() : nullptr;
	}

private:

	// Build the absolute path for the .olg directory:
	//   - file-mode   → <m_strFile>/oeslog
	//   - server-mode → <m_strDirLocal>/oeslog when the opener named the base's folder,
	//                   else <wxStandardPaths::GetUserLocalDataDir>/OES/<server>_<db>/logs
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

	// The base's own folder on this machine, when its opener keeps one (CreateServerAppDataEnv).
	wxString m_strDirLocal;
};

///////////////////////////////////////////////////////////////////////////////
#define user_table				wxT("sys_user")
#define session_table			wxT("sys_session")
#define sequence_table			wxT("sys_sequence")
#define event_table				wxT("sys_event")
#define bytecode_cache_table	wxT("sys_bytecode_cache")
#define lock_table				wxT("sys_lock")
// sys_job — one row per scheduled job, holding the LAST RUN as every process on
// this base sees it. Without it each process keeps its own clock and a job runs
// once per process per interval instead of once per interval.
#define job_table				wxT("sys_job")
// sys_settings — one row per saved setting: a packed runtime value under an
// address of category + object + name + user. What a person arranged survives
// the form that arranged it. See backend/settings/settingsStorage.h.
#define settings_table			wxT("sys_settings")
///////////////////////////////////////////////////////////////////////////////

#endif
