#include "connectionServer.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

#include "core/localization/dictionary.h"   // _() — what a refusal says to a person

#include "core/fileSystem.h"           // ibPathOf — the trusted certificates' file

#include "core/net/httpClient.h"       // ws:// — and wss://, the client's TLS (ibTlsClientContext)
#include "core/net/ioThreads.h"
#include "core/net/ports.h"            // ibPort::Clients — the port an address leaves out
#include "core/net/webSocketFrame.h"   // ibInflate — a packet of some size comes deflated
#include "core/serialize/dataBuilder.h"   // ibBinaryProvider::IsPacket — a packet told from a deflated one

namespace {

// How long Close waits for the server to be told the socket is closing, before its thread is stopped under it.
constexpr std::chrono::seconds kCloseWithin(1);

// A base's name in the path — its UTF-8 bytes, every one but the unreserved escaped.
std::string EscapeSegment(const ibString& segment)
{
	static const char s_hex[] = "0123456789ABCDEF";
	const std::string utf8 = segment.utf8_str();
	std::string escaped;
	for (std::size_t i = 0; i < utf8.length(); ++i) {
		const unsigned char c = static_cast<unsigned char>(utf8.data()[i]);
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
			|| c == '-' || c == '.' || c == '_' || c == '~') {
			escaped += static_cast<char>(c);
		}
		else {
			escaped += '%';
			escaped += s_hex[c >> 4];
			escaped += s_hex[c & 0x0F];
		}
	}
	return escaped;
}

} // namespace

bool ibProtocolConnectionServer::ParseAddress(const ibString& address, ibString& host, int& port, ibString& base,
	bool& secure)
{
	ibString rest;
	secure = address.StartsWith(ibT("oess://"), &rest);
	if (!secure && !address.StartsWith(ibT("oes://"), &rest))
		return false;
	const int slash = rest.Find(ibT('/'));
	if (slash == wxNOT_FOUND)
		return false;
	ibString authority = rest.Left(slash);
	base = rest.Mid(slash + 1);
	if (base.EndsWith(ibT("/")))
		base.RemoveLast();

	port = ibPort::Clients.m_first;
	const int colon = authority.Find(ibT(':'), true);
	if (colon != wxNOT_FOUND && !authority.EndsWith(ibT("]"))) {
		long number = 0;
		if (!authority.Mid(colon + 1).ToLong(&number) || number <= 0 || number > 65535)
			return false;
		port = static_cast<int>(number);
		authority = authority.Left(colon);
	}
	host = authority;
	return !host.IsEmpty() && !base.IsEmpty() && base.Find(ibT('/')) == wxNOT_FOUND;
}

std::string ibProtocolConnectionServer::ClientPath(const ibString& base)
{
	return "/" + EscapeSegment(base) + "/client";
}

bool ibProtocolConnectionServer::ClientTls(const ibString& trusted, std::shared_ptr<const ibTlsContext>& tls,
	ibString& error)
{
	std::string pem, refused;
	if (!trusted.IsEmpty()) {
		// The file's bytes as they lie — a PEM is text the TLS layer reads itself; a file that cannot be read is said below.
		std::ifstream in(ibPathOf(trusted), std::ios::binary);
		if (!in) {
			error = ibString::Format(_("the trusted certificates %s could not be read"), trusted);
			return false;
		}
		pem.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		if (in.bad()) {
			error = ibString::Format(_("the trusted certificates %s could not be read"), trusted);
			return false;
		}
	}
	if (ibTlsClientContext(pem, std::string(), std::string(), /*verify*/ true, tls, refused) != ibHttpClientError::None) {
		error = ibString::FromUTF8(refused);
		return false;
	}
	return true;
}

ibProtocolConnectionServer::ibProtocolConnectionServer() = default;

