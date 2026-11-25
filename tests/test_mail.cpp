// =============================================================================
// Mail out - the core's SMTP client and MIME (core/net/smtpClient.h), and InternetMail* over them.
//
// A server is brought up on 127.0.0.1 inside the test, a plain socket that answers as it is told and keeps what it was
// sent; the client is held against what that server SAW - the commands, the envelope, the message's bytes, decoded
// here - not against its own idea of what it sent (development.md, section 5). Nothing is installed, no network is
// needed. TLS is not spoken here: it is the core's one TLS, tested with HTTPS (test_valueHttpSecure).
// =============================================================================

#ifdef _WIN32
#	include <winsock2.h>   // FIRST: windows.h, pulled in below, would bring the old winsock.h
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <unistd.h>
#endif

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT - the engine's headers include no wx

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "backend/backend_exception.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueBinaryData.h"
#include "backend/system/value/valueMail.h"

#include "core/memory/memoryBuffer.h"
#include "core/net/smtpClient.h"
#include "core/utils/base64.h"

namespace {

#ifdef _WIN32
using ibSocket = SOCKET;
const ibSocket kNoSocket = INVALID_SOCKET;
void CloseSocket(ibSocket socket) { ::shutdown(socket, SD_BOTH); ::closesocket(socket); }
struct WinsockUp {
	WinsockUp() { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); }
	~WinsockUp() { WSACleanup(); }
};
#else
using ibSocket = int;
const ibSocket kNoSocket = -1;
void CloseSocket(ibSocket socket) { ::shutdown(socket, SHUT_RDWR); ::close(socket); }
struct WinsockUp {};
#endif

// A peer gone is an error to send to, not a signal that ends the test run.
#ifdef MSG_NOSIGNAL
const int kSendFlags = MSG_NOSIGNAL;
#else
const int kSendFlags = 0;
#endif

sockaddr_in Loopback(std::uint16_t port)
{
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(port);
	return address;
}

// A MAIL SERVER OF THE TEST'S OWN — one connection: it greets, answers each command as `answer` says, takes the
// message after DATA up to the line holding a dot, and keeps all of it. `silent`: it takes the connection and says
// nothing at all.
class FakeSmtpServer {
public:

	using Answer = std::function<std::string(const std::string& command)>;

	explicit FakeSmtpServer(Answer answer, bool silent = false) : m_answer(std::move(answer)), m_silent(silent)
	{
		m_listen = ::socket(AF_INET, SOCK_STREAM, 0);
		sockaddr_in address = Loopback(0);
		::bind(m_listen, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
		::listen(m_listen, 1);
		socklen_t length = sizeof(address);
		::getsockname(m_listen, reinterpret_cast<sockaddr*>(&address), &length);
		m_port = ntohs(address.sin_port);
		m_thread = std::thread([this] { Serve(); });
	}

	~FakeSmtpServer()
	{
		Stop();
	}

	std::uint16_t Port() const { return m_port; }

	// What it was told - read after the client is done with it.
	std::vector<std::string> Commands() { Stop(); return m_commands; }
	std::string Data() { Stop(); return m_data; }

private:

	void Stop()
	{
		// Nobody came: a connection of its own wakes the accept, which a closed socket does not do everywhere.
		if (!m_accepted && m_listen != kNoSocket) {
			const ibSocket waker = ::socket(AF_INET, SOCK_STREAM, 0);
			const sockaddr_in address = Loopback(m_port);
			::connect(waker, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
			CloseSocket(waker);
		}
		if (m_listen != kNoSocket) {
			CloseSocket(m_listen);
			m_listen = kNoSocket;
		}
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_peer != kNoSocket) {
				CloseSocket(m_peer);
				m_peer = kNoSocket;
			}
		}
		if (m_thread.joinable())
			m_thread.join();
	}

	void Say(ibSocket peer, const std::string& text)
	{
		::send(peer, text.data(), static_cast<int>(text.size()), kSendFlags);
	}

