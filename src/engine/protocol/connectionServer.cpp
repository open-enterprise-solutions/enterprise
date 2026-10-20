#include "connectionServer.h"

#include <chrono>
#include <string>

#include <wx/intl.h>   // _() — what a refusal says to a person
#include <wx/mstream.h>
#include <wx/zstream.h>

// ⚠ PLAIN ws:// ONLY, as the core's client speaks it (core/net/httpClient.h).
#include "core/net/httpClient.h"
#include "core/net/ioThreads.h"

#include "protocolNode.h"   // a short message read, to tell a notification from an answer

namespace {

// How long Close waits for the server to be told the socket is closing, before its thread is stopped under it.
constexpr std::chrono::seconds kCloseWithin(1);

// The longest a notification of the server's is — a line; anything longer is an answer.
constexpr std::size_t kNotificationMax = 1024;

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

// A binary message is the answer deflated — raw, RFC 1951, as the server packs it (appserver/clientListener).
bool Inflate(std::string& message)
{
	wxMemoryInputStream packed(message.data(), message.size());
	wxZlibInputStream unzip(packed, wxZLIB_NO_HEADER);
	std::string text;
	char chunk[16384];
	for (;;) {
		unzip.Read(chunk, sizeof(chunk));
		const std::size_t got = unzip.LastRead();
		if (got == 0)
			break;
		text.append(chunk, got);
	}
	if (unzip.GetLastError() != wxSTREAM_NO_ERROR && unzip.GetLastError() != wxSTREAM_EOF)
		return false;
	message.swap(text);
	return true;
}

} // namespace

bool ibProtocolConnectionServer::ParseAddress(const wxString& address, wxString& host, int& port, wxString& base)
{
	wxString rest;
	if (!address.StartsWith(wxT("oes://"), &rest))
		return false;
	const int slash = rest.Find(wxT('/'));
	if (slash == wxNOT_FOUND)
		return false;
	wxString authority = rest.Left(slash);
	base = rest.Mid(slash + 1);
	if (base.EndsWith(wxT("/")))
		base.RemoveLast();

	port = kDefaultPort;
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

ibProtocolConnectionServer::ibProtocolConnectionServer() = default;

bool ibProtocolConnectionServer::Open(const wxString& address, wxString& error)
{
	Close();

	wxString host, base;
	int port = kDefaultPort;
	if (!ParseAddress(address, host, port, base)) {
		error = wxString::Format(_("'%s' is not the address of a base: oes://server[:port]/<base>"), address);
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
		[this](std::uint8_t opcode, std::string message) {
			// A deflated answer that does not inflate: the two ends no longer agree, and the socket counts as gone.
			if (opcode == ibWebSocketOpcode::Binary && !Inflate(message)) {
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_closed = true;
					m_reason = "an answer that does not inflate";
				}
				m_answered.notify_all();
				return;
			}

			// A NOTIFICATION — a method and no id, said unasked: to whoever listens, here. Only a short message can be one
			// (the server's notifications are a line), so an answer of a frame is never parsed twice.
			if (message.size() <= kNotificationMax) {
				ibProtocolNode node;
				if (ibProtocolNode::Read(message, node) && node.Has(ibProtocol::ibRpc::Method) && !node.Has(ibProtocol::ibRpc::Id)) {
					std::function<void(const std::string&)> notified;
					{
						std::lock_guard<std::mutex> lock(m_mutex);
						notified = m_notified;
					}
					if (notified)
						notified(message);
					return;
				}
			}

			// …else the answer to the request waiting.
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_answer = std::move(message);
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
	// A copy: Open closes the socket first, and the address it parses must not be the member it may clear.
	const wxString address = m_address;
	if (address.IsEmpty()) {
		error = wxT("no server to return to");
		return false;
	}
	return Open(address, error);
}

bool ibProtocolConnectionServer::Exchange(const std::string& request, std::string& answer,
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

void ibProtocolConnectionServer::Listen(std::function<void(const std::string& text)> notified)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_notified = std::move(notified);
}
