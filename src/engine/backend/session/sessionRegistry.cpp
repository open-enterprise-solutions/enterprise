#include "sessionRegistry.h"
#include "sessionPolicy.h"
#include "designerExclusivePolicy.h"

#include "backend/appData.h"
#include "sessionSnapshot.h"
#include "backend/backend_exception.h"
#include "backend/guid.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — q(&m_writeHolder) resolves to the holder's bound conn (write helpers); snapshot reads still raw
#include "workerPool.h"
#include "workerPoolHeadless.h"
#include "backend/lock/lockManager.h"
#include "backend/utils/debugTrace.h"   // ibTraceToFile — Die's reason must survive a GUI build

#include <chrono>
#include <iostream>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <process.h>
#else
#	include <unistd.h>
#endif

#include <wx/log.h>
#include <wx/datetime.h>

namespace {

int CurrentPid()
{
#if defined(_WIN32)
	return static_cast<int>(GetCurrentProcessId());
#else
	return static_cast<int>(::getpid());
#endif
}

// Diagnostic sink for the [session …] lines. Two destinations, both free and
// neither of them a file: stderr for a console host, and wxLogDebug for a GUI one
// (whose stderr is closed — that is what the removed hand-rolled log file was
// working around, at the price of an fopen/fclose per line on the registry's own
// INSERT / sweep / signal paths, and a path baked into the binary).
//
// Deliberately NOT the platform journal: these fire on every sweep tick, and a
// journal row per tick would bury what an administrator actually reads.
void LogSession(const std::string& msg)
{
	std::ostringstream line;
	line << "[pid=" << CurrentPid() << "] " << msg;
	const std::string tagged = line.str();
	std::cerr << tagged << std::endl;
	wxLogDebug(wxT("%s"), wxString::FromUTF8(tagged.c_str()));
}

} // namespace

#define SESSION_LOG(expr) do { std::ostringstream _o; _o << expr; LogSession(_o.str()); } while(0)

// No static `Instance()` definition here — accessor moved to
// `ibApplicationData::GetSessionRegistry()` (declared inline in appData.h).
// Single coordinator pattern: subsystems do not own their own global
// state, they exist for the duration of appData.

ibSessionRegistry::ibSessionRegistry(ib::AppDataCtorToken, std::size_t maxWorkers)
{
	// Allocate the worker pool here so registry owns the whole
	// session-management subsystem end-to-end: pool stops before
	// sessions tear down inside our Stop(). maxWorkers == 0 means
	// "no pool" — desktop GUI modes that run a single session on the
	// wx main thread don't need a thread pool at all.
	if (maxWorkers > 0)
		m_workerPool = std::make_unique<ibWorkerPoolHeadless>(maxWorkers);
}

void ibSessionRegistry::SetWorkerPool(std::unique_ptr<ibWorkerPool> pool)
{
	// Drain the outgoing pool before it dies: Stop() lets pending tasks
	// run to completion against sessions that are still alive, which a
	// plain reset would not (the unique_ptr would destroy the pool with
	// queued work still in it).
	if (m_workerPool != nullptr)
		m_workerPool->Stop();

	m_workerPool = std::move(pool);
}

void ibSessionRegistry::CloseAll(bool force)
{
	// Snapshot under shared lock — Close() submits Remove which acquires
	// m_ownMutex unique-locked from the registry thread, so we can't
	// hold the snapshot lock while calling Close. shared_ptr keeps
	// each session alive across the iteration even if the registry
	// drops its strong-ref via ProcessRemove mid-loop.
	std::vector<std::shared_ptr<ibSession>> snapshot;
	{
		std::shared_lock<std::shared_mutex> lk(m_ownMutex);
		snapshot.reserve(m_own.size());
		for (auto& kv : m_own)
			if (auto s = kv.second.Share()) snapshot.push_back(std::move(s));
	}
	// force closes each window without asking (and breaks any in-flight
	// script out of its loop); without it every session gets the normal
	// close path and may refuse.
	for (auto& s : snapshot)
		s->Close(force);
}

void ibSessionRegistry::InstallUser(ibSession* s,
                                     const ibUserInfo& info,
                                     const wxString& rawPassword)
{
	if (s == nullptr) return;
	s->SetUserInfo(info);
	s->SetSessionRawPassword(rawPassword);
}

void ibSessionRegistry::EnableDebugForSession(ibSession* s)
{
	if (s == nullptr) return;
	s->EnableDebug();
}

ibSessionRegistry::~ibSessionRegistry()
{
	// Best-effort — in normal shutdown Stop() should have been called via
	// ibApplicationData::Disconnect. Reaching the dtor with m_thread still
	// joinable means something forgot to stop; join here so the process
	// doesn't terminate with a running thread (which std::thread's dtor
	// would turn into std::terminate anyway).
	Stop();
}

ibSessionWatch ibSessionRegistry::Find(const wxString& id)
{
	// Lookup by session id in m_own — the live map keyed by GetId(),
	// populated through the normal Connect / worker-pool flow. The
	// designer echoes GetId() back as the debug sid, so this resolves
	// Continue / Step / Pause targets and the web "session paused?" query
	// (wfrontendSessionPaused).
	std::shared_lock<std::shared_mutex> lock(m_ownMutex);
	auto it = m_own.find(id);
	if (it == m_own.end())
		return ibSessionWatch();
	// The stored watch IS the answer — expired means "the owner is gone
	// and the sweep has not caught up", which reads the same as "no such
	// session" to every caller.
	return it->second;
}

ibSessionWatch ibSessionRegistry::FindSessionByRoot(ibValueModuleManagerRuntimeConfiguration* mm) const
{
	if (mm == nullptr) return ibSessionWatch();
	std::shared_lock<std::shared_mutex> lock(m_ownMutex);
	for (const auto& kv : m_own) {
		auto s = kv.second.Share();
		if (s && s->GetManagerModule() == mm)
			return kv.second;
	}
	return ibSessionWatch();
}

ibSessionWatch ibSessionRegistry::FindSessionByFrame(ibBackendDocFrame* frame) const
{
	if (frame == nullptr) return ibSessionWatch();
	std::shared_lock<std::shared_mutex> lock(m_ownMutex);
	for (const auto& kv : m_own) {
		auto s = kv.second.Share();
		if (s && s->GetFrame() == frame)
			return kv.second;
	}
	return ibSessionWatch();
}

bool ibSessionRegistry::HasClients() const
{
	std::shared_ptr<ibSession> server;
	{
		std::shared_lock<std::shared_mutex> lk(m_serverMutex);
		server = m_currentServer.lock();
	}
	if (!server) return false;
	std::shared_lock<std::shared_mutex> lock(m_ownMutex);
	for (const auto& kv : m_own) {
		auto s = kv.second.Share();
		if (!s || s == server) continue;
		auto srv = s->Server();
		if (srv && srv.get() == server.get())
			return true;
	}
	return false;
}

// --- Session factory facade ----------------------------------------------

void ibSessionRegistry::EnsureStartedForCreateSession(ibRunMode runMode)
{
	if (m_threadAlive.load(std::memory_order_acquire)) return;

	// Own sys_session row I/O: registry handles INSERT / UPDATE / DELETE
	// via its write connection and holds pessimistic row locks that peers
	// use to detect liveness. Replaces the pre-2026-04-20 1 Hz heartbeat
	// UPDATE path. EnableSysSessionOwnership is idempotent; only effective
	// before the first Start().
	EnableSysSessionOwnership(true);

	// Designer-exclusive policy — only one designer process per IB at a
	// time. AddPolicy must happen BEFORE Start so the chain is immutable
	// once the consumer thread is running.
	if (runMode == eDESIGNER_MODE)
		AddPolicy(std::make_unique<ibDesignerExclusivePolicy>(this));

	Start();
}

ibSessionHolder ibSessionRegistry::CreateSessionWithFactory(ibRunMode runMode,
                                                            const wxString& computer,
                                                            ibConnectRequest::SessionFactory factory)
{
	EnsureStartedForCreateSession(runMode);

	// Anonymous-phase Connect — registry INSERTs a row with userName=''
	// immediately so peers (Active Users UI, designer-exclusive policy)
	// see "someone is logging in". Session kind is explicit: wes's own
	// technical session is WebServer; desktop modes default via runMode.
	ibConnectRequest req;
	req.m_computer       = computer;
	req.m_appMode        = runMode;
	req.m_kind           = (runMode == eWEB_RUNTIME_MODE)
	                         ? ibSessionKind::WebServer
	                         : SessionKindFromRunMode(runMode);
	req.m_sessionFactory = std::move(factory);

	auto result = Connect(req);
	if (result.m_code != ibConnectResult::Ok) {
		// Surface the actual rejection reason (e.g. designer-exclusive
		// policy veto, exclusive-mode block, registry-down) so the caller's
		// catch shows something better than the generic "Failed to create
		// session" wrapper in FinishCreateSession.
		if (!result.m_reason.IsEmpty())
			ibBackendCoreException::Error(result.m_reason);
		return ibSessionHolder();
	}
	// Hand the thread of life to the caller. Until it is moved into an
	// owner, it lives in the caller's stack frame — so an early return
	// anywhere upstream closes the session instead of leaking it.
	return std::move(result.m_holder);
}

ibSessionHolder ibSessionRegistry::CreateSessionOfKind(ibRunMode runMode,
                                                       const wxString& computer,
                                                       ibSessionKind kind,
                                                       ibConnectRequest::SessionFactory factory)
{
	EnsureStartedForCreateSession(runMode);

	// Same anonymous-phase Connect as the facade above; only the kind is the
	// caller's rather than derived from runMode. The row appears immediately with
	// an empty user name, so a job is visible from the moment it exists — before
	// it has an identity, and whether or not it ever gets one.
	ibConnectRequest req;
	req.m_computer       = computer;
	req.m_appMode        = runMode;
	req.m_kind           = kind;
	req.m_sessionFactory = std::move(factory);

	auto result = Connect(req);
	if (result.m_code != ibConnectResult::Ok) {
		if (!result.m_reason.IsEmpty())
			ibBackendCoreException::Error(result.m_reason);
		return ibSessionHolder();
	}
	return std::move(result.m_holder);
}

