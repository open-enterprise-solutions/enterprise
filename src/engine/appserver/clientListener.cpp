#include "clientListener.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>

#include "sfrontend/client/clientHost.h"

// After wx, with the guard already set: wx aliases ssize_t and so does cpp-httplib (backend/mcp/mcpServer.cpp).
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include "httplib.h"

namespace {

// The largest request a client sends — a frame's answer goes the other way; a request is a method and its
// arguments, a typed text at most.
constexpr std::size_t kMaxRequestBytes = 4 * 1024 * 1024;

// How often a connection waiting for its client looks up to see whether the server is stopping.
constexpr std::chrono::seconds kStopPoll(1);

// The furthest a chosen port goes from where it started looking.
constexpr unsigned int kChooseRange = 100;

} // namespace

class ibClientListener::ibServer {
public:

	httplib::Server                   m_http;
	std::map<wxString, ibClientHost*> m_hosts;
	std::atomic<bool>                 m_stopping{ false };

	// The base the path names; null — none served by that name.
	ibClientHost* FindHost(const httplib::Request& req) const
	{
		const auto base = req.path_params.find("base");
		if (base == req.path_params.end())
			return nullptr;
		const auto host = m_hosts.find(wxString::FromUTF8(base->second));
		return host != m_hosts.end() ? host->second : nullptr;
	}
};

ibClientListener::ibClientListener() = default;

ibClientListener::~ibClientListener()
{
	Stop();
}

bool ibClientListener::Start(const wxString& host, unsigned short& port, bool choose,
	std::map<wxString, ibClientHost*> hosts, wxString& refusal)
{
	m_server = std::make_unique<ibServer>();
	ibServer* const server = m_server.get();
	server->m_hosts = std::move(hosts);

	server->m_http.set_payload_max_length(kMaxRequestBytes);
	server->m_http.set_tcp_nodelay(true);

	// 🛑 ONE LISTENER PER PORT, SAID TO THE OPERATING SYSTEM — on Windows httplib's SO_REUSEADDR lets a second
	// process bind a port another one listens on, and which of them a connection reaches is undefined. Two
	// application servers on one machine would both "start" on 7373. The MCP listener's cure, the same lines
	// (backend/mcp/mcpServer.cpp, Bind).
#ifdef _WIN32
	server->m_http.set_socket_options([](auto sock) {
		const int yes = 1;
		::setsockopt((SOCKET)sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			reinterpret_cast<const char*>(&yes), sizeof(yes));
	});
#endif

	server->m_http.Post("/:base/client", [server](const httplib::Request& req, httplib::Response& res) {
		ibClientHost* const clientHost = server->FindHost(req);
		if (clientHost == nullptr) {
			res.status = httplib::StatusCode::NotFound_404;
			return;
		}
		const wxString response = clientHost->Call(wxString::FromUTF8(req.body), nullptr,
			wxString::FromUTF8(req.remote_addr));
		if (response.IsEmpty())
			res.status = httplib::StatusCode::NoContent_204;   // a notification — no answer wanted
		else
			res.set_content(std::string(response.utf8_str()), "application/json");
	});

	server->m_http.WebSocket("/:base/client", [server](const httplib::Request& req, httplib::ws::WebSocket& ws) {
		ibClientHost* const clientHost = server->FindHost(req);
		if (clientHost == nullptr) {
			ws.close(httplib::ws::CloseStatus::PolicyViolation, "no such base");
			return;
		}

		// The connection's own wait is bounded, so a stopping server is noticed between messages.
		ws.set_read_timeout(kStopPoll);
		const wxString address = wxString::FromUTF8(req.remote_addr);
		std::string message;
		for (;;) {
			const httplib::ws::ReadResult read = ws.read(message);
			if (read == httplib::ws::Timeout) {
				if (server->m_stopping.load())
					break;
				continue;
			}
			if (read != httplib::ws::Text)
				break;   // closed — or a binary frame, which this protocol does not speak (yet)

			const wxString response = clientHost->Call(wxString::FromUTF8(message), &ws, address);
			if (!response.IsEmpty() && !ws.send(std::string(response.utf8_str())))
				break;
		}

		// THE CLIENTS LOGGED IN THROUGH IT GO WITH IT — a closed tab is a client gone, not one idle for
		// half an hour.
		clientHost->Disconnect(&ws);
	});

	const std::string address = host.IsEmpty() ? std::string("0.0.0.0") : std::string(host.utf8_str());
	const unsigned int first = port;
	const unsigned int last = choose ? std::min(first + kChooseRange, 65535u) : first;
	for (unsigned int candidate = first; candidate <= last; ++candidate) {
		if (server->m_http.bind_to_port(address, static_cast<int>(candidate))) {
			port = static_cast<unsigned short>(candidate);
			m_thread = std::thread([server]() { server->m_http.listen_after_bind(); });
			return true;
		}
	}

	refusal = choose
		? wxString::Format(wxT("no free port from %u to %u on %s"), first, last, wxString::FromUTF8(address))
		: wxString::Format(wxT("port %u on %s is taken - another application server is probably holding it"),
			first, wxString::FromUTF8(address));
	m_server.reset();
	return false;
}

void ibClientListener::Stop()
{
	if (m_server == nullptr)
		return;

	// The connections first: each notices between messages and lets its clients go; then the port closes, and
	// listen_after_bind returns once the last connection has.
	m_server->m_stopping.store(true);
	m_server->m_http.stop();
	if (m_thread.joinable())
		m_thread.join();
	m_server.reset();
}
