////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider
//	Description : app info
////////////////////////////////////////////////////////////////////////////

#include "backend/appData.h"

#include <thread>
#include <algorithm>

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/syntaxHelper/helpService.h"
#include "backend/plugin/pluginManager.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/logger/logger.h"
#include "backend/logger/loggerSweep.h"
#include "backend/lock/lockManager.h"
#include "backend/job/jobManager.h"           // ibJobManager (owned via GetJobManager)
#include "backend/mcp/mcpServer.h"            // ibMcpServer (owned via GetMcpServer)
#include "backend/job/platformJobs.h"         // the engine's own jobs, declared when a database opens
#include "backend/settings/settingsStorage.h" // ibSettingsStorage (owned via GetSettingsStorage)

#include "backend/backend_exception.h"        // ibBackendCoreException — a build with no driver says so
#include "backend/utils/passwordHash.hpp"

#include "backend/moduleManager/moduleManager.h"

// databases. The driver headers are INCLUDED UNDER THE SAME GUARD as the code using them,
// and that is load-bearing on MSVC rather than tidiness: these classes are BACKEND_API, i.e.
// __declspec(dllexport) while backend.dll is being built, and an exported class is
// instantiated in FULL by every translation unit that merely sees its definition. So an
// unguarded #include makes this object file demand the driver's whole vtable, constructor and
// destructor even when every use of it is compiled out — which is exactly how a build with
// OES_USE_POSTGRESQL=OFF failed to link (CI, 2026-08-02). SQLite is always embedded, so it
// needs no guard.
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/firebird/firebirdMaintenanceScheduler.h"   // registered by the startup sequence, not by Open
#endif
#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#endif
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/connectionPool.h"

#include "backend/query/queryableFactory.h"   // ibQueryableFactory (owned via GetQueryableFactory)

// GetDatabaseLayer — trivial delegate. The actual priority chain
// lives inside ibConnectionPool (TX > scope TL > primary). Kept here
// so the db_query macro's target stays stable; legacy call sites
// continue to see ibApplicationData::GetDatabaseLayer as the entry
// point even as the pool grows more responsibilities.
std::shared_ptr<ibDatabaseLayer> ibApplicationData::GetDatabaseLayer()
{
	return ibConnectionPool::GetDatabaseLayer();
}

wxString ibApplicationData::ResolveLogDir() const
{
	const wxString sep = wxFileName::GetPathSeparator();
	if (m_dbMode == ibDatabaseMode::eFILE) {
		// Lives next to sys.fdb — admins see logs alongside the base.
		return m_strFile + sep + wxT("oeslog");
	}
	if (m_dbMode == ibDatabaseMode::eSERVER) {
		// Per-user persistent location. %TEMP% would be wiped by
		// Windows disk cleanup; %LOCALAPPDATA% survives reboots and
		// "temp-file cleanup". Until compute-server arrives
		// this is the only place a client's journal lives.
		wxString tag = m_strDatabase;
		if (tag.IsEmpty()) tag = m_strServer;
		if (tag.IsEmpty()) tag = wxT("default");
		// Sanitise — path separators in db names would break Mkdir.
		tag.Replace(wxT("\\"), wxT("_"));
		tag.Replace(wxT("/"),  wxT("_"));
		tag.Replace(wxT(":"),  wxT("_"));
		return wxStandardPaths::Get().GetUserLocalDataDir()
		       + sep + wxT("OES") + sep + tag + sep + wxT("logs");
	}
	return wxEmptyString;
}

void ibApplicationData::CreateLogger()
{
	if (m_runMode == ibRunMode::eLAUNCHER_MODE) return;
	const wxString dir = ResolveLogDir();
	if (dir.IsEmpty()) return;
	try {
		m_logger = std::make_unique<ibLogger>(dir);
	} catch (...) {
		// Best-effort — logger init must not break appData bring-up.
		m_logger.reset();
	}
	// Retention — kick off the daily-sweep thread. Default 90 days.
	// StartDailySweep also runs RunOnce once immediately on entry, so
	// .olg files older than the cutoff are removed before the first
	// 24h tick. Thread is joined inside ~ibLogger.
	if (m_logger) {
		const int retentionDays = 90;
		m_logger->StartDailySweep(retentionDays);
	}
}

//sandbox
#include "metadataConfiguration.h"

// ibSessionSnapshot moved to backend/session/sessionSnapshot.{h,cpp}
// — implementation lives next to ibSessionRegistry that produces it.

namespace {

// Short label for session.opened / session.closed audit rows. Mirrors
// ibSessionKind values; kept local to appData.cpp because the only
// consumer is the listener wiring below.
wxString DescribeSessionKind(ibSessionKind k) {
	switch (k) {
	case ibSessionKind::Launcher:   return wxT("Launcher");
	case ibSessionKind::Designer:   return wxT("Designer");
	case ibSessionKind::Enterprise: return wxT("Enterprise");
	case ibSessionKind::Service:    return wxT("Service");
	case ibSessionKind::WebServer:  return wxT("WebServer");
	case ibSessionKind::WebClient:  return wxT("WebClient");
	case ibSessionKind::BackgroundJob: return wxT("BackgroundJob");
	case ibSessionKind::ScheduledJob:  return wxT("ScheduledJob");
	case ibSessionKind::SystemJob:     return wxT("SystemJob");
	}
	return wxT("Unknown");
}

}   // namespace

///////////////////////////////////////////////////////////////////////////////
//								ibApplicationData
///////////////////////////////////////////////////////////////////////////////

ibApplicationData* ibApplicationData::s_instance = nullptr;

///////////////////////////////////////////////////////////////////////////////

// Pick the worker-pool cap based on run mode. Single-session GUI hosts
// (designer/enterprise/launcher) don't need a pool at all — return 0
// and the registry leaves m_workerPool nullptr. Headless multi-session
// hosts (wes, future oes-server) get 4 × CPU cores up to 32.
static std::size_t PickWorkerCount(ibRunMode runMode)
{
	if (runMode != eWEB_RUNTIME_MODE) return 0;
	const std::size_t hw = std::thread::hardware_concurrency();
	return std::min<std::size_t>(32, std::max<std::size_t>(4, hw * 4));
}

// Pick the connection-pool idle floor based on run mode. Sets two
// things at once: how many conns get pre-warmed at Init (so first
// requests don't pay the Open cost), and the floor below which idle
// shrinking won't go.
//
// Baseline = 2 for GUI: one for the session manager's persistent
// write connection (the registry checks it out at Start and never
// returns it), one for the script thread's actual
// UI work. Server modes scale higher because tab counts amplify both
// concurrency and burst rate; pool grows past minIdle up to maxSize
// under load and shrinks back here on idle timeout.
static std::size_t PickConnectionMinIdle(ibRunMode runMode)
{
	switch (runMode) {
	case eWEB_RUNTIME_MODE: return 4;
	case eSERVICE_MODE:        return 2;
	default:                   return 2;   // designer / enterprise / launcher
	}
}

