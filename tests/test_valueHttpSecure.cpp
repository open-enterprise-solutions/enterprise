// A secure connection for the script's HTTP client - CertificateFile, SecureConnection, and what an
// HTTPConnection does with them. The first half needs no wire; the second brings the library's own TLS server up
// on 127.0.0.1 inside the test, with the identities in tests/certs/ (made once, self-signed, good for a century -
// its README says how), and asks what a client sees: an answer, or a refusal with the reason in the sentence,
// and never a request that reached a server it should not have trusted.
//
// ⚠ The server is the same library as the client. What that proves is that the roads in THIS code lead where
// they say; that the library's TLS agrees with other servers is what the library's own tests are for.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <wx/filename.h>

#include "backend/backend_exception.h"
#include "backend/system/value/valueHttp.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueSecureConnection.h"

// As in mcp/mcpServer.cpp, which says why: wx and cpp-httplib both alias ssize_t, and MSVC refuses the second.
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include "3rdparty/cpp-httplib/httplib.h"

// This binary brings a TLS server up itself, so it is a user of Mbed TLS in its own right - and Mbed TLS is a
// static library, so its copy here has its own mutexes to be given. One translation unit of this module.
#include "oes_mbedtls_threading.h"

namespace {

std::string Utf8(const wxString& text)
{
	const wxScopedCharBuffer utf8 = text.utf8_str();
	return std::string(utf8.data(), utf8.length());
}

template <class Call>
wxString Refusal(Call call)
{
	try {
		call();
	}
	catch (const ibBackendException& refusal) {
		const wxString why = refusal.GetErrorDescription();
		return why.empty() ? wxString(wxT("(a refusal with no sentence)")) : why;
	}
	return wxString();
}

// tests/certs, beside this file's directory - the same way the script corpus finds tests/scripts.
wxString CertPath(const wxChar* name)
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.AppendDir(wxT("certs"));
	here.SetFullName(name);
	return here.GetFullPath();
}

ibValuePtr<ibValueCertificateFile> Certificate(const wxChar* name, const wxString& password = wxString())
{
	ibValuePtr<ibValueCertificateFile> file(new ibValueCertificateFile());
	file->Open(CertPath(name), password);
	return file;
}

ibValuePtr<ibValueSecureConnection> Secure(const ibValuePtr<ibValueCertificateFile>& mine, const ibValuePtr<ibValueCertificateFile>& trusted, bool verify = true)
{
	ibValuePtr<ibValueSecureConnection> secure(new ibValueSecureConnection());
	if (mine != nullptr) secure->SetClientCertificate(mine);
	if (trusted != nullptr) secure->SetTrustedCertificates(trusted);
	secure->SetVerifyServerCertificate(verify);
	return secure;
}

const ibValuePtr<ibValueCertificateFile> kNone;

} // namespace

// ---------------------------------------------------------------------------
//  Without a wire
// ---------------------------------------------------------------------------

TEST(HttpSecureValues, ThisBuildHasTls)
{
#ifdef CPPHTTPLIB_SSL_ENABLED
	SUCCEED();
#else
	FAIL() << "CPPHTTPLIB_MBEDTLS_SUPPORT is not set for this build - the root CMakeLists.txt says it for every target";
#endif
}

TEST(HttpSecureValues, ACertificateFileIsAFileThatIsThere)
{
	ibValueCertificateFile file;
	const wxString why = Refusal([&] { file.Open(wxT("C:\\no\\such\\file.pem"), wxString()); });
	ASSERT_FALSE(why.empty());
	EXPECT_TRUE(why.Contains(wxT("file.pem"))) << "the refusal names the file: " << Utf8(why);
	EXPECT_TRUE(file.IsEmpty());
	EXPECT_FALSE(Refusal([&] { file.Open(wxString(), wxString()); }).empty());

	file.Open(CertPath(wxT("server.pem")), wxT("pass"));
	EXPECT_FALSE(file.IsEmpty());
	EXPECT_EQ(file.Path(), CertPath(wxT("server.pem")));

	// The path is read back; the password never is.
	ibValue out;
	ASSERT_TRUE(file.GetPropVal(file.FindProp(wxT("Path")), out));
	EXPECT_EQ(out.GetString(), CertPath(wxT("server.pem")));
	EXPECT_LT(file.FindProp(wxT("Password")), 0);
}

