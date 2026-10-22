#include "appCluster.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <functional>
#include <string>

#include <wx/cmdline.h>

#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::Install — the process's journal and dumps
#include "core/exception.h"                // ibCoreException — the listener refused, with the reason
#include "core/net/webSocketRelay.h"
#include "core/programFolder.h"            // ibBackendConfPath — the certificate the port speaks TLS over
#include "core/server/clusterControl.h"
#include "core/server/listener.h"

#include "clusterService.h"
#include "clusterWorker.h"

wxIMPLEMENT_APP_CONSOLE(ibAppCluster);

namespace {

// How often the servers' pipes are read: what they say comes in lines, and a port said is a client let through.
constexpr int kTimerMs = 200;

// ASKED TO STOP — by Ctrl+C, SIGTERM, the service manager: from a signal or a thread of their own, so only a flag; the
// timer takes it on the main thread, where the cluster stops (ibAppCluster::Stop).
std::atomic<bool> s_stopAsked{ false };

void OnStopSignal(int)
{
	s_stopAsked.store(true);
}

// ONE TRAY'S SOCKET (core/server/clusterControl.h) — what it sends handed on to `received`.
class TraySocket : public ibWebSocketHandler {
public:

	TraySocket(std::shared_ptr<ibWebSocket> socket, std::function<void(const ibString& text)> received) :
		m_socket(std::move(socket)), m_received(std::move(received))
	{
	}

	virtual void OnMessage(const ibString& text) override
	{
		m_received(text);
	}

	virtual void OnBinary(std::string) override
	{
		m_socket->Close(ibWebSocketClose::Unsupported, "the control speaks text");
	}

	virtual void OnClose() override
	{
	}

private:
	std::shared_ptr<ibWebSocket>               m_socket;
	std::function<void(const ibString& text)>  m_received;
};

} // namespace

ibAppCluster::ibAppCluster() = default;

// Out of line: the servers are only declared in the header.
ibAppCluster::~ibAppCluster() = default;

#if wxUSE_CMDLINE_PARSER

