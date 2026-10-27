#include "httpServer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include <asio/dispatch.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>

#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>   // mbedtls_platform_zeroize — the key's text wiped once the TLS context has it

#include <llhttp.h>

#include "httpStream.h"
#include "tlsStream.h"
#include "webSocketFrame.h"

namespace {

using ibClock = std::chrono::steady_clock;

// THE LIMITS — what one connection may ask of the server before it is refused or let go. A request's body and the
// silence between two requests are the options' (m_maxBodyBytes, m_idleSeconds).
constexpr std::size_t kReadChunk         = 16 * 1024;
constexpr std::size_t kMaxHeadBytes      = 16 * 1024;          // the request line and the headers
constexpr std::size_t kMaxHeaders        = 100;
constexpr std::size_t kMaxMessageBytes   = 4 * 1024 * 1024;    // a WebSocket's message is a method and its arguments
constexpr std::size_t kMaxPendingBytes   = 64 * 1024 * 1024;   // sent and not yet taken by the peer
constexpr std::size_t kMaxQueuedMessages = 32;                 // received and not yet handled — reading waits

constexpr std::chrono::seconds kRequestTime(30);     // a TLS handshake, or a request from its first byte to its last
constexpr std::chrono::seconds kWriteTime(60);       // one write the peer does not take
constexpr std::chrono::seconds kFirstMessage(15);    // a WebSocket that has said nothing yet
constexpr std::chrono::seconds kPingAfter(30);       // a WebSocket's silence before it is asked whether it is there
constexpr std::chrono::seconds kPongWithin(30);      // …and before it is let go
constexpr std::chrono::seconds kCloseWithin(5);      // the peer's answer to a close
constexpr std::chrono::seconds kStopWithin(30);      // the connections' teardown at a stop

// A file of the mounted directories: kept in memory up to this size, and none served above the second.
constexpr std::uintmax_t kShelfFileMax = 8 * 1024 * 1024;
constexpr std::uintmax_t kShelfMax     = 128 * 1024 * 1024;
constexpr std::uintmax_t kFileMax      = 256 * 1024 * 1024;

std::string Lower(std::string text)
{
	for (char& c : text) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return text;
}

std::string Trim(const std::string& text)
{
	const std::size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return std::string();
	const std::size_t last = text.find_last_not_of(" \t");
	return text.substr(first, last - first + 1);
}

// A path's UTF-8 text as plain bytes — since C++20 `u8string()` answers std::u8string, not std::string.
std::string Utf8(const std::u8string& text)
{
	return std::string(text.begin(), text.end());
}

// `token` among the comma-separated words of a header, as Connection and Upgrade list them.
bool HasToken(const std::string& list, const std::string& token)
{
	std::size_t from = 0;
	while (from <= list.size()) {
		const std::size_t comma = list.find(',', from);
		const std::string word = Lower(Trim(list.substr(from, comma == std::string::npos ? std::string::npos : comma - from)));
		if (word == token)
			return true;
		if (comma == std::string::npos)
			break;
		from = comma + 1;
	}
	return false;
}

int HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// %XX undone — and, in a query, '+' as a space. False for an escape that is not one, or a zero byte.
bool Decode(const std::string& text, bool query, std::string& decoded)
{
	decoded.clear();
	decoded.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (c == '%') {
			if (i + 2 >= text.size())
				return false;
			const int high = HexDigit(text[i + 1]);
			const int low = HexDigit(text[i + 2]);
			if (high < 0 || low < 0)
				return false;
			const char byte = static_cast<char>((high << 4) | low);
			if (byte == '\0')
				return false;
			decoded += byte;
			i += 2;
		}
		else if (c == '+' && query) {
			decoded += ' ';
		}
		else if (c == '\0') {
			return false;
		}
		else {
			decoded += c;
		}
	}
	return true;
}

const char* Reason(int status)
{
	switch (status) {
	case 100: return "Continue";
	case 101: return "Switching Protocols";
	case 200: return "OK";
	case 204: return "No Content";
	case 301: return "Moved Permanently";
	case 304: return "Not Modified";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 408: return "Request Timeout";
	case 413: return "Payload Too Large";
	case 414: return "URI Too Long";
	case 426: return "Upgrade Required";
	case 431: return "Request Header Fields Too Large";
	case 500: return "Internal Server Error";
	case 502: return "Bad Gateway";
	case 503: return "Service Unavailable";
	case 504: return "Gateway Timeout";
	default:  return "Status";
	}
}

// What a file is, by its extension.
const char* ContentType(const std::filesystem::path& file)
{
	static const std::map<std::string, const char*> s_types = {
		{ ".html", "text/html; charset=utf-8" },  { ".htm", "text/html; charset=utf-8" },
		{ ".js", "text/javascript; charset=utf-8" }, { ".mjs", "text/javascript; charset=utf-8" },
		{ ".css", "text/css; charset=utf-8" },    { ".json", "application/json" },
		{ ".map", "application/json" },           { ".txt", "text/plain; charset=utf-8" },
		{ ".xml", "application/xml" },            { ".svg", "image/svg+xml" },
		{ ".png", "image/png" },                  { ".jpg", "image/jpeg" },
		{ ".jpeg", "image/jpeg" },                { ".gif", "image/gif" },
		{ ".webp", "image/webp" },                { ".ico", "image/x-icon" },
		{ ".woff", "font/woff" },                 { ".woff2", "font/woff2" },
		{ ".ttf", "font/ttf" },                   { ".otf", "font/otf" },
		{ ".wasm", "application/wasm" },          { ".pdf", "application/pdf" },
	};
	const auto found = s_types.find(Lower(Utf8(file.extension().u8string())));
	return found != s_types.end() ? found->second : "application/octet-stream";
}

// A segment Windows reads as something other than a file of that name: a device (CON, NUL, COM1…), a name it
// trims (a trailing dot or space), a stream (a colon).
bool IsDeviceName(const std::string& segment)
{
	const std::string stem = Lower(segment.substr(0, segment.find('.')));
	static const char* const s_devices[] = { "con", "prn", "aux", "nul", "conin$", "conout$" };
	for (const char* device : s_devices) {
		if (stem == device)
			return true;
	}
	return stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0)
		&& stem[3] >= '0' && stem[3] <= '9';
}

// THE FILE A PATH NAMES UNDER `root` — or false: a path that leaves the root, names a hidden file (.git, .env), or one
// the system would read as something else.
bool Resolve(const std::filesystem::path& root, const std::string& relative, std::filesystem::path& file)
{
	file = root;
	std::size_t from = 0;
	while (from < relative.size()) {
		std::size_t slash = relative.find('/', from);
		if (slash == std::string::npos)
			slash = relative.size();
		const std::string segment = relative.substr(from, slash - from);
		from = slash + 1;
		if (segment.empty())
			continue;
		if (segment[0] == '.' || segment.back() == '.' || segment.back() == ' '
			|| segment.find_first_of("\\:*?\"<>|") != std::string::npos || IsDeviceName(segment))
			return false;
		file /= std::filesystem::u8path(segment);
	}
	return true;
}

// `file` inside `root`, both canonical — a link that leads out of the root leads nowhere.
bool IsInside(const std::filesystem::path& root, const std::filesystem::path& file)
{
	auto at = file.begin();
	for (const std::filesystem::path& part : root) {
		if (at == file.end() || *at != part)
			return false;
		++at;
	}
	return true;
}

} // namespace

bool ibIsLoopback(const ibString& address)
{
	const std::string own = Lower(address.TrimAll().ToUtf8());
	if (own == "localhost" || own == "::1" || own == "[::1]")
		return true;
	std::error_code error;
	const asio::ip::address parsed = asio::ip::make_address(own, error);
	// Only in the spelling it prints back as: Windows reads a short form ("127.0.0") as an address and the other
	// systems refuse it, and one rule answers the same on all of them.
	return !error && parsed.is_v4() && parsed.is_loopback() && parsed.to_string() == own;
}

//*******************************************************************************************
//*                                     The request                                         *
//*******************************************************************************************

std::string ibHttpRequest::GetHeader(const std::string& name) const
{
	for (const auto& header : m_headers) {
		if (header.first == name)
			return header.second;
	}
	return std::string();
}