TEST(HttpSecureValues, ASecureConnectionTrustsTheSystemAndChecksUnlessSaid)
{
	ibValueSecureConnection secure;
	EXPECT_EQ(secure.ClientCertificate(), nullptr);
	EXPECT_EQ(secure.TrustedCertificates(), nullptr);
	EXPECT_TRUE(secure.VerifyServerCertificate());

	const ibValuePtr<ibValueCertificateFile> mine = Certificate(wxT("client.pem"), wxT("secret"));
	const ibValuePtr<ibValueCertificateFile> trusted = Certificate(wxT("server.pem"));
	secure.SetClientCertificate(mine);
	secure.SetTrustedCertificates(trusted);
	EXPECT_EQ(secure.ClientCertificate(), static_cast<const ibValueCertificateFile*>(mine));
	EXPECT_EQ(secure.TrustedCertificates(), static_cast<const ibValueCertificateFile*>(trusted));

	// Through the script's door: a string is not a certificate, a number is not a switch, Undefined clears.
	EXPECT_FALSE(Refusal([&] { secure.SetPropVal(secure.FindProp(wxT("ClientCertificate")), ibValue(wxString(wxT("x.pem")))); }).empty());
	EXPECT_EQ(secure.ClientCertificate(), static_cast<const ibValueCertificateFile*>(mine)) << "a refused assignment leaves what stood";
	EXPECT_FALSE(Refusal([&] { secure.SetPropVal(secure.FindProp(wxT("VerifyServerCertificate")), ibValue(0)); }).empty());
	EXPECT_TRUE(secure.VerifyServerCertificate());
	ASSERT_TRUE(secure.SetPropVal(secure.FindProp(wxT("VerifyServerCertificate")), ibValue(false)));
	EXPECT_FALSE(secure.VerifyServerCertificate());
	ASSERT_TRUE(secure.SetPropVal(secure.FindProp(wxT("TrustedCertificates")), ibValue()));
	EXPECT_EQ(secure.TrustedCertificates(), nullptr);
}

TEST(HttpSecureValues, ThePortNobodyGaveFollowsTheConnectionsKind)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("api.example"), 80, wxString(), wxString(), 30, /*portGiven*/ false);
	EXPECT_EQ(connection.Port(), 80);
	const ibValuePtr<ibValueSecureConnection> secure(new ibValueSecureConnection());
	connection.SetSecureConnection(secure);
	EXPECT_EQ(connection.Port(), 443);
	connection.SetSecureConnection(ibValue());
	EXPECT_EQ(connection.Port(), 80);

	ibValueHttpConnection given;
	given.Open(wxT("api.example"), 8443, wxString(), wxString(), 30);
	given.SetSecureConnection(secure);
	EXPECT_EQ(given.Port(), 8443) << "a port that was given is the port";

	EXPECT_FALSE(Refusal([&] { connection.SetSecureConnection(ibValue(wxString(wxT("yes")))); }).empty()) << "a string is not a SecureConnection";
	ibValue out;
	ASSERT_TRUE(given.GetPropVal(given.FindProp(wxT("SecureConnection")), out));
	EXPECT_EQ(dynamic_cast<ibValueSecureConnection*>(out.GetRef()), static_cast<ibValueSecureConnection*>(secure));
}

// The first thing a person writes for a secure connection is the scheme in the host, and the sentence that
// refuses it is the only guidance they get. Until this commit it ended "a secure connection is a property
// this build does not have yet", which stopped being true here - and a refusal that denies the feature the
// same commit adds sends the reader away from it.
TEST(HttpSecureValues, AHostWithASchemeIsToldWhereHttpsLives)
{
	ibValueHttpConnection connection;
	const wxString why = Refusal([&] { connection.Open(wxT("https://api.bank.example"), 443, wxString(), wxString(), 30, true); });
	ASSERT_FALSE(why.empty()) << "a host with a scheme was taken";
	EXPECT_TRUE(why.Contains(wxT("the host is a name"))) << Utf8(why);
	EXPECT_TRUE(why.Contains(wxT("SecureConnection"))) << "the refusal does not say where https goes: " << Utf8(why);
}

// ---------------------------------------------------------------------------
//  Over the wire
// ---------------------------------------------------------------------------

namespace {

struct ibSeen {
	std::string m_method, m_path;
};

// A TLS server on 127.0.0.1 with the test identity, and what it saw.
class HttpSecure : public ::testing::Test {
protected:
	void SetUp() override
	{
		m_certificate = Utf8(CertPath(wxT("server.pem")));
		m_key = Utf8(CertPath(wxT("server.key")));
		m_clientCa = Utf8(CertPath(wxT("client.pem")));
		Start(m_server, m_certificate, m_key, nullptr, m_port, m_thread);
	}