	void Serve()
	{
		const ibSocket peer = ::accept(m_listen, nullptr, nullptr);
		m_accepted = true;
		if (peer == kNoSocket)
			return;
#ifdef SO_NOSIGPIPE
		const int one = 1;
		::setsockopt(peer, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_peer = peer;
		}
		if (!m_silent)
			Say(peer, "220 fake.test ESMTP\r\n");

		std::string in;
		bool data = false;
		char buffer[4096];
		for (;;) {
			const int got = ::recv(peer, buffer, sizeof(buffer), 0);
			if (got <= 0)
				return;
			if (m_silent)
				continue;
			in.append(buffer, static_cast<std::size_t>(got));
			for (std::size_t end = in.find("\r\n"); end != std::string::npos; end = in.find("\r\n")) {
				const std::string line = in.substr(0, end);
				in.erase(0, end + 2);
				if (data) {
					if (line == ".") {
						data = false;
						Say(peer, "250 2.0.0 queued\r\n");
					}
					else
						m_data += line + "\r\n";
					continue;
				}
				m_commands.push_back(line);
				const std::string reply = m_answer(line);
				Say(peer, reply);
				if (line == "DATA" && reply.compare(0, 3, "354") == 0)
					data = true;
				if (line == "QUIT")
					return;
			}
		}
	}

	WinsockUp                m_winsock;
	Answer                   m_answer;
	bool                     m_silent = false;
	ibSocket                 m_listen = kNoSocket;
	ibSocket                 m_peer = kNoSocket;
	std::atomic<bool>        m_accepted{ false };
	std::mutex               m_mutex;
	std::uint16_t            m_port = 0;
	std::thread              m_thread;
	std::vector<std::string> m_commands;
	std::string              m_data;
};

bool Starts(const std::string& text, const std::string& head) { return text.compare(0, head.size(), head) == 0; }

// The answers of a server that takes everything; `extensions` - what its EHLO lists after its name.
FakeSmtpServer::Answer Agreeable(const std::string& extensions = "AUTH PLAIN LOGIN")
{
	return [extensions](const std::string& command) -> std::string {
		if (Starts(command, "EHLO"))
			return extensions.empty() ? "250 fake.test\r\n" : "250-fake.test\r\n250 " + extensions + "\r\n";
		if (Starts(command, "AUTH PLAIN"))
			return "235 2.7.0 accepted\r\n";
		if (command == "DATA")
			return "354 go on\r\n";
		if (command == "QUIT")
			return "221 bye\r\n";
		return "250 ok\r\n";
	};
}

std::string Base64Decoded(std::string text)
{
	std::string compact;
	for (const char c : text) {
		if (c != '\r' && c != '\n')
			compact += c;
	}
	ibMemoryBuffer bytes;
	EXPECT_TRUE(ibBase64::Read(ibString::FromUTF8(compact), bytes)) << compact;
	return std::string(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
}

// The base64 body of the part whose headers hold `marker`: from the blank line after it to the next boundary.
std::string PartBody(const std::string& message, const std::string& marker)
{
	const std::size_t at = message.find(marker);
	if (at == std::string::npos)
		return std::string();
	const std::size_t start = message.find("\r\n\r\n", at) + 4;
	const std::size_t end = message.find("\r\n--", start);
	return message.substr(start, end - start);
}

std::string Utf8(const ibString& text) { return text.ToUtf8(); }

ibMailMessage AMessage()
{
	ibMailMessage message;
	message.m_from = "Shop <shop@example.com>";
	message.m_to = { "buyer@example.net" };
	message.m_cc = { "Accounts <accounts@example.net>" };
	message.m_bcc = { "archive@example.com" };
	message.m_subject = Utf8(ibString(wxT("\u0421\u0447\u0451\u0442 42")));
	message.m_plain = "The invoice is attached.\r\n.a line that begins with a dot\r\nThanks";
	message.m_attachments.push_back({ "invoice-42.pdf", std::string("%PDF-1.4\0\x01\x02", 11) });
	return message;
}

} // namespace