std::string ibHttpRequest::GetParameter(const std::string& name) const
{
	std::size_t from = 0;
	while (from <= m_query.size()) {
		std::size_t amp = m_query.find('&', from);
		if (amp == std::string::npos)
			amp = m_query.size();
		const std::string pair = m_query.substr(from, amp - from);
		const std::size_t equals = pair.find('=');
		std::string key, value;
		if (Decode(pair.substr(0, equals), true, key) && key == name) {
			if (equals == std::string::npos || !Decode(pair.substr(equals + 1), true, value))
				value.clear();
			return value;
		}
		from = amp + 1;
	}
	return std::string();
}

std::string ibHttpRequest::GetTarget() const
{
	// Every byte of the path but the unreserved and the separators escaped — what Decode read, written back.
	static const char s_hex[] = "0123456789ABCDEF";
	std::string target;
	for (const char ch : m_path) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
			|| c == '-' || c == '.' || c == '_' || c == '~' || c == '/') {
			target += ch;
		}
		else {
			target += '%';
			target += s_hex[c >> 4];
			target += s_hex[c & 0x0F];
		}
	}
	if (!m_query.empty()) {
		target += '?';
		target += m_query;
	}
	return target;
}

ibString ibHttpRequest::GetText() const
{
	return ibString::FromUTF8(m_body.data(), m_body.size());
}

void ibHttpResponse::SetContent(std::string body, const std::string& type)
{
	m_body = std::move(body);
	for (auto& header : m_headers) {
		if (Lower(header.first) == "content-type") {
			header.second = type;
			return;
		}
	}
	m_headers.emplace_back("Content-Type", type);
}

void ibHttpResponse::SetText(const ibString& text, const std::string& type)
{
	SetContent(text.ToUtf8(), type);
}

//*******************************************************************************************
//*                                       The files                                         *
//*******************************************************************************************

namespace {

// AN ANSWER AS IT IS SENT — the body shared, so a file kept in memory goes to a thousand peers as one copy.
struct ibReply {
	int                                               m_status = 200;
	std::vector<std::pair<std::string, std::string>>  m_headers;
	std::shared_ptr<const std::string>                m_body;
	bool                                              m_bodyless = false;   // HEAD: the length said, the body not sent
};

ibReply ToReply(ibHttpResponse response)
{
	ibReply reply;
	reply.m_status = response.m_status;
	reply.m_headers = std::move(response.m_headers);
	reply.m_body = std::make_shared<const std::string>(std::move(response.m_body));
	return reply;
}

ibReply Plain(int status, const std::string& text)
{
	ibReply reply;
	reply.m_status = status;
	reply.m_headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
	reply.m_body = std::make_shared<const std::string>(text);
	return reply;
}

// ONE CHUNK of a body whose length nobody knows ahead (an event stream's): its size in hex, the bytes, CRLF.
std::string Chunk(const std::string& bytes)
{
	char size[24];
	std::snprintf(size, sizeof(size), "%zx\r\n", bytes.size());
	return size + bytes + "\r\n";
}

// ONE EVENT as a stream sends it: a `data:` line for each line of it, and the blank line that ends it.
std::string EventOf(const std::string& data)
{
	std::string event;
	std::size_t at = 0;
	for (;;) {
		const std::size_t end = data.find('\n', at);
		std::string line = data.substr(at, end == std::string::npos ? std::string::npos : end - at);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		event += "data: " + line + "\n";
		if (end == std::string::npos)
			break;
		at = end + 1;
	}
	return event + "\n";
}

// A FILE HELD IN MEMORY — a pack's: its bytes, and its tag made once as it was mounted.
struct ibHeldFile {
	std::shared_ptr<const std::string> m_data;
	std::string                        m_tag;
};

// A held file's tag: its size and a hash of its bytes (FNV-1a) — a file changed in a new pack has a new one.
std::string TagOf(const std::string& data)
{
	std::uint64_t hash = 14695981039346656037ull;
	for (const unsigned char c : data) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	char tag[64];
	std::snprintf(tag, sizeof(tag), "\"%llx-%llx\"", static_cast<unsigned long long>(data.size()),
		static_cast<unsigned long long>(hash));
	return tag;
}

// THE FILES OF THE MOUNTED DIRECTORIES — read once and kept while unchanged (the same time and size), the copy a
// thousand peers are sent. A file over kShelfFileMax is read for each request and not kept.
class ibStaticFiles {
public:

	// The answer to a GET or HEAD of `relative` under `root` (canonical).
	ibReply Serve(const std::filesystem::path& root, const std::string& relative, const ibHttpRequest& request)
	{
		std::filesystem::path file;
		if (!Resolve(root, relative, file))
			return Plain(404, "Not Found");
		std::error_code error;
		if (std::filesystem::is_directory(file, error))
			file /= "index.html";
		file = std::filesystem::weakly_canonical(file, error);
		if (error || !IsInside(root, file) || !std::filesystem::is_regular_file(file, error))
			return Plain(404, "Not Found");

		const std::uintmax_t size = std::filesystem::file_size(file, error);
		const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
		if (error || size > kFileMax)
			return Plain(404, "Not Found");

		std::shared_ptr<const std::string> data;
		const std::string key = Utf8(file.u8string());
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto found = m_files.find(key);
			if (found != m_files.end() && found->second.m_time == time && found->second.m_size == size)
				data = found->second.m_data;
		}
		if (!data) {
			std::ifstream in(file, std::ios::binary);
			std::string bytes(static_cast<std::size_t>(size), '\0');
			if (!in || !in.read(&bytes[0], static_cast<std::streamsize>(size)))
				return Plain(404, "Not Found");
			data = std::make_shared<const std::string>(std::move(bytes));
			if (size <= kShelfFileMax) {
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_held + size > kShelfMax) {
					m_files.clear();
					m_held = 0;
				}
				auto& shelved = m_files[key];
				m_held -= shelved.m_data ? shelved.m_data->size() : 0;
				shelved = ibShelved{ time, size, data };
				m_held += size;
			}
		}

		char tag[64];
		std::snprintf(tag, sizeof(tag), "\"%llx-%llx\"", static_cast<unsigned long long>(size),
			static_cast<unsigned long long>(time.time_since_epoch().count()));
		return Reply(std::move(data), tag, file, request);
	}

	// …and of `relative` among files held in memory, refused as a directory's would be.
	ibReply Serve(const std::map<std::string, ibHeldFile>& files, const std::string& relative,
		const ibHttpRequest& request) const
	{
		std::filesystem::path path;
		if (!Resolve(std::filesystem::path(), relative, path))
			return Plain(404, "Not Found");
		const std::string name = Utf8(path.generic_u8string());
		auto found = files.end();
		if (!name.empty() && relative.back() != '/')
			found = files.find(name);
		if (found == files.end())   // a folder: its index.html
			found = files.find(name.empty() ? std::string("index.html") : name + "/index.html");
		if (found == files.end())
			return Plain(404, "Not Found");
		return Reply(found->second.m_data, found->second.m_tag, std::filesystem::u8path(found->first), request);
	}

private:

	// A file's answer, by its tag: asked again each time, and 304 for the copy the browser holds.
	static ibReply Reply(std::shared_ptr<const std::string> data, const std::string& tag,
		const std::filesystem::path& file, const ibHttpRequest& request)
	{
		ibReply reply;
		reply.m_headers.emplace_back("ETag", tag);
		// Asked again each time: the files are the application's, and a page must not outlive a new release.
		reply.m_headers.emplace_back("Cache-Control", "no-cache");
		if (request.GetHeader("if-none-match") == tag) {
			reply.m_status = 304;
			return reply;
		}
		reply.m_headers.emplace_back("Content-Type", ContentType(file));
		reply.m_body = std::move(data);
		return reply;
	}

	struct ibShelved {
		std::filesystem::file_time_type    m_time;
		std::uintmax_t                     m_size = 0;
		std::shared_ptr<const std::string> m_data;
	};

	std::mutex                                 m_mutex;
	std::unordered_map<std::string, ibShelved> m_files;
	std::uintmax_t                             m_held = 0;
};

class ibConnection;

} // namespace

//*******************************************************************************************
//*                                     The server's state                                  *
//*******************************************************************************************

struct ibHttpServerState {

	ibHttpServerOptions m_options;

	asio::io_context                                                         m_io;
	std::optional<asio::executor_work_guard<asio::io_context::executor_type>> m_work;
	std::vector<std::thread>                                                 m_threads;
	std::unique_ptr<asio::thread_pool>                                       m_workers;
	std::shared_ptr<const ibTlsContext>                                      m_tls;

