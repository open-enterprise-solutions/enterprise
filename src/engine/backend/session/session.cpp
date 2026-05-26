#include "session.h"
#include "sessionRegistry.h"

#include "backend/moduleManager/moduleManager.h"
#include "backend/metadataConfiguration.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/procUnitState.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/appData.h"
#include "workerPool.h"

#include <utility>
#include <chrono>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace {
// Per-thread current-session map. Lookup semantics depend on the
// access mode owned by ibSessionRegistry — see ibSession::Current.
//
// weak_ptr storage so a session destroyed while a binding still
// references it auto-expires. Critical for the nested-SessionScope
// refresh-cycle race: the inner scope's dtor restores its captured
// m_prev into the map; if that previous session got destroyed in the
// meantime, restoring a raw pointer would resurrect a dangling
// reference. With weak_ptr the expired binding either unlocks to
// nullptr (Current returns null/fallback) or is simply erased on the
// next observation — no UAF.
std::shared_mutex s_currentMutex;
std::unordered_map<std::thread::id, std::weak_ptr<ibSession>> s_currentByThread;
} // namespace

ibSession::ibSession(wxString id, ibSessionKind kind)
	: m_id(std::move(id))
	, m_kind(kind)
	, m_workDate(wxDateTime::Now())
{
}

ibSession::~ibSession()
{
	// s_currentByThread holds weak_ptr<ibSession>; when the last strong
	// reference drops, every entry pointing here auto-expires. Subsequent
	// Current() calls do lock() and observe nullptr. The normal teardown
	// path also calls UnbindSession(this) explicitly from appData's
	// OnDisconnect listener — that's the place that does the cleanup;
	// duplicating it here would only walk the bindings map under a unique
	// lock on every destruction, contending with Current() readers, with
	// no correctness benefit.

	// Ensure the destruction chain for objects created by this session
	// runs even when the session falls off without an explicit ClearRoot
	// (registry-driven Remove, abnormal teardown). Idempotent — ClearRoot
	// is a no-op when m_root is already null.
	ClearRoot();
}

ibValueModuleManagerConfiguration* ibSession::GetManagerModule() const
{
	return m_root;   // ibValuePtr's implicit operator T*()
}

void ibSession::Close(bool force)
{
	// Force close cuts the session's runtime everywhere: any in-flight
	// script breaks out of its interpreter loop on the next opcode
	// (m_forceExit flag, checked in ibProcUnit::Execute), and OnForceExit
	// fires the per-kind side effect (GUI exits wxApp; web/server just
	// stops running). Hard-close path was previously achieved through
	// the process-level ibApplicationData::ForceExit; folding it into
	// Close(true) keeps a single "kick this session" entry point.
	if (force)
		RequestForceExit();

	// Main-thread teardown hook — wx frame destruction must run on the
	// thread that created the frame. Derived ibGUISession overrides
	// OnDestroySession to schedule frame->Destroy() through wx's event
	// loop. force=false lets the override veto (AllowClose script,
	// unsaved-data prompt); force=true skips the check and tears down.
	if (!OnDestroySession(force) && !force)
		return;

	// Submit Remove@Urgent — registry-thread ProcessRemove erases m_own,
	// which drops the last shared_ptr and destroys this session.
	auto& reg = ibSessionRegistry::Instance();
	if (reg.IsFatal())
		return;
	ibRegistryRequest req;
	req.kind    = ibRegistryRequestKind::Remove;
	req.session = shared_from_this();
	reg.Submit(std::move(req), ibPriority::Urgent);
}

void ibSession::Detach(std::chrono::milliseconds timeout)
{
	auto& reg = ibSessionRegistry::Instance();
	if (reg.IsFatal()) return;
	if (State() != ibSessionState::Added) return;

	// Prime the axis so WaitForAuth below sees the actual handler result.
	if (Auth() == ibAuthState::Authenticated)
		TransitionAuth(ibAuthState::Authenticated);

	ibRegistryRequest req;
	req.kind    = ibRegistryRequestKind::Detach;
	req.session = shared_from_this();
	reg.Submit(std::move(req), ibPriority::Normal);

	WaitForAuth(Auth(), timeout);
}

