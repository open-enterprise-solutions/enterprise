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

// The file's bytes into `bytes`, or a refusal naming it. Read HERE, by the name in whatever script, and handed
// to the server as text: the server is the core's and reads no file of backend.conf's.
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

} // namespace

bool ibIsLoopback(const wxString& address)
{
	const wxString own = wxString(address).Trim(true).Trim(false).Lower();
	return own == wxT("localhost") || own == wxT("::1") || IsLoopbackIPv4(own);
}

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

ibHttpServerOptions ibListenerOptions(const wxString& address, std::uint16_t port, const ibTlsFiles& tls)
{
	ibHttpServerOptions options;
	options.m_host = std::string(address.utf8_str());
	if (!tls.IsNamed()) {
		if (!ibIsLoopback(address))
			ibBackendCoreException::Error(_("%s is not this machine's own address, and backend.conf names no certificate: the server would listen in the clear. Name Certificate and PrivateKey there, or keep the address at 127.0.0.1."), address);
		options.m_httpPort = port;
		return options;
	}
	if (tls.m_certificate.IsEmpty() || tls.m_privateKey.IsEmpty())
		ibBackendCoreException::Error(_("backend.conf names a Certificate without a PrivateKey, or the key without the certificate: the two go together."));
	for (const wxString& file : { tls.m_certificate, tls.m_privateKey }) {
		if (!wxFileName::FileExists(file))
			ibBackendCoreException::Error(_("The file %s named in backend.conf is not there."), file);
	}
	// Both read here, or the whole listener is refused.
	ReadWhole(tls.m_certificate, options.m_certificate);
	ReadWhole(tls.m_privateKey, options.m_privateKey);
	options.m_httpsPort = port;
	return options;
}
