#include "clientInstance.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"

#include "frmserver/docView/docView.h"

#include "clientFrame.h"
#include "clientSession.h"

namespace {

std::int64_t NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

ibClientInstance::ibClientInstance(ibApplicationInstance* applicationInstance, const ibString& id, const ibString& address,
	ibProtocol::ibLogin::ibMode mode)
	: m_applicationInstance(applicationInstance), m_id(id), m_address(address), m_mode(mode), m_lastActiveMs(NowMs())
{
}

ibClientInstance::~ibClientInstance()
{
	OnExit();
}

ibClientSession* ibClientInstance::Session() const
{
	return m_frame != nullptr ? m_frame->Session() : nullptr;
}

bool ibClientInstance::CreateSession()
{
	std::lock_guard<std::recursive_mutex> lifeLock(m_lifecycleMutex);

	if (m_applicationInstance == nullptr)
		return false;
	if (m_frame != nullptr)
		return true;   // made already

	// The session is made in the base as soon as it is asked for — its row in sys_session shows "logging in"
	// until the login settles — under the instance's own id, one identifier for the client, the registry's
	// session and the row. A wrong password lets the frame go, and the holder with it, which removes the row.
	// ITS KIND IS THE CLIENT'S MODE: the runtime's a thin client; the designer's a thin designer — a designer as the
	// desktop's is (no runtime, the one designer of the base), whatever process hosts it.
	const ibSessionKind kind = m_mode == ibProtocol::ibLogin::ibMode::Designer ? ibSessionKind::ThinDesigner : ibSessionKind::ThinClient;
	// …REFUSED BEFORE ANY LOGIN — another designer at work (ibDesignerExclusivePolicy): thrown on, its reason the person's
	// to read. Turned into false here, it reached the client as a wrong password, and the login window asked for a name
	// that was never the question (2026-10-20).
	ibSessionHolder holder = m_applicationInstance->CreateSession<ibClientSession>(kind, m_id, m_address);
	if (!holder)
		return false;

	// THE FRAME AT ONCE, of the client's mode, and published before the login runs anything: the login's compile may
	// say a failure in the frame's error window, a start may open forms or ask the person something — the client
	// reaches all of it through the frame. Its menus and the designer's debugger come with the start (OnCreate): they
	// read the rights and the configuration the login brings.
	m_session = ibSessionWatch(holder);
	m_frame = ibClientFrame::Create(m_mode, std::move(holder), this);
	if (m_frame == nullptr) {
		// Nobody took the holder: it goes with this call, and the session ends here.
		ibJournalWarning(ibT("client"), ibT("client %s refused: no mode %d"), m_id, static_cast<int>(m_mode));
		m_session.Reset();
		return false;
	}
	return true;
}

ibSession::OpenResult ibClientInstance::Open(const ibString& user, const ibString& password)
{
	// No lifecycle lock, as the start: the login's compile may ask the person (the error window), and an exit must be
	// able to come in meanwhile and cancel it.
	ibClientSession* const session = Session();
	if (session == nullptr)
		return ibSession::OpenResult::Failed;

	// ⭐ THROWN ON, SAID AS IT IS — Failed is the name or the password, nothing else. A configuration that does not
	// compile (ibSession::CompileRoot throws), a base held exclusively, a pool that gave no connection: caught and
	// turned into a refused password they reached the client as one, and its login window asked for a name nobody
	// questioned (Max, 2026-11-13: a module that does not compile opens the login window). The host says them.
	ibSession::OpenResult opened = ibSession::OpenResult::Failed;
	try {
		opened = session->Open(user, password);
	}
	catch (const ibCoreException&) {
		// …except a failure the frame's error window has said (BackendError keeps it in the messages, whatever the
		// person chose — a compile, a script of the session's start): read and answered, the login ends quietly, as a
		// login the person cancelled.
		bool said = false;
		for (const ibClientFrame::PendingMessage& message : m_frame->DrainPendingMessages())
			said = said || message.level == ibStatusMessage_Error;
		m_frame.reset();   // the frame releases the holder — the session ends
		m_session.Reset();
		if (said)
			return ibSession::OpenResult::Cancelled;
		throw;
	}
	if (opened != ibSession::OpenResult::Authenticated) {
		m_frame.reset();
		m_session.Reset();
		return opened;
	}
	m_user = user;
	return opened;
}

bool ibClientInstance::Start()
{
	// No lifecycle lock here: the start may wait for the person (a request), and an exit must be able to come in
	// meanwhile and cancel it. The worker orders the two: the exit's teardown runs after the start has unwound.
	if (m_frame == nullptr)
		return false;   // never made, or the login let it go

	// The frame's own parts — the menus by the rights the login brought (OnCreate). Then the mode's start — the veto
	// point of the whole login. Then the mode's own tabs, as the desktop window opens
	// them (ibFrontendMainFrame::Show).
	m_frame->OnCreate();
	if (!m_frame->AllowRun()) {
		m_frame.reset();   // the frame releases the holder — the session ends
		m_session.Reset();
		return false;
	}
	m_frame->CreateStartupPage();
	return true;
}

void ibClientInstance::OnExit()
{
	std::lock_guard<std::recursive_mutex> lifeLock(m_lifecycleMutex);

	if (std::shared_ptr<ibSession> session = m_session.Share()) {
		// A script parked at a breakpoint is let go, and one waiting for the person (a request: ibWorkerPool::Await)
		// is cancelled — it runs this session's tasks while it waits, so the teardown below would otherwise run
		// INSIDE its wait, and the documents and the runtime would go down under it. Cancelled, it unwinds first.
		session->WakeDebugLoop();
		session->Cancel();

		// THE TEARDOWN ON THE WORKER — the documents' views close the forms, the configuration's exit and the
		// runtime are the session's, and the session's state is reached through its worker's binding. The
		// worker's queue is FIFO, so this runs after everything the cancelled script left behind.
		try {
			session->Submit([this, &session]() {
				// Whatever is still open goes down unconditionally, as the desktop's main window closed its
				// documents: the frame closes each one — its views told (OnClosingDocument), its form let go.
				if (m_frame != nullptr)
					m_frame->CloseDocuments();
				if (ibValueModuleManagerRuntimeConfiguration* const manager = session->GetManagerModule())
					manager->ExitMainModule();
				session->ClearRoot();
			}).get();
		}
		catch (...) {
			// The rest of the teardown does not depend on it: the frame and the session go regardless.
		}
	}

	// The frame releases the holder — which closes the session and removes its row.
	m_frame.reset();
	m_session.Reset();
}

std::int64_t ibClientInstance::LastActiveMs() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_lastActiveMs;
}

void ibClientInstance::Touch()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_lastActiveMs = NowMs();
}

bool ibClientInstance::RequestClose()
{
	m_closeRequested.store(true);
	return true;
}