// No Connect, no queue, no row — the session simply exists and is owned. Mirrors what
// ibJobManager does inline for a rented read; lifted here so the one place that may call
// SetUnlisted is the registry that owns the listing rule. See the header for who uses it.
ibSessionHolder ibSessionRegistry::MintUnlisted(std::shared_ptr<ibSession> session)
{
	if (!session)
		return ibSessionHolder();
	session->SetUnlisted();
	return ibSessionHolder(std::move(session));
}

ibSessionHolder ibSessionRegistry::CreateSessionWithFactory(ibRunMode runMode,
                                                            const wxString& computer,
                                                            const wxString& presetGuid,
                                                            const wxString& address,
                                                            ibConnectRequest::SessionFactory factory)
{
	// Per-tab variant — registry is normally already running by the time
	// per-tab logins arrive (wes process bring-up created its WebServer
	// system session at startup). EnsureStartedForCreateSession is
	// idempotent so calling it here is safe in either order.
	EnsureStartedForCreateSession(runMode);

	ibConnectRequest req;
	req.m_computer       = computer;
	req.m_appMode        = runMode;
	req.m_kind           = ibSessionKind::WebClient;
	req.m_address        = address;
	req.m_presetGuid     = presetGuid;
	req.m_sessionFactory = std::move(factory);

	auto result = Connect(req);
	if (result.m_code != ibConnectResult::Ok) {
		if (!result.m_reason.IsEmpty())
			ibBackendCoreException::Error(result.m_reason);
		return ibSessionHolder();
	}
	return std::move(result.m_holder);
}

// --- Thread lifecycle ----------------------------------------------------

void ibSessionRegistry::Start()
{
	if (m_threadAlive.load(std::memory_order_acquire)) return;
	if (m_fatal.load(std::memory_order_acquire))       return;

	// When we own sys_session, grab ONE dedicated pool connection for
	// INSERT/UPDATE/DELETE + JobRefreshSnapshot's SELECT. (The NOWAIT-probe
	// conn went with the row-lock scheme.) nullptr-tolerant downstream — if the
	// pool is not yet initialised (early startup / test harness) the DB
	// ops no-op gracefully.
	//
	// The historical third connection (m_lockConn for HoldRowLocks +
	// pessimistic-lock liveness) was removed when liveness moved to the
	// heartbeat-on-lastActive model — see "HoldRowLocks self-deadlock"
	// memory note. Frees a pool slot for productive use.
	if (m_ownsSysSession) {
		// EnsureConnection (not AcquireFreeConnection) BINDS the conn to its holder — so a query door
		// opened with q(&m_writeHolder) resolves to exactly this conn via GetScopeConn. FB self-heal
		// still acts on the same object after a leader handoff.
		m_writeConn = m_writeHolder.EnsureConnection();
	}

	m_stop.store(false, std::memory_order_release);
	m_thread = std::thread([this]{ ThreadBody(); });

	// Wait until ThreadBody has entered its loop and flipped
	// m_threadAlive to true. Without this, a Connect / Submit issued
	// immediately after Start races the thread's start-up: the check at
	// the top of Connect sees m_threadAlive == false and bails with
	// RegistryDown. Short bounded wait — if the OS can't start a thread
	// in 2s something is seriously broken, fall through and let Connect
	// report RegistryDown for real.
	using namespace std::chrono;
	const auto deadline = steady_clock::now() + seconds(2);
	while (!m_threadAlive.load(std::memory_order_acquire) &&
	       !m_fatal.load(std::memory_order_acquire) &&
	       steady_clock::now() < deadline) {
		std::this_thread::sleep_for(milliseconds(1));
	}
}

void ibSessionRegistry::Stop()
{
	if (!m_thread.joinable()) {
		// Even with no registry thread running, the pool may be live —
		// the appData ctor allocates it before the registry starts. Stop
		// anyway so workers join before the host returns from Stop.
		if (m_workerPool) {
			m_workerPool->Stop();
			m_workerPool.reset();
		}
		return;
	}

	// Drain worker pool BEFORE killing sessions: tasks in flight reference
	// session pointers; if we tear sessions down first, the pool's worker
	// threads are left calling into freed memory. After Stop, the pool's
	// worker threads have joined and no new task can be submitted (Submit
	// rejects with set_exception on a stopped pool).
	if (m_workerPool) {
		m_workerPool->Stop();
		m_workerPool.reset();
	}

	// Quiesce the registry thread FIRST without giving it any new work.
	// Earlier shape ran Removes through Submit + ThreadBody so they
	// happened on the registry thread; that turned shutdown into a
	// cross-thread mutex chase between main (in unwind / dtor) and
	// ThreadBody (in ProcessRemove → session.m_mtx / NotifyDisconnect
	// listener → wxApp / DeleteSessionRow → fbclient lock), which
	// deadlocked once a listener or session destructor on main happened
	// to share a lock with one of those paths. Inline drain after the
	// join keeps every shutdown side-effect on a single thread, so a
	// hang here surfaces directly in main's stack instead of as an
	// invisible deadlock waiting on `m_thread.join()`.
	{
		std::lock_guard<std::mutex> lk(m_submitMtx);
		m_stop.store(true, std::memory_order_release);
	}
	m_submitCv.notify_all();

	// Timed join with detach backstop. ThreadBody may be mid-ProcessRemove
	// when m_stop fires; if its NotifyDisconnect → DetachRuntime path
	// blocks on a mutex that main holds further up the stack (classic
	// case: main crashed inside BeforeStart's lock_guard<m_runtimeMutex>,
	// SEH dispatch unrolled to OnFatalException → ~ibApplicationData →
	// Stop without releasing C++ frames — the lock is still held), a
	// blind join() hangs forever. m_threadAlive is set by ThreadBody
	// at entry and cleared on exit (incl. exception path via the noexcept
	// guarantee + the explicit clear at function tail / Die()). Poll it
	// with a deadline; on timeout, detach and let the OS reap the thread
	// on process exit.
	using clock = std::chrono::steady_clock;
	const auto deadline = clock::now() + std::chrono::seconds(2);
	while (m_threadAlive.load(std::memory_order_acquire)
		&& clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	const bool detached = m_threadAlive.load(std::memory_order_acquire);
	if (detached) {
		wxLogWarning(wxT("registry: m_thread join timed out - detaching ")
			wxT("(deadlock against an in-flight ProcessRemove -> DetachRuntime / ")
			wxT("listener callback chain). Process will exit anyway."));
		m_thread.detach();
	}
	else {
		m_thread.join();
	}

	// Inline Remove drain — DELETE sys_session row + fire OnDisconnect
	// listeners for every still-owned session. Per-Remove try/catch keeps
	// one bad row from poisoning the rest (e.g. fbclient already
	// disconnected → DeleteSessionRow throws ibBackendException).
	//
	// Skipped on the detached path: ProcessRemove → NotifyDisconnect →
	// DetachRuntime would acquire m_runtimeMutex, the same lock the
	// in-flight ThreadBody is stuck on (and which main itself may still
	// hold via BeforeStart's lock_guard if we're unwinding from a crash).
	// The detached ThreadBody picks up the queued Removes if it ever
	// resumes; otherwise the rows are reaped on the next process's
	// startup sweep.
	if (!detached) {
		std::vector<std::shared_ptr<ibSession>> owned;
		{
			std::shared_lock<std::shared_mutex> lk(m_ownMutex);
			owned.reserve(m_own.size());
			for (auto& kv : m_own) {
				if (auto s = kv.second.Share()) owned.push_back(std::move(s));
			}
		}
		for (auto& s : owned) {
			ibRegistryRequest req;
			req.kind    = ibRegistryRequestKind::Remove;
			req.session = s;
			try { ProcessRemove(req); } catch (...) { /* swallowed: registry-Stop fan-out, keep going so every owned row gets a Remove attempt even if one fails */ }
		}
	}

	// Drop the pool checkout. shared_ptr custom deleter on pool connections
	// reparks it on the pool's idle list — no explicit pool-side Return needed.
	m_writeConn.reset();
}

// --- Submit / Drain ------------------------------------------------------

void ibSessionRegistry::Submit(ibRegistryRequest req, ibPriority priority)
{
	// Dropped silently when the registry is stopped or fatal. Producers
	// detect the failure through the session's state machine (WaitState
	// times out, state remains Created).
	if (m_fatal.load(std::memory_order_acquire)) return;

	const std::size_t bin = static_cast<std::size_t>(priority);
	if (bin >= kPriorityBinCount) return;   // defensive — unreachable

	{
		std::lock_guard<std::mutex> lk(m_submitMtx);
		if (m_stop.load(std::memory_order_acquire)) return;
		m_bins[bin].push_back(std::move(req));
	}
	m_submitCv.notify_one();
}

// --- Connect -------------------------------------------------------------

ibConnectResult ibSessionRegistry::Connect(const ibConnectRequest& req,
                                           std::chrono::milliseconds timeout)
{
	ibConnectResult r;

	if (m_fatal.load(std::memory_order_acquire) || !m_threadAlive.load(std::memory_order_acquire)) {
		r.m_code   = ibConnectResult::RegistryDown;
		r.m_reason = m_fatalReason.IsEmpty() ? _("registry not running") : m_fatalReason;
		return r;
	}

	// Identity from req. Caller-supplied preset guid wins when valid
	// (web tab passes its sessionStorage tabSid so the id is shared
	// between browser header, cookie, SessionManager key, ibSession,
	// and sys_session.session PK — single identifier end-to-end).
	// Fresh mint only when the caller didn't set one.
	ibSessionIdentity identity;
	if (!req.m_presetGuid.IsEmpty()) {
		ibGuid candidate(req.m_presetGuid);
		identity.m_guid = candidate.isValid() ? candidate : wxNewUniqueGuid;
	}
	else {
		identity.m_guid = wxNewUniqueGuid;
	}
	identity.m_userName          = req.m_userName;
	identity.m_userGuid.clear();  // filled by Attach
	identity.m_computer          = req.m_computer;
	identity.m_address           = req.m_address;
	identity.m_appMode           = req.m_appMode;
	identity.m_started           = wxDateTime::Now();
	identity.m_pid               = CurrentPid();
	identity.m_expectsAnonPhase  = req.m_userName.IsEmpty();

	const wxString idStr = identity.m_guid;

	// Factory path — typed CreateSession<T> on appData hands us a
	// callback that builds a derived session (ibGUISession on desktop,
	// ibWebClientSession per web tab). Default path keeps the base class.
	auto session = req.m_sessionFactory
		? req.m_sessionFactory(idStr, req.m_kind)
		: std::make_shared<ibSession>(idStr, req.m_kind);
	session->SetIdentity(identity);

	// --- Submit Add + wait for Created → Added / Rejected ---
	{
		ibRegistryRequest add;
		add.kind    = ibRegistryRequestKind::Add;
		add.session = session;
		Submit(std::move(add), ibPriority::Normal);
	}
	ibSessionState st = session->WaitForState(ibSessionState::Created, timeout);
	if (st == ibSessionState::Created) {
		r.m_code   = ibConnectResult::Timeout;
		r.m_reason = _("registry did not answer to Add");
		return r;
	}
	if (st == ibSessionState::Rejected) {
		r.m_code   = ibConnectResult::RejectedPolicy;
		r.m_reason = session->Reason();
		return r;
	}
	if (st != ibSessionState::Added) {
		r.m_code   = ibConnectResult::RegistryDown;
		r.m_reason = _("unexpected session state after Add");
		return r;
	}

	// --- Optional Attach for creds-supplied flow ---
	if (!req.m_userName.IsEmpty()) {
		{
			ibRegistryRequest att;
			att.kind     = ibRegistryRequestKind::Attach;
			att.session  = session;
			att.user     = req.m_userName;
			att.password = req.m_password;
			Submit(std::move(att), ibPriority::Normal);
		}
		ibAuthState auth = session->WaitForAuth(ibAuthState::Anonymous, timeout);
		if (auth == ibAuthState::Anonymous) {
			// Producer gave up; tear down the session so we don't leak an
			// orphan — Remove runs before we return.
			ibRegistryRequest rm;
			rm.kind    = ibRegistryRequestKind::Remove;
			rm.session = session;
			Submit(std::move(rm), ibPriority::Urgent);
			r.m_code   = ibConnectResult::Timeout;
			r.m_reason = _("registry did not answer to Attach");
			return r;
		}
		if (auth != ibAuthState::Authenticated) {
			// Auth failed — drop the session (anonymous row no longer useful).
			ibRegistryRequest rm;
			rm.kind    = ibRegistryRequestKind::Remove;
			rm.session = session;
			Submit(std::move(rm), ibPriority::Urgent);
			r.m_code   = ibConnectResult::RejectedAuth;
			r.m_reason = session->Reason();
			return r;
		}
	}

	// Success — hand over ownership, and it really is ownership: the
	// registry's m_own is a weak index, so this holder is the only thing
	// keeping the session alive. Whatever the caller moves it into
	// decides when the session ends.
	r.m_code   = ibConnectResult::Ok;
	r.m_holder = ibSessionHolder(std::move(session));
	return r;
}

std::vector<ibRegistryRequest> ibSessionRegistry::DrainAll()
{
	std::vector<ibRegistryRequest> out;
	// Snapshot all bins top-down under one lock. Strict descending:
	// Urgent → Normal → Low → Background. FIFO preserved within each bin.
	std::lock_guard<std::mutex> lk(m_submitMtx);
	std::size_t total = 0;
	for (auto& bin : m_bins) total += bin.size();
	out.reserve(total);
	for (auto& bin : m_bins) {
		while (!bin.empty()) {
			out.push_back(std::move(bin.front()));
			bin.pop_front();
		}
	}
	return out;
}

// --- Policy chain --------------------------------------------------------

void ibSessionRegistry::AddPolicy(std::unique_ptr<ibSessionPolicy> policy)
{
	if (!policy) return;
	// Only safe before Start(); the thread reads m_policies without a
	// lock. Callers wire policies during app bootstrap.
	m_policies.push_back(std::move(policy));
}

// --- Lifecycle events ---------------------------------------------------

void ibSessionRegistry::OnConnectCreate(SessionCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listConnectCreate.push_back(std::move(cb));
}

void ibSessionRegistry::OnAuthenticated(SessionCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listAuthenticated.push_back(std::move(cb));
}

void ibSessionRegistry::OnFirstConnect(SessionCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listFirstConnect.push_back(std::move(cb));
}

void ibSessionRegistry::OnDisconnect(SessionCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listDisconnect.push_back(std::move(cb));
}

void ibSessionRegistry::OnLastDisconnect(VoidCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listLastDisconnect.push_back(std::move(cb));
}

void ibSessionRegistry::OnAfterCompile(SessionCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listAfterCompile.push_back(std::move(cb));
}

void ibSessionRegistry::OnReload(SessionCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listReload.push_back(std::move(cb));
}

void ibSessionRegistry::NotifyReload(ibSession* s)
{
	if (s == nullptr) return;
	std::vector<SessionCallback> listeners;
	{
		std::lock_guard<std::mutex> lk(m_eventMutex);
		listeners = m_listReload;
	}
	for (const auto& cb : listeners)
		if (cb) cb(s);
}

void ibSessionRegistry::OnShouldKeepAlive(KeepAliveHook h)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listKeepAlive.push_back(std::move(h));
}