ibApplicationData::ibApplicationData(ibRunMode runMode) :
	m_runMode(runMode),
	m_strComputer(wxGetHostName()),
	// `unique_ptr(new X(...))` rather than `make_unique` — every
	// subsystem ctor takes an `ib::AppDataCtorToken` and `make_unique`
	// isn't a friend of the token. Direct `new X(...)` works because
	// this TU is `ibApplicationData`'s and can mint the token.
	//
	// Init order matches the declaration order in appData.h —
	// connectionPool first, activeMetaData last. See the ownership
	// block in the header for the destruction contract that this
	// order encodes.
	m_connectionPool(std::unique_ptr<ibConnectionPool>(new ibConnectionPool(ib::AppDataCtorToken{}))),
	m_pluginManager(std::unique_ptr<ibPluginManager>(new ibPluginManager(ib::AppDataCtorToken{}))),
	m_lockManager(std::unique_ptr<ibLockManager>(new ibLockManager(ib::AppDataCtorToken{}))),
	m_queryableFactory(std::unique_ptr<ibQueryableFactory>(new ibQueryableFactory(ib::AppDataCtorToken{}))),
	m_sessionRegistry(std::unique_ptr<ibSessionRegistry>(new ibSessionRegistry(ib::AppDataCtorToken{}, PickWorkerCount(runMode)))),
	m_jobManager(std::unique_ptr<ibJobManager>(new ibJobManager(ib::AppDataCtorToken{}))),
	m_mcpServer(std::unique_ptr<ibMcpServer>(new ibMcpServer(ib::AppDataCtorToken{}))),
	m_settingsStorage(std::unique_ptr<ibSettingsStorage>(new ibSettingsStorage(ib::AppDataCtorToken{}))),
	m_dbMode(ibDatabaseMode::eNONE),
	m_locale_lang(wxLanguage::wxLANGUAGE_UNKNOWN)
{
	// Pick the session access mode from runMode — every Single-session
	// app (enterprise/designer/daemon/codeRunner) gets
	// Single, the web server (wes) gets Server (per-tab + system fallback).
	// Apps no longer need to call SetAccessMode themselves.
	//
	// Drive the registry directly here — the global `appData` pointer is
	// assigned only after this ctor returns, so going through
	// ibSession::SetAccessMode (which routes through ibSessionRegistry::
	// Instance() and asserts on the global) would fire wxASSERT mid-ctor.
	switch (runMode) {
	case eWEB_RUNTIME_MODE:
		m_sessionRegistry->SetAccessMode(ibSession::AccessMode::Shared);
		// Worker pool was allocated by the registry ctor (PickWorkerCount
		// returned a positive cap for this mode) — registry owns its own
		// pool lifecycle, no further setup here.
		break;
	case eLAUNCHER_MODE:
		// Launcher has no session — leave default; Current() returns
		// nullptr until something explicitly binds.
		break;
	default:
		m_sessionRegistry->SetAccessMode(ibSession::AccessMode::Single);
		break;
	}

	// Load everything under <exe-dir>/plugins that exports the OES plugin ABI.
	// Launcher has no script/metadata subsystem so plugins have nothing to hook —
	// skip it there to avoid paying the scan cost on every connection chooser.
	if (runMode != eLAUNCHER_MODE)
		m_pluginManager->LoadAll();

	// Wire session-lifecycle event listeners — drives metadata load on
	// first auth + per-session runtime bring-up + last-auth-out cleanup.
	if (runMode != eLAUNCHER_MODE)
		WireSessionEvents();
}

// Fabric — replaces ibMetaDataConfigurationBase::Initialize. Picks the
// subclass by runMode (the same switch the legacy static used) and
// stashes it under `m_activeMetaData`. Single ownership, polymorphic
// dtor chain on reset.
bool ibApplicationData::CreateActiveMetaData(ibRunMode mode, int flags)
{
	if (s_instance == nullptr)
		return false;
	if (s_instance->m_activeMetaData)
		return false;   // already initialised — caller passes a fresh appData

	// Same dispatch the historical Initialize() did. The metadata
	// subclass ctors are gated on ib::AppDataCtorToken — this TU is
	// ibApplicationData's, so it can mint the token; nobody outside
	// can. Launcher mode has no metadata at all — that's a successful
	// no-op so callers can branch uniformly.
	switch (mode) {
	case eLAUNCHER_MODE:
		return true;
	case eDESIGNER_MODE:
		s_instance->m_activeMetaData.reset(new ibMetaDataConfigurationStorage(ib::AppDataCtorToken{}));
		break;
	default:
		s_instance->m_activeMetaData.reset(new ibMetaDataConfiguration(ib::AppDataCtorToken{}));
		break;
	}

	return s_instance->m_activeMetaData
		? s_instance->m_activeMetaData->OnInitialize(flags)
		: false;
}

bool ibApplicationData::DestroyActiveMetaData()
{
	if (s_instance == nullptr) return true;
	if (s_instance->m_activeMetaData) {
		s_instance->m_activeMetaData->OnDestroy();
	}
	// reset() fires the polymorphic dtor chain → subclass ~Storage /
	// ~Configuration / ~File / ~Base in order.
	s_instance->m_activeMetaData.reset();
	return true;
}

