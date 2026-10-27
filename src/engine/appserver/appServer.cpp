#include "appServer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#ifdef __WXMSW__
#include <windows.h>   // SetConsoleCtrlHandler — the console window closing
#endif

#include "backend/appHost.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/mcp/mcpServer.h"
#include "backend/session/session.h"

#include "frmserver/client/clientHost.h"

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

// THE CLUSTER'S WORD — a server the cluster started (--worker) is told to stop on its stdin, and stops when its stdin
// closes too: the cluster gone, nobody would ever stop it. The thread is left to the process: it waits in a read.
void ListenToCluster()
{
	std::thread([] {
		std::string line;
		while (std::getline(std::cin, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line == ibWorkerPipe::Stop)
				break;
		}
		OnStopSignal(0);
	}).detach();
}

} // namespace

ibAppServer::ibAppServer(ibServerConfig& config, const ibString& locale, const ibString& workerBase, unsigned route) :
	m_config(config), m_locale(locale), m_workerBase(workerBase), m_route(route)
{
}

// Out of line: the client host is only declared in the header.
ibAppServer::~ibAppServer() = default;

int ibAppServer::Run()
{
	// A SERVER OF ONE BASE, for the cluster: the clients come in by the cluster's port, and the cluster hands each to the
	// server of its base. Nothing else starts one.
	if (m_workerBase.IsEmpty()) {
		ibAppServerSay(ibJournalMark::Error, ibT("a server serves one base for the application cluster - run appcluster, "
			"which starts one for each base of %s"), m_config.GetConfPath());
		return 1;
	}

	if (!m_config.HasConf()) {
		ibAppServerSay(ibJournalMark::Error, ibT("no config at %s - nothing to serve"), m_config.GetConfPath());
		return 1;
	}

	ibString keyError;
	if (!m_config.LoadKey(keyError)) {
		ibAppServerSay(ibJournalMark::Error, ibT("%s - nothing is served"), keyError);
		return 1;
	}

	// Its base as the cluster left the config: the Id and the assistant's port written before it was started.
	std::vector<ibConfiguredInstance> instances = m_config.ReadInstances();
	m_config.AssignIds(instances);
	const auto base = std::find_if(instances.begin(), instances.end(),
		[this](const ibConfiguredInstance& instance) { return instance.m_name == m_workerBase; });
	if (base == instances.end()) {
		ibAppServerSay(ibJournalMark::Error, ibT("the config %s has no base '%s' - nothing to serve"),
			m_config.GetConfPath(), m_workerBase);
		return 1;
	}

	// Told to stop from here on — while the base is still being opened too: what is open is then closed in order
	// instead of the process being cut off with its base in hand.
	std::signal(SIGINT, OnStopSignal);
	std::signal(SIGTERM, OnStopSignal);
#ifdef SIGBREAK
	std::signal(SIGBREAK, OnStopSignal);   // Windows: Ctrl+Break
#endif
#ifdef __WXMSW__
	::SetConsoleCtrlHandler(OnConsoleClose, TRUE);   // after the signals: asked first, it passes on what is theirs
#endif
	ListenToCluster();

	// However Run ends — served and stopped, or refused at any step — a console closing waits no longer.
	struct ibStoppedMark { ~ibStoppedMark() { s_state.store(ibServeState::Stopped); } } stoppedMark;

	// A base that did not open has said why already: its own line is the cause the cluster shows.
	if (s_state.load() != ibServeState::Serving || !Open(*base)) {
		ibApplicationInstance::DestroyAppDataEnv();
		return 1;
	}

	// THE PORT THE CLUSTER HANDS THE CLIENTS TO — on this machine alone, the first free one of the servers' own
	// (ibPort::Workers), said to the cluster once it serves.
	std::map<ibString, ibClientHost*> hosts;
	for (const auto& entry : m_clientHosts)
		hosts[entry.first] = entry.second.get();
	unsigned short port = 0;
	ibString refusal;
	if (!m_clientListener.Start(ibPort::Workers, port, std::move(hosts), refusal)) {
		ibAppServerSay(ibJournalMark::Error, ibT("the clients' port did not open: %s - nothing is served"), refusal);
		Stop();
		return 1;
	}
	std::printf("%s %u\n", ibWorkerPipe::Serving, static_cast<unsigned>(port));
	std::fflush(stdout);

	ibAppServerSay(ibJournalMark::Info, ibT("serving '%s' (route %u) for the cluster on 127.0.0.1:%u"), m_workerBase,
		m_route, static_cast<unsigned>(port));

	while (s_state.load() == ibServeState::Serving)
		std::this_thread::sleep_for(std::chrono::milliseconds(250));

	ibAppServerSay(ibJournalMark::Info, ibT("stopping"));
	Stop();
	ibAppServerSay(ibJournalMark::Info, ibT("stopped"));
	return 0;
}

