#ifndef __CORE_NET_HTTP_SERVER_H__
#define __CORE_NET_HTTP_SERVER_H__

// THE ENGINE'S HTTP — HTTP/1.1 and WebSocket (RFC 6455), over plain TCP or TLS, ASYNCHRONOUS: a few I/O threads
// carry every connection (Asio), and a socket waiting for its person costs a buffer, not a thread — the port holds
// thousands of them. The I/O threads only move bytes: parsing (llhttp), framing, TLS. What may take long — a file
// read, a base's call — is a handler's, and handlers run on the workers.
//
// What it refuses by itself, before any handler sees a request: a header block over 16 KB, a body or a WebSocket
// message over 4 MB, a request that takes more than 30 seconds to arrive, a keep-alive connection silent for a minute,
// a WebSocket from a page of another origin, a client that does not mask its frames, a text that is not UTF-8. A
// connection that does not read what it is sent is let go. A body's limit and the keep-alive's are the options' to
// move (m_maxBodyBytes, m_idleSeconds): an assistant's request is larger, and its pauses are for thinking.
//
// HTTP AND HTTPS, either or both, each on its own port. ⭐ THE CLEAR DOES NOT CROSS A NETWORK — the rule of
// core/server/listener.h too: on this machine's own addresses (ibIsLoopback) HTTP serves; on any other it only sends
// a browser on to HTTPS, and with no HTTPS to send it to it is refused.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/core.h"
#include "core/fstring.h"   // ibString — the peer's address, made once for its connection

namespace asio { class io_context; }

// What the server's threads share — its routes, its threads, its connections (httpServer.cpp).
struct ibHttpServerState;

// One request, whole — its head and its body.
struct CORE_API ibHttpRequest {
	std::string m_method;    // as written: GET, HEAD, POST
	std::string m_path;      // decoded, without the query: /trade/client
	std::string m_query;     // as written, after the '?'
	std::vector<std::pair<std::string, std::string>> m_headers;   // names lower-cased, in their order
	std::string m_body;
	ibString    m_address;   // the peer's IP — its connection's, every request and handler one more owner of it
	bool        m_secure = false;   // came over TLS

	// The first header of that lower-case name; empty when there is none.
	std::string GetHeader(const std::string& name) const;
	// One parameter of the query, decoded; empty when there is none.
	std::string GetParameter(const std::string& name) const;
	// The path escaped again and its query — what a request carried on to another server is written with
	// (ibWebSocketClient::Open, ibHttpPost).
	std::string GetTarget() const;
	// The body as the engine's text — a text protocol's request (JSON-RPC), its bytes read once.
	ibString GetText() const;
};

struct CORE_API ibHttpResponse {
	int         m_status = 200;
	std::vector<std::pair<std::string, std::string>> m_headers;
	std::string m_body;

	// The body and its Content-Type…
	void SetContent(std::string body, const std::string& type);
	// …and as the engine's text, its bytes made once — the pair of the request's GetText.
	void SetText(const ibString& text, const std::string& type);
};

// THE ANSWER TO A REQUEST — given once, from any thread: at once, or when what it waits for has come. The request a
// handler is given is its own copy, alive while the handler runs; one answered later is the handler's to copy.
using ibHttpResponder = std::function<void(ibHttpResponse response)>;
using ibHttpHandler   = std::function<void(const ibHttpRequest& request, ibHttpResponder respond)>;

// HOW BYTES GO — as they are, or deflated when the peer asked for it as it connected (?compress=deflate). The sender's
// to say, for it knows what it sends: a packet of the protocol's may be; what a relay carries on is final already.
enum class ibWebSocketSend : std::uint8_t { AsIs = 0, Deflate = 1 };

// AN OPEN WEBSOCKET, as its handler holds it. Every call is safe from any thread; after the socket closed it does
// nothing.
class ibWebSocket {
public:
	virtual ~ibWebSocket() = default;

	// A message, queued and sent in order. The engine's text has its bytes made here, once — and, for a peer that asked
	// for it as it connected (?compress=deflate), one of some size goes deflated in a binary message (ibDeflate).
	virtual void Send(const ibString& text) = 0;
	// …and bytes, in a binary message — deflated as a text is when `flags` say they may be.
	virtual void Send(const void* data, std::size_t len, ibWebSocketSend flags = ibWebSocketSend::AsIs) = 0;
	// The closing handshake begun, with `code` and `reason` (RFC 6455 § 7.4); OnClose follows.
	virtual void Close(std::uint16_t code, const std::string& reason) = 0;

	// The request it was opened by.
	virtual const ibHttpRequest& GetRequest() const = 0;
};

// WHAT A WEBSOCKET IS TOLD — on a worker, one call at a time for that socket, in the order it happened.
class ibWebSocketHandler {
public:
	virtual ~ibWebSocketHandler() = default;

	// One text message, whole — the engine's text, its bytes read once.
	virtual void OnMessage(const ibString& text) = 0;
	// One binary message, whole — a protocol that speaks text closes the socket (ibWebSocketClose::Unsupported).
	virtual void OnBinary(std::string data) = 0;
	// The socket is gone — closed by either side, dropped, or the server stopping: once, after the last OnMessage.
	virtual void OnClose() = 0;
};