// THE CONVERSATION AS THE SERVER SAW IT: EHLO with the client's address, AUTH PLAIN with the user and the password,
// the envelope - Bcc in it, each recipient once - DATA, QUIT.
TEST(SmtpClient, TheServerSeesTheConversation)
{
	FakeSmtpServer server(Agreeable());
	ibSmtpClient client("127.0.0.1", server.Port());
	std::string error;
	ASSERT_EQ(client.Logon("shop", "secret", std::chrono::seconds(5), error), ibSmtpError::None) << error;
	ibMailMessage message = AMessage();
	message.m_to.push_back("BUYER@example.net");   // the same recipient, said twice
	ASSERT_EQ(client.Send(message, std::chrono::seconds(5), error), ibSmtpError::None) << error;
	client.Logoff(std::chrono::seconds(5));

	const std::vector<std::string> commands = server.Commands();
	ASSERT_EQ(commands.size(), 8u);
	EXPECT_EQ(commands[0], "EHLO [127.0.0.1]");
	EXPECT_EQ(commands[1], "AUTH PLAIN AHNob3AAc2VjcmV0");   // \0shop\0secret
	EXPECT_EQ(commands[2], "MAIL FROM:<shop@example.com>");
	EXPECT_EQ(commands[3], "RCPT TO:<buyer@example.net>");
	EXPECT_EQ(commands[4], "RCPT TO:<accounts@example.net>");
	EXPECT_EQ(commands[5], "RCPT TO:<archive@example.com>");
	EXPECT_EQ(commands[6], "DATA");
	EXPECT_EQ(commands[7], "QUIT");
}

// THE MESSAGE AS THE SERVER TOOK IT: the headers (Bcc not among them, the subject an encoded word), the text and the
// attachment decoded back to what was given - a line of the text that begins with a dot among them.
TEST(SmtpClient, TheMessageIsMimeTheServerCanRead)
{
	FakeSmtpServer server(Agreeable());
	ibSmtpClient client("127.0.0.1", server.Port());
	std::string error;
	ASSERT_EQ(client.Logon(std::string(), std::string(), std::chrono::seconds(5), error), ibSmtpError::None) << error;
	ASSERT_EQ(client.Send(AMessage(), std::chrono::seconds(5), error), ibSmtpError::None) << error;
	client.Logoff(std::chrono::seconds(5));

	std::string data = server.Data();
	ASSERT_FALSE(data.empty());
	// Dot-stuffing undone, as the server's reading does.
	std::string message;
	std::size_t at = 0;
	for (std::size_t end = data.find("\r\n"); end != std::string::npos; at = end + 2, end = data.find("\r\n", at)) {
		std::string line = data.substr(at, end - at);
		if (Starts(line, ".."))
			line.erase(0, 1);
		message += line + "\r\n";
	}

	EXPECT_NE(message.find("From: \"Shop\" <shop@example.com>\r\n"), std::string::npos) << message;
	EXPECT_NE(message.find("To: buyer@example.net\r\n"), std::string::npos);
	EXPECT_NE(message.find("Cc: \"Accounts\" <accounts@example.net>\r\n"), std::string::npos);
	EXPECT_EQ(message.find("Bcc"), std::string::npos) << "a blind copy is in the envelope, never in the headers";
	EXPECT_EQ(message.find("archive@example.com"), std::string::npos);
	EXPECT_NE(message.find("MIME-Version: 1.0\r\n"), std::string::npos);
	EXPECT_NE(message.find("Content-Type: multipart/mixed; boundary="), std::string::npos);

	const std::size_t subject = message.find("Subject: =?UTF-8?B?");
	ASSERT_NE(subject, std::string::npos);
	const std::size_t open = subject + 19;
	EXPECT_EQ(Base64Decoded(message.substr(open, message.find("?=", open) - open)), Utf8(ibString(wxT("\u0421\u0447\u0451\u0442 42"))));

	EXPECT_EQ(Base64Decoded(PartBody(message, "Content-Type: text/plain; charset=utf-8")), AMessage().m_plain);
	EXPECT_NE(message.find("Content-Type: application/pdf; name=\"invoice-42.pdf\""), std::string::npos);
	EXPECT_NE(message.find("Content-Disposition: attachment; filename=\"invoice-42.pdf\""), std::string::npos);
	EXPECT_EQ(Base64Decoded(PartBody(message, "filename=\"invoice-42.pdf\"")), AMessage().m_attachments.front().m_bytes);

	for (std::size_t from = 0, end = message.find("\r\n"); end != std::string::npos; from = end + 2, end = message.find("\r\n", from))
		EXPECT_LE(end - from, 998u) << "a line longer than the format allows";
}

