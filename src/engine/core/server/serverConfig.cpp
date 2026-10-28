#include "serverConfig.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include <wx/fileconf.h>

#ifndef __IBMSW__
#include <sys/stat.h>
#endif

#include "core/crypto/fieldCipher.h"
#include "core/fstring.h"
#include "core/guid.h"
#include "core/fileSystem.h"
#include "core/server/clusterControl.h"   // ibClusterControl::TokenFile
#include "core/utils/base64.h"   // ibBase64 — the key's file and a sealed secret, in text

namespace {

// Values as written: without this flag `\t` in a Windows path (`F:\bases\trade`) is read as a tab.
constexpr long kConfStyle = wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS;

// What a sealed secret starts with.
const ibChar* const kSealed = ibT("enc:");

// The cluster's token: as many random bytes as a key has.
constexpr std::size_t kTokenBytes = ibFieldCipher::kKeySize;

// The field a secret is bound to — a sealed value moved to another base or another key does not open.
ibCipherContext SecretContext(const std::vector<unsigned char>& key, const ibString& base, const ibString& name)
{
	ibCipherContext context;
	context.m_key   = key;
	context.m_field = (base + ibT("/") + name).ToUtf8();
	return context;
}

// The whole file, as its bytes — false when it cannot be opened or read.
bool ReadAll(const ibString& path, std::string& bytes)
{
	std::ifstream file(ibPathOf(path), std::ios::binary);
	if (!file)
		return false;
	bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return !file.bad();
}

// `bytes` as the whole file, made or replaced — false when it cannot be written.
bool WriteAll(const ibString& path, const std::string& bytes)
{
	std::ofstream file(ibPathOf(path), std::ios::binary | std::ios::trunc);
	file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	file.close();
	return !file.fail();
}

} // namespace

ibServerConfig::ibServerConfig(const ibString& folder) :
	m_folder(ibString((ibPathOf(folder) / "").parent_path().wstring())),   // one spelling, no trailing separator
	m_confPath(m_folder + ibFILE_SEP_PATH + ibT("server.conf"))
{
}

ibString ibServerConfig::DefaultFolder()
{
	return ibProgramFolder() + ibFILE_SEP_PATH + ibT("server");
}

bool ibServerConfig::HasConf() const
{
	return ibFileExists(m_confPath);
}

bool ibServerConfig::LoadKey(ibString& error)
{
	const ibString path = m_folder + ibFILE_SEP_PATH + ibT("server.key");

	if (ibFileExists(path)) {
		std::string read;
		if (!ReadAll(path, read)) {
			error = ibString::Format(ibT("the key %s cannot be read"), path);
			return false;
		}
		ibString text = ibString::FromUTF8(read.data(), read.size());
		text.Trim().Trim(false);
		ibMemoryBuffer raw;
		if (!ibBase64::Read(text, raw) || raw.GetDataLen() != ibFieldCipher::kKeySize) {
			error = ibString::Format(ibT("the key %s is not a key"), path);
			return false;
		}
		const unsigned char* bytes = static_cast<const unsigned char*>(raw.GetData());
		m_key.assign(bytes, bytes + raw.GetDataLen());
		return true;
	}

	// The first start of this installation: a key of its own, never one shipped in the release.
	std::vector<unsigned char> key = ibFieldCipher::Random(ibFieldCipher::kKeySize);
	if (key.size() != ibFieldCipher::kKeySize) {
		error = ibT("the platform gave no randomness for a key");
		return false;
	}
	std::error_code made;
	std::filesystem::create_directories(ibPathOf(m_folder), made);
	std::string line;
	ibBase64::Append(line, key.data(), key.size());
	line += '\n';
	if (!WriteAll(path, line)) {
		error = ibString::Format(ibT("the key %s cannot be written"), path);
		return false;
	}
#ifndef __IBMSW__
	::chmod(path.ToUtf8().c_str(), S_IRUSR | S_IWUSR);   // the server's user alone
#endif
	ibAppServerSay(ibJournalMark::Info, ibT("a new key for this installation: %s - keep it with the config"), path);
	m_key = std::move(key);
	return true;
}