void ibSession::SetActivity(const wxString& activity)
{
	auto& reg = ibSessionRegistry::Instance();
	if (reg.IsFatal()) return;

	ibRegistryRequest req;
	req.kind     = ibRegistryRequestKind::SetActivity;
	req.session  = shared_from_this();
	req.activity = activity;
	reg.Submit(std::move(req), ibPriority::Low);
}

void ibSession::SetExclusive(bool on)
{
	// Registry runs the queue handshake + wait and gives us back the
	// verdict; we only translate it into an exception for the script
	// layer. Granted == success path (acquire AND release).
	const ibExclusiveResult r = ibSessionRegistry::Instance().SetExclusive(this, on);
	switch (r) {
	case ibExclusiveResult::Granted:
		return;
	case ibExclusiveResult::HeldByOther:
		ibBackendCoreException::Error(_("Another session is in exclusive mode"));
	case ibExclusiveResult::NotSole:
		ibBackendCoreException::Error(_("Cannot acquire exclusive mode: other sessions are active"));
	case ibExclusiveResult::Pending:
		ibBackendCoreException::Error(_("Exclusive mode request did not complete"));
	}
}

ibValueModuleManagerConfiguration* ibSession::CreateRoot(ibMetaDataConfigurationBase* metaData)
{
	// Per-session root mm — replaces the legacy ibMetaDataConfigurationFile
	// process-singleton. Each session owns its own copy of the metadata
	// tree's runtime state. CreateRoot only allocates; CreateMainModule
	// (compile) runs from RunDatabase after common-module descriptors are
	// registered via OnBeforeRunMetaObject. Idempotent — second call with
	// the same metadata returns the existing root unchanged.
	if (metaData == nullptr)
		return nullptr;
	if (m_root)
		return m_root;
	auto* commonMeta = metaData->GetCommonMetaObject();
	if (commonMeta == nullptr)
		return nullptr;

	m_root = ibValuePtr<ibValueModuleManagerConfiguration>(
		new ibValueModuleManagerConfiguration(metaData, commonMeta));

	return m_root;
}

bool ibSession::CompileRoot()
{
	if (!m_root) return false;
	if (!m_root->CreateMainModule()) return false;

	// Runtime bring-up — formerly an explicit mm->AttachRuntime(s)
	// call from appData / webSession after CompileRoot. Folded in
	// here so callers see one entry point: compile + attach is the
	// session's own responsibility. AttachRuntime self-gates by
	// session kind (Enterprise / WebClient / Service execute; others
	// short-circuit), so no external wantsRuntime check is needed.
	m_root->AttachRuntime(this);

	// Lambda executor — m_root's procUnit is live after AttachRuntime,
	// so SetParent target is valid. ibValueFunction's Execute resolves
	// this through ibSession::GetLambdaRuntime().
	//
	// Custom frame array layout: regular ProcUnit setup puts own
	// m_cCurContext at m_pppArrayList[0] AND [1] (duplicate, since
	// runtime slot indices start at 1 with bDelta=false). The shim
	// has no own locals — lambda body's frame is per-call cRunContext
	// — so we substitute root's frame for the [0,1] pair. That way
	// lambda compile's depth=1 stamping (lambda discipline walks bc
	// chain to topmost = root, single increment) lands directly on
	// root mm's bound slots: Catalogs / Documents / Manager / system
	// functions all resolve at depth=1 without an offset hack.
	if (m_lambdaRuntime == nullptr) {
		if (auto rootPu = m_root->GetProcUnit()) {
			m_lambdaRuntime = std::make_unique<ibProcUnit>();
			m_lambdaRuntime->SetParent(rootPu.get());

			ibProcUnit* shim = m_lambdaRuntime.get();
			const unsigned int n = shim->GetParentCount();
			shim->m_ppArrayCode = new ibProcUnit*[n + 1];
			shim->m_ppArrayCode[0] = shim;
			shim->m_pppArrayList = new ibValue**[n + 2];
			shim->m_pppArrayList[0] = rootPu->m_cCurContext.m_pRefLocVars;
			shim->m_pppArrayList[1] = rootPu->m_cCurContext.m_pRefLocVars;
			for (unsigned int i = 0; i < n; i++) {
				ibProcUnit* p = shim->GetParent(i);
				shim->m_ppArrayCode[i + 1] = p;
				shim->m_pppArrayList[i + 2] = p->m_cCurContext.m_pRefLocVars;
			}
		}
	}

	return true;
}

