#ifndef __WEBSERVER_HTTP_CLIENT_H__
#define __WEBSERVER_HTTP_CLIENT_H__

// THE WEB SERVER'S OWN CONNECTIONS — to an application server, over its port: a WebSocket for each browser's, a POST for
// each browser's POST. In the clear, as a thin client's (protocol/connectionServer.h): the application server is on
// its own network, and speaks no TLS yet. Asynchronous, on the HTTP server's I/O threads — a thousand browsers are a
// thousand sockets here, and no thread each.

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include "webSocketFrame.h"

// A WEBSOCKET TO A SERVER — opened at once and said to when it has opened; what is sent before that waits for it.
// Every call is safe from any thread; the handlers are called on the socket's strand.
class ibWebSocketClient : public std::enable_shared_from_this<ibWebSocketClient> {
public:

	// A message, whole — its opcode (ibWebSocketOpcode::Text or ::Binary) and its payload.
	using ibMessage = std::function<void(std::uint8_t opcode, std::string payload)>;
	// The socket is gone — refused, closed by either side, or dropped; once. `reason` — why, for the journal.
	using ibClosed = std::function<void(const std::string& reason)>;

	ibWebSocketClient(asio::io_context& io, ibMessage message, ibClosed closed);

	// `target` — the path and its query, as written on the request line.
	void Open(const std::string& host, std::uint16_t port, const std::string& target);
	void Send(std::string text);
	void Close();

private:

	void Connected();
	void ReadHead();
	void Opened(std::string leftover);
	void Read();
	void OnFrames(const char* data, std::size_t size);
	void SendFrame(std::uint8_t opcode, std::string payload);
	void Queue(std::string bytes);
	void Pump();
	void Drop(const std::string& reason);

	asio::ip::tcp::socket   m_socket;   // on a strand of its own — the resolver and the timer go by its executor
	asio::ip::tcp::resolver m_resolver;
	asio::steady_timer      m_timer;    // the opening's deadline
	ibMessage               m_message;
	ibClosed                m_closed;
	std::string             m_host;
	std::uint16_t           m_port = 0;
	std::string             m_target;
	std::string             m_key;

	bool                    m_open = false;
	bool                    m_gone = false;
	bool                    m_closeSent = false;
	bool                    m_closeWhenWritten = false;
	std::vector<std::string> m_waiting;   // sent before it opened
	std::string             m_head;       // the response's head, as it comes
	std::deque<std::string> m_out;
	std::size_t             m_outBytes = 0;
	bool                    m_writing = false;
	ibWebSocketFrameReader  m_frames;
	std::string             m_assembled;
	std::uint8_t            m_assembledOpcode = ibWebSocketOpcode::Text;
	bool                    m_assembling = false;
	char                    m_buffer[16 * 1024];
};

// ONE POST to host:port at `target` — answered to `done` on an I/O thread, once: the status (0 — none came, and `error`
// says why) and the body. `wait` — how long the answer may take.
void ibHttpPost(asio::io_context& io, const std::string& host, std::uint16_t port, const std::string& target,
	std::string body, const std::string& type, std::chrono::seconds wait,
	std::function<void(int status, std::string body, const std::string& error)> done);

#endif
