#include "serverConfig.h"

#include <wx/base64.h>
#include <wx/ffile.h>
#include <wx/fileconf.h>
#include <wx/filename.h>

#ifndef __WXMSW__
#include <sys/stat.h>
#endif

#include "backend/cipher/fieldCipher.h"
#include "core/guid.h"

namespace {

// Values as written: without this flag `\t` in a Windows path (`F:\bases\trade`) is read as a tab.
constexpr long kConfStyle = wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS;

// What a sealed secret starts with.
const wxChar* const kSealed = wxT("enc:");

// The field a secret is bound to — a sealed value moved to another base or another key does not open.
ibCipherContext SecretContext(const std::vector<unsigned char>& key, const wxString& base, const wxString& name)
{
	ibCipherContext context;
	context.m_key   = key;
	context.m_field = std::string((base + wxT("/") + name).utf8_str());
	return context;
}

} // namespace

ibServerConfig::ibServerConfig(const wxString& folder) :
	m_folder(wxFileName::DirName(folder).GetPath()),   // one spelling, no trailing separator
	m_confPath(m_folder + wxFileName::GetPathSeparator() + wxT("server.conf"))
{
}

bool ibServerConfig::HasConf() const
{
	return wxFileName::FileExists(m_confPath);
}

bool ibServerConfig::LoadKey(wxString& error)
{
	const wxString path = m_folder + wxFileName::GetPathSeparator() + wxT("server.key");

	if (wxFileName::FileExists(path)) {
		wxFFile file(path, wxT("rb"));
		wxString text;
		if (!file.IsOpened() || !file.ReadAll(&text)) {
			error = wxString::Format(wxT("the key %s cannot be read"), path);
			return false;
		}
		const wxMemoryBuffer raw = wxBase64Decode(text.Trim().Trim(false));
		if (raw.GetDataLen() != ibFieldCipher::kKeySize) {
			error = wxString::Format(wxT("the key %s is not a key"), path);
			return false;
		}
		const unsigned char* bytes = static_cast<const unsigned char*>(raw.GetData());
		m_key.assign(bytes, bytes + raw.GetDataLen());
		return true;
	}

	// The first start of this installation: a key of its own, never one shipped in the release.
	std::vector<unsigned char> key = ibFieldCipher::Random(ibFieldCipher::kKeySize);
	if (key.size() != ibFieldCipher::kKeySize) {
		error = wxT("the platform gave no randomness for a key");
		return false;
	}
	wxFileName::Mkdir(m_folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	wxFFile file(path, wxT("wb"));
	if (!file.IsOpened() || !file.Write(wxBase64Encode(key.data(), key.size()) + wxT("\n"))) {
		error = wxString::Format(wxT("the key %s cannot be written"), path);
		return false;
	}
	file.Close();
#ifndef __WXMSW__
	::chmod(path.fn_str(), S_IRUSR | S_IWUSR);   // the server's user alone
#endif
	ibAppServerSay(ibJournalMark::Info, wxT("a new key for this installation: %s - keep it with the config"), path);
	m_key = std::move(key);
	return true;
}

std::vector<ibConfiguredInstance> ibServerConfig::ReadInstances() const
{
	std::vector<ibConfiguredInstance> instances;
	wxFileConfig conf(wxEmptyString, wxEmptyString, m_confPath, wxEmptyString, kConfStyle);

	wxString group;
	long cookie = 0;
	for (bool more = conf.GetFirstGroup(group, cookie); more; more = conf.GetNextGroup(group, cookie)) {
		const wxString at = wxT("/") + group + wxT("/");

		ibConfiguredInstance instance;
		instance.m_name       = group;
		instance.m_id         = conf.Read(at + wxT("Id"));
		instance.m_kind       = conf.Read(at + wxT("Kind")).Lower();
		instance.m_path       = conf.Read(at + wxT("Path"));
		instance.m_server     = conf.Read(at + wxT("Server"));
		instance.m_port       = conf.Read(at + wxT("Port"));
		instance.m_database   = conf.Read(at + wxT("Database"));
		instance.m_user       = conf.Read(at + wxT("User"));
		instance.m_password   = conf.Read(at + wxT("Password"));
		instance.m_ibUser     = conf.Read(at + wxT("IbUser"));
		instance.m_ibPassword = conf.Read(at + wxT("IbPassword"));

		instances.push_back(instance);
	}
	return instances;
}

wxString ibServerConfig::ReadHost() const
{
	wxFileConfig conf(wxEmptyString, wxEmptyString, m_confPath, wxEmptyString, kConfStyle);
	return conf.Read(wxT("/Host"));
}

bool ibServerConfig::ReadPort(unsigned short& port, wxString& error) const
{
	wxFileConfig conf(wxEmptyString, wxEmptyString, m_confPath, wxEmptyString, kConfStyle);
	const wxString written = conf.Read(wxT("/Port")).Trim().Trim(false);

	port = 0;
	if (written.IsEmpty())
		return true;

	unsigned long value = 0;
	if (!written.ToULong(&value) || value == 0 || value > 65535) {
		error = wxString::Format(wxT("Port=%s in %s is not a port"), written, m_confPath);
		return false;
	}
	port = static_cast<unsigned short>(value);
	return true;
}

void ibServerConfig::WritePort(unsigned short port) const
{
	wxFileConfig conf(wxEmptyString, wxEmptyString, m_confPath, wxEmptyString, kConfStyle);
	conf.Write(wxT("/Port"), static_cast<long>(port));
	if (!conf.Flush())
		ibAppServerSay(ibJournalMark::Warning, wxT("the config %s cannot be written - port %u is not kept"),
			m_confPath, static_cast<unsigned>(port));
}

void ibServerConfig::AssignIds(std::vector<ibConfiguredInstance>& instances) const
{
	wxFileConfig conf(wxEmptyString, wxEmptyString, m_confPath, wxEmptyString, kConfStyle);

	bool assigned = false;
	for (ibConfiguredInstance& instance : instances) {
		if (!ibGuid(instance.m_id).isValid()) {
			instance.m_id = ibGuid(ibGuid::newGuid()).str();
			conf.Write(wxT("/") + instance.m_name + wxT("/Id"), instance.m_id);
			assigned = true;
		}
		if (instance.m_path.IsEmpty())
			instance.m_path = m_folder + wxFileName::GetPathSeparator() + instance.m_id;
	}

	if (assigned && !conf.Flush())
		ibAppServerSay(ibJournalMark::Warning, wxT("the config %s cannot be written - the new Ids are not kept"), m_confPath);
}

bool ibServerConfig::OpenSecret(const wxString& base, const wxString& name, const wxString& stored, wxString& secret,
	wxString& error) const
{
	secret.clear();
	if (stored.IsEmpty())
		return true;

	wxString sealed;
	if (!stored.StartsWith(kSealed, &sealed)) {
		error = wxString::Format(wxT("%s/%s is written in plain text - seal it: appserver --set-password=%s/%s"),
			base, name, base, name);
		return false;
	}

	const wxMemoryBuffer raw = wxBase64Decode(sealed);
	const unsigned char* bytes = static_cast<const unsigned char*>(raw.GetData());
	std::vector<unsigned char> plain;
	if (!ibFieldCipher::Decrypt(std::vector<unsigned char>(bytes, bytes + raw.GetDataLen()),
			SecretContext(m_key, base, name), plain)) {
		error = wxString::Format(wxT("%s/%s does not open with this installation's key"), base, name);
		return false;
	}
	secret = wxString::FromUTF8(reinterpret_cast<const char*>(plain.data()), plain.size());
	return true;
}

bool ibServerConfig::SealSecret(const wxString& base, const wxString& name, const wxString& secret,
	wxString& error) const
{
	const wxScopedCharBuffer utf8 = secret.utf8_str();
	const std::vector<unsigned char> plain(utf8.data(), utf8.data() + utf8.length());

	const std::vector<unsigned char> sealed = ibFieldCipher::Encrypt(plain, SecretContext(m_key, base, name));
	if (sealed.empty()) {
		error = wxString::Format(wxT("%s/%s could not be sealed"), base, name);
		return false;
	}

	wxFileConfig conf(wxEmptyString, wxEmptyString, m_confPath, wxEmptyString, kConfStyle);
	conf.Write(wxT("/") + base + wxT("/") + name, kSealed + wxBase64Encode(sealed.data(), sealed.size()));
	if (!conf.Flush()) {
		error = wxString::Format(wxT("the config %s cannot be written"), m_confPath);
		return false;
	}
	return true;
}