void ibSessionRegistry::OnForceExit(SessionCallback cb)
{
	std::lock_guard<std::mutex> lk(m_eventMutex);
	m_listForceExit.push_back(std::move(cb));
}

void ibSessionRegistry::NotifyForceExit(ibSession* s)
{
	std::vector<SessionCallback> hooks;
	{
		std::lock_guard<std::mutex> lk(m_eventMutex);
		hooks = m_listForceExit;
	}
	for (const auto& cb : hooks)
		if (cb) cb(s);
}

bool ibSessionRegistry::ShouldKeepAlive() const
{
	std::vector<KeepAliveHook> hooks;
	{
		std::lock_guard<std::mutex> lk(m_eventMutex);
		hooks = m_listKeepAlive;
	}
	for (auto& h : hooks) {
		if (h && h()) return true;
	}
	return false;
}

void ibSessionRegistry::NotifyAfterCompile(ibSession* s)
{
	if (s == nullptr) return;
	std::vector<SessionCallback> listeners;
	{
		std::lock_guard<std::mutex> lk(m_eventMutex);
		listeners = m_listAfterCompile;
	}
	for (const auto& cb : listeners)
		if (cb) cb(s);
}

void ibSessionRegistry::NotifyConnectCreate(ibSession* s)
{
	if (s == nullptr) return;
	std::vector<SessionCallback> snapshot;
	{
		std::lock_guard<std::mutex> lk(m_eventMutex);
		snapshot = m_listConnectCreate;
	}
	for (const auto& cb : snapshot)
		if (cb) cb(s);
}

void ibSessionRegistry::NotifyAuthenticated(ibSession* s)
{
	if (s == nullptr) return;
	// Pin session as Current() on the calling thread BEFORE listeners
	// fire, so RunDatabase / CompileRoot / etc. can resolve through
	// ibSession::Current() inside listener bodies without each listener
	// having to bind manually.
	ibSession::BindSessionToThread(s, std::this_thread::get_id());

	std::vector<SessionCallback> auths;
	std::vector<SessionCallback> firsts;
	bool fireFirst = false;
	{
		std::lock_guard<std::mutex> lk(m_eventMutex);
		++m_authenticatedCount;
		if (!m_firstConnectFired) {
			m_firstConnectFired = true;
			fireFirst = true;
			firsts = m_listFirstConnect;
		}
		auths = m_listAuthenticated;
	}
	if (fireFirst) {
		for (const auto& cb : firsts)
			if (cb) cb(s);
	}
	// Between phases — OnFirstConnect's metadataCreate may have just set
	// activeMetaData; OnAuthenticated's listeners (RunDatabase ->
	// OnBeforeRunMetaObject) need session->mm to exist. Session creates
	// its root here so ownership stays in ibSession (see EnsureRoot).
	s->EnsureRoot();
	for (const auto& cb : auths)
		if (cb) cb(s);
}

void ibSessionRegistry::NotifyDisconnect(ibSession* s)
{
	if (s == nullptr) return;
	// Only authenticated sessions move the counter — sessions removed
	// pre-auth (Rejected / cancelled Add) don't pair with NotifyAuthenticated.
	const bool wasAuth = (s->Auth() == ibAuthState::Authenticated);
	std::vector<SessionCallback> disconnects;
	std::vector<VoidCallback>    lasts;
	bool fireLast = false;
	{
		std::lock_guard<std::mutex> lk(m_eventMutex);
		disconnects = m_listDisconnect;
		if (wasAuth && m_authenticatedCount > 0) {
			--m_authenticatedCount;
			if (m_authenticatedCount == 0) {
				m_firstConnectFired = false;
				fireLast = true;
				lasts = m_listLastDisconnect;
			}
		}
	}
	for (const auto& cb : disconnects)
		if (cb) cb(s);
	if (fireLast) {
		for (const auto& cb : lasts)
			if (cb) cb();
	}
}

// --- Access mode + fallback ---------------------------------------------

void ibSessionRegistry::SetAccessMode(ibSession::AccessMode mode)
{
	std::unique_lock<std::shared_mutex> lk(m_accessMutex);
	m_accessMode = mode;
}

ibSession::AccessMode ibSessionRegistry::GetAccessMode() const
{
	std::shared_lock<std::shared_mutex> lk(m_accessMutex);
	return m_accessMode;
}

void ibSessionRegistry::SetFallback(ibSession* s)
{
	std::unique_lock<std::shared_mutex> lk(m_accessMutex);
	m_fallback = s ? s->weak_from_this() : std::weak_ptr<ibSession>{};
}

void ibSessionRegistry::ClearFallback()
{
	std::unique_lock<std::shared_mutex> lk(m_accessMutex);
	m_fallback.reset();
}

ibSession* ibSessionRegistry::GetFallback() const
{
	std::shared_lock<std::shared_mutex> lk(m_accessMutex);
	return m_fallback.lock().get();
}

// --- debug thread → parked session redirection --------------------------

void ibSessionRegistry::RegisterDebugThread(std::thread::id tid)
{
	std::unique_lock<std::shared_mutex> lk(m_debugMtx);
	m_debugThreads.insert(tid);
}

void ibSessionRegistry::UnregisterDebugThread(std::thread::id tid)
{
	std::unique_lock<std::shared_mutex> lk(m_debugMtx);
	m_debugThreads.erase(tid);
}