	// THE PORTS — HTTPS's, HTTP's, or both.
	struct ibListener {
		std::unique_ptr<asio::ip::tcp::acceptor> m_acceptor;
		std::unique_ptr<asio::steady_timer>      m_pause;      // after an accept failed
		bool                                     m_secure = false;
		bool                                     m_redirect = false;   // HTTP beyond this machine: on to HTTPS only
	};
	std::vector<std::unique_ptr<ibListener>> m_listeners;
	std::uint16_t                            m_httpPort = 0;
	std::uint16_t                            m_httpsPort = 0;

	std::map<std::string, ibHttpHandler>      m_get;
	std::map<std::string, ibHttpHandler>      m_post;
	std::map<std::string, ibWebSocketOpening> m_sockets;
	std::map<std::string, ibEventStreamOpening> m_streams;
	struct ibMount {
		std::string           m_prefix;
		std::filesystem::path m_root;   // canonical
		std::shared_ptr<const std::map<std::string, ibHeldFile>> m_files;   // …or files held in memory, by their path
	};
	std::vector<ibMount> m_mounts;
	ibStaticFiles        m_files;

	// The connections — for the limits, and for the stop, which lets each go and waits until all have.
	std::mutex                                         m_mutex;
	std::condition_variable                            m_drained;
	std::map<ibConnection*, std::weak_ptr<ibConnection>> m_connections;
	std::map<ibString, std::size_t>                    m_perAddress;
	bool                                               m_stopping = false;

	// Bound and listening — false with why in `error`; the port it took is said back in `port`.
	bool Listen(const asio::ip::address& address, std::uint16_t& port, bool secure, bool redirect, std::string& error);
	void Accept(ibListener& listener);
	void Open(const ibListener& listener, asio::ip::tcp::socket socket);
	void Release(ibConnection* connection, const ibString& address);
};

//*******************************************************************************************
//*                                     The connection                                      *
//*******************************************************************************************

namespace {

// ⭐ THE TRANSPORT DEFLATES, for a peer that asked for it as it connected (?compress=deflate): a text from this many
// bytes goes as a binary message holding it deflated, a smaller one as text. What it carries never knows: the same
// text either way. A frame is text that repeats itself (names, ids, the same fields on every control) and shrinks
// several times over.
constexpr std::size_t kDeflateFrom = 1024;

// A WEBSOCKET AS ITS HANDLER HOLDS IT — the connection's, by a weak reference: a handler that keeps it longer than
// the connection lives holds nothing that sends.
class ibServerWebSocket : public ibWebSocket {
public:

	ibServerWebSocket(std::weak_ptr<ibConnection> connection, ibHttpRequest request) :
		m_connection(std::move(connection)), m_request(std::move(request)),
		m_deflate(m_request.GetParameter("compress") == "deflate")
	{
	}

	virtual void Send(const ibString& text) override;
	virtual void Send(const void* data, std::size_t len, ibWebSocketSend flags) override;
	virtual void Close(std::uint16_t code, const std::string& reason) override;

	virtual const ibHttpRequest& GetRequest() const override { return m_request; }

private:
	std::weak_ptr<ibConnection> m_connection;
	ibHttpRequest               m_request;
	bool                        m_deflate;   // the peer asked for it as it connected
};

// AN EVENT STREAM AS ITS HANDLER HOLDS IT — the connection's, by a weak reference, as a WebSocket is.
class ibServerEventStream : public ibEventStream {
public:

	ibServerEventStream(std::weak_ptr<ibConnection> connection, ibHttpRequest request) :
		m_connection(std::move(connection)), m_request(std::move(request))
	{
	}

	virtual void Send(std::string data) override;
	virtual void Close() override;

	virtual const ibHttpRequest& GetRequest() const override { return m_request; }

private:
	std::weak_ptr<ibConnection> m_connection;
	ibHttpRequest               m_request;
};

// ONE CONNECTION — its requests one after the other, and, once a request asked for it, its WebSocket. Everything
// here runs on the connection's strand (the socket's executor); what may be called from elsewhere says so and goes
// there first.
class ibConnection : public std::enable_shared_from_this<ibConnection> {
public:

	ibConnection(ibHttpServerState& server, std::unique_ptr<ibHttpStream> stream, const ibString& address, bool secure,
		bool redirect) :
		m_server(server), m_stream(std::move(stream)), m_executor(m_stream->GetSocket().get_executor()),
		m_timer(m_executor), m_writeTimer(m_executor), m_address(address), m_secure(secure),
		m_redirect(redirect)
	{
		llhttp_init(&m_parser, HTTP_REQUEST, &Settings());
		m_parser.data = this;
	}

	~ibConnection()
	{
		m_server.Release(this, m_address);
	}

	// Its handshake (TLS) and its first request — from the accepting thread.
	void Start()
	{
		asio::dispatch(m_executor, [self = shared_from_this()]() {
			self->Arm(kRequestTime);
			self->m_stream->Handshake([self](const std::error_code& error) {
				if (error)
					self->Drop();
				else
					self->ReadRequest();
			});
		});
	}

	// Let go, at once — from any thread.
	void Abort()
	{
		asio::dispatch(m_executor, [self = shared_from_this()]() { self->Drop(); });
	}

	// The WebSocket's doors — from any thread.
	void SendMessage(std::uint8_t opcode, std::string payload)
	{
		asio::post(m_executor, [self = shared_from_this(), opcode, payload = std::move(payload)]() mutable {
			if (self->m_closed || self->m_closeSent)
				return;
			if (!self->m_upgraded) {
				self->m_early.emplace_back(opcode, std::move(payload));   // said before the 101 went out
				return;
			}
			self->SendFrame(opcode, std::move(payload));
		});
	}

	void BeginClose(std::uint16_t code, std::string reason)
	{
		asio::post(m_executor, [self = shared_from_this(), code, reason = std::move(reason)]() {
			if (self->m_closed || self->m_closeSent)
				return;
			if (!self->m_upgraded) {
				self->Drop();
				return;
			}
			self->SendClose(code, reason);
			self->Arm(kCloseWithin, [self]() { self->Drop(); });
		});
	}

	// The event stream's doors — from any thread.
	void SendEvent(std::string data)
	{
		asio::post(m_executor, [self = shared_from_this(), data = std::move(data)]() mutable {
			if (self->m_closed || self->m_streamEnded)
				return;
			if (!self->m_streaming) {
				self->m_earlyEvents.push_back(std::move(data));   // said before the stream's head went out
				return;
			}
			self->Queue(std::make_shared<const std::string>(Chunk(EventOf(data))));
		});
	}

	void EndStream()
	{
		asio::post(m_executor, [self = shared_from_this()]() {
			if (self->m_closed || self->m_streamEnded)
				return;
			if (!self->m_streaming) {
				self->Drop();
				return;
			}
			self->m_streamEnded = true;
			self->Disarm();
			self->Queue(std::make_shared<const std::string>("0\r\n\r\n"));   // the last chunk
			self->m_closeWhenWritten = true;
			self->Pump();
		});
	}

private:

	//*************************************************************************
	//*                         Reading a request                             *
	//*************************************************************************

	static const llhttp_settings_t& Settings()
	{
		static const llhttp_settings_t s_settings = []() {
			llhttp_settings_t settings;
			llhttp_settings_init(&settings);
			settings.on_message_begin    = &ibConnection::OnMessageBegin;
			settings.on_url              = &ibConnection::OnUrl;
			settings.on_header_field     = &ibConnection::OnHeaderField;
			settings.on_header_value     = &ibConnection::OnHeaderValue;
			settings.on_headers_complete = &ibConnection::OnHeadersComplete;
			settings.on_body             = &ibConnection::OnBody;
			settings.on_message_complete = &ibConnection::OnMessageComplete;
			return settings;
		}();
		return s_settings;
	}

	static ibConnection& Of(llhttp_t* parser) { return *static_cast<ibConnection*>(parser->data); }

	// Counted against the head's limit; false — over it, the request refused with `status`.
	bool Count(std::size_t size, int status)
	{
		m_headBytes += size;
		if (m_headBytes <= kMaxHeadBytes)
			return true;
		m_refusal = status;
		return false;
	}

	void CommitHeader()
	{
		if (m_field.empty() && !m_hasValue)
			return;
		m_request.m_headers.emplace_back(Lower(m_field), Trim(m_value));
		m_field.clear();
		m_value.clear();
		m_hasValue = false;
	}