bool ibServerConfig::WriteClusterToken(ibString& token, ibString& error) const
{
	const std::vector<unsigned char> random = ibFieldCipher::Random(kTokenBytes);
	if (random.size() != kTokenBytes) {
		error = ibT("the platform gave no randomness for the cluster's token");
		return false;
	}
	// In hex: it goes in a query as it is.
	static const char digits[] = "0123456789abcdef";
	std::string line;
	for (const unsigned char byte : random) {
		line += digits[byte >> 4];
		line += digits[byte & 0x0F];
	}

	const ibString path = m_folder + ibFILE_SEP_PATH + ibString(ibClusterControl::TokenFile);
	if (!WriteAll(path, line)) {
		error = ibString::Format(ibT("the cluster's token %s cannot be written"), path);
		return false;
	}
#ifndef __IBMSW__
	::chmod(path.ToUtf8().c_str(), S_IRUSR | S_IWUSR);   // the server's user alone
#endif
	token = ibString::FromUTF8(line.data(), line.size());
	return true;
}

bool ibServerConfig::ReadClusterToken(ibString& token, ibString& error) const
{
	const ibString path = m_folder + ibFILE_SEP_PATH + ibString(ibClusterControl::TokenFile);
	// No file is an answer, not a fault — the tray asks every two seconds.
	if (!ibFileExists(path)) {
		error = ibString::Format(ibT("there is no %s - the cluster has not started"), path);
		return false;
	}
	std::string read;
	if (!ReadAll(path, read)) {
		error = ibString::Format(ibT("the cluster's token %s cannot be read"), path);
		return false;
	}
	token = ibString::FromUTF8(read.data(), read.size()).TrimAll();
	if (token.IsEmpty()) {
		error = ibString::Format(ibT("the cluster's token %s is empty"), path);
		return false;
	}
	return true;
}

bool ibServerConfig::RemoveClusterToken(ibString& error) const
{
	const ibString path = m_folder + ibFILE_SEP_PATH + ibString(ibClusterControl::TokenFile);
	if (!ibFileExists(path) || ibRemoveFile(path))
		return true;
	error = ibString::Format(ibT("the cluster's token %s cannot be removed"), path);
	return false;
}

std::vector<ibConfiguredInstance> ibServerConfig::ReadInstances() const
{
	std::vector<ibConfiguredInstance> instances;
	wxFileConfig conf(ibEmptyString, ibEmptyString, m_confPath, ibEmptyString, kConfStyle);

	wxString group;   // wxFileConfig's own string: the group's name is written into it
	long cookie = 0;
	for (bool more = conf.GetFirstGroup(group, cookie); more; more = conf.GetNextGroup(group, cookie)) {
		const ibString at = ibT("/") + group + ibT("/");

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
		instance.m_mcpPort    = conf.Read(at + wxT("McpPort"));
		long processes = 1;
		conf.Read(at + wxT("Processes"), &processes, 1L);
		instance.m_processes  = processes > 1 ? static_cast<unsigned>(processes) : 1;

		instances.push_back(instance);
	}
	return instances;
}

ibString ibServerConfig::ReadHost() const
{
	wxFileConfig conf(ibEmptyString, ibEmptyString, m_confPath, ibEmptyString, kConfStyle);
	return conf.Read(wxT("/Host"), wxT("127.0.0.1"));
}

bool ibServerConfig::ReadPort(unsigned short& port, ibString& error) const
{
	wxFileConfig conf(ibEmptyString, ibEmptyString, m_confPath, ibEmptyString, kConfStyle);
	ibString written = conf.Read(wxT("/Port"));
	written.Trim().Trim(false);

	port = 0;
	if (written.IsEmpty())
		return true;

	unsigned long value = 0;
	if (!written.ToULong(&value) || value == 0 || value > 65535) {
		error = ibString::Format(ibT("Port=%s in %s is not a port"), written, m_confPath);
		return false;
	}
	port = static_cast<unsigned short>(value);
	return true;
}

void ibServerConfig::WritePort(unsigned short port) const
{
	wxFileConfig conf(ibEmptyString, ibEmptyString, m_confPath, ibEmptyString, kConfStyle);
	conf.Write(wxT("/Port"), static_cast<long>(port));
	if (!conf.Flush())
		ibAppServerSay(ibJournalMark::Warning, ibT("the config %s cannot be written - port %u is not kept"),
			m_confPath, static_cast<unsigned>(port));
}