bool ibSessionRegistry::IsDebugThread(std::thread::id tid) const
{
	std::shared_lock<std::shared_mutex> lk(m_debugMtx);
	return m_debugThreads.find(tid) != m_debugThreads.end();
}

void ibSessionRegistry::EnterDebugLoop(ibSession* s)
{
	if (s == nullptr) return;
	std::unique_lock<std::shared_mutex> lk(m_debugMtx);
	// Idempotent — drop any expired or duplicate entry pointing at s
	// before pushing. Re-entrant breakpoint shouldn't happen but we
	// stay correct if it does.
	for (auto it = m_debugQueue.begin(); it != m_debugQueue.end();) {
		auto cur = it->lock();
		if (!cur || cur.get() == s) it = m_debugQueue.erase(it);
		else                        ++it;
	}
	m_debugQueue.push_back(s->weak_from_this());
}

void ibSessionRegistry::LeaveDebugLoop(ibSession* s)
{
	if (s == nullptr) return;
	std::unique_lock<std::shared_mutex> lk(m_debugMtx);
	for (auto it = m_debugQueue.begin(); it != m_debugQueue.end();) {
		auto cur = it->lock();
		if (!cur || cur.get() == s) it = m_debugQueue.erase(it);
		else                        ++it;
	}
}

ibSessionWatch ibSessionRegistry::GetActiveDebugTarget() const
{
	std::shared_lock<std::shared_mutex> lk(m_debugMtx);
	for (const auto& w : m_debugQueue) {
		if (auto sp = w.lock()) return ibSessionWatch(std::move(sp));
	}
	return ibSessionWatch();
}

// --- Handlers ------------------------------------------------------------

static bool InsertSessionRow(ibDatabaseConnectionHolder& holder, const ibSession& s, const wxString& userName)
{
	const ibSessionIdentity& id = s.Identity();

	// Primary INSERT — core 6 columns present in every sys_session
	// since the original schema. Additional columns (pid / address /
	// currentActivity), added 2026-04-20, are filled by a separate
	// UPDATE below. Splitting these avoids a single INSERT failure
	// when the DB was created before the migration ran (the 9-column
	// INSERT would throw on unknown column and leave the session
	// unregistered, which breaks Active Users listings across the
	// cluster).
	ibDatabaseQueryBuilder q(&holder);   // resolves to the holder's bound conn (= m_writeConn) via GetScopeConn
	try {
		q.Execute(ibInsert(session_table, {
			{ wxT("session"),     ibConst(ibValue(s.GetId())) },
			{ wxT("userName"),    ibConst(ibValue(userName)) },
			{ wxT("application"), ibConst(ibValue(int(id.m_appMode))) },
			{ wxT("started"),     ibConst(ibValue(id.m_started)) },
			{ wxT("lastActive"),  ibConst(ibValue(id.m_started)) },
			{ wxT("computer"),    ibConst(ibValue(id.m_computer)) },
		}));
		SESSION_LOG("[session INSERT] ok guid=" << s.GetId()
		          << " user='" << (const char*)userName.ToUTF8().data() << "'"
		          << " mode=" << int(id.m_appMode));
	}
	catch (const ibBackendException& err) {
		SESSION_LOG("[session INSERT] FAILED: "
		          << (const char*)err.GetErrorDescription().ToUTF8().data());
		return false;
	}
	catch (...) {
		SESSION_LOG("[session INSERT] FAILED: unknown exception");
		return false;
	}

	// Best-effort population of extension columns — silently skipped
	// when the schema pre-dates them. MigrateTableSession tries to
	// add the columns on startup; this is the follow-up writer.
	try {
		q.Execute(ibUpdate(session_table, {
			{ wxT("pid"),     ibConst(ibValue(id.m_pid)) },
			{ wxT("address"), ibConst(ibValue(id.m_address)) },
			{ wxT("kind"),    ibConst(ibValue(int(s.GetKind()))) },
			// …AND `exclusive` = 0, EXPLICITLY. A session that never takes monopoly mode used to leave
			// this column NULL for its whole life, while one that took it and let it go left 0 — two
			// spellings of "not exclusive" in one table. Nothing reads them apart TODAY (the driver
			// reports NULL as 0), but a fact with two spellings is answered by whoever asks NEXT, and
			// the next asker may be SQL, where `exclusive <> 1` and `IS NULL` part company. The row
			// says it from birth instead of acquiring the answer later.
			{ wxT("exclusive"), ibConst(ibValue(0)) },
		}, ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(s.GetId())))));
	} catch (...) { /* legacy schema — fine */ }

	return true;
}

static bool UpdateSessionUser(ibDatabaseConnectionHolder& holder, const wxString& guidStr, const wxString& userName, const wxString& userGuid)
{
	(void)userGuid;   // schema currently has no userGuid column — placeholder for when we add it
	try {
		ibDatabaseQueryBuilder q(&holder);
		q.Execute(ibUpdate(session_table,
			{ { wxT("userName"), ibConst(ibValue(userName)) } },
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(guidStr)))));
		return true;
	} catch (...) { return false; }
}

static bool DeleteSessionRow(ibDatabaseConnectionHolder& holder, const wxString& guidStr)
{
	try {
		ibDatabaseQueryBuilder q(&holder);
		q.Execute(ibDelete(session_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(guidStr)))));
		return true;
	} catch (...) { return false; }
}

void ibSessionRegistry::ProcessAdd(ibRegistryRequest& req)
{
	if (!req.session) return;
	ibSession& s = *req.session;

	// Server (parent) auto-tracking — kind == WebServer registers as
	// the process's server; subsequent non-server sessions get Server()
	// pinned to it so keep-alive / topology queries see the tree without
	// the caller threading the pointer through. Single-session apps
	// (desktop GUI, daemon, codeRunner) never add a WebServer-kind
	// session, so m_currentServer stays nullptr and Server() does too.
	if (s.GetKind() == ibSessionKind::WebServer) {
		std::unique_lock<std::shared_mutex> lk(m_serverMutex);
		m_currentServer = s.weak_from_this();
	} else {
		std::shared_ptr<ibSession> srv;
		{
			std::shared_lock<std::shared_mutex> lk(m_serverMutex);
			srv = m_currentServer.lock();
		}
		if (srv) s.SetServer(srv.get());
	}

	// Exclusive (monopoly) gate — if another session holds the IB in
	// exclusive mode, park the request. The session stays in Created
	// state; the producer's WaitForState in Connect blocks until either
	// the holder releases (ProcessSetExclusive(off)) or the holder's
	// Remove fires — both drain m_pendingExclusive and re-run ProcessAdd
	// on the parked requests.
	std::shared_ptr<ibSession> holder;
	{
		std::shared_lock<std::shared_mutex> lk(m_exclusiveMutex);
		holder = m_exclusiveSession.lock();
	}
	if (holder && holder.get() != &s) {
		m_pendingExclusive.push_back(std::move(req));
		return;
	}

	// ⭐ REFRESH BEFORE REFUSING. This gate used to read the CACHED snapshot while the refresh below sat
	// after it, so a row that had already gone — the previous designer run's own exclusive row, released
	// on exit — kept rejecting the next start until something else rebuilt the cache. Restarting the
	// application "fixed" it, which is the signature of a stale read rather than a busy database.
	//
	// A refusal is final here (a peer holder is not tied to our event loop, so we cannot park on it), and
	// a decision that cannot be revisited may not be taken on a cached picture. The same coalescing as
	// below keeps this cheap: a snapshot taken moments ago is not re-read.
	// ⭐ AND SWEEP THE DEAD FIRST. Refreshing only re-reads the table — it does not ask whether the rows
	// in it belong to anything still running. A process that ended without unregistering (a designer
	// closed while it held monopoly, a crash) leaves its row behind, and a refusal built on that row is
	// a refusal on behalf of nobody: the message names an EMPTY user, and the block clears itself only
	// when the periodic sweep happens to fire — which is exactly what made it come and go.
	//
	// The sweep is what decides liveness (lastActive against the stale window), so it must run BEFORE
	// the snapshot the gate reads, not on its own timer beside it. Best-effort in both directions: a
	// sweep that fails leaves the old behaviour rather than blocking a start.
	if (SnapshotOlderThan(std::chrono::milliseconds(500))) {
		try { JobSweepStale(); }      catch (...) { /* best-effort: gate below still works on live rows */ }
		try { JobRefreshSnapshot(); } catch (...) { /* best-effort: an unrefreshed snapshot still gates below */ }
	}

	// Cross-process gate — peer process holds exclusive (visible in our
	// snapshot). Reject with policy veto: peer holders aren't tied to
	// our event loop, so parking would deadlock if the peer's release
	// never reaches us (e.g. peer crashed, sweep hasn't fired yet).
	// Cluster sweep + JobRefreshSnapshot will eventually clear the row;
	// the producer's caller can retry.
	{
		std::shared_lock<std::shared_mutex> lk(m_snapshotMtx);
		if (m_snapshot) {
			const wxString ownId = s.GetId();
			const unsigned int n = m_snapshot->GetSessionCount();
			for (unsigned int i = 0; i < n; ++i) {
				if (m_snapshot->GetSession(i) == ownId) continue;
				if (m_snapshot->IsExclusive(i)) {
					wxString reason = wxString::Format(
						_("Another session holds exclusive mode (user '%s')"),
						m_snapshot->GetUserName(i));
					s.Transition(ibSessionState::Rejected, reason);
					return;
				}
			}
		}
	}

	// Refresh the cluster snapshot before consulting policies — closes the
	// race where a second designer starts up faster than the first refresh
	// tick (~3s) and the cached snapshot doesn't yet show the first
	// designer's INSERTed row, so the exclusion policy permits the second
	// instance. Refresh is on this thread (consumer), so it can't deadlock
	// with itself.
	//
	// COALESCED, because the refresh is a full SELECT of sys_session and this is the thread every
	// session queues its Add and Remove behind. A snapshot taken moments ago cannot have gone stale
	// in the meantime, so re-reading the table for a second Add in the same instant buys nothing —
	// and when sessions are created in quick succession it is the difference between a registry
	// that keeps up and one the whole application waits on.
	//
	// The race this guards stays closed: it is about a second application STARTING UP, which is
	// seconds of work away, not milliseconds. Deliberately says nothing about WHO is being added —
	// the registry has no business knowing which of its sessions belong to a schedule.
	if (SnapshotOlderThan(std::chrono::milliseconds(500))) {
		try { JobRefreshSnapshot(); } catch (...) { /* swallowed: best-effort pre-policy refresh, stale snapshot is acceptable here */ }
	}

	// Policy veto chain — first reject wins.
	for (auto& p : m_policies) {
		wxString reason;
		if (!p->CanAdd(s, reason)) {
			s.Transition(ibSessionState::Rejected, reason);
			return;
		}
	}

	// Index us before touching the DB — ProcessRemove finds us in m_own
	// regardless of whether the INSERT below succeeded. This is a weak
	// entry: the request's own shared_ptr keeps the session alive for the
	// duration of this handler, and after that only the caller's holder
	// does.
	{
		std::unique_lock<std::shared_mutex> lock(m_ownMutex);
		m_own[s.GetId()] = ibSessionWatch(req.session);
	}

	// DB ownership gate. When `m_ownsSysSession` is off, registry only
	// drives state transitions (useful for tests / embedders that keep
	// their own sys_session). When on, INSERT the anonymous row here
	// so peers can see the in-progress login via the Active Users
	// snapshot.
	//
	// Note: row-level pessimistic locks (HoldRowLocks) are NOT used
	// as the liveness signal anymore — holding `SELECT ... WITH LOCK`
	// on our own rows from a long-lived TX blocks any UPDATE to those
	// rows from a second connection, including our own `JobHeartbeatOwn`
	// refresh of `lastActive`. Liveness is now heartbeat-driven:
	// we UPDATE lastActive every refresh tick; sweep considers rows
	// whose lastActive is older than `kStaleCutoffSec` (see
	// JobSweepStale) as zombies.
	if (m_ownsSysSession && m_writeConn) {
		if (s.Identity().m_expectsAnonPhase) {
			if (InsertSessionRow(m_writeHolder, s, /*userName=*/wxEmptyString)) {
				s.SetInserted(true);
			}
			// Failed INSERT doesn't veto Add — session goes Added with
			// m_inserted=false. Heartbeat will skip it; no peer sees a
			// row; no cleanup needed. Best-effort during bring-up.
		}
	}

	s.Transition(ibSessionState::Added);
	NotifyConnectCreate(&s);
}