// AUTH LOGIN where PLAIN is not offered: the user and the password, each in base64, each after its 334.
TEST(SmtpClient, LoginIsSpokenWherePlainIsNotOffered)
{
	FakeSmtpServer server([](const std::string& command) -> std::string {
		if (Starts(command, "EHLO"))
			return "250-fake.test\r\n250 AUTH LOGIN\r\n";
		if (command == "AUTH LOGIN")
			return "334 VXNlcm5hbWU6\r\n";
		if (command == "c2hvcA==")   // shop
			return "334 UGFzc3dvcmQ6\r\n";
		if (command == "c2VjcmV0")   // secret
			return "235 accepted\r\n";
		return "250 ok\r\n";
	});
	ibSmtpClient client("127.0.0.1", server.Port());
	std::string error;
	EXPECT_EQ(client.Logon("shop", "secret", std::chrono::seconds(5), error), ibSmtpError::None) << error;
	EXPECT_TRUE(client.IsLoggedOn());
}

// A login the server refuses is said, with its code, and the client is not logged on.
TEST(SmtpClient, ARefusedLoginIsSaid)
{
	FakeSmtpServer server([](const std::string& command) -> std::string {
		if (Starts(command, "EHLO"))
			return "250-fake.test\r\n250 AUTH PLAIN\r\n";
		if (Starts(command, "AUTH"))
			return "535 5.7.8 authentication failed\r\n";
		return "250 ok\r\n";
	});
	ibSmtpClient client("127.0.0.1", server.Port());
	std::string error;
	EXPECT_EQ(client.Logon("shop", "wrong", std::chrono::seconds(5), error), ibSmtpError::Refused);
	EXPECT_NE(error.find("535"), std::string::npos) << error;
	EXPECT_EQ(error.find("wrong"), std::string::npos) << "a refusal never repeats the password";
	EXPECT_FALSE(client.IsLoggedOn());
}

// A RECIPIENT REFUSED SENDS NOTHING: no DATA, RSET clears the half transaction, and the connection stays for the next.
TEST(SmtpClient, ARefusedRecipientSendsNothing)
{
	FakeSmtpServer server([](const std::string& command) -> std::string {
		if (Starts(command, "EHLO"))
			return "250 fake.test\r\n";
		if (command == "RCPT TO:<accounts@example.net>")
			return "550 5.1.1 no such user\r\n";
		if (command == "DATA")
			return "354 go on\r\n";
		return "250 ok\r\n";
	});
	ibSmtpClient client("127.0.0.1", server.Port());
	std::string error;
	ASSERT_EQ(client.Logon(std::string(), std::string(), std::chrono::seconds(5), error), ibSmtpError::None) << error;
	EXPECT_EQ(client.Send(AMessage(), std::chrono::seconds(5), error), ibSmtpError::Refused);
	EXPECT_NE(error.find("accounts@example.net"), std::string::npos) << error;
	EXPECT_NE(error.find("550"), std::string::npos) << error;
	EXPECT_TRUE(client.IsLoggedOn());
	client.Logoff(std::chrono::seconds(5));

	const std::vector<std::string> commands = server.Commands();
	EXPECT_EQ(std::find(commands.begin(), commands.end(), "DATA"), commands.end());
	EXPECT_NE(std::find(commands.begin(), commands.end(), "RSET"), commands.end());
	EXPECT_TRUE(server.Data().empty());
}

