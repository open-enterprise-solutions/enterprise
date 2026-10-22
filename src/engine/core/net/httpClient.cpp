#include "httpClient.h"

#include <deque>
#include <optional>
#include <vector>

#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/write.hpp>

#include <llhttp.h>

#include "httpStream.h"
#include "tlsStream.h"

namespace {

// What a server may answer — a frame of a form is large, and is not refused.
constexpr std::size_t kMaxAnswerBytes = 64 * 1024 * 1024;
// The opening's head, and what is queued for a server that does not read.
constexpr std::size_t kMaxHeadBytes    = 16 * 1024;
constexpr std::size_t kMaxPendingBytes = 64 * 1024 * 1024;
// A server that does not answer the opening.
constexpr std::chrono::seconds kOpenWithin(15);

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
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// The host as a Host header writes it: an IPv6 address in brackets.
std::string Bracketed(const std::string& host)
{
	const bool literal6 = host.find(':') != std::string::npos && host.front() != '[';
	return literal6 ? "[" + host + "]" : host;
}

std::string Authority(const std::string& host, std::uint16_t port)
{
	return Bracketed(host) + ":" + std::to_string(port);
}

// THE HANDSHAKE FAILED — told apart by what the certificate's check said: a chain nobody vouches for first, the name
// after it; neither — the server does not speak TLS, or not this TLS. Why, into `why`.
ibHttpClientError HandshakeRefusal(const ibTlsStream& stream, const std::string& host, const std::error_code& error,
	std::string& why)
{
	const std::uint32_t flags = stream.GetVerification();
	if ((flags & ~static_cast<std::uint32_t>(MBEDTLS_X509_BADCERT_CN_MISMATCH)) != 0) {
		char said[512] = {};
		mbedtls_x509_crt_verify_info(said, sizeof(said), "", flags);

		// A line per failed check, each ended with '\n' — one sentence for the refusal.
		why = said;
		while (!why.empty() && why.back() == '\n')
			why.pop_back();
		for (std::size_t at = why.find('\n'); at != std::string::npos; at = why.find('\n', at))
			why.replace(at, 1, "; ");
		return ibHttpClientError::Verification;
	}
	if (flags != 0) {
		why = "the certificate is not for '" + host + "'";
		return ibHttpClientError::Hostname;
	}
	why = "the TLS handshake failed: " + ibErrorText(error);
	return ibHttpClientError::Handshake;
}

} // namespace

ibHttpClientError ibTlsClientContext(const std::string& trusted, const std::string& certificate,
	const std::string& password, bool verify, std::shared_ptr<const ibTlsContext>& context, std::string& error)
{
	auto made = std::make_shared<ibTlsContext>();
	const ibHttpClientError refused = made->LoadClient(trusted, certificate, password, verify, error);
	if (refused == ibHttpClientError::None)
		context = std::move(made);
	return refused;
}

//*******************************************************************************************
//*                                     The WebSocket                                       *
//*******************************************************************************************

// THE SOCKET AND WHAT IT HOLDS — Asio's, so it stays in the core: a module that holds a client includes no Asio, and
// what Asio's headers make in every file that includes them (its error categories, Winsock's start) is made here
// only. Shared: each handler holds it while it runs, and it outlives the client that let it go until they have.
struct ibWebSocketClientState : public std::enable_shared_from_this<ibWebSocketClientState> {

	using ibOpened  = ibWebSocketClient::ibOpened;
	using ibText    = ibWebSocketClient::ibText;
	using ibBinary  = ibWebSocketClient::ibBinary;
	using ibClosed  = ibWebSocketClient::ibClosed;

	ibWebSocketClientState(asio::io_context& io, ibOpened opened, ibText text, ibBinary binary, ibClosed closed);

	void Open(const std::string& host, std::uint16_t port, const std::string& target);
	// A message — ibWebSocketOpcode::Text or ::Binary.
	void Send(std::uint8_t opcode, std::string payload);
	void Close();

	// The connection made: its bytes go by the stream from here — TLS's handshake first, when it is secure.
	void Connected(asio::ip::tcp::socket socket);
	void Upgrade();
	void ReadHead();
	void Opened(std::string leftover);
	void Read();
	void OnFrames(const char* data, std::size_t size);
	// A message whole, to whoever takes its kind — a text made the engine's.
	void Deliver(std::uint8_t opcode, std::string payload);
	void SendFrame(std::uint8_t opcode, std::string payload);
	void Queue(std::string bytes);
	void Pump();
	void Drop(const std::string& reason);