void ibSessionRegistry::ProcessAttach(ibRegistryRequest& req)
{
	if (!req.session) return;
	ibSession& s = *req.session;

	if (appData == nullptr) {
		s.TransitionAuth(ibAuthState::AuthFailed, _("appData unavailable"));
		return;
	}

	// Single auth entry — verifies creds and (when info.IsOk()) writes
	// m_userInfo / m_sessionRawPassword onto the target session via
	// InstallUser. Pin scope to the target so InstallUser routes to this
	// session, not whatever the registry thread last touched.
	ibUserInfo info;
	bool ok;
	{
		ibSessionScope scope(&s);
		ok = appData->Login(req.user, req.password, info);
	}
	if (!ok) {
		s.TransitionAuth(ibAuthState::AuthFailed, _("invalid user or password"));
		return;
	}

	// Open-access pass-through: Login returned true with an empty info
	// (no sys_user rows AND caller supplied no creds). Transition to
	// Authenticated anyway — userInfo just stays empty; nothing more to do.
	if (!info.IsOk()) {
		s.TransitionAuth(ibAuthState::Authenticated);
		return;
	}

	// Surface the authenticated user in the row-level identity too.
	ibSessionIdentity id = s.Identity();
	id.m_userName = info.m_strUserName;
	id.m_userGuid = info.m_strUserGuid;
	s.SetIdentity(id);

	// DB wiring — two paths depending on whether the anonymous row
	// was already inserted by ProcessAdd.
	if (m_ownsSysSession && m_writeConn) {
		const wxString guidStr = s.GetId();
		if (s.Inserted()) {
			// Was anonymous, now authenticated — update userName only.
			UpdateSessionUser(m_writeHolder, guidStr, info.m_strUserName, info.m_strUserGuid);
		} else {
			// Creds-first path — Connect(req) with non-empty user skipped
			// the anon-phase INSERT. Write the row now.
			if (InsertSessionRow(m_writeHolder, s, info.m_strUserName))
				s.SetInserted(true);
		}
	}

	s.TransitionAuth(ibAuthState::Authenticated);
}

void ibSessionRegistry::ProcessDetach(ibRegistryRequest& req)
{
	if (!req.session) return;
	ibSession& s = *req.session;
	s.SetUserInfo({});

	if (m_ownsSysSession && m_writeConn && s.Inserted()) {
		const wxString guidStr = s.GetId();
		UpdateSessionUser(m_writeHolder, guidStr, wxEmptyString, wxEmptyString);
	}

	ibSessionIdentity id = s.Identity();
	id.m_userName.clear();
	id.m_userGuid.clear();
	s.SetIdentity(id);

	s.TransitionAuth(ibAuthState::Anonymous);
}

void ibSessionRegistry::DeleteOwnSessionRow(ibSession& s)
{
	// Nothing was ever taken in (an unlisted rented read, or a registry that does not own
	// sys_session), so there is nothing to give back.
	if (!m_ownsSysSession || !s.Inserted())
		return;

	// The SESSION's connection, never the registry's write connection: this runs on whichever
	// thread is closing the session, and two threads sharing one connection is how a driver-level
	// race is bought. The session owns exactly one connection and it is idle by now — the work it
	// was doing is over, that is why we are here.
	ibDatabaseConnectionHolder* const holder = s.Holder();
	if (holder == nullptr)
		return;

	if (DeleteSessionRow(*holder, s.GetId()))
		s.SetInserted(false);   // ProcessRemove then skips its own DELETE — one row, one statement
}

void ibSessionRegistry::ProcessRemove(ibRegistryRequest& req)
{
	if (!req.session) return;
	ibSession& s = *req.session;
	const bool wasAuthenticated = (s.Auth() == ibAuthState::Authenticated);
	s.Transition(ibSessionState::Stopping);

	// Drop the session's queue from the worker pool so any pending tasks
	// for this session don't hold its slot once the session itself goes
	// away. Caller flow (ibWebSession::OnExit) drains via blocking
	// RunOnWorker(...).get() before Close, so by this point there are
	// no in-flight tasks; DropSession just removes the empty queue
	// entry from the pool's per-session map.
	if (m_workerPool) m_workerPool->DropSession(&s);

	// If this session was holding exclusive mode, release it before the
	// row teardown so any parked Adds resume. Drop the weak under the
	// dedicated mutex, then drain outside the lock — DrainPendingExclusive
	// re-enters ProcessAdd which acquires m_exclusiveMutex shared.
	bool wasExclusiveHolder = false;
	{
		std::unique_lock<std::shared_mutex> lk(m_exclusiveMutex);
		auto cur = m_exclusiveSession.lock();
		if (cur && cur.get() == &s) {
			m_exclusiveSession.reset();
			wasExclusiveHolder = true;
		}
	}
	if (wasExclusiveHolder) {
		s.m_exclusive.store(false, std::memory_order_release);
		DrainPendingExclusive();
	}

	// If the leaving session was the registered server, drop the
	// auto-tracking pointer. Subsequent CreateSession calls will leave
	// Server() null until another WebServer-kind session is added.
	{
		std::unique_lock<std::shared_mutex> lk(m_serverMutex);
		if (auto cur = m_currentServer.lock(); cur && cur.get() == &s)
			m_currentServer.reset();
	}

	// Fire Disconnect listeners while session is still alive (between
	// Stopping and Gone) — they may need to query session identity.
	// NotifyDisconnect itself gates the auth-counter decrement on
	// session's auth state, so non-authenticated removals don't
	// disturb the first/last-connect bookkeeping.
	NotifyDisconnect(&s);
	(void)wasAuthenticated;

	// Drop any long-held sys_lock rows owned by this session before the
	// row teardown below. Cluster-aware — every wes process owning the
	// session sees the DELETE on next snapshot tick (or on next acquire
	// attempt against the same key, which then succeeds). See
	// docs/record-locks.md "Planned upgrade path".
	if (auto* lm = ibApplicationData::GetLockManager())
		lm->OnSessionEnd(s.Identity().m_guid);

	if (m_ownsSysSession && m_writeConn && s.Inserted()) {
		const wxString guidStr = s.GetId();
		DeleteSessionRow(m_writeHolder, guidStr);
		s.SetInserted(false);
	}

	// Erase only if the map entry still points to the same ibSession we
	// are removing. On a refresh cycle with tabSid-preset guid, Add for
	// session2 replaces m_own[guid] with session2 before session1's
	// Remove runs; a blind erase here would yank session2's entry and
	// break subsequent heartbeat / snapshot for a live session.
	{
		std::unique_lock<std::shared_mutex> lock(m_ownMutex);
		auto it = m_own.find(s.GetId());
		// owner_before comparison, not a raw ==: the entry may already be
		// expired (owner gone), and an expired entry that belonged to us
		// is exactly the one to erase.
		if (it != m_own.end()) {
			auto held = it->second.Share();
			if (!held || held.get() == &s)
				m_own.erase(it);
		}
	}

	// (No snapshot refresh here — tried on 2026-08-03 and REVERTED. It made Active Users drop a
	//  closed session immediately, and cost a full SELECT of sys_session on the registry thread for
	//  EVERY session close. A job on a short interval closes a session every tick, so the thread
	//  that also serves interactive sessions never got a quiet moment and the UI stopped answering.
	//  The periodic refresh already tells the truth within one tick, and the DELETE above is what
	//  peer processes actually read.)

	s.Transition(ibSessionState::Gone);
}

ibSession::ibExclusiveResult ibSessionRegistry::SetExclusive(ibSession* session, bool on)
{
	using R = ibSession::ibExclusiveResult;
	if (session == nullptr) return R::Pending;
	if (IsFatal())          return R::Pending;

	// Reset the session's result slot before submitting so we can detect
	// when the handler has filled it.
	{
		std::lock_guard<std::mutex> lk(session->m_mtx);
		session->m_exclusiveResult = R::Pending;
	}

	ibRegistryRequest req;
	req.kind        = ibRegistryRequestKind::SetExclusive;
	req.session     = session->shared_from_this();
	req.exclusiveOn = on;
	Submit(std::move(req), ibPriority::Normal);

	std::unique_lock<std::mutex> lk(session->m_mtx);
	session->m_cv.wait_for(lk, std::chrono::seconds(5),
		[&]{ return session->m_exclusiveResult != R::Pending; });
	return session->m_exclusiveResult;
}

