#include "webSession.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/moduleManager/moduleManager.h"

#include "webApplication.h"
#include "webClientSession.h"
#include "webFrame.h"
#include "wfrontend.h"

ibBackendDocFrame* ibWebClientSession::GetFrame() const
{
	return m_frame;   // ibWebFrame* → ibBackendDocFrame* (implicit upcast)
}

void ibWebClientSession::SetFrame(ibWebFrame* frame)
{
	m_frame = frame;
}

extern void wfrontendCallProcessExitHook();
extern void wfrontendRequestDestroySession(const wxString& sessionId);

bool ibWebClientSession::OnClose(bool force)
{
	// Web close = destroy this tab: its ibWebSession goes, dropping the
	// app, the frame and finally the holder — the same chain the desktop
	// gets from closing its window.
	// No tab (login failed before the frame existed) — nothing to close,
	// so end it the windowless way rather than wait for a holder release
	// that nobody will make.
	if (m_frame == nullptr)
		return ibSession::OnClose(force);

	// The tab has nobody to ask yet, so it never refuses. When the app
	// grows its own "may I close?" (unsaved input, a running report) it
	// answers on the !force path and returns false from here — Close()
	// then reports the refusal with nothing torn down, exactly like the
	// desktop's AllowClose. Under force nobody is asked either way.

	// Queued, not done inline: the caller is usually this session's own
	// worker (script EndJob) or the registry thread, and the teardown
	// drains that worker — doing it here would deadlock against itself.
	wfrontendRequestDestroySession(GetId());

	// In debug mode the close must also break listen_after_bind, or wes
	// would keep serving with no debuggee left.
	if (wfrontendDebugMode())
		wfrontendCallProcessExitHook();
	return true;
}

