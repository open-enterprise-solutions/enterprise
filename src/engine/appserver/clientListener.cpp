#include "clientListener.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

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

// A socket that has not logged in holds a thread and has not reached the half-hour round. This is how long
// it may live, counted from the moment it opened. A message does not move that mark.
constexpr std::chrono::seconds kUnauthenticatedIdle(15);

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

// How many of the next thread creations fail, the way the operating system refuses one. A test sets it.
// The server never does. Consumed under the queue's lock, so a single failure is a single refused socket.
std::atomic<unsigned> g_failNextThreads{ 0 };

// While this is set, an idle thread does not take a job. A test uses it to hold the pool still between
// two enqueues. The server never sets it.
std::atomic<bool> g_holdIdle{ false };

// ⭐ THE POOL GROWS FOR A BURST, AND THE THREADS THAT BURST ADDED THEN LEAVE.
//
// cpp-httplib's ThreadPool starts a new thread only when its idle count is already zero. A burst that is
// queued while the base threads are still counted idle — they have not taken a job yet — never grows the
// pool. Those jobs then sit there: the threads that exist are inside a WebSocket for its whole life, and
// nothing calls enqueue again to notice the queue. The client is accepted and never answered.
//
// So this queue starts another thread whenever more work is waiting than there are idle threads, up to the
// ceiling. The base threads stay for the life of the listener. A thread above that base waits
// CPPHTTPLIB_THREAD_POOL_IDLE_TIMEOUT, the same few seconds httplib gives its own extra threads, and then
// leaves — so a burst does not pin threads until the process does. It moves itself aside and another thread
// joins it: joining itself, while it still holds this lock, deadlocks.
//
// At the ceiling, with nothing idle, the job is not queued. httplib closes that socket. Queueing it would
// accept a client nobody will read. A thread the operating system will not create is the same answer, and
// the exception stays here: out of the accept loop it ends the process.
class ibClientConnectionQueue : public httplib::TaskQueue {
public:
	ibClientConnectionQueue(std::size_t base, std::size_t ceiling)
		: m_ceiling(std::max<std::size_t>(ceiling, 1))
	{
		const std::size_t start = std::min(std::max<std::size_t>(base, 1), m_ceiling);
		// A throw from here escapes the accept loop and ends the process. Fewer base threads still serve;
		// enqueue grows them, up to the ceiling, or refuses when it cannot.
		for (std::size_t i = 0; i < start; ++i) {
			try {
				Spawn(false);
			}
			catch (const std::system_error&) {
				break;
			}
		}
		// A thread is in the pool before it is idle. A connection in that gap, when the ceiling is the
		// base, looks like a full pool — idle is still zero — and would be refused. Wait until each
		// one is waiting for work.
		std::unique_lock<std::mutex> lock(m_mutex);
		m_ready.wait(lock, [this] { return m_shutdown || m_idle == m_base.size(); });
	}

	~ibClientConnectionQueue() override { shutdown(); }

	bool enqueue(std::function<void()> fn) override
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_shutdown)
				return false;
			Reap();
			const std::size_t alive = m_base.size() + m_extra.size();
			// An idle thread that already has a queued job is not free.
			if (m_jobs.size() >= m_idle && alive >= m_ceiling)
				return false;
			m_jobs.push_back(std::move(fn));
			// One new thread per job the idle ones cannot cover. A burst of N, with B base threads still
			// idle, starts N - B more — so the queue is not left holding connections nobody will read.
			if (m_jobs.size() > m_idle && alive < m_ceiling) {
				try {
					Spawn(true);
				}
				catch (const std::system_error&) {
					m_jobs.pop_back();
					return false;
				}
			}
		}
		m_cond.notify_one();
		return true;
	}

	void shutdown() override
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_shutdown)
				return;
			m_shutdown = true;
		}
		m_cond.notify_all();
		for (std::thread& thread : m_base)
			if (thread.joinable())
				thread.join();
		// The extra threads leave this vector themselves when they time out, and shutdown joins whichever
		// are still in it. Take them out under the lock so that move does not race the join.
		std::vector<std::thread> extra;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			extra.swap(m_extra);
		}
		for (std::thread& thread : extra)
			if (thread.joinable())
				thread.join();
		std::lock_guard<std::mutex> lock(m_mutex);
		Reap();
	}

