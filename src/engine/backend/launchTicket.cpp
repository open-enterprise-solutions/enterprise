#include "backend/launchTicket.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>

#include "core/diagnostics/journal.h"

namespace {

constexpr char kMagic[4] = { 'O', 'E', 'S', '1' };

bool AnyPassword(const ibLaunchSecrets& secrets)
{
	return !secrets.m_dbPassword.IsEmpty() || !secrets.m_ibPassword.IsEmpty();
}

bool FieldFits(const wxString& value)
{
	const wxScopedCharBuffer utf8 = value.utf8_str();
	return utf8.length() <= ibLaunchSecrets::kMaxFieldBytes;
}

void PutField(std::vector<unsigned char>& out, const wxString& value)
{
	const wxScopedCharBuffer utf8 = value.utf8_str();
	const std::uint32_t n = static_cast<std::uint32_t>(utf8.length());
	out.push_back(static_cast<unsigned char>(n & 0xffu));
	out.push_back(static_cast<unsigned char>((n >> 8) & 0xffu));
	out.push_back(static_cast<unsigned char>((n >> 16) & 0xffu));
	out.push_back(static_cast<unsigned char>((n >> 24) & 0xffu));
	if (n == 0)
		return;
	out.insert(out.end(),
	           reinterpret_cast<const unsigned char*>(utf8.data()),
	           reinterpret_cast<const unsigned char*>(utf8.data()) + n);
}

bool TakeField(const unsigned char*& p, const unsigned char* end, wxString& value)
{
	if (end - p < 4)
		return false;
	const std::uint32_t n = static_cast<std::uint32_t>(p[0])
		| (static_cast<std::uint32_t>(p[1]) << 8)
		| (static_cast<std::uint32_t>(p[2]) << 16)
		| (static_cast<std::uint32_t>(p[3]) << 24);
	p += 4;
	if (n > ibLaunchSecrets::kMaxFieldBytes || static_cast<std::size_t>(end - p) < n)
		return false;
	value = wxString::FromUTF8(reinterpret_cast<const char*>(p), n);
	p += n;
	return true;
}

bool ModeIsOwnerOnly(const wxString& path)
{
#if defined(__WXMSW__)
	(void)path;
	return true;
#else
	wxStructStat st;
	if (wxStat(path, &st) != 0)
		return false;
	return (st.st_mode & 0777) == 0600;
#endif
}

} // namespace

wxString ibWriteLaunchTicket(const ibLaunchSecrets& secrets)
{
	if (!AnyPassword(secrets))
		return wxEmptyString;
	if (!FieldFits(secrets.m_dbUser) || !FieldFits(secrets.m_dbPassword)
		|| !FieldFits(secrets.m_ibUser) || !FieldFits(secrets.m_ibPassword)) {
		ibJournalError(wxT("launch"), wxT("A launch credential is too long to store."));
		return wxEmptyString;
	}

	const wxString path = wxFileName::CreateTempFileName(wxT("oes-cred-"));
	if (path.IsEmpty())
		return wxEmptyString;

	// The temp file exists before the secret is written. Narrow it first,
	// and refuse the file if that did not stick: a group-readable ticket
	// is the leak this exists to close.
	wxChmod(path, 0600);
	if (!ModeIsOwnerOnly(path)) {
		wxRemoveFile(path);
		ibJournalError(wxT("launch"), wxT("The credential file could not be kept private."));
		return wxEmptyString;
	}

	std::vector<unsigned char> bytes;
	bytes.insert(bytes.end(), kMagic, kMagic + 4);
	PutField(bytes, secrets.m_dbUser);
	PutField(bytes, secrets.m_dbPassword);
	PutField(bytes, secrets.m_ibUser);
	PutField(bytes, secrets.m_ibPassword);

	wxFile file;
	if (!file.Open(path, wxFile::write) || file.Write(bytes.data(), bytes.size()) != bytes.size()) {
		file.Close();
		wxRemoveFile(path);
		ibJournalError(wxT("launch"), wxT("The credential file could not be written."));
		return wxEmptyString;
	}
	file.Close();
	return path;
}

bool ibReadLaunchTicket(const wxString& path, ibLaunchSecrets& out)
{
	out = ibLaunchSecrets();
	if (path.IsEmpty() || !wxFileExists(path))
		return false;

	wxFile file;
	if (!file.Open(path, wxFile::read)) {
		wxRemoveFile(path);
		return false;
	}
	const wxFileOffset length = file.Length();
	if (length < 4 || length > 4 + 4 * (4 + static_cast<wxFileOffset>(ibLaunchSecrets::kMaxFieldBytes))) {
		file.Close();
		wxRemoveFile(path);
		return false;
	}
	std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
	const ssize_t got = file.Read(bytes.data(), bytes.size());
	file.Close();
	wxRemoveFile(path);
	if (got < 0 || static_cast<wxFileOffset>(got) != length)
		return false;

	if (bytes[0] != 'O' || bytes[1] != 'E' || bytes[2] != 'S' || bytes[3] != '1')
		return false;
	const unsigned char* p = bytes.data() + 4;
	const unsigned char* end = bytes.data() + bytes.size();
	ibLaunchSecrets loaded;
	if (!TakeField(p, end, loaded.m_dbUser) || !TakeField(p, end, loaded.m_dbPassword)
		|| !TakeField(p, end, loaded.m_ibUser) || !TakeField(p, end, loaded.m_ibPassword)
		|| p != end)
		return false;
	out = loaded;
	return true;
}

wxString ibAppendLaunchTicket(wxString& command, const ibLaunchSecrets& secrets)
{
	const wxString path = ibWriteLaunchTicket(secrets);
	if (path.IsEmpty())
		return wxEmptyString;
	command += wxT(" --cred-file=\"") + path + wxT("\"");
	return path;
}

void ibNotePlaintextLaunchPassword(const wxString& flag)
{
	ibJournalWarning(wxT("launch"),
		wxT("--%s puts a password on the command line; pass --cred-file instead"), flag);
	std::fprintf(stderr, "oes: --%s puts a password on the command line; pass --cred-file instead\n",
		static_cast<const char*>(flag.utf8_str()));
}

void ibTakeLaunchCredentials(const wxString& credFile,
                             bool plaintextDb, bool plaintextIb,
                             wxString& dbUser, wxString& dbPassword,
                             wxString& ibUser, wxString& ibPassword)
{
	if (plaintextDb)
		ibNotePlaintextLaunchPassword(wxT("password"));
	if (plaintextIb)
		ibNotePlaintextLaunchPassword(wxT("ibpwd"));
	if (credFile.IsEmpty())
		return;

	ibLaunchSecrets loaded;
	if (!ibReadLaunchTicket(credFile, loaded)) {
		ibJournalError(wxT("launch"), wxT("The credential file could not be read."));
		std::fprintf(stderr, "oes: the credential file could not be read\n");
		return;
	}
	if (!loaded.m_dbUser.IsEmpty() && dbUser.IsEmpty())
		dbUser = loaded.m_dbUser;
	if (!loaded.m_ibUser.IsEmpty() && ibUser.IsEmpty())
		ibUser = loaded.m_ibUser;
	if (!loaded.m_dbPassword.IsEmpty())
		dbPassword = loaded.m_dbPassword;
	if (!loaded.m_ibPassword.IsEmpty())
		ibPassword = loaded.m_ibPassword;
}
