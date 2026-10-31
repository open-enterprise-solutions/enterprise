#include "core/launchTicket.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/diagnostics/journal.h"
#include "core/startProcess.h"

#if defined(__IBMSW__)
#  include <windows.h>
#else
#  include <cerrno>
#  include <unistd.h>
#  include <sys/types.h>
#endif

namespace {

constexpr char kMagic[4] = { 'O', 'E', 'S', '1' };

bool AnyPassword(const ibLaunchSecrets& secrets)
{
	return !secrets.m_dbPassword.IsEmpty() || !secrets.m_ibPassword.IsEmpty();
}

bool FieldFits(const ibString& value)
{
	const std::string utf8 = value.ToUtf8();
	return utf8.length() <= ibLaunchSecrets::kMaxFieldBytes;
}

void PutField(std::vector<unsigned char>& out, const ibString& value)
{
	const std::string utf8 = value.ToUtf8();
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

bool TakeField(const unsigned char*& p, const unsigned char* end, ibString& value)
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
	value = ibString::FromUTF8(reinterpret_cast<const char*>(p), n);
	p += n;
	return true;
}

std::vector<unsigned char> Encode(const ibLaunchSecrets& secrets)
{
	std::vector<unsigned char> bytes;
	if (!AnyPassword(secrets))
		return bytes;
	if (!FieldFits(secrets.m_dbUser) || !FieldFits(secrets.m_dbPassword)
		|| !FieldFits(secrets.m_ibUser) || !FieldFits(secrets.m_ibPassword)) {
		ibJournalError(ibT("launch"), ibT("A launch password is too long to pass."));
		return bytes;
	}
	bytes.insert(bytes.end(), kMagic, kMagic + 4);
	PutField(bytes, secrets.m_dbUser);
	PutField(bytes, secrets.m_dbPassword);
	PutField(bytes, secrets.m_ibUser);
	PutField(bytes, secrets.m_ibPassword);
	return bytes;
}

bool Decode(const unsigned char* data, std::size_t n, ibLaunchSecrets& out)
{
	out = ibLaunchSecrets();
	if (data == nullptr || n < 4)
		return false;
	if (data[0] != 'O' || data[1] != 'E' || data[2] != 'S' || data[3] != '1')
		return false;
	const unsigned char* p = data + 4;
	const unsigned char* end = data + n;
	ibLaunchSecrets loaded;
	if (!TakeField(p, end, loaded.m_dbUser) || !TakeField(p, end, loaded.m_dbPassword)
		|| !TakeField(p, end, loaded.m_ibUser) || !TakeField(p, end, loaded.m_ibPassword)
		|| p != end)
		return false;
	out = loaded;
	return true;
}

constexpr std::size_t kMaxBlock =
	4u + 4u * (4u + static_cast<std::size_t>(ibLaunchSecrets::kMaxFieldBytes));

} // namespace

long ibStartProcess(const ibString& command, const ibLaunchSecrets& secrets)
{
	return ibStartProcess(command, Encode(secrets));
}

void ibNotePlaintextLaunchPassword(const ibString& flag)
{
	ibJournalWarning(ibT("launch"),
		ibT("--%s puts a password on the command line"), flag);
	std::fprintf(stderr, "oes: --%s puts a password on the command line\n",
		flag.ToUtf8().c_str());
}

void ibApplyLaunchSecrets(ibLaunchSecrets& line, bool plaintextDb, bool plaintextIb)
{
	if (plaintextDb)
		ibNotePlaintextLaunchPassword(ibT("password"));
	if (plaintextIb)
		ibNotePlaintextLaunchPassword(ibT("ibpwd"));

#if defined(__IBMSW__)
	const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode = 0;
	if (in == nullptr || in == INVALID_HANDLE_VALUE || GetConsoleMode(in, &mode))
		return;
	std::vector<unsigned char> bytes;
	bytes.reserve(256);
	unsigned char buf[4096];
	while (bytes.size() < kMaxBlock) {
		DWORD got = 0;
		const DWORD want = static_cast<DWORD>((std::min)(sizeof(buf), kMaxBlock - bytes.size()));
		if (!ReadFile(in, buf, want, &got, nullptr) || got == 0)
			break;
		bytes.insert(bytes.end(), buf, buf + got);
	}
#else
	if (isatty(STDIN_FILENO))
		return;
	std::vector<unsigned char> bytes;
	bytes.reserve(256);
	unsigned char buf[4096];
	while (bytes.size() < kMaxBlock) {
		const ssize_t got = ::read(STDIN_FILENO, buf, std::min(sizeof(buf), kMaxBlock - bytes.size()));
		if (got < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (got == 0)
			break;
		bytes.insert(bytes.end(), buf, buf + got);
	}
#endif

	ibLaunchSecrets loaded;
	if (!Decode(bytes.data(), bytes.size(), loaded))
		return;
	if (!loaded.m_dbUser.IsEmpty() && line.m_dbUser.IsEmpty())
		line.m_dbUser = loaded.m_dbUser;
	if (!loaded.m_ibUser.IsEmpty() && line.m_ibUser.IsEmpty())
		line.m_ibUser = loaded.m_ibUser;
	if (!loaded.m_dbPassword.IsEmpty())
		line.m_dbPassword = loaded.m_dbPassword;
	if (!loaded.m_ibPassword.IsEmpty())
		line.m_ibPassword = loaded.m_ibPassword;
}
