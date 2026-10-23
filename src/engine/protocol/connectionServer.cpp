#include "connectionServer.h"

#include <chrono>
#include <string>

#include <wx/ffile.h>
#include <wx/intl.h>   // _() — what a refusal says to a person
#include <wx/log.h>

#include "core/net/httpClient.h"   // ws:// — and wss://, the client's TLS (ibTlsClientContext)
#include "core/net/ioThreads.h"
#include "core/net/ports.h"        // ibPort::Clients — the port an address leaves out

namespace {

// How long Close waits for the server to be told the socket is closing, before its thread is stopped under it.
constexpr std::chrono::seconds kCloseWithin(1);

// How a notification of the server's begins — the method first, where an answer has its id.
const ibString kNotificationHead(wxT("{\"jsonrpc\":\"2.0\",\"method\":"));

// A base's name in the path — its UTF-8 bytes, every one but the unreserved escaped.
std::string EscapeSegment(const wxString& segment)
{
	static const char s_hex[] = "0123456789ABCDEF";
	const wxScopedCharBuffer utf8 = segment.utf8_str();
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

bool ibProtocolConnectionServer::ParseAddress(const wxString& address, wxString& host, int& port, wxString& base,
	bool& secure)
{
	wxString rest;
	secure = address.StartsWith(wxT("oess://"), &rest);
	if (!secure && !address.StartsWith(wxT("oes://"), &rest))
		return false;
	const int slash = rest.Find(wxT('/'));
	if (slash == wxNOT_FOUND)
		return false;
	wxString authority = rest.Left(slash);
	base = rest.Mid(slash + 1);
	if (base.EndsWith(wxT("/")))
		base.RemoveLast();

	port = ibPort::Clients.m_first;
	const int colon = authority.Find(wxT(':'), true);
	if (colon != wxNOT_FOUND && !authority.EndsWith(wxT("]"))) {
		long number = 0;
		if (!authority.Mid(colon + 1).ToLong(&number) || number <= 0 || number > 65535)
			return false;
		port = static_cast<int>(number);
		authority = authority.Left(colon);
	}
	host = authority;
	return !host.IsEmpty() && !base.IsEmpty() && base.Find(wxT('/')) == wxNOT_FOUND;
}

std::string ibProtocolConnectionServer::ClientPath(const wxString& base)
{
	return "/" + EscapeSegment(base) + "/client";
}

bool ibProtocolConnectionServer::ClientTls(const wxString& trusted, std::shared_ptr<const ibTlsContext>& tls,
	wxString& error)
{
	std::string pem, refused;
	if (!trusted.IsEmpty()) {
		// wxFFile reports a file it cannot open through wxLog — a box in a window's process; the refusal below says it.
		wxLogNull quiet;
		wxFFile in(trusted, wxT("rb"));
		wxString text;
		if (!in.IsOpened() || !in.ReadAll(&text, wxConvUTF8)) {
			error = wxString::Format(_("the trusted certificates %s could not be read"), trusted);
			return false;
		}
		pem = std::string(text.utf8_str());
	}
	if (ibTlsClientContext(pem, std::string(), std::string(), /*verify*/ true, tls, refused) != ibHttpClientError::None) {
		error = wxString::FromUTF8(refused);
		return false;
	}
	return true;
}

ibProtocolConnectionServer::ibProtocolConnectionServer() = default;

bool ibProtocolConnectionServer::Open(const wxString& address, const wxString& trusted, wxString& error)
{
	Close();

	wxString host, base;
	int port = 0;
	bool secure = false;
	if (!ParseAddress(address, host, port, base, secure)) {
		error = wxString::Format(_("'%s' is not the address of a base: oes://server[:port]/<base> or oess://…"), address);
		return false;
	}

	// OVER TLS — whom the client trusts (ClientTls).
	std::shared_ptr<const ibTlsContext> tls;
	wxString refused;
	if (secure && !ClientTls(trusted, tls, refused)) {
		error = wxString::Format(_("The connection to the server at %s:%d is not prepared: %s."), host, port, refused);
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
		[this](const ibString& text) {
			Receive(text);
		},
		// A binary message is a text deflated, as the server packs one of some size (ibWebSocket::Send) — one that does
		// not inflate: the two ends no longer agree, and the socket counts as gone.
		[this](std::string packed) {
			ibString text;
			if (ibInflate(packed, text)) {
				Receive(text);
				return;
			}
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_closed = true;
				m_reason = "an answer that does not inflate";
			}
			m_answered.notify_all();
		},
		[this](const std::string& reason) {
			// The socket gone: a request waiting is answered with that.
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_closed = true;
				m_reason = reason;
			}
			m_answered.notify_all();
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
	error = wxString::Format(_("The connection to the server at %s:%d did not open: %s."), host, port,
		wxString::FromUTF8(reason));
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

bool ibProtocolConnectionServer::Reconnect(wxString& error)
{
	// Copies: Open closes the socket first, and what it parses must not be the members it may clear.
	const wxString address = m_address, trusted = m_trusted;
	if (address.IsEmpty()) {
		error = wxT("no server to return to");
		return false;
	}
	return Open(address, trusted, error);
}

std::unique_ptr<ibProtocolConnection> ibProtocolConnectionServer::OpenChannel(wxString& error)
{
	if (m_address.IsEmpty()) {
		error = wxT("no server to open a second connection to");
		return nullptr;
	}
	auto channel = std::make_unique<ibProtocolConnectionServer>();
	if (!channel->Open(m_address, m_trusted, error))
		return nullptr;
	return channel;
}

bool ibProtocolConnectionServer::Exchange(const ibString& request, ibString& answer,
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	std::lock_guard<std::mutex> exchanging(m_exchanging);

	// A socket gone took the clients logged in through it: log in anew.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_socketClient || m_closed) {
			refusal = ibProtocol::ibRefusal::NoSession;
			error = wxT("the connection to the server is closed");
			return false;
		}
		m_answer.reset();
	}
	// Queued, and sent in order; a socket that cannot take it goes, and the wait below hears that.
	m_socketClient->Send(request);

	// Its answer is the next answer the socket hands over: one request is in flight at a time.
	std::unique_lock<std::mutex> lock(m_mutex);
	m_answered.wait(lock, [this]() { return m_answer.has_value() || m_closed; });
	if (!m_answer.has_value()) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = wxT("the server closed the connection");
		return false;
	}
	answer = std::move(*m_answer);
	m_answer.reset();
	return true;
}

void ibProtocolConnectionServer::Receive(const ibString& text)
{
	// A NOTIFICATION — a method and no id, said unasked: to whoever listens, here. Told by how it begins: the server
	// writes a notification's method where an answer's id stands (ibClientHost::NotifyChanged, NotifyDebug), so an
	// answer of a frame is never parsed twice, and the debugger's news, of any length, is never an answer.
	if (text.StartsWith(kNotificationHead)) {
		std::function<void(const ibString&)> notified;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			notified = m_notified;
		}
		if (notified)
			notified(text);
		return;
	}

	// …else the answer to the request waiting.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_answer = text;
	}
	m_answered.notify_all();
}

void ibProtocolConnectionServer::Listen(std::function<void(const ibString& text)> notified)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_notified = std::move(notified);
}
