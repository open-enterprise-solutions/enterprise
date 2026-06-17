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

void ibWebClientSession::SetFrame(ibWebFrame* f)
{
	m_frame = f;
}

extern void wfrontendCallProcessExitHook();

void ibWebClientSession::OnForceExit()
{
	// Web counterpart to ibGUISession's wxTheApp::Exit. Fires from
	// ibSession::RequestForceExit (called by Close(true)). Now that
	// Open/Close pin this session via ibSessionScope, the debug
	// thread's Current() resolves to the right WebClient — designer
	// kill-debug ends up here, not on the wes system row.
	if (!wfrontendDebugMode())
		return;
	wfrontendCallProcessExitHook();
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
	return m_session.get();
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
	// desktop's appData->CreateSession<ibEnterpriseSession>() (see
	// enterprise/mainApp.cpp). The anonymous row lands in sys_session
	// immediately (empty userName + eWEB_RUNTIME_MODE app mode) so
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
	// Connect / OnCreateSession failure; web HTTP handlers expect bool
	// false instead of an exception escaping into httplib's loop.
	// Translate here. Server() is auto-populated by the registry from
	// the most recent WebServer-kind session — wes's system session,
	// already added at wfrontendInit — so keep-alive hook sees this tab
	// as a real client without an explicit pointer here.
	ibWebClientSession* sessionRaw = nullptr;
	try {
		sessionRaw = appData->CreateSession<ibWebClientSession>(presetGuid, address);
	} catch (const ibBackendException&) {
		sessionRaw = nullptr;
	}
	if (sessionRaw == nullptr)
		return false;
	// Grab our own strong reference now — registry's m_own holds another
	// one, but a concurrent ProcessAdd that reuses our presetGuid would
	// overwrite m_own[guid] and drop registry's reference, freeing the
	// session out from under our raw pointer. shared_from_this is safe
	// because ibSession was constructed via std::make_shared in the
	// registry factory path.
	m_session = sessionRaw->shared_from_this();

	// Session-owned auth — unified path with desktop ibAppEnterprise /
	// ibAppDesigner flow. ibSession::Authenticate submits Attach through
	// the registry directly (shared_from_this), and on failure fires
	// OnShowAuthenticate. Base ibSession returns false there — web's
	// HTTP login form is the user-visible prompt, driven from the
	// client-side, not from a modal.
	if (m_session->Open(user, password) != ibSession::OpenResult::Authenticated) {
		m_session->Close();   // submit Remove → sys_session row DELETEd
		m_session.reset();
		return false;
	}

	m_user = user;

	// Spin up the per-cookie application. Session lifetime tied to
	// m_session — explicitly Close()d in OnExit / dtor.
	auto app = std::make_unique<ibWebApplication>();
	// sessionRaw is typed ibWebClientSession*; SetSessionContext takes the
	// same type so SetFrame later doesn't need a cast.
	app->SetSessionContext(sessionRaw);

	// CreateRoot / CompileRoot / AttachRuntime are driven by
	// ibSessionRegistry::NotifyAuthenticated → appData::WireSessionEvents
	// (OnFirstConnect → metadataCreate; EnsureRoot; OnAuthenticated →
	// RunDatabase + CompileRoot + AttachRuntime for runtime modes).
	// Both fired inside the m_session->Open(...) call above. Calling them
	// again here would re-execute the main module's top-level script.
	bool initOk = false;
	{
		ibSessionScope scope(m_session.get());
		initOk = app->OnInit();
	}
	if (!initOk) {
		m_session->Close();
		m_session.reset();
		return false;
	}

	m_app = std::move(app);
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

	// If the script worker is parked at a breakpoint, unpark it before
	// touching the runtime — DetachRuntime below resets the
	// per-session ProcUnit, and m_app->OnExit's RunOnWorker(...) must
	// dispatch onto an idle worker. A worker stuck inside DoDebugLoop's
	// CV wait would block both. WakeDebugLoop sets m_forceExit on the
	// session and pops the debug-park flag, so the parked thread
	// unwinds out of DoDebugLoop, the next opcode-loop iteration in
	// ibProcUnit::Execute throws on the cancellation flag, and the
	// originally-Submit'ed task returns with an exception. Designer
	// side gets a clean LeaveLoop on the wire — the unpark path inside
	// DoDebugLoop sends it before returning, just as a designer-issued
	// Continue would.
	if (m_session)
		m_session->WakeDebugLoop();

	// Drain the worker before tearing down the runtime. Submit a no-op
	// and wait — the per-session worker queue is FIFO, so by the time
	// our no-op runs, the cancelled breakpoint task has unwound and
	// every prior task has drained. Without this, DetachRuntime
	// would race the unwinding script over ProcUnit destruction.
	// Swallow any exception (failed task, race with Close) so OnExit
	// keeps making progress; the rest of teardown is idempotent.
	if (m_session) {
		try { m_session->Submit([] {}).get(); }
		catch (...) { /* drain best-effort */ }
	}

	// Symmetric teardown — drop this session's ProcUnit entries from
	// the shared moduleManager before destroying the app + closing
	// the session. ClearRoot folds DetachRuntime + DestroyMainModule
	// + m_root reset; ibSessionScope keeps the session pinned so the
	// detach bookkeeping runs in the right context.
	if (m_session) {
		ibSessionScope scope(m_session.get());
		m_session->ClearRoot();
	}

	if (m_app) {
		m_app->OnExit();
		m_app.reset();
	}

	// Submit Remove@Urgent through session — registry thread DELETEs the
	// sys_session row and releases the row lock. Safe to call here: the
	// worker already joined inside m_app->OnExit. Our m_session strong-ref
	// keeps the session alive until reset() below — registry's m_own may
	// have already been overwritten by a concurrent same-presetGuid login,
	// but the object stays valid through Close()'s shared_from_this().
	if (m_session) {
		m_session->Close();
		m_session.reset();
	}

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