bool ibSession::DestroyRoot()
{
	if (!m_root) return false;
	// Lambda runtime's parent is m_root's procUnit; drop it first so
	// the SetParent target stays valid right up to the moment we
	// release it.
	m_lambdaRuntime.reset();
	// Symmetric to CompileRoot: detach runtime before destroying the
	// main module so common-module ProcUnits drop in order. Formerly
	// an explicit mm->DetachRuntime(s) call from webSession.
	m_root->DetachRuntime(this);
	return m_root->DestroyMainModule();
}

void ibSession::ClearRoot()
{
	// Lambda runtime depends on m_root's procUnit (SetParent target);
	// drop it before m_root itself goes away.
	m_lambdaRuntime.reset();

	if (m_root) {
		m_root->DetachRuntime(this);
		m_root->DestroyMainModule();
		m_root = nullptr;
	}
}

void ibSession::EnsureRoot()
{
	// Wired by ibSessionRegistry::NotifyAuthenticated to land between
	// OnFirstConnect (metadataCreate) and OnAuthenticated (RunDatabase /
	// CompileRoot). CreateRoot itself is idempotent; this wrapper just
	// guards on activeMetaData so headless sessions (Launcher, technical)
	// without metadata don't fault.
	if (m_root) return;
	if (activeMetaData == nullptr) return;
	CreateRoot(activeMetaData);
}

ibSession* ibSession::Current()
{
	auto& reg = ibSessionRegistry::Instance();
	const auto tid = std::this_thread::get_id();

	// Debug-thread redirection: a thread registered as a debug-server
	// worker resolves Current() to "whichever script thread is parked
	// at a breakpoint right now" (front of the FIFO queue maintained
	// by EnterDebugLoop / LeaveDebugLoop). Lets debug command handlers
	// (Eval, ExpandExpression, EvalToolTip, EvalAutocomplete) reach
	// the right session through the same Current() call other code
	// uses, without an explicit sid threaded through every handler.
	if (reg.IsDebugThread(tid)) {
		// shared_ptr<...>::get() — caller holds nothing; returned raw
		// is valid as long as some other strong-ref keeps the session
		// alive (registry's m_own typically). Debug commands run
		// synchronously while the script thread is parked, so the
		// session is alive for the duration of the handler.
		if (auto sp = reg.GetActiveDebugTarget()) return sp.get();
		// No session parked → fall through to the regular path below
		// so a debug worker can still observe its own Designer-side
		// connection on a thread that was bound separately.
	}

	const auto mode = reg.GetAccessMode();
	std::shared_lock<std::shared_mutex> lk(s_currentMutex);
	switch (mode) {
	case AccessMode::Single:
		// One session per process. Map holds at most one entry; return
		// the lone value regardless of calling thread. Empty map
		// (pre-bind / post-clear) or expired weak_ptr → nullptr.
		return s_currentByThread.empty()
			? nullptr
			: s_currentByThread.begin()->second.lock().get();
	case AccessMode::Shared:
		// Per-thread lookup with fallback to the registry's system session.
		// Expired binding (session destroyed without explicit Unbind) →
		// fall through to fallback, same as no binding at all.
		if (auto it = s_currentByThread.find(tid); it != s_currentByThread.end()) {
			if (auto sp = it->second.lock()) return sp.get();
		}
		return reg.GetFallback();
	}
	return nullptr;
}

void ibSession::SetAccessMode(AccessMode mode)
{
	ibSessionRegistry::Instance().SetAccessMode(mode);
}

ibSession::AccessMode ibSession::GetAccessMode()
{
	return ibSessionRegistry::Instance().GetAccessMode();
}

void ibSession::SetFallback(ibSession* s)
{
	ibSessionRegistry::Instance().SetFallback(s);
}

void ibSession::ClearFallback()
{
	ibSessionRegistry::Instance().ClearFallback();
}

