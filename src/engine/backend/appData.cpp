////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider
//	Description : app info
////////////////////////////////////////////////////////////////////////////

#include "backend/appData.h"
#include "backend/appHost.h"

#include <thread>
#include <algorithm>
#include <sstream>
#include <utility>   // std::exchange — the active configuration handed over

#include <wx/ffile.h>      // infobase.conf — written for a new base
#include <wx/fileconf.h>   // infobase.conf — a base's own settings
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/serviceExclusivePolicy.h"   // CanOpen — a base another process holds is refused at open
#include "backend/logger/logger.h"
#include "backend/logger/loggerSweep.h"
#include "backend/lock/lockManager.h"
#include "backend/job/jobManager.h"           // ibJobManager (owned via GetJobManager)
#include "backend/mcp/mcpServer.h"            // ibMcpServer (owned via GetMcpServer)
#include "backend/job/platformJobs.h"         // the engine's own jobs, declared when a database opens
#include "backend/settings/settingsStorage.h" // ibSettingsStorage (owned via GetSettingsStorage)
#include "backend/temp/tempStorage.h"         // ibTempStorage (owned via GetTempStorage)
#include "backend/server/serverConfig.h"      // a server reads where its base lives itself

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
// continue to see ibApplicationInstance::GetDatabaseLayer as the entry
// point even as the pool grows more responsibilities.
std::shared_ptr<ibDatabaseLayer> ibApplicationInstance::GetDatabaseLayer()
{
	return ibConnectionPool::GetDatabaseLayer();
}

wxString ibApplicationInstance::ResolveLogDir() const
{
	const wxString sep = wxFileName::GetPathSeparator();
	if (m_strServer.IsEmpty() && !m_strFile.IsEmpty()) {
		// Lives next to sys.fdb — admins see logs alongside the base.
		return m_strFile + sep + wxT("oeslog");
	}
	if (!m_strServer.IsEmpty()) {
		// A server keeps each base in a folder of its own — the journal lives there, beside nothing else
		// (a file base's folder holds the database as well).
		if (!m_strDirLocal.IsEmpty())
			return m_strDirLocal + sep + wxT("oeslog");

		// Per-user persistent location. %TEMP% would be wiped by
		// Windows disk cleanup; %LOCALAPPDATA% survives reboots and
		// "temp-file cleanup". Until compute-server arrives
		// this is the only place a client's journal lives.
		//
		// ⚠ KEYED BY THE SERVER AND THE BASE. The base's name alone collided: two bases called the same on
		// two servers — which one process of several bases may hold at once — wrote one journal.
		const auto sanitised = [](wxString tag) {
			// Path separators in db names would break Mkdir.
			tag.Replace(wxT("\\"), wxT("_"));
			tag.Replace(wxT("/"),  wxT("_"));
			tag.Replace(wxT(":"),  wxT("_"));
			return tag;
		};
		wxString legacy = m_strDatabase;
		if (legacy.IsEmpty()) legacy = m_strServer;
		if (legacy.IsEmpty()) legacy = wxT("default");
		const wxString tag = (!m_strServer.IsEmpty() && !m_strDatabase.IsEmpty())
			? sanitised(m_strServer + wxT("_") + m_strDatabase)
			: sanitised(legacy);

		const wxString root = wxStandardPaths::Get().GetUserLocalDataDir() + sep + wxT("OES") + sep;
		// …and a journal kept under the name alone is MOVED to the new key once, so its history stays
		// readable instead of starting over beside it. A move that fails (the folder in use) costs only that.
		const wxString old = root + sanitised(legacy);
		if (tag != sanitised(legacy) && !wxDirExists(root + tag) && wxDirExists(old))
			wxRenameFile(old, root + tag);

		return root + tag + sep + wxT("logs");
	}
	return wxEmptyString;
}

void ibApplicationInstance::CreateLogger()
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
	case ibSessionKind::ThinClient:    return wxT("ThinClient");
	case ibSessionKind::ThinDesigner:  return wxT("ThinDesigner");
	case ibSessionKind::Unknown:       return wxT("Unknown");
	}
	return wxT("Unknown");
}

}   // namespace

///////////////////////////////////////////////////////////////////////////////
//								ibApplicationInstance
///////////////////////////////////////////////////////////////////////////////

ibApplicationInstance* ibApplicationInstance::Get(bool required)
{
	// ⭐⭐ THROUGH THE SESSION (Max, 2026-09-30: `appData = GetSession()->GetAppData()`, `activeMetaData =
	// … ->GetMetaData()`). There is no global current base and no global current configuration: `appData`
	// and `activeMetaData` are those of the session this thread works for — session → its registry → base.
	if (ibSession* const session = ibSession::Current())
		if (ibApplicationInstance* const applicationInstance = session->GetApplicationInstance())
			return applicationInstance;

	// …a thread with NO session: the base it is bound to — the one it opened (ibApplicationInstanceScope).
	// Thread-local, never global.
	if (ibApplicationInstance* const applicationInstance = ibApplicationInstanceScope::Current())
		return applicationInstance;

	// …and neither: before the first base opens and after the last one closes, nothing. Otherwise this
	// thread named no base, and that is said, not guessed — handing it "the only one" or "the first one" is
	// how a line of one base would land in another.
	if (!required || ibApplicationHost::IsEmpty())
		return nullptr;
	std::ostringstream thread;
	thread << std::this_thread::get_id();
	ibBackendCoreException::Error(_("Thread %s works for no base: it has no session and no base bound to it."),
		wxString(thread.str()));
	return nullptr;
}

ibHelpService* ibApplicationInstance::GetHelpService()
{
	ibApplicationHost* const host = ibApplicationHost::Get();
	return host != nullptr ? host->GetHelpService() : nullptr;
}

wxString ibApplicationInstance::GetLocale() const
{
	ibApplicationHost* const host = ibApplicationHost::Get();
	return host != nullptr ? host->GetLocale() : wxString();
}