	asio::strand<asio::io_context::executor_type> m_strand;   // the socket's, the resolver's, the timer's
	asio::ip::tcp::resolver m_resolver;
	asio::steady_timer      m_timer;    // the opening's deadline
	std::optional<asio::ip::tcp::socket> m_connecting;   // until it is connected…
	std::unique_ptr<ibHttpStream>        m_stream;       // …and its bytes from then on: plain, or TLS over m_tls
	std::shared_ptr<const ibTlsContext>  m_tls;          // null — in the clear
	ibOpened                m_opened;
	ibText                  m_text;
	ibBinary                m_binary;
	ibClosed                m_closed;
	std::string             m_host;
	std::uint16_t           m_port = 0;
	std::string             m_target;
	std::string             m_key;

	bool                    m_open = false;
	bool                    m_gone = false;
	bool                    m_closeSent = false;
	bool                    m_closeWhenWritten = false;
	std::vector<std::pair<std::uint8_t, std::string>> m_waiting;   // sent before it opened, each with its opcode
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

ibWebSocketClientState::ibWebSocketClientState(asio::io_context& io, ibOpened opened, ibText text, ibBinary binary,
	ibClosed closed) :
	m_strand(asio::make_strand(io)), m_resolver(m_strand), m_timer(m_strand),
	m_opened(std::move(opened)), m_text(std::move(text)), m_binary(std::move(binary)), m_closed(std::move(closed)),
	m_frames(/*masked*/ false, kMaxAnswerBytes)
{
}

void ibWebSocketClientState::Open(const std::string& host, std::uint16_t port, const std::string& target)
{
	asio::post(m_strand, [self = shared_from_this(), host, port, target]() {
		self->m_host = host;
		self->m_port = port;
		self->m_target = target;
		self->m_timer.expires_after(kOpenWithin);
		self->m_timer.async_wait([self](const std::error_code& error) {
			if (!error && !self->m_open)
				self->Drop("the server did not answer within " + std::to_string(kOpenWithin.count()) + " s");
		});
		self->m_resolver.async_resolve(host, std::to_string(port),
			[self](const std::error_code& error, const asio::ip::tcp::resolver::results_type& found) {
				if (error) {
					self->Drop("'" + self->m_host + "' is not found: " + ibErrorText(error));
					return;
				}
				if (self->m_gone)
					return;
				self->m_connecting.emplace(self->m_strand);
				asio::async_connect(*self->m_connecting, found,
					[self](const std::error_code& connected, const asio::ip::tcp::endpoint&) {
						if (connected) {
							self->Drop(Authority(self->m_host, self->m_port) + " is not reached: " + ibErrorText(connected));
							return;
						}
						asio::ip::tcp::socket socket = std::move(*self->m_connecting);
						self->m_connecting.reset();
						self->Connected(std::move(socket));
					});
			});
	});
}

void ibWebSocketClientState::Connected(asio::ip::tcp::socket socket)
{
	if (m_gone)
		return;
	std::error_code ignored;
	socket.set_option(asio::ip::tcp::no_delay(true), ignored);
	if (m_tls)
		m_stream = std::make_unique<ibTlsStream>(std::move(socket), m_tls, m_host);
	else
		m_stream = std::make_unique<ibPlainStream>(std::move(socket));
	m_stream->Handshake([self = shared_from_this()](const std::error_code& shaken) {
		if (self->m_gone)
			return;
		if (shaken) {
			std::string why;
			HandshakeRefusal(static_cast<const ibTlsStream&>(*self->m_stream), self->m_host, shaken, why);
			self->Drop(why);
			return;
		}
		self->Upgrade();
	});
}

void ibWebSocketClientState::Upgrade()
{
	m_key = ibWebSocketKey();
	Queue("GET " + m_target + " HTTP/1.1\r\n"
		"Host: " + Authority(m_host, m_port) + "\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: " + m_key + "\r\n"
		"Sec-WebSocket-Version: 13\r\n\r\n");
	ReadHead();
}

void ibWebSocketClientState::ReadHead()
{
	m_stream->ReadSome(m_buffer, sizeof(m_buffer),
		[self = shared_from_this()](const std::error_code& error, std::size_t bytes) {
			if (error) {
				self->Drop("the server closed the opening: " + ibErrorText(error));
				return;
			}
			self->m_head.append(self->m_buffer, bytes);
			const std::size_t end = self->m_head.find("\r\n\r\n");
			if (end == std::string::npos) {
				if (self->m_head.size() > kMaxHeadBytes)
					self->Drop("the server's answer to the opening is not HTTP");
				else
					self->ReadHead();
				return;
			}

			// The server's own answer, as RFC 6455 § 4.1 has a client check it: 101, the upgrade, our key's accept.
			const std::string head = self->m_head.substr(0, end);
			std::string leftover = self->m_head.substr(end + 4);
			self->m_head.clear();
			const std::size_t lineEnd = head.find("\r\n");
			const std::string status = head.substr(0, lineEnd);
			if (status.compare(0, 9, "HTTP/1.1 ") != 0 || status.compare(9, 3, "101") != 0) {
				self->Drop("the server refused the socket: " + status);
				return;
			}
			std::string upgrade, accept;
			std::size_t from = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
			while (from < head.size()) {
				std::size_t next = head.find("\r\n", from);
				if (next == std::string::npos)
					next = head.size();
				const std::string line = head.substr(from, next - from);
				const std::size_t colon = line.find(':');
				if (colon != std::string::npos) {
					const std::string name = Lower(Trim(line.substr(0, colon)));
					if (name == "upgrade")
						upgrade = Lower(Trim(line.substr(colon + 1)));
					else if (name == "sec-websocket-accept")
						accept = Trim(line.substr(colon + 1));
				}
				from = next + 2;
			}
			if (upgrade != "websocket" || accept != ibWebSocketAccept(self->m_key)) {
				self->Drop("the server's answer to the opening is not a WebSocket's");
				return;
			}
			self->Opened(std::move(leftover));
		});
}

void ibWebSocketClientState::Opened(std::string leftover)
{
	m_open = true;
	m_timer.cancel();
	// Told once; and let go of, as the other handlers are when the socket goes.
	const ibOpened opened = std::move(m_opened);
	m_opened = nullptr;
	if (opened)
		opened();
	for (std::pair<std::uint8_t, std::string>& waiting : m_waiting)
		SendFrame(waiting.first, std::move(waiting.second));
	m_waiting.clear();
	if (!leftover.empty())
		OnFrames(leftover.data(), leftover.size());
	else
		Read();
}

void ibWebSocketClientState::Read()
{
	m_stream->ReadSome(m_buffer, sizeof(m_buffer),
		[self = shared_from_this()](const std::error_code& error, std::size_t bytes) {
			if (error) {
				self->Drop(error == asio::error::eof ? std::string() : "the server's socket dropped: " + ibErrorText(error));
				return;
			}
			self->OnFrames(self->m_buffer, bytes);
		});
}

void ibWebSocketClientState::OnFrames(const char* data, std::size_t size)
{
	if (m_gone)
		return;
	m_frames.Feed(data, size);
	ibWebSocketFrame frame;
	std::uint16_t refusal = 0;
	while (!m_gone && m_frames.Next(frame, refusal)) {
		switch (frame.m_opcode) {
		case ibWebSocketOpcode::Ping:
			SendFrame(ibWebSocketOpcode::Pong, std::move(frame.m_payload));
			break;
		case ibWebSocketOpcode::Pong:
			break;
		case ibWebSocketOpcode::Close:
			Drop(std::string());   // the server closed it
			return;
		case ibWebSocketOpcode::Text:
		case ibWebSocketOpcode::Binary:
			if (m_assembling) {
				Drop("the server broke the WebSocket protocol");
				return;
			}
			if (frame.m_final) {
				Deliver(frame.m_opcode, std::move(frame.m_payload));
				break;
			}
			m_assembling = true;
			m_assembledOpcode = frame.m_opcode;
			m_assembled = std::move(frame.m_payload);
			break;
		case ibWebSocketOpcode::Continuation:
			if (!m_assembling || m_assembled.size() + frame.m_payload.size() > kMaxAnswerBytes) {
				Drop("the server broke the WebSocket protocol");
				return;
			}
			m_assembled += frame.m_payload;
			if (frame.m_final) {
				m_assembling = false;
				std::string whole;
				whole.swap(m_assembled);
				Deliver(m_assembledOpcode, std::move(whole));
			}
			break;
		}
	}
	if (refusal != 0) {
		Drop("the server broke the WebSocket protocol");
		return;
	}
	if (!m_gone)
		Read();
}

void ibWebSocketClientState::Deliver(std::uint8_t opcode, std::string payload)
{
	if (opcode == ibWebSocketOpcode::Binary) {
		if (m_binary)
			m_binary(std::move(payload));
	}
	else if (m_text) {
		m_text(ibString::FromUTF8(payload.data(), payload.size()));
	}
}

void ibWebSocketClientState::Send(std::uint8_t opcode, std::string payload)
{
	asio::post(m_strand, [self = shared_from_this(), opcode, payload = std::move(payload)]() mutable {
		if (self->m_gone || self->m_closeSent)
			return;
		if (!self->m_open) {
			self->m_waiting.emplace_back(opcode, std::move(payload));
			return;
		}
		self->SendFrame(opcode, std::move(payload));
	});
}

void ibWebSocketClientState::Close()
{
	asio::post(m_strand, [self = shared_from_this()]() {
		if (self->m_gone || self->m_closeSent)
			return;
		self->m_closeSent = true;
		if (!self->m_open) {
			// Still opening — nothing has been said on it yet, and nothing will be.
			self->Drop(std::string());
			return;
		}
		self->SendFrame(ibWebSocketOpcode::Close, std::string("\x03\xE8", 2));   // 1000, normal
		self->m_closeWhenWritten = true;
		self->Pump();
	});
}

void ibWebSocketClientState::SendFrame(std::uint8_t opcode, std::string payload)
{
	ibWebSocketFrame frame;
	frame.m_opcode = opcode;
	frame.m_payload = std::move(payload);
	Queue(frame.Encode(/*masked*/ true));
}

void ibWebSocketClientState::Queue(std::string bytes)
{
	if (m_gone)
		return;
	m_outBytes += bytes.size();
	if (m_outBytes > kMaxPendingBytes) {
		Drop("the server does not read what it is sent");
		return;
	}
	m_out.push_back(std::move(bytes));
	Pump();
}

void ibWebSocketClientState::Pump()
{
	if (m_writing || m_gone)
		return;
	if (m_out.empty()) {
		if (m_closeWhenWritten)
			Drop(std::string());
		return;
	}
	m_writing = true;
	// The front stays where it is while it is written: a deque's push_back moves no element.
	m_stream->Write(m_out.front().data(), m_out.front().size(),
		[self = shared_from_this()](const std::error_code& error, std::size_t) {
			self->m_writing = false;
			if (error) {
				self->Drop("the server's socket dropped: " + ibErrorText(error));
				return;
			}
			self->m_outBytes -= self->m_out.front().size();
			self->m_out.pop_front();
			self->Pump();
		});
}

void ibWebSocketClientState::Drop(const std::string& reason)
{
	if (m_gone)
		return;
	m_gone = true;
	m_timer.cancel();
	m_resolver.cancel();
	// Closed, not let go: what is still outstanding on the stream is told, and finds the socket gone.
	std::error_code ignored;
	if (m_connecting)
		m_connecting->close(ignored);
	if (m_stream)
		m_stream->Close();
	// Told once; and what was handed over is let go here, so nothing it holds outlives the socket.
	const ibClosed closed = std::move(m_closed);
	m_closed = nullptr;
	m_opened = nullptr;
	m_text = nullptr;
	m_binary = nullptr;
	if (closed)
		closed(reason);
}

ibWebSocketClient::ibWebSocketClient(asio::io_context& io, ibOpened opened, ibText text, ibBinary binary,
	ibClosed closed) :
	m_state(std::make_shared<ibWebSocketClientState>(io, std::move(opened), std::move(text), std::move(binary),
		std::move(closed)))
{
}

ibWebSocketClient::~ibWebSocketClient()
{
	m_state->Close();
}

void ibWebSocketClient::Secure(std::shared_ptr<const ibTlsContext> context)
{
	// Read by the strand only once Open has posted to it: said before Open, as the header asks.
	m_state->m_tls = std::move(context);
}

void ibWebSocketClient::Open(const std::string& host, std::uint16_t port, const std::string& target)
{
	m_state->Open(host, port, target);
}

void ibWebSocketClient::Send(const ibString& text)
{
	m_state->Send(ibWebSocketOpcode::Text, text.ToUtf8());
}

void ibWebSocketClient::SendBinary(std::string data)
{
	m_state->Send(ibWebSocketOpcode::Binary, std::move(data));
}

void ibWebSocketClient::Close()
{
	m_state->Close();
}

//*******************************************************************************************
//*                                        The POST                                         *
//*******************************************************************************************

namespace {

class ibPostExchange : public std::enable_shared_from_this<ibPostExchange> {
public:

	using ibDone = std::function<void(int status, std::string body, const std::string& error)>;

	ibPostExchange(asio::io_context& io, std::shared_ptr<const ibTlsContext> tls, ibDone done) :
		m_strand(asio::make_strand(io)), m_resolver(m_strand), m_timer(m_strand), m_tls(std::move(tls)),
		m_done(std::move(done))
	{
		llhttp_settings_init(&m_settings);
		m_settings.on_body = &ibPostExchange::OnBody;
		m_settings.on_message_complete = &ibPostExchange::OnMessageComplete;
		llhttp_init(&m_parser, HTTP_RESPONSE, &m_settings);
		m_parser.data = this;
	}

	void Start(const std::string& host, std::uint16_t port, const std::string& target, std::string body,
		const std::string& type, std::chrono::seconds wait)
	{
		m_request = "POST " + target + " HTTP/1.1\r\n"
			"Host: " + Authority(host, port) + "\r\n"
			"Content-Type: " + type + "\r\n"
			"Content-Length: " + std::to_string(body.size()) + "\r\n"
			"Connection: close\r\n\r\n" + body;
		asio::post(m_strand, [self = shared_from_this(), host, port, wait]() {
			self->m_timer.expires_after(wait);
			self->m_timer.async_wait([self, wait](const std::error_code& error) {
				if (!error)
					self->Finish(0, "the server did not answer within " + std::to_string(wait.count()) + " s");
			});
			self->m_resolver.async_resolve(host, std::to_string(port),
				[self, host, port](const std::error_code& error, const asio::ip::tcp::resolver::results_type& found) {
					if (error) {
						self->Finish(0, "'" + host + "' is not found: " + ibErrorText(error));
						return;
					}
					if (!self->m_done)
						return;
					self->m_connecting.emplace(self->m_strand);
					asio::async_connect(*self->m_connecting, found,
						[self, host, port](const std::error_code& connected, const asio::ip::tcp::endpoint&) {
							if (connected) {
								self->Finish(0, Authority(host, port) + " is not reached: " + ibErrorText(connected));
								return;
							}
							if (!self->m_done)
								return;
							if (self->m_tls)
								self->m_stream = std::make_unique<ibTlsStream>(std::move(*self->m_connecting), self->m_tls, host);
							else
								self->m_stream = std::make_unique<ibPlainStream>(std::move(*self->m_connecting));
							self->m_connecting.reset();
							self->m_stream->Handshake([self, host](const std::error_code& shaken) {
								if (!self->m_done)
									return;
								if (shaken) {
									std::string why;
									HandshakeRefusal(static_cast<const ibTlsStream&>(*self->m_stream), host, shaken, why);
									self->Finish(0, why);
									return;
								}
								self->m_stream->Write(self->m_request.data(), self->m_request.size(),
									[self](const std::error_code& written, std::size_t) {
										if (written)
											self->Finish(0, "the request did not go: " + ibErrorText(written));
										else
											self->Read();
									});
							});
						});
				});
		});
	}

private:

