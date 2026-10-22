#ifndef __CORE_NET_HTTP_CLIENT_H__
#define __CORE_NET_HTTP_CLIENT_H__

// THE ENGINE'S CONNECTIONS OUT — a WebSocket to a server, and a POST. The web server's to an application server, over
// its port: a WebSocket for each browser's, a POST for each browser's POST. A thin client's to its application server
// (protocol/connectionServer.h). The designer's debugger to a runtime's debug port (backend/debugger/debugClient.h).
// Over TLS when the caller hands them a client's TLS (ibTlsClientContext) — an application server's address that says
// oess:// — in the clear otherwise. Asynchronous, on whichever I/O threads the user runs — the HTTP server's, or its
// own (ioThreads.h): a thousand sockets here are no thread each.
//
// …and a SCRIPT'S HTTP AND HTTPS (ibHttpClient, backend/system/value/valueHttp.h): synchronous, since a script waits for
// its answer, and over TLS when it is asked to be.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/core.h"
#include "core/net/httpServer.h"       // ibHttpRequest, ibHttpResponse — what a client sends and is answered, too
#include "core/net/webSocketFrame.h"   // ibWebSocketOpcode — what a message's opcode is compared with

namespace asio { class io_context; }

// WHOM A CLIENT TRUSTS AND WHAT IT SHOWS — Mbed TLS's, kept in the core (tlsStream.h): held by whoever opens the
// connections, made once and shared by all of them.
class ibTlsContext;

// WHY A REQUEST WAS NOT ANSWERED — or a secure client not prepared: what a caller tells a person by, each its own
// sentence.
enum class ibHttpClientError {
	None,
	Trusted,        // the trusted certificates are not readable, or the platform's store gave none
	Certificate,    // the client's certificate or its key is not readable, or the password does not open the key
	Connection,     // the name does not resolve, or nothing listens on the port
	Handshake,      // the server on the port does not speak TLS, or the handshake failed
	Verification,   // the server's certificate is not one the client trusts
	Hostname,       // …or is another host's
	Write,          // the connection was lost before the request was whole
	Read,           // …before the answer was whole
	Answer,         // what came back is not HTTP, or is larger than the client holds (kMaxAnswerBytes)
	Timeout,        // nobody answered within the time allowed
};

// A CLIENT'S TLS into `context` — in ibTlsContext::LoadClient's words: `trusted` PEM text, empty — the platform's store;
// `certificate` PEM text of the client's certificate and its key, the key opened with `password`; `verify` — the
// server's chain and its name checked. ::Trusted or ::Certificate, with why in `error`, when they are not readable.
// Made once for every connection that speaks it: the platform's store is read here, not for each socket.
CORE_API ibHttpClientError ibTlsClientContext(const std::string& trusted, const std::string& certificate,
	const std::string& password, bool verify, std::shared_ptr<const ibTlsContext>& context, std::string& error);

// The socket and what it holds — Asio's, kept in the core (httpClient.cpp).
struct ibWebSocketClientState;

// A WEBSOCKET TO A SERVER — opened at once and said to when it has opened; what is sent before that waits for it.
// Every call is safe from any thread; the handlers are called on the socket's strand.
class CORE_API ibWebSocketClient {
public:

	// The socket is open — the server's answer to the opening checked; once, before any message.
	using ibOpened = std::function<void()>;
	// A text message, whole — the engine's text, its bytes read once…
	using ibText = std::function<void(const ibString& text)>;
	// …and a binary one, as it came (a deflated text is the protocol's to inflate: ibInflate).
	using ibBinary = std::function<void(std::string data)>;
	// The socket is gone — refused, closed by either side, or dropped; once. `reason` — why, for the journal.
	using ibClosed = std::function<void(const std::string& reason)>;

	ibWebSocketClient(asio::io_context& io, ibOpened opened, ibText text, ibBinary binary, ibClosed closed);
	// Closes it, as Close: a client let go holds no socket open. `closed` is still told, on the strand — so neither
	// handler holds what dies with the client.
	~ibWebSocketClient();

	// OVER TLS — said before Open (ibTlsClientContext): the host Open names is the name the server's certificate is
	// checked against. Not said — in the clear.
	void Secure(std::shared_ptr<const ibTlsContext> context);
	// `target` — the path and its query, as written on the request line.
	void Open(const std::string& host, std::uint16_t port, const std::string& target);
	// A message, queued and sent in order — as ibWebSocket's: the text's bytes made here, once.
	void Send(const ibString& text);
	void SendBinary(std::string data);
	void Close();

private:

	std::shared_ptr<ibWebSocketClientState> m_state;
};

// ONE POST to host:port at `target` — over `tls` when it is given (ibTlsClientContext), in the clear when it is null —
// answered to `done` on an I/O thread, once: the status (0 — none came, and `error` says why) and the body. `wait` —
// how long the answer may take.
CORE_API void ibHttpPost(asio::io_context& io, const std::string& host, std::uint16_t port,
	std::shared_ptr<const ibTlsContext> tls, const std::string& target, std::string body, const std::string& type,
	std::chrono::seconds wait, std::function<void(int status, std::string body, const std::string& error)> done);

// The socket, its TLS and the answer being read — Asio's and Mbed TLS's, kept in the core (httpClient.cpp).
struct ibHttpClientState;

// ONE HOST AND PORT, its connection kept alive from one request to the next. SYNCHRONOUS — the caller's thread waits,
// within the time it allows for the whole of a request. A redirect is an answer (its status and its Location):
// following it is the caller's decision.
class CORE_API ibHttpClient {
public:

	// An answer is held in memory, whole: this is the most of it.
	static constexpr std::size_t kMaxAnswerBytes = 100 * 1024 * 1024;

	ibHttpClient(const std::string& host, std::uint16_t port);
	~ibHttpClient();

	// OVER TLS from here on — the words of ibTlsClientContext, its context this client's own.
	ibHttpClientError Secure(const std::string& trusted, const std::string& certificate, const std::string& password,
		bool verify, std::string& error);

	// ONE REQUEST — m_method, m_path and m_query as they are written (encoded), m_headers, m_body — and its answer: the
	// status, the headers as they came, the body. ::None — answered, whatever the status; otherwise why not, with the
	// detail in `error`. `wait` bounds the whole of it, from the connection to the answer's last byte.
	ibHttpClientError Send(const ibHttpRequest& request, ibHttpResponse& response, std::chrono::seconds wait,
		std::string& error);

private:
	std::unique_ptr<ibHttpClientState> m_state;
};

#endif