	void TearDown() override
	{
		Stop(m_server, m_thread);
		if (m_strict) Stop(*m_strict, m_strictThread);
	}

	void Start(httplib::SSLServer& server, const std::string& cert, const std::string& key, const char* clientCa, int& port, std::thread& thread)
	{
		ASSERT_TRUE(server.is_valid()) << "the server could not read " << cert << " / " << key;
		server.Get(".*", [this](const httplib::Request& request, httplib::Response& response) {
			{
				const std::lock_guard<std::mutex> lock(m_mutex);
				m_seen.push_back({ request.method, request.path });
			}
			response.set_content("secret answer", "text/plain");
		});
		port = server.bind_to_any_port("127.0.0.1");
		ASSERT_GT(port, 0);
		thread = std::thread([&server] { server.listen_after_bind(); });
		server.wait_until_ready();
		(void)clientCa;
	}

	static void Stop(httplib::SSLServer& server, std::thread& thread)
	{
		server.stop();
		if (thread.joinable()) {
			thread.join();
		}
	}

	// A second server that asks the client for a certificate signed by client.pem (itself: it is self-signed).
	void StartStrict()
	{
		m_strict.reset(new httplib::SSLServer(m_certificate.c_str(), m_key.c_str(), m_clientCa.c_str()));
		Start(*m_strict, m_certificate, m_key, m_clientCa.c_str(), m_strictPort, m_strictThread);
	}

	size_t Count()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_seen.size();
	}

	static ibValuePtr<ibValueHttpResponse> Ask(ibValueHttpConnection& connection, const wxChar* path = wxT("/"))
	{
		ibValueHttpRequest request;
		request.SetResourceAddress(path);
		return ibValuePtr<ibValueHttpResponse>(connection.Send(ibHttpMethod_Get, request));
	}

	std::string m_certificate, m_key, m_clientCa;
	httplib::SSLServer m_server{ Utf8(CertPath(wxT("server.pem"))).c_str(), Utf8(CertPath(wxT("server.key"))).c_str() };
	std::thread m_thread;
	int m_port = 0;
	std::unique_ptr<httplib::SSLServer> m_strict;
	std::thread m_strictThread;
	int m_strictPort = 0;
	std::mutex m_mutex;
	std::vector<ibSeen> m_seen;
};

} // namespace

TEST_F(HttpSecure, AServerWhoseCertificateIsTrustedAnswers)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("server.pem"))));
	const ibValuePtr<ibValueHttpResponse> response = Ask(connection, wxT("/balance"));
	ASSERT_TRUE(response != nullptr);
	EXPECT_EQ(response->StatusCode(), 200);
	EXPECT_EQ(response->BodyAsString(ibTextEncoding_UTF8), wxString(wxT("secret answer")));
	EXPECT_EQ(Count(), 1u);
}

TEST_F(HttpSecure, AServerTheSystemDoesNotTrustIsRefusedBeforeAnyRequestReachesIt)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(ibValuePtr<ibValueSecureConnection>(new ibValueSecureConnection()));   // the system's store
	const wxString why = Refusal([&] { Ask(connection); });
	ASSERT_FALSE(why.empty()) << "a self-signed certificate was trusted";
	EXPECT_TRUE(why.Contains(wxT("not trusted"))) << Utf8(why);
	EXPECT_TRUE(why.Contains(wxT("https://127.0.0.1"))) << Utf8(why);
	EXPECT_EQ(Count(), 0u) << "the request went out to a server that was not trusted";

	// ...and a certificate from another self-signed root is not trusted either: trust is the file named, not "any".
	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("other.pem"))));
	const wxString other = Refusal([&] { Ask(connection); });
	ASSERT_FALSE(other.empty());
	EXPECT_TRUE(other.Contains(wxT("not trusted"))) << Utf8(other);
	EXPECT_EQ(Count(), 0u);
}

TEST_F(HttpSecure, TheSwitchDropsTheCheckAndOnlyTheSwitchDoes)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(Secure(kNone, kNone, /*verify*/ false));
	const ibValuePtr<ibValueHttpResponse> response = Ask(connection);
	ASSERT_TRUE(response != nullptr) << "with the check off, a self-signed server answers";
	EXPECT_EQ(response->StatusCode(), 200);
	EXPECT_EQ(Count(), 1u);
}

