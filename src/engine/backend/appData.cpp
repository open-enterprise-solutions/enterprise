////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider
//	Description : app info
////////////////////////////////////////////////////////////////////////////

#include "backend/appData.h"
#include "backend/appHost.h"
#include "core/launchTicket.h"

#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <utility>   // std::exchange — the active configuration handed over

#include <wx/ffile.h>      // infobase.conf — written for a new base
#include <wx/fileconf.h>   // infobase.conf — a base's own settings
#include <wx/stdpaths.h>

#include "core/fileSystem.h"
#include "core/memory/memoryPool.h"   // Drain — the pool's free list handed back at teardown

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
#include "core/server/serverConfig.h"      // a server reads where its base lives itself

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

ibString ibApplicationInstance::ResolveLogDir() const
{
	const ibString sep(ibFILE_SEP_PATH);
	if (m_strServer.IsEmpty() && !m_strFile.IsEmpty()) {
		// Lives next to sys.fdb — admins see logs alongside the base.
		return m_strFile + sep + ibT("oeslog");
	}
	if (!m_strServer.IsEmpty()) {
		// A server keeps each base in a folder of its own — the journal lives there, beside nothing else
		// (a file base's folder holds the database as well).
		if (!m_strDirLocal.IsEmpty())
			return m_strDirLocal + sep + ibT("oeslog");

		// Per-user persistent location. %TEMP% would be wiped by
		// Windows disk cleanup; %LOCALAPPDATA% survives reboots and
		// "temp-file cleanup". Until compute-server arrives
		// this is the only place a client's journal lives.
		//
		// ⚠ KEYED BY THE SERVER AND THE BASE. The base's name alone collided: two bases called the same on
		// two servers — which one process of several bases may hold at once — wrote one journal.
		const auto sanitised = [](ibString tag) {
			// Path separators in db names would break Mkdir.
			tag.Replace(ibT("\\"), ibT("_"));
			tag.Replace(ibT("/"),  ibT("_"));
			tag.Replace(ibT(":"),  ibT("_"));
			return tag;
		};
		ibString legacy = m_strDatabase;
		if (legacy.IsEmpty()) legacy = m_strServer;
		if (legacy.IsEmpty()) legacy = ibT("default");
		const ibString tag = (!m_strServer.IsEmpty() && !m_strDatabase.IsEmpty())
			? sanitised(m_strServer + ibT("_") + m_strDatabase)
			: sanitised(legacy);

		const ibString root = wxStandardPaths::Get().GetUserLocalDataDir() + sep + ibT("OES") + sep;
		// …and a journal kept under the name alone is MOVED to the new key once, so its history stays
		// readable instead of starting over beside it. A move that fails (the folder in use) costs only that.
		const ibString old = root + sanitised(legacy);
		if (tag != sanitised(legacy) && !ibDirExists(root + tag) && ibDirExists(old))
			ibRenameFile(old, root + tag);

		return root + tag + sep + ibT("logs");
	}
	return ibEmptyString;
}

void ibApplicationInstance::CreateLogger()
{
	if (m_runMode == ibRunMode::eLAUNCHER_MODE) return;
	const ibString dir = ResolveLogDir();
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
ibString DescribeSessionKind(ibSessionKind k) {
	switch (k) {
	case ibSessionKind::Launcher:   return ibT("Launcher");
	case ibSessionKind::Designer:   return ibT("Designer");
	case ibSessionKind::Enterprise: return ibT("Enterprise");
	case ibSessionKind::Service:    return ibT("Service");
	case ibSessionKind::WebServer:  return ibT("WebServer");
	case ibSessionKind::WebClient:  return ibT("WebClient");
	case ibSessionKind::BackgroundJob: return ibT("BackgroundJob");
	case ibSessionKind::ScheduledJob:  return ibT("ScheduledJob");
	case ibSessionKind::SystemJob:     return ibT("SystemJob");
	case ibSessionKind::ThinClient:    return ibT("ThinClient");
	case ibSessionKind::ThinDesigner:  return ibT("ThinDesigner");
	case ibSessionKind::Unknown:       return ibT("Unknown");
	}
	return ibT("Unknown");
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
		ibString(thread.str()));
	return nullptr;
}

ibHelpService* ibApplicationInstance::GetHelpService()
{
	ibApplicationHost* const host = ibApplicationHost::Get();
	return host != nullptr ? host->GetHelpService() : nullptr;
}