	static int OnMessageBegin(llhttp_t* parser)
	{
		ibConnection& self = Of(parser);
		self.m_request = ibHttpRequest();
		self.m_target.clear();
		self.m_field.clear();
		self.m_value.clear();
		self.m_hasValue = false;
		self.m_headBytes = 0;
		self.m_refusal = 0;
		self.Arm(kRequestTime);
		return 0;
	}

	static int OnUrl(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibConnection& self = Of(parser);
		if (!self.Count(size, 414))
			return -1;
		self.m_target.append(at, size);
		return 0;
	}

	static int OnHeaderField(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibConnection& self = Of(parser);
		if (!self.Count(size, 431))
			return -1;
		if (self.m_hasValue)
			self.CommitHeader();
		self.m_field.append(at, size);
		return 0;
	}

	static int OnHeaderValue(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibConnection& self = Of(parser);
		if (!self.Count(size, 431))
			return -1;
		self.m_value.append(at, size);
		self.m_hasValue = true;
		return 0;
	}

	static int OnHeadersComplete(llhttp_t* parser)
	{
		ibConnection& self = Of(parser);
		self.CommitHeader();
		ibHttpRequest& request = self.m_request;
		if (request.m_headers.size() > kMaxHeaders) {
			self.m_refusal = 431;
			return -1;
		}
		if (parser->content_length > self.m_server.m_options.m_maxBodyBytes) {
			self.m_refusal = 413;
			return -1;
		}
		// The origin form only — /path?query: no absolute URL (a proxy's), no '*'.
		if (self.m_target.empty() || self.m_target[0] != '/') {
			self.m_refusal = 400;
			return -1;
		}
		const std::size_t question = self.m_target.find('?');
		if (!Decode(self.m_target.substr(0, question), false, request.m_path)) {
			self.m_refusal = 400;
			return -1;
		}
		if (question != std::string::npos)
			request.m_query = self.m_target.substr(question + 1);
		request.m_method = llhttp_method_name(static_cast<llhttp_method_t>(parser->method));
		if (parser->http_major == 1 && parser->http_minor == 1 && request.GetHeader("host").empty()) {
			self.m_refusal = 400;   // HTTP/1.1 names its host (RFC 9112 § 3.2)
			return -1;
		}
		// A client that waits to be told to send its body is told: curl, for a large one.
		if (Lower(request.GetHeader("expect")) == "100-continue")
			self.Queue(std::make_shared<const std::string>("HTTP/1.1 100 Continue\r\n\r\n"));
		return 0;
	}

	static int OnBody(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibConnection& self = Of(parser);
		if (self.m_request.m_body.size() + size > self.m_server.m_options.m_maxBodyBytes) {
			self.m_refusal = 413;
			return -1;
		}
		self.m_request.m_body.append(at, size);
		return 0;
	}

	static int OnMessageComplete(llhttp_t* parser)
	{
		ibConnection& self = Of(parser);
		self.m_keepAlive = llhttp_should_keep_alive(parser) != 0;
		// One request at a time: the parser stops here, and what was read past it waits for the answer.
		return HPE_PAUSED;
	}

	void ReadRequest()
	{
		if (m_closed)
			return;
		if (!m_leftover.empty()) {
			std::string bytes;
			bytes.swap(m_leftover);
			Parse(bytes.data(), bytes.size());
			return;
		}
		if (m_answered > 0)
			Arm(std::chrono::seconds(m_server.m_options.m_idleSeconds));
		Read();
	}

	void Read()
	{
		if (m_closed)
			return;
		m_stream->ReadSome(m_buffer, sizeof(m_buffer), [self = shared_from_this()](const std::error_code& error, std::size_t bytes) {
			if (error || self->m_closed) {
				self->Drop();
				return;
			}
			if (self->m_streaming)
				self->Read();   // a listener says nothing; what it sends anyway is not a request
			else if (self->m_upgraded)
				self->OnFrames(self->m_buffer, bytes);
			else
				self->Parse(self->m_buffer, bytes);
		});
	}

	void Parse(const char* data, std::size_t size)
	{
		const llhttp_errno_t code = llhttp_execute(&m_parser, data, size);
		if (code == HPE_OK) {
			Read();
			return;
		}
		// Paused at the request's end (OnMessageComplete) — or after an upgrade's head, if the parser stops there first.
		if (code == HPE_PAUSED || code == HPE_PAUSED_UPGRADE) {
			const char* const stop = llhttp_get_error_pos(&m_parser);
			if (stop != nullptr && stop >= data && stop < data + size)
				m_leftover.assign(stop, static_cast<std::size_t>(data + size - stop));
			Dispatch();
			return;
		}
		Refuse(m_refusal != 0 ? m_refusal : 400);
	}

	//*************************************************************************
	//*                         Answering a request                           *
	//*************************************************************************

	void Dispatch()
	{
		Disarm();
		m_request.m_address = m_address;
		m_request.m_secure = m_secure;
		if (m_redirect) {
			Redirect();
			return;
		}
		if (llhttp_get_upgrade(&m_parser) != 0) {
			Upgrade();
			return;
		}

		const std::string& method = m_request.m_method;
		const auto stream = method == "GET" ? m_server.m_streams.find(m_request.m_path) : m_server.m_streams.end();
		if (stream != m_server.m_streams.end()) {
			OpenStream(stream->second);
			return;
		}
		const bool head = method == "HEAD";
		const std::map<std::string, ibHttpHandler>* routes = nullptr;
		if (method == "GET" || head)
			routes = &m_server.m_get;
		else if (method == "POST")
			routes = &m_server.m_post;
		else {
			ibReply reply = Plain(405, "Method Not Allowed");
			reply.m_headers.emplace_back("Allow", "GET, HEAD, POST");
			Answer(std::move(reply));
			return;
		}

		auto self = shared_from_this();
		const auto request = std::make_shared<const ibHttpRequest>(m_request);
		const auto route = routes->find(request->m_path);
		if (route != routes->end()) {
			// THE ANSWER, ONCE — a handler that answers twice is heard the first time.
			const auto answered = std::make_shared<std::atomic<bool>>(false);
			ibHttpResponder respond = [self, answered, head](ibHttpResponse response) {
				if (answered->exchange(true))
					return;
				asio::post(self->m_executor, [self, head, response = std::move(response)]() mutable {
					ibReply reply = ToReply(std::move(response));
					reply.m_bodyless = head;
					self->Answer(std::move(reply));
				});
			};
			const ibHttpHandler handler = route->second;
			asio::post(*m_server.m_workers, [request, handler, respond]() {
				try {
					handler(*request, respond);
				}
				catch (...) {
					ibHttpResponse failed;
					failed.m_status = 500;
					respond(std::move(failed));
				}
			});
			return;
		}

		if (method != "POST") {
			for (const ibHttpServerState::ibMount& mount : m_server.m_mounts) {
				const std::string& prefix = mount.m_prefix;
				const std::string& path = request->m_path;
				if (path.compare(0, prefix.size(), prefix) != 0)
					continue;
				if (prefix.back() != '/' && path.size() > prefix.size() && path[prefix.size()] != '/')
					continue;
				const std::filesystem::path root = mount.m_root;
				const auto files = mount.m_files;
				// The file is found on a worker — the disk is not the I/O threads' to wait for.
				asio::post(*m_server.m_workers, [self, request, root, files, prefix, head]() {
					const std::string relative = request->m_path.substr(prefix.size());
					ibReply reply = files ? self->m_server.m_files.Serve(*files, relative, *request)
						: self->m_server.m_files.Serve(root, relative, *request);
					reply.m_bodyless = head;
					asio::post(self->m_executor, [self, reply = std::move(reply)]() mutable { self->Answer(std::move(reply)); });
				});
				return;
			}
		}
		Answer(Plain(404, "Not Found"));
	}

