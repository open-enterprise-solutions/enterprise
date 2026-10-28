#include "clientListener.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/exception.h"                   // ibCoreException — the listener refused, with the reason
#include "core/net/webSocketFrame.h"         // ibWebSocketClose — what a socket that did not log in is told
#include "core/server/listener.h"            // ibListenerOptions — the one door every listener opens through

#include "frmserver/client/clientHost.h"

#include "backend/appHost.h"                 // ClientConnections — how many connections at once
#include "core/server/serverConfig.h"     // ibAppServerSay — the line a person watching the console sees

namespace {

// ⭐ A SOCKET THAT HAS NOT LOGGED IN GOES FIFTEEN SECONDS AFTER IT OPENED — counted from the open: a message does not
// move that mark. A connection costs a buffer here, not a thread, but the ceiling (ClientConnections) is counted in
// connections, and sockets that never log in would hold every place. Once logged in, the host's half-hour round is
// the idle, and this no longer applies.
constexpr std::chrono::seconds kLoginWithin(15);

// How often the round looks for a socket past that mark.
constexpr std::chrono::seconds kLoginRoundEvery(1);

// A CLIENT'S CONNECTION TO A BASE THIS SERVER HOLDS — the connection its clients log in through, told what the base
// says to them unasked, and they leave with it. Packets both ways (OES-RPC): the socket hands them over as they are
// (and deflates one of some size, when the client asked).
class ibHostRelay : public ibWebSocketHandler {
public:

	ibHostRelay(ibClientHost& host, std::shared_ptr<ibWebSocket> socket) :
		m_host(host), m_socket(std::move(socket))
	{
		// WHAT THE SERVER SAYS UNASKED goes out on it too — a notification from a session's thread (a client's frame
		// changed by itself), between the answers. The socket queues both in order, and once it closed sends nothing.
		const std::weak_ptr<ibWebSocket> told = m_socket;
		m_host.SetNotifier(this, [told](const ibMemoryBuffer& packet) {
			if (const std::shared_ptr<ibWebSocket> socket = told.lock())
				socket->Send(packet.GetData(), packet.GetDataLen(), ibWebSocketSend::Deflate);
		});
	}

	virtual void OnMessage(const ibString&) override
	{
		m_socket->Close(ibWebSocketClose::Unsupported, "the protocol speaks packets");
	}

	virtual void OnBinary(std::string data) override
	{
		const ibMemoryBuffer packet(data.data(), data.size());
		const ibMemoryBuffer answer = m_host.Call(packet, this, m_socket->GetRequest().m_address);
		if (!answer.IsEmpty())
			m_socket->Send(answer.GetData(), answer.GetDataLen(), ibWebSocketSend::Deflate);
	}

	// THE CLIENTS LOGGED IN THROUGH IT GO WITH IT — a closed tab is a client gone, not one idle for half an hour.
	virtual void OnClose() override
	{
		m_host.Disconnect(this);
	}

	ibClientHost& GetHost() const { return m_host; }
	ibWebSocket&  GetSocket() const { return *m_socket; }

private:
	ibClientHost&                m_host;
	std::shared_ptr<ibWebSocket> m_socket;
};

} // namespace

// THE FIFTEEN SECONDS BEFORE A LOGIN, kept by one thread for the whole listener: a socket waiting for its client has
// no thread of its own to keep a deadline on.
class ibClientListener::ibLoginRound {
public:

	ibLoginRound() : m_thread([this]() { Run(); }) {}

	~ibLoginRound()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_wake.notify_all();
		m_thread.join();
	}

	// A socket just opened, its mark counted from now.
	void Watch(const std::shared_ptr<ibHostRelay>& relay)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_watched.push_back(ibWatched{ relay, std::chrono::steady_clock::now() + kLoginWithin });
	}