bool ibProtocolConnectionServer::Open(const ibString& address, const ibString& trusted, ibString& error)
{
	Close();

	ibString host, base;
	int port = 0;
	bool secure = false;
	if (!ParseAddress(address, host, port, base, secure)) {
		error = ibString::Format(_("'%s' is not the address of a base: oes://server[:port]/<base> or oess://..."), address);
		return false;
	}

	// OVER TLS — whom the client trusts (ClientTls).
	std::shared_ptr<const ibTlsContext> tls;
	ibString refused;
	if (secure && !ClientTls(trusted, tls, refused)) {
		error = ibString::Format(_("The connection to the server at %s:%d is not prepared: %s."), host, port, refused);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_opened = false;
		m_closed = false;
		m_reason.clear();
		m_answer.reset();
	}

	m_ioThreads = std::make_unique<ibIoThreads>();
	m_socketClient = std::make_shared<ibWebSocketClient>(m_ioThreads->GetContext(),
		[this]() {
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_opened = true;
			}
			m_answered.notify_all();
		},
		// The protocol speaks packets: a text is the two ends no longer agreeing, and the socket counts as gone.
		[this](const ibString&) {
			Closed("a text message, where the protocol speaks packets");
		},
		// A packet as it is, or deflated, as the server packs one of some size (ibWebSocket::Send) — told by its first
		// bytes. One that does not inflate: the socket counts as gone.
		[this](std::string data) {
			ibMemoryBuffer packet;
			if (ibBinaryProvider::IsPacket(data.data(), data.size()))
				packet.AppendData(data.data(), data.size());
			else if (!ibInflate(data, packet)) {
				Closed("an answer that does not inflate");
				return;
			}
			Receive(packet);
		},
		[this](const std::string& reason) {
			Closed(reason);
		});
	// The host as a resolver takes it — an IPv6 literal without the brackets the address wraps it in.
	std::string name(host.utf8_str());
	if (name.size() > 2 && name.front() == '[' && name.back() == ']')
		name = name.substr(1, name.size() - 2);
	if (tls)
		m_socketClient->Secure(std::move(tls));
	// Compressed, as the protocol offers it: an answer of some size comes as a binary message.
	m_socketClient->Open(name, static_cast<std::uint16_t>(port), ClientPath(base) + "?compress=deflate");

	// OPEN, OR WHY NOT — before the window, as a socket connected in place used to answer. The opening's own deadline
	// bounds the wait (core/net/httpClient.cpp).
	std::string reason;
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_answered.wait(lock, [this]() { return m_opened || m_closed; });
		if (!m_closed) {
			m_address = address;
			m_trusted = trusted;
			return true;
		}
		reason = m_reason;
	}
	Close();
	error = ibString::Format(_("The connection to the server at %s:%d did not open: %s."), host, port,
		ibString::FromUTF8(reason));
	return false;
}

void ibProtocolConnectionServer::Close()
{
	if (!m_socketClient)
		return;

	// The server told the socket is closing, and given a moment to hear it; then the thread stops, and nothing of the
	// socket is called after.
	m_socketClient->Close();
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_answered.wait_for(lock, kCloseWithin, [this]() { return m_closed; });
	}
	m_socketClient.reset();
	m_ioThreads.reset();
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_closed = true;
	}
}

ibProtocolConnectionServer::~ibProtocolConnectionServer()
{
	Close();
}

bool ibProtocolConnectionServer::Reconnect(ibString& error)
{
	// Copies: Open closes the socket first, and what it parses must not be the members it may clear.
	const ibString address = m_address, trusted = m_trusted;
	if (address.IsEmpty()) {
		error = ibT("no server to return to");
		return false;
	}
	return Open(address, trusted, error);
}

std::unique_ptr<ibProtocolConnection> ibProtocolConnectionServer::OpenChannel(ibString& error)
{
	if (m_address.IsEmpty()) {
		error = ibT("no server to open a second connection to");
		return nullptr;
	}
	auto channel = std::make_unique<ibProtocolConnectionServer>();
	if (!channel->Open(m_address, m_trusted, error))
		return nullptr;
	return channel;
}

bool ibProtocolConnectionServer::Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	std::lock_guard<std::mutex> exchanging(m_exchanging);

	// A socket gone took the clients logged in through it: log in anew.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_socketClient || m_closed) {
			refusal = ibProtocol::ibRefusal::NoSession;
			error = ibT("the connection to the server is closed");
			return false;
		}
		m_answer.reset();
	}
	// Queued, and sent in order; a socket that cannot take it goes, and the wait below hears that.
	m_socketClient->Send(request.GetData(), request.GetDataLen());

	// Its answer is the next answer the socket hands over: one request is in flight at a time.
	std::unique_lock<std::mutex> lock(m_mutex);
	m_answered.wait(lock, [this]() { return m_answer.has_value() || m_closed; });
	if (!m_answer.has_value()) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = ibT("the server closed the connection");
		return false;
	}
	answer = std::move(*m_answer);
	m_answer.reset();
	return true;
}

void ibProtocolConnectionServer::Receive(const ibMemoryBuffer& packet)
{
	// Checked here, once, as it lies — a packet that is none: the two ends no longer agree.
	ibProtocolNode said;
	if (!ibProtocolNode::Read(packet, said)) {
		Closed("a message that is not a packet");
		return;
	}

	// A NOTIFICATION — a method and no id, said unasked: to whoever listens, here. An answer always has its id
	// (ibClientHost::Answer), so the debugger's news, of any length, is never an answer.
	if (!said.Has(ibProtocol::ibRpc::Id)) {
		std::function<void(const ibProtocolNode&)> notified;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			notified = m_notified;
		}
		if (notified)
			notified(said);
		return;
	}

	// …else the answer to the request waiting.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_answer = std::move(said);
	}
	m_answered.notify_all();
}

void ibProtocolConnectionServer::Closed(const std::string& reason)
{
	// The socket gone, or no longer understood: a request waiting is answered with that.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_closed = true;
		m_reason = reason;
	}
	m_answered.notify_all();
}

void ibProtocolConnectionServer::Listen(std::function<void(const ibProtocolNode& said)> notified)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_notified = std::move(notified);
}