ibSession* ibSession::GetByThread(std::thread::id tid)
{
	const auto mode = ibSessionRegistry::Instance().GetAccessMode();
	std::shared_lock<std::shared_mutex> lk(s_currentMutex);
	switch (mode) {
	case AccessMode::Single:
		return s_currentByThread.empty()
			? nullptr
			: s_currentByThread.begin()->second.lock().get();
	case AccessMode::Shared:
		if (auto it = s_currentByThread.find(tid); it != s_currentByThread.end()) {
			if (auto sp = it->second.lock()) return sp.get();
		}
		return ibSessionRegistry::Instance().GetFallback();
	}
	return nullptr;
}

std::vector<std::pair<std::thread::id, ibSession*>> ibSession::SnapshotByThread()
{
	std::shared_lock<std::shared_mutex> lk(s_currentMutex);
	std::vector<std::pair<std::thread::id, ibSession*>> out;
	out.reserve(s_currentByThread.size());
	for (const auto& kv : s_currentByThread) {
		// Skip expired entries — the binding's session was destroyed.
		// Snapshot reflects live state, not historical bindings.
		if (auto sp = kv.second.lock())
			out.emplace_back(kv.first, sp.get());
	}
	return out;
}

void ibSession::BindSessionToThread(ibSession* s, std::thread::id tid)
{
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	if (s != nullptr)
		// weak_from_this is C++17, doesn't throw if the session isn't
		// yet wrapped in a shared_ptr — returns expired weak_ptr in
		// that case. All ibSession instances are made via make_shared
		// in the registry's typed factory, so by the time anyone calls
		// Bind the shared control block exists. If a future code path
		// constructs ibSession on the stack, the binding silently
		// expires on next lookup — safer than dangling raw pointer.
		s_currentByThread[tid] = s->weak_from_this();
	else
		s_currentByThread.erase(tid);
	// Interpreter state needs no separate setup — ibSession::GetPUState()
	// resolves via Current() each call, so the binding above is the
	// single point that "switches" the state visible to this thread.
}

void ibSession::UnbindThread(std::thread::id tid)
{
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	s_currentByThread.erase(tid);
}

void ibSession::UnbindSession(ibSession* s)
{
	// Idempotent cleanup. Erases entries pointing to `s` AND any expired
	// weak_ptr entries we encounter while iterating — defensive, since
	// the bindings would auto-expire on next lookup anyway. Caller still
	// passes raw `ibSession*` (this) for ergonomics.
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	for (auto it = s_currentByThread.begin(); it != s_currentByThread.end();) {
		auto locked = it->second.lock();
		if (!locked || (s != nullptr && locked.get() == s))
			it = s_currentByThread.erase(it);
		else
			++it;
	}
}

ibBackendDocFrame* ibSession::CurrentFrame()
{
	ibSession* s = Current();
	return s != nullptr ? s->GetFrame() : nullptr;
}

ibRunContext* ibSession::CurrentRunContext()
{
	if (ibSession* s = Current())
		if (auto* dbg = s->Debug())
			return dbg->m_runContext;
	return nullptr;
}

ibProcUnitState* ibSession::GetPUState()
{
	if (ibSession* s = Current())
		return &s->m_procUnitState;

	// Sessionless fallback — codeRunner.exe (and any other host that
	// runs ad-hoc scripts without a session, e.g. command-line script
	// runners) needs a real ibProcUnitState to back m_currentRunModule
	// / m_runContext stack / error_place during Compile + Execute.
	// thread_local so concurrent sessionless callers each get their
	// own state — no shared mutation, no race.
	static thread_local ibProcUnitState ts_fallbackPUState;
	return &ts_fallbackPUState;
}

void ibSession::WakeDebugLoop()
{
	// Mark the session for cancellation and pop any parked debug loop.
	//
	// Step 1 — set m_forceExit so ibProcUnit::Execute's opcode loop
	// unwinds on the next iteration (after returning from DoDebugLoop)
	// instead of resuming user script. We bypass RequestForceExit
	// because its OnForceExit side effect (web → wfrontendCallProcessExitHook
	// when wes was started in --debug mode) would kill the entire
	// process; here we want to cancel only THIS session's interpreter.
	// Direct atomic store; deduplication against a later RequestForceExit
	// is fine since the destroy path doesn't follow up with one.
	m_forceExit.store(true, std::memory_order_release);
	// Step 2 — flip the debug-park flag and notify the per-session CV
	// so a script worker parked in ibDebuggerServer::DoDebugLoop
	// returns immediately. Mirrors the per-session wake performed by
	// ibDebuggerServer::WakeDebugSession on designer disconnect — same
	// graceful LeaveLoop is sent on the wire when the loop unwinds.
	if (m_debug == nullptr) return;
	m_debug->m_debugLoop = false;
	std::lock_guard<std::mutex> lk(m_debug->m_mutex);
	m_debug->m_cv.notify_all();
}