// A message larger than the server's SIZE is refused before anything of it is said.
TEST(SmtpClient, AMessageLargerThanTheServerTakesIsNotSent)
{
	FakeSmtpServer server(Agreeable("SIZE 100"));
	ibSmtpClient client("127.0.0.1", server.Port());
	std::string error;
	ASSERT_EQ(client.Logon(std::string(), std::string(), std::chrono::seconds(5), error), ibSmtpError::None) << error;
	EXPECT_EQ(client.Send(AMessage(), std::chrono::seconds(5), error), ibSmtpError::Message);
	client.Logoff(std::chrono::seconds(5));
	for (const std::string& command : server.Commands())
		EXPECT_FALSE(Starts(command, "MAIL")) << command;
}

// TLS asked for and STARTTLS not offered: refused, rather than the password said in the clear.
TEST(SmtpClient, TlsAskedForAndNotOfferedIsRefused)
{
	FakeSmtpServer server(Agreeable());
	ibSmtpClient client("127.0.0.1", server.Port());
	std::string error;
	ASSERT_EQ(client.Secure(std::string(), std::string(), std::string(), false, false, error), ibSmtpError::None) << error;
	EXPECT_EQ(client.Logon("shop", "secret", std::chrono::seconds(5), error), ibSmtpError::NoStartTls);
	for (const std::string& command : server.Commands())
		EXPECT_FALSE(Starts(command, "AUTH")) << command;
}

// A server that takes the connection and says nothing is not waited for past the time allowed; nothing listening is a
// connection not made.
TEST(SmtpClient, SilenceIsATimeoutAndNobodyIsAConnection)
{
	{
		FakeSmtpServer server(Agreeable(), true);
		ibSmtpClient client("127.0.0.1", server.Port());
		std::string error;
		const auto started = std::chrono::steady_clock::now();
		EXPECT_EQ(client.Logon(std::string(), std::string(), std::chrono::seconds(1), error), ibSmtpError::Timeout);
		EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
	}
	std::uint16_t closed = 0;
	{
		FakeSmtpServer spare(Agreeable());   // a port the system just gave out, and nobody listening on it after
		closed = spare.Port();
	}
	ibSmtpClient client("127.0.0.1", closed);
	std::string error;
	EXPECT_EQ(client.Logon(std::string(), std::string(), std::chrono::seconds(5), error), ibSmtpError::Connection) << error;
}

// AN ADDRESS IS ONE OR IS REFUSED, and no header carries a line break - a subject such as "x\r\nBcc: y" would add a
// header of its maker's.
TEST(MailMessage, AddressesAndHeadersAreChecked)
{
	std::string name, address;
	EXPECT_TRUE(ibMailMessage::ParseAddress("Shop <shop@example.com>", name, address));
	EXPECT_EQ(name, "Shop");
	EXPECT_EQ(address, "shop@example.com");
	EXPECT_TRUE(ibMailMessage::ParseAddress("\"Shop, Ltd\" <shop@example.com>", name, address));
	EXPECT_EQ(name, "Shop, Ltd");
	for (const char* wrong : { "", "shop", "@example.com", "shop@", "shop @example.com", "a@b.c\r\nRCPT TO:<x@y.z>",
			"Shop <shop@example.com", "<a@b.c>, <d@e.f>" })
		EXPECT_FALSE(ibMailMessage::ParseAddress(wrong, name, address)) << wrong;

	ibMailMessage message = AMessage();
	message.m_subject = "x\r\nBcc: spy@example.org";
	std::string text, error;
	EXPECT_FALSE(message.Compose(text, error));

	message = AMessage();
	message.m_to = { "not an address" };
	EXPECT_FALSE(message.Compose(text, error));
	EXPECT_NE(error.find("not an address"), std::string::npos) << error;

	message = AMessage();
	message.m_to.clear();
	message.m_cc.clear();
	message.m_bcc.clear();
	EXPECT_FALSE(message.Compose(text, error)) << "a message with no recipient";
}