ibPluginManager* ibApplicationInstance::GetPluginManager() const
{
	ibApplicationHost* const host = ibApplicationHost::Get();
	return host != nullptr ? host->GetPluginManager() : nullptr;
}

///////////////////////////////////////////////////////////////////////////////

// (The worker pool is the process's — ibApplicationHost, sized by its run mode there.)

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
	case eSERVER_MODE: return 4;
	default:           return 2;   // a file base / a sandbox / the launcher
	}
}

// A NEW BASE GETS ITS OWN SETTINGS FILE — infobase.conf in its folder, every key written with its default, so a
// person finds them where the base is and changes them there. Written when the base is created; one that is there
// is never touched. (A note above each key, not after it: an INI value runs to the end of its line.)
static void WriteInfobaseConf(const wxString& folder)
{
	const wxString path = folder + wxFileName::GetPathSeparator() + wxT("infobase.conf");
	if (folder.IsEmpty() || wxFileName::FileExists(path))
		return;
	wxFFile file(path, wxT("w"));
	if (!file.IsOpened() || !file.Write(
			wxT("; This base's own settings - read when the base is opened.\n")
			wxT("; Connections - connections to its DBMS at most; 0 - the default (backend.conf, else 32).\n")
			wxT("Connections = 0\n"))) {
		ibTechJournal::Print(ibJournalMark::Warning, wxT("appdata"),
			wxT("%s cannot be written - the base runs on the defaults"), path);
	}
}

// HOW MANY CONNECTIONS TO ITS DBMS THIS BASE MAY HOLD — the base's own word, infobase.conf in its folder
// (Connections), over the process's default (backend.conf). A base's connections are its own: a PostgreSQL
// base and an MS SQL one are two worlds, so the number lives with each base. A value that cannot be used is
// said, not quietly replaced. Two at least: the registry holds one for its writes, a session needs another.
static std::size_t ReadInfobaseConnections(const wxString& folder, std::size_t byDefault)
{
	const wxString path = folder + wxFileName::GetPathSeparator() + wxT("infobase.conf");
	if (folder.IsEmpty() || !wxFileName::FileExists(path))
		return byDefault;

	const wxFileConfig conf(wxT(""), wxT(""), path, wxT(""), wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);
	return ibApplicationHost::ReadCount(conf, path, wxT("Connections"), 2, byDefault);
}

// ⭐ ASKED OF THE SESSION DOING THE WORK. A person's session says what it works in, whatever process hosts it — a
// thin designer and a thin runtime live in one application server side by side, and a file base has its designer
// and its runtime as a server does. A session nobody sits at (a job) goes by the process's own session in the base
// — the designer's window, the client's, the server's login (the registry's fallback): a job in the designer's base
// builds no runtime.
bool ibApplicationInstance::DesignerMode() const
{
	if (const ibSession* const session = ibSession::Current()) {
		if (IsDesignerSessionKind(session->GetKind()))
			return true;
		if (IsRuntimeSessionKind(session->GetKind()))
			return false;
	}
	const ibSession* const own = m_sessionRegistry != nullptr ? m_sessionRegistry->GetFallback() : nullptr;
	return own != nullptr && IsDesignerSessionKind(own->GetKind());
}

bool ibApplicationInstance::EnterpriseMode() const
{
	if (const ibSession* const session = ibSession::Current()) {
		if (IsRuntimeSessionKind(session->GetKind()))
			return true;
		if (IsDesignerSessionKind(session->GetKind()))
			return false;
	}
	const ibSession* const own = m_sessionRegistry != nullptr ? m_sessionRegistry->GetFallback() : nullptr;
	return (own == nullptr || !IsDesignerSessionKind(own->GetKind())) && GetActiveMetaData(this) != nullptr;
}

bool ibApplicationInstance::WebEnterpriseMode() const
{
	const ibSession* const session = ibSession::Current();
	return session != nullptr && (session->GetKind() == ibSessionKind::WebServer || session->GetKind() == ibSessionKind::WebClient);
}

bool ibApplicationInstance::ServiceMode() const
{
	return m_runMode == ibRunMode::eSERVER_MODE;
}

ibApplicationInstance::ibApplicationInstance(ibApplicationHost* host, ibRunMode runMode) :
	m_host(host),
	m_runMode(runMode),
	m_strComputer(wxGetHostName()),
	// `unique_ptr(new X(...))` rather than `make_unique` — every
	// subsystem ctor takes an `ib::AppDataCtorToken` and `make_unique`
	// isn't a friend of the token. Direct `new X(...)` works because
	// this TU is `ibApplicationInstance`'s and can mint the token.
	//
	// Init order matches the declaration order in appData.h —
	// connectionPool first, activeMetaData last. See the ownership
	// block in the header for the destruction contract that this
	// order encodes.
	//
	// Every token is minted with `this`: the base that makes a subsystem is its owner, and the token says so.
	m_connectionPool(std::unique_ptr<ibConnectionPool>(new ibConnectionPool(ib::AppDataCtorToken{ this }))),
	m_lockManager(std::unique_ptr<ibLockManager>(new ibLockManager(ib::AppDataCtorToken{ this }))),
	m_queryableFactory(std::unique_ptr<ibQueryableFactory>(new ibQueryableFactory(ib::AppDataCtorToken{ this }))),
	m_sessionRegistry(std::unique_ptr<ibSessionRegistry>(new ibSessionRegistry(ib::AppDataCtorToken{ this }))),
	m_jobManager(std::unique_ptr<ibJobManager>(new ibJobManager(ib::AppDataCtorToken{ this }))),
	m_mcpServer(std::unique_ptr<ibMcpServer>(new ibMcpServer(ib::AppDataCtorToken{ this }))),
	m_settingsStorage(std::unique_ptr<ibSettingsStorage>(new ibSettingsStorage(ib::AppDataCtorToken{ this }))),
	m_tempStorage(std::unique_ptr<ibTempStorage>(new ibTempStorage(ib::AppDataCtorToken{ this })))
{
	// (Plugins are the PROCESS's — loaded once by ibApplicationHost, not per base.)

	// Wire session-lifecycle event listeners — drives metadata load on
	// first auth + per-session runtime bring-up + last-auth-out cleanup.
	if (runMode != eLAUNCHER_MODE)
		WireSessionEvents();
}