void ibAppCluster::OnInitCmdLine(wxCmdLineParser& parser)
{
	parser.AddOption(wxT("d"),  wxT("dir"),       "Server folder: server.conf, server.key and a folder per base (default: server beside the executable)",
		wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("lc"), wxT("locale"),    "The platform's locale every server is started with",
		wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddSwitch(wxT("s"),  wxT("service"),   "Run under the system's service manager (Windows: installed by --install; elsewhere: a systemd unit's ExecStart)");
	parser.AddSwitch(wxT("i"),  wxT("install"),   "Install the cluster of the server folder as a service, started with the system (an administrator's)");
	parser.AddSwitch(wxT("u"),  wxT("uninstall"), "Remove the cluster's service, stopped first (an administrator's)");

	wxAppConsole::OnInitCmdLine(parser);
}

bool ibAppCluster::OnCmdLineParsed(wxCmdLineParser& parser)
{
	wxString folder = ibServerConfig::DefaultFolder(), locale;
	parser.Found(wxT("dir"), &folder);
	parser.Found(wxT("locale"), &locale);
	m_folder = folder;
	m_locale = locale;
	m_mode = parser.Found(wxT("install")) ? ibMode::Install
		: parser.Found(wxT("uninstall")) ? ibMode::Uninstall
		: parser.Found(wxT("service")) ? ibMode::Service
		: ibMode::Console;

	return wxAppConsole::OnCmdLineParsed(parser);
}

#endif

bool ibAppCluster::OnInit()
{
	// THE JOURNAL AND THE DUMPS — the first act, as every application of the platform does.
	ibCrashGuard::Install(wxT("appcluster"));

	if (!wxAppConsole::OnInit())
		return false;

	// What the cluster says, and every warning and error, is what its console shows — a service has none.
	if (m_mode != ibMode::Service)
		ibTechJournal::EchoToStderr();

	ibString error;
	if (m_mode == ibMode::Install || m_mode == ibMode::Uninstall) {
		const bool done = m_mode == ibMode::Install
			? ibClusterService::Install(m_folder, error)
			: ibClusterService::Uninstall(error);
		if (!done)
			ibAppClusterSay(ibJournalMark::Error, wxT("%s"), error);
		else if (m_mode == ibMode::Install)
			ibAppClusterSay(ibJournalMark::Info, wxT("installed as the service '%s', for %s"),
				ibClusterControl::ServiceName, m_folder);
		else
			ibAppClusterSay(ibJournalMark::Info, wxT("the service '%s' removed"), ibClusterControl::ServiceName);
		SetErrorExitCode(done ? 0 : 1);
		return false;
	}

	// Told to stop from here on — by the console, the system, or the service manager.
	std::signal(SIGINT, OnStopSignal);
	std::signal(SIGTERM, OnStopSignal);
#ifdef SIGBREAK
	std::signal(SIGBREAK, OnStopSignal);   // Windows: Ctrl+Break
#endif
	if (m_mode == ibMode::Service && !ibClusterService::Connect([] { OnStopSignal(0); }, error))
		return Refuse(error);

	m_config = std::make_unique<ibServerConfig>(m_folder);
	if (!m_config->HasConf())
		return Refuse(ibString::Format(wxT("no config at %s"), m_config->GetConfPath()));

	// The installation's key made here at the first start, once — each server would otherwise make one of its own.
	if (!m_config->LoadKey(error))
		return Refuse(error);

	// The Ids written before a server reads them: each base's folder is named by its Id.
	std::vector<ibConfiguredInstance> instances = m_config->ReadInstances();
	m_config->AssignIds(instances);
	if (instances.empty())
		return Refuse(ibString::Format(wxT("the config %s names no base"), m_config->GetConfPath()));
	for (const ibConfiguredInstance& instance : instances)
		m_workers.push_back(std::make_unique<ibClusterWorker>(*this, instance.m_name));

	// EACH BASE'S CLIENTS — every socket carried on to the server of its base, on this machine, as it was asked for (the
	// path and its query), in the clear: TLS ends here, at the port. A server not serving — starting, fallen — is not
	// reached: the client is told so and comes again (the communicator logs in anew).
	asio::io_context& io = m_http.GetContext();
	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers) {
		const ibClusterWorker* const held = worker.get();
		const std::string path = "/" + held->GetBase().ToUtf8() + "/client";
		m_http.WebSocket(path, [&io, held](const std::shared_ptr<ibWebSocket>& socket) {
			const ibString base = held->GetBase();
			return std::make_shared<ibWebSocketRelay>(io, "127.0.0.1", held->GetPort(), nullptr,
				socket->GetRequest().GetTarget(), socket, [base](const ibString& reason) {
					ibAppClusterSay(ibJournalMark::Warning, wxT("base '%s': %s"), base, reason);
				});
		});
	}

	// THE TRAYS — from this machine alone, with the token of this start (core/server/clusterControl.h): a tray is told the
	// list as it comes, and again on every change; what it sends is taken on the main thread, where the servers are.
	if (!m_config->WriteClusterToken(m_token, error))
		return Refuse(error);
	m_http.WebSocket(ibClusterControl::Path, [this](const std::shared_ptr<ibWebSocket>& socket)
		-> std::shared_ptr<ibWebSocketHandler> {
		const ibHttpRequest& request = socket->GetRequest();
		const std::string token = request.GetParameter(ibClusterControl::Token);
		if (!ibIsLoopback(request.m_address) || ibString::FromUTF8(token.data(), token.size()) != m_token) {
			ibAppClusterSay(ibJournalMark::Warning, wxT("a control socket from %s refused: not this machine, or not its token"),
				request.m_address);
			return nullptr;
		}
		const std::weak_ptr<ibWebSocket> tray = socket;
		CallAfter([this, tray] {
			if (const std::shared_ptr<ibWebSocket> open = tray.lock()) {
				m_trays.push_back(tray);
				OnWorkerChanged();
			}
		});
		return std::make_shared<TraySocket>(socket, [this](const ibString& text) {
			CallAfter([this, text] { Receive(text); });
		});
	});

	// THE PORT THE CLIENTS COME IN BY — the config's, as the application server takes it alone (appserver,
	// ibClientListener): the one written, or, at the first start, the first free one from the server's own, written back.
	// A written port that is taken is refused, never moved off. Its transport is the listener's (core/server/listener.h):
	// TLS over the certificate backend.conf names, else plain on this machine's own address only.
	unsigned short port = 0;
	if (!m_config->ReadPort(port, error))
		return Refuse(error);
	const bool choose = port == 0;
	if (choose)
		port = ibServerConfig::s_defaultPort;
	ibHttpServerOptions options;
	try {
		options = ibListenerOptions(m_config->ReadHost(), port, ibTlsFiles::FromConf(ibBackendConfPath()));
	}
	catch (const ibCoreException& err) {
		return Refuse(err.GetErrorDescription());
	}
	std::string refused;
	if (!m_http.Start(options, choose ? ibServerConfig::s_choosePorts : 1, refused)) {
		return Refuse(choose
			? ibString::Format(wxT("no free port from %u to %u (%s)"), static_cast<unsigned>(port),
				static_cast<unsigned>(port + ibServerConfig::s_choosePorts - 1), ibString::FromUTF8(refused.data(), refused.size()))
			: ibString::Format(wxT("port %u did not open - another server is probably holding it (%s)"),
				static_cast<unsigned>(port), ibString::FromUTF8(refused.data(), refused.size())));
	}
	m_port = options.m_httpsPort ? m_http.GetHttpsPort() : m_http.GetHttpPort();
	if (choose)
		m_config->WritePort(m_port);

	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers)
		worker->Start();

	m_timer.Bind(wxEVT_TIMER, &ibAppCluster::OnTimer, this);
	m_timer.Start(kTimerMs);

	ibAppClusterSay(ibJournalMark::Info, wxT("serving %s on port %u (%s): %u base(s)"), m_config->GetFolder(),
		static_cast<unsigned>(m_port), options.m_httpsPort ? wxT("oess://, TLS") : wxT("oes://, this machine only"),
		static_cast<unsigned>(m_workers.size()));
	return true;
}

