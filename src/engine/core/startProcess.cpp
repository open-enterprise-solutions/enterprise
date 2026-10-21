#include "core/startProcess.h"

#include "core/diagnostics/journal.h"

#include <wx/log.h>   // wxSysErrorMsgStr

#include <vector>

#ifdef __WXMSW__
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>

#include <wx/cmdline.h>

#ifdef __APPLE__
#include <crt_externs.h>   // a shared library on macOS has no `environ` of its own
#define environ (*_NSGetEnviron())
#else
extern char** environ;
#endif
#endif

#ifdef __WXMSW__
long ibStartProcess(const wxString& command)
{
	// CreateProcessW may write into the command line, so it gets a copy of its own.
	const wxWCharBuffer wide = command.wc_str();
	std::vector<wchar_t> line(wide.data(), wide.data() + wide.length() + 1);

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION started{};

	// The flags wxExecute gave the same call: no handles inherited, and a console program gets the console it
	// always got - the web server's window stays where a person can close it.
	if (!::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
		CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr, &startup, &started)) {
		const DWORD error = ::GetLastError();
		ibJournalWarning(wxT("process"), wxT("%s did not start: %s"), command, wxSysErrorMsgStr(error));
		return 0;
	}

	::CloseHandle(started.hThread);
	::CloseHandle(started.hProcess);
	return (long)started.dwProcessId;
}
#else
long ibStartProcess(const wxString& command)
{
	const wxArrayString args = wxCmdLineParser::ConvertStringToArgs(command, wxCMD_LINE_SPLIT_UNIX);
	if (args.empty())
		return 0;

	std::vector<std::string> text;
	text.reserve(args.size());
	for (const wxString& arg : args)
		text.emplace_back(arg.mb_str(wxConvWhateverWorks));

	std::vector<char*> argv;
	argv.reserve(text.size() + 1);
	for (std::string& arg : text)
		argv.push_back(&arg[0]);
	argv.push_back(nullptr);

	// ⚠ THE CALLER'S SIGNAL MASK IS INHERITED, and a worker thread may block SIGTERM - the very signal app_run's
	// restart closes the child with. The child starts with nothing blocked.
	posix_spawnattr_t attributes;
	posix_spawnattr_init(&attributes);
	sigset_t none;
	sigemptyset(&none);
	posix_spawnattr_setsigmask(&attributes, &none);
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);

	pid_t pid = 0;
	const int error = posix_spawnp(&pid, argv[0], nullptr, &attributes, argv.data(), environ);
	posix_spawnattr_destroy(&attributes);

	if (error != 0) {
		ibJournalWarning(wxT("process"), wxT("%s did not start: %s"), command, wxSysErrorMsgStr(error));
		return 0;
	}

	// Nobody else waits for this child, and one nobody waits for stays a zombie after it ends.
	std::thread([pid]() {
		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	}).detach();

	return (long)pid;
}
#endif
