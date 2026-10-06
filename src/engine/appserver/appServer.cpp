#include "appServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <wx/utils.h>   // wxGetFullHostName — the server named in a base's address

#ifdef __WXMSW__
#include <windows.h>   // SetConsoleCtrlHandler — the console window closing
#endif

#include "backend/appHost.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/mcp/mcpServer.h"
#include "backend/session/session.h"

#include "sfrontend/client/clientHost.h"

namespace {

// WHERE THE SERVER STANDS — one value, read by the main thread and the signals alike. Ctrl+C, a console closing, a
// service stop only ask for the stop (Serving → Stopping); the main thread does it, and says when it is done
// (Stopped), which is what a console closing waits for (OnConsoleClose).
enum class ibServeState { Serving, Stopping, Stopped };
std::atomic<ibServeState> s_state{ ibServeState::Serving };

void OnStopSignal(int)
{
	ibServeState serving = ibServeState::Serving;
	s_state.compare_exchange_strong(serving, ibServeState::Stopping);
}

#ifdef __WXMSW__
// THE CONSOLE WINDOW CLOSING, a logoff, a shutdown: Windows ends the process the moment this handler returns,
// so raising the flag and returning stopped nothing — the bases were cut off with their sessions in hand, and the
// journal had no "stopping" (measured 2026-10-06). This one waits for the main thread's stop, within the few
// seconds the system grants. Ctrl+C and Ctrl+Break go on to the signal handlers.
BOOL WINAPI OnConsoleClose(DWORD type)
{
	if (type != CTRL_CLOSE_EVENT && type != CTRL_LOGOFF_EVENT && type != CTRL_SHUTDOWN_EVENT)
		return FALSE;
	OnStopSignal(0);
	for (int waited = 0; waited < 4500 && s_state.load() != ibServeState::Stopped; waited += 50)
		::Sleep(50);
	return TRUE;
}
#endif

} // namespace

ibAppServer::ibAppServer(ibServerConfig& config, const wxString& locale) :
	m_config(config), m_locale(locale)
{
}

// Out of line: the client host is only declared in the header.
ibAppServer::~ibAppServer() = default;

int ibAppServer::Run()
{
	if (!m_config.HasConf()) {
		ibAppServerSay(ibJournalMark::Error, wxT("no config at %s - nothing to serve"), m_config.GetConfPath());
		return 1;
	}

	wxString keyError;
	if (!m_config.LoadKey(keyError)) {
		ibAppServerSay(ibJournalMark::Error, wxT("%s - nothing is served"), keyError);
		return 1;
	}

	std::vector<ibConfiguredInstance> instances = m_config.ReadInstances();
	m_config.AssignIds(instances);
	ibAppServerSay(ibJournalMark::Info, wxT("config %s: %u base(s)"),
		m_config.GetConfPath(), static_cast<unsigned>(instances.size()));

	// Told to stop from here on — while the bases are still being opened too: what is open is then closed in
	// order instead of the process being cut off with its bases in hand.
	std::signal(SIGINT, OnStopSignal);
	std::signal(SIGTERM, OnStopSignal);
#ifdef SIGBREAK
	std::signal(SIGBREAK, OnStopSignal);   // Windows: Ctrl+Break
#endif
#ifdef __WXMSW__
	::SetConsoleCtrlHandler(OnConsoleClose, TRUE);   // after the signals: asked first, it passes on what is theirs
#endif

	// However Run ends — served and stopped, or refused at any step — a console closing waits no longer.
	struct ibStoppedMark { ~ibStoppedMark() { s_state.store(ibServeState::Stopped); } } stoppedMark;

	for (const ibConfiguredInstance& instance : instances) {
		if (s_state.load() != ibServeState::Serving)
			break;
		Open(instance);
	}

	if (m_sessions.empty()) {
		ibAppServerSay(ibJournalMark::Error, wxT("no base could be served - stopping"));
		ibApplicationInstance::DestroyAppDataEnv();
		return 1;
	}

	wxString names;
	for (const ibSessionHolder& session : m_sessions) {
		if (!names.IsEmpty())
			names += wxT(", ");
		names += session->GetApplicationInstance()->GetInstanceName();
	}

	// THE PORT THE CLIENTS COME IN BY — the one written in the config, or, at the first start, the first free
	// one from the server's own, written back so that it stays the one clients are pointed at. A written port
	// that is taken is refused, never moved off: the clients look for this server there.
	wxString portError;
	unsigned short port = 0;
	if (!m_config.ReadPort(port, portError)) {
		ibAppServerSay(ibJournalMark::Error, wxT("%s - nothing is served"), portError);
		Stop();
		return 1;
	}
	const bool choose = port == 0;
	if (choose)
		port = ibServerConfig::s_defaultPort;

	std::map<wxString, ibClientHost*> hosts;
	for (const auto& entry : m_clientHosts)
		hosts[entry.first] = entry.second.get();

	const wxString host = m_config.ReadHost();
	wxString refusal;
	if (!m_clientListener.Start(host, port, choose, std::move(hosts), refusal)) {
		ibAppServerSay(ibJournalMark::Error, wxT("the clients' port did not open: %s - nothing is served"), refusal);
		Stop();
		return 1;
	}
	if (choose)
		m_config.WritePort(port);

	// EACH BASE'S ADDRESS, as a person gives it to a client — oes://<server>[:port]/<base>, which the client
	// opens as the WebSocket /<base>/client on that port. Listening on every interface, the server is named by
	// this machine's name.
	const wxString server = host.IsEmpty() ? wxGetFullHostName() : host;
	for (const auto& entry : m_clientHosts)
		ibAppServerSay(ibJournalMark::Info, wxT("base '%s' for clients: oes://%s:%u/%s"), entry.first, server,
			static_cast<unsigned>(port), entry.first);

	ibAppServerSay(ibJournalMark::Info, wxT("serving %u of %u base(s): %s - Ctrl+C to stop"),
		static_cast<unsigned>(m_sessions.size()), static_cast<unsigned>(instances.size()), names);

	while (s_state.load() == ibServeState::Serving)
		std::this_thread::sleep_for(std::chrono::milliseconds(250));

	ibAppServerSay(ibJournalMark::Info, wxT("stopping"));
	Stop();
	ibAppServerSay(ibJournalMark::Info, wxT("stopped"));
	return 0;
}