namespace {

std::int64_t NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

ibWebSession::ibWebSession(wxString id, wxString user)
	: m_id(std::move(id))
	, m_user(std::move(user))
	, m_lastActiveMs(NowMs())
{
}

ibWebSession::~ibWebSession()
{
	if (m_initialized)
		OnExit();
}

ibSession* ibWebSession::Session() const
{
	// Valid for as long as the owning frame is alive — which is exactly
	// as long as any caller here can meaningfully use it.
	return m_session.Share().get();
}

bool ibWebSession::OnInit()
{
	if (activeMetaData != nullptr)
		m_configName = activeMetaData->GetConfigName();

	m_initialized = true;
	return true;
}

bool ibWebSession::Login(const wxString& user, const wxString& password)
{
	// Serialize against a concurrent OnExit on this same instance.
	// SessionManager::Login holds a shared_ptr keeper while we run; if
	// SessionManager::Destroy fires for the same id at the same time it
	// holds its own keeper and proceeds to OnExit. Without the lock the
	// two raced over m_session / m_app and worker bring-up vs teardown.
	std::lock_guard<std::recursive_mutex> lifeLock(m_lifecycleMutex);

	if (!m_initialized || appData == nullptr)
		return false;
	if (m_app)
		return true;  // already logged in — idempotent

	// Anonymous-phase create through the session registry — same shape as
	// desktop's appData->CreateSession<ibGUISession>() (see
	// enterprise/mainApp.cpp). The anonymous row lands in sys_session
	// immediately (empty userName + WebClient kind) so
	// admin / Active-Users listings see "login in progress" between
	// GET / (cookie mint) and POST /login settling. Reuse the tab's
	// sessionStorage id as the registry's session guid — one identifier
	// across cookie / SessionManager / sys_session.session column. Auth
	// happens separately via session->Open below so a wrong password
	// just rejects the Attach; the anonymous row can be retried with new
	// creds without re-creating the ticket.
	const wxString& presetGuid = m_id;
	const wxString  address    = wxString::FromUTF8(wfrontendServerAddress().c_str());
	// CreateSession throws via ibBackendCoreException::Error on registry
	// Connect failure; web HTTP handlers expect bool
	// false instead of an exception escaping into httplib's loop.
	// Translate here. Server() is auto-populated by the registry from
	// the most recent WebServer-kind session — wes's system session,
	// already added at wfrontendInit — so keep-alive hook sees this tab
	// as a real client without an explicit pointer here.
	ibSessionHolder holder;
	try {
		holder = appData->CreateSession<ibWebClientSession>(ibSessionKind::WebClient, presetGuid, address);
	} catch (const ibCoreException&) {
		holder.Reset();
	}
	if (!holder)
		return false;
	ibSession* const sessionRaw = holder.Get();

	// Session-owned auth — unified path with desktop ibAppEnterprise /
	// ibAppDesigner flow. ibSession::Authenticate submits Attach through
	// the registry directly (shared_from_this), and on failure fires
	// OnShowAuthenticate. Base ibSession returns false there — web's
	// HTTP login form is the user-visible prompt, driven from the
	// client-side, not from a modal.
	// A failed login just lets the holder die here — that removes the
	// anonymous sys_session row. No cleanup call to forget.
	//
	// …and a configuration that does not start refuses the login too (ibSession::CompileRoot throws). The
	// page can show only "refused"; the reason goes where an administrator reads it.
	//
	// ⭐ ON THE SESSION'S OWN WORKER — the open and the start below both run the configuration's script (its main
	// module, its start handlers), and everything else of this session runs there: on the request's thread they
	// ran BESIDE the worker serving this session's polls, two threads in one session, and a question asked at
	// start waited without holding the session (ibWorkerPool::Await).
	bool opened = false;
	try {
		sessionRaw->Submit([&]() {
			opened = holder->Open(user, password) == ibSession::OpenResult::Authenticated;
		}).get();
	}
	catch (const ibCoreException& err) {
		ibJournalError(wxT("web"), wxT("login of '%s' refused: %s"), user, err.GetErrorDescription());
		return false;
	}
	if (!opened)
		return false;

	m_user = user;

	// Spin up the per-cookie application. It picks the session up from
	// the holder we hand it in OnInit — the same holder that goes into
	// the web frame, which is what actually owns the session.
	auto app = std::make_unique<ibWebApplication>();

	// CreateRoot / CompileRoot / AttachRuntime are driven by
	// ibSessionRegistry::NotifyAuthenticated → appData::WireSessionEvents
	// (OnFirstConnect → metadataCreate; EnsureRoot; OnAuthenticated →
	// RunDatabase + CompileRoot + AttachRuntime for runtime modes).
	// Both fired inside the m_session->Open(...) call above. Calling them
	// again here would re-execute the main module's top-level script.
	// Ownership goes where it goes on desktop too — into the main window,
	// which OnInit builds. We keep only a watch: this object observes the
	// session, the web frame owns it.
	m_session = ibSessionWatch(holder);

	// PUBLISHED BEFORE THE START RUNS — a question asked at start (OnStart) reaches the client through /session,
	// and /session finds the frame through m_app: published after, the question waited for a client that could
	// not see it (the stage-0 run, 2026-10-05). A start that fails takes it back.
	m_app = std::move(app);
	bool initOk = false;
	sessionRaw->Submit([&]() { initOk = m_app->OnInit(std::move(holder)); }).get();   // the worker, as above
	if (!initOk) {
		m_app.reset();
		m_session.Reset();
		return false;
	}
	return true;
}

void ibWebSession::OnExit()
{
	// Serialize against a concurrent Login on this same instance — see
	// the comment in Login(). Acquired before any state inspection so a
	// racing Login that wins the lock first sees a consistent state on
	// its check, and Destroy's OnExit tears down a fully-initialized
	// session rather than half-set-up state.
	std::lock_guard<std::recursive_mutex> lifeLock(m_lifecycleMutex);

	if (!m_initialized)
		return;

	// If the script is parked at a breakpoint, unpark it before touching
	// the runtime — DetachRuntime below resets the per-session ProcUnit.
	// On a worker fiber the wait parks the fiber; WakeDebugLoop clears
	// the flag and Wakes the pool so that fiber resumes and leaves
	// DoDebugLoop. On a thread that is not a fiber the same call notifies
	// the condition variable. Either way the script sends LeaveLoop and
	// the cancel below unwinds it, and the originally submitted task
	// returns with an exception.
	//
	// And a script waiting for its client — a question (ibWorkerPool::Await) — is cancelled: it runs this
	// session's tasks while it waits, so the drain below would run INSIDE its wait and the runtime and the tabs
	// would go down under it. Cancelled, it unwinds first and the drain waits behind it.
	if (auto s = m_session.Share()) {
		s->WakeDebugLoop();
		s->Cancel();
	}

	// Drain the worker before tearing down the runtime. Submit a no-op
	// and wait — the per-session worker queue is FIFO, so by the time
	// our no-op runs, the cancelled breakpoint task has unwound and
	// every prior task has drained. Without this, DetachRuntime
	// would race the unwinding script over ProcUnit destruction.
	// Swallow any exception (failed task, race with Close) so OnExit
	// keeps making progress; the rest of teardown is idempotent.
	if (auto s = m_session.Share()) {
		try { s->Submit([] {}).get(); }
		catch (...) { /* drain best-effort */ }
	}

	// Symmetric teardown — drop this session's ProcUnit entries from
	// the shared moduleManager before destroying the app + closing
	// the session. ClearRoot folds DetachRuntime + DestroyMainModule
	// + m_root reset; ibSessionScope keeps the session pinned so the
	// detach bookkeeping runs in the right context.
	if (auto s = m_session.Share()) {
		ibSessionScope scope(s.get());
		s->ClearRoot();
	}

	// Destroying the app destroys the web frame, and the frame releases
	// the holder — which closes the session and DELETEs its sys_session
	// row. Safe here: the worker already joined inside m_app->OnExit.
	if (m_app) {
		m_app->OnExit();
		m_app.reset();
	}

	m_session.Reset();

	m_initialized = false;
}

std::int64_t ibWebSession::LastActiveMs() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_lastActiveMs;
}

void ibWebSession::Touch()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_lastActiveMs = NowMs();
}