void ibApplicationData::WireSessionEvents()
{
	auto* registry = m_sessionRegistry.get();
	if (registry == nullptr) return;

	// First authenticated session in the process → load metadata skeleton
	// only. CreateRoot / RunDatabase / CompileRoot live in OnAuthenticated
	// (and the designer's manual RunDatabase after the window shows) — this
	// listener is just the one-shot metadata bootstrap.
	//
	// Direct CreateActiveMetaData call — we are inside ibApplicationData
	// already, so the legacy `metaDataCreate(...)` macro indirection
	// (which expands to `ibApplicationData::CreateActiveMetaData(...)`)
	// just adds a step. The macro stays for outside callers.
	registry->OnFirstConnect([this](ibSession* /*s*/) {
		if (m_created_metadata) return;
		if (!CreateActiveMetaData(m_runMode, m_loadMetadataFlags)) return;
		m_created_metadata = true;
	});

	// Every authenticated session (including the first) gets per-session
	// bring-up: thread binding, root mm allocation + compile, runtime start
	// for runtime-enabled modes.
	registry->OnAuthenticated([this](ibSession* s) {
		if (s == nullptr) return;
		// session.opened goes through Audit BEFORE we bind so the row
		// already has session_id resolved from the freshly-attached
		// ibSession::Current() once BindSessionToThread runs below.
		try {
			if (ibLog) {
				ibLog->Audit(wxT("session"), wxT("opened"),
				             wxString::Format(wxT("kind=%s id=%s"),
				                              DescribeSessionKind(s->GetKind()),
				                              s->GetId()));
			}
		} catch (...) {}
		ibSession::BindSessionToThread(s, std::this_thread::get_id());
		auto* registry = m_sessionRegistry.get();
		if (registry && registry->GetFallback() == nullptr) {
			// The first authenticated session becomes what an UNBOUND thread
			// resolves to. No longer gated on Shared mode: identity resolution is
			// now one rule everywhere (ibSession::Current), and a desktop process
			// stopped having a single session the moment jobs and readers took
			// their own. On the desktop this fallback IS the window's session —
			// the same answer the old "hand back the lone map entry" gave, minus
			// the part where it stopped being lone.
			registry->SetFallback(s);
		}
		// A PERSON'S OWN MCP SERVER, read the moment they are let in — the
		// settings are keyed by user, so opening the designer is when "whose
		// server is this" gets its answer. Nothing saved yet is a cold start,
		// not a failure: the defaults stand, and the defaults are off.
		if (m_mcpServer) m_mcpServer->LoadSettings(s);

		// Enable per-session debug context iff this process was started
		// with --debug. Marks the session as debugged so ibProcUnit's
		// breakpoint dispatch + DoDebugLoop's CV wait route through the
		// session's own state instead of the legacy server-singleton.
		if ((m_loadMetadataFlags & _app_start_create_debug_server_flag) != 0)
			registry->EnableDebugForSession(s);
		if (activeMetaData != nullptr) {
			// Root mm is allocated by ibSession::EnsureRoot (called by the
			// registry between OnFirstConnect and this listener) for runtime
			// sessions; the Designer has no root mm (it uses the lightweight
			// designer manager in the compile cache). Here we drive cross-process
			// metadata bring-up (RunDatabase once per process — fires OnBefore/
			// After RunMetaObject which populate ibCompileValueCache +
			// ibModuleStorage) and per-session compile + runtime start.
			if (!m_run_metadata) {
				m_run_metadata = activeMetaData->RunDatabase();
			}
			// CompileRoot folds compile + AttachRuntime + lambda runtime wire-up.
			// No-op when there's no root mm (Designer). AttachRuntime self-gates by
			// session kind, so no explicit runMode check needed here.
			s->CompileRoot();
		}
	});

	// Per-session teardown — runs on the registry thread inside
	// ProcessRemove while the session is in Stopping state. UnbindSession
	// (by pointer, not thread id) erases bindings regardless of which
	// thread originally pinned them.
	registry->OnDisconnect([this](ibSession* s) {
		if (s == nullptr) return;
		// Capture session.closed BEFORE UnbindSession + DestroyRoot —
		// ibSession::Current() still resolves to `s` so the audit row
		// carries the right session_id / user_name. After UnbindSession
		// the user identity is gone.
		try {
			if (ibLog) {
				ibLog->Audit(wxT("session"), wxT("closed"),
				             wxString::Format(wxT("kind=%s id=%s"),
				                              DescribeSessionKind(s->GetKind()),
				                              s->GetId()));
			}
		} catch (...) {}
		if (auto* mm = s->GetManagerModule())
			mm->DetachRuntime(s);
		s->DestroyRoot();
		ibSession::UnbindSession(s);
		auto* registry = m_sessionRegistry.get();
		if (registry && registry->GetFallback() == s)
			registry->ClearFallback();
	});

	// Last authenticated session out — unload metadata so the process is
	// back to a sessionless state (refcount = 0). Then request process
	// exit; the keep-alive predicate (web tabs, etc.) can decline.
	registry->OnLastDisconnect([this]() {
		if (m_run_metadata) {
			const bool isConfigOpen = activeMetaData != nullptr && activeMetaData->IsConfigOpen();
			if (isConfigOpen)
				activeMetaData->CloseDatabase(forceCloseFlag);
		}
		m_created_metadata = false;
		m_run_metadata = false;
		// EndJob and other "no more work" paths route here. ForceExit
		// goes through ProcessExitHook (wes' main → svr->stop()) or
		// wxTheApp->Exit (desktop), with ShouldKeepAlive declining when
		// non-debug clients are still live.
		if (auto* reg = ibApplicationData::GetSessionRegistry()) {
			if (!reg->ShouldKeepAlive())
				reg->CloseAll(true);
		}
	});
}

ibApplicationData::~ibApplicationData()
{
	// Business-action sequence — every subsystem with a pre-dtor hook
	// (Stop / UnloadAll / OnDestroy / Shutdown) gets it called here in
	// the order that matches the declaration-order destruction below.
	//
	// We do NOT reset() any unique_ptr field; the compiler-generated
	// destruction sweeps fields in reverse declaration order, and the
	// order was chosen (see appData.h ownership block) so that every
	// field's dtor finds its dependencies still alive.
	//
	// 0. JOBS FIRST — everything this process started on its own goes down before
	//    anything it depends on. Stop() ends the tick, waits out every scheduled
	//    run and cancels every background run, so by the time the metadata, the
	//    registry and the connection pool come down below, nothing of ours is
	//    still executing against them.
	//
	//    This ordering is the whole reason the subsystems underneath need no
	//    "is it still there?" guards: a job cannot be mid-Services-API-call while
	//    the driver is freed, because it is already finished. History — an earlier
	//    shape let a detached maintenance worker outlive the pool shutdown and
	//    dereference the freed interface (EIP=0xdddddddd in
	//    WaitForServiceCompletion, 2026-05-26 and -05-29); that thread is gone,
	//    and this order is what keeps its replacement honest.
	if (m_jobManager) m_jobManager->Stop();

	// Same reasoning one line up: the listener works in the name of a session and
	// touches metadata on every exchange, so it is joined BEFORE anything it can
	// reach starts going away.
	if (m_mcpServer) m_mcpServer->Stop();

	// 1. activeMetaData — OnDestroy may save state, close compile
	//    caches, run cascading detach; those paths still want db_query.
	if (m_activeMetaData) m_activeMetaData->OnDestroy();

	// 2. Stop the registry — submits Remove@Urgent for every session
	//    still in m_own, drains the queue, joins the worker. sys_session
	//    DELETEs + OnDisconnect listeners fire before pool dies.
	if (m_sessionRegistry) m_sessionRegistry->Stop();

	// 3. Plugins see Destroy() while the host is still alive. The
	//    registry's vector clears here too — but m_pluginManager's
	//    own dtor will fire later via the reverse-declaration sweep.
	if (m_pluginManager) m_pluginManager->UnloadAll();

	// 4. Pool — close master + clones, invalidate outstanding hand-outs.
	//    Runs AFTER the registry's Stop so any session-bound DB work
	//    has already completed.
	if (m_connectionPool) m_connectionPool->Shutdown();

	// From here the default destructor unwinds in reverse declaration
	// order: m_activeMetaData → m_helpService → m_jobManager →
	// m_sessionRegistry → m_logger → m_lockManager → m_pluginManager →
	// m_connectionPool. Each step is a normal unique_ptr<T>::~unique_ptr()
	// that fires the polymorphic dtor chain on T — no explicit reset()
	// needed. (The job manager's own dtor calls Stop() again; it is
	// idempotent, so the explicit step above only fixes the ORDER.)
	//
	// Nothing may report "what survived" from HERE: the sweep above happens
	// after this body returns, so the metadata tree is still fully alive at
	// this point. The live-object report therefore sits in DestroyAppDataEnv,
	// past the delete. (Measured 2026-07-30: reporting here printed 510 live
	// property objects that the sweep then destroyed to zero.)
}


// ---------------------------------------------------------------------------
// User-identity accessors — route through the current thread's ibSession
// (per-cookie on web, main user session on desktop). Without an active
// ibSessionScope (pre-auth bootstrap, standalone codeRunner) we return a
// shared empty sentinel — readers see an unauthenticated state, callers
// that depend on a real user must arrange a session scope first.
// ---------------------------------------------------------------------------