	static int OnBody(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibPostExchange& self = *static_cast<ibPostExchange*>(parser->data);
		if (self.m_body.size() + size > kMaxAnswerBytes)
			return -1;
		self.m_body.append(at, size);
		return 0;
	}

	static int OnMessageComplete(llhttp_t* parser)
	{
		static_cast<ibPostExchange*>(parser->data)->m_complete = true;
		return HPE_PAUSED;
	}

	void Read()
	{
		m_stream->ReadSome(m_buffer, sizeof(m_buffer),
			[self = shared_from_this()](const std::error_code& error, std::size_t bytes) {
				if (!self->m_done)
					return;
				llhttp_errno_t code = HPE_OK;
				if (error == asio::error::eof)
					code = llhttp_finish(&self->m_parser);   // an answer that runs to the connection's end
				else if (error) {
					self->Finish(0, "the server's socket dropped: " + ibErrorText(error));
					return;
				}
				else
					code = llhttp_execute(&self->m_parser, self->m_buffer, bytes);
				if (self->m_complete) {
					self->Finish(llhttp_get_status_code(&self->m_parser), std::string());
					return;
				}
				if (code != HPE_OK || error) {
					self->Finish(0, "the server's answer is not HTTP");
					return;
				}
				self->Read();
			});
	}