	void Answer(ibReply reply)
	{
		if (m_closed)
			return;
		++m_answered;
		const bool closing = !m_keepAlive || m_refused;
		const bool lengthless = reply.m_status == 204 || reply.m_status == 304 || reply.m_status < 200;
		const std::size_t length = reply.m_body ? reply.m_body->size() : 0;

		std::string head = "HTTP/1.1 " + std::to_string(reply.m_status) + " " + Reason(reply.m_status) + "\r\n";
		for (const auto& header : reply.m_headers) {
			std::string value = header.second;
			value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == '\r' || c == '\n'; }), value.end());
			head += header.first + ": " + value + "\r\n";
		}
		if (!lengthless)
			head += "Content-Length: " + std::to_string(length) + "\r\n";
		head += "X-Content-Type-Options: nosniff\r\n"
			"X-Frame-Options: SAMEORIGIN\r\n"
			"Referrer-Policy: same-origin\r\n";
		head += closing ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";

		Queue(std::make_shared<const std::string>(std::move(head)));
		if (!lengthless && !reply.m_bodyless && length > 0)
			Queue(reply.m_body);

		if (closing) {
			m_closeWhenWritten = true;
			Pump();
			return;
		}
		llhttp_resume(&m_parser);
		ReadRequest();
	}

	// HTTP BEYOND THIS MACHINE — nothing in the clear: a page is sent on to the same address over HTTPS, and
	// anything else is refused.
	void Redirect()
	{
		m_refused = true;
		const std::string& method = m_request.m_method;
		if ((method != "GET" && method != "HEAD") || llhttp_get_upgrade(&m_parser) != 0) {
			Answer(Plain(403, "HTTPS only"));
			return;
		}
		// The host as a browser sends it — a name or an address, and a port; nothing else is sent anywhere.
		std::string host = m_request.GetHeader("host");
		if (host.empty() || host.find_first_not_of(
			"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]") != std::string::npos) {
			Answer(Plain(400, "Bad Request"));
			return;
		}
		if (host.front() == '[')
			host = host.substr(0, host.find(']') + 1);
		else if (host.find(':') != std::string::npos)
			host = host.substr(0, host.find(':'));
		const std::uint16_t port = m_server.m_httpsPort;
		ibReply reply = Plain(301, "Moved Permanently");
		reply.m_headers.emplace_back("Location",
			"https://" + host + (port == 443 ? std::string() : ":" + std::to_string(port)) + m_target);
		Answer(std::move(reply));
	}

	// The request refused — answered with `status`, and the connection closed after it: what follows it on the wire
	// cannot be trusted to begin where the parser thinks.
	void Refuse(int status)
	{
		m_refused = true;
		Disarm();
		Answer(Plain(status, Reason(status)));
	}

	//*************************************************************************
	//*                              Writing                                  *
	//*************************************************************************

	void Queue(std::shared_ptr<const std::string> bytes)
	{
		if (m_closed)
			return;
		m_outBytes += bytes->size();
		if (m_outBytes > kMaxPendingBytes) {
			Drop();   // the peer does not read what it is sent
			return;
		}
		m_out.push_back(std::move(bytes));
		Pump();
	}

	void Pump()
	{
		if (m_writing || m_closed)
			return;
		if (m_out.empty()) {
			if (m_closeWhenWritten)
				Drop();
			return;
		}
		m_writing = true;
		const std::shared_ptr<const std::string> chunk = m_out.front();
		const unsigned turn = ++m_writeTurn;
		m_writeTimer.expires_after(kWriteTime);
		m_writeTimer.async_wait([self = shared_from_this(), turn](const std::error_code& error) {
			if (!error && self->m_writing && turn == self->m_writeTurn)
				self->Drop();
		});
		m_stream->Write(chunk->data(), chunk->size(), [self = shared_from_this(), chunk](const std::error_code& error, std::size_t) {
			self->m_writing = false;
			++self->m_writeTurn;
			self->m_writeTimer.cancel();
			if (error) {
				self->Drop();
				return;
			}
			self->m_outBytes -= chunk->size();
			self->m_out.pop_front();
			self->Pump();
		});
	}

	//*************************************************************************
	//*                              The timer                                *
	//*************************************************************************

	// THE READING'S DEADLINE — one timer, re-armed as the connection moves on: a handshake, a request arriving, a
	// keep-alive, a WebSocket's silence. `fired` — what happens then; left out, the connection goes.
	void Arm(std::chrono::steady_clock::duration after, std::function<void()> fired = nullptr)
	{
		const unsigned turn = ++m_turn;
		m_timer.expires_after(after);
		m_timer.async_wait([self = shared_from_this(), turn, fired = std::move(fired)](const std::error_code& error) {
			if (error || turn != self->m_turn || self->m_closed)
				return;
			if (fired)
				fired();
			else
				self->Drop();
		});
	}

	void Disarm()
	{
		++m_turn;
		m_timer.cancel();
	}

	//*************************************************************************
	//*                            The WebSocket                              *
	//*************************************************************************

	bool IsAllowedOrigin(const std::string& origin) const
	{
		const std::string own = (m_secure ? "https://" : "http://") + Lower(m_request.GetHeader("host"));
		if (Lower(origin) == own)
			return true;
		for (const std::string& allowed : m_server.m_options.m_origins) {
			if (Lower(allowed) == Lower(origin))
				return true;
		}
		return false;
	}

	void Upgrade()
	{
		m_refused = true;   // whatever is answered here but the 101 ends the connection
		const auto found = m_server.m_sockets.find(m_request.m_path);
		if (m_request.m_method != "GET" || found == m_server.m_sockets.end()) {
			Answer(Plain(404, "Not Found"));
			return;
		}
		if (!HasToken(m_request.GetHeader("upgrade"), "websocket") || !HasToken(m_request.GetHeader("connection"), "upgrade")) {
			Answer(Plain(400, "Bad Request"));
			return;
		}
		if (Trim(m_request.GetHeader("sec-websocket-version")) != "13") {
			ibReply reply = Plain(426, "Upgrade Required");
			reply.m_headers.emplace_back("Sec-WebSocket-Version", "13");
			Answer(std::move(reply));
			return;
		}
		const std::string key = Trim(m_request.GetHeader("sec-websocket-key"));
		unsigned char nonce[32];
		std::size_t decoded = 0;
		if (key.size() != 24 || mbedtls_base64_decode(nonce, sizeof(nonce), &decoded,
			reinterpret_cast<const unsigned char*>(key.data()), key.size()) != 0 || decoded != 16) {
			Answer(Plain(400, "Bad Request"));
			return;
		}
		// ⭐ A PAGE OF ANOTHER SITE DOES NOT OPEN A SOCKET HERE — the browser sends the person's cookies with it,
		// and the socket would be theirs (cross-site WebSocket hijacking). Its Origin is the server's own, or one
		// web.conf names.
		const std::string origin = m_request.GetHeader("origin");
		if (!origin.empty() && !IsAllowedOrigin(origin)) {
			Answer(Plain(403, "Forbidden"));
			return;
		}

		m_accept = ibWebSocketAccept(key);
		m_work.emplace(asio::make_strand(*m_server.m_workers));
		const auto socket = std::make_shared<ibServerWebSocket>(weak_from_this(), m_request);
		const ibWebSocketOpening opening = found->second;
		asio::post(*m_work, [self = shared_from_this(), socket, opening]() {
			std::shared_ptr<ibWebSocketHandler> handler;
			try {
				handler = opening(socket);
			}
			catch (...) {
				handler.reset();
			}
			asio::post(self->m_executor, [self, handler]() { self->Opened(handler); });
		});
	}

	void Opened(const std::shared_ptr<ibWebSocketHandler>& handler)
	{
		if (m_closed) {
			// Gone while the handler was made: it is told, as any other is.
			if (handler)
				asio::post(*m_work, [self = shared_from_this(), handler]() { handler->OnClose(); });
			return;
		}
		if (!handler) {
			Answer(Plain(404, "Not Found"));
			return;
		}
		m_handler = handler;
		Queue(std::make_shared<const std::string>("HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + m_accept + "\r\n\r\n"));
		m_upgraded = true;
		m_frames.emplace(/*masked*/ true, kMaxMessageBytes);
		m_opened = m_heard = ibClock::now();
		for (auto& early : m_early)
			SendFrame(early.first, std::move(early.second));
		m_early.clear();
		Watch();

		if (!m_leftover.empty()) {
			std::string bytes;
			bytes.swap(m_leftover);
			OnFrames(bytes.data(), bytes.size());
		}
		else {
			Read();
		}
	}

	// THE SOCKET'S SILENCE, watched: a first message within kFirstMessage, an answer to a ping within kPongWithin.
	// Not while reading waits for the handler — the silence is then this side's.
	void Watch()
	{
		Arm(m_firstHeard ? kPingAfter / 2 : std::chrono::seconds(5), [this, self = shared_from_this()]() {
			const ibClock::time_point now = ibClock::now();
			if (!m_firstHeard && now - m_opened >= kFirstMessage) {
				Fail(ibWebSocketClose::Policy);
				return;
			}
			if (!m_paused && now - m_heard >= kPingAfter + kPongWithin) {
				Drop();
				return;
			}
			if (!m_paused && now - m_heard >= kPingAfter)
				SendFrame(ibWebSocketOpcode::Ping, std::string());
			Watch();
		});
	}

	void OnFrames(const char* data, std::size_t size)
	{
		m_heard = ibClock::now();
		m_frames->Feed(data, size);
		ibWebSocketFrame frame;
		std::uint16_t refusal = 0;
		while (!m_closed && !m_closing && m_frames->Next(frame, refusal)) {
			switch (frame.m_opcode) {
			case ibWebSocketOpcode::Ping:
				SendFrame(ibWebSocketOpcode::Pong, std::move(frame.m_payload));
				break;
			case ibWebSocketOpcode::Pong:
				break;
			case ibWebSocketOpcode::Close:
				OnCloseFrame(frame.m_payload);
				return;
			case ibWebSocketOpcode::Text:
			case ibWebSocketOpcode::Binary:
				if (m_inMessage) {
					Fail(ibWebSocketClose::ProtocolError);
					return;
				}
				m_messageOpcode = frame.m_opcode;
				m_message = std::move(frame.m_payload);
				m_inMessage = !frame.m_final;
				if (frame.m_final)
					Deliver();
				break;
			case ibWebSocketOpcode::Continuation:
				if (!m_inMessage) {
					Fail(ibWebSocketClose::ProtocolError);
					return;
				}
				if (m_message.size() + frame.m_payload.size() > kMaxMessageBytes) {
					Fail(ibWebSocketClose::TooBig);
					return;
				}
				m_message += frame.m_payload;
				if (frame.m_final) {
					m_inMessage = false;
					Deliver();
				}
				break;
			}
		}
		if (refusal != 0) {
			Fail(refusal);
			return;
		}
		if (m_closed || m_closing)
			return;
		if (m_queued >= kMaxQueuedMessages) {
			m_paused = true;   // reading goes on as the handler catches up (Handled)
			return;
		}
		Read();
	}

	void Deliver()
	{
		std::string message;
		message.swap(m_message);
		// Binary is the handler's to take or refuse (OnBinary): the protocol over it decides what it speaks.
		const bool binary = m_messageOpcode == ibWebSocketOpcode::Binary;
		if (!binary && !ibIsUtf8(message)) {
			Fail(ibWebSocketClose::NotText);
			return;
		}
		if (m_closeSent)
			return;   // closing: nothing more is handed on
		m_firstHeard = true;
		++m_queued;
		asio::post(*m_work, [self = shared_from_this(), handler = m_handler, binary, message = std::move(message)]() mutable {
			try {
				// A text is made the engine's here, on the worker — the one reading of its bytes past the check above.
				if (binary)
					handler->OnBinary(std::move(message));
				else
					handler->OnMessage(ibString::FromUTF8(message.data(), message.size()));
			}
			catch (...) {
			}
			asio::post(self->m_executor, [self]() { self->Handled(); });
		});
	}

	void Handled()
	{
		--m_queued;
		if (m_paused && m_queued <= kMaxQueuedMessages / 2 && !m_closed && !m_closing) {
			m_paused = false;
			m_heard = ibClock::now();
			Read();
		}
	}

	void OnCloseFrame(const std::string& payload)
	{
		if (!m_closeSent) {
			// The peer began: its code answered back, when it is one a peer may send.
			std::uint16_t code = ibWebSocketClose::Normal;
			if (payload.size() >= 2) {
				code = static_cast<std::uint16_t>((static_cast<unsigned char>(payload[0]) << 8)
					| static_cast<unsigned char>(payload[1]));
				if (code < 1000 || code > 4999 || code == 1004 || code == 1005 || code == 1006 || code == 1015)
					code = ibWebSocketClose::ProtocolError;
			}
			SendClose(code, std::string());
		}
		m_closing = true;
		m_closeWhenWritten = true;
		Pump();
	}

	// The peer broke the protocol: told why, and let go once that is written.
	void Fail(std::uint16_t code)
	{
		if (!m_closeSent)
			SendClose(code, std::string());
		m_closing = true;
		m_closeWhenWritten = true;
		Pump();
	}

	void SendFrame(std::uint8_t opcode, std::string payload)
	{
		ibWebSocketFrame frame;
		frame.m_opcode = opcode;
		frame.m_payload = std::move(payload);
		Queue(std::make_shared<const std::string>(frame.Encode(/*masked*/ false)));
	}

	void SendClose(std::uint16_t code, const std::string& reason)
	{
		std::string payload;
		payload += static_cast<char>(code >> 8);
		payload += static_cast<char>(code & 0xFF);
		payload += reason.substr(0, 123);
		SendFrame(ibWebSocketOpcode::Close, std::move(payload));
		m_closeSent = true;
	}

	//*************************************************************************
	//*                          The event stream                             *
	//*************************************************************************

	// ASKED ITS HANDLER on a worker, as a WebSocket is: its handler, or the refusal answered instead.
	void OpenStream(const ibEventStreamOpening& opening)
	{
		m_refused = true;   // whatever is answered here ends the connection: a stream's body runs until its close
		m_work.emplace(asio::make_strand(*m_server.m_workers));
		const auto stream = std::make_shared<ibServerEventStream>(weak_from_this(), m_request);
		asio::post(*m_work, [self = shared_from_this(), stream, opening]() {
			ibHttpResponse refusal;
			refusal.m_status = 404;
			refusal.SetContent("Not Found", "text/plain; charset=utf-8");
			std::shared_ptr<ibEventStreamHandler> handler;
			try {
				handler = opening(stream, refusal);
			}
			catch (...) {
				handler.reset();
				refusal = ibHttpResponse();
				refusal.m_status = 500;
			}
			asio::post(self->m_executor, [self, handler, refusal = std::move(refusal)]() mutable {
				self->StreamOpened(handler, std::move(refusal));
			});
		});
	}

	void StreamOpened(const std::shared_ptr<ibEventStreamHandler>& handler, ibHttpResponse refusal)
	{
		if (m_closed) {
			// Gone while the handler was made: it is told, as any other is.
			if (handler)
				asio::post(*m_work, [self = shared_from_this(), handler]() { handler->OnClose(); });
			return;
		}
		if (!handler) {
			Answer(ToReply(std::move(refusal)));
			return;
		}
		m_streamHandler = handler;
		Queue(std::make_shared<const std::string>("HTTP/1.1 200 OK\r\n"
			"Content-Type: text/event-stream\r\nCache-Control: no-cache\r\nTransfer-Encoding: chunked\r\n"
			"X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n"));
		m_streaming = true;
		for (const std::string& early : m_earlyEvents)
			Queue(std::make_shared<const std::string>(Chunk(EventOf(early))));
		m_earlyEvents.clear();
		Beat();
		Read();
	}

	// A LISTENER THAT IS GONE IS FOUND BY WRITING TO IT — a comment line, after a silence: a write the peer does not
	// take lets the connection go (Pump).
	void Beat()
	{
		Arm(kPingAfter, [this, self = shared_from_this()]() {
			if (m_streamEnded)
				return;
			Queue(std::make_shared<const std::string>(Chunk(":\n\n")));
			Beat();
		});
	}

	//*************************************************************************
	//*                              The end                                  *
	//*************************************************************************

	// THE CONNECTION GOES — once: the socket closed, the timers stopped, and the WebSocket's handler told, after
	// whatever it was handed before.
	void Drop()
	{
		if (m_closed)
			return;
		m_closed = true;
		Disarm();
		m_writeTimer.cancel();
		m_stream->Close();
		if (m_handler) {
			asio::post(*m_work, [self = shared_from_this(), handler = m_handler]() {
				try {
					handler->OnClose();
				}
				catch (...) {
				}
			});
		}
		if (m_streamHandler) {
			asio::post(*m_work, [self = shared_from_this(), handler = m_streamHandler]() {
				try {
					handler->OnClose();
				}
				catch (...) {
				}
			});
		}
	}

	ibHttpServerState&            m_server;
	std::unique_ptr<ibHttpStream> m_stream;
	asio::any_io_executor         m_executor;
	asio::steady_timer            m_timer;
	asio::steady_timer            m_writeTimer;
	unsigned                      m_turn = 0;
	unsigned                      m_writeTurn = 0;
	ibString                      m_address;
	bool                          m_secure;
	bool                          m_redirect;   // a page asked for in the clear beyond this machine: sent on to HTTPS
	bool                          m_closed = false;
	char                          m_buffer[kReadChunk];

	// The request being read.
	llhttp_t      m_parser;
	ibHttpRequest m_request;
	std::string   m_target;
	std::string   m_field;
	std::string   m_value;
	bool          m_hasValue = false;
	std::size_t   m_headBytes = 0;
	int           m_refusal = 0;
	bool          m_keepAlive = true;
	bool          m_refused = false;     // answered with a refusal: closed after it
	std::size_t   m_answered = 0;
	std::string   m_leftover;            // read past the request: the next one, or the WebSocket's first frames

	// What is being written, in order.
	std::deque<std::shared_ptr<const std::string>> m_out;
	std::size_t                                    m_outBytes = 0;
	bool                                           m_writing = false;
	bool                                           m_closeWhenWritten = false;

	// The WebSocket.
	bool                                                       m_upgraded = false;
	std::string                                                m_accept;
	std::optional<asio::strand<asio::thread_pool::executor_type>> m_work;   // its handler's calls, one at a time
	std::shared_ptr<ibWebSocketHandler>                        m_handler;
	std::optional<ibWebSocketFrameReader>                      m_frames;
	std::vector<std::pair<std::uint8_t, std::string>>          m_early;
	std::string                                                m_message;
	std::uint8_t                                               m_messageOpcode = ibWebSocketOpcode::Text;
	bool                                                       m_inMessage = false;
	std::size_t                                                m_queued = 0;
	bool                                                       m_paused = false;
	bool                                                       m_firstHeard = false;
	bool                                                       m_closeSent = false;
	bool                                                       m_closing = false;   // a close received or failed: nothing more is read
	ibClock::time_point                                        m_opened;
	ibClock::time_point                                        m_heard;

	// The event stream — on the WebSocket's worker strand (m_work) too.
	bool                                  m_streaming = false;     // its head sent: events go out as chunks
	bool                                  m_streamEnded = false;   // its last chunk queued
	std::shared_ptr<ibEventStreamHandler> m_streamHandler;
	std::vector<std::string>              m_earlyEvents;           // sent before its head went out
};

// Packed on the sender's thread, not the socket's.
void ibServerWebSocket::Send(const ibString& text)
{
	const std::shared_ptr<ibConnection> connection = m_connection.lock();
	if (!connection)
		return;
	std::string bytes = text.ToUtf8();
	std::string packed;
	if (m_deflate && bytes.size() >= kDeflateFrom && ibDeflate(bytes, packed))
		connection->SendMessage(ibWebSocketOpcode::Binary, std::move(packed));
	else
		connection->SendMessage(ibWebSocketOpcode::Text, std::move(bytes));
}

// …bytes the same way: nothing to encode, deflated only when the sender said they may be (ibWebSocketSend).
void ibServerWebSocket::Send(const void* data, std::size_t len, ibWebSocketSend flags)
{
	const std::shared_ptr<ibConnection> connection = m_connection.lock();
	if (!connection)
		return;
	std::string bytes(static_cast<const char*>(data), len);
	std::string packed;
	if (flags == ibWebSocketSend::Deflate && m_deflate && bytes.size() >= kDeflateFrom && ibDeflate(bytes, packed))
		connection->SendMessage(ibWebSocketOpcode::Binary, std::move(packed));
	else
		connection->SendMessage(ibWebSocketOpcode::Binary, std::move(bytes));
}

void ibServerWebSocket::Close(std::uint16_t code, const std::string& reason)
{
	if (const std::shared_ptr<ibConnection> connection = m_connection.lock())
		connection->BeginClose(code, reason);
}

void ibServerEventStream::Send(std::string data)
{
	if (const std::shared_ptr<ibConnection> connection = m_connection.lock())
		connection->SendEvent(std::move(data));
}

void ibServerEventStream::Close()
{
	if (const std::shared_ptr<ibConnection> connection = m_connection.lock())
		connection->EndStream();
}

} // namespace

