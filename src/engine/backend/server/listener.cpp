////////////////////////////////////////////////////////////////////////////
//	Description : the listener of a server of this platform - TLS when backend.conf names a certificate (listener.h)
////////////////////////////////////////////////////////////////////////////

#include "backend/server/listener.h"

#include "backend/backend_exception.h"   // a listener refused is an exception with the reason

#include <wx/confbase.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <cstddef>
#include <initializer_list>
#include <string>

// As in mcp/mcpServer.cpp, which says why: wx and cpp-httplib both alias ssize_t, and MSVC refuses the second.
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include <cpp-httplib/httplib.h>

#ifdef CPPHTTPLIB_SSL_ENABLED
#	include <mbedtls/platform_util.h>   // mbedtls_platform_zeroize - the key's bytes wiped after the server took them
#endif

namespace {

// Longer than any certificate chain with its key; a file past it is not one, and is not read into memory.
constexpr std::size_t kMostPemBytes = 1u << 20;

wxString Resolved(const wxString& value, const wxString& folder)
{
	// wxFileConfig hands a value back with its blanks: "server.pem " is not a file.
	const wxString path = wxString(value).Trim(true).Trim(false);
	if (path.IsEmpty())
		return path;
	wxFileName name(path);
	if (name.IsRelative() && !folder.IsEmpty())
		name.MakeAbsolute(folder);
	return name.GetFullPath();
}

// The file's bytes into `bytes`, or a refusal naming it. Read HERE and handed to the library as memory:
// the library would open the path itself with fopen, which on Windows takes the ANSI code page - a
// certificate under a profile folder spelled in another script would be "not PEM" by that road.
void ReadWhole(const wxString& file, std::string& bytes)
{
	// wxFFile reports a file it cannot open through wxLog, which in the designer is a pane that pops up; the
	// refusal below is the sentence, and it goes to the caller.
	wxLogNull quiet;
	wxFFile in(file, wxT("rb"));
	const wxFileOffset length = in.IsOpened() ? in.Length() : wxInvalidOffset;
	if (length < 0)
		ibBackendCoreException::Error(_("The file %s named in backend.conf could not be read."), file);
	if (static_cast<unsigned long long>(length) > kMostPemBytes)
		ibBackendCoreException::Error(_("The file %s named in backend.conf is larger than a certificate or a key is."), file);
	bytes.resize(static_cast<std::size_t>(length));
	if (length > 0 && in.Read(&bytes[0], bytes.size()) != bytes.size())
		ibBackendCoreException::Error(_("The file %s named in backend.conf could not be read."), file);
}

// An IPv4 address of 127.0.0.0/8, by number: four decimal parts, the first 127 - not a name that begins
// with "127." (127.example.com resolves wherever its owner says).
bool IsLoopbackIPv4(const wxString& address)
{
	unsigned long parts[4];
	std::size_t count = 0, at = 0;
	for (;;) {
		const std::size_t dot = address.find(wxT('.'), at);
		const wxString part = address.Mid(at, dot == wxString::npos ? wxString::npos : dot - at);
		if (count == 4 || part.IsEmpty() || part.length() > 3 || part.find_first_not_of(wxT("0123456789")) != wxString::npos
			|| !part.ToCULong(&parts[count]) || parts[count] > 255)
			return false;   // a fifth part, or one that is not a number of a byte
		++count;
		if (dot == wxString::npos)
			break;
		at = dot + 1;
	}
	return count == 4 && parts[0] == 127;
}

// This machine's own addresses, as a listener binds them: 127.0.0.0/8 by number, ::1, localhost.
bool IsLoopback(const wxString& address)
{
	const wxString own = wxString(address).Trim(true).Trim(false).Lower();
	return own == wxT("localhost") || own == wxT("::1") || IsLoopbackIPv4(own);
}

} // namespace

ibTlsFiles ibTlsFiles::FromConf(const wxConfigBase& conf, const wxString& folder)
{
	ibTlsFiles files;
	wxString value;
	if (conf.Read(wxT("Certificate"), &value))
		files.m_certificate = Resolved(value, folder);
	if (conf.Read(wxT("PrivateKey"), &value))
		files.m_privateKey = Resolved(value, folder);
	return files;
}

std::unique_ptr<httplib::Server> ibMakeListener(const wxString& address, const ibTlsFiles& tls)
{
	if (!tls.IsNamed()) {
		if (!IsLoopback(address))
			ibBackendCoreException::Error(_("%s is not this machine's own address, and backend.conf names no certificate: the server would listen in the clear. Name Certificate and PrivateKey there, or keep the address at 127.0.0.1."), address);
		return std::unique_ptr<httplib::Server>(new httplib::Server());
	}
	if (tls.m_certificate.IsEmpty() || tls.m_privateKey.IsEmpty())
		ibBackendCoreException::Error(_("backend.conf names a Certificate without a PrivateKey, or the key without the certificate: the two go together."));
	for (const wxString& file : { tls.m_certificate, tls.m_privateKey }) {
		if (!wxFileName::FileExists(file))
			ibBackendCoreException::Error(_("The file %s named in backend.conf is not there."), file);
	}
#ifdef CPPHTTPLIB_SSL_ENABLED
	// Both read here, or the whole listener is refused: httplib's server made over a certificate it could not
	// read answers is_valid() false and nothing else.
	std::string certificate, privateKey;
	// The key's bytes are wiped on every way out: the server has its own copy by then.
	struct ibWiped {
		std::string& m_bytes;
		~ibWiped() { if (!m_bytes.empty()) mbedtls_platform_zeroize(&m_bytes[0], m_bytes.size()); }
	} const wiped{ privateKey };
	ReadWhole(tls.m_certificate, certificate);
	ReadWhole(tls.m_privateKey, privateKey);
	httplib::SSLServer::PemMemory pem{};
	pem.cert_pem = certificate.data();
	pem.cert_pem_len = certificate.size();
	pem.key_pem = privateKey.data();
	pem.key_pem_len = privateKey.size();
	std::unique_ptr<httplib::SSLServer> server(new httplib::SSLServer(pem));
	if (!server->is_valid())
		ibBackendCoreException::Error(_("The certificate %s or its key %s could not be read: not PEM, a key under a password, or the key is not the certificate's."), tls.m_certificate, tls.m_privateKey);
	return server;
#else
	ibBackendCoreException::Error(_("backend.conf names a certificate, and this build has no TLS."));
	return nullptr;
#endif
}
