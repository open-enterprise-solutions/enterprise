#include "appCluster.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <functional>
#include <string>

#include <wx/cmdline.h>

#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::Install — the process's journal and dumps
#include "core/exception.h"                // ibCoreException — the listener refused, with the reason
#include "core/fileSystem.h"               // ibBackendConfPath — the certificate the port speaks TLS over
#include "core/server/clusterControl.h"
#include "core/server/listener.h"

#include "clusterRoute.h"
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
	// wxCmdLineParser's own: it writes what it found into wx strings.
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
	ibCrashGuard::Install(ibT("appcluster"));

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
			ibAppClusterSay(ibJournalMark::Error, ibT("%s"), error);
		else if (m_mode == ibMode::Install)
			ibAppClusterSay(ibJournalMark::Info, ibT("installed as the service '%s', for %s"),
				ibClusterControl::ServiceName, m_folder);
		else
			ibAppClusterSay(ibJournalMark::Info, ibT("the service '%s' removed"), ibClusterControl::ServiceName);
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
		return Refuse(ibString::Format(ibT("no config at %s"), m_config->GetConfPath()));

	// The installation's key made here at the first start, once — each server would otherwise make one of its own.
	if (!m_config->LoadKey(error))
		return Refuse(error);

	// The Ids written before a server reads them: each base's folder is named by its Id. And each base's assistant port,
	// here, once: the servers start side by side and only read.
	std::vector<ibConfiguredInstance> instances = m_config->ReadInstances();
	m_config->AssignIds(instances);
	m_config->AssignMcpPorts(instances);
	if (instances.empty())
		return Refuse(ibString::Format(ibT("the config %s names no base"), m_config->GetConfPath()));

	// EACH BASE'S SERVERS, as many as its Processes, and its clients — every socket carried on to one of them as they are
	// when it opens (ibClusterRoute), on this machine, as it was asked for (the path and its query), in the clear: TLS
	// ends here, at the port. A server not serving — starting, fallen — is not reached: the client is told so and comes
	// again (the communicator logs in anew).
	asio::io_context& io = m_http.GetContext();
	for (const ibConfiguredInstance& instance : instances) {
		for (unsigned route = 1; route <= instance.m_processes; ++route)
			m_workers.push_back(std::make_unique<ibClusterWorker>(*this, ibClusterServer{ instance.m_name, route }));
		const ibString base = instance.m_name;
		m_http.WebSocket("/" + base.ToUtf8() + "/client", [this, &io, base](const std::shared_ptr<ibWebSocket>& socket) {
			return std::make_shared<ibClusterRoute>(io, WorkersOf(base), socket);
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
			ibAppClusterSay(ibJournalMark::Warning, ibT("a control socket from %s refused: not this machine, or not its token"),
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

	// THE PORT THE CLIENTS COME IN BY — the config's: the one written, or the coordinator's own (ibPort::Clients), written
	// back at the first start. A port that is taken is refused, never moved off: the clients look for the cluster there.
	// Its transport is the listener's (core/server/listener.h): TLS over the certificate backend.conf names, else plain
	// on this machine's own address only.
	unsigned short port = 0;
	if (!m_config->ReadPort(port, error))
		return Refuse(error);
	const bool written = port != 0;
	if (!written)
		port = ibPort::Clients.m_first;
	ibHttpServerOptions options;
	try {
		options = ibListenerOptions(m_config->ReadHost(), port, ibTlsFiles::FromConf(ibBackendConfPath()));
	}
	catch (const ibCoreException& err) {
		return Refuse(err.GetErrorDescription());
	}
	std::string refused;
	if (!m_http.Start(options, 1, refused)) {
		return Refuse(ibString::Format(ibT("port %u did not open - another cluster is probably holding it; write another "
			"Port in %s (%s)"), static_cast<unsigned>(port), m_config->GetConfPath(),
			ibString::FromUTF8(refused.data(), refused.size())));
	}
	m_port = options.m_httpsPort ? m_http.GetHttpsPort() : m_http.GetHttpPort();
	if (!written)
		m_config->WritePort(m_port);

	// Each base's first server; its others once it serves (OnWorkerChanged) — what a base's first opening makes, it makes
	// alone.
	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers) {
		if (worker->GetServer().m_route == 1)
			worker->Start();
		else
			m_held.push_back(worker.get());
	}

	m_timer.Bind(wxEVT_TIMER, &ibAppCluster::OnTimer, this);
	m_timer.Start(kTimerMs);

	ibAppClusterSay(ibJournalMark::Info, ibT("serving %s on port %u (%s): %u base(s), %u server(s)"),
		m_config->GetFolder(), static_cast<unsigned>(m_port),
		options.m_httpsPort ? ibT("oess://, TLS") : ibT("oes://, this machine only"),
		static_cast<unsigned>(instances.size()), static_cast<unsigned>(m_workers.size()));
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
	m_retired.clear();
	// The token goes with the cluster: the next one writes its own, and a tray finds none in between.
	ibString error;
	if (!m_config->RemoveClusterToken(error))
		ibAppClusterSay(ibJournalMark::Warning, ibT("%s"), error);
	ibAppClusterSay(ibJournalMark::Info, ibT("stopped"));

	const int code = wxAppConsole::OnExit();
	ibClusterService::Disconnect(code);
	return code;
}

void ibAppCluster::Stop()
{
	if (m_stopping)
		return;
	m_stopping = true;
	ibAppClusterSay(ibJournalMark::Info, ibT("stopping"));
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
		for (const std::unique_ptr<ibClusterWorker>& worker : m_workers)
			servers.push_back(worker->GetServer());
		const ibString list = ibClusterControl::WriteServers(m_port, servers);
		for (const std::weak_ptr<ibWebSocket>& tray : m_trays) {
			if (const std::shared_ptr<ibWebSocket> open = tray.lock())
				open->Send(list);
		}
	}

	if (!m_stopping) {
		// The servers held till their base's first serves — taken out of the hold before they are started: a start tells
		// this again.
		std::vector<ibClusterWorker*> ready;
		m_held.erase(std::remove_if(m_held.begin(), m_held.end(), [this, &ready](ibClusterWorker* held) {
			const bool first = std::any_of(m_workers.begin(), m_workers.end(),
				[held](const std::unique_ptr<ibClusterWorker>& worker) {
					const ibClusterServer& server = worker->GetServer();
					return server.m_base == held->GetServer().m_base && server.m_route == 1
						&& server.m_state == ibClusterState::Serving;
				});
			if (first)
				ready.push_back(held);
			return first;
		}), m_held.end());
		for (ibClusterWorker* const worker : ready)
			worker->Start();
		return;
	}
	for (const std::vector<std::unique_ptr<ibClusterWorker>>* list : { &m_workers, &m_retired }) {
		for (const std::unique_ptr<ibClusterWorker>& worker : *list) {
			if (worker->GetServer().m_state != ibClusterState::Stopped)
				return;
		}
	}
	ExitMainLoop();
}

void ibAppCluster::Receive(const ibString& message)
{
	ibString verb;
	ibClusterServer named;
	if (!ibClusterControl::ReadVerb(message, verb, named)) {
		ibAppClusterSay(ibJournalMark::Warning, ibT("a tray's message is not a verb: %s"), message);
		return;
	}
	if (m_stopping)
		return;   // the servers are going, whatever is asked
	if (verb == ibClusterControl::StopCluster) {
		ibAppClusterSay(ibJournalMark::Info, ibT("told to stop by a tray"));
		Stop();
		return;
	}
	if (verb != ibClusterControl::Start && verb != ibClusterControl::Stop && verb != ibClusterControl::Restart) {
		ibAppClusterSay(ibJournalMark::Warning, ibT("a tray's verb is not one: %s"), verb);
		return;
	}
	// Started or restarted, its base's servers are counted again first: server.conf may say another Processes now.
	if (verb != ibClusterControl::Stop)
		ApplyProcesses(named.m_base);

	// The server named, by its base and route. Held, it is held no more: the tray decides it.
	const auto found = std::find_if(m_workers.begin(), m_workers.end(),
		[&named](const std::unique_ptr<ibClusterWorker>& worker) {
			return worker->GetServer().m_base == named.m_base && worker->GetServer().m_route == named.m_route;
		});
	if (found == m_workers.end()) {
		ibAppClusterSay(ibJournalMark::Warning, ibT("a tray named no server of the cluster: '%s' #%u"), named.m_base,
			named.m_route);
		return;
	}
	ibClusterWorker* const worker = found->get();
	m_held.erase(std::remove(m_held.begin(), m_held.end(), worker), m_held.end());
	if (verb == ibClusterControl::Start)
		worker->Start();
	else if (verb == ibClusterControl::Stop)
		worker->Stop();
	else
		worker->Restart();
}

void ibAppCluster::ApplyProcesses(const ibString& base)
{
	const std::vector<ibConfiguredInstance> instances = m_config->ReadInstances();
	const auto instance = std::find_if(instances.begin(), instances.end(),
		[&base](const ibConfiguredInstance& one) { return one.m_name == base; });
	if (instance == instances.end())
		return;   // gone from the config: its servers stay as they are until the cluster starts again
	const unsigned processes = instance->m_processes;
	const auto ofBase = [&base](const std::unique_ptr<ibClusterWorker>& worker) {
		return worker->GetServer().m_base == base;
	};
	// A base the cluster did not start with has no path on the port: it comes with the cluster's next start.
	const unsigned count = static_cast<unsigned>(std::count_if(m_workers.begin(), m_workers.end(), ofBase));
	if (count == 0 || processes == count)
		return;
	ibAppClusterSay(ibJournalMark::Info, ibT("base '%s': %u server(s), %u before (Processes in %s)"), base, processes,
		count, m_config->GetConfPath());

	if (processes > count) {
		// After the base's last one, the list kept in order; held, as at the start, till the base's first serves.
		std::lock_guard<std::mutex> lock(m_workersMutex);
		auto at = std::find_if(m_workers.rbegin(), m_workers.rend(), ofBase).base();
		for (unsigned route = count + 1; route <= processes; ++route) {
			at = m_workers.insert(at, std::make_unique<ibClusterWorker>(*this, ibClusterServer{ base, route }));
			m_held.push_back(at->get());
			++at;
		}
	}
	else {
		// The last ones out of the list, then stopped — their sessions go with them, as with a tray's stop.
		std::vector<std::unique_ptr<ibClusterWorker>> taken;
		{
			std::lock_guard<std::mutex> lock(m_workersMutex);
			for (auto worker = m_workers.begin(); worker != m_workers.end();) {
				if (ofBase(*worker) && (*worker)->GetServer().m_route > processes) {
					taken.push_back(std::move(*worker));
					worker = m_workers.erase(worker);
				}
				else
					++worker;
			}
		}
		for (std::unique_ptr<ibClusterWorker>& worker : taken) {
			m_held.erase(std::remove(m_held.begin(), m_held.end(), worker.get()), m_held.end());
			m_retired.push_back(std::move(worker));
			m_retired.back()->Stop();
		}
	}
	OnWorkerChanged();
}

std::vector<ibClusterWorker*> ibAppCluster::WorkersOf(const ibString& base)
{
	std::lock_guard<std::mutex> lock(m_workersMutex);
	std::vector<ibClusterWorker*> workers;
	for (const std::unique_ptr<ibClusterWorker>& worker : m_workers) {
		if (worker->GetServer().m_base == base)
			workers.push_back(worker.get());
	}
	return workers;
}

bool ibAppCluster::Refuse(const ibString& reason)
{
	ibAppClusterSay(ibJournalMark::Error, ibT("%s - nothing is served"), reason);
	SetErrorExitCode(1);
	ibClusterService::Disconnect(1);
	return false;
}

void ibAppCluster::OnTimer(wxTimerEvent&)
{
	if (s_stopAsked.exchange(false))
		Stop();
	// The retired ones too, while they go: a pipe nobody reads fills, and the server writing to it waits.
	for (const std::vector<std::unique_ptr<ibClusterWorker>>* list : { &m_workers, &m_retired }) {
		for (const std::unique_ptr<ibClusterWorker>& worker : *list)
			worker->ReadPipes();
	}
}