void ibSession::RequestForceExit()
{
	// Set first, then dispatch. The interpreter check observes the
	// flag on its next opcode loop iteration; OnForceExit dispatches
	// the per-kind side effect (wx exit, schedule Close, etc.).
	if (m_forceExit.exchange(true, std::memory_order_acq_rel))
		return;   // already requested — don't fire OnForceExit twice
	OnForceExit();

	// Registry fan-out — covers session kinds whose virtual OnForceExit
	// is the empty base (wes' WebServer technical session). Without it,
	// a debug-thread Current() that falls back to the system row would
	// close it but no host listener would learn about it.
	ibSessionRegistry::Instance().NotifyForceExit(this);
}

std::future<void> ibSession::Submit(std::function<void()> task)
{
	auto* pool = ibSessionRegistry::Instance().GetWorkerPool();
	if (pool != nullptr)
		return pool->Submit(this, std::move(task));

	// No pool — single-session GUI host. Run the task inline so the
	// caller's future contract still holds (call returns with the
	// future already fulfilled or carrying the exception). When the
	// GUI worker pool lands later, this fallback turns into a
	// CallAfter-backed dispatch on the wx main thread.
	std::promise<void> p;
	try { task(); p.set_value(); }
	catch (...) { p.set_exception(std::current_exception()); }
	return p.get_future();
}

wxString ibSession::Reason() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_reason;
}

void ibSession::Transition(ibSessionState next, const wxString& reason)
{
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (!reason.IsEmpty()) m_reason = reason;
		m_state.store(next, std::memory_order_release);
	}
	// notify_all — multiple producers may wait on different predicates
	// (WaitForState vs WaitForAuth). Spurious wakes short-circuit via the
	// predicate lambda.
	m_cv.notify_all();
}

void ibSession::TransitionAuth(ibAuthState next, const wxString& reason)
{
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (!reason.IsEmpty()) m_reason = reason;
		m_auth.store(next, std::memory_order_release);
	}
	m_cv.notify_all();
}

ibSessionState ibSession::WaitForState(ibSessionState from, std::chrono::milliseconds timeout)
{
	std::unique_lock<std::mutex> lk(m_mtx);
	m_cv.wait_for(lk, timeout, [this, from]{
		return m_state.load(std::memory_order_acquire) != from;
	});
	return m_state.load(std::memory_order_acquire);
}

ibAuthState ibSession::WaitForAuth(ibAuthState from, std::chrono::milliseconds timeout)
{
	std::unique_lock<std::mutex> lk(m_mtx);
	m_cv.wait_for(lk, timeout, [this, from]{
		return m_auth.load(std::memory_order_acquire) != from;
	});
	return m_auth.load(std::memory_order_acquire);
}