int ibAppCluster::OnExit()
{
	m_timer.Stop();
	// The port first — no client comes in any more, and those carried are let go, the trays among them; the servers
	// then, a server still running stops itself as this process's pipes close.
	m_http.Stop();
	m_trays.clear();
	m_workers.clear();
	ibAppClusterSay(ibJournalMark::Info, wxT("stopped"));

	const int code = wxAppConsole::OnExit();
	ibClusterService::Disconnect(code);
	return code;
}

void ibAppCluster::Stop()
{
	if (m_stopping)
		return;
	m_stopping = true;
	ibAppClusterSay(ibJournalMark::Info, wxT("stopping"));
	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers)
		worker->Stop();
	OnWorkerChanged();
}

void ibAppCluster::OnWorkerChanged()
{
	m_trays.erase(std::remove_if(m_trays.begin(), m_trays.end(),
		[](const std::weak_ptr<ibWebSocket>& tray) { return tray.expired(); }), m_trays.end());
	if (!m_trays.empty()) {
		std::vector<ibClusterServer> servers;
		servers.reserve(m_workers.size());
		for (const std::unique_ptr<ibClusterWorker>& worker : m_workers) {
			ibClusterServer server;
			server.m_base      = worker->GetBase();
			server.m_state     = worker->GetState();
			server.m_pid       = worker->GetPid();
			server.m_falls     = worker->GetFalls();
			server.m_cause     = worker->GetCause();
			servers.push_back(std::move(server));
		}
		const ibString list = ibClusterControl::WriteServers(m_port, servers);
		for (const std::weak_ptr<ibWebSocket>& tray : m_trays) {
			if (const std::shared_ptr<ibWebSocket> open = tray.lock())
				open->Send(list);
		}
	}

	if (!m_stopping)
		return;
	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers) {
		if (worker->GetState() != ibClusterState::Stopped)
			return;
	}
	ExitMainLoop();
}

void ibAppCluster::Receive(const ibString& message)
{
	ibString verb, base;
	if (!ibClusterControl::ReadVerb(message, verb, base)) {
		ibAppClusterSay(ibJournalMark::Warning, wxT("a tray's message is not a verb: %s"), message);
		return;
	}
	if (m_stopping)
		return;   // the servers are going, whatever is asked
	if (verb == ibClusterControl::StopCluster) {
		ibAppClusterSay(ibJournalMark::Info, wxT("told to stop by a tray"));
		Stop();
		return;
	}
	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers) {
		if (worker->GetBase() != base)
			continue;
		if (verb == ibClusterControl::Start)
			worker->Start();
		else if (verb == ibClusterControl::Stop)
			worker->Stop();
		else if (verb == ibClusterControl::Restart)
			worker->Restart();
		else
			ibAppClusterSay(ibJournalMark::Warning, wxT("a tray's verb is not one: %s"), verb);
		return;
	}
	ibAppClusterSay(ibJournalMark::Warning, wxT("a tray named no base of the cluster: '%s'"), base);
}

bool ibAppCluster::Refuse(const ibString& reason)
{
	ibAppClusterSay(ibJournalMark::Error, wxT("%s - nothing is served"), reason);
	SetErrorExitCode(1);
	ibClusterService::Disconnect(1);
	return false;
}

void ibAppCluster::OnTimer(wxTimerEvent&)
{
	if (s_stopAsked.exchange(false))
		Stop();
	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers)
		worker->ReadPipes();
}