// Fabric — replaces ibMetaDataConfigurationBase::Initialize. Picks the subclass by the kind of the base's first
// session and puts it in `m_activeMetaData`, for the sessions to come; each takes its own reference. On the
// application data NAMED — the session listeners below pass their own rather than let the registry thread
// resolve one.
bool ibApplicationInstance::CreateActiveMetaData(ibApplicationInstance* applicationInstance, ibSessionKind kind, int flags)
{
	if (applicationInstance == nullptr)
		return false;
	if (applicationInstance->m_activeMetaData)
		return false;   // already initialised — caller passes a fresh appData

	// WHAT THE BASE WAS STARTED AS — the kind of its first session: a file base's is what it was started as, a
	// server's is the server itself. A designer's base holds the configuration to edit; anyone else's the one the application runs.
	// The metadata subclass ctors are gated on ib::AppDataCtorToken — this TU is ibApplicationInstance's, so it can
	// mint the token; nobody outside can. The launcher has no metadata at all — that's a successful no-op so
	// callers can branch uniformly.
	if (kind == ibSessionKind::Launcher)
		return true;
	const ib::AppDataCtorToken owner{ applicationInstance };
	std::shared_ptr<ibMetaDataConfigurationBase> metaData;
	if (IsDesignerSessionKind(kind))
		metaData = ibMetaData::MakeShared<ibMetaDataConfigurationStorage>(owner);
	else
		metaData = ibMetaData::MakeShared<ibMetaDataConfiguration>(owner);

	// In place BEFORE it initialises — its loading asks for `activeMetaData`. The session being let in holds none
	// yet (it takes its own right after, NotifyAuthenticated), so its thread works in it through a scope meanwhile.
	{
		std::lock_guard<std::mutex> lk(ibApplicationHost::s_mutex);
		applicationInstance->m_activeMetaData = metaData;
	}
	const ibApplicationInstanceScope initialising(applicationInstance);
	return metaData->OnInitialize(flags);
}

ibMetaDataConfigurationBase* ibApplicationInstance::GetActiveMetaData(const ibApplicationInstance* applicationInstance)
{
	if (applicationInstance == nullptr)
		return nullptr;
	// The session's own, through its reference…
	if (const ibSession* const session = ibSession::Current())
		if (ibMetaDataConfigurationBase* const metaData = session->GetMetaData())
			if (metaData->GetApplicationInstance() == applicationInstance)
				return metaData;
	// …a thread working for the base without a session: the one its scope holds for the length of the work…
	if (ibMetaDataConfigurationBase* const metaData = ibApplicationInstanceScope::GetMetaData())
		if (metaData->GetApplicationInstance() == applicationInstance)
			return metaData;
	// …and a thread that said neither — a session being let in or already gone, the thread that opened the base:
	// the base's active one, read under the lock. Whoever works in it beyond this line holds `shared_from_this()`.
	std::lock_guard<std::mutex> lk(ibApplicationHost::s_mutex);
	return applicationInstance->m_activeMetaData.get();
}

void ibApplicationInstance::ReplaceActiveMetaData(std::shared_ptr<ibMetaDataConfigurationBase> metaData)
{
	const wxString digest = metaData->GetConfigMD5();
	std::shared_ptr<ibMetaDataConfigurationBase> previous;
	{
		std::lock_guard<std::mutex> lk(ibApplicationHost::s_mutex);
		previous = std::exchange(m_activeMetaData, std::move(metaData));
	}

	// …and the base lets the old one go. The sessions working in it hold it, each by its own reference, and the
	// last of them closes it (ibMetaData::MakeShared) — or the end of this call does, when nobody works in it.
	ibJournalInfo(wxT("metadata"), wxT("the active configuration is now %s; the one before stays with %ld holder(s)"),
		digest, previous != nullptr ? previous.use_count() - 1 : 0L);
}

