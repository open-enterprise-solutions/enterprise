// appserver — the application server: the process that serves the bases of its server folder without a window
// (backend/server/serverConfig.h — the folder; appServer.h — the serving; docs/private/multi-base-process.md § 5). It was
// called `daemon` until 2026-10-01. Started by hand it narrates in its console — the technological journal
// mirrored onto standard error — and stays until Ctrl+C; later it becomes a service and the journal stays.

#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/filename.h>
#include "core/programFolder.h"

#ifdef __WXMSW__
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <iostream>
#include <string>

#include "appServer.h"
#include "frontend/diagnostics/oesConsole.h"

static const wxCmdLineEntryDesc s_cmdLineDesc[] = {
	{ wxCMD_LINE_OPTION, "d",  "dir",          "Server folder: server.conf, server.key and a folder per base (default: server beside the executable)", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "lc", "locale",       "UI locale",               wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "sp", "set-password", "Seal a password into the config: <base>/Password or <base>/IbPassword, typed at the keyboard", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_SWITCH, "h",  "help",         "Show this help message.", wxCMD_LINE_VAL_NONE,   wxCMD_LINE_OPTION_HELP },
	{ wxCMD_LINE_NONE,   nullptr, nullptr,     nullptr,                   wxCMD_LINE_VAL_NONE,   0 }
};

#include "core/diagnostics/leakTracker.h"

IB_LEAK_TRACKER_ARM();

namespace {

// Where a server started without --dir keeps its bases — `server` in the program folder.
// Inside a bundle that is beside the .app, not in Contents/MacOS/.
wxString DefaultServerDir()
{
	wxFileName fn = wxFileName::DirName(ibProgramFolder());
	fn.AppendDir(wxT("server"));
	return fn.GetPath();
}

// Typed at the keyboard, not echoed. A console reads it in UTF-16, so a password in any alphabet arrives
// whole; anything else (a pipe) is read as a line of UTF-8.
wxString ReadHidden()
{
	wxString line;
#ifdef __WXMSW__
	HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode = 0;
	if (::GetConsoleMode(in, &mode)) {
		::SetConsoleMode(in, mode & ~ENABLE_ECHO_INPUT);
		wchar_t buffer[512];
		DWORD read = 0;
		if (::ReadConsoleW(in, buffer, 511, &read, nullptr))
			line = wxString(buffer, read);
		::SetConsoleMode(in, mode);
	}
	else {
		std::string raw;
		std::getline(std::cin, raw);
		line = wxString::FromUTF8(raw);
	}
#else
	termios old{};
	const bool tty = ::tcgetattr(STDIN_FILENO, &old) == 0;
	if (tty) {
		termios quiet = old;
		quiet.c_lflag &= ~ECHO;
		::tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
	}
	std::string raw;
	std::getline(std::cin, raw);
	if (tty)
		::tcsetattr(STDIN_FILENO, TCSANOW, &old);
	line = wxString::FromUTF8(raw);
#endif
	std::fputs("\n", stderr);
	line.Replace(wxT("\r"), wxEmptyString);
	line.Replace(wxT("\n"), wxEmptyString);
	return line;
}

// `appserver --set-password=<base>/<Password|IbPassword>` — the secret typed at the keyboard, sealed into the
// config: it travels neither on a command line nor through a file.
int SetPassword(ibServerConfig& config, const wxString& target)
{
	const wxString base = target.BeforeLast(wxT('/'));
	const wxString name = target.AfterLast(wxT('/'));
	if (base.IsEmpty() || (name != wxT("Password") && name != wxT("IbPassword"))) {
		ibAppServerSay(ibJournalMark::Error, wxT("--set-password wants <base>/Password or <base>/IbPassword, not '%s'"),
			target);
		return 1;
	}

	wxString error;
	if (!config.LoadKey(error)) {
		ibAppServerSay(ibJournalMark::Error, wxT("%s"), error);
		return 1;
	}

	std::fprintf(stderr, "%s for %s: ", static_cast<const char*>(name.utf8_str()),
		static_cast<const char*>(base.utf8_str()));
	if (!config.SealSecret(base, name, ReadHidden(), error)) {
		ibAppServerSay(ibJournalMark::Error, wxT("%s"), error);
		return 1;
	}
	ibAppServerSay(ibJournalMark::Info, wxT("%s/%s sealed into %s"), base, name, config.GetConfPath());
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	wxApp::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "appserver");

	// wxInitializer + wxSocketBase::Initialize + ibCrashGuard::Install, in one line. Headless: no wxApp, faults
	// from worker / debug-listener threads write minidumps instead of a silent abort.
	ibOesConsoleBoot boot(wxT("appserver"), argc, argv);
	if (!boot.IsOk()) {
		std::fprintf(stderr, "Failed to initialize the wxWidgets library, aborting.");
		return -1;
	}

	// What the server says, and every warning and error, is what its console shows; the running commentary is the
	// journal file's.
	ibTechJournal::EchoToStderr();

	wxCmdLineParser parser(s_cmdLineDesc, argc, argv);
	if (parser.Parse() != 0)
		return 1;

	wxString folder = DefaultServerDir(), locale = wxT("en"), sealTarget;
	parser.Found(wxT("dir"), &folder);
	parser.Found(wxT("locale"), &locale);
	ibServerConfig config(folder);

	if (parser.Found(wxT("set-password"), &sealTarget))
		return SetPassword(config, sealTarget);

#ifdef __WXMSW__
	::DisableProcessWindowsGhosting();
#endif

	return ibAppServer(config, locale).Run();
}
