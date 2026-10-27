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

bool ibClientInstance::Login(const ibString& user, const ibString& password)
{
	std::lock_guard<std::recursive_mutex> lifeLock(m_lifecycleMutex);

	if (m_applicationInstance == nullptr)
		return false;
	if (m_session.Share() != nullptr)
		return true;   // logged in already

	// The session is made in the base as soon as it is asked for — its row in sys_session shows "logging in"
	// until the login settles — under the instance's own id, one identifier for the client, the registry's
	// session and the row. A wrong password just lets the holder die here, which removes the row.
	// ITS KIND IS THE CLIENT'S MODE: the runtime's a thin client; the designer's a thin designer — a designer as the
	// desktop's is (no runtime, the one designer of the base), whatever process hosts it.
	const ibSessionKind kind = m_mode == ibProtocol::ibLogin::ibMode::Designer ? ibSessionKind::ThinDesigner : ibSessionKind::ThinClient;
	// …REFUSED BEFORE ANY LOGIN — another designer at work (ibDesignerExclusivePolicy): thrown on, its reason the person's
	// to read. Turned into false here, it reached the client as a wrong password, and the login window asked for a name
	// that was never the question (2026-10-20).
	ibSessionHolder holder = m_applicationInstance->CreateSession<ibClientSession>(kind, m_id, m_address);
	if (!holder)
		return false;
	ibSession* const session = holder.Get();

	// ⭐ ON THE SESSION'S OWN WORKER — the open runs the configuration's script, and everything else of this
	// session runs there: on the caller's thread it would run BESIDE the worker serving this session's requests,
	// two threads in one session.
	ibSession::OpenResult opened = ibSession::OpenResult::Failed;
	try {
		session->Submit([&]() {
			opened = holder->Open(user, password);
		}).get();
	}
	catch (const ibCoreException& err) {
		// A configuration that does not start refuses the login too (ibSession::CompileRoot throws). The client can
		// be told only "refused"; the reason goes where an administrator reads it.
		ibJournalError(ibT("client"), ibT("login of '%s' refused: %s"), user, err.GetErrorDescription());
		return false;
	}
	// …A REGISTRY THAT DID NOT ANSWER is a busy server, not a wrong name: thrown on as the registry's silence at Add
	// is, and said as it is. Read as a wrong password, it sent people under load back to the login window to type
	// a password nobody had questioned (2026-10-23, 300 people at once).
	if (opened == ibSession::OpenResult::Timeout)
		ibBackendException::Error(_("registry did not answer to Attach"));
	if (opened != ibSession::OpenResult::Authenticated)
		return false;

	m_user = user;
	m_session = ibSessionWatch(holder);
	m_holder = std::move(holder);
	return true;
}

bool ibClientInstance::Start()
{
	// No lifecycle lock here: the start may wait for the person (a request), and an exit must be able to come in
	// meanwhile and cancel it. The worker orders the two: the exit's teardown runs after the start has unwound.
	if (!m_holder)
		return m_frame != nullptr;   // started already, or never logged in

	// THE FRAME FIRST, of the client's mode, and published before the start runs: a start may open forms or make a
	// request to the person, and the client reaches both through the frame.
	m_frame = ibClientFrame::Create(m_mode, std::move(m_holder), this);
	if (m_frame == nullptr) {
		ibJournalWarning(ibT("client"), ibT("client %s refused: no mode %d"), m_id, static_cast<int>(m_mode));
		m_holder.Reset();   // nobody took it — the session ends here
		m_session.Reset();
		return false;
	}

	// The mode's start — the veto point of the whole login. Then the mode's own tabs, as the desktop window opens
	// them (ibFrontendMainFrame::Show).
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