void ibServerConfig::AssignIds(std::vector<ibConfiguredInstance>& instances) const
{
	wxFileConfig conf(ibEmptyString, ibEmptyString, m_confPath, ibEmptyString, kConfStyle);

	bool assigned = false;
	for (ibConfiguredInstance& instance : instances) {
		if (!ibGuid(instance.m_id).isValid()) {
			instance.m_id = ibGuid(ibGuid::newGuid()).str();
			conf.Write(wxT("/") + instance.m_name + wxT("/Id"), static_cast<wxString>(instance.m_id));
			assigned = true;
		}
		if (instance.m_path.IsEmpty())
			instance.m_path = m_folder + ibFILE_SEP_PATH + instance.m_id;
	}

	if (assigned && !conf.Flush())
		ibAppServerSay(ibJournalMark::Warning, ibT("the config %s cannot be written - the new Ids are not kept"), m_confPath);
}

void ibServerConfig::AssignMcpPorts(std::vector<ibConfiguredInstance>& instances) const
{
	// Every port written, a mistaken one too: what a person wrote is theirs, and said where it is read.
	std::set<unsigned long> taken;
	for (const ibConfiguredInstance& instance : instances) {
		unsigned long port = 0;
		if (instance.m_mcpPort.ToULong(&port))
			taken.insert(port);
	}

	wxFileConfig conf(ibEmptyString, ibEmptyString, m_confPath, ibEmptyString, kConfStyle);

	bool assigned = false;
	unsigned long next = ibPort::BaseAssistant.m_first;
	for (ibConfiguredInstance& instance : instances) {
		if (!instance.m_mcpPort.IsEmpty())
			continue;
		while (taken.count(next) != 0)
			++next;
		if (next > ibPort::BaseAssistant.Last()) {
			ibAppServerSay(ibJournalMark::Warning, ibT("base '%s': no assistant port left from %u to %u - write its McpPort"),
				instance.m_name, static_cast<unsigned>(ibPort::BaseAssistant.m_first),
				static_cast<unsigned>(ibPort::BaseAssistant.Last()));
			continue;
		}
		instance.m_mcpPort = ibString::Format(ibT("%lu"), next);
		conf.Write(wxT("/") + instance.m_name + wxT("/McpPort"), static_cast<long>(next));
		taken.insert(next);
		assigned = true;
	}

	if (assigned && !conf.Flush())
		ibAppServerSay(ibJournalMark::Warning, ibT("the config %s cannot be written - the assistant ports are not kept"),
			m_confPath);
}

bool ibServerConfig::OpenSecret(const ibString& base, const ibString& name, const ibString& stored, ibString& secret,
	ibString& error) const
{
	secret.Clear();
	if (stored.IsEmpty())
		return true;

	ibString sealed;
	if (!stored.StartsWith(kSealed, &sealed)) {
		error = ibString::Format(ibT("%s/%s is written in plain text - seal it: appserver --set-password=%s/%s"),
			base, name, base, name);
		return false;
	}

	ibMemoryBuffer raw;
	const bool decoded = ibBase64::Read(sealed, raw);
	const unsigned char* bytes = static_cast<const unsigned char*>(raw.GetData());
	std::vector<unsigned char> plain;
	if (!decoded || !ibFieldCipher::Decrypt(std::vector<unsigned char>(bytes, bytes + raw.GetDataLen()),
			SecretContext(m_key, base, name), plain)) {
		error = ibString::Format(ibT("%s/%s does not open with this installation's key"), base, name);
		return false;
	}
	secret = ibString::FromUTF8(reinterpret_cast<const char*>(plain.data()), plain.size());
	return true;
}

bool ibServerConfig::SealSecret(const ibString& base, const ibString& name, const ibString& secret,
	ibString& error) const
{
	const std::string utf8 = secret.ToUtf8();
	const std::vector<unsigned char> plain(utf8.begin(), utf8.end());

	const std::vector<unsigned char> sealed = ibFieldCipher::Encrypt(plain, SecretContext(m_key, base, name));
	if (sealed.empty()) {
		error = ibString::Format(ibT("%s/%s could not be sealed"), base, name);
		return false;
	}

	wxFileConfig conf(ibEmptyString, ibEmptyString, m_confPath, ibEmptyString, kConfStyle);
	ibString text(kSealed);
	ibBase64::Append(text, sealed.data(), sealed.size());
	conf.Write(wxT("/") + base + wxT("/") + name, static_cast<wxString>(text));
	if (!conf.Flush()) {
		error = ibString::Format(ibT("the config %s cannot be written"), m_confPath);
		return false;
	}
	return true;
}
