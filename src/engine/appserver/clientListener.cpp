#include "clientListener.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "frmserver/client/clientHost.h"

#include "backend/appHost.h"                 // ClientConnections — how many of these threads
#include "backend/server/serverConfig.h"     // ibAppServerSay — the line a person watching the console sees

// After wx, with the guard already set: wx aliases ssize_t and so does cpp-httplib (backend/mcp/mcpServer.cpp).
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include <cpp-httplib/httplib.h>

#include <wx/mstream.h>
#include <wx/zstream.h>

namespace {

// The largest request a client sends — a frame's answer goes the other way; a request is a method and its
// arguments, a typed text at most.
constexpr std::size_t kMaxRequestBytes = 4 * 1024 * 1024;

// How often a connection waiting for its client looks up to see whether the server is stopping.
constexpr std::chrono::seconds kStopPoll(1);

// The furthest a chosen port goes from where it started looking.
constexpr unsigned int kChooseRange = 100;

// ⭐ THE TRANSPORT DEFLATES, when the client asked for it as it connected (?compress=deflate): an answer of some size
// goes as a BINARY message holding it deflated — raw, RFC 1951, what a browser's DecompressionStream("deflate-raw")
// reads — and a small one as text, as before. The protocol never knows: the same JSON either way. A frame is text
// that repeats itself (names, ids, the same fields on every control) and shrinks several times over.
constexpr std::size_t kDeflateFrom = 1024;

bool SendAnswer(httplib::ws::WebSocket& ws, const std::string& text, bool deflate)
{
	if (!deflate || text.size() < kDeflateFrom)
		return ws.send(text);

	wxMemoryOutputStream packed;
	{
		wxZlibOutputStream zip(packed, wxZ_DEFAULT_COMPRESSION, wxZLIB_NO_HEADER);
		zip.Write(text.data(), text.size());
		if (!zip.Close())
			return ws.send(text);   // not packed — sent as it is
	}
	const wxStreamBuffer* const buffer = packed.GetOutputStreamBuffer();
	return ws.send(static_cast<const char*>(buffer->GetBufferStart()), static_cast<size_t>(packed.GetLength()));
}

// A connection's way out — one send at a time, and none once it closed: a notification from a session's thread may
// come while the connection goes.
struct ibConnectionOut {
	std::mutex mutex;
	bool       open = true;
};

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

	// STOPGAP until the network core is asynchronous. A connection holds whichever pool thread
	// accepted it for as long as the socket lives. cpp-httplib's own pool grows to
	// 4 * max(8, cores - 1) — 32 threads on an 8-core machine — and the client after that is
	// accepted and never answered; nothing refuses it. ClientConnections (backend.conf) is how
	// many of those threads this process will make. Left out, or 0, that is 1000. The threads
	// still exist; only the ceiling moved. An asynchronous core is what retires this.
	const std::size_t connections = ibApplicationHost::Get() != nullptr
		? ibApplicationHost::Get()->GetClientConnections()
		: static_cast<std::size_t>(1000);
	const std::size_t limit = std::max<std::size_t>(connections, 1);
	const std::size_t base = std::min(limit, static_cast<std::size_t>(CPPHTTPLIB_THREAD_POOL_COUNT));
	server->m_http.new_task_queue = [base, limit] {
		return new httplib::ThreadPool(base, limit);
	};

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

		// The connection's wait is bounded, so a stopping server is noticed between messages.
		ws.set_read_timeout(kStopPoll);
		const wxString address = wxString::FromUTF8(req.remote_addr);
		const bool deflate = req.has_param("compress") && req.get_param_value("compress") == "deflate";

		// WHAT THE SERVER SAYS UNASKED goes out on it too — a notification from a session's thread (a client's frame
		// changed by itself), between the answers this thread sends: one send at a time, and none once it closed.
		const std::shared_ptr<ibConnectionOut> out = std::make_shared<ibConnectionOut>();
		clientHost->SetNotifier(&ws, [&ws, out, deflate](const wxString& text) {
			std::lock_guard<std::mutex> lock(out->mutex);
			if (out->open)
				SendAnswer(ws, std::string(text.utf8_str()), deflate);
		});

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
			if (response.IsEmpty())
				continue;
			std::lock_guard<std::mutex> lock(out->mutex);
			if (!SendAnswer(ws, std::string(response.utf8_str()), deflate))
				break;
		}

		// THE CLIENTS LOGGED IN THROUGH IT GO WITH IT — a closed tab is a client gone, not one idle for
		// half an hour; and the connection is told nothing more: closed here, before the socket goes, for a
		// notification already on its way.
		{
			std::lock_guard<std::mutex> lock(out->mutex);
			out->open = false;
		}
		clientHost->Disconnect(&ws);
	});

	const std::string address = host.IsEmpty() ? std::string("0.0.0.0") : std::string(host.utf8_str());
	const unsigned int first = port;
	const unsigned int last = choose ? std::min(first + kChooseRange, 65535u) : first;
	for (unsigned int candidate = first; candidate <= last; ++candidate) {
		if (server->m_http.bind_to_port(address, static_cast<int>(candidate))) {
			port = static_cast<unsigned short>(candidate);
			ibAppServerSay(ibJournalMark::Info, wxT("accepting %u client connections"),
				static_cast<unsigned>(connections));
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