// A WebSocket asked for: its handler, or null — refused, and the request answered 404. On a worker.
using ibWebSocketOpening = std::function<std::shared_ptr<ibWebSocketHandler>(const std::shared_ptr<ibWebSocket>& socket)>;

// AN OPEN EVENT STREAM (text/event-stream), as its handler holds it — the server speaks, the peer only listens: an
// assistant told what the platform says. Every call is safe from any thread; after the stream closed it does nothing.
class ibEventStream {
public:
	virtual ~ibEventStream() = default;

	// One event — its data, a `data:` line for each of its lines.
	virtual void Send(std::string data) = 0;
	// The stream ended by this side; OnClose follows.
	virtual void Close() = 0;

	// The request it was opened by.
	virtual const ibHttpRequest& GetRequest() const = 0;
};

// WHAT AN EVENT STREAM IS TOLD — on a worker, once.
class ibEventStreamHandler {
public:
	virtual ~ibEventStreamHandler() = default;

	// The stream is gone — closed by either side, dropped, or the server stopping.
	virtual void OnClose() = 0;
};

// An event stream asked for (GET): its handler — or null, and `refusal` answered instead (404 unless the opening says
// otherwise: its status, its headers, its body). On a worker.
using ibEventStreamOpening = std::function<std::shared_ptr<ibEventStreamHandler>(const std::shared_ptr<ibEventStream>& stream,
	ibHttpResponse& refusal)>;

struct ibHttpServerOptions {
	std::string   m_host;              // empty — every interface (HTTP there only sends on to HTTPS, see above)

	// The ports, each listened to when it is given: 0 — the system chooses, and GetHttpPort / GetHttpsPort say which.
	std::optional<std::uint16_t> m_httpPort;
	std::optional<std::uint16_t> m_httpsPort;

	// ⚠ HTTP SERVED IN THE CLEAR BEYOND THIS MACHINE TOO — the debug server's alone, until its port speaks TLS: the
	// designer reaches a runtime's debugger over ws:// only (backend/debugger/debugClient.cpp).
	bool          m_clearBeyondMachine = false;

	// PEM text — the certificate (its chain after it) and its key: what HTTPS is spoken over, and only it.
	std::string   m_certificate;
	std::string   m_privateKey;

	std::size_t   m_threads = 0;       // the I/O threads; 0 — one per core
	std::size_t   m_workers = 0;       // the handlers' threads; 0 — four per core, 32 at least
	std::size_t   m_maxConnections = 10000;
	std::size_t   m_maxConnectionsPerAddress = 0;   // 0 — no limit: an office behind one address is a thousand people
	std::size_t   m_maxBodyBytes = 4 * 1024 * 1024;  // a request's body; past it — 413
	std::size_t   m_idleSeconds = 60;                // a kept-alive connection silent between its requests, before it goes

	// The origins a page may open a WebSocket from besides the server's own (scheme://host[:port], as a browser
	// sends it). A request without an Origin is no browser's, and is let in.
	std::vector<std::string> m_origins;
};

class CORE_API ibHttpServer {
public:

	ibHttpServer();
	~ibHttpServer();

	// THE ROUTES — by the exact decoded path; a request for none of them goes to the files mounted, and failing them
	// is answered 404. Said before Start.
	void Get(const std::string& path, ibHttpHandler handler);
	void Post(const std::string& path, ibHttpHandler handler);
	void WebSocket(const std::string& path, ibWebSocketOpening opening);
	void EventStream(const std::string& path, ibEventStreamOpening opening);
	// The files of `directory` under the path `prefix` — GET and HEAD, a folder answered by its index.html, nothing
	// outside the directory, no hidden file, no listing.
	void Mount(const std::string& prefix, const std::string& directory);
	// …and files held in memory, by their path under `prefix` (index.html, js/app.js) — a pack read whole: served as a
	// directory's are.
	void Mount(const std::string& prefix, std::map<std::string, std::string> files);

	// Listening, with the threads up — or false, with why in `error`.
	bool Start(const ibHttpServerOptions& options, std::string& error);
	// …on the first free port among `tries` from the options' own — HTTPS's when it is given one, HTTP's otherwise
	// (GetHttpsPort, GetHttpPort say which) — or false, with why the last one was refused.
	bool Start(ibHttpServerOptions options, unsigned int tries, std::string& error);
	// The port closed and every connection let go: each WebSocket's handler is told OnClose before Stop returns.
	void Stop();

	// The ports listened to; 0 — that one is not.
	std::uint16_t GetHttpPort() const;
	std::uint16_t GetHttpsPort() const;

	// THE I/O THREADS' CONTEXT — for a connection a handler opens itself (the web server's to an application server):
	// it is carried by the same threads, and stops with them.
	asio::io_context& GetContext();

private:
	std::unique_ptr<ibHttpServerState> m_state;
};

// THIS MACHINE'S OWN ADDRESSES, known as written — 127.0.0.0/8 by number, ::1, localhost — not a name that resolves
// there, since a name resolves wherever its owner says: where the clear is spoken (a listener's host, the header), and
// who may steer the application cluster (a peer's address, core/server/clusterControl.h).
CORE_API bool ibIsLoopback(const ibString& address);

#endif