void ibApplicationInstance::WireSessionEvents()
{
	auto* registry = m_sessionRegistry.get();
	if (registry == nullptr) return;

	// First authenticated session in the process → load metadata skeleton
	// only. CreateRoot / RunDatabase / CompileRoot live in OnAuthenticated
	// (and the designer's manual RunDatabase after the window shows) — this
	// listener is just the one-shot metadata bootstrap.
	//
	// ⚠ EVERY LISTENER HERE WORKS ON ITS OWN BASE — `this`, its members — and never through `appData`,
	// `activeMetaData` or `ibLog`: those answer the base of the thread that fires the listener, and with
	// several bases in the process the registry thread of one base must not be asked which base it is.
	// The base's FIRST SESSION says what it was started as — a file base's is what it was started as, a server's
	// the server itself — and so, by its kind, which configuration it holds. Its schedule starts with it, not before: nobody comes
	// into a base ahead of the one who opened it, a job due at start included.
	registry->OnFirstConnect([this](ibSession* s) {
		if (m_created_metadata) return;
		if (!CreateActiveMetaData(this, s != nullptr ? s->GetKind() : ibSessionKind::Unknown, m_loadMetadataFlags)) return;
		m_created_metadata = true;
		if (m_jobManager) m_jobManager->Start();
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
			if (m_logger) {
				m_logger->Audit(wxT("session"), wxT("opened"),
				             wxString::Format(wxT("kind=%s id=%s"),
				                              DescribeSessionKind(s->GetKind()),
				                              s->GetId()));
			}
		} catch (...) {}
		ibSession::BindSessionToThread(s, std::this_thread::get_id());
		// ⭐ THE PROCESS'S OWN SESSION IN THIS BASE — of a process kind (IsProcessSessionKind): the designer's window,
		// the client's window, the application server's login, the web server's technical row. Not a job's
		// and not a visitor's tab, which are let in through this same door and used to take what follows by
		// being FIRST: the job manager starts with the base, before anybody logs in, so a scheduled job due at
		// start made its run-as user what every unbound thread answered with, and a job under a user with
		// settings of their own re-pointed the running MCP server at that user's token (census, 2026-10-01).
		// …and a thin client's in a FILE base: there the process IS the thin client (fileserver in its own process), as
		// the desktop's window is enterprise.exe — on a server the same session is one visitor among many. Asked by the
		// debugger's Stop when nothing is parked (ibSession::Current on its thread falls back to this session).
		const bool ownSession = IsProcessSessionKind(s->GetKind())
			|| (m_runMode == ibRunMode::eFILE_MODE
				&& (s->GetKind() == ibSessionKind::ThinClient || s->GetKind() == ibSessionKind::ThinDesigner));
		auto* registry = m_sessionRegistry.get();
		if (ownSession && registry && registry->GetFallback() == nullptr) {
			// What an UNBOUND thread resolves to (ibSession::Current — one rule everywhere). On the desktop
			// this IS the window's session, the answer the old "hand back the lone map entry" gave.
			registry->SetFallback(s);
		}
		// A PERSON'S OWN MCP SERVER, read the moment they are let in — the
		// settings are keyed by user, so opening the designer is when "whose
		// server is this" gets its answer. Nothing saved yet is a cold start,
		// not a failure: the defaults stand, and the defaults are off.
		if (ownSession && m_mcpServer) m_mcpServer->LoadSettings(s);

		// Enable per-session debug context iff this process was started
		// with --debug. Marks the session as debugged so ibProcUnit's
		// breakpoint dispatch + DoDebugLoop's CV wait route through the
		// session's own state instead of the legacy server-singleton.
		if ((m_loadMetadataFlags & _app_start_create_debug_server_flag) != 0)
			registry->EnableDebugForSession(s);
		if (ibMetaDataConfigurationBase* const metaData = s->GetMetaData()) {
			// Root mm is allocated by ibSession::EnsureRoot (called by the
			// registry between OnFirstConnect and this listener) for runtime
			// sessions; the Designer has no root mm (it uses the lightweight
			// designer manager in the compile cache). Here we drive cross-process
			// metadata bring-up (RunDatabase once per process — fires OnBefore/
			// After RunMetaObject which populate ibCompileValueCache +
			// ibModuleStorage) and per-session compile + runtime start. The
			// session's own configuration — the active one when it came in;
			// a replacement arrives already run (ReplaceActiveMetaData).
			if (!m_run_metadata) {
				m_run_metadata = metaData->RunDatabase();
			}
			// CompileRoot folds compile + AttachRuntime + lambda runtime wire-up.
			// No-op when there's no root mm (Designer). AttachRuntime self-gates by
			// session kind, so no explicit runMode check needed here.
			s->CompileRoot();
		}
	});

	// Per-session teardown — runs on the thread that releases the session's
	// holder (ibSession::Teardown), the mirror of the bring-up above, while
	// the session is in Stopping state; only a session nobody released is
	// taken down by the registry (ProcessRemove). UnbindSession (by pointer,
	// not thread id) erases bindings regardless of which thread originally
	// pinned them.
	registry->OnDisconnect([this](ibSession* s) {
		if (s == nullptr) return;
		// Capture session.closed BEFORE UnbindSession + DestroyRoot —
		// ibSession::Current() still resolves to `s` so the audit row
		// carries the right session_id / user_name. After UnbindSession
		// the user identity is gone.
		try {
			if (m_logger) {
				m_logger->Audit(wxT("session"), wxT("closed"),
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
			const bool isConfigOpen = m_activeMetaData != nullptr && m_activeMetaData->IsConfigOpen();
			if (isConfigOpen)
				m_activeMetaData->CloseDatabase(forceCloseFlag);
		}
		m_created_metadata = false;
		m_run_metadata = false;
		// EndJob and other "no more work" paths route here. ForceExit
		// goes through ProcessExitHook (wes' main → svr->stop()) or
		// wxTheApp->Exit (desktop), with ShouldKeepAlive declining when
		// non-debug clients are still live.
		if (auto* reg = m_sessionRegistry.get()) {
			if (!reg->ShouldKeepAlive())
				reg->CloseAll(true);
		}
	});
}

ibApplicationInstance::~ibApplicationInstance()
{
	// Closed already by its owner (ibApplicationHost::Close) — then every step finds its field gone.
	Close();
}

void ibApplicationInstance::Close()
{
	// Business-action sequence — every subsystem with a pre-dtor hook
	// (Stop / UnloadAll / OnDestroy / Shutdown) gets it called here in
	// the order that matches the declaration-order destruction below.
	//
	// The fields are then released in reverse declaration order — the
	// order was chosen (see appData.h ownership block) so that every
	// field's dtor finds its dependencies still alive — explicitly, at
	// the end of this body, so each one reads null before it dies.
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

	// 1. Stop the registry — submits Remove@Urgent for every session
	//    still in m_own, drains the queue, joins the worker. sys_session
	//    DELETEs + OnDisconnect listeners fire before pool dies. Right after
	//    what runs on the sessions — a job's session is cancelled by its
	//    manager, not by the registry — and before the metadata goes.
	if (m_sessionRegistry) m_sessionRegistry->Stop();

	// 2. activeMetaData — OnDestroy may save state, close compile
	//    caches, run cascading detach; those paths still want db_query.
	if (m_activeMetaData) m_activeMetaData->OnDestroy();

	// (Plugins are the process's: they are unloaded by ibApplicationHost once
	//  every base is gone, not by each base.)

	// 3. Pool — close master + clones, invalidate outstanding hand-outs.
	//    Runs AFTER the registry's Stop so any session-bound DB work
	//    has already completed.
	if (m_connectionPool) m_connectionPool->Shutdown();

	// 4. The fields, in reverse declaration order: m_activeMetaData → m_tempStorage → m_settingsStorage →
	//    m_mcpServer → m_jobManager → m_sessionRegistry → m_logger → m_queryableFactory → m_lockManager →
	//    m_connectionPool. (The job manager's own dtor calls Stop() again; it is idempotent, so the
	//    explicit step above only fixes the ORDER.)
	//
	//    ⚠ EACH ONE NULL BEFORE IT DIES — reset(), not the default unwinding. The closing thread works for
	//    this base (ibApplicationHost::Close), and what a field's destructor asks — a connection holder its
	//    pool, the pool the current session, the session its registry — is answered down the chain through
	//    THESE pointers. A unique_ptr left to its own destructor keeps pointing at what it has deleted: the
	//    chain read the freed registry and every process crashed on exit (2026-10-01). Each field is emptied
	//    first, so a question asked after it is gone reads "none" — emptied UNDER THE PROCESS'S LOCK, which
	//    HasRegistry / HasPool read the fields under from any thread, and deleted outside it, where its own
	//    destructor may ask them.
	const auto release = [](auto& field) {
		std::remove_reference_t<decltype(field)> gone;
		{
			std::lock_guard<std::mutex> lk(ibApplicationHost::s_mutex);
			gone = std::move(field);
		}
	};
	release(m_activeMetaData);
	release(m_tempStorage);
	release(m_settingsStorage);
	release(m_mcpServer);
	release(m_jobManager);
	release(m_sessionRegistry);
	release(m_logger);
	release(m_queryableFactory);
	release(m_lockManager);
	release(m_connectionPool);

	// Nothing reports "what survived" from inside the teardown: the live-object report sits in
	// DestroyAppDataEnv, past the delete, where everything this base owned has had its chance to go.
	// (Measured 2026-07-30: reporting before the fields were released printed 510 live property objects
	// that their release then destroyed to zero.)
}


// ---------------------------------------------------------------------------
// User-identity accessors — route through the current thread's ibSession
// (per-cookie on web, main user session on desktop). Without an active
// ibSessionScope (pre-auth bootstrap, standalone codeRunner) we return a
// shared empty sentinel — readers see an unauthenticated state, callers
// that depend on a real user must arrange a session scope first.
// ---------------------------------------------------------------------------

const ibUserInfo& ibApplicationInstance::GetUserInfo() const
{
	if (auto* ctx = ibSession::Current())
		return ctx->GetUserInfo();
	static const ibUserInfo s_empty;
	return s_empty;
}

bool ibApplicationInstance::ExclusiveMode() const
{
	// Process-wide query through the registry — answers "is anyone in
	// exclusive mode right now?". Cluster-aware variant adds the
	// sys_session.exclusive snapshot in a follow-up.
	return m_sessionRegistry != nullptr && m_sessionRegistry->HasExclusiveSession();
}

const wxString& ibApplicationInstance::GetUserName() const
{
	return GetUserInfo().m_strUserName;
}

const wxString& ibApplicationInstance::GetUserPassword() const
{
	// Historical quirk: GetUserPassword returned fullName — kept for
	// source compat with call sites that still use the alias.
	return GetUserInfo().m_strUserFullName;
}

const std::vector<ibUserInfo::ibUserRole>&
ibApplicationInstance::GetUserRoleArray() const
{
	return GetUserInfo().m_roleArray;
}

wxString ibApplicationInstance::GetUserLanguageCode() const
{
	return GetUserInfo().m_strLanguageCode;
}

wxString ibApplicationInstance::ComputeMd5() const
{
	return ComputeMd5(GetUserInfo().m_strUserPassword);
}

///////////////////////////////////////////////////////////////////////////////

// ⭐ OPENING ADDS A BASE — to the process's set (ibApplicationHost), and a host that opens once holds a set
// of one. It used to REPLACE: every Create began by destroying the instance there was, which is exactly what
// a process of several bases cannot do. Every host of today opens once and destroys on exit, so for them
// nothing changes.
bool ibApplicationInstance::CreateAppDataEnv(ibRunMode runMode)
{
	ibApplicationInstance* const applicationInstance = ibApplicationHost::Adopt(
		std::unique_ptr<ibApplicationInstance>(new ibApplicationInstance(ibApplicationHost::Ensure(runMode), runMode)));
	ibApplicationHost::SetThreadInstance(applicationInstance);   // the thread that opens a base works for it
	return SetLocaleAppDataEnv();
}

#define sys_db wxT("sys.fdb")

// THE REQUEST'S TYPE IS THE RUN MODE — a file base holds its base itself, a server holds it for its clients. Only a
// file base makes a base not made yet (the designer's opening); a server never does — that is the launcher's.
ibApplicationInstance* ibApplicationInstance::CreateAppDataEnv(const ibFileInstanceRequest& request)
{
	return Open(ibRunMode::eFILE_MODE, request, request, request.m_create);
}

// A SERVER IS STARTED FROM ITS SETTINGS — it reads where the base lives itself, from the base's group in server.conf:
// its Kind, its folder, the DBMS, and the DBMS's password sealed with the installation's key.
ibApplicationInstance* ibApplicationInstance::CreateAppDataEnv(const ibServerInstanceRequest& request)
{
	ibServerConfig config(request.m_folder);
	if (!config.HasConf())
		ibBackendCoreException::Error(wxT("no config at %s"), config.GetConfPath());
	wxString error;
	if (!config.LoadKey(error))
		ibBackendCoreException::Error(wxT("%s"), error);

	std::vector<ibConfiguredInstance> instances = config.ReadInstances();
	config.AssignIds(instances);   // its folder is named by its Id
	const auto base = std::find_if(instances.begin(), instances.end(),
		[&request](const ibConfiguredInstance& instance) { return instance.m_name == request.m_name; });
	if (base == instances.end())
		ibBackendCoreException::Error(wxT("the config %s has no base '%s'"), config.GetConfPath(), request.m_name);

	ibInstanceStorage storage;
	storage.m_directory = base->m_path;
	if (base->m_kind == wxT("postgresql")) {
		storage.m_server   = base->m_server;
		storage.m_port     = base->m_port;
		storage.m_user     = base->m_user;
		storage.m_database = base->m_database;
		if (!config.OpenSecret(base->m_name, wxT("Password"), base->m_password, storage.m_password, error))
			ibBackendCoreException::Error(wxT("%s"), error);
	}
	else if (base->m_kind != wxT("firebird"))
		ibBackendCoreException::Error(wxT("unknown Kind '%s' - expected firebird or postgresql"), base->m_kind);
	return Open(ibRunMode::eSERVER_MODE, request, storage, false);
}

ibApplicationInstance* ibApplicationInstance::Open(ibRunMode runMode, const ibInstanceRequest& request,
	const ibInstanceStorage& storage, bool create)
{
	// WHERE THE BASE LIVES — the folder's sys.fdb (Firebird), or a PostgreSQL server when one is named.
	if (storage.m_server.IsEmpty()) {
#ifndef OES_USE_FIREBIRD
		// ⭐⭐ A BUILD WITHOUT THE DRIVER MUST SAY SO — this used to be a bare `return false`.
		//
		// A base in a folder IS a Firebird base (sys.fdb), so with the driver left out there is nothing to
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
			_("This build has no Firebird driver (OES_USE_FIREBIRD is not defined), and a base kept in a folder is a Firebird one."));
		return nullptr;
#else
		// The process first — it refuses a base it has no room for before the database is touched.
		ibApplicationHost* const host = ibApplicationHost::Ensure(runMode);

		// ⭐ NO FILE, NO BASE — asked before the driver is. Given a path with nothing at it the driver CREATES the
		// database there (that is how a base is made), so an opening that makes nothing — a server, which only opens
		// what its settings name — left an empty sys.fdb behind and refused it without a word, at every start
		// (2026-10-06). Whether to create is this opening's word, said once.
		const wxString database = storage.m_directory + wxFileName::GetPathSeparator() + sys_db;
		if (!create && !wxFileName::FileExists(database))
			ibBackendCoreException::Error(_("There is no base in %s (no %s), and this opening does not create one."),
				storage.m_directory, sys_db);

		std::shared_ptr<ibDatabaseLayerFirebird> db(new ibDatabaseLayerFirebird());
		if (!db->Open(database))
			return nullptr;

		std::unique_ptr<ibApplicationInstance> opening(new ibApplicationInstance(host, runMode));
		opening->m_strFile = storage.m_directory;
		const wxArrayString dirs = wxFileName::DirName(storage.m_directory).GetDirs();
		opening->m_strInstance = !request.m_name.IsEmpty() ? request.m_name
			: dirs.IsEmpty() ? storage.m_directory : dirs.Last();

		ibApplicationInstance* const applicationInstance = Open(std::move(opening), db, storage.m_directory, request, create);

		// …and the Firebird driver's OWN maintenance, declared by the startup sequence like the platform's jobs and
		// one layer deeper: it used to declare itself from inside ibDatabaseLayerFirebird::Open — before this object
		// existed, before the pool was up, before sys_job was created. WHETHER this base is ours to maintain is the
		// driver's answer; WHEN to act on it is this sequence's.
		if (applicationInstance != nullptr && db->IsLocalMaintenanceEligible())
			ibFirebirdMaintenanceJob::Register(applicationInstance);
		return applicationInstance;
#endif
	}

#ifndef OES_USE_POSTGRESQL
	// The PostgreSQL base — same silence, same reason, same cure as the Firebird one above.
	// A build missing this driver would otherwise refuse every server connection with no cause
	// given, and the search would go to the network and the credentials, where nothing is wrong.
	ibBackendCoreException::Error(
		_("This build has no PostgreSQL driver (OES_USE_POSTGRESQL is not defined), and a base on a database server is a PostgreSQL one."));
	return nullptr;
#else
	// The process first — see the Firebird base above.
	ibApplicationHost* const host = ibApplicationHost::Ensure(runMode);

	std::shared_ptr<ibDatabaseLayerPostgres> db(new ibDatabaseLayerPostgres());
	if (!db->Open(storage.m_server, storage.m_port, storage.m_database, storage.m_user, storage.m_password))
		return nullptr;

	std::unique_ptr<ibApplicationInstance> opening(new ibApplicationInstance(host, runMode));
	opening->m_strServer   = storage.m_server;
	opening->m_strPort     = storage.m_port;
	opening->m_strUser     = storage.m_user;
	opening->m_strPassword = storage.m_password;
	opening->m_strDatabase = storage.m_database;
	opening->m_strDirLocal = storage.m_directory;
	opening->m_strInstance = !request.m_name.IsEmpty() ? request.m_name : storage.m_database;

	// A PostgreSQL base has no directory of its own: its settings and its journal are kept in its local folder.
	return Open(std::move(opening), db, storage.m_directory, request, create);
#endif
}

