#include "httpClient.h"

#include <deque>
#include <vector>

#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/write.hpp>

#include <llhttp.h>

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

std::string Authority(const std::string& host, std::uint16_t port)
{
	const bool literal6 = host.find(':') != std::string::npos && host.front() != '[';
	return (literal6 ? "[" + host + "]" : host) + ":" + std::to_string(port);
}

} // namespace

//*******************************************************************************************
//*                                     The WebSocket                                       *
//*******************************************************************************************

// THE SOCKET AND WHAT IT HOLDS — Asio's, so it stays in the core: a module that holds a client includes no Asio, and
// what Asio's headers make in every file that includes them (its error categories, Winsock's start) is made here
// only. Shared: each handler holds it while it runs, and it outlives the client that let it go until they have.
struct ibWebSocketClientState : public std::enable_shared_from_this<ibWebSocketClientState> {

	using ibOpened  = ibWebSocketClient::ibOpened;
	using ibMessage = ibWebSocketClient::ibMessage;
	using ibClosed  = ibWebSocketClient::ibClosed;

	ibWebSocketClientState(asio::io_context& io, ibOpened opened, ibMessage message, ibClosed closed);

	void Open(const std::string& host, std::uint16_t port, const std::string& target);
	// A message — ibWebSocketOpcode::Text or ::Binary.
	void Send(std::uint8_t opcode, std::string payload);
	void Close();

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
	ibOpened                m_opened;
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

ibWebSocketClientState::ibWebSocketClientState(asio::io_context& io, ibOpened opened, ibMessage message, ibClosed closed) :
	m_socket(asio::make_strand(io)), m_resolver(m_socket.get_executor()), m_timer(m_socket.get_executor()),
	m_opened(std::move(opened)), m_message(std::move(message)), m_closed(std::move(closed)),
	m_frames(/*masked*/ false, kMaxAnswerBytes)
{
}

void ibWebSocketClientState::Open(const std::string& host, std::uint16_t port, const std::string& target)
{
	asio::post(m_socket.get_executor(), [self = shared_from_this(), host, port, target]() {
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
					self->Drop("'" + self->m_host + "' is not found: " + error.message());
					return;
				}
				asio::async_connect(self->m_socket, found,
					[self](const std::error_code& connected, const asio::ip::tcp::endpoint&) {
						if (connected) {
							self->Drop(Authority(self->m_host, self->m_port) + " is not reached: " + connected.message());
							return;
						}
						std::error_code ignored;
						self->m_socket.set_option(asio::ip::tcp::no_delay(true), ignored);
						self->Connected();
					});
			});
	});
}

void ibWebSocketClientState::Connected()
{
	if (m_gone)
		return;
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
	m_socket.async_read_some(asio::buffer(m_buffer, sizeof(m_buffer)),
		[self = shared_from_this()](const std::error_code& error, std::size_t bytes) {
			if (error) {
				self->Drop("the server closed the opening: " + error.message());
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
	m_socket.async_read_some(asio::buffer(m_buffer, sizeof(m_buffer)),
		[self = shared_from_this()](const std::error_code& error, std::size_t bytes) {
			if (error) {
				self->Drop(error == asio::error::eof ? std::string() : "the server's socket dropped: " + error.message());
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
				if (m_message)
					m_message(frame.m_opcode, std::move(frame.m_payload));
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
				if (m_message)
					m_message(m_assembledOpcode, std::move(whole));
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

void ibWebSocketClientState::Send(std::uint8_t opcode, std::string payload)
{
	asio::post(m_socket.get_executor(), [self = shared_from_this(), opcode, payload = std::move(payload)]() mutable {
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
	asio::post(m_socket.get_executor(), [self = shared_from_this()]() {
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
	asio::async_write(m_socket, asio::buffer(m_out.front()),
		[self = shared_from_this()](const std::error_code& error, std::size_t) {
			self->m_writing = false;
			if (error) {
				self->Drop("the server's socket dropped: " + error.message());
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
	std::error_code ignored;
	m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
	m_socket.close(ignored);
	// Told once; and what was handed over is let go here, so nothing it holds outlives the socket.
	const ibClosed closed = std::move(m_closed);
	m_closed = nullptr;
	m_opened = nullptr;
	m_message = nullptr;
	if (closed)
		closed(reason);
}

ibWebSocketClient::ibWebSocketClient(asio::io_context& io, ibOpened opened, ibMessage message, ibClosed closed) :
	m_state(std::make_shared<ibWebSocketClientState>(io, std::move(opened), std::move(message), std::move(closed)))
{
}

ibWebSocketClient::~ibWebSocketClient()
{
	m_state->Close();
}

void ibWebSocketClient::Open(const std::string& host, std::uint16_t port, const std::string& target)
{
	m_state->Open(host, port, target);
}

void ibWebSocketClient::Send(std::string text)
{
	m_state->Send(ibWebSocketOpcode::Text, std::move(text));
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

	ibPostExchange(asio::io_context& io, ibDone done) :
		m_socket(asio::make_strand(io)), m_resolver(m_socket.get_executor()), m_timer(m_socket.get_executor()),
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
		asio::post(m_socket.get_executor(), [self = shared_from_this(), host, port, wait]() {
			self->m_timer.expires_after(wait);
			self->m_timer.async_wait([self, wait](const std::error_code& error) {
				if (!error)
					self->Finish(0, "the server did not answer within " + std::to_string(wait.count()) + " s");
			});
			self->m_resolver.async_resolve(host, std::to_string(port),
				[self, host, port](const std::error_code& error, const asio::ip::tcp::resolver::results_type& found) {
					if (error) {
						self->Finish(0, "'" + host + "' is not found: " + error.message());
						return;
					}
					asio::async_connect(self->m_socket, found,
						[self, host, port](const std::error_code& connected, const asio::ip::tcp::endpoint&) {
							if (connected) {
								self->Finish(0, Authority(host, port) + " is not reached: " + connected.message());
								return;
							}
							asio::async_write(self->m_socket, asio::buffer(self->m_request),
								[self](const std::error_code& written, std::size_t) {
									if (written)
										self->Finish(0, "the request did not go: " + written.message());
									else
										self->Read();
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
		m_socket.async_read_some(asio::buffer(m_buffer, sizeof(m_buffer)),
			[self = shared_from_this()](const std::error_code& error, std::size_t bytes) {
				llhttp_errno_t code = HPE_OK;
				if (error == asio::error::eof)
					code = llhttp_finish(&self->m_parser);   // an answer that runs to the connection's end
				else if (error) {
					self->Finish(0, "the server's socket dropped: " + error.message());
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
		std::error_code ignored;
		m_socket.close(ignored);
		const ibDone done = std::move(m_done);
		m_done = nullptr;
		done(status, status != 0 ? std::move(m_body) : std::string(), error);
	}

	asio::ip::tcp::socket   m_socket;
	asio::ip::tcp::resolver m_resolver;
	asio::steady_timer      m_timer;
	ibDone                  m_done;
	std::string             m_request;
	llhttp_settings_t       m_settings;
	llhttp_t                m_parser;
	std::string             m_body;
	bool                    m_complete = false;
	char                    m_buffer[16 * 1024];
};

} // namespace

void ibHttpPost(asio::io_context& io, const std::string& host, std::uint16_t port, const std::string& target,
	std::string body, const std::string& type, std::chrono::seconds wait,
	std::function<void(int status, std::string body, const std::string& error)> done)
{
	const auto exchange = std::make_shared<ibPostExchange>(io, std::move(done));
	exchange->Start(host, port, target, std::move(body), type, wait);
}