	void Finish(int status, const std::string& error)
	{
		if (!m_done)
			return;
		m_timer.cancel();
		m_resolver.cancel();
		// Closed, not let go: what is still outstanding on the stream is told, and finds the exchange finished.
		std::error_code ignored;
		if (m_connecting)
			m_connecting->close(ignored);
		if (m_stream)
			m_stream->Close();
		const ibDone done = std::move(m_done);
		m_done = nullptr;
		done(status, status != 0 ? std::move(m_body) : std::string(), error);
	}

	asio::strand<asio::io_context::executor_type> m_strand;   // the socket's, the resolver's, the timer's
	asio::ip::tcp::resolver              m_resolver;
	asio::steady_timer                   m_timer;
	std::optional<asio::ip::tcp::socket> m_connecting;   // until it is connected…
	std::unique_ptr<ibHttpStream>        m_stream;       // …and its bytes from then on: plain, or TLS over m_tls
	std::shared_ptr<const ibTlsContext>  m_tls;          // null — in the clear
	ibDone                  m_done;
	std::string             m_request;
	llhttp_settings_t       m_settings;
	llhttp_t                m_parser;
	std::string             m_body;
	bool                    m_complete = false;
	char                    m_buffer[16 * 1024];
};

} // namespace

void ibHttpPost(asio::io_context& io, const std::string& host, std::uint16_t port,
	std::shared_ptr<const ibTlsContext> tls, const std::string& target, std::string body, const std::string& type,
	std::chrono::seconds wait, std::function<void(int status, std::string body, const std::string& error)> done)
{
	const auto exchange = std::make_shared<ibPostExchange>(io, std::move(tls), std::move(done));
	exchange->Start(host, port, target, std::move(body), type, wait);
}

//*******************************************************************************************
//*                                       The client                                        *
//*******************************************************************************************

// ONE CLIENT'S I/O — on its own context, run by the caller's thread for as long as a request is allowed. Every handler
// carries the turn of the request it was started for, and does nothing for another.
struct ibHttpClientState {