// A name that is not ASCII goes as encoded words, an attachment's as RFC 2231 pieces - and no line runs past 998.
TEST(MailMessage, ANameThatIsNotAsciiIsEncoded)
{
	ibMailMessage message = AMessage();
	message.m_from = Utf8(ibString(wxT("\u041C\u0430\u0433\u0430\u0437\u0438\u043D <shop@example.com>")));
	ibString longName;
	for (int n = 0; n < 60; ++n)
		longName += wxT("\u0440\u0430\u0445\u0443\u043D\u043E\u043A");
	message.m_attachments = { { Utf8(longName) + ".pdf", "bytes" } };
	std::string text, error;
	ASSERT_TRUE(message.Compose(text, error)) << error;
	EXPECT_NE(text.find("From: =?UTF-8?B?"), std::string::npos);
	EXPECT_NE(text.find("filename*0*=UTF-8''%D1%80"), std::string::npos);
	EXPECT_NE(text.find("filename*1*="), std::string::npos) << "a long name is cut into pieces";
	for (std::size_t from = 0, end = text.find("\r\n"); end != std::string::npos; from = end + 2, end = text.find("\r\n", from))
		EXPECT_LE(end - from, 998u);
}

// ---------------------------------------------------------------------------
//  From a script's values
// ---------------------------------------------------------------------------

namespace {

void CallMethod(ibValue& value, const wxChar* name, std::vector<ibValue> args, ibValue& out)
{
	const long method = value.FindMethod(name);
	ASSERT_GE(method, 0) << Utf8(name);
	std::vector<ibValue*> params;
	for (ibValue& arg : args)
		params.push_back(&arg);
	// …a procedure through CallAsProc and a function through CallAsFunc, as the interpreter picks them (HasRetVal).
	const long count = static_cast<long>(params.size());
	ASSERT_TRUE(value.HasRetVal(method) ? value.CallAsFunc(method, out, params.data(), count)
		: value.CallAsProc(method, params.data(), count)) << Utf8(name);
}

ibValue Prop(ibValue& value, const wxChar* name)
{
	const long prop = value.FindProp(name);
	EXPECT_GE(prop, 0) << Utf8(name);
	ibValue out;
	value.GetPropVal(prop, out);
	return out;
}

void SetProp(ibValue& value, const wxChar* name, const ibValue& to)
{
	const long prop = value.FindProp(name);
	ASSERT_GE(prop, 0) << Utf8(name);
	ASSERT_TRUE(value.SetPropVal(prop, to)) << Utf8(name);
}

template <class Call>
ibString Refusal(Call call)
{
	try {
		call();
	}
	catch (const ibBackendException& refusal) {
		const ibString why = refusal.GetErrorDescription();
		return why.empty() ? ibString(wxT("(a refusal with no sentence)")) : why;
	}
	return ibString();
}

} // namespace

// A profile's port is the security's unless given; its password is written and never read back.
TEST(InternetMail, TheProfileSaysThePortAndKeepsThePassword)
{
	ibValueInternetMailProfile profile;
	EXPECT_EQ(Prop(profile, wxT("SMTPPort")).GetInteger(), 25);
	SetProp(profile, wxT("SecureConnection"), ibValuePtr<ibValueSecureConnection>(new ibValueSecureConnection()));
	EXPECT_EQ(Prop(profile, wxT("SMTPPort")).GetInteger(), 465);
	SetProp(profile, wxT("SMTPPort"), ibValue(ibNumber(587)));
	EXPECT_EQ(Prop(profile, wxT("SMTPPort")).GetInteger(), 587);

	SetProp(profile, wxT("SMTPPassword"), ibValue(ibString(wxT("secret"))));
	EXPECT_EQ(profile.GetPassword(), ibString(wxT("secret")));
	ibValue read;
	EXPECT_FALSE(profile.GetPropVal(profile.FindProp(wxT("SMTPPassword")), read)) << "a password is not read back";

	EXPECT_FALSE(Refusal([&] { SetProp(profile, wxT("Timeout"), ibValue(ibNumber(0))); }).empty());
	EXPECT_FALSE(Refusal([&] { SetProp(profile, wxT("SecureConnection"), ibValue(ibString(wxT("yes")))); }).empty());
}