//*******************************************************************************************
//*                                  Accepting connections                                  *
//*******************************************************************************************

bool ibHttpServerState::Listen(const asio::ip::address& address, std::uint16_t& port, bool secure, bool redirect,
	std::string& error)
{
	auto listener = std::make_unique<ibListener>();
	// The port on a strand of its own: its accepts, its pause and its closing (Stop) never run at once.
	const auto strand = asio::make_strand(m_io);
	listener->m_acceptor = std::make_unique<asio::ip::tcp::acceptor>(strand);
	listener->m_pause = std::make_unique<asio::steady_timer>(strand);
	listener->m_secure = secure;
	listener->m_redirect = redirect;

	const asio::ip::tcp::endpoint endpoint(address, port);
	asio::ip::tcp::acceptor& acceptor = *listener->m_acceptor;
	std::error_code code;
	acceptor.open(endpoint.protocol(), code);
	if (!code) {
#ifdef __IBMSW__
		// 🛑 ONE LISTENER PER PORT, said to the system — without it a second process binds the port this one listens on
		// and which of them a connection reaches is undefined (appserver/clientListener.cpp says the same).
		const BOOL yes = TRUE;
		::setsockopt(acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
		acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true), code);
#endif
		acceptor.bind(endpoint, code);
	}
	if (!code)
		acceptor.listen(asio::socket_base::max_listen_connections, code);
	if (code) {
		error = std::string(secure ? "HTTPS" : "HTTP") + " port " + std::to_string(port) + " on "
			+ address.to_string() + " did not open: " + ibErrorText(code);
		return false;
	}
	port = acceptor.local_endpoint(code).port();
	m_listeners.push_back(std::move(listener));
	return true;
}