	ibHttpClientState(const std::string& host, std::uint16_t port) :
		m_host(host), m_port(port), m_resolver(m_io)
	{
		llhttp_settings_init(&m_settings);
		m_settings.on_header_field     = &ibHttpClientState::OnHeaderField;
		m_settings.on_header_value     = &ibHttpClientState::OnHeaderValue;
		m_settings.on_headers_complete = &ibHttpClientState::OnHeadersComplete;
		m_settings.on_body             = &ibHttpClientState::OnBody;
		m_settings.on_message_complete = &ibHttpClientState::OnMessageComplete;
	}

	// A KEPT-ALIVE CONNECTION THE SERVER CLOSED MEANWHILE is let go before it is written to: its end would be read as
	// the end of the next answer. Asked of the socket without waiting — nothing to read, and not closed: alive.
	bool IsAlive()
	{
		asio::ip::tcp::socket& socket = m_stream->GetSocket();
		if (!socket.is_open())
			return false;
		std::error_code code;
		socket.non_blocking(true, code);
		char probe = 0;
		socket.receive(asio::buffer(&probe, 1), asio::socket_base::message_peek, code);
		std::error_code ignored;
		socket.non_blocking(false, ignored);
		return code == asio::error::would_block;
	}

	// The connection let go — kept until what it still has outstanding has been told (Drain).
	void LetGo()
	{
		if (!m_stream)
			return;
		m_stream->Close();
		m_dropped.push_back(std::move(m_stream));
	}

	// What a closed socket and a cancelled lookup still owe their handlers, run here: each finds a request finished.
	void Drain()
	{
		std::error_code ignored;
		m_resolver.cancel();
		if (m_connecting)
			m_connecting->close(ignored);
		m_io.restart();
		m_io.poll();
		m_connecting.reset();
		m_dropped.clear();
	}

	void Begin(const ibHttpRequest& request, ibHttpResponse& response)
	{
		++m_turn;
		m_finished = false;
		m_result = ibHttpClientError::None;
		m_why.clear();
		m_response = &response;
		m_head = request.m_method == "HEAD";
		m_field.clear();
		m_value.clear();
		m_hasValue = false;
		m_answerBytes = 0;
		m_complete = false;
		m_tooLarge = false;
		m_keep = false;
		llhttp_init(&m_parser, HTTP_RESPONSE, &m_settings);
		m_parser.data = this;

		const auto has = [&request](const std::string& name) {
			for (const auto& header : request.m_headers) {
				if (Lower(header.first) == name)
					return true;
			}
			return false;
		};
		// The port a scheme is spoken on by itself is not written: a server behind a name says it is that name.
		const bool usual = m_port == (m_tls ? 443 : 80);
		const std::string& method = request.m_method;
		m_out = method + " " + (request.m_path.empty() ? std::string("/") : request.m_path)
			+ (request.m_query.empty() ? std::string() : "?" + request.m_query) + " HTTP/1.1\r\n"
			"Host: " + (usual ? Bracketed(m_host) : Authority(m_host, m_port)) + "\r\n";
		for (const auto& header : request.m_headers)
			m_out += header.first + ": " + header.second + "\r\n";
		if (!has("accept"))
			m_out += "Accept: */*\r\n";
		if (!has("user-agent"))
			m_out += "User-Agent: OES-Enterprise\r\n";
		// A body nobody named the type of goes as text, as it always went.
		if (!request.m_body.empty() && !has("content-type"))
			m_out += "Content-Type: text/plain\r\n";
		if (!request.m_body.empty() || method == "POST" || method == "PUT" || method == "PATCH")
			m_out += "Content-Length: " + std::to_string(request.m_body.size()) + "\r\n";
		m_out += "\r\n";
		m_out += request.m_body;

		if (m_stream)
			Write();
		else
			Connect();
	}