void ibSessionRegistry::ProcessSetExclusive(ibRegistryRequest& req)
{
	if (!req.session) return;
	ibSession& s = *req.session;

	auto setResult = [&](ibSession::ibExclusiveResult r) {
		std::lock_guard<std::mutex> lk(s.m_mtx);
		s.m_exclusiveResult = r;
		s.m_cv.notify_all();
	};

	// DB-side flip helper — UPDATE sys_session.exclusive on the row this
	// session owns. No-op for tests / embedders that don't own sys_session
	// or whose schema predates the column. Best-effort; failure here
	// doesn't roll back the in-process state since cluster sweep cleans
	// up zombie rows anyway.
	auto writeExclusiveColumn = [&](int value) {
		if (!m_ownsSysSession || !m_writeConn || !s.Inserted()) return;
		try {
			ibDatabaseQueryBuilder q(&m_writeHolder);
			q.Execute(ibUpdate(session_table,
				{ { wxT("exclusive"), ibConst(ibValue(value)) } },
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(s.GetId())))));
		} catch (...) { /* legacy schema — silent */ }
	};

	if (req.exclusiveOn) {
		// Acquire path.
		std::shared_ptr<ibSession> holder;
		{
			std::shared_lock<std::shared_mutex> lk(m_exclusiveMutex);
			holder = m_exclusiveSession.lock();
		}
		if (holder) {
			if (holder.get() == &s) {
				// Re-acquiring our own — idempotent success.
				setResult(ibSession::ibExclusiveResult::Granted);
				return;
			}
			setResult(ibSession::ibExclusiveResult::HeldByOther);
			return;
		}

		// In-process sole-live check — every other Added session blocks
		// us. Iterate m_own under shared lock; only the registry thread
		// writes m_own so the snapshot is consistent for the duration of
		// this call.
		bool soleLive = true;
		{
			std::shared_lock<std::shared_mutex> lock(m_ownMutex);
			for (const auto& kv : m_own) {
				auto other = kv.second.Share();
				if (!other || other.get() == &s) continue;
				if (other->State() == ibSessionState::Added) {
					soleLive = false;
					break;
				}
			}
		}
		if (!soleLive) {
			setResult(ibSession::ibExclusiveResult::NotSole);
			return;
		}

		// ASK THE TABLE, DON'T ASK LAST SECOND'S MEMORY OF IT. The check below reads m_snapshot, which a
		// job refreshes on its own tick — so a peer that disconnected a moment ago is still standing in it
		// and the acquire is refused for a session that no longer exists. What the user sees is "exclusive
		// mode refused while nobody is connected", and then it works on the second or third press, once a
		// tick has passed. Refresh first: this runs ON the registry thread (the same thread that owns the
		// refresh), so it cannot deadlock with itself, and a failure just leaves the previous snapshot in
		// place — the same stale answer we would have given anyway. ProcessAdd already does exactly this,
		// for exactly this race.
		try { JobRefreshSnapshot(); } catch (...) { /* swallowed: best-effort — a stale snapshot is the old behaviour, not worse */ }

		// Cluster sole-live check + cross-process exclusive scan — peer
		// processes' rows live in m_snapshot. Any peer row blocks acquire;
		// our own row is the one we just verified above.
		{
			std::shared_lock<std::shared_mutex> lk(m_snapshotMtx);
			if (m_snapshot) {
				const wxString ownId = s.GetId();
				const unsigned int n = m_snapshot->GetSessionCount();
				bool clusterSole = true;
				bool peerExclusive = false;
				for (unsigned int i = 0; i < n; ++i) {
					if (m_snapshot->GetSession(i) == ownId) continue;
					clusterSole = false;
					if (m_snapshot->IsExclusive(i)) {
						peerExclusive = true;
						break;
					}
				}
				if (peerExclusive) {
					setResult(ibSession::ibExclusiveResult::HeldByOther);
					return;
				}
				if (!clusterSole) {
					setResult(ibSession::ibExclusiveResult::NotSole);
					return;
				}
			}
		}

		{
			std::unique_lock<std::shared_mutex> lk(m_exclusiveMutex);
			m_exclusiveSession = s.weak_from_this();
		}
		s.m_exclusive.store(true, std::memory_order_release);
		writeExclusiveColumn(1);
		setResult(ibSession::ibExclusiveResult::Granted);
		return;
	}

	// Release path — only the holder can release. Releasing while not
	// the holder is a silent success (idempotent). Drain outside the
	// lock (DrainPendingExclusive re-enters ProcessAdd).
	bool wasHolder = false;
	{
		std::unique_lock<std::shared_mutex> lk(m_exclusiveMutex);
		auto cur = m_exclusiveSession.lock();
		if (cur && cur.get() == &s) {
			m_exclusiveSession.reset();
			wasHolder = true;
		}
	}
	if (wasHolder) {
		s.m_exclusive.store(false, std::memory_order_release);
		writeExclusiveColumn(0);
		DrainPendingExclusive();
	}
	setResult(ibSession::ibExclusiveResult::Granted);
}

void ibSessionRegistry::DrainPendingExclusive()
{
	// Move out so a re-park during the loop body (shouldn't happen — the
	// holder is gone — but defensive) doesn't iterate a moving deque.
	std::deque<ibRegistryRequest> pending;
	pending.swap(m_pendingExclusive);
	for (auto& r : pending)
		ProcessAdd(r);
}

void ibSessionRegistry::ProcessSetActivity(ibRegistryRequest& req)
{
	if (!req.session) return;
	if (!m_ownsSysSession || !m_writeConn) return;
	if (!req.session->Inserted()) return;   // nothing to update yet

	const wxString guidStr = req.session->GetId();
	try {
		ibDatabaseQueryBuilder q(&m_writeHolder);
		q.Execute(ibUpdate(session_table,
			{ { wxT("currentActivity"), ibConst(ibValue(req.activity)) } },
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(guidStr)))));
	} catch (...) { /* swallowed: SetActivity is a UI-state hint, not a correctness signal — a failed UPDATE just means peer dialogs show a stale label until the next tick */ }
}

// --- Periodic jobs --------------------------------------------------------

void ibSessionRegistry::JobSweepStale()
{
	if (!m_ownsSysSession || !m_writeConn) return;

	// Post-handoff grace — see m_sweepSuppressUntilMs in the header
	// for the rationale. During the ~5 s after we recover from a
	// failed refresh, peers' lastActive may be trailing for "we just
	// reconnected" reasons rather than "owner is actually dead", so
	// we skip pruning until everyone has had a chance to bump their
	// own rows.
	{
		const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		const auto deadline = m_sweepSuppressUntilMs.load(std::memory_order_acquire);
		if (deadline != 0 && nowMs < deadline) {
			static std::int64_t lastLoggedDeadline = -1;
			if (lastLoggedDeadline != deadline) {
				SESSION_LOG("[session sweep] suppressed for "
				          << (deadline - nowMs) << " ms (post-handoff grace)");
				lastLoggedDeadline = deadline;
			}
			return;
		}
	}

	// Liveness detection — `lastActive` staleness. Each process's
	// `JobHeartbeatOwn` UPDATEs lastActive every 1s on its own rows,
	// so any row whose lastActive trails `now` by more than
	// `kStaleCutoffSec` is treated as a zombie (force-killed or
	// otherwise dead owner).
	//
	// Row-lock probes (TryProbeRowLock) were tried as a fast path but
	// removed: nobody holds a long-running WITH LOCK on their own
	// rows anymore (that design ate a self-deadlock — see
	// docs/session-registry.md §4). Without HoldRowLocks the probe
	// just succeeds on *every* row, making it useless for
	// distinguishing alive from dead.
	constexpr int kStaleCutoffSec = 10;   // 10× heartbeat interval — force-killed
	                                      // owners disappear from Active Users
	                                      // within ~10s of their last heartbeat

	wxDateTime cutoff = wxDateTime::Now();
	(void)cutoff.Subtract(wxTimeSpan(0, 0, kStaleCutoffSec));

	std::vector<wxString> zombies;
	std::vector<ibGuid>   live;   // everyone that survives this pass — the lock sweep's input
	try {
		ibDatabaseQueryBuilder q(&m_writeHolder);
		ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibProject(ibScan(session_table),
			{ { ibCol(wxT("session")), wxEmptyString }, { ibCol(wxT("lastActive")), wxEmptyString } })));

		while (rs.Next()) {
			const wxString guid = rs.GetResultString(wxT("session"));
			// Ours AND still owned. An expired entry means the owner died
			// without the Remove landing yet — leave it to the stale
			// cutoff below rather than protecting a row nobody holds.
			auto own = m_own.find(guid);
			if (own != m_own.end() && own->second) {
				live.emplace_back(guid);
				continue;  // our own — heartbeat keeps lastActive fresh
			}

			const wxDateTime lastActive = rs.GetResultDate(wxT("lastActive"));
			if (lastActive.IsValid() && lastActive.IsEarlierThan(cutoff))
				zombies.push_back(guid);
			else
				live.emplace_back(guid);
		}
	}
	catch (...) {
		return;  // transient DB error — swallow, try again next tick
	}

	if (!zombies.empty()) {
		SESSION_LOG("[session sweep] removing " << zombies.size()
		          << " zombie row(s) from sys_session");
	}
	for (const wxString& g : zombies) {
		try { DeleteSessionRow(m_writeHolder, g); } catch (...) { /* swallowed: per-row cleanup loop, keep going even if one row's DELETE fails (next sweep retries) */ }
	}

	// Cluster-wide sys_lock cleanup — hand over WHO IS ALIVE and let the lock
	// manager drop everything else. Unconditional, even when this pass found no
	// zombies: a lock outlives the session row that owned it whenever the two
	// deletes are not one operation — a crash between them, a peer that swept the
	// session while this process held the lock table, a kill during shutdown — and
	// a lock whose session is already gone was, until now, unreachable by any
	// cleanup at all. It stayed until someone deleted the row by hand, and until
	// then the document it guarded could not be opened on any machine.
	try {
		if (auto* lm = ibApplicationData::GetLockManager())
			lm->SweepOrphans(live);
	}
	catch (...) { /* swallowed: lock cleanup is best-effort, next sweep retries on stale rows */ }
}