// A MESSAGE BUILT AS A SCRIPT BUILDS IT, SENT THROUGH InternetMail - and the server holds what the script said.
TEST(InternetMail, AMessageFromAScriptReachesTheServer)
{
	FakeSmtpServer server(Agreeable());

	ibValue profile(ibValuePtr<ibValueInternetMailProfile>(new ibValueInternetMailProfile()));
	SetProp(profile, wxT("SMTPServerAddress"), ibValue(ibString(wxT("127.0.0.1"))));
	SetProp(profile, wxT("SMTPPort"), ibValue(ibNumber(static_cast<int>(server.Port()))));
	SetProp(profile, wxT("SMTPUser"), ibValue(ibString(wxT("shop"))));
	SetProp(profile, wxT("SMTPPassword"), ibValue(ibString(wxT("secret"))));
	SetProp(profile, wxT("Timeout"), ibValue(ibNumber(5)));

	ibValue message(ibValuePtr<ibValueInternetMailMessage>(new ibValueInternetMailMessage()));
	SetProp(message, wxT("From"), ibValue(ibString(wxT("Shop <shop@example.com>"))));
	SetProp(message, wxT("Subject"), ibValue(ibString(wxT("Invoice 42"))));
	ibValue to = Prop(message, wxT("To"));
	ibValue out;
	CallMethod(to, wxT("Add"), { ibValue(ibString(wxT("buyer@example.net"))) }, out);
	ibValue texts = Prop(message, wxT("Texts"));
	CallMethod(texts, wxT("Add"), { ibValue(ibString(wxT("<p>Attached.</p>"))),
		ibValue::CreateEnumObject<ibValueEnumInternetMailTextType>(ibInternetMailTextType_HTML) }, out);
	EXPECT_EQ(Utf8(Prop(out, wxT("Text")).GetString()), "<p>Attached.</p>");
	ibValue attachments = Prop(message, wxT("Attachments"));
	CallMethod(attachments, wxT("Add"), { ibValue(ibValuePtr<ibValueBinaryData>(new ibValueBinaryData("abc", 3))),
		ibValue(ibString(wxT("a.txt"))) }, out);
	EXPECT_EQ(Utf8(Prop(out, wxT("Name")).GetString()), "a.txt");

	ibValue mail(ibValuePtr<ibValueInternetMail>(new ibValueInternetMail()));
	EXPECT_FALSE(Refusal([&] { CallMethod(mail, wxT("Send"), { message }, out); }).empty()) << "not logged on";
	CallMethod(mail, wxT("Logon"), { profile }, out);
	CallMethod(mail, wxT("Send"), { message }, out);
	CallMethod(mail, wxT("Logoff"), {}, out);

	const std::vector<std::string> commands = server.Commands();
	EXPECT_NE(std::find(commands.begin(), commands.end(), "RCPT TO:<buyer@example.net>"), commands.end());
	const std::string data = server.Data();
	EXPECT_NE(data.find("Subject: Invoice 42\r\n"), std::string::npos) << data;
	EXPECT_EQ(Base64Decoded(PartBody(data, "Content-Type: text/html; charset=utf-8")), "<p>Attached.</p>");
	EXPECT_EQ(Base64Decoded(PartBody(data, "filename=\"a.txt\"")), "abc");
}