	void Connect()
	{
		const unsigned turn = m_turn;
		m_resolver.async_resolve(m_host, std::to_string(m_port),
			[this, turn](const std::error_code& error, const asio::ip::tcp::resolver::results_type& found) {
				if (turn != m_turn || m_finished)
					return;
				if (error) {
					Finish(ibHttpClientError::Connection, "'" + m_host + "' is not found: " + ibErrorText(error));
					return;
				}
				m_connecting.emplace(m_io);
				asio::async_connect(*m_connecting, found, [this, turn](const std::error_code& connected, const asio::ip::tcp::endpoint&) {
					if (turn != m_turn || m_finished)
						return;
					if (connected) {
						Finish(ibHttpClientError::Connection, Authority(m_host, m_port) + " is not reached: " + ibErrorText(connected));
						return;
					}
					std::error_code ignored;
					m_connecting->set_option(asio::ip::tcp::no_delay(true), ignored);
					if (m_tls)
						m_stream = std::make_unique<ibTlsStream>(std::move(*m_connecting), m_tls, m_host);
					else
						m_stream = std::make_unique<ibPlainStream>(std::move(*m_connecting));
					m_connecting.reset();
					m_stream->Handshake([this, turn](const std::error_code& shaken) {
						if (turn != m_turn || m_finished)
							return;
						if (shaken)
							Refused(shaken);
						else
							Write();
					});
				});
			});
	}

	void Refused(const std::error_code& error)
	{
		std::string why;
		const ibHttpClientError refused = HandshakeRefusal(static_cast<const ibTlsStream&>(*m_stream), m_host, error, why);
		Finish(refused, std::move(why));
	}

	void Write()
	{
		const unsigned turn = m_turn;
		m_stream->Write(m_out.data(), m_out.size(), [this, turn](const std::error_code& error, std::size_t) {
			if (turn != m_turn || m_finished)
				return;
			if (error) {
				Finish(ibHttpClientError::Write, "the request did not go whole: " + ibErrorText(error));
				return;
			}
			Read();
		});
	}

	void Read()
	{
		const unsigned turn = m_turn;
		m_stream->ReadSome(m_buffer, sizeof(m_buffer), [this, turn](const std::error_code& error, std::size_t bytes) {
			if (turn != m_turn || m_finished)
				return;
			llhttp_errno_t code = HPE_OK;
			if (error == asio::error::eof)
				code = llhttp_finish(&m_parser);   // an answer that runs to the connection's end
			else if (error) {
				Finish(ibHttpClientError::Read, "the connection was lost: " + ibErrorText(error));
				return;
			}
			else
				code = llhttp_execute(&m_parser, m_buffer, bytes);
			if (m_complete) {
				// Bytes past the answer answer nothing that was asked: that connection is not spoken over again.
				const char* const stop = llhttp_get_error_pos(&m_parser);
				const bool past = !error && stop != nullptr && stop < m_buffer + bytes;
				m_keep = !error && !past && llhttp_should_keep_alive(&m_parser) != 0;
				Finish(ibHttpClientError::None, std::string());
				return;
			}
			if (m_tooLarge) {
				Finish(ibHttpClientError::Answer, "the answer is larger than " + std::to_string(ibHttpClient::kMaxAnswerBytes) + " bytes");
				return;
			}
			if (error) {
				Finish(ibHttpClientError::Read, "the connection was closed before the answer was whole");
				return;
			}
			if (code != HPE_OK) {
				Finish(ibHttpClientError::Answer, "the server's answer is not HTTP");
				return;
			}
			Read();
		});
	}

	void Finish(ibHttpClientError result, std::string why)
	{
		if (m_finished)
			return;
		m_finished = true;
		m_result = result;
		m_why = std::move(why);
		if (result == ibHttpClientError::None)
			m_response->m_status = llhttp_get_status_code(&m_parser);
		if (result != ibHttpClientError::None || !m_keep)
			LetGo();
		m_io.stop();
	}