bool ibAppServer::Open(const ibConfiguredInstance& instance)
{
	ibString ibPassword, error;
	if (!m_config.OpenSecret(instance.m_name, ibT("IbPassword"), instance.m_ibPassword, ibPassword, error)) {
		ibAppServerSay(ibJournalMark::Error, ibT("base '%s' is not opened: %s"), instance.m_name, error);
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
	catch (const ibCoreException& err) {
		error = err.GetErrorDescription();
	}
	if (applicationInstance == nullptr) {
		ibAppServerSay(ibJournalMark::Error, ibT("base '%s' did not open: %s"), instance.m_name,
			error.IsEmpty() ? ibString(ibT("no reason given")) : error);
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
	catch (const ibCoreException& err) {
		ibAppServerSay(ibJournalMark::Error, ibT("base '%s': login refused: %s"), instance.m_name,
			err.GetErrorDescription());
		session.Reset();
	}
	if (!session) {
		ibAppServerSay(ibJournalMark::Error, ibT("base '%s': the server could not log in - the base is closed again"),
			instance.m_name);
		ibApplicationInstance::DestroyAppDataEnv(applicationInstance);
		return false;
	}

	// THE BASE'S CLIENTS — the protocol served for it (frmserver), and the base's MCP when its settings switch it
	// on: an assistant reaches the clients through it (client_call), as a client. Whether it is on, and its key, are
	// the base's settings; its port is the config's (McpPort) — the bases of one cluster never meet on one, and of a
	// base's servers the first one holds it.
	m_clientHosts[instance.m_name] = std::make_unique<ibClientHost>(applicationInstance, m_route);
	if (m_route > 1) {
		ibAppServerSay(ibJournalMark::Info, ibT("base '%s': its MCP is the first server's, not this one's (route %u)"),
			instance.m_name, m_route);
	}
	else if (ibMcpServer* const mcp = ibApplicationInstance::GetMcpServer(applicationInstance)) {
		ibString refusal;
		mcp->LoadSettings(session.Get());
		unsigned long port = 0;
		if (instance.m_mcpPort.ToULong(&port) && port != 0 && port <= 65535) {
			ibMcpSettings settings = mcp->GetSettings();
			settings.m_port = static_cast<unsigned short>(port);
			mcp->Configure(settings);
		}
		else {
			refusal = ibString::Format(ibT("McpPort=%s in %s is not a port"), instance.m_mcpPort, m_config.GetConfPath());
		}
		if (refusal.IsEmpty() && mcp->Start(session.Get(), refusal))
			ibAppServerSay(ibJournalMark::Info, ibT("base '%s': MCP at %s"), instance.m_name, mcp->GetEndpoint());
		else
			ibAppServerSay(ibJournalMark::Info, ibT("base '%s': no MCP - %s"), instance.m_name, refusal);
	}

	m_sessions.push_back(std::move(session));
	ibAppServerSay(ibJournalMark::Info, ibT("base '%s' is served: %s"), instance.m_name,
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