void ibHttpServerState::Accept(ibListener& listener)
{
	// Each connection on a strand of its own: its handlers never run at once, on whichever I/O thread is free.
	listener.m_acceptor->async_accept(asio::any_io_executor(asio::make_strand(m_io)),
		[this, &listener](const std::error_code& error, asio::ip::tcp::socket socket) {
			if (!listener.m_acceptor->is_open())
				return;   // stopping
			if (!error) {
				Open(listener, std::move(socket));
				Accept(listener);
				return;
			}
			// Out of descriptors, or the like: a pause rather than a loop that spins on the error.
			listener.m_pause->expires_after(std::chrono::milliseconds(100));
			listener.m_pause->async_wait([this, &listener](const std::error_code& paused) {
				if (!paused && listener.m_acceptor->is_open())
					Accept(listener);
			});
		});
}

void ibHttpServerState::Open(const ibListener& listener, asio::ip::tcp::socket socket)
{
	std::error_code error;
	const asio::ip::tcp::endpoint peer = socket.remote_endpoint(error);
	if (error)
		return;   // gone already
	asio::ip::address address = peer.address();
	if (address.is_v6() && address.to_v6().is_v4_mapped())
		address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
	// The engine's text once, here: the connection, each of its requests and whoever serves them hold this one.
	const ibString name(address.to_string().c_str());
	socket.set_option(asio::ip::tcp::no_delay(true), error);   // an answer goes out at once, not after Nagle's wait

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping || m_connections.size() >= m_options.m_maxConnections)
			return;   // the socket closes as it goes out of scope
		std::size_t& fromThere = m_perAddress[name];
		if (m_options.m_maxConnectionsPerAddress != 0 && fromThere >= m_options.m_maxConnectionsPerAddress)
			return;
		++fromThere;
	}

	std::unique_ptr<ibHttpStream> stream;
	if (listener.m_secure)
		stream = std::make_unique<ibTlsStream>(std::move(socket), m_tls);
	else
		stream = std::make_unique<ibPlainStream>(std::move(socket));
	// A peer on this very machine is served in the clear wherever the port listens: its bytes cross no network.
	const auto connection = std::make_shared<ibConnection>(*this, std::move(stream), name, listener.m_secure,
		listener.m_redirect && !address.is_loopback());
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_connections[connection.get()] = connection;
	}
	connection->Start();
}

void ibHttpServerState::Release(ibConnection* connection, const ibString& address)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_connections.erase(connection);
	const auto counted = m_perAddress.find(address);
	if (counted != m_perAddress.end() && --counted->second == 0)
		m_perAddress.erase(counted);
	if (m_connections.empty())
		m_drained.notify_all();
}

//*******************************************************************************************
//*                                        The server                                       *
//*******************************************************************************************

ibHttpServer::ibHttpServer() :
	m_state(std::make_unique<ibHttpServerState>())
{
}

ibHttpServer::~ibHttpServer()
{
	Stop();
}

void ibHttpServer::Get(const std::string& path, ibHttpHandler handler)
{
	m_state->m_get[path] = std::move(handler);
}

