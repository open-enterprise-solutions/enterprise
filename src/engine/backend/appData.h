#ifndef __APP_DATA_H__
#define __APP_DATA_H__

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "backend/backend_core.h"

#define appData				(ibApplicationData::Get())

#define appDataCreateFile(mode,database,locale) (ibApplicationData::CreateFileAppDataEnv(mode,database,locale))
#define appDataCreateServer(mode,server,port,user,password,database,locale) (ibApplicationData::CreateServerAppDataEnv(mode,server,port,user,password,database,locale))

#define appDataDestroy()	(ibApplicationData::DestroyAppDataEnv())

#define db_query  (ibApplicationData::GetDatabaseLayer())

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
enum class ibSessionKind : int;   // defined in backend/session/session.h

// ibSessionSnapshot — cluster-wide sys_session snapshot — moved to
// backend/session/sessionSnapshot.h. Producer is ibSessionRegistry's
// JobRefreshSnapshot; consumers (designer Active Users dialog) read
// it through ibSessionRegistry::Instance().GetClusterSnapshot().

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

#pragma region helpCorpus

	// Syntax-helper corpus (design v5 §3.1 / §3.4).
	//
	// Single corpus per process — the helper reflects the platform locale
	// at startup. Readers grab a snapshot via GetHelpCorpus() (atomic
	// load) and hold the shared_ptr through any operation that uses
	// returned ibHelpEntry pointers; reload (Phase 5 — configuration
	// save) builds a NEW corpus and atomically swaps the published
	// shared_ptr.
	//
	// Invariant: GetHelpCorpus() never returns nullptr. On total load
	// failure the corpus is an empty ibHelpCorpus so the read path
	// stays branch-free.
	std::shared_ptr<const class ibHelpCorpus> GetHelpCorpus() const;

	// Rebuild from disk; atomically swap on success. Phase 1 runs this
	// on the calling thread (typical use: once at startup from InitLocale;
	// Phase 5 will move it onto a worker thread when configuration-save
	// hooks fire reload). Serialised by m_helpReloadMutex — concurrent
	// reload requests collapse to latest-wins.
	//
	// Errors are queryable through the published corpus snapshot:
	//   appData->GetHelpCorpus()->LoadErrors()
	// No parallel error vector on appData itself — keeps the
	// snapshot-and-errors pair atomic by construction (one shared_ptr
	// publish, both surfaces visible together).
	void ReloadHelpCorpus();

#pragma endregion

#pragma region session

	// Cluster-wide sys_session snapshot — readers go through
	// ibSessionRegistry::Instance().GetClusterSnapshot() directly.
	// Snapshot type ibSessionSnapshot lives in
	// backend/session/sessionSnapshot.h.

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

	std::unique_ptr<class ibPluginManager> m_pluginManager;
	// Connection pool — the sole owner of every ibDatabaseLayer in
	// the process. Master (opened at Init) plus lazy clones up to
	// maxSize. `db_query` / GetDatabaseLayer resolve through the
	// pool; ibApplicationData keeps no direct DB handle of its own.
	std::unique_ptr<class ibConnectionPool> m_connectionPool;

	// Session manager (registry). Owned here — created in ctor, destroyed
	// in dtor. Process-wide singleton in practice (appData itself is one),
	// accessed via ibSessionRegistry::Instance() facade or directly via
	// GetSessionRegistry(). Owns the per-session worker pool too — pool
	// is an extension of session-management infrastructure.
	std::unique_ptr<class ibSessionRegistry> m_sessionRegistry;

	// Syntax-helper corpus (design v5 §3.1, §3.4).
	//
	// Mutable `shared_ptr` because reload swaps the snapshot in place.
	// All public access goes through GetHelpCorpus() / ReloadHelpCorpus()
	// which use atomic_load_explicit / atomic_store_explicit — direct
	// member access is forbidden. m_helpReloadMutex serialises the build
	// phase of concurrent reloads.
	mutable std::shared_ptr<const class ibHelpCorpus> m_helpCorpus;
	mutable std::mutex                                m_helpReloadMutex;

	// Internal — runs LoadHelpCorpus, publishes via atomic store. Called
	// once at Init time (with no per-config dir yet) and on every
	// ReloadHelpCorpus() invocation. Holds m_helpReloadMutex.
	void RebuildHelpCorpus();

public:
	class ibSessionRegistry* GetSessionRegistry() const { return m_sessionRegistry.get(); }

private:

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
///////////////////////////////////////////////////////////////////////////////

#endif