bool ibAppServer::Open(const ibConfiguredInstance& instance)
{
	wxString ibPassword, error;
	if (!m_config.OpenSecret(instance.m_name, wxT("IbPassword"), instance.m_ibPassword, ibPassword, error)) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s' is not opened: %s"), instance.m_name, error);
		return false;
	}

	// THE THREAD COMES BACK AS IT WAS. Opening leaves it working for the base — right for a window's thread; wrong
	// for this one, which opens every base and works for none: the next base would be asked through this one.
	// It is given back. (The login gives its session binding back itself — NotifyAuthenticated.)
	const ibApplicationInstanceScope opening(nullptr);

	ibApplicationInstance* applicationInstance = nullptr;
	try {
		// By its name in the server folder — where it lives the base's group says, and the opening reads it.
		ibServerInstanceRequest request;
		request.m_folder = m_config.GetFolder();
		request.m_name   = instance.m_name;
		request.m_locale = m_locale;
		applicationInstance = ibApplicationInstance::CreateAppDataEnv(request);
	}
	catch (const ibBackendException& err) {
		error = err.GetErrorDescription();
	}
	if (applicationInstance == nullptr) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s' did not open: %s"), instance.m_name,
			error.IsEmpty() ? wxString(wxT("no reason given")) : error);
		return false;
	}

	// THE SERVER'S OWN SESSION IN THE BASE — made by the base in hand and opened while this thread still works
	// for it (opening left it so), so the registry that makes it and the configuration it loads are the base's
	// own. Of kind Service: while it lives the base admits other application servers and no client process
	// (ibServiceExclusivePolicy).
	ibSessionHolder session;
	try {
		session = applicationInstance->CreateSession(ibSessionKind::Service);
		if (session && session->Open(instance.m_ibUser, ibPassword) != ibSession::OpenResult::Authenticated)
			session.Reset();
	}
	catch (const ibBackendException& err) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s': login refused: %s"), instance.m_name,
			err.GetErrorDescription());
		session.Reset();
	}
	if (!session) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s': the server could not log in - the base is closed again"),
			instance.m_name);
		ibApplicationInstance::DestroyAppDataEnv(applicationInstance);
		return false;
	}

	// THE BASE'S CLIENTS — the protocol served for it (sfrontend), and the base's MCP when its settings switch it
	// on: an assistant reaches the clients through it (client_call), as a client.
	m_clientHosts[instance.m_name] = std::make_unique<ibClientHost>(applicationInstance);
	if (ibMcpServer* const mcp = ibApplicationInstance::GetMcpServer(applicationInstance)) {
		wxString refusal;
		mcp->LoadSettings(session.Get());
		if (mcp->Start(session.Get(), refusal))
			ibAppServerSay(ibJournalMark::Info, wxT("base '%s': MCP at %s"), instance.m_name, mcp->GetEndpoint());
		else
			ibAppServerSay(ibJournalMark::Info, wxT("base '%s': no MCP - %s"), instance.m_name, refusal);
	}

	m_sessions.push_back(std::move(session));
	ibAppServerSay(ibJournalMark::Info, wxT("base '%s' is served: %s"), instance.m_name,
		applicationInstance->GetDatabaseDescription());
	return true;
}

void ibAppServer::Stop()
{
	// The port first — no client comes in any more, and those connected are let go; then the clients, each
	// base's while the base is still open: every client's session is torn down on its own worker. Then each
	// base's MCP, which reaches them.
	m_clientListener.Stop();
	m_clientHosts.clear();
	for (const ibSessionHolder& session : m_sessions) {
		if (ibMcpServer* const mcp = ibApplicationInstance::GetMcpServer(session->GetApplicationInstance()))
			mcp->Stop();
	}

	while (!m_sessions.empty()) {
		ibSessionHolder session = std::move(m_sessions.back());
		m_sessions.pop_back();
		ibApplicationInstanceScope working(session->GetApplicationInstance());
		session.Reset();
	}

	// Every base, newest first, and the process with them.
	ibApplicationInstance::DestroyAppDataEnv();
}