const ibUserInfo& ibApplicationData::GetUserInfo() const
{
	if (auto* ctx = ibSession::Current())
		return ctx->GetUserInfo();
	static const ibUserInfo s_empty;
	return s_empty;
}

bool ibApplicationData::ExclusiveMode() const
{
	// Process-wide query through the registry — answers "is anyone in
	// exclusive mode right now?". Cluster-aware variant adds the
	// sys_session.exclusive snapshot in a follow-up.
	return m_sessionRegistry != nullptr && m_sessionRegistry->HasExclusiveSession();
}

const wxString& ibApplicationData::GetUserName() const
{
	return GetUserInfo().m_strUserName;
}

const wxString& ibApplicationData::GetUserPassword() const
{
	// Historical quirk: GetUserPassword returned fullName — kept for
	// source compat with call sites that still use the alias.
	return GetUserInfo().m_strUserFullName;
}

const std::vector<ibUserInfo::ibUserRole>&
ibApplicationData::GetUserRoleArray() const
{
	return GetUserInfo().m_roleArray;
}

ibGuid ibApplicationData::GetUserLanguageGuid() const
{
	return GetUserInfo().m_strLanguageGuid;
}

wxString ibApplicationData::GetUserLanguageCode() const
{
	return GetUserInfo().m_strLanguageCode;
}

wxString ibApplicationData::ComputeMd5() const
{
	return ComputeMd5(GetUserInfo().m_strUserPassword);
}

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::CreateAppDataEnv(ibRunMode runMode)
{
	if (s_instance != nullptr) s_instance->DestroyAppDataEnv();
		s_instance = new ibApplicationData(runMode);
		s_instance->ReadEngineConfig();

		if (!SetLocaleAppDataEnv())
			return false;
		return true;
}

#define sys_db wxT("sys.fdb")

bool ibApplicationData::CreateFileAppDataEnv(ibRunMode runMode, const wxString& strDirDatabase, const wxString& strLocale)
{
	if (s_instance != nullptr) s_instance->DestroyAppDataEnv();
#ifndef OES_USE_FIREBIRD
	// ⭐⭐ A BUILD WITHOUT THE DRIVER MUST SAY SO — this used to be a bare `return false`.
	//
	// A file base IS a Firebird base (sys.fdb), so with the driver left out there is nothing to
	// open. But the caller reports what it is GIVEN, and it was given nothing: no exception, no
	// error chain, no code. The startup dialog then said "the failure carried no description" —
	// which is true and useless, because the reason is not a runtime failure at all. It is a
	// property OF THE BUILD, known before the program ran.
	//
	// It cost a day. Release binaries were shipped for months with every OES_USE_* macro dropped
	// (the Release ItemDefinitionGroups had lost `%(PreprocessorDefinitions)`, so nothing was
	// inherited from ConfigurationDefs.props) — and the only symptom anyone could see was an
	// infobase that would not open, with no reason given, in Release but never in Debug.
	ibBackendCoreException::Error(
		_("This build has no Firebird driver (OES_USE_FIREBIRD is not defined), and a file infobase is a Firebird one."));
	return false;
#else
		std::shared_ptr<ibDatabaseLayerFirebird> db(new ibDatabaseLayerFirebird());

		wxString pathSep = wxFileName::GetPathSeparator();
		if (db->Open(strDirDatabase + pathSep + sys_db)) {
			s_instance = new ibApplicationData(runMode);
			s_instance->m_strFile = strDirDatabase;

			s_instance->ReadEngineConfig();

			s_instance->m_dbMode = ibDatabaseMode::eFILE;

			// ⭐ THE APPLICATION SAYS WHAT IT IS, into the journal that has been waiting for it since
			// the process began. The journal's own banner can only greet — the binary, the build, the
			// machine — because at that moment nothing has been decided yet. WHICH base, opened HOW,
			// under WHICH run mode is the first fact worth knowing about a session, and this is the
			// first moment it exists.
			ibJournalInfo(wxT("appdata"), wxT("connected: file base at %s (run mode %d)"),
				s_instance->m_strFile, static_cast<int>(runMode));

			// The pool is the single owner of every connection in the
			// process. `db` here is the master — the pool holds it as
			// m_source for Clone() and also as the first idle entry so
			// the earliest Checkout hands it out directly. Size=32 —
			// slack over ~20 concurrent User sessions. minIdle picked
			// per runMode (server pre-warms; GUI doesn't). Beyond
			// minIdle clones grow lazily and shrink on idle timeout.
			s_instance->m_connectionPool->Init(db, 32, PickConnectionMinIdle(runMode));

			if (runMode == ibRunMode::eDESIGNER_MODE && !ibApplicationData::TableAlreadyCreated()) {
				ibApplicationData::CreateTableSession();
				ibApplicationData::CreateTableUser();
				ibApplicationData::CreateTableEvent();
				ibApplicationData::CreateTableLock();
			}
			else if (!ibApplicationData::TableAlreadyCreated()) {
				DestroyAppDataEnv();
				return false;
			}

			// Additive migration for pre-2026-04-20 schemas — registry's
			// INSERT / snapshot SELECT assume pid / address / currentActivity.
			ibApplicationData::MigrateTableSession();
			ibApplicationData::MigrateTableBytecodeCache();
			// sys_lock — long-held pessimistic lock table. Idempotent
			// CREATE so existing DBs pick it up on startup.
			ibApplicationData::CreateTableLock();
			// sys_job — the shared last-run clock.
			ibApplicationData::CreateTableJob();
			// … and its settings columns, for a base created before jobs had any.
			ibApplicationData::MigrateTableJob();
			// sys_settings — what people saved on their forms and their lists.
			ibApplicationData::CreateTableSettings();

			if (!SetLocaleAppDataEnv(strLocale))
				return false;

			// Audit + trace logger — built after pool + tables so the
			// first Audit row (session.opened) can fire on the next
			// Authenticate.
			s_instance->CreateLogger();

			// ⚠⚠ DECLARED AFTER ITS TABLE EXISTS, and that is the whole point of the position.
			//
			// The platform's own scheduled work is declared HERE, not by each host: a database is
			// open, so the jobs have something to be about, and every host that opens one gets the
			// same list without repeating it in its own main. Declaring is cheap — no session, no
			// metadata; a job builds those on its first run.
			//
			// But declaring is NOT read-free: Register() asks sys_job for a stored schedule so a
			// setting made in the Designer survives, and seeds a row when there is none. This call
			// used to stand ~25 lines ABOVE, before CreateTableJob — while the comment there claimed
			// the table was "created before the platform's jobs are declared below". The comment
			// described the INTENDED order and the code did the other one.
			//
			// It was not an old-database problem. A base created from scratch has no sys_job at this
			// point either, so EVERY first run of enterprise.exe raised "Table unknown SYS_JOB" out
			// of CreateFileAppDataEnv and never reached a window.
			ibRegisterPlatformJobs();

			// …and the Firebird driver's OWN maintenance, for the same reason and one layer deeper:
			// it used to declare itself from inside ibDatabaseLayerFirebird::Open — before this
			// object exists, before the pool is up, before sys_job is created. WHETHER this base is
			// ours to maintain is the driver's answer; WHEN to act on it is this sequence's.
			if (db->IsLocalMaintenanceEligible())
				ibFirebirdMaintenanceJob::Register();

			return true;
		}
		return false;
#endif
}