void ibSessionRegistry::JobHeartbeatOwn()
{
	if (!m_ownsSysSession || !m_writeConn) return;
	if (m_own.empty()) return;

	// Wrap the whole body — PrepareStatement can throw on a dead
	// connection, and that happens during shutdown of a leader
	// process: atexit kills the spawned firebird.exe before this
	// background-thread heartbeat job has been told to stop. The
	// throw would escape into the scheduler thread and abort the
	// process. Silently return on any failure — the registry will
	// be torn down by Shutdown shortly anyway, and "transient: next
	// tick retries" is the correct semantics for live failures too.
	//
	// On failure we also kick `ReconnectIfStale` so the next tick
	// runs against a freshly-attached connection — without this,
	// after a cluster-level FB leader handoff our long-lived
	// m_writeConn would loop forever on the dead TCP socket to the
	// previous leader's spawned firebird.exe.
	try {
		ibDatabaseQueryBuilder q(&m_writeHolder);
		const wxDateTime now = wxDateTime::Now();
		for (const auto& kv : m_own) {
			auto s = kv.second.Share();
			if (!s || !s->Inserted()) continue;
			try {
				q.Execute(ibUpdate(session_table,
					{ { wxT("lastActive"), ibConst(ibValue(now)) } },
					ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")),
					        ibConst(ibValue(wxString::FromUTF8(kv.first.c_str()))))));
			}
			catch (...) { /* transient — next tick retries */ }
		}
	}
	catch (...) {
		// Shutdown race / dead connection — next tick retries.
		// Self-heal after leader handoff is handled inside the FB
		// driver's DoRunQuery* / DoPrepareStatement.
	}
}

// Shared UPDATE path for admin directives. Uses the global `db_query`
// (not the registry's pool-checkout'ed connections) because the admin
// endpoints may be reached from callers — designer's Active Users
// dialog — that haven't checked out a pool connection themselves.
static bool WriteSessionSignal(const wxString& sessionGuid,
                                const wxString& signalValue)
{
	if (sessionGuid.IsEmpty()) return false;
	try {
		// default-ctor builder = the global db_query channel (CurrentHolder) — the admin endpoint may
		// be reached from a caller that never checked out a pool connection; a passive scope throws → false.
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpdate(session_table,
			{ { wxT("signal"), ibConst(ibValue(signalValue)) } },
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(sessionGuid)))));
		return true;
	} catch (...) {
		return false;
	}
}

bool ibSessionRegistry::Kick(const wxString& sessionGuid)
{
	return WriteSessionSignal(sessionGuid, wxT("kick"));
}

bool ibSessionRegistry::Reload(const wxString& sessionGuid)
{
	return WriteSessionSignal(sessionGuid, wxT("reload"));
}

void ibSessionRegistry::JobCheckSignal()
{
	if (!m_ownsSysSession || !m_writeConn) return;
	if (m_own.empty()) return;

	// Read-phase: collect (guid, signal) for each own row — one L2 SELECT per own row
	// (a handful per tick; parameterised IN isn't portable, so per-row stays simplest).
	struct Pending { wxString guid; wxString signal; };
	std::vector<Pending> pending;
	ibDatabaseQueryBuilder q(&m_writeHolder);   // bound conn = m_writeConn; reused by the act-phase clear below
	try {
		for (const auto& kv : m_own) {
			auto s = kv.second.Share();
			if (!s || !s->Inserted()) continue;
			const wxString guid = wxString::FromUTF8(kv.first.c_str());
			ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibProject(
				ibFilter(ibScan(session_table),
					ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(guid)))),
				{ { ibCol(wxT("signal")), wxEmptyString } })));
			if (!rs.Next()) continue;
			const wxString sig = rs.GetResultString(wxT("signal"));
			if (sig.IsEmpty()) continue;
			pending.push_back({ guid, sig });
		}
	} catch (...) { /* signal column may be missing on legacy schema */ return; }

	if (pending.empty()) return;

	// Act-phase: dispatch the signal, then clear the cell so the directive fires exactly once
	// per admin write. A failed clear (dead connection) leaves the signal in DB to be picked up
	// next tick (or by a peer process) — acceptable for a one-shot kick / reload directive.
	for (const auto& p : pending) {
		SESSION_LOG("[session SIGNAL] guid=" << (const char*)p.guid.ToUTF8().data()
		          << " value='" << (const char*)p.signal.ToUTF8().data() << "'");

		if (p.signal == wxT("kick")) {
			auto it = m_own.find(p.guid);
			if (auto target = it != m_own.end() ? it->second.Share() : nullptr) {
				// Cooperative cancel first — gives the running script a chance to unwind via
				// ibBackendInterruptException at the next opcode, so the close below runs against an idle
				// worker rather than racing a still-executing task.
				if (m_workerPool)
					m_workerPool->CancelSession(target.get());

				// THE USER IS OWED A SENTENCE. Their window is about to vanish under their hands, and a
				// window that vanishes silently reads as a crash. The reason rides on the session itself,
				// where the frontend's force-exit listener reads it; an ordinary shutdown leaves it empty
				// and says nothing, because the user closing the app already knows why it closed. A job
				// session ignores it — nobody is sitting there to read it.
				target->SetReason(_("Your session has been closed by the administrator."));

				// A KICK MUST REACH THE OWNER, NOT JUST THE BOOKKEEPING. Remove is the teardown of the
				// session RECORD — state to Stopping, worker queue dropped, locks released, row deleted —
				// and it says nothing to whoever is USING the session. So a kicked desktop client lost its
				// row (and vanished from Active Users, which read as success) while its window carried on
				// living without a session. Close(true) is the door that does both, in order: it raises the
				// force-exit flag the interpreter polls and calls OnClose(force) — and WHAT that means is
				// the session kind's own business: a desktop session closes its frame, a web client queues
				// its tab's destroy, a job stops its run (ibJobSession). CloseAll already went through
				// Close(force); only the kick did not.
				//
				// Teardown then follows by itself: the owner dies, its holder is released, and that
				// release IS the Remove. Submitting one here as well would race that path — it stays only
				// as the fallback for a close that answered "not now".
				if (!target->Close(true)) {
					ibRegistryRequest rm;
					rm.kind    = ibRegistryRequestKind::Remove;
					rm.session = target;
					Submit(std::move(rm), ibPriority::Urgent);
				}
			}
		}
		else if (p.signal == wxT("reload")) {
			// Broad "reload metadata" directive. Wes uses the process-wide
			// flag (eviction on next /poll). Per-session listeners
			// (the frontend registers one at startup)
			// react with their own teardown — backend stays GUI-free.
			m_reloadRequested.store(true, std::memory_order_release);
			auto it = m_own.find(p.guid);
			if (it != m_own.end()) {
				if (auto target = it->second.Share())
					NotifyReload(target.get());
			}
		}
		// Future: "refresh" → broadcast SSE prod to client.

		try {
			q.Execute(ibUpdate(session_table,
				{ { wxT("signal"), ibConst(ibValue()) } },   // clear (NULL/empty — read-phase treats both as "no signal")
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(p.guid)))));
		} catch (...) { /* transient — retries next tick */ }
	}
}

