#include "connectionServer.h"

#include <string>

#include <wx/intl.h>   // _() — what a refusal says to a person
#include <wx/mstream.h>
#include <wx/zstream.h>

// After wx, with the guard already set: wx aliases ssize_t and so does cpp-httplib (backend/mcp/mcpServer.cpp).
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

// ⚠ PLAIN ws:// ONLY, so no TLS context is made here. The day oess:// (wss) is spoken, backend/tls/
// oes_mbedtls_threading.h belongs in this file: Mbed TLS is a static library, and this module's copy needs its
// mutexes handed over as the engine's does (the header says why).
#include <cpp-httplib/httplib.h>

#include "protocolNode.h"   // a short message read, to tell a notification from an answer

namespace {

// How long the reader waits at a time before it asks whether the socket is closing.
constexpr std::chrono::seconds kReadPoll(1);

// The longest a notification of the server's is — a line; anything longer is an answer.
constexpr std::size_t kNotificationMax = 1024;

// `oes://server[:port]/<base>` — the address the server prints for every base it serves.
bool ParseAddress(const wxString& address, wxString& host, int& port, wxString& base)
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

	port = ibProtocolConnectionServer::kDefaultPort;
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

	// Compressed, as the protocol offers it: an answer of some size comes as a binary message.
	const std::string url = "ws://" + std::string(host.utf8_str()) + ":" + std::to_string(port) + "/"
		+ EscapeSegment(base) + "/client?compress=deflate";
	auto socket = std::make_unique<httplib::ws::WebSocketClient>(url);
	if (!socket->is_valid()) {
		error = wxString::Format(_("'%s' is not the address of a base: oes://server[:port]/<base>"), address);
		return false;
	}
	const auto connected = socket->connect();
	if (!connected) {
		error = connected.status() > 0
			? wxString::Format(_("The server at %s:%d did not take the connection (%d)."), host, port, connected.status())
			: wxString::Format(_("The server at %s:%d is not reached: %s."), host, port,
				wxString::FromUTF8(httplib::to_string(connected.error())));
		return false;
	}

	// Read with a bound — a read that waits out the library's own default is a failure that closes the socket, and a
	// socket nobody asks anything of waits long: the reader's wait ends every second, and goes on unless it is closing.
	socket->set_read_timeout(kReadPoll);

	m_socket = std::move(socket);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_closed = false;
		m_answer.reset();
	}
	m_stopping = false;
	m_reader = std::thread(&ibProtocolConnectionServer::ReadMessages, this);
	m_address = address;
	return true;
}

void ibProtocolConnectionServer::Close()
{
	if (!m_socket)
		return;

	// The reader stops at its next wait, the socket closed under it; then the socket goes.
	m_stopping = true;
	m_socket->close();
	if (m_reader.joinable())
		m_reader.join();
	m_socket.reset();
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
	ibProtocolRefusal& refusal, wxString& error)
{
	std::lock_guard<std::mutex> exchanging(m_exchanging);

	// A socket gone took the clients logged in through it: log in anew.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_socket || m_closed) {
			refusal = ibProtocolRefusal::NoSession;
			error = wxT("the connection to the server is closed");
			return false;
		}
		m_answer.reset();
	}
	if (!m_socket->send(request)) {
		refusal = ibProtocolRefusal::NoSession;
		error = wxT("the request could not be sent");
		return false;
	}

	// Its answer is the next answer the reader is handed: one request is in flight at a time.
	std::unique_lock<std::mutex> lock(m_mutex);
	m_answered.wait(lock, [this]() { return m_answer.has_value() || m_closed; });
	if (!m_answer.has_value()) {
		refusal = ibProtocolRefusal::NoSession;
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

void ibProtocolConnectionServer::ReadMessages()
{
	std::string message;
	for (;;) {
		const httplib::ws::ReadResult read = m_socket->read(message);
		if (read == httplib::ws::Timeout) {
			if (m_stopping)
				break;
			continue;
		}
		const bool binary = read == httplib::ws::Binary;
		if ((binary && !Inflate(message)) || (!binary && read != httplib::ws::Text))
			break;   // closed — by the server, or by Close

		// A NOTIFICATION — a method and no id, said unasked: to whoever listens, here. Only a short message can be one
		// (the server's notifications are a line), so an answer of a frame is never parsed twice.
		if (message.size() <= kNotificationMax) {
			ibProtocolNode node;
			if (ibProtocolNode::Read(message, node) && node.Has(ibProtocolName::RpcMethod) && !node.Has(ibProtocolName::RpcId)) {
				std::function<void(const std::string&)> notified;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					notified = m_notified;
				}
				if (notified)
					notified(message);
				continue;
			}
		}

		// …else the answer to the request waiting.
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_answer = std::move(message);
		}
		m_answered.notify_all();
		message.clear();
	}

	// The socket gone: a request waiting is answered with that.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_closed = true;
	}
	m_answered.notify_all();
}
