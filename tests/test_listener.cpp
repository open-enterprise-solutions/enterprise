////////////////////////////////////////////////////////////////////////////
//	Description : the listener of a server of this platform (core/server/listener.h) - TLS over the files
//	backend.conf names, plain on this machine's own addresses otherwise, refused off them. The identities are
//	tests/certs/ (self-signed, made once); the servers - the core's (core/net/httpServer.h) - come up on
//	127.0.0.1, and cpp-httplib's clients, another library, are what speaks to them - and the core's own WebSocket
//	client over TLS (core/net/httpClient.h), as an oess:// address opens it.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <wx/ffile.h>
#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/sstream.h>
#include <wx/utils.h>

// As in mcp/mcpServer.cpp, which says why: wx and cpp-httplib both alias ssize_t, and MSVC refuses the second.
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include <cpp-httplib/httplib.h>

#include "core/exception.h"
#include "core/net/httpClient.h"
#include "core/net/httpServer.h"
#include "core/net/ioThreads.h"
#include "core/server/listener.h"

namespace {

// tests/certs, beside this file's directory - as test_valueHttpSecure.cpp finds them.
wxString CertPath(const wxChar* name)
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.AppendDir(wxT("certs"));
	here.SetFullName(name);
	return here.GetFullPath();
}

ibTlsFiles TestIdentity()
{
	ibTlsFiles files;
	files.m_certificate = CertPath(wxT("server.pem"));
	files.m_privateKey  = CertPath(wxT("server.key"));
	return files;
}

// The door's refusal for `address` over `files` - empty when it gave the options.
wxString Refusal(const wxString& address, const ibTlsFiles& files)
{
	try {
		ibListenerOptions(address, 0, files);
	}
	catch (const ibCoreException& err) {
		return err.GetErrorDescription();
	}
	return wxString();
}

// A WebSocket that answers each message with itself.
class Echo : public ibWebSocketHandler {
public:
	explicit Echo(std::shared_ptr<ibWebSocket> socket) : m_socket(std::move(socket)) {}
	virtual void OnMessage(const ibString& text) override { m_socket->Send(text); }
	virtual void OnBinary(std::string) override {}
	virtual void OnClose() override {}
private:
	std::shared_ptr<ibWebSocket> m_socket;
};

// A server started with the door's options, on a port the system chooses, answering "ok" at / and echoing at
// /echo; m_port is 0 and m_error says why when it did not start. Stopped with the test, so a fatal assertion does
// not end the binary.
struct Up {
	ibHttpServer m_server;
	std::string  m_error;
	int          m_port = 0;
	explicit Up(ibHttpServerOptions options) {
		m_server.Get("/", [](const ibHttpRequest&, ibHttpResponder respond) {
			ibHttpResponse response;
			response.SetContent("ok", "text/plain");
			respond(std::move(response));
		});
		m_server.WebSocket("/echo", [](const std::shared_ptr<ibWebSocket>& socket) {
			return std::make_shared<Echo>(socket);
		});
		options.m_threads = 1;
		options.m_workers = 2;
		if (m_server.Start(options, m_error))
			m_port = m_server.GetHttpsPort() != 0 ? m_server.GetHttpsPort() : m_server.GetHttpPort();
	}
	~Up() { m_server.Stop(); }
};

// What a client's socket was told: the echo, or why it went. Held by its handlers too, which may be told after the
// socket was let go.
struct Told {
	std::mutex  m_mutex;
	std::string m_text;
	std::string m_reason;
	bool        m_closed = false;
};