TEST_F(HttpSecure, ACertificateForAnotherNameIsRefused)
{
	// server.pem is for the address 127.0.0.1 and no name; asked for as "localhost" it is the wrong certificate,
	// even though it is trusted.
	ibValueHttpConnection connection;
	connection.Open(wxT("localhost"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("server.pem"))));
	const wxString why = Refusal([&] { Ask(connection); });
	ASSERT_FALSE(why.empty()) << "a certificate for another name was taken";
	EXPECT_TRUE(why.Contains(wxT("not for this host"))) << Utf8(why);
	EXPECT_EQ(Count(), 0u);
}

TEST_F(HttpSecure, AServerThatAsksForTheClientsCertificateGetsItOrRefuses)
{
	StartStrict();
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_strictPort, wxString(), wxString(), 10);

	connection.SetSecureConnection(Secure(Certificate(wxT("client.pem"), wxT("secret")), Certificate(wxT("server.pem"))));
	const ibValuePtr<ibValueHttpResponse> shown = Ask(connection);
	ASSERT_TRUE(shown != nullptr) << "the client's certificate, with its key opened by the password, was not shown";
	EXPECT_EQ(shown->StatusCode(), 200);

	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("server.pem"))));
	EXPECT_FALSE(Refusal([&] { Ask(connection); }).empty()) << "no certificate shown, and the server let it in";

	const wxString wrong = Refusal([&] { connection.SetSecureConnection(Secure(Certificate(wxT("client.pem"), wxT("not it")), Certificate(wxT("server.pem")))); Ask(connection); });
	ASSERT_FALSE(wrong.empty()) << "a key that did not open was shown";
	EXPECT_TRUE(wrong.Contains(wxT("certificate file")) || wrong.Contains(wxT("password"))) << Utf8(wrong);
}

// The wrong kind of server on either side is a sentence, and it comes within the time allowed rather than
// never. Which sentence depends on what the other side does: a TLS server answers a plain request line fast
// enough to name the mismatch, while a plain server simply never answers a handshake - it is waiting for a
// request line that is not coming - and the time allowed is what ends that. Both are said here because the
// difference is the server's, not ours.
TEST_F(HttpSecure, TheWrongKindOfServerIsASentenceWithinTheTimeAllowed)
{
	ibValueHttpConnection plain;
	plain.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 5);
	const auto started = std::chrono::steady_clock::now();
	const wxString why = Refusal([&] { Ask(plain); });
	const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	ASSERT_FALSE(why.empty());
	EXPECT_LT(took, 4.5) << "a plain client on a TLS port: " << Utf8(why);

	// The server is stopped and joined by a destructor: a fatal assertion below returns from the function, and
	// a joinable thread destroyed on the way out ends the whole binary rather than this one test.
	httplib::Server open;
	open.Get(".*", [](const httplib::Request&, httplib::Response& response) { response.set_content("plain", "text/plain"); });
	struct ibRunning {
		httplib::Server& m_server;
		std::thread m_thread;
		explicit ibRunning(httplib::Server& server) : m_server(server), m_thread([&server] { server.listen_after_bind(); }) {}
		~ibRunning() { m_server.stop(); if (m_thread.joinable()) { m_thread.join(); } }
	};
	const int openPort = open.bind_to_any_port("127.0.0.1");
	ASSERT_GT(openPort, 0);
	const ibRunning running(open);
	open.wait_until_ready();

	ibValueHttpConnection secure;
	secure.Open(wxT("127.0.0.1"), openPort, wxString(), wxString(), 5);
	secure.SetSecureConnection(Secure(kNone, kNone, false));
	const auto began = std::chrono::steady_clock::now();
	const wxString spoken = Refusal([&] { Ask(secure); });
	const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
	ASSERT_FALSE(spoken.empty()) << "a plain server was taken for a TLS one";
	EXPECT_TRUE(spoken.Contains(wxT("does not speak TLS")) || spoken.Contains(wxT("within the 5 s"))) << Utf8(spoken);
	EXPECT_LT(waited, 8.0) << "and it did not wait forever for a handshake nobody is answering";
}

TEST_F(HttpSecure, TheConnectionFollowsItsPropertyRequestByRequest)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(Secure(kNone, kNone, false));
	ASSERT_TRUE(Ask(connection) != nullptr);
	connection.SetSecureConnection(Secure(kNone, kNone, true));                      // now the check is on: refused
	EXPECT_FALSE(Refusal([&] { Ask(connection); }).empty()) << "the switch was set after a request was made, and the old transport went on";
	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("server.pem"))));  // and trusted: answers again
	ASSERT_TRUE(Ask(connection) != nullptr);
	EXPECT_EQ(Count(), 2u);
}