bool ibApplicationData::CreateServerAppDataEnv(ibRunMode runMode, const wxString& strServer, const wxString& strPort,
	const wxString& strUser, const wxString& strPassword, const wxString& strDatabase, const wxString& strLocale)
{
	if (s_instance != nullptr) s_instance->DestroyAppDataEnv();
#ifndef OES_USE_POSTGRESQL
	// The server base is PostgreSQL — same silence, same reason, same cure as the file base above.
	// A build missing this driver would otherwise refuse every server connection with no cause
	// given, and the search would go to the network and the credentials, where nothing is wrong.
	ibBackendCoreException::Error(
		_("This build has no PostgreSQL driver (OES_USE_POSTGRESQL is not defined), and a server infobase is a PostgreSQL one."));
	return false;
#else
		std::shared_ptr<ibDatabaseLayerPostgres> db(new ibDatabaseLayerPostgres());
		if (db->Open(strServer, strPort, strDatabase, strUser, strPassword)) {

			s_instance = new ibApplicationData(runMode);

			s_instance->m_strServer = strServer;
			s_instance->m_strPort = strPort;
			s_instance->m_strUser = strUser;
			s_instance->m_strPassword = strPassword;
			s_instance->m_strDatabase = strDatabase;

			s_instance->ReadEngineConfig();

			s_instance->m_dbMode = ibDatabaseMode::eSERVER;

			// …and the same for a server base — see the note at the file-mode site above.
			ibJournalInfo(wxT("appdata"), wxT("connected: server base %s on %s (run mode %d)"),
				s_instance->m_strDatabase, s_instance->m_strServer, static_cast<int>(runMode));

			// Pool owns every connection. `db` becomes the master (source
			// for Clone + first hand-out). maxSize=32 covers heartbeat +
			// metadata watcher + ~20 concurrent sessions/SSE + slack.
			// minIdle picked per runMode (server pre-warms; GUI doesn't).
			// Beyond minIdle clones grow lazily and shrink on idle timeout.
			s_instance->m_connectionPool->Init(db, 32, PickConnectionMinIdle(runMode));

			if (!SetLocaleAppDataEnv(strLocale))
				return false;

			if (runMode == ibRunMode::eDESIGNER_MODE && !ibApplicationData::TableAlreadyCreated()) {
				ibApplicationData::CreateTableSession();
				ibApplicationData::CreateTableUser();
				ibApplicationData::CreateTableEvent();
				ibApplicationData::CreateTableLock();
			}
			else if (!ibApplicationData::TableAlreadyCreated()) {
				DestroyAppDataEnv();
				return false;
			}

			ibApplicationData::MigrateTableSession();
			ibApplicationData::MigrateTableBytecodeCache();
			// ⚠ THE SAME STARTUP TABLES AS THE FILE BRANCH, and they were missing here entirely.
			// Idempotent CREATEs, so an existing base picks them up on the next start — which is
			// exactly the reasoning the file branch already carries. sys_lock was created only
			// inside the designer-on-a-fresh-base arm above, and sys_job was never created on this
			// path at all, while the job registration below reads it.
			ibApplicationData::CreateTableLock();
			ibApplicationData::CreateTableJob();
			ibApplicationData::MigrateTableJob();
			// sys_settings — what people saved on their forms and their lists.
			ibApplicationData::CreateTableSettings();

			// LAST IN THE TABLE BLOCK, for the reason spelled out in the file branch: declaring a
			// job READS sys_job, so nothing may declare one before every table it touches exists.
			// The platform's job list belongs to whoever opened the database, not to whichever
			// executable did it.
			ibRegisterPlatformJobs();

			// Audit + trace logger. In server-mode the log directory
			// (…\OES\<tag>\logs, holding oes_YYYY_MM.olg)
			// lives under %LOCALAPPDATA% — every client of this PG/FB
			// server keeps its own journal locally until compute-server
			// arrives.
			s_instance->CreateLogger();

			return true;
		}
		return false;
#endif
}

bool ibApplicationData::SetLocaleAppDataEnv(const wxString& strLocale)
{
	if (s_instance == nullptr)
		return false;

	return s_instance->InitLocale(strLocale);
}