// The core's WebSocket client to the server at `port`, over `tls` — "ping" sent, and what came back, or why the socket
// went, within five seconds.
std::shared_ptr<Told> Ping(int port, std::shared_ptr<const ibTlsContext> tls)
{
	const std::shared_ptr<Told> told = std::make_shared<Told>();
	ibIoThreads threads;
	{
		ibWebSocketClient client(threads.GetContext(), nullptr,
			[told](const ibString& text) {
				std::lock_guard<std::mutex> lock(told->m_mutex);
				told->m_text = text.ToUtf8();
			},
			nullptr,
			[told](const std::string& reason) {
				std::lock_guard<std::mutex> lock(told->m_mutex);
				told->m_closed = true;
				told->m_reason = reason;
			});
		client.Secure(std::move(tls));
		client.Open("127.0.0.1", static_cast<std::uint16_t>(port), "/echo");
		client.Send(ibString(wxT("ping")));
		for (int waited = 0; waited < 5000; waited += 10) {
			{
				std::lock_guard<std::mutex> lock(told->m_mutex);
				if (!told->m_text.empty() || told->m_closed)
					break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
	return told;
}

} // namespace

// The files named: the listener speaks TLS, and a client that trusts the certificate reads the answer.
TEST(Listener, NamedFilesMakeATlsListener)
{
	wxInitializer init;
	const Up up(ibListenerOptions(wxT("127.0.0.1"), 0, TestIdentity()));
	ASSERT_GT(up.m_port, 0) << up.m_error;

	// The TLS server is the core's, the client here is cpp-httplib over this binary's own copy of Mbed TLS - two
	// copies, each given its mutexes by its own module.
	httplib::SSLClient client("127.0.0.1", up.m_port);
	client.set_ca_cert_path(std::string(CertPath(wxT("server.pem")).utf8_str()).c_str());
	client.enable_server_certificate_verification(true);
	const auto answer = client.Get("/");
	ASSERT_TRUE(answer) << "no answer over TLS: " << httplib::to_string(answer.error());
	EXPECT_EQ(200, answer->status);
	EXPECT_EQ("ok", answer->body);

	// ...and a plain client meets a server that does not speak its tongue.
	httplib::Client plain("127.0.0.1", up.m_port);
	plain.set_connection_timeout(2);
	plain.set_read_timeout(2);
	EXPECT_FALSE(plain.Get("/"));
}

// AN oess:// SOCKET — the core's own client over TLS to the listener the files make: it opens and is echoed when it
// trusts the server's certificate; when it trusts another, it is refused at the handshake, with why, and nothing is
// said on it.
TEST(Listener, AWebSocketClientSpeaksTlsToTheListener)
{
	wxInitializer init;
	const Up up(ibListenerOptions(wxT("127.0.0.1"), 0, TestIdentity()));
	ASSERT_GT(up.m_port, 0) << up.m_error;

	const auto readPem = [](const wxChar* name) {
		wxFFile in(CertPath(name), wxT("rb"));
		wxString text;
		in.ReadAll(&text, wxConvUTF8);
		return std::string(text.utf8_str());
	};

	std::shared_ptr<const ibTlsContext> trusting;
	std::string error;
	ASSERT_EQ(ibHttpClientError::None, ibTlsClientContext(readPem(wxT("server.pem")), std::string(), std::string(),
		/*verify*/ true, trusting, error)) << error;
	const std::shared_ptr<Told> echoed = Ping(up.m_port, trusting);
	EXPECT_EQ("ping", echoed->m_text) << echoed->m_reason;

	std::shared_ptr<const ibTlsContext> stranger;
	ASSERT_EQ(ibHttpClientError::None, ibTlsClientContext(readPem(wxT("other.pem")), std::string(), std::string(),
		/*verify*/ true, stranger, error)) << error;
	const std::shared_ptr<Told> refused = Ping(up.m_port, stranger);
	EXPECT_TRUE(refused->m_closed);
	EXPECT_TRUE(refused->m_text.empty());
	EXPECT_FALSE(refused->m_reason.empty());
}

// Nothing named: a plain listener, on this machine's own address.
TEST(Listener, NoFilesMakeAPlainListenerOnLoopback)
{
	wxInitializer init;
	const Up up(ibListenerOptions(wxT("127.0.0.1"), 0, ibTlsFiles()));
	ASSERT_GT(up.m_port, 0) << up.m_error;
	httplib::Client client("127.0.0.1", up.m_port);
	const auto answer = client.Get("/");
	ASSERT_TRUE(answer);
	EXPECT_EQ("ok", answer->body);
}

// Nothing named and an address that is not this machine's own: refused, with backend.conf named in the
// reason; the same address with the files named is a TLS listener.
TEST(Listener, ThePlainListenerStaysOnLoopback)
{
	wxInitializer init;
	const wxString why = Refusal(wxT("0.0.0.0"), ibTlsFiles());
	EXPECT_TRUE(why.Contains(wxT("backend.conf"))) << why.ToStdString();
	EXPECT_TRUE(why.Contains(wxT("0.0.0.0"))) << why.ToStdString();
	EXPECT_FALSE(Refusal(wxT("192.168.1.10"), ibTlsFiles()).IsEmpty());
	const wxString named = Refusal(wxT("0.0.0.0"), TestIdentity());
	EXPECT_TRUE(named.IsEmpty()) << named.ToStdString();
	EXPECT_TRUE(Refusal(wxT("localhost"), ibTlsFiles()).IsEmpty());
	EXPECT_TRUE(Refusal(wxT("::1"), ibTlsFiles()).IsEmpty());
}

// A certificate mislaid is a refusal that names the file - never a listener in the clear; one file
// without the other is a refusal too.
TEST(Listener, MislaidFilesAreARefusalNotAFallback)
{
	wxInitializer init;
	ibTlsFiles files = TestIdentity();
	files.m_certificate = CertPath(wxT("nobody-wrote-this.pem"));
	wxString why = Refusal(wxT("127.0.0.1"), files);
	EXPECT_TRUE(why.Contains(wxT("nobody-wrote-this.pem"))) << why.ToStdString();
	EXPECT_TRUE(why.Contains(wxT("is not there"))) << why.ToStdString();   // said before any reader is asked

	ibTlsFiles half = TestIdentity();
	half.m_privateKey.clear();
	why = Refusal(wxT("127.0.0.1"), half);
	EXPECT_TRUE(why.Contains(wxT("PrivateKey"))) << why.ToStdString();

	// A key that is there, well formed, and not the certificate's: refused by the server that reads the pair -
	// it does not start, and nothing listens in the clear in its place.
	ibTlsFiles wrong = TestIdentity();
	wrong.m_privateKey = CertPath(wxT("stranger.key"));
	{
		const Up up(ibListenerOptions(wxT("127.0.0.1"), 0, wrong));
		EXPECT_EQ(0, up.m_port);
		EXPECT_NE(std::string::npos, up.m_error.find("not the certificate's")) << up.m_error;
	}

	// A file that is there and is not a key at all: refused the same way.
	ibTlsFiles notAKey = TestIdentity();
	notAKey.m_privateKey = CertPath(wxT("other.pem"));
	{
		const Up up(ibListenerOptions(wxT("127.0.0.1"), 0, notAKey));
		EXPECT_EQ(0, up.m_port);
		EXPECT_NE(std::string::npos, up.m_error.find("private key")) << up.m_error;
	}

	// A file longer than any certificate is - not read into memory, refused by name.
	struct ibTempFile {
		wxString m_path;
		~ibTempFile() { if (!m_path.IsEmpty()) wxRemoveFile(m_path); }
	} huge;
	huge.m_path = wxFileName::CreateTempFileName(wxT("oes-listener-"));
	ASSERT_FALSE(huge.m_path.IsEmpty());
	{
		wxFFile out(huge.m_path, wxT("wb"));
		ASSERT_TRUE(out.IsOpened());
		const std::string block(1u << 16, '-');
		for (int i = 0; i < 16; ++i)
			ASSERT_EQ(block.size(), out.Write(block.data(), block.size()));
		ASSERT_EQ(std::size_t(1), out.Write("-", 1));   // one byte past a megabyte
	}
	ibTlsFiles tooBig = TestIdentity();
	tooBig.m_certificate = huge.m_path;
	why = Refusal(wxT("127.0.0.1"), tooBig);
	EXPECT_TRUE(why.Contains(wxT("larger than a certificate"))) << why.ToStdString();
}

// This machine's own addresses, as the door knows them - asked through the door: the clear is taken on them
// and refused on everything else.
TEST(Listener, LoopbackIsKnownByItsNames)
{
	wxInitializer init;
	// [::1] among them: the server takes the brackets off and binds ::1 (ibHttpServer::Start) — one test of this machine
	// for the door and the server (ibIsLoopback, core/net/httpServer.h).
	for (const wxChar* own : { wxT("127.0.0.1"), wxT("127.5.5.5"), wxT("127.255.255.255"), wxT(" 127.0.0.1 "), wxT("localhost"), wxT("LocalHost"), wxT("::1"), wxT("[::1]") }) {
		const wxString why = Refusal(own, ibTlsFiles());
		EXPECT_TRUE(why.IsEmpty()) << own << ": " << why.ToStdString();
	}
	// By number, not by the look of a name: what begins with "127." and is not an address resolves wherever
	// its owner says, and a name that resolves to loopback today is still a name.
	for (const wxChar* other : { wxT("0.0.0.0"), wxT("::"), wxT("192.168.1.10"), wxT("10.0.0.1"), wxT("example.com"), wxT(""),
			wxT("127.example.com"), wxT("127.0.0.1.evil"), wxT("127"), wxT("127.0.0"), wxT("127.0.0.256"), wxT("127.0.0.+1"), wxT("1270.0.0.1"), wxT("localhost.") })
		EXPECT_FALSE(Refusal(other, ibTlsFiles()).IsEmpty()) << other;
}

// backend.conf's two keys, a relative name resolved against the file's folder, an absolute one kept.
TEST(Listener, TheConfNamesTheFilesRelativeToItsFolder)
{
	wxInitializer init;
	const wxString folder = wxFileName(CertPath(wxT("server.pem"))).GetPath();
	// A wxFileConfig fed from a stream unescapes while it parses, before a style can be set on it, so the
	// absolute name is spelled with forward slashes here (a Windows path comes back native from the door);
	// the file the process reads, backslashes and all, is the next test.
	wxString absolute = CertPath(wxT("server.key"));
	absolute.Replace(wxT("\\"), wxT("/"));
	// Blanks after a value stay in what wxFileConfig hands back; the door takes them off.
	wxStringInputStream text(wxT("Certificate = server.pem   \nPrivateKey = ") + absolute + wxT("\n"));
	wxFileConfig conf(text);
	const ibTlsFiles files = ibTlsFiles::FromConf(conf, folder);
	EXPECT_TRUE(files.IsNamed());
	EXPECT_EQ(CertPath(wxT("server.pem")), files.m_certificate);
	EXPECT_EQ(CertPath(wxT("server.key")), files.m_privateKey);

	wxStringInputStream nothing(wxT("Locale = en\n"));
	wxFileConfig bare(nothing);
	EXPECT_FALSE(ibTlsFiles::FromConf(bare, folder).IsNamed());
}

// A backend.conf file read as the host reads it (ibApplicationHost::ReadBackendConf: no escape characters), with
// every backslash of a Windows path kept - C:\oes\tests\certs\server.key is a path, not a tab in it.
TEST(Listener, TheConfFileIsReadAsWritten)
{
	wxInitializer init;
	// The file gone on every way out - a fatal assertion's too.
	struct ibTempFile {
		wxString m_path;
		~ibTempFile() { if (!m_path.IsEmpty()) wxRemoveFile(m_path); }
	} conf;
	conf.m_path = wxFileName::CreateTempFileName(wxT("oes-listener-"));
	ASSERT_FALSE(conf.m_path.IsEmpty());
	{
		wxFFile out(conf.m_path, wxT("w"));
		ASSERT_TRUE(out.IsOpened());
		out.Write(wxT("Locale = en\nCertificate = ") + CertPath(wxT("server.pem")) + wxT("\nPrivateKey = ") + CertPath(wxT("server.key")) + wxT("\n"));
	}
	wxFileConfig read(wxT(""), wxT(""), wxT(""), conf.m_path,
		wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_GLOBAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);
	const ibTlsFiles files = ibTlsFiles::FromConf(read, wxFileName(conf.m_path).GetPath());
	EXPECT_EQ(CertPath(wxT("server.pem")), files.m_certificate);
	EXPECT_EQ(CertPath(wxT("server.key")), files.m_privateKey);

	// And the listener started over what the file names is the TLS one.
	const wxString why = Refusal(wxT("0.0.0.0"), files);
	EXPECT_TRUE(why.IsEmpty()) << why.ToStdString();
	EXPECT_TRUE(ibListenerOptions(wxT("0.0.0.0"), 0, files).m_httpsPort.has_value());
}