private:

	struct ibWatched {
		std::weak_ptr<ibHostRelay>            m_relay;      // gone — the socket closed by itself
		std::chrono::steady_clock::time_point m_deadline;
	};

	void Run()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		while (!m_wake.wait_for(lock, kLoginRoundEvery, [this]() { return m_stopping; })) {
			// The marks in the order the sockets opened, so the round stops at the first one still ahead.
			const auto now = std::chrono::steady_clock::now();
			auto past = m_watched.begin();
			for (; past != m_watched.end() && past->m_deadline <= now; ++past) {
				const std::shared_ptr<ibHostRelay> relay = past->m_relay.lock();
				if (relay != nullptr && !relay->GetHost().LoggedIn(relay.get()))
					relay->GetSocket().Close(ibWebSocketClose::Policy, "not logged in");
			}
			m_watched.erase(m_watched.begin(), past);
		}
	}

	std::mutex              m_mutex;
	std::condition_variable m_wake;
	bool                    m_stopping = false;
	std::vector<ibWatched>  m_watched;
	std::thread             m_thread;   // last: it starts on the members above
};

ibClientListener::ibClientListener() = default;

ibClientListener::~ibClientListener()
{
	Stop();
}

bool ibClientListener::Start(const ibPortRange& ports, unsigned short& port, std::map<ibString, ibClientHost*> hosts,
	ibString& refusal)
{
	ibHttpServerOptions options;
	try {
		options = ibListenerOptions(ibT("127.0.0.1"), ports.m_first, ibTlsFiles());
	}
	catch (const ibCoreException& err) {
		refusal = err.GetErrorDescription();
		return false;
	}

	m_server = std::make_unique<ibHttpServer>();
	m_loginRound = std::make_unique<ibLoginRound>();
	ibLoginRound* const round = m_loginRound.get();

	for (const auto& entry : hosts) {
		ibClientHost* const clientHost = entry.second;
		const std::string path = "/" + std::string(entry.first.utf8_str()) + "/client";

		m_server->Post(path, [clientHost](const ibHttpRequest& request, ibHttpResponder respond) {
			const ibString response = clientHost->Call(request.GetText(), nullptr, request.m_address);
			ibHttpResponse answer;
			if (response.IsEmpty())
				answer.m_status = 204;   // a notification — no answer wanted
			else
				answer.SetText(response, "application/json");
			respond(std::move(answer));
		});

		m_server->WebSocket(path, [clientHost, round](const std::shared_ptr<ibWebSocket>& socket) {
			const std::shared_ptr<ibHostRelay> relay = std::make_shared<ibHostRelay>(*clientHost, socket);
			round->Watch(relay);
			return relay;
		});
	}

	// ClientConnections (backend.conf) is how many this process holds at once. Left out, or 0, that is
	// kDefaultClientConnections (1000), not "no limit".
	const std::size_t connections = ibApplicationHost::Get() != nullptr
		? ibApplicationHost::Get()->GetClientConnections()
		: ibApplicationHost::kDefaultClientConnections;

	options.m_maxConnections = std::max<std::size_t>(connections, 1);

	std::string error;
	if (m_server->Start(options, ports.m_count, error)) {
		port = m_server->GetHttpPort();
		ibAppServerSay(ibJournalMark::Info, ibT("client connections: up to %u"),
			static_cast<unsigned>(options.m_maxConnections));
		return true;
	}

	refusal = ibString::Format(ibT("no free port from %u to %u on 127.0.0.1 (%s)"), static_cast<unsigned>(ports.m_first),
		static_cast<unsigned>(ports.Last()), ibString::FromUTF8(error));
	m_server.reset();
	m_loginRound.reset();
	return false;
}

void ibClientListener::Stop()
{
	if (m_server == nullptr)
		return;

	// The port closes and every connection goes — each one's clients let go (OnClose) before Stop returns; the round
	// after them, since an opening socket hands itself to it.
	m_server->Stop();
	m_server.reset();
	m_loginRound.reset();
}