void ibSessionRegistry::JobRefreshSnapshot()
{
	if (!m_ownsSysSession || !m_writeConn) {
		static bool warned = false;
		if (!warned) {
			SESSION_LOG("[session REFRESH] skip - ownsSysSession="
			          << m_ownsSysSession << " writeConn="
			          << (m_writeConn ? "ok" : "null"));
			warned = true;
		}
		return;
	}

	// Re-SELECT the full table. Fresh snapshot built outside the lock;
	// swap it in under the writer lock so readers see either the old
	// or new one, never a half-mutated array.
	auto fresh = std::make_unique<ibSessionSnapshot>();
	unsigned rowCount = 0;
	try {
		// kind column may be missing on pre-migration schemas — fall back
		// to reading it via a second pass-tolerant lookup. SELECT itself
		// stays restricted to core columns so the query parses everywhere;
		// kind is read best-effort (missing column would throw from some
		// drivers mid-stream and abort the snapshot).
		ibDatabaseQueryBuilder q(&m_writeHolder);
		ibQueryResult rs = q.From(session_table)
			.Select({ wxT("userName"), wxT("application"), wxT("started"), wxT("computer"), wxT("session") })
			.OrderBy(wxT("started")).OrderBy(wxT("session"))
			.Execute();
		while (rs.Next()) {
			fresh->AppendSession(
				static_cast<ibRunMode>(rs.GetResultInt("application")),
				0,  // kind filled in below for legacy-schema safety
				rs.GetResultDate  ("started"),
				rs.GetResultString("userName"),
				rs.GetResultString("computer"),
				rs.GetResultString("session"));
			++rowCount;
		}
		// Second pass: augment with kind where the column exists. Wrapped
		// in its own try/catch so a legacy schema missing `kind` doesn't
		// torpedo the snapshot built above.
		try {
			ibDatabaseQueryBuilder qk(&m_writeHolder);
			ibQueryResult rsk = qk.ExecuteIR(ibQueryIR(ibProject(ibScan(session_table),
				{ { ibCol(wxT("session")), wxEmptyString }, { ibCol(wxT("kind")), wxEmptyString } })));
			std::unordered_map<wxString, int> kindBySession;
			while (rsk.Next()) {
				kindBySession[rsk.GetResultString("session")]
					= rsk.GetResultInt("kind");
			}
			fresh->SetKindsFromMap(kindBySession);
		} catch (...) { /* legacy schema — fine, kinds stay 0 */ }

		// Third pass: exclusive flag — same legacy-tolerant pattern as
		// kind. Pre-migration schemas keep m_exclusive=false everywhere
		// (default-constructed) which means cluster gates degrade
		// gracefully to "no monopoly anywhere".
		try {
			ibDatabaseQueryBuilder qx(&m_writeHolder);
			ibQueryResult rsx = qx.ExecuteIR(ibQueryIR(ibProject(ibScan(session_table),
				{ { ibCol(wxT("session")), wxEmptyString }, { ibCol(wxT("exclusive")), wxEmptyString } })));
			std::unordered_map<wxString, bool> exclusiveBySession;
			while (rsx.Next()) {
				exclusiveBySession[rsx.GetResultString("session")]
					= rsx.GetResultInt("exclusive") != 0;
			}
			fresh->SetExclusiveFromMap(exclusiveBySession);
		} catch (...) { /* legacy schema — fine, exclusive stays false */ }
	}
	catch (const ibBackendException& err) {
		SESSION_LOG("[session REFRESH] SELECT failed: "
		          << (const char*)err.GetErrorDescription().ToUTF8().data());
		// Self-heal after a leader handoff is handled inside the FB
		// driver's DoRunQuery* — the next tick attaches to whichever
		// leader is current. Record the failure so the recovery edge
		// (next successful refresh) can trigger the soft-landing
		// protocol (immediate own-heartbeat + sweep suppression).
		m_refreshFailedLastTick.store(true, std::memory_order_release);
		return;
	}
	catch (...) {
		SESSION_LOG("[session REFRESH] SELECT failed: unknown exception");
		m_refreshFailedLastTick.store(true, std::memory_order_release);
		return;
	}

	// The table has just been read — stamp it, so an on-demand refresh a moment from now can see
	// that there is nothing to gain by reading it again (SnapshotOlderThan).
	m_snapshotAt = std::chrono::steady_clock::now();

	// Recovery edge — previous tick failed, this one succeeded. That
	// almost always means we just rode through an FB leader handoff:
	// our long-lived m_writeConn was on the dead leader's TCP, the
	// FB driver's proactive ReconnectIfLeaderChanged() at the top of
	// DoRunQueryWithResults reattached us to the new leader. During
	// the gap we couldn't UPDATE our own rows' lastActive. Two
	// follow-ups soften the landing for the cluster:
	//   1. Immediately bump our own rows so the next sweep on ANY
	//      member doesn't see us as stale.
	//   2. Extend sweep suppression — give peers ~5 s to do the same
	//      before any pruning happens (kPostHandoffGraceMs below).
	if (m_refreshFailedLastTick.exchange(false, std::memory_order_acq_rel)) {
		SESSION_LOG("[session REFRESH] recovered after failure - "
		          << "running soft-landing protocol");
		try { JobHeartbeatOwn(); } catch (...) { /* swallowed: soft-landing heartbeat after refresh recovery — next regular tick will retry */ }

		constexpr std::int64_t kPostHandoffGraceMs = 5000;
		const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		m_sweepSuppressUntilMs.store(nowMs + kPostHandoffGraceMs,
		                             std::memory_order_release);
	}

	static unsigned lastCount = UINT_MAX;
	if (rowCount != lastCount) {
		SESSION_LOG("[session REFRESH] snapshot now has " << rowCount
		          << " row(s) (was " << (lastCount == UINT_MAX ? 0 : lastCount)
		          << ")");
		lastCount = rowCount;
	}

	std::unique_lock<std::shared_mutex> lk(m_snapshotMtx);
	m_snapshot = std::move(fresh);
}

ibSessionSnapshot ibSessionRegistry::GetClusterSnapshot() const
{
	std::shared_lock<std::shared_mutex> lk(m_snapshotMtx);
	if (!m_snapshot) return {};
	return *m_snapshot;   // copy under read-lock
}

// --- Thread body ---------------------------------------------------------

void ibSessionRegistry::ThreadBody() noexcept
{
	m_threadAlive.store(true, std::memory_order_release);

	using clock = std::chrono::steady_clock;
	// Snapshot refresh runs at 1 Hz — cheap (one SELECT on sys_session).
	// Sweep runs at 3 s — heavier (one SELECT over the cluster + zombie-row
	// DELETEs) and its output is not time-critical for UI.
	constexpr auto kRefreshInterval = std::chrono::seconds(1);
	constexpr auto kSweepInterval   = std::chrono::seconds(3);
	auto nextRefresh = clock::now() + kRefreshInterval;
	auto nextSweep   = clock::now() + kSweepInterval;

	// Eager initial sweep + snapshot — two reasons:
	//   - UI (Active Users) gets data immediately instead of waiting
	//     for the first refresh tick.
	//   - Zombie rows left over from force-killed previous runs get
	//     cleaned up at startup, so the new process's UI never shows
	//     a list polluted with stale entries. sweep is bounded by
	//     `kStaleCutoffSec` staleness check so even when the FB engine
	//     hasn't rolled back the orphan lock TXs yet, we still DELETE
	//     rows whose lastActive is older than the cutoff.
	try { JobSweepStale();      } catch (...) { /* swallowed: eager initial sweep is best-effort; periodic ticks below will retry */ }
	try { JobRefreshSnapshot(); } catch (...) { /* swallowed: eager initial refresh is best-effort; periodic ticks below will retry */ }

	try {
		while (!m_stop.load(std::memory_order_acquire)) {
			// Wait for work OR next refresh tick OR stop. Refresh is
			// the shorter timer so we pick it here.
			{
				std::unique_lock<std::mutex> lk(m_submitMtx);
				m_submitCv.wait_until(lk, nextRefresh, [this]{
					for (auto& bin : m_bins)
						if (!bin.empty()) return true;
					return m_stop.load(std::memory_order_acquire);
				});
			}
			if (m_stop.load(std::memory_order_acquire)) break;

			// Drain queue by strict descending priority.
			auto batch = DrainAll();
			for (auto& req : batch) {
				switch (req.kind) {
					case ibRegistryRequestKind::Add:          ProcessAdd(req);          break;
					case ibRegistryRequestKind::Attach:       ProcessAttach(req);       break;
					case ibRegistryRequestKind::Detach:       ProcessDetach(req);       break;
					case ibRegistryRequestKind::Remove:       ProcessRemove(req);       break;
					case ibRegistryRequestKind::SetActivity:  ProcessSetActivity(req);  break;
					case ibRegistryRequestKind::SetExclusive: ProcessSetExclusive(req); break;
				}
			}

			// Refresh every second — UI polling sees updates within a
			// frame of each other instead of waiting a full sweep.
			const auto now = clock::now();
			if (now >= nextRefresh) {
				JobHeartbeatOwn();       // bump lastActive for every own row
				JobRefreshSnapshot();
				nextRefresh = now + kRefreshInterval;
			}

			// Sweep only every 3s — cluster-wide zombie cleanup, bounded
			// latency for dead-session pickup is fine. Signal check piggy-
			// backs on the same tick since it's cheap and similarly
			// latency-tolerant.
			if (now >= nextSweep) {
				JobSweepStale();
					JobCheckSignal();
				nextSweep = now + kSweepInterval;
			}

			m_tickCounter.fetch_add(1, std::memory_order_release);

			// Mark startup complete after the first full iteration —
			// drain + housekeeping ran without an exception, so sys_session
			// has reached a consistent state for this process. From now
			// on Die() takes the hard std::terminate path; before now
			// (e.g. an exception inside the very first ProcessAdd /
			// JobHeartbeatOwn after eager sweep) it soft-fails so the
			// producer surfaces a startup error instead of the process
			// vanishing.
			m_started.store(true, std::memory_order_release);
		}

		// Graceful stop: drain urgent work one last time so pending
		// Removes get a chance to DELETE their rows before DB closes.
		auto final_batch = DrainAll();
		for (auto& req : final_batch) {
			if (req.kind == ibRegistryRequestKind::Remove)
				ProcessRemove(req);
			// Non-urgent pending requests dropped — producers waiting on
			// their session's cv observe Timeout / state-unchanged.
		}
	}
	// THE PROJECT'S OWN EXCEPTION FIRST — it now DERIVES from std::exception, so the order is what
	// C++ requires (derived before base) rather than a preference. Before it derived, it landed in
	// `catch (...)` and died as "unknown": the message it carries, the only thing that says WHAT
	// went wrong, was thrown away at the exact moment the process decided to stop.
	catch (const ibBackendException& err) {
		Die(wxString::Format(wxT("registry-thread backend exception: %s"), err.GetErrorDescription()));
	}
	catch (const std::exception& e) {
		// what() is UTF-8 bytes (that is what the standard door carries) — convert, don't let the
		// narrow overload re-read them in the current locale and mangle a Cyrillic message.
		Die(wxString::Format(wxT("registry-thread exception: %s"), wxString::FromUTF8(e.what())));
	}
	catch (...) {
		Die(wxT("registry-thread unknown exception"));
	}

	m_threadAlive.store(false, std::memory_order_release);
}

// --- Fatal fail-stop -----------------------------------------------------

void ibSessionRegistry::Die(const wxString& why)
{
	m_fatalReason = why;
	m_fatal.store(true, std::memory_order_release);
	m_threadAlive.store(false, std::memory_order_release);

	// Log first — survives both the soft-fail and the std::terminate paths.
	//
	// TO A FILE as well as to stderr: a GUI build has no console, so the stderr line reaches
	// nobody, and the wxLog flush cannot outrun the std::terminate below. This is the last thing
	// the process says about why it is stopping — it has to land somewhere readable afterwards.
	ibTraceToFile(wxT("[session] FATAL: ") + why);
	std::cerr << "[session] FATAL: " << why.ToUTF8().data() << std::endl;
	wxLog::FlushActive();

	// Soft-fail path: ThreadBody never completed its first main-loop
	// iteration. The thread had no committed sys_session writes (other
	// than the eager sweep's DELETEs of stale rows, which are idempotent
	// across runs), so there's no cluster-visible inconsistency to clean
	// up. Return — producer (session->Open) blocks on WaitForAuth, the
	// timeout elapses, Open returns false, and the top-level OnRun catch
	// turns the recorded m_fatalReason into a startup-error dialog. No
	// silent terminate, no minidump-less death.
	if (!m_started.load(std::memory_order_acquire)) {
		// Wake any producer that's parked on a session's auth cv so they
		// observe IsFatal() promptly instead of waiting the full 20s.
		m_submitCv.notify_all();
		return;
	}

	// Steady-state failure: registry was alive and consistent. Pretending
	// otherwise would let stale state propagate cluster-wide. std::terminate
	// lets a custom terminate_handler log / dump before exit; abort() would
	// skip any registered handler. Either way the process stops —
	// registry-thread death means sys_session lies, nothing good can come
	// from continuing.
	std::terminate();
}