// 🛑 A SecureConnection IS A VALUE A SCRIPT HOLDS, and it can be changed after a connection has been made
// with it. The connection read it once, kept the transport alive (keep-alive), and went on with the check it
// started with - off - for the rest of the session. Turning the check back on has to be obeyed, or the switch
// is a one-way door on a connection to a bank.
TEST_F(HttpSecure, ASecureConnectionChangedInPlaceIsObeyedByTheNextRequest)
{
	const ibValuePtr<ibValueSecureConnection> secure = Secure(kNone, kNone, /*verify*/ false);
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(secure);
	ASSERT_TRUE(Ask(connection) != nullptr) << "with the check off, a self-signed server answers";

	secure->SetVerifyServerCertificate(true);                  // the object, not the property of the connection
	const wxString why = Refusal([&] { Ask(connection); });
	ASSERT_FALSE(why.empty()) << "the check was turned back on and the old transport went on without it";
	EXPECT_TRUE(why.Contains(wxT("not trusted"))) << Utf8(why);

	secure->SetTrustedCertificates(Certificate(wxT("server.pem")));
	ASSERT_TRUE(Ask(connection) != nullptr) << "and naming the authority is obeyed the same way";

	// ...and one SecureConnection held by two connections is read by both, each for itself.
	ibValueHttpConnection second;
	second.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	second.SetSecureConnection(secure);
	ASSERT_TRUE(Ask(second) != nullptr);
	secure->SetTrustedCertificates(Certificate(wxT("other.pem")));
	EXPECT_FALSE(Refusal([&] { Ask(second); }).empty()) << "the second connection kept the first's trust";
	EXPECT_FALSE(Refusal([&] { Ask(connection); }).empty()) << "and so did the first";

	// ...and so are the other two halves of the shape: which certificate is shown, and the password that
	// opens its key. A server that demands one is what can tell.
	StartStrict();
	const ibValuePtr<ibValueSecureConnection> shown = Secure(kNone, Certificate(wxT("server.pem")));
	ibValueHttpConnection strict;
	strict.Open(wxT("127.0.0.1"), m_strictPort, wxString(), wxString(), 10);
	strict.SetSecureConnection(shown);
	EXPECT_FALSE(Refusal([&] { Ask(strict); }).empty()) << "the server demands a certificate and was given none";

	shown->SetClientCertificate(Certificate(wxT("client.pem"), wxT("secret")));
	EXPECT_TRUE(Ask(strict) != nullptr) << "a certificate named on the object in place was not shown";

	shown->SetClientCertificate(Certificate(wxT("client.pem"), wxT("not it")));   // the same file, another password
	EXPECT_FALSE(Refusal([&] { Ask(strict); }).empty()) << "the password was changed and the old key went on being used";

	shown->SetClientCertificate(kNone);
	EXPECT_FALSE(Refusal([&] { Ask(strict); }).empty()) << "the certificate was taken away and went on being shown";
}

// A certificate file that cannot be read is said once - and said again, because the library loads its trusted
// certificates under a std::call_once that never fires twice: a transport kept after that failure would go on
// with an empty chain, refusing every server for a reason its sentence no longer names.
TEST_F(HttpSecure, AFileThatCannotBeReadIsSaidEveryTimeItIsAsked)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("README"))));
	for (int attempt = 0; attempt < 2; attempt++) {
		const wxString why = Refusal([&] { Ask(connection); });
		ASSERT_FALSE(why.empty()) << "attempt " << attempt;
		EXPECT_TRUE(why.Contains(wxT("certificate file"))) << "attempt " << attempt << ": " << Utf8(why);
	}
	// ...and the connection is not spoiled by it: named the right file, it answers.
	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("server.pem"))));
	EXPECT_TRUE(Ask(connection) != nullptr);
}

// A connection can hold TWO files, so "a certificate file could not be read" names neither of them. The help
// says the refusal carries the name; this is what holds it to that.
TEST_F(HttpSecure, AFileThatIsNotACertificateIsSaidByName)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	connection.SetSecureConnection(Secure(kNone, Certificate(wxT("README"))));
	const wxString why = Refusal([&] { Ask(connection); });
	ASSERT_FALSE(why.empty()) << "a README was taken for a certificate";
	EXPECT_TRUE(why.Contains(wxT("certificate file"))) << Utf8(why);
	EXPECT_TRUE(why.Contains(wxT("README"))) << "the file that could not be read is not named: " << Utf8(why);
	EXPECT_EQ(Count(), 0u);
}