// ⭐ THE ONE ROAD A BASE COMES UP BY, once its database is open — a file base and a server base alike: listed in
// the process, the opening thread working for it, then its pool, its tables, its locale, its journal and its jobs.
// A base that does not come up — refused or thrown — is closed again, alone, and the thread gets back what it had.
ibApplicationInstance* ibApplicationInstance::Open(std::unique_ptr<ibApplicationInstance> opening,
	std::shared_ptr<ibDatabaseLayer> db, const wxString& folder, const ibInstanceRequest& request, bool create)
{
	const ibRunMode runMode = opening->m_runMode;
	ibApplicationInstance* const applicationInstance = ibApplicationHost::Adopt(std::move(opening));

	// THE THREAD THAT OPENS A BASE WORKS FOR IT — while it comes up (the pool, the tables, the journal and the
	// platform's jobs reach it through `db_query` and the accessors, and there is no session yet) and afterwards:
	// the host goes on to log into it from this thread.
	ibApplicationInstance* const previous = ibApplicationInstanceScope::Current();
	ibApplicationHost::SetThreadInstance(applicationInstance);
	// ⚠ …AND NOT THROUGH ANOTHER BASE'S SESSION. A login leaves its session bound to the thread that made it
	// (NotifyAuthenticated), and a session outranks the base a thread is bound to — so the application server,
	// opening its second base on the thread that had logged into the first, brought the second one up IN THE FIRST
	// (2026-10-01, "serving 2 of 2: trade1, trade1"). The bring-up puts that binding aside and gives it back.
	const ibSessionScope bringingUp(nullptr);
	const auto refuse = [&]() -> ibApplicationInstance* {
		ibApplicationHost::Close(applicationInstance);
		ibApplicationHost::SetThreadInstance(previous);
		return nullptr;
	};

	try {
		// ⭐ THE APPLICATION SAYS WHAT IT IS, into the journal that has been waiting for it since
		// the process began. The journal's own banner can only greet — the binary, the build, the
		// machine — because at that moment nothing has been decided yet. WHICH base, opened HOW,
		// under WHICH run mode is the first fact worth knowing about a session, and this is the
		// first moment it exists.
		ibJournalInfo(wxT("appdata"), wxT("connected: %s (run mode %d)"),
			applicationInstance->GetDatabaseDescription(), static_cast<int>(runMode));

		// The pool is the single owner of every connection of the base.
		// `db` is the master — the pool holds it as m_source for Clone()
		// and also as the first idle entry so the earliest Checkout hands
		// it out directly. Size — the base's infobase.conf, else the
		// process's default (backend.conf, 32 unless said). minIdle picked
		// per runMode (server pre-warms; GUI doesn't), never above the size.
		// Beyond minIdle clones grow lazily and shrink on idle timeout.
		const std::size_t connections = ReadInfobaseConnections(folder, applicationInstance->m_host->GetDefaultConnections());
		applicationInstance->m_connectionPool->Init(db, connections,
			std::min(PickConnectionMinIdle(runMode), connections));

		// ⭐ HELD BY ANOTHER PROCESS? Asked before this one writes anything into the base — its tables, its jobs,
		// its sweep of sys_session — and refused out loud (ibServiceExclusivePolicy::CanOpen). It used to be
		// asked only at the first session, after the bring-up below had written into a base somebody held.
		wxString heldBy;
		if (TableAlreadyCreated() && !ibServiceExclusivePolicy::CanOpen(runMode, heldBy))
			ibBackendCoreException::Error(wxT("%s"), heldBy);

		if (create && !TableAlreadyCreated()) {
			CreateTableSession();
			CreateTableUser();
			CreateTableEvent();
			CreateTableLock();
			WriteInfobaseConf(folder);   // a new base — its own settings file, with the defaults
		}
		// A base with no tables is not opened empty — and says so: a bare refusal left its caller nothing to report
		// ("did not open: no reason given").
		else if (!TableAlreadyCreated())
			ibBackendCoreException::Error(_("The base %s is empty (it has no system tables), and this opening does not create them."),
				applicationInstance->GetDatabaseDescription());

		// Additive and idempotent — a base made before any of these picks them up at its next open.
		MigrateTableSession();         // pid / address / currentActivity, which the registry's INSERT assumes
		MigrateTableBytecodeCache();
		CreateTableLock();             // sys_lock — long-held pessimistic locks
		CreateTableJob();              // sys_job — the shared last-run clock…
		MigrateTableJob();             // …and its settings columns
		CreateTableSettings();         // sys_settings — what people saved on their forms and their lists
		CreateTableFile();             // sys_file — the sessions' temporary files

		if (!SetLocaleAppDataEnv(request.m_locale))
			return refuse();

		// Audit + trace logger — built after pool + tables so the
		// first Audit row (session.opened) can fire on the next
		// Authenticate.
		applicationInstance->CreateLogger();

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
		// of CreateAppDataEnv and never reached a window.
		ibRegisterPlatformJobs(applicationInstance);

		return applicationInstance;
	}
	catch (...) {
		// …and thrown — held by another process, a table that would not be made: not left listed and half up.
		refuse();
		throw;
	}
}