private:
	void Spawn(bool extra)
	{
		unsigned left = g_failNextThreads.load(std::memory_order_relaxed);
		while (left > 0) {
			if (g_failNextThreads.compare_exchange_weak(left, left - 1, std::memory_order_relaxed))
				throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
		}
		if (extra)
			m_extra.emplace_back([this] { Worker(true); });
		else
			m_base.emplace_back([this] { Worker(false); });
	}

	// Under the lock. A thread that has already returned is joined here, from a thread that is not it.
	void Reap()
	{
		for (std::thread& thread : m_finished)
			if (thread.joinable())
				thread.join();
		m_finished.clear();
	}

	// Under the lock. The caller is the extra thread, which then returns and releases the lock.
	void MoveToFinished()
	{
		const std::thread::id id = std::this_thread::get_id();
		for (auto it = m_extra.begin(); it != m_extra.end(); ++it) {
			if (it->get_id() == id) {
				m_finished.push_back(std::move(*it));
				m_extra.erase(it);
				return;
			}
		}
	}

	void Worker(bool extra)
	{
		for (;;) {
			std::function<void()> fn;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				++m_idle;
				m_ready.notify_all();
				if (extra) {
					const bool work = m_cond.wait_for(lock,
						std::chrono::seconds(CPPHTTPLIB_THREAD_POOL_IDLE_TIMEOUT),
						[this] { return m_shutdown || !m_jobs.empty(); });
					if (!work) {
						--m_idle;
						MoveToFinished();
						return;
					}
				}
				else {
					m_cond.wait(lock, [this] { return m_shutdown || !m_jobs.empty(); });
				}
				// Still counted idle. A test may be parking the pool between two arrivals; the job stays
				// queued until the hold lifts, and shutdown is not a hold.
				while (g_holdIdle.load(std::memory_order_relaxed) && !m_shutdown)
					m_cond.wait_for(lock, std::chrono::milliseconds(50));
				--m_idle;
				if (m_shutdown && m_jobs.empty())
					return;
				if (m_jobs.empty())
					continue;
				fn = std::move(m_jobs.front());
				m_jobs.pop_front();
			}
			fn();
		}
	}

	const std::size_t                 m_ceiling;
	std::mutex                        m_mutex;
	std::condition_variable           m_cond;
	std::condition_variable           m_ready;
	std::deque<std::function<void()>> m_jobs;
	std::vector<std::thread>          m_base;
	std::vector<std::thread>          m_extra;
	std::vector<std::thread>          m_finished;
	std::size_t                       m_idle = 0;
	bool                              m_shutdown = false;
};

} // namespace

void ibClientListenerFailNextThreads(unsigned count)
{
	g_failNextThreads.store(count, std::memory_order_relaxed);
}

void ibClientListenerHoldIdleThreads(bool hold)
{
	g_holdIdle.store(hold, std::memory_order_relaxed);
}

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
	// The handshake keeps cpp-httplib's own read timeout, five seconds. The fifteen seconds before a
	// login belong to the WebSocket loop, counted from the moment the socket opened.

	// STOPGAP until the network core is asynchronous. A connection holds whichever pool thread
	// accepted it for as long as the socket lives. cpp-httplib's own pool grows to
	// 4 * max(8, cores - 1) — 32 threads on an 8-core machine — and the client after that is
	// accepted and never answered; nothing refuses it. ClientConnections (backend.conf) is how
	// many of those threads this process will make. Left out, or 0, that is kDefaultClientConnections
	// (1000), not "no limit". Threads above the base leave once they have been idle. A socket that has
	// not logged in is closed fifteen seconds after it opened, and a message does not extend that.
	const std::size_t connections = ibApplicationHost::Get() != nullptr
		? ibApplicationHost::Get()->GetClientConnections()
		: ibApplicationHost::kDefaultClientConnections;
	const std::size_t limit = std::max<std::size_t>(connections, 1);
	const std::size_t base = std::min(limit, static_cast<std::size_t>(CPPHTTPLIB_THREAD_POOL_COUNT));
	server->m_http.new_task_queue = [base, limit] {
		return new ibClientConnectionQueue(base, limit);
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
		// Timeouts are how a stop is noticed. Until a login, they are also the life of the socket, counted
		// from when it opened: a message does not move that mark. After a login, the host's half-hour round
		// is the idle, and this window no longer applies.
		//
		// The read itself times out every second, so a quiet socket hits the check below. A socket that
		// sends faster than that never sees a timeout, and would keep its thread for as long as it liked
		// if the deadline lived only in the timeout branch. It is therefore asked on every pass, and a
		// text that arrives past it is closed instead of answered.
		const auto opened = std::chrono::steady_clock::now();
		const auto pastLoginDeadline = [&]() {
			return !clientHost->LoggedIn(&ws)
				&& std::chrono::steady_clock::now() - opened >= kUnauthenticatedIdle;
		};
		for (;;) {
			const httplib::ws::ReadResult read = ws.read(message);
			if (read == httplib::ws::Timeout) {
				if (server->m_stopping.load())
					break;
				if (pastLoginDeadline())
					break;
				continue;
			}
			if (read != httplib::ws::Text)
				break;   // closed — or a binary frame, which this protocol does not speak (yet)

			if (pastLoginDeadline())
				break;

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
			ibAppServerSay(ibJournalMark::Info, wxT("client threads: up to %u"),
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
