////////////////////////////////////////////////////////////////////////////
//	Description : the listener of a server of this platform (backend/server/listener.h) - TLS over the files
//	backend.conf names, plain on this machine's own addresses otherwise, refused off them. The identities are
//	tests/certs/ (self-signed, made once); the servers come up on 127.0.0.1.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <memory>
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

#include "backend/server/listener.h"

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

// A listener brought up on a free port of 127.0.0.1, answering "ok" at /, and taken down with the test.
struct Up {
	std::unique_ptr<httplib::Server> m_server;
	std::thread m_thread;
	int m_port = 0;
	explicit Up(std::unique_ptr<httplib::Server> server) : m_server(std::move(server)) {
		m_server->Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content("ok", "text/plain"); });
		m_port = m_server->bind_to_any_port("127.0.0.1");
		m_thread = std::thread([this] { m_server->listen_after_bind(); });
		if (m_port > 0)
			m_server->wait_until_ready();
	}
	// Stopped and joined here, so that a fatal assertion in a test returns from it and does not end the binary.
	~Up() {
		m_server->stop();
		if (m_thread.joinable())
			m_thread.join();
	}
};

} // namespace

// The files named: the listener speaks TLS, and a client that trusts the certificate reads the answer.
TEST(Listener, NamedFilesMakeATlsListener)
{
	wxInitializer init;
	wxString why;
	std::unique_ptr<httplib::Server> server = ibMakeListener(wxT("127.0.0.1"), TestIdentity(), &why);
	ASSERT_NE(server, nullptr) << why.ToStdString();
	const Up up(std::move(server));
	ASSERT_GT(up.m_port, 0);

	// The TLS server was made in backend.dll and is served through it (its virtuals), the client here is this
	// binary's own copy of the library - two copies of Mbed TLS, each given its mutexes by its own module.
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

// Nothing named: a plain listener, on this machine's own address.
TEST(Listener, NoFilesMakeAPlainListenerOnLoopback)
{
	wxInitializer init;
	wxString why;
	std::unique_ptr<httplib::Server> server = ibMakeListener(wxT("127.0.0.1"), ibTlsFiles(), &why);
	ASSERT_NE(server, nullptr) << why.ToStdString();
	const Up up(std::move(server));
	ASSERT_GT(up.m_port, 0);
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
	wxString why;
	EXPECT_EQ(nullptr, ibMakeListener(wxT("0.0.0.0"), ibTlsFiles(), &why));
	EXPECT_TRUE(why.Contains(wxT("backend.conf"))) << why.ToStdString();
	EXPECT_TRUE(why.Contains(wxT("0.0.0.0"))) << why.ToStdString();
	EXPECT_EQ(nullptr, ibMakeListener(wxT("192.168.1.10"), ibTlsFiles(), &why));
	EXPECT_NE(nullptr, ibMakeListener(wxT("0.0.0.0"), TestIdentity(), &why)) << why.ToStdString();
	EXPECT_NE(nullptr, ibMakeListener(wxT("localhost"), ibTlsFiles(), &why));
	EXPECT_NE(nullptr, ibMakeListener(wxT("::1"), ibTlsFiles(), &why));
}

// A certificate mislaid is a refusal that names the file - never a listener in the clear; one file
// without the other is a refusal too.
TEST(Listener, MislaidFilesAreARefusalNotAFallback)
{
	wxInitializer init;
	wxString why;
	ibTlsFiles files = TestIdentity();
	files.m_certificate = CertPath(wxT("nobody-wrote-this.pem"));
	EXPECT_EQ(nullptr, ibMakeListener(wxT("127.0.0.1"), files, &why));
	EXPECT_TRUE(why.Contains(wxT("nobody-wrote-this.pem"))) << why.ToStdString();
	EXPECT_TRUE(why.Contains(wxT("is not there"))) << why.ToStdString();   // said before any reader is asked

	ibTlsFiles half = TestIdentity();
	half.m_privateKey.clear();
	EXPECT_EQ(nullptr, ibMakeListener(wxT("127.0.0.1"), half, &why));
	EXPECT_TRUE(why.Contains(wxT("PrivateKey"))) << why.ToStdString();

	// A key that is there, well formed, and not the certificate's: refused by the one who reads the pair.
	ibTlsFiles wrong = TestIdentity();
	wrong.m_privateKey = CertPath(wxT("stranger.key"));
	EXPECT_EQ(nullptr, ibMakeListener(wxT("127.0.0.1"), wrong, &why));
	EXPECT_TRUE(why.Contains(wxT("stranger.key"))) << why.ToStdString();
	EXPECT_TRUE(why.Contains(wxT("not the certificate's"))) << why.ToStdString();

	// A file that is there and is not a key at all: refused the same way.
	ibTlsFiles notAKey = TestIdentity();
	notAKey.m_privateKey = CertPath(wxT("other.pem"));
	EXPECT_EQ(nullptr, ibMakeListener(wxT("127.0.0.1"), notAKey, &why));
	EXPECT_TRUE(why.Contains(wxT("other.pem"))) << why.ToStdString();

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
	EXPECT_EQ(nullptr, ibMakeListener(wxT("127.0.0.1"), tooBig, &why));
	EXPECT_TRUE(why.Contains(wxT("larger than a certificate"))) << why.ToStdString();
}

// This machine's own addresses, as the door knows them - asked through the door: the clear is taken on them
// and refused on everything else.
TEST(Listener, LoopbackIsKnownByItsNames)
{
	wxInitializer init;
	wxString why;
	for (const wxChar* own : { wxT("127.0.0.1"), wxT("127.5.5.5"), wxT("127.255.255.255"), wxT(" 127.0.0.1 "), wxT("localhost"), wxT("LocalHost"), wxT("::1") })
		EXPECT_NE(nullptr, ibMakeListener(own, ibTlsFiles(), &why)) << own << ": " << why.ToStdString();
	// By number, not by the look of a name: what begins with "127." and is not an address resolves wherever
	// its owner says, and a name that resolves to loopback today is still a name. [::1] is not how a listener
	// binds the address, so it is not how it is written here.
	for (const wxChar* other : { wxT("0.0.0.0"), wxT("::"), wxT("192.168.1.10"), wxT("10.0.0.1"), wxT("example.com"), wxT(""),
			wxT("127.example.com"), wxT("127.0.0.1.evil"), wxT("127"), wxT("127.0.0"), wxT("127.0.0.256"), wxT("127.0.0.+1"), wxT("1270.0.0.1"), wxT("localhost."), wxT("[::1]") })
		EXPECT_EQ(nullptr, ibMakeListener(other, ibTlsFiles(), &why)) << other;
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

// The process's own file: backend.conf in the working directory, read as the engine reads its settings,
// with every backslash of a Windows path kept - C:\oes\tests\certs\server.key is a path, not a tab in it.
TEST(Listener, TheProcessConfIsReadFromTheWorkingDirectoryAsWritten)
{
	wxInitializer init;
	// Back where the process was, and the folder gone, on every way out - a fatal assertion's too.
	struct ibInFolder {
		wxString m_was, m_folder;
		ibInFolder() : m_was(wxGetCwd()),
			m_folder(wxFileName::GetTempDir() + wxFILE_SEP_PATH + wxString::Format(wxT("oes-listener-%lu"), wxGetProcessId())) {}
		~ibInFolder() {
			wxSetWorkingDirectory(m_was);
			wxRemoveFile(m_folder + wxFILE_SEP_PATH + wxT("backend.conf"));
			wxRmdir(m_folder);
		}
	};
	const ibInFolder here;
	ASSERT_TRUE(wxFileName::Mkdir(here.m_folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) << here.m_folder.ToStdString();
	ASSERT_TRUE(wxSetWorkingDirectory(here.m_folder)) << here.m_folder.ToStdString();
	{
		wxFFile conf(here.m_folder + wxFILE_SEP_PATH + wxT("backend.conf"), wxT("w"));
		ASSERT_TRUE(conf.IsOpened());
		conf.Write(wxT("Locale = en\nCertificate = ") + CertPath(wxT("server.pem")) + wxT("\nPrivateKey = ") + CertPath(wxT("server.key")) + wxT("\n"));
	}
	const ibTlsFiles files = ibTlsFiles::FromBackendConf();
	EXPECT_EQ(CertPath(wxT("server.pem")), files.m_certificate);
	EXPECT_EQ(CertPath(wxT("server.key")), files.m_privateKey);

	// And the listener made over what the file names is the TLS one.
	wxString why;
	EXPECT_NE(nullptr, ibMakeListener(wxT("0.0.0.0"), files, &why)) << why.ToStdString();
}