bool ibApplicationData::DestroyAppDataEnv()
{
	if (s_instance != nullptr && s_instance->m_connectionPool != nullptr &&
	    s_instance->m_connectionPool->IsInitialised()) {

		// The active session itself is closed by its holder
		// (mainApp/webSession) via ibSession::Close(), which clears the
		// session's own cached creds; ~ibApplicationData runs Stop() to
		// drain pending Removes when s_instance is destroyed below.
		s_instance->m_connected_to_db = false;

		s_instance->m_strServer = wxEmptyString;
		s_instance->m_strPort = wxEmptyString;
		s_instance->m_strUser = wxEmptyString;
		s_instance->m_strPassword = wxEmptyString;
		s_instance->m_strDatabase = wxEmptyString;

		// Pool shutdown is now driven by ~ibApplicationData (RAII) —
		// see the dtor for the ordering rationale.
		wxDELETE(s_instance);

		// The ibPropertyObject live-register used to be printed here. It answered its question —
		// "none alive, clean teardown" — and the answer is now kept by something cheaper and
		// wider: the CRT exit dump is empty (docs/engineering-playbook/25-memory-leaks.md), so a
		// property object that outlives teardown shows up there by itself, named, with the
		// tracker able to produce its stack. A mutex and a set on every construction, in every
		// Debug run, to re-answer a settled question was the wrong trade.

		// Last: hand the string pool's free list back. It is a cache, but the CRT cannot tell a
		// cache from a leak — every cached block sits in the exit dump holding its old contents,
		// which is where the "leaked" fragments of metadata names came from. Draining here, past
		// the metadata tree and every session, leaves the dump saying only what actually leaked.
		// Per-module and per-thread (see the note on Drain), so this covers the backend's main
		// thread — the one that churns strings.
		ibFStringPool::Drain();

		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////

#include <wx/fileconf.h>
#include <wx/stdpaths.h>

bool ibApplicationData::InitLocale(const wxString& locale)
{
	if (m_locale_lang == wxLanguage::wxLANGUAGE_UNKNOWN) {

#ifdef DEBUG_TRANSLATE
		wxLog::AddTraceMask(wxS("i18n"));
#endif // WXDEBUG

		m_locale_lang = wxLocale::GetSystemLanguage();
		if (m_locale_lang == wxLanguage::wxLANGUAGE_UNKNOWN)
			m_locale_lang = wxLanguage::wxLANGUAGE_DEFAULT;

		if (!locale.IsEmpty()) {
			const wxLanguageInfo* foundedLocale = wxLocale::FindLanguageInfo(locale);
			if (foundedLocale != nullptr)
				m_locale_lang = foundedLocale->Language;
		}
		else if (m_configInfo.IsSetLocale()) {
			const wxLanguageInfo* foundedLocale = wxLocale::FindLanguageInfo(m_configInfo.m_strLocale);
			if (foundedLocale != nullptr)
				m_locale_lang = foundedLocale->Language;
		}

		// Independently of whether we succeeded to set the locale or not, try
		// to load the translations (for the default system language) here.

		const wxString& workingDir = wxGetCwd();

		// normally this wouldn't be necessary as the catalog files would be found
		// in the default locations, but when the program is not installed the
		// catalogs are in the build directory where we wouldn't find them by
		// default

		wxFileName fn(wxStandardPaths::Get().GetExecutablePath());

		wxLocale::AddCatalogLookupPathPrefix(workingDir + wxFILE_SEP_PATH + wxT("lang"));
		wxLocale::AddCatalogLookupPathPrefix(fn.GetPath() + wxFILE_SEP_PATH + wxT("lang"));
#if defined(__WXOSX__) || defined(__APPLE__)
		// On macOS, also check outside .app bundle
		wxFileName bundleLang(fn.GetPath());
		bundleLang.RemoveLastDir(); bundleLang.RemoveLastDir(); bundleLang.RemoveLastDir();
		wxLocale::AddCatalogLookupPathPrefix(bundleLang.GetPath() + wxFILE_SEP_PATH + wxT("lang"));
#endif

		if (!m_locale.Init(m_locale_lang)) {
			if (!m_locale.Init(wxLanguage::wxLANGUAGE_ENGLISH))
				return false;
			m_locale_lang = wxLanguage::wxLANGUAGE_ENGLISH;
		}

		// Initialize the catalogs we'll be using.
		m_locale.AddCatalog(wxT("open_es"));

		// Initialize localization engine
		ibBackendLocalization::SetUserLanguage(m_locale.GetName());

		//Set default time
		wxDateTime::SetCountry(wxDateTime::Country::Country_Default);

		// Syntax-helper corpus comes up here — locale is settled.
		m_helpService.reset(new ibHelpService(ib::AppDataCtorToken{}, m_locale.GetCanonicalName()));

		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Phased startup (split of legacy Connect). Apps compose the phases;
// runtime start is NOT here — it's driven from the session owned by the
// app's main frame (the window is built around the holder). Connect() stays
// as a convenience wrapper for callers without inter-phase hooks
// (codeRunner, daemon, tests).
// ---------------------------------------------------------------------------


#pragma region config

#define BACKEND_CONF wxT("backend.conf")

void ibApplicationData::ReadEngineConfig()
{
	const wxString& workingDir = wxGetCwd(); wxString strConfigFile;
	if (wxFileName::FileExists(workingDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
		strConfigFile = workingDir +
			wxFILE_SEP_PATH + BACKEND_CONF;
	}
	else {
		wxFileName fn(wxStandardPaths::Get().GetExecutablePath());
		wxString exeDir = fn.GetPath();
		if (wxFileName::FileExists(exeDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
			strConfigFile = exeDir + wxFILE_SEP_PATH + BACKEND_CONF;
		}
#if defined(__WXOSX__) || defined(__APPLE__)
		// On macOS, exe is inside .app/Contents/MacOS/ — check 3 levels up
		else {
			wxFileName bundlePath(exeDir);
			bundlePath.RemoveLastDir(); // MacOS
			bundlePath.RemoveLastDir(); // Contents
			bundlePath.RemoveLastDir(); // .app
			wxString bundleDir = bundlePath.GetPath();
			if (wxFileName::FileExists(bundleDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
				strConfigFile = bundleDir + wxFILE_SEP_PATH + BACKEND_CONF;
			}
		}
#endif
	}

	wxFileConfig fc(wxT(""), wxT(""), wxT(""), strConfigFile);
	fc.Read(wxT("Locale"), &m_configInfo.m_strLocale);

	// (THE MCP SERVER'S SETTINGS ARE NOT HERE. They belong to a PERSON in a
	//  BASE — the server is started from an authenticated designer session and
	//  has nothing to say before one exists — so they live in sys_settings with
	//  every other saved setting, under their own category. See
	//  ibMcpServer::LoadSettings. A copy in this file would be a second road,
	//  and the two would disagree the first time somebody edited the page.)
}

#pragma endregion 

///////////////////////////////////////////////////////////////////////////////
#include "backend/debugger/debugClient.h"

#pragma region execute 
long ibApplicationData::RunApplication(const wxString& strAppName, bool searchDebug, bool useManifest) const
{
	// Hand the child process the raw password captured at login, not the stored hash.
	// Otherwise enterprise.exe authenticates with the hash itself — which only worked
	// before MD5→PBKDF2 because the verifier silently treated hash==hash as a match,
	// and even then it let the stored hash act as a bearer token.
	//
	// Creds come from the calling thread's ibSession (ibSessionScope set
	// by the host app's mainApp once session->Open succeeds). If no
	// session is scoped — codeRunner or pre-auth tools that have no
	// business spawning interactive children — we still call through with
	// empty strings; the child will fall back to its own login flow.
	wxString userName, rawPassword;
	if (auto* s = ibSession::Current()) {
		userName    = s->GetUserInfo().m_strUserName;
		rawPassword = s->GetSessionRawPassword();
	}
	return RunApplication(strAppName, userName, rawPassword, searchDebug, useManifest);
}

long ibApplicationData::RunApplication(const wxString& strAppName, const wxString& strUserName, const wxString& strUserPassword, bool searchDebug, bool useManifest) const
{
	// All OES binaries spawn with unified `--flag=value` syntax using
	// wenterprise-server's long-name set (server/dbport/db/user/password/
	// file/ibuser/ibpwd/locale/debug). enterprise.exe / designer.exe /
	// daemon.exe declare these as the long name of their legacy short
	// options, so one builder feeds every parser.
	wxString executeCmd = strAppName + wxT(' ');

	if (m_strFile.IsEmpty()) {

		if (!m_strServer.IsEmpty())
			executeCmd += wxString::Format(wxT(" --server=%s"), m_strServer);
		if (!m_strPort.IsEmpty())
			executeCmd += wxString::Format(wxT(" --dbport=%s"), m_strPort);
		if (!m_strDatabase.IsEmpty())
			executeCmd += wxString::Format(wxT(" --db=%s"), m_strDatabase);
		if (!m_strUser.IsEmpty())
			executeCmd += wxString::Format(wxT(" --user=%s"), m_strUser);
		if (!m_strPassword.IsEmpty())
			executeCmd += wxString::Format(wxT(" --password=%s"), m_strPassword);
	}
	else {
		executeCmd += wxString::Format(wxT(" --file=%s"), m_strFile);
	}

	if (searchDebug)
		executeCmd += wxT(" --debug");

	if (!strUserName.IsEmpty())
		executeCmd += wxString::Format(wxT(" --ibuser=%s"), strUserName);

	if (!strUserPassword.IsEmpty())
		executeCmd += wxString::Format(wxT(" --ibpwd=%s"), strUserPassword);

	executeCmd += wxString::Format(wxT(" --locale=%s"), m_locale.GetName());

	// Manifest-mode: wes-style spawn. Skip the debug-client handshake —
	// that's enterprise.exe's in-process debugger attach, not relevant
	// to a headless wes child.
	if (useManifest)
		return SpawnWebServerWithManifest(executeCmd, searchDebug);

	const long execute = wxExecute(executeCmd);

	if (searchDebug) {

		unsigned short num_attempts = 0;

		debugClient->SearchServer(true);
		while (debugClient != nullptr) {

			if (debugClient->GetConnectionSuccess())
				break;

			if (num_attempts > 300)
				break;

			num_attempts++;
			wxMilliSleep(5);
		}
	}

	return execute;
}

long ibApplicationData::SpawnWebServerWithManifest(wxString cmd, bool searchDebug)
{
	// --port=0 → OS picks an ephemeral port. --manifest=<file> → wes writes
	// host/port/prefix/url there once it is actually accepting connections.
	const wxString manifestPath = wxFileName::CreateTempFileName(
		wxT("oes-wes-"));
	cmd += wxString::Format(wxT(" --port=0 --manifest=\"%s\""), manifestPath);

	// Spawn detached so the caller can exit without killing the server.
	const long pid = wxExecute(cmd, wxEXEC_ASYNC);
	if (pid == 0) {
		wxRemoveFile(manifestPath);
		return 0;
	}

	// Poll manifest up to 5s in the calling thread. wxYieldIfNeeded
	// lets the GUI dispatch paint / mouse events so the window stays
	// responsive while wes's InitBackend (metadata + DB connect) runs
	// for ~1s. Detaching the poll to a worker thread was tried but
	// raced with the caller's Close(true) / wxApp teardown — browser
	// sometimes got launched after wx state was already half-torn-
	// down, ending up on a URL the running wes wasn't yet serving.
	// Synchronous poll keeps the caller alive long enough for
	// wxLaunchDefaultBrowser to fully ShellExecute before return.
	wxString url;
	for (int waited = 0; waited < 5000; waited += 100) {
		wxMilliSleep(100);
		wxYieldIfNeeded();
		wxFile f;
		if (!f.Open(manifestPath, wxFile::read))
			continue;
		wxCharBuffer buf(static_cast<size_t>(f.Length()));
		if (f.Read(buf.data(), f.Length()) <= 0) {
			f.Close();
			continue;
		}
		const wxString content = wxString::FromUTF8(buf.data(), f.Length());
		f.Close();
		const int pos = content.Find(wxT("url="));
		if (pos == wxNOT_FOUND)
			continue;
		url = content.Mid(pos + 4);
		url = url.BeforeFirst(wxT('\n'));
		url.Trim().Trim(false);
		if (!url.IsEmpty())
			break;
	}
	wxRemoveFile(manifestPath);
	if (!url.IsEmpty())
		wxLaunchDefaultBrowser(url);

	// Web debug attach: wes is up with --debug, its in-process
	// ibDebuggerServer is listening on defaultDebuggerPort..+diapason
	// in non-blocking (wait=false) mode. SearchServer scans + verifies
	// each port, but verified+Scanner stays Scanner — designer never
	// auto-promotes to Debugger on a non-waiting server. We retry
	// SearchServer in rounds because a single sweep can race with wes's
	// metadataCreate (debug server still coming up) — first sweep's
	// scan threads exit on connect-refused, leaving counter=-1, which
	// lets the next SearchServer call create fresh threads. Once verify
	// succeeds (m_connectionSuccess), AttachAllVerified pushes
	// CommandId_StartSession so wes flips to Debugger mode.
	if (searchDebug && pid != 0) {
		const int kRounds      = 20;   // 20 rounds * 250ms = ~5s
		const int kRoundSleepMs = 250;
		for (int round = 0; round < kRounds; ++round) {
			if (debugClient == nullptr) break;
			debugClient->SearchServer(true);
			wxMilliSleep(kRoundSleepMs);
			if (debugClient->GetConnectionSuccess())
				break;
		}
		if (debugClient != nullptr && debugClient->GetConnectionSuccess())
			debugClient->AttachAllVerified();
	}

	return pid;
}

#pragma endregion

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::AuthenticateUser(const wxString& strUserName,
                                          const wxString& strUserPassword,
                                          ibUserInfo& outInfo)
{
	// Open-access mode — no sys_user rows at all AND caller did not
	// supply a user name. Historical behaviour is "pass through";
	// outInfo stays default-constructed (IsOk() false).
	if (strUserName.IsEmpty() && !ibUserInfo::HasAny())
		return true;

	outInfo = ibUserInfo::Read(strUserName);
	if (!outInfo.IsOk()) {
		try {
			if (ibLog) ibLog->Audit(wxT("auth"), wxT("login_failed"),
			                        wxString::Format(wxT("user=%s reason=unknown"), strUserName));
		} catch (...) {}
		return false;
	}

	if (!ibPasswordHash::Verify(strUserPassword, outInfo.m_strUserPassword)) {
		try {
			if (ibLog) ibLog->Audit(wxT("auth"), wxT("login_failed"),
			                        wxString::Format(wxT("user=%s reason=bad_password"), strUserName),
			                        outInfo.m_strUserGuid, /*refMetaId=*/0);
		} catch (...) {}
		return false;
	}

	// Lazy upgrade: if we just verified a legacy MD5 hash or a PBKDF2 hash
	// with a below-policy iteration count, re-store the password using the
	// current parameters. Silent — if the DB write fails we don't fail the
	// login, just keep the old hash.
	if (ibPasswordHash::NeedsRehash(outInfo.m_strUserPassword)) {
		try {
			outInfo.m_strUserPassword = ibPasswordHash::Hash(strUserPassword);
			(void)ibUserInfo::Save(outInfo);
			if (ibLog) ibLog->Audit(wxT("auth"), wxT("password_rehash"),
			                        wxString::Format(wxT("user=%s"), strUserName),
			                        outInfo.m_strUserGuid, /*refMetaId=*/0);
		} catch (...) {
			// ignore — login already succeeded
		}
	}

	return true;
}

void ibApplicationData::InstallUser(const ibUserInfo& info,
                                     const wxString& rawPassword)
{
	// User identity now lives only on the ibSession. The registry thread
	// calls InstallUser under a ibSessionScope bound to the target session
	// (ProcessAttach); the desktop login dialog calls it under the main-
	// thread scope of the session that owns the dialog. Without a current
	// session there is nowhere to install — the caller is in a pre-auth
	// path that has no business calling this.
	if (auto* ctx = ibSession::Current()) {
		if (auto* registry = ibApplicationData::GetSessionRegistry())
			registry->InstallUser(ctx, info, rawPassword);
	}
}

bool ibApplicationData::Login(const wxString& strUserName,
                              const wxString& strUserPassword,
                              ibUserInfo& outInfo)
{
	if (!AuthenticateUser(strUserName, strUserPassword, outInfo))
		return false;

	// Open-access pass-through: verification succeeded but no real user
	// was resolved (sys_user empty + caller supplied no creds). Caller
	// treats this as "auth settled"; nothing to install.
	if (outInfo.IsOk()) {
		InstallUser(outInfo, strUserPassword);
		try {
			if (ibLog) {
				// ref_meta_id = 0 marks a system-table (sys_user) ref —
				// viewer recognises 0 as "not a metadata object, drill via
				// the User Admin form keyed by m_strUserGuid instead".
				ibLog->Audit(wxT("auth"), wxT("login"),
				             wxString::Format(wxT("user=%s"), strUserName),
				             outInfo.m_strUserGuid, /*refMetaId=*/0);
			}
		} catch (...) {}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////

#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/mstream.h>
#include <wx/filename.h>

bool ibApplicationData::LoadDatabase(const wxString& strFullPath)
{
	wxFileInputStream fis(strFullPath);
	if (!fis.IsOk()) {
		ibJournalError(wxT("appdata"),"Couldn't open the file '%s'.", strFullPath);
		return false;
	}

	if (!ClearDatabase() && !ClearTableUser())
		return false;

	wxZipInputStream zis(fis);
	std::unique_ptr<wxZipEntry> entry;

	// The entries are READ IN FILE ORDER, and the order carries meaning: rows can only be written once
	// the structure holding them exists. Our own writer emits config before data, so this is a guard
	// against a file that says otherwise — answered with a refusal rather than with rows quietly
	// landing in whatever structure the database happened to have.
	bool configLoaded = false;

	// Iterate through all entries in the zip file
	while (entry.reset(zis.GetNextEntry()), entry) {

		if (!entry->IsDir() && entry->GetName() == wxT("config")) {

			// Open the entry for reading and write its data to a new file
			if (zis.OpenEntry(*entry)) {
				wxMemoryOutputStream fos;
				if (fos.IsOk()) {
					zis.Read(fos); // Read from zip stream, write to file stream
				}
				else {
					return false;
				}

				//load configuration 
				wxMemoryBuffer buffer;
				fos.CopyTo(buffer.GetAppendBuf(fos.GetSize()), fos.GetSize());
				buffer.SetDataLen(fos.GetSize());

				// LoadConfigFromBuffer already RUNs the loaded tree (the
				// ibMetaDataConfiguration override) — no separate RunDatabase
				// here, that would be a double-run (asserts !m_configOpened).
				if (!activeMetaData->LoadConfigFromBuffer(buffer))
					return false;

				// ⭐⭐ THE STRUCTURE IS BUILT FIRST, AND THE LOAD STOPS IF IT WAS NOT.
				//
				// This is the whole shape of a load: the file carries the configuration and the rows, so
				// the configuration is applied — creating the very tables, with the very column ids, the
				// rows are about to be written into — and only then does `data` arrive. The answer used
				// to be discarded. A failed apply therefore went unnoticed and the load carried on,
				// pouring rows into tables that were not there or were still the shape of the database
				// being overwritten: no rows restored, no word said, and a base left half-replaced.
				if (!activeMetaData->SaveDatabase(saveConfigFlag)) {
					ibJournalError(wxT("appdata"),_("The configuration from the file could not be applied - the data was not loaded"));
					return false;
				}
				configLoaded = true;
			}
		}
		else if (!entry->IsDir() && entry->GetName() == wxT("user")) {

			// Open the entry for reading and write its data to a new file
			if (zis.OpenEntry(*entry)) {
				wxMemoryOutputStream fos;
				if (fos.IsOk()) {
					zis.Read(fos); // Read from zip stream, write to file stream
				}
				else {
					return false;
				}

				//load user 
				wxMemoryBuffer buffer;
				fos.CopyTo(buffer.GetAppendBuf(fos.GetSize()), fos.GetSize());
				buffer.SetDataLen(fos.GetSize());

				if (!LoadUserInfoFromBuffer(buffer))
					return false;
			}
		}
		else if (!entry->IsDir() && entry->GetName() == wxT("data")) {

			// Open the entry for reading and write its data to a new file
			if (zis.OpenEntry(*entry)) {
				wxMemoryOutputStream fos;
				if (fos.IsOk()) {
					zis.Read(fos); // Read from zip stream, write to file stream
				}
				else {
					return false;
				}

				//load data 
				wxMemoryBuffer buffer;
				fos.CopyTo(buffer.GetAppendBuf(fos.GetSize()), fos.GetSize());
				buffer.SetDataLen(fos.GetSize());

				if (!configLoaded) {
					ibJournalError(wxT("appdata"),_("The file carries data before the configuration - it cannot be loaded"));
					return false;
				}

				if (!activeMetaData->RestoreDataFromBuffer(buffer))
					return false;
			}
		}
	}

	return true;
}

bool ibApplicationData::SaveDatabase(const wxString& strFullPath)
{
	// 1. Create the physical file output stream
	wxFFileOutputStream out(strFullPath);
	if (!out.IsOk())
	{
		ibJournalError(wxT("appdata"),"Cannot create output file %s", strFullPath);
		return false;
	}

	// 2. Wrap it in a wxZipOutputStream for compression
	wxZipOutputStream zip(out);

	// PutNextEntry sets up the new entry in the archive
	if (zip.PutNextEntry(wxT("config"))) {
		//save configuration 
		wxMemoryBuffer bufferConfig;
		if (activeMetaData->SaveConfigToBuffer(bufferConfig)) {
			// Wrap the content in an input stream to write it easily
			wxMemoryInputStream contentStream(bufferConfig.GetData(), bufferConfig.GetDataLen());
			zip.Write(contentStream); // Write the data from the content stream
		}
	}

	if (zip.PutNextEntry(wxT("user"))) {
		//save users 
		wxMemoryBuffer bufferUser;
		if (!SaveUserInfoToBuffer(bufferUser))
			return false;
		// Wrap the content in an input stream to write it easily
		wxMemoryInputStream contentStream(bufferUser.GetData(), bufferUser.GetDataLen());
		zip.Write(contentStream); // Write the data from the content stream
	}

	if (zip.PutNextEntry(wxT("data"))) {
		//save data
		wxMemoryBuffer bufferData;
		if (!activeMetaData->DumpDataToBuffer(bufferData))
			return false;
		// Wrap the content in an input stream to write it easily
		wxMemoryInputStream contentStream(bufferData.GetData(), bufferData.GetDataLen());
		zip.Write(contentStream); // Write the data from the content stream
	}

	// 3. Close the zip stream (this is essential to finalize the archive)
	// The underlying file stream 'out' will be closed automatically when 'zip' goes out of scope,
	// or when explicitly calling zip.Close().
	return zip.Close();
}

bool ibApplicationData::ClearDatabase()
{
	if (!m_created_metadata)
		return false;

	if (!activeMetaData->ReCreateDatabase())
		return false;

	return true;
}

wxString ibApplicationData::GetDatabaseDescription()
{
	if (m_dbMode == ibDatabaseMode::eFILE)
		return m_strFile;

	if (m_dbMode == ibDatabaseMode::eSERVER)
		return m_strServer + wxT(":") + m_strPort + wxT("/") + m_strDatabase;

	return wxT("");
}

///////////////////////////////////////////////////////////////////////////////

#include "backend/utils/md5.hpp"

wxString ibApplicationData::ComputeMd5(const wxString& userPassword) const
{
	if (userPassword.Length() > 0)
		return ibMD5::ComputeMd5(userPassword);

	return wxEmptyString;
}

///////////////////////////////////////////////////////////////////////////////


