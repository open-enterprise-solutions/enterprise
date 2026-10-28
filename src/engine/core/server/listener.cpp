////////////////////////////////////////////////////////////////////////////
//	Description : the listener of a server of this platform - TLS when backend.conf names a certificate (listener.h)
////////////////////////////////////////////////////////////////////////////

#include "core/server/listener.h"

#include "core/exception.h"   // a listener refused is an exception with the reason
#include "core/fileSystem.h"  // ibFileExists

#include <wx/confbase.h>
#include <wx/fileconf.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>

namespace {

// Longer than any certificate chain with its key; a file past it is not one, and is not read into memory.
constexpr std::size_t kMostPemBytes = 1u << 20;

ibString Resolved(const ibString& value, const ibString& folder)
{
	// wxFileConfig hands a value back with its blanks: "server.pem " is not a file.
	const ibString path = ibString(value).Trim(true).Trim(false);
	if (path.IsEmpty())
		return path;
	// An absolute one is normalised too: "F:/certs/server.key" comes back as the system writes it.
	const std::filesystem::path name = ibPathOf(path);
	const std::filesystem::path full = name.is_relative() && !folder.IsEmpty() ? ibPathOf(folder) / name : name;
	return ibString(full.lexically_normal().wstring());
}

// The file's bytes into `bytes`, or a refusal naming it. Read HERE, by the name in whatever script, and handed
// to the server as text: the server reads no file of backend.conf's.
void ReadWhole(const ibString& file, std::string& bytes)
{
	std::error_code error;
	const std::uintmax_t length = std::filesystem::file_size(ibPathOf(file), error);
	std::ifstream in(ibPathOf(file), std::ios::binary);
	if (error || !in)
		ibCoreException::Error(_("The file %s named in backend.conf could not be read."), file);
	if (length > kMostPemBytes)
		ibCoreException::Error(_("The file %s named in backend.conf is larger than a certificate or a key is."), file);
	bytes.resize(static_cast<std::size_t>(length));
	if (length > 0 && !in.read(&bytes[0], static_cast<std::streamsize>(bytes.size())))
		ibCoreException::Error(_("The file %s named in backend.conf could not be read."), file);
}

} // namespace

ibTlsFiles ibTlsFiles::FromConf(const wxConfigBase& conf, const ibString& folder)
{
	ibTlsFiles files;
	wxString value;   // wxConfigBase's own string: the value is written into it
	if (conf.Read(wxT("Certificate"), &value))
		files.m_certificate = Resolved(value, folder);
	if (conf.Read(wxT("PrivateKey"), &value))
		files.m_privateKey = Resolved(value, folder);
	return files;
}

ibTlsFiles ibTlsFiles::FromConf(const ibString& path)
{
	if (path.IsEmpty() || !ibFileExists(path))
		return ibTlsFiles();
	// Read as the host reads it (ibApplicationHost::ReadBackendConf).
	const wxFileConfig conf(wxT(""), wxT(""), path, wxT(""), wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);
	return FromConf(conf, ibString(ibPathOf(path).parent_path().wstring()));
}

ibHttpServerOptions ibListenerOptions(const ibString& address, std::uint16_t port, const ibTlsFiles& tls)
{
	ibHttpServerOptions options;
	options.m_host = std::string(address.utf8_str());
	if (!tls.IsNamed()) {
		if (!ibIsLoopback(address))
			ibCoreException::Error(_("%s is not this machine's own address, and backend.conf names no certificate: the server would listen in the clear. Name Certificate and PrivateKey there, or keep the address at 127.0.0.1."), address);
		options.m_httpPort = port;
		return options;
	}
	if (tls.m_certificate.IsEmpty() || tls.m_privateKey.IsEmpty())
		ibCoreException::Error(_("backend.conf names a Certificate without a PrivateKey, or the key without the certificate: the two go together."));
	for (const ibString& file : { tls.m_certificate, tls.m_privateKey }) {
		if (!ibFileExists(file))
			ibCoreException::Error(_("The file %s named in backend.conf is not there."), file);
	}
	// Both read here, or the whole listener is refused.
	ReadWhole(tls.m_certificate, options.m_certificate);
	ReadWhole(tls.m_privateKey, options.m_privateKey);
	options.m_httpsPort = port;
	return options;
}