	static ibHttpClientState& Of(llhttp_t* parser) { return *static_cast<ibHttpClientState*>(parser->data); }

	// Counted against the most an answer may be, its head with it; past it — the answer refused.
	bool Count(std::size_t size)
	{
		m_answerBytes += size;
		m_tooLarge = m_answerBytes > ibHttpClient::kMaxAnswerBytes;
		return !m_tooLarge;
	}

	void CommitHeader()
	{
		if (m_field.empty() && !m_hasValue)
			return;
		m_response->m_headers.emplace_back(m_field, Trim(m_value));
		m_field.clear();
		m_value.clear();
		m_hasValue = false;
	}

	static int OnHeaderField(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibHttpClientState& self = Of(parser);
		if (!self.Count(size))
			return -1;
		if (self.m_hasValue)
			self.CommitHeader();
		self.m_field.append(at, size);
		return 0;
	}

	static int OnHeaderValue(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibHttpClientState& self = Of(parser);
		if (!self.Count(size))
			return -1;
		self.m_value.append(at, size);
		self.m_hasValue = true;
		return 0;
	}

	static int OnHeadersComplete(llhttp_t* parser)
	{
		ibHttpClientState& self = Of(parser);
		self.CommitHeader();
		return self.m_head ? 1 : 0;   // 1 — the answer to a HEAD has no body, whatever its length says
	}

	static int OnBody(llhttp_t* parser, const char* at, std::size_t size)
	{
		ibHttpClientState& self = Of(parser);
		if (!self.Count(size))
			return -1;
		self.m_response->m_body.append(at, size);
		return 0;
	}

	static int OnMessageComplete(llhttp_t* parser)
	{
		Of(parser).m_complete = true;
		return HPE_PAUSED;
	}

	asio::io_context                           m_io;
	std::string                                m_host;
	std::uint16_t                              m_port;
	asio::ip::tcp::resolver                    m_resolver;
	std::optional<asio::ip::tcp::socket>       m_connecting;
	std::shared_ptr<const ibTlsContext>        m_tls;        // null — in the clear
	std::unique_ptr<ibHttpStream>              m_stream;     // the connection kept alive; null — none yet
	std::vector<std::unique_ptr<ibHttpStream>> m_dropped;    // let go, until their handlers are told

	// The request under way.
	unsigned          m_turn = 0;
	bool              m_finished = false;
	ibHttpClientError m_result = ibHttpClientError::None;
	std::string       m_why;
	std::string       m_out;
	ibHttpResponse*   m_response = nullptr;
	bool              m_head = false;
	llhttp_settings_t m_settings;
	llhttp_t          m_parser;
	std::string       m_field;
	std::string       m_value;
	bool              m_hasValue = false;
	std::size_t       m_answerBytes = 0;
	bool              m_complete = false;
	bool              m_tooLarge = false;
	bool              m_keep = false;
	char              m_buffer[16 * 1024];
};

ibHttpClient::ibHttpClient(const std::string& host, std::uint16_t port) :
	m_state(std::make_unique<ibHttpClientState>(host, port))
{
}

ibHttpClient::~ibHttpClient()
{
	m_state->LetGo();
	m_state->Drain();
}

ibHttpClientError ibHttpClient::Secure(const std::string& trusted, const std::string& certificate,
	const std::string& password, bool verify, std::string& error)
{
	std::shared_ptr<const ibTlsContext> context;
	const ibHttpClientError refused = ibTlsClientContext(trusted, certificate, password, verify, context, error);
	if (refused != ibHttpClientError::None)
		return refused;
	// A connection made in the clear is not the one spoken over TLS.
	m_state->LetGo();
	m_state->Drain();
	m_state->m_tls = std::move(context);
	return ibHttpClientError::None;
}

ibHttpClientError ibHttpClient::Send(const ibHttpRequest& request, ibHttpResponse& response, std::chrono::seconds wait,
	std::string& error)
{
	ibHttpClientState& state = *m_state;
	response = ibHttpResponse();
	if (state.m_stream && !state.IsAlive())
		state.LetGo();

	state.m_io.restart();
	state.Begin(request, response);
	state.m_io.run_until(std::chrono::steady_clock::now() + wait);
	if (!state.m_finished)
		state.Finish(ibHttpClientError::Timeout, "nobody answered within " + std::to_string(wait.count()) + " s");
	state.Drain();

	if (state.m_result != ibHttpClientError::None) {
		response = ibHttpResponse();
		error = state.m_why;
	}
	return state.m_result;
}
