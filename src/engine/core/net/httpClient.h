#ifndef __CORE_NET_HTTP_CLIENT_H__
#define __CORE_NET_HTTP_CLIENT_H__

// THE ENGINE'S CONNECTIONS OUT — a WebSocket to a server, and a POST. The web server's to an application server, over
// its port: a WebSocket for each browser's, a POST for each browser's POST. A thin client's to its application server
// (protocol/connectionServer.h). The designer's debugger to a runtime's debug port (backend/debugger/debugClient.h).
// In the clear: the application server is on its own network, and speaks no TLS yet. Asynchronous, on whichever I/O
// threads the user runs — the HTTP server's, or its own (ioThreads.h): a thousand sockets here are no thread each.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/core.h"
#include "core/net/webSocketFrame.h"   // ibWebSocketOpcode — what a message's opcode is compared with

namespace asio { class io_context; }

// The socket and what it holds — Asio's, kept in the core (httpClient.cpp).
struct ibWebSocketClientState;

// A WEBSOCKET TO A SERVER — opened at once and said to when it has opened; what is sent before that waits for it.
// Every call is safe from any thread; the handlers are called on the socket's strand.
class CORE_API ibWebSocketClient {
public:

	// The socket is open — the server's answer to the opening checked; once, before any message.
	using ibOpened = std::function<void()>;
	// A message, whole — its opcode (ibWebSocketOpcode::Text or ::Binary) and its payload.
	using ibMessage = std::function<void(std::uint8_t opcode, std::string payload)>;
	// The socket is gone — refused, closed by either side, or dropped; once. `reason` — why, for the journal.
	using ibClosed = std::function<void(const std::string& reason)>;

	ibWebSocketClient(asio::io_context& io, ibOpened opened, ibMessage message, ibClosed closed);
	// Closes it, as Close: a client let go holds no socket open. `closed` is still told, on the strand — so neither
	// handler holds what dies with the client.
	~ibWebSocketClient();

	// `target` — the path and its query, as written on the request line.
	void Open(const std::string& host, std::uint16_t port, const std::string& target);
	// A message, queued and sent in order — as ibWebSocket's.
	void Send(std::string text);
	void SendBinary(std::string data);
	void Close();

private:

	std::shared_ptr<ibWebSocketClientState> m_state;
};

// ONE POST to host:port at `target` — answered to `done` on an I/O thread, once: the status (0 — none came, and `error`
// says why) and the body. `wait` — how long the answer may take.
CORE_API void ibHttpPost(asio::io_context& io, const std::string& host, std::uint16_t port, const std::string& target,
	std::string body, const std::string& type, std::chrono::seconds wait,
	std::function<void(int status, std::string body, const std::string& error)> done);

#endif