void ibHttpServer::Post(const std::string& path, ibHttpHandler handler)
{
	m_state->m_post[path] = std::move(handler);
}

void ibHttpServer::WebSocket(const std::string& path, ibWebSocketOpening opening)
{
	m_state->m_sockets[path] = std::move(opening);
}

void ibHttpServer::EventStream(const std::string& path, ibEventStreamOpening opening)
{
	m_state->m_streams[path] = std::move(opening);
}

namespace {

// A mount among the others — the longest prefix first: /app/ before /.
void AddMount(std::vector<ibHttpServerState::ibMount>& mounts, ibHttpServerState::ibMount mount)
{
	mounts.push_back(std::move(mount));
	std::sort(mounts.begin(), mounts.end(),
		[](const ibHttpServerState::ibMount& a, const ibHttpServerState::ibMount& b) {
			return a.m_prefix.size() > b.m_prefix.size();
		});
}

} // namespace

void ibHttpServer::Mount(const std::string& prefix, const std::string& directory)
{
	std::error_code error;
	const std::filesystem::path root = std::filesystem::weakly_canonical(std::filesystem::u8path(directory), error);
	AddMount(m_state->m_mounts, ibHttpServerState::ibMount{ prefix.empty() ? std::string("/") : prefix,
		error ? std::filesystem::u8path(directory) : root, nullptr });
}

void ibHttpServer::Mount(const std::string& prefix, std::map<std::string, std::string> files)
{
	auto held = std::make_shared<std::map<std::string, ibHeldFile>>();
	for (auto& file : files) {
		std::string tag = TagOf(file.second);
		(*held)[file.first] = ibHeldFile{ std::make_shared<const std::string>(std::move(file.second)), std::move(tag) };
	}
	AddMount(m_state->m_mounts, ibHttpServerState::ibMount{ prefix.empty() ? std::string("/") : prefix,
		std::filesystem::path(), std::move(held) });
}

bool ibHttpServer::Start(const ibHttpServerOptions& options, std::string& error)
{
	ibHttpServerState& state = *m_state;
	state.m_options = options;
	// The key is the TLS context's (below): the server keeps no text of it.
	std::string& key = state.m_options.m_privateKey;
	if (!key.empty())
		mbedtls_platform_zeroize(&key[0], key.size());
	key.clear();

	// ⭐ THE CLEAR DOES NOT CROSS A NETWORK — see the header. A certificate named and not readable is a refusal,
	// never a fallback to the clear.
	if (!options.m_httpPort && !options.m_httpsPort) {
		error = "neither HTTP nor HTTPS is given a port";
		return false;
	}
	const bool named = !options.m_certificate.empty() || !options.m_privateKey.empty();
	if (options.m_httpsPort) {
		if (options.m_certificate.empty() || options.m_privateKey.empty()) {
			error = "HTTPS wants a certificate and its key, and one of them is not named";
			return false;
		}
		auto tls = std::make_shared<ibTlsContext>();
		if (!tls->Load(options.m_certificate, options.m_privateKey, error))
			return false;
		state.m_tls = std::move(tls);
	}
	else if (named) {
		error = "a certificate is named and HTTPS is given no port";
		return false;
	}
	// Where HTTP listens it serves in the clear: on this machine's own addresses, or wherever the debug server's
	// exception says (the header).
	const bool clear = ibIsLoopback(ibString::FromUTF8(options.m_host.data(), options.m_host.size()))
		|| options.m_clearBeyondMachine;
	if (options.m_httpPort && !clear && !options.m_httpsPort) {
		error = "plain HTTP serves on this machine's own addresses only (127.0.0.1, ::1, localhost); to listen on "
			+ (options.m_host.empty() ? std::string("every interface") : options.m_host)
			+ ", give HTTPS a port, a certificate and its key - HTTP there then only sends a browser on to HTTPS";
		return false;
	}

	std::error_code code;
	asio::ip::address address;
	if (options.m_host.empty())
		address = asio::ip::address_v4::any();
	else if (Lower(options.m_host) == "localhost")
		address = asio::ip::address_v4::loopback();
	else {
		std::string host = options.m_host;
		if (host.size() > 2 && host.front() == '[' && host.back() == ']')
			host = host.substr(1, host.size() - 2);
		address = asio::ip::make_address(host, code);
		if (code) {
			asio::ip::tcp::resolver resolver(state.m_io);
			const auto found = resolver.resolve(host, std::string(), code);
			if (code || found.empty()) {
				error = "'" + options.m_host + "' is not an address of this machine";
				return false;
			}
			address = found.begin()->endpoint().address();
		}
	}

	// HTTPS first: HTTP beyond this machine sends a browser on to it, and says its port.
	if (options.m_httpsPort) {
		state.m_httpsPort = *options.m_httpsPort;
		if (!state.Listen(address, state.m_httpsPort, /*secure*/ true, /*redirect*/ false, error)) {
			state.m_listeners.clear();
			return false;
		}
	}
	if (options.m_httpPort) {
		state.m_httpPort = *options.m_httpPort;
		if (!state.Listen(address, state.m_httpPort, /*secure*/ false, /*redirect*/ !clear, error)) {
			state.m_listeners.clear();
			return false;
		}
	}

	const std::size_t cores = std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
	const std::size_t threads = options.m_threads != 0 ? options.m_threads : cores;
	const std::size_t workers = options.m_workers != 0 ? options.m_workers : std::max<std::size_t>(32, cores * 4);
	state.m_workers = std::make_unique<asio::thread_pool>(workers);
	state.m_work.emplace(asio::make_work_guard(state.m_io));
	for (std::size_t i = 0; i < threads; ++i) {
		state.m_threads.emplace_back([&state]() {
			// A handler that throws is not the end of the threads that carry every connection.
			for (;;) {
				try {
					state.m_io.run();
					return;
				}
				catch (...) {
				}
			}
		});
	}
	for (const std::unique_ptr<ibHttpServerState::ibListener>& listener : state.m_listeners) {
		ibHttpServerState::ibListener* const each = listener.get();
		asio::post(state.m_io, [&state, each]() { state.Accept(*each); });
	}
	return true;
}

bool ibHttpServer::Start(ibHttpServerOptions options, unsigned int tries, std::string& error)
{
	std::optional<std::uint16_t>& port = options.m_httpsPort ? options.m_httpsPort : options.m_httpPort;
	const unsigned int first = port.value_or(0);
	const unsigned int last = std::min(first + std::max(tries, 1u) - 1, 65535u);
	for (unsigned int candidate = first; candidate <= last; ++candidate) {
		port = static_cast<std::uint16_t>(candidate);
		if (Start(options, error))
			return true;
	}
	return false;
}

void ibHttpServer::Stop()
{
	ibHttpServerState& state = *m_state;
	if (state.m_threads.empty())
		return;

	// No one more comes in…
	std::vector<std::shared_ptr<ibConnection>> open;
	{
		std::lock_guard<std::mutex> lock(state.m_mutex);
		state.m_stopping = true;
		for (const auto& entry : state.m_connections) {
			if (const std::shared_ptr<ibConnection> connection = entry.second.lock())
				open.push_back(connection);
		}
	}
	for (const std::unique_ptr<ibHttpServerState::ibListener>& listener : state.m_listeners) {
		ibHttpServerState::ibListener& closing = *listener;
		asio::post(closing.m_acceptor->get_executor(), [&closing]() {
			std::error_code ignored;
			closing.m_acceptor->close(ignored);
			closing.m_pause->cancel();
		});
	}

	// …everyone there is let go, and each WebSocket's handler told: a connection is gone when the last task that
	// holds it — its handler's OnClose among them — has run.
	for (const std::shared_ptr<ibConnection>& connection : open)
		connection->Abort();
	open.clear();
	{
		std::unique_lock<std::mutex> lock(state.m_mutex);
		state.m_drained.wait_for(lock, kStopWithin, [&state]() { return state.m_connections.empty(); });
	}

	state.m_workers->join();
	state.m_work.reset();
	state.m_io.stop();
	for (std::thread& thread : state.m_threads)
		thread.join();
	state.m_threads.clear();
}

std::uint16_t ibHttpServer::GetHttpPort() const
{
	return m_state->m_httpPort;
}

std::uint16_t ibHttpServer::GetHttpsPort() const
{
	return m_state->m_httpsPort;
}

asio::io_context& ibHttpServer::GetContext()
{
	return m_state->m_io;
}