bool ibSession::Open(const wxString& user, const wxString& password)
{
	// Must be Added (registry accepted the session); Anonymous or AuthFailed
	// on the auth axis — either is a valid retry point.
	if (State() != ibSessionState::Added) return false;

	auto& reg = ibSessionRegistry::Instance();
	if (reg.IsFatal()) return false;

	constexpr auto timeout = std::chrono::seconds(20);

	auto submitAttach = [&](const wxString& u, const wxString& p) {
		// Reset auth axis so WaitForAuth below has a known "from" value —
		// otherwise a prior AuthFailed state would trigger the wait
		// immediately without the new Attach having been processed.
		TransitionAuth(ibAuthState::Anonymous);

		ibRegistryRequest req;
		req.kind     = ibRegistryRequestKind::Attach;
		req.session  = shared_from_this();
		req.user     = u;
		req.password = p;
		reg.Submit(std::move(req), ibPriority::Normal);

		return WaitForAuth(ibAuthState::Anonymous, timeout);
	};

	ibAuthState res = submitAttach(user, password);
	if (res == ibAuthState::Authenticated) {
		// NotifyAuthenticated fires three phases in order:
		//   1. OnFirstConnect listeners — process-level metadata bootstrap
		//      (metadataCreate, populates activeMetaData) on the first auth.
		//   2. session->EnsureRoot — per-session root mm allocated NOW so
		//      step 3's listeners can rely on GetManagerModule() != null.
		//   3. OnAuthenticated listeners — per-session bring-up
		//      (RunDatabase fires OnBefore/AfterRunMetaObject which read
		//      session->mm; CompileRoot; AttachRuntime).
		// Note: NotifyAuthenticated already calls BindSessionToThread
		// before firing listeners, so any breakpoint hit inside them
		// resolves Current() to THIS session (the registry-fallback
		// trap is closed at that level, no extra scope needed here).
		reg.NotifyAuthenticated(this);
		return true;
	}

	// Interactive fallback — GUI override shows login dialog (shared for
	// designer + enterprise via ibGUISession::OnShowAuthenticate). Pin
	// `this` as Current() for the dialog's lifetime so the OK handler's
	// appData->Login → InstallUser writes m_userInfo / m_sessionRawPassword
	// onto THIS session (Current() resolves to it). Without the scope,
	// InstallUser would target whatever the calling thread last bound —
	// often nullptr in pre-auth flows — and m_userInfo would stay empty,
	// making submitAttach below fire with blank creds (= "invalid user
	// or password" on the second pass through ProcessAttach).
	bool dlgOk;
	{
		ibSessionScope scope(this);
		dlgOk = OnShowAuthenticate(user, password);
	}
	if (!dlgOk) return false;

	res = submitAttach(m_userInfo.m_strUserName, m_sessionRawPassword);
	if (res == ibAuthState::Authenticated)
		reg.NotifyAuthenticated(this);
	return res == ibAuthState::Authenticated;
}

// --- ibSessionThreadBinding --------------------------------------------

ibSessionThreadBinding::ibSessionThreadBinding(ibSession* s) noexcept
	: m_tid(std::this_thread::get_id())
{
	ibSession::BindSessionToThread(s, m_tid);
}

ibSessionThreadBinding::~ibSessionThreadBinding()
{
	ibSession::UnbindThread(m_tid);
}

// --- ibSessionScope -----------------------------------------------------

ibSessionScope::ibSessionScope(ibSession* s)
{
	const std::thread::id tid = std::this_thread::get_id();
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	auto it = s_currentByThread.find(tid);
	// Save the previous binding as a weak_ptr COPY (not raw). If the
	// referenced session is destroyed while this scope is alive, the
	// dtor's restore-step lock()s and either falls back to "no binding"
	// or restores a still-live session — never resurrects a freed
	// pointer. Was the root cause of the rapid-F5 UAF in CurrentFrame
	// (see project_refresh_execute_crash 2026-04-27).
	if (it != s_currentByThread.end()) m_prev = it->second;
	if (s != nullptr)
		s_currentByThread[tid] = s->weak_from_this();
	else
		s_currentByThread.erase(tid);
	// Interpreter state — no separate cache to manage. ibSession::GetPUState()
	// resolves through Current() each call; the binding update above is
	// what makes the new session's state visible.
}

ibSessionScope::~ibSessionScope()
{
	const std::thread::id tid = std::this_thread::get_id();
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	if (auto sp = m_prev.lock())
		s_currentByThread[tid] = m_prev;   // weak_ptr copy of still-live binding
	else
		s_currentByThread.erase(tid);
}

std::shared_ptr<ibDatabaseLayer> ibSession::DatabaseLayer()
{
	ibSession* sess = ibSession::Current();
	if (sess == nullptr)
		ibBackendCoreException::Error(_("ses_query: no active session"));
	// Single entry — holder's EnsureConnection resolves TX > scope >
	// fresh Checkout (auto-bound as scope). See connectionHolder.h.
	auto conn = sess->EnsureConnection();
	if (!conn || !conn->IsOpen())
		ibBackendCoreException::Error(_("ses_query: session failed to acquire a database connection"));
	return conn;
}
