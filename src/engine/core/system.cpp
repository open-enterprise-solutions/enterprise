#include "core/system.h"

#include <string>

#ifdef __IBMSW__
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW
#else
#include <cstring>         // strchr - the host name's domain cut off
#include <fstream>         // /proc/self/cmdline
#include <iterator>
#include <pwd.h>           // getpwuid - the user's name
#include <stdlib.h>        // setenv
#include <sys/utsname.h>   // uname - the system in words
#include <unistd.h>
#ifdef __IBOSX__
#include <crt_externs.h>   // _NSGetArgc / _NSGetArgv - a shared library on macOS has no argv of its own
#endif
#endif

#ifdef __IBMSW__
ibString ibHostName()
{
	wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
	DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
	return ::GetComputerNameW(name, &size) ? ibString(name) : ibString();
}

ibString ibUserName()
{
	wchar_t name[257] = {};   // UNLEN + 1
	DWORD size = 257;
	return ::GetUserNameW(name, &size) ? ibString(name) : ibString();
}

ibString ibOsDescription()
{
	// RtlGetVersion, as wxMSW asks it: GetVersionEx answers the version the program's manifest says it was tested on.
	// Windows 11 still calls itself 10.0 - its builds begin at 22000, which is how wx tells the two apart.
	using GetVersionFn = LONG (WINAPI*)(OSVERSIONINFOEXW*);
	OSVERSIONINFOEXW info = {};
	info.dwOSVersionInfoSize = sizeof(info);
	const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
	const GetVersionFn getVersion = ntdll != nullptr
		? reinterpret_cast<GetVersionFn>(reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")))
		: nullptr;
	if (getVersion == nullptr || getVersion(&info) != 0)
		return ibT("Windows");

	const bool server = info.wProductType != VER_NT_WORKSTATION;
	const ibString name = !server && info.dwMajorVersion == 10
		? ibString(info.dwBuildNumber >= 22000 ? ibT("Windows 11") : ibT("Windows 10"))
		: ibString::Format(server ? ibT("Windows Server %lu.%lu") : ibT("Windows %lu.%lu"), info.dwMajorVersion, info.dwMinorVersion);
#ifdef _WIN64
	const bool wide = true;
#else
	BOOL wow = FALSE;
	const bool wide = ::IsWow64Process(::GetCurrentProcess(), &wow) && wow;
#endif
	return ibString::Format(ibT("%s (build %lu)%s"), name, info.dwBuildNumber, wide ? ibT(", 64-bit edition") : ibT(""));
}

bool ibSetEnv(const ibString& name, const ibString& value)
{
	// Both the system's block and the C runtime's copy: a library may read either (GetEnvironmentVariable, getenv).
	return ::SetEnvironmentVariableW(name.wc_str(), value.wc_str()) != FALSE
		&& ::_wputenv_s(name.wc_str(), value.wc_str()) == 0;
}

std::vector<ibString> ibCommandLineArguments()
{
	// The line split as wxMSW split it for wxApp's argv, whatever entry point the program has.
	std::vector<ibString> args;
	int count = 0;
	if (LPWSTR* const words = ::CommandLineToArgvW(::GetCommandLineW(), &count)) {
		for (int i = 0; i < count; ++i)
			args.push_back(ibString(words[i]));
		::LocalFree(words);
	}
	return args;
}
#else
ibString ibHostName()
{
	char name[256] = {};
	if (::gethostname(name, sizeof(name) - 1) != 0)
		return ibString();
	// the name without its domain, as wxGetHostName gave it - macOS and the BSDs answer the full one ("mac.local"),
	// and the sessions, the locks and the journal have always said the short one
	if (char* const dot = std::strchr(name, '.'))
		*dot = '\0';
	return ibString::FromUTF8(name);
}

ibString ibUserName()
{
	if (const struct passwd* const user = ::getpwuid(::getuid()))
		return ibString::FromUTF8(user->pw_name);
	return ibString();
}

ibString ibOsDescription()
{
	// What wxGetOsDescription says on Unix: the kernel's name, its release and the machine.
	struct utsname name;
	if (::uname(&name) != 0)
		return ibString();
	return ibString::FromUTF8(std::string(name.sysname) + " " + name.release + " " + name.machine);
}

bool ibSetEnv(const ibString& name, const ibString& value)
{
	return ::setenv(name.ToUtf8().c_str(), value.ToUtf8().c_str(), 1) == 0;
}

std::vector<ibString> ibCommandLineArguments()
{
	std::vector<ibString> args;
#ifdef __IBOSX__
	const int count = *_NSGetArgc();
	char** const words = *_NSGetArgv();
	for (int i = 0; i < count; ++i)
		args.push_back(ibString::FromUTF8(words[i]));
#else
	// Linux keeps it in /proc: the words one after another, each ended by a zero.
	std::ifstream file("/proc/self/cmdline", std::ios::binary);
	const std::string line((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	for (std::size_t from = 0; from < line.size();) {
		std::size_t end = line.find('\0', from);
		if (end == std::string::npos)
			end = line.size();
		args.push_back(ibString::FromUTF8(line.data() + from, end - from));
		from = end + 1;
	}
#endif
	return args;
}
#endif