ibString ibApplicationInstance::GetLocale() const
{
	if (m_dictionary != nullptr)
		return m_dictionary->GetLanguage();
	ibApplicationHost* const host = ibApplicationHost::Get();
	return host != nullptr ? host->GetLocale() : ibString();
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
static void WriteInfobaseConf(const ibString& folder)
{
	const ibString path = folder + ibFILE_SEP_PATH + ibT("infobase.conf");
	if (folder.IsEmpty() || ibFileExists(path))
		return;
	wxFFile file(path, wxT("w"));
	if (!file.IsOpened() || !file.Write(
			wxT("; This base's own settings - read when the base is opened.\n")
			wxT("; Connections - connections to its DBMS at most; 0 - the default (backend.conf, else 32).\n")
			wxT("Connections = 0\n"))) {
		ibTechJournal::Print(ibJournalMark::Warning, ibT("appdata"),
			ibT("%s cannot be written - the base runs on the defaults"), path);
	}
}

// HOW MANY CONNECTIONS TO ITS DBMS THIS BASE MAY HOLD — the base's own word, infobase.conf in its folder
// (Connections), over the process's default (backend.conf). A base's connections are its own: a PostgreSQL
// base and an MS SQL one are two worlds, so the number lives with each base. A value that cannot be used is
// said, not quietly replaced. Two at least: the registry holds one for its writes, a session needs another.
static std::size_t ReadInfobaseConnections(const ibString& folder, std::size_t byDefault)
{
	const ibString path = folder + ibFILE_SEP_PATH + ibT("infobase.conf");
	if (folder.IsEmpty() || !ibFileExists(path))
		return byDefault;

	const wxFileConfig conf(wxT(""), wxT(""), path, wxT(""), wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);
	return ibApplicationHost::ReadCount(conf, path, ibT("Connections"), 2, byDefault);
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

// Fabric — replaces ibMetaDataConfigurationBase::Initialize. The configuration the application runs, put in
// `m_activeMetaData` for the sessions to come — each takes its own reference — loaded and run. On the application
// data NAMED: the opening passes the base it opens rather than let the thread resolve one.
bool ibApplicationInstance::CreateActiveMetaData(ibApplicationInstance* applicationInstance, int flags)
{
	if (applicationInstance == nullptr)
		return false;
	if (applicationInstance->m_activeMetaData)
		return false;   // already initialised — caller passes a fresh appData

	// The metadata subclass ctors are gated on ib::AppDataCtorToken — this TU is ibApplicationInstance's, so it can
	// mint the token; nobody outside can.
	const std::shared_ptr<ibMetaDataConfigurationBase> metaData =
		ibMetaData::MakeShared<ibMetaDataConfiguration>(ib::AppDataCtorToken{ applicationInstance });

	// In place BEFORE it initialises — its loading asks for `activeMetaData`; the opening thread works in it through
	// a scope meanwhile.
	{
		std::lock_guard<std::mutex> lk(ibApplicationHost::s_mutex);
		applicationInstance->m_activeMetaData = metaData;
	}
	const ibApplicationInstanceScope initialising(applicationInstance);
	return metaData->OnInitialize(flags) && metaData->RunDatabase();
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

bool ibApplicationInstance::ReplaceActiveMetaData()
{
	// ⭐ THE BASE READS ITS OWN, made with its own token — for no session, whatever thread asks: the designer's apply
	// asks on its own, and what the designer reads there is the designer's (its compile cache with it). Read as the
	// apply's copy is read, the mirror of the database (loadConfigFlag).
	const std::shared_ptr<ibMetaDataConfiguration> metaData =
		ibMetaData::MakeShared<ibMetaDataConfiguration>(ib::AppDataCtorToken{ this });
	if (!metaData->LoadDatabase(onlyLoadFlag) || !metaData->RunDatabase(loadConfigFlag))
		return false;
	ReplaceActiveMetaData(metaData);
	return true;
}

void ibApplicationInstance::ReplaceActiveMetaData(std::shared_ptr<ibMetaDataConfigurationBase> metaData)
{
	const ibString digest = metaData->GetConfigMD5();
	std::shared_ptr<ibMetaDataConfigurationBase> previous;
	{
		std::lock_guard<std::mutex> lk(ibApplicationHost::s_mutex);
		previous = std::exchange(m_activeMetaData, std::move(metaData));
	}

	// …and the base lets the old one go. The sessions working in it hold it, each by its own reference, and the
	// last of them closes it (ibMetaData::MakeShared) — or the end of this call does, when nobody works in it.
	ibJournalInfo(ibT("metadata"), ibT("the active configuration is now %s; the one before stays with %ld holder(s)"),
		digest, previous != nullptr ? previous.use_count() - 1 : 0L);
}

void ibApplicationInstance::WireSessionEvents()
{
	auto* registry = m_sessionRegistry.get();
	if (registry == nullptr) return;

	// ⚠ EVERY LISTENER HERE WORKS ON ITS OWN BASE — `this`, its members — and never through `appData`,
	// `activeMetaData` or `ibLog`: those answer the base of the thread that fires the listener, and with
	// several bases in the process the registry thread of one base must not be asked which base it is.
	//
	// (The configuration is up already — loaded and run as the base opened (Open), before anybody was let in.)

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
				m_logger->Audit(ibT("session"), ibT("opened"),
				             ibString::Format(ibT("kind=%s id=%s"),
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
		// …and a thin client's in a FILE base: there the process IS the thin client (fileserver in its own process) —
		// on a server the same session is one visitor among many. Asked by the
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

		if (s->GetMetaData() != nullptr) {
			// Root mm is allocated by ibSession::EnsureRoot (called by the registry before this listener) for
			// runtime sessions; the Designer has no root mm (it uses the lightweight designer manager in the
			// compile cache). The session's configuration comes to it run: the active one ran as the base opened,
			// a replacement arrives run (ReplaceActiveMetaData), a designer's own ran as it was made
			// (ibSession::AcquireMetaData).
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
				m_logger->Audit(ibT("session"), ibT("closed"),
				             ibString::Format(ibT("kind=%s id=%s"),
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

	// Last authenticated session out — request process exit; the keep-alive predicate (web tabs, etc.) can
	// decline. The configuration stays: it is the base's, up while the base is open, and closes with it (Close).
	registry->OnLastDisconnect([this]() {
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

	// ⚠ EACH FIELD NULL BEFORE IT DIES — reset(), not the default unwinding. The closing thread works for
	// this base (ibApplicationHost::Close), and what a field's destructor asks — a connection holder its
	// pool, the pool the current session, the session its registry — is answered down the chain through
	// THESE pointers. A unique_ptr left to its own destructor keeps pointing at what it has deleted: the
	// chain read the freed registry and every process crashed on exit (2026-10-01). Each field is emptied
	// first, so a question asked after it is gone reads "none" — emptied UNDER THE PROCESS'S LOCK, which
	// HasRegistry / HasPool read the fields under from any thread, and deleted outside it, where its own
	// destructor may ask them.
	const auto release = [](auto& field) {
		std::remove_reference_t<decltype(field)> gone;
		{
			std::lock_guard<std::mutex> lk(ibApplicationHost::s_mutex);
			gone = std::move(field);
		}
	};

	// 2. activeMetaData — OnDestroy may save state, close compile
	//    caches, run cascading detach; those paths still want db_query.
	//    …and the base lets its reference go while the pool still stands: every session is gone by now, so the
	//    base is its last holder, and it closes as every last holder's does (ibMetaData::MakeShared).
	if (m_activeMetaData) m_activeMetaData->OnDestroy();
	release(m_activeMetaData);

	// (Plugins are the process's: they are unloaded by ibApplicationHost once
	//  every base is gone, not by each base.)

	// 3. Pool — close master + clones, invalidate outstanding hand-outs.
	//    Runs AFTER the registry's Stop so any session-bound DB work
	//    has already completed.
	if (m_connectionPool) m_connectionPool->Shutdown();

	// 4. The rest of the fields, in reverse declaration order: m_tempStorage → m_settingsStorage →
	//    m_mcpServer → m_jobManager → m_sessionRegistry → m_logger → m_queryableFactory → m_lockManager →
	//    m_connectionPool (m_activeMetaData, declared last, went first). (The job manager's own dtor calls
	//    Stop() again; it is idempotent, so the explicit step above only fixes the ORDER.)
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

const ibString& ibApplicationInstance::GetUserName() const
{
	return GetUserInfo().m_strUserName;
}

const ibString& ibApplicationInstance::GetUserPassword() const
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

ibString ibApplicationInstance::GetUserLanguageCode() const
{
	return GetUserInfo().m_strLanguageCode;
}

ibString ibApplicationInstance::ComputeMd5() const
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

#define sys_db ibT("sys.fdb")

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
		ibBackendCoreException::Error(ibT("no config at %s"), config.GetConfPath());
	ibString error;
	if (!config.LoadKey(error))
		ibBackendCoreException::Error(ibT("%s"), error);

	std::vector<ibConfiguredInstance> instances = config.ReadInstances();
	config.AssignIds(instances);   // its folder is named by its Id
	const auto base = std::find_if(instances.begin(), instances.end(),
		[&request](const ibConfiguredInstance& instance) { return instance.m_name == request.m_name; });
	if (base == instances.end())
		ibBackendCoreException::Error(ibT("the config %s has no base '%s'"), config.GetConfPath(), request.m_name);

	ibInstanceStorage storage;
	storage.m_directory = base->m_path;
	if (base->m_kind == ibT("postgresql")) {
		storage.m_server   = base->m_server;
		storage.m_port     = base->m_port;
		storage.m_user     = base->m_user;
		storage.m_database = base->m_database;
		ibString password;
		if (!config.OpenSecret(base->m_name, ibT("Password"), base->m_password, password, error))
			ibBackendCoreException::Error(ibT("%s"), error);
		storage.m_password = password;
	}
	else if (base->m_kind != ibT("firebird"))
		ibBackendCoreException::Error(ibT("unknown Kind '%s' - expected firebird or postgresql"), base->m_kind);
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
		const ibString database = storage.m_directory + ibFILE_SEP_PATH + sys_db;
		if (!create && !ibFileExists(database))
			ibBackendCoreException::Error(_("There is no base in %s (no %s), and this opening does not create one."),
				storage.m_directory, sys_db);

		std::shared_ptr<ibDatabaseLayerFirebird> db(new ibDatabaseLayerFirebird());
		if (!db->Open(database))
			return nullptr;

		std::unique_ptr<ibApplicationInstance> opening(new ibApplicationInstance(host, runMode));
		opening->m_strFile = storage.m_directory;
		const std::filesystem::path folder = ibPathOf(storage.m_directory).lexically_normal();   // "base/" and "base" alike
		const ibString last(folder.has_filename() ? folder.filename().wstring() : folder.parent_path().filename().wstring());
		opening->m_strInstance = !request.m_name.IsEmpty() ? request.m_name
			: last.IsEmpty() ? storage.m_directory : last;

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

// THE LOCALE THE BASE NAMES — a shared row of sys_settings (Regional, a fixed object, no user): which language its
// words come out in belongs to the base, not to the process, since one process holds several bases. Empty — it
// names none.
static ibString ReadLocale(const ibApplicationInstance* applicationInstance)
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(applicationInstance);
	ibDataNode row;
	ibString locale;
	if (storage != nullptr && storage->Restore(ibSettingsKey(ibSettingsCategory::Regional, ibGuid(ibT("72656769-6f6e-616c-0000-000000000002"))), row))
		row.GetValue(ibT("locale"), locale);
	if (!locale.IsEmpty())
		ibJournalInfo(ibT("session.locale"), ibT("the base names the locale %s"), locale);
	return locale;
}

// ⭐ THE ONE ROAD A BASE COMES UP BY, once its database is open — a file base and a server base alike: listed in
// the process, the opening thread working for it, then its pool, its tables, its locale, its journal and its jobs.
// A base that does not come up — refused or thrown — is closed again, alone, and the thread gets back what it had.
ibApplicationInstance* ibApplicationInstance::Open(std::unique_ptr<ibApplicationInstance> opening,
	std::shared_ptr<ibDatabaseLayer> db, const ibString& folder, const ibInstanceRequest& request, bool create)
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
		ibJournalInfo(ibT("appdata"), ibT("connected: %s (run mode %d)"),
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
		ibString heldBy;
		if (TableAlreadyCreated() && !ibServiceExclusivePolicy::CanOpen(runMode, heldBy))
			ibBackendCoreException::Error(ibT("%s"), heldBy);

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
		// The configuration's own — what the database publishes, what a designer saves, its sequence: read at the
		// end of this opening. A designer's first login used to make them, and it was the only one that read them.
		if (!ibMetaDataConfigurationStorage::TableAlreadyCreated()) {
			ibMetaDataConfigurationStorage::CreateConfigTable();
			ibMetaDataConfigurationStorage::CreateConfigSaveTable();
			ibMetaDataConfigurationStorage::CreateConfigSequence();
		}

		if (!SetLocaleAppDataEnv(request.m_locale))
			return refuse();
		applicationInstance->m_dictionary = ibDictionary::ForLanguage(ReadLocale(applicationInstance));

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

		// ⭐ THE CONFIGURATION, UP BEFORE ANYBODY COMES IN (Max, 2026-10-20: *"first the metadata is loaded, then the
		// users connect — not the first user, as before"*). The base's first session used to make it and the first
		// to be let in ran it, so two first logins at once ran it twice (`!IsConfigOpen()` in RunDatabase, under
		// the web server's load test). Now every session takes it ready, by its own reference; a designer's session
		// works in one of its own (ibSession::AcquireMetaData). A configuration that does not come up refuses the base.
		applicationInstance->m_loadMetadataFlags = request.m_loadMetadataFlags;
		if (!CreateActiveMetaData(applicationInstance, request.m_loadMetadataFlags))
			ibBackendCoreException::Error(_("The configuration of the base %s could not be loaded."),
				applicationInstance->GetDatabaseDescription());

		// …THEN THE DOOR THE SESSIONS COME IN BY — the registry, its policies and its thread. It too used to start with
		// the base's first session, and two hundred first logins at once each started it (the web server's load test,
		// 2026-10-20: the policy vector torn under a push_back).
		applicationInstance->m_sessionRegistry->Start();

		// …and ITS JOBS RUN WITH IT, from the moment it is up, whoever comes in or not (Max, 2026-10-20: *"with no
		// users, a background job would never start"*): the tick runs the schedule and gives a finished background
		// run its session back, and neither waits for a person. It used to start with the base's first session.
		applicationInstance->m_jobManager->Start();

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

bool ibApplicationInstance::SetLocaleAppDataEnv(const ibString& strLocale)
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

		// Last: hand the pool's free list (texts' and buffers' blocks) back. It is a cache, but the CRT cannot tell a
		// cache from a leak — every cached block sits in the exit dump holding its old contents,
		// which is where the "leaked" fragments of metadata names came from. Draining here, past
		// the metadata tree and every session, leaves the dump saying only what actually leaked.
		// Per-module and per-thread (see the note on Drain), so this covers the backend's main
		// thread — the one that churns strings.
		ibMemoryPool::Drain();

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
#include "core/diagnostics/crashGuard.h"   // CurrentPidPortable — whom a debug run goes with
#include "core/startProcess.h"   // ibStartProcess — a launch is asked for from a fiber too (app_run on the server)

#pragma region execute 
long ibApplicationInstance::RunApplication(const ibString& strAppName, bool searchDebug, bool useManifest, ibString* page) const
{
	// Hand the child process the raw password captured at login, not the stored hash.
	// Otherwise the child authenticates with the hash itself — which only worked
	// before MD5→PBKDF2 because the verifier silently treated hash==hash as a match,
	// and even then it let the stored hash act as a bearer token.
	//
	// Creds come from the calling thread's ibSession (ibSessionScope set
	// by the host app's mainApp once session->Open succeeds). If no
	// session is scoped — codeRunner or pre-auth tools that have no
	// business spawning interactive children — we still call through with
	// empty strings; the child will fall back to its own login flow.
	ibString userName, rawPassword;
	if (auto* s = ibSession::Current()) {
		userName    = s->GetUserInfo().m_strUserName;
		rawPassword = s->GetSessionRawPassword();
	}
	const long started = RunApplication(strAppName, userName, rawPassword, searchDebug, useManifest, page);
	// The session keeps the password. A second launch of this same session
	// still has it; only the copy on this stack is overwritten.
	for (size_t i = 0; i < rawPassword.length(); ++i)
		rawPassword[i] = ibT('x');
	rawPassword.clear();
	return started;
}

long ibApplicationInstance::RunApplication(const ibString& strAppName, const ibString& strUserName, const ibString& strUserPassword, bool searchDebug, bool useManifest, ibString* page) const
{
	// All OES binaries spawn with unified `--flag=value` syntax: one long-name
	// set (server/dbport/db/user/file/ibuser/locale/debug). Passwords go on
	// the child's stdin. enterprise-thin.exe / designer-thin.exe declare these as the
	// long name of their legacy short options, so one builder feeds every
	// parser - webserver aside, below.

	const auto add = [](ibString& cmd, const ibString& name, const ibString& value) {
		if (value.IsEmpty()) return;
		cmd += ibT(" --") + name + ibT("=\"") + value + ibT("\"");
	};

	// Resolve the binary next to this one (ibExecutablePath). A bare "enterprise" is looked up on PATH and in the
	// working directory, and it is in neither; on macOS it is not even a plain file, since the sibling is enterprise.app
	// and the executable sits inside it. Without this the fork succeeds, the exec fails, and wxExecute still returns a
	// pid — so the caller is told the application started while nothing runs. Quoted because a resolved path can contain
	// a space and the command is split on those.
	ibString executeCmd = ibT('"') + ibExecutablePath(strAppName) + ibT("\" ");

	// THE WEB SERVER publishes a file base: the base, its debugger, its language - a login is the browser's, and a
	// base on a DBMS server is no publication of it (webserver/webServer.h). Its page is opened once the server says
	// where it is.
	if (useManifest) {
		if (m_strFile.IsEmpty()) {
			ibJournalWarning(ibT("process"), ibT("%s publishes a file base, not one on a DBMS server"), strAppName);
			return 0;
		}
		add(executeCmd, ibT("file"), m_strFile);
		if (searchDebug)
			executeCmd += ibString::Format(ibT(" --debug --owner=%u"), ibCrashGuard::CurrentPidPortable());   // a debug run goes with whoever debugs it
		add(executeCmd, ibT("locale"), GetLocale());   // the base's, else the process's: the child comes up as this base's clients do

		// The wait is the core's (std::this_thread::sleep_for between its looks, up to five seconds). The page is the
		// caller's to open — a browser is a window's, and the engine draws none.
		ibString url;
		const long pid = ibStartWebServer(executeCmd, url);
		if (page != nullptr)
			*page = url;

		// Web debug attach: the server is up with --debug, its in-process
		// ibDebuggerServer is listening on one of ibPort::Debugger
		// in non-blocking (wait=false) mode. SearchServer scans + verifies
		// each port, but verified+Scanner stays Scanner — designer never
		// auto-promotes to Debugger on a non-waiting server. We retry
		// SearchServer in rounds because a single sweep can race with the
		// server's metadataCreate (debug server still coming up) — first sweep's
		// scan threads exit on connect-refused, leaving counter=-1, which
		// lets the next SearchServer call create fresh threads. Once verify
		// succeeds (m_connectionSuccess), AttachAllVerified pushes
		// CommandId_StartSession so the server flips to Debugger mode.
		if (searchDebug && pid != 0) {
			const int kRounds      = 20;   // 20 rounds * 250ms = ~5s
			const int kRoundSleepMs = 250;
			for (int round = 0; round < kRounds; ++round) {
				if (debugClient == nullptr) break;
				debugClient->SearchServer(true);
				std::this_thread::sleep_for(std::chrono::milliseconds(kRoundSleepMs));
				if (debugClient->GetConnectionSuccess())
					break;
			}
			if (debugClient != nullptr && debugClient->GetConnectionSuccess())
				debugClient->AttachAllVerified();
		}
		return pid;
	}

	if (m_strFile.IsEmpty()) {
		add(executeCmd, ibT("server"), m_strServer);
		add(executeCmd, ibT("dbport"), m_strPort);
		add(executeCmd, ibT("db"), m_strDatabase);
		add(executeCmd, ibT("user"), m_strUser);
	}
	else {
		// QUOTED, for the same reason the binary above is: the command is split on spaces, so
		// a base under "C:\My Bases\…" reached the child as two arguments and the second one was not a
		// flag anything declared. The path is the one thing here a person chooses, so it is the one
		// most likely to carry a space (2026-08-20).
		add(executeCmd, ibT("file"), m_strFile);
	}

	if (searchDebug)
		executeCmd += ibT(" --debug");

	add(executeCmd, ibT("ibuser"), strUserName);

	ibLaunchSecrets secrets;
	secrets.m_dbUser     = m_strUser;
	secrets.m_dbPassword = m_strPassword;
	secrets.m_ibUser     = strUserName;
	secrets.m_ibPassword = strUserPassword;

	add(executeCmd, ibT("locale"), GetLocale());   // the base's, else the process's: the child comes up as this base's clients do

	const long execute = ibStartProcess(executeCmd, secrets);

	if (searchDebug && execute != 0) {
		// Scan in rounds, the same way the web server's path above does, and for the same
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
			std::this_thread::sleep_for(std::chrono::milliseconds(kRoundSleepMs));

			if (debugClient->GetConnectionSuccess())
				break;
		}
	}

	return execute;
}

#pragma endregion

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationInstance::AuthenticateUser(const ibString& strUserName,
                                          const ibString& strUserPassword,
                                          ibUserInfo& outInfo)
{
	// An empty name used to be accepted whenever HasAny() came back false,
	// and HasAny() came back false both for an empty table and for a
	// query that threw. An empty list that was actually read is open
	// access on every host, including the application server and the web
	// server. A list that could not be read is a refusal.
	if (strUserName.IsEmpty()) {
		bool noUsers = false;
		try {
			noUsers = !ibUserInfo::HasAny();
		}
		catch (const ibBackendInterruptException&) {
			throw;
		}
		catch (const ibCoreException& err) {
			try {
				if (ibLog) ibLog->Audit(ibT("auth"), ibT("login_failed"),
				                        ibString::Format(ibT("reason=user_list_unreadable detail=%s"), err.GetErrorDescription()));
			} catch (...) {}
			return false;
		}

		if (noUsers) {
			try {
				if (ibLog) ibLog->Audit(ibT("auth"), ibT("open_access"), ibT("reason=empty_user_list"));
			} catch (...) {}
			return true;
		}
	}

	outInfo = ibUserInfo::Read(strUserName);
	if (!outInfo.IsOk()) {
		// An unknown name refuses no sooner than a wrong password: refused before any hashing, it
		// answered hundreds of milliseconds faster and told which names exist. Hash runs the same
		// PBKDF2 that Verify runs on a stored hash; its answer is thrown away.
		(void)ibPasswordHash::Hash(strUserPassword);
		try {
			if (ibLog) ibLog->Audit(ibT("auth"), ibT("login_failed"),
			                        ibString::Format(ibT("user=%s reason=unknown"), strUserName));
		} catch (...) {}
		return false;
	}

	if (!ibPasswordHash::Verify(strUserPassword, outInfo.m_strUserPassword)) {
		try {
			if (ibLog) ibLog->Audit(ibT("auth"), ibT("login_failed"),
			                        ibString::Format(ibT("user=%s reason=bad_password"), strUserName),
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
			if (ibLog) ibLog->Audit(ibT("auth"), ibT("password_rehash"),
			                        ibString::Format(ibT("user=%s"), strUserName),
			                        outInfo.m_strUserGuid, /*refMetaId=*/0);
		} catch (...) {
			// ignore — login already succeeded
		}
	}

	return true;
}

void ibApplicationInstance::InstallUser(const ibUserInfo& info,
                                     const ibString& rawPassword)
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

bool ibApplicationInstance::Login(const ibString& strUserName,
                              const ibString& strUserPassword,
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
				ibLog->Audit(ibT("auth"), ibT("login"),
				             ibString::Format(ibT("user=%s"), strUserName),
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

#include "core/memory/memoryStream.h"   // ibReaderMemory — a user/data member is walked before the base is touched

namespace {

bool ReadZipEntry(wxZipInputStream& zis, wxZipEntry& entry, ibMemoryBuffer& out)
{
	if (!zis.OpenEntry(entry))
		return false;

	wxMemoryOutputStream fos;
	if (!fos.IsOk())
		return false;

	zis.Read(fos);
	const wxStreamError err = zis.GetLastError();
	if (err != wxSTREAM_NO_ERROR && err != wxSTREAM_EOF)
		return false;

	const size_t n = fos.GetSize();
	out.SetDataLen(0);
	if (n > 0)
		fos.CopyTo(out.GetAppendBuf(n), n);
	out.SetDataLen(n);
	return true;
}

// The same walk LoadUserInfoFromBuffer uses. A member whose chunk header does
// not fit is a truncated backup: false, and the caller has not cleared anything.
// An empty member is a real backup (no users, or no rows).
bool BufferWalks(const ibMemoryBuffer& buffer)
{
	if (buffer.GetDataLen() == 0)
		return true;

	try {
		ibReaderMemory reader(buffer);
		std::uint64_t id = 0;
		ibReaderMemory* last = nullptr;
		while (!reader.eof()) {
			// The iterator closes `last` itself before reading the next header.
			ibReaderMemory* chunk = reader.open_chunk_iterator(id, last);
			last = nullptr;
			if (chunk == nullptr)
				break;
			last = chunk;
		}
		if (last != nullptr)
			last->close();
		return true;
	}
	catch (const ibCoreException&) {
		return false;
	}
}

} // namespace

bool ibApplicationInstance::LoadDatabase(const ibString& strFullPath)
{
	wxFileInputStream fis(strFullPath);
	if (!fis.IsOk()) {
		ibJournalError(ibT("appdata"),"Couldn't open the file '%s'.", strFullPath);
		return false;
	}

	return LoadDatabase(fis);
}

bool ibApplicationInstance::LoadDatabase(wxInputStream& stream)
{
	// The archive is read to the end, and refused, before either clear. An empty
	// zip, a file that is not a zip, or a backup this build cannot read used to
	// drop the configuration tables and every user, then return success.
	wxZipInputStream zis(stream);
	if (!zis.IsOk()) {
		ibJournalError(ibT("appdata"), "The file is not a readable archive. Nothing was cleared.");
		return false;
	}

	ibMemoryBuffer config;
	ibMemoryBuffer user;
	ibMemoryBuffer data;
	bool sawConfig = false;
	bool sawUser = false;
	bool sawData = false;
	bool duplicate = false;
	bool dataBeforeConfig = false;

	std::unique_ptr<wxZipEntry> entry;
	while (entry.reset(zis.GetNextEntry()), entry) {
		if (entry->IsDir())
			continue;

		const ibString name = entry->GetName();
		const bool isConfig = name == ibT("config");
		const bool isUser = name == ibT("user");
		const bool isData = name == ibT("data");
		if (!isConfig && !isUser && !isData)
			continue;

		ibMemoryBuffer buffer;
		if (!ReadZipEntry(zis, *entry, buffer)) {
			ibJournalError(ibT("appdata"), "The archive entry '%s' could not be read. Nothing was cleared.", name);
			return false;
		}

		if (isConfig) {
			duplicate = duplicate || sawConfig;
			config = buffer;
			sawConfig = true;
		}
		else if (isUser) {
			duplicate = duplicate || sawUser;
			user = buffer;
			sawUser = true;
		}
		else {
			duplicate = duplicate || sawData;
			if (!sawConfig)
				dataBeforeConfig = true;
			data = buffer;
			sawData = true;
		}
	}

	if (duplicate) {
		ibJournalError(ibT("appdata"), "The archive repeats config, user or data. Nothing was cleared.");
		return false;
	}

	// Rows can only be written once the structure holding them exists. Our own
	// writer emits config before data; a file that says otherwise is refused
	// rather than landing rows in whatever structure the database already has.
	if (dataBeforeConfig) {
		ibJournalError(ibT("appdata"), _("The file carries data before the configuration - it cannot be loaded"));
		return false;
	}

	if (!sawConfig || !sawUser || !sawData) {
		ibJournalError(ibT("appdata"), "The archive must contain config, user and data. Nothing was cleared.");
		return false;
	}

	if (!BufferWalks(user) || !BufferWalks(data)) {
		ibJournalError(ibT("appdata"), "The archive's user or data entry is truncated. Nothing was cleared.");
		return false;
	}

	if (activeMetaData == nullptr) {
		ibJournalError(ibT("appdata"), "There is no configuration to load the file into. Nothing was cleared.");
		return false;
	}

	// The configuration is read first by the very reader that applies it below, into a
	// separate configuration that nothing runs: a blob that reader refuses leaves the
	// base untouched.
	try {
		if (!ibMetaData::MakeShared<ibMetaDataConfigurationFile>()->LoadConfigFromBuffer(config)) {
			ibJournalError(ibT("appdata"), "The file has no configuration this build can read. Nothing was cleared.");
			return false;
		}
	}
	catch (const ibCoreException& err) {
		ibJournalError(ibT("appdata"), "%s Nothing was cleared.", err.GetErrorDescription());
		return false;
	}

	// Both clears. The old `&&` ran the user clear only when the schema clear
	// had already failed, so a restored base kept every user the archive did
	// not name. A schema clear that fails leaves the users where they were.
	if (!ClearDatabase())
		return false;
	if (!ClearTableUser())
		CreateTableUser();

	// LoadConfigFromBuffer already RUNs the loaded tree (the
	// ibMetaDataConfiguration override). No separate RunDatabase here,
	// that would be a double-run (asserts !m_configOpened).
	if (!activeMetaData->LoadConfigFromBuffer(config))
		return false;

	// The configuration is applied before the rows: it creates the tables,
	// with the column ids, that `data` is about to be written into. A failed
	// apply stops the load.
	if (!activeMetaData->SaveDatabase(saveConfigFlag)) {
		ibJournalError(ibT("appdata"),_("The configuration from the file could not be applied - the data was not loaded"));
		return false;
	}

	if (!LoadUserInfoFromBuffer(user))
		return false;

	if (!activeMetaData->RestoreDataFromBuffer(data))
		return false;

	return true;
}

bool ibApplicationInstance::SaveDatabase(const ibString& strFullPath)
{
	// 1. Create the physical file output stream
	wxFFileOutputStream out(strFullPath);
	if (!out.IsOk())
	{
		ibJournalError(ibT("appdata"),"Cannot create output file %s", strFullPath);
		return false;
	}

	return SaveDatabase(out);
}

bool ibApplicationInstance::SaveDatabase(wxOutputStream& out)
{
	// 2. Wrap it in a wxZipOutputStream for compression
	wxZipOutputStream zip(out);

	// PutNextEntry sets up the new entry in the archive
	if (zip.PutNextEntry(wxT("config"))) {
		//save configuration — a backup without it restores nothing, so it is not written
		ibMemoryBuffer bufferConfig;
		if (!activeMetaData->SaveConfigToBuffer(bufferConfig))
			return false;
		// Wrap the content in an input stream to write it easily
		wxMemoryInputStream contentStream(bufferConfig.GetData(), bufferConfig.GetDataLen());
		zip.Write(contentStream); // Write the data from the content stream
	}

	if (zip.PutNextEntry(wxT("user"))) {
		//save users 
		ibMemoryBuffer bufferUser;
		if (!SaveUserInfoToBuffer(bufferUser))
			return false;
		// Wrap the content in an input stream to write it easily
		wxMemoryInputStream contentStream(bufferUser.GetData(), bufferUser.GetDataLen());
		zip.Write(contentStream); // Write the data from the content stream
	}

	if (zip.PutNextEntry(wxT("data"))) {
		//save data
		ibMemoryBuffer bufferData;
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
	if (activeMetaData == nullptr)
		return false;

	if (!activeMetaData->ReCreateDatabase())
		return false;

	return true;
}

ibString ibApplicationInstance::GetDatabaseDescription() const
{
	// Where the base lives: a PostgreSQL server's database, or a Firebird folder (none — the launcher's, no base).
	if (!m_strServer.IsEmpty())
		return m_strServer + ibT(":") + m_strPort + ibT("/") + m_strDatabase;
	return m_strFile;
}

///////////////////////////////////////////////////////////////////////////////

#include "core/utils/md5.hpp"

ibString ibApplicationInstance::ComputeMd5(const ibString& userPassword) const
{
	if (userPassword.Length() > 0)
		return ibMD5::ComputeMd5(userPassword);

	return ibEmptyString;
}

///////////////////////////////////////////////////////////////////////////////