bool ibApplicationInstance::DestroyAppDataEnv(ibApplicationInstance* applicationInstance)
{
	if (applicationInstance == nullptr || ibApplicationHost::Get() == nullptr)
		return false;
	ibApplicationHost::Close(applicationInstance);
	return true;
}

bool ibApplicationInstance::SetLocaleAppDataEnv(const wxString& strLocale)
{
	// The locale is the PROCESS's — settled by the first base, answered as settled to the rest.
	ibApplicationHost* const host = ibApplicationHost::Get();
	return host != nullptr && host->InitLocale(strLocale);
}

bool ibApplicationInstance::DestroyAppDataEnv()
{
	if (ibApplicationHost::Get() != nullptr) {

		// The active session itself is closed by its holder
		// (mainApp/webSession) via ibSession::Close(); each base's Close()
		// stops its registry, which drains the pending Removes.
		//
		// EVERY base, newest first, and the process with them. This used to
		// delete the instance only when its pool had been initialised, so a
		// base opened without a database (launcher, codeRunner) was never
		// destroyed at all — the next Create simply replaced the pointer.
		// Pool shutdown is driven by ibApplicationInstance::Close — see it
		// for the ordering rationale.
		ibApplicationHost::CloseAll();

		// The ibPropertyObject live-register used to be printed here. It answered its question —
		// "none alive, clean teardown" — and the answer is now kept by something cheaper and
		// wider: the CRT exit dump is empty (docs/private/engineering-playbook/25-memory-leaks.md), so a
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

// (The platform locale and backend.conf are the PROCESS's — ibApplicationHost::InitLocale /
//  ReadBackendConf, appHost.cpp.)

// ---------------------------------------------------------------------------
// Phased startup (split of legacy Connect). Apps compose the phases;
// runtime start is NOT here — it's driven from the session owned by the
// app's main frame (the window is built around the holder). Connect() stays
// as a convenience wrapper for callers without inter-phase hooks
// (codeRunner, appserver, tests).
// ---------------------------------------------------------------------------


///////////////////////////////////////////////////////////////////////////////
#include "backend/debugger/debugClient.h"

#pragma region execute 
long ibApplicationInstance::RunApplication(const wxString& strAppName, bool searchDebug, bool useManifest) const
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

long ibApplicationInstance::RunApplication(const wxString& strAppName, const wxString& strUserName, const wxString& strUserPassword, bool searchDebug, bool useManifest) const
{
	// All OES binaries spawn with unified `--flag=value` syntax using
	// wenterprise-server's long-name set (server/dbport/db/user/password/
	// file/ibuser/ibpwd/locale/debug). enterprise.exe / designer.exe /
	// appserver.exe declare these as the long name of their legacy short
	// options, so one builder feeds every parser.

	// Resolve the binary next to this one. A bare "enterprise" is looked up on PATH and in the
	// working directory, and it is in neither; on macOS it is not even a plain file, since the
	// sibling is enterprise.app and the executable sits inside it. Without this the fork succeeds,
	// the exec fails, and wxExecute still returns a pid — so the caller is told the application
	// started while nothing runs.
	wxFileName home = wxFileName::DirName(
		wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());

#ifdef __WXOSX__
	// This binary is itself inside a bundle, so its siblings are three levels up.
	if (home.GetDirCount() >= 2
		&& home.GetDirs().Last().IsSameAs(wxT("MacOS"))
		&& home.GetDirs()[home.GetDirCount() - 2].IsSameAs(wxT("Contents"))) {
		home.RemoveLastDir();
		home.RemoveLastDir();
		home.RemoveLastDir();
	}
#endif

	wxFileName binary(home);
	binary.SetFullName(strAppName);

#ifdef __WXOSX__
	if (!binary.FileExists()) {
		wxFileName bundled(home);
		bundled.AppendDir(strAppName + wxT(".app"));
		bundled.AppendDir(wxT("Contents"));
		bundled.AppendDir(wxT("MacOS"));
		bundled.SetFullName(strAppName);
		if (bundled.FileExists())
			binary = bundled;
	}
#endif

	// The bare name stays the fallback, so a layout this does not recognise behaves as before.
	// Quoted because a resolved path can contain a space and wxExecute splits on those.
	wxString executeCmd = binary.FileExists()
		? (wxT('"') + binary.GetFullPath() + wxT("\" "))
		: (strAppName + wxT(' '));

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
		// QUOTED, for the same reason the binary above is: wxExecute splits the command on spaces, so
		// a base under "C:\My Bases\…" reached the child as two arguments and the second one was not a
		// flag anything declared. The path is the one thing here a person chooses, so it is the one
		// most likely to carry a space (2026-08-20).
		executeCmd += wxString::Format(wxT(" --file=\"%s\""), m_strFile);
	}

	if (searchDebug)
		executeCmd += wxT(" --debug");

	if (!strUserName.IsEmpty())
		executeCmd += wxString::Format(wxT(" --ibuser=%s"), strUserName);

	if (!strUserPassword.IsEmpty())
		executeCmd += wxString::Format(wxT(" --ibpwd=%s"), strUserPassword);

	ibApplicationHost* const host = ibApplicationHost::Get();
	executeCmd += wxString::Format(wxT(" --locale=%s"), host != nullptr ? host->GetLocaleName() : wxString());

	// Manifest-mode: wes-style spawn. Skip the debug-client handshake —
	// that's enterprise.exe's in-process debugger attach, not relevant
	// to a headless wes child.
	if (useManifest)
		return SpawnWebServerWithManifest(executeCmd, searchDebug);

	const long execute = wxExecute(executeCmd);

	if (searchDebug) {
		// Scan in rounds, the same way the manifest path below does, and for the same
		// reason: one sweep races the process that was just started. Its debug server
		// is created near the end of bootstrap, after the database is open, so the
		// first sweep finds nothing listening and its threads exit on connect-refused
		// with the counter spent - which is what lets the next SearchServer call make
		// fresh ones. A single sweep followed by a 1.5-second wait, which is what stood
		// here, could only ever succeed on a fast warm start.
		const int kRounds       = 60;   // 60 rounds * 250ms = ~15s, and it stops on success
		const int kRoundSleepMs = 250;

		for (int round = 0; round < kRounds; ++round) {

			if (debugClient == nullptr)
				break;

			debugClient->SearchServer(true);
			wxMilliSleep(kRoundSleepMs);

			if (debugClient->GetConnectionSuccess())
				break;
		}
	}

	return execute;
}

long ibApplicationInstance::SpawnWebServerWithManifest(wxString cmd, bool searchDebug)
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

bool ibApplicationInstance::AuthenticateUser(const wxString& strUserName,
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

void ibApplicationInstance::InstallUser(const ibUserInfo& info,
                                     const wxString& rawPassword)
{
	// User identity now lives only on the ibSession. The registry thread
	// calls InstallUser under a ibSessionScope bound to the target session
	// (ProcessAttach); the desktop login dialog calls it under the main-
	// thread scope of the session that owns the dialog. Without a current
	// session there is nowhere to install — the caller is in a pre-auth
	// path that has no business calling this.
	if (auto* ctx = ibSession::Current()) {
		if (auto* registry = ibApplicationInstance::GetSessionRegistry())
			registry->InstallUser(ctx, info, rawPassword);
	}
}

bool ibApplicationInstance::Login(const wxString& strUserName,
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

bool ibApplicationInstance::LoadDatabase(const wxString& strFullPath)
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

bool ibApplicationInstance::SaveDatabase(const wxString& strFullPath)
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

bool ibApplicationInstance::ClearDatabase()
{
	if (!m_created_metadata)
		return false;

	if (!activeMetaData->ReCreateDatabase())
		return false;

	return true;
}

wxString ibApplicationInstance::GetDatabaseDescription() const
{
	// Where the base lives: a PostgreSQL server's database, or a Firebird folder (none — the launcher's, no base).
	if (!m_strServer.IsEmpty())
		return m_strServer + wxT(":") + m_strPort + wxT("/") + m_strDatabase;
	return m_strFile;
}

///////////////////////////////////////////////////////////////////////////////

#include "backend/utils/md5.hpp"

wxString ibApplicationInstance::ComputeMd5(const wxString& userPassword) const
{
	if (userPassword.Length() > 0)
		return ibMD5::ComputeMd5(userPassword);

	return wxEmptyString;
}

///////////////////////////////////////////////////////////////////////////////


