#include "core/startProcess.h"

#include "core/diagnostics/journal.h"
#include "core/fileSystem.h"   // ibRemoveFile

#include <wx/ffile.h>
#include <wx/filename.h>   // wxFileName::CreateTempFileName
#include <wx/log.h>   // wxSysErrorMsgStr

#include <chrono>
#include <thread>
#include <vector>

#ifdef __WXMSW__
#include <algorithm>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <wx/cmdline.h>

#ifdef __APPLE__
#include <crt_externs.h>   // a shared library on macOS has no `environ` of its own
#define environ (*_NSGetEnviron())
#else
extern char** environ;
#endif
#endif

#ifdef __WXMSW__
long ibStartProcess(const wxString& command, const std::vector<unsigned char>& input)
{
	// CreateProcessW may write into the command line, so it gets a copy of its own.
	const wxWCharBuffer wide = command.wc_str();
	std::vector<wchar_t> line(wide.data(), wide.data() + wide.length() + 1);

	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	PROCESS_INFORMATION started{};

	const auto refuse = [&command](DWORD error) {
		ibJournalWarning(wxT("process"), wxT("%s did not start: %s"), command, wxSysErrorMsgStr(error));
		return 0L;
	};

	// The flags wxExecute gave the same call: no handles inherited, and a console program gets the console it
	// always got - the web server's window stays where a person can close it.
	//
	// …except what the input needs. Its pipe's read end is the child's stdin, and the attribute list names the
	// handles the child takes: that end, and this process's own output where it can be inherited at all. Nothing
	// else of this process - its sockets, its open files - goes along.
	HANDLE readEnd = nullptr;
	HANDLE writeEnd = nullptr;
	std::vector<HANDLE> inherited;
	std::vector<unsigned char> attributes;
	if (!input.empty()) {
		SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		if (!::CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
			return refuse(::GetLastError());
		::SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);
		inherited.push_back(readEnd);

		const auto output = [&inherited](DWORD which) -> HANDLE {
			const HANDLE handle = ::GetStdHandle(which);
			DWORD flags = 0;
			if (handle == nullptr || handle == INVALID_HANDLE_VALUE
				|| !::GetHandleInformation(handle, &flags) || (flags & HANDLE_FLAG_INHERIT) == 0)
				return nullptr;
			if (std::find(inherited.begin(), inherited.end(), handle) == inherited.end())
				inherited.push_back(handle);
			return handle;
		};
		startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdInput  = readEnd;
		startup.StartupInfo.hStdOutput = output(STD_OUTPUT_HANDLE);
		startup.StartupInfo.hStdError  = output(STD_ERROR_HANDLE);

		SIZE_T size = 0;
		::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
		attributes.resize(size);
		startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
		if (!::InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)
			|| !::UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				inherited.data(), inherited.size() * sizeof(HANDLE), nullptr, nullptr)) {
			const DWORD error = ::GetLastError();
			::CloseHandle(readEnd);
			::CloseHandle(writeEnd);
			return refuse(error);
		}
	}

	const BOOL ok = ::CreateProcessW(nullptr, line.data(), nullptr, nullptr, input.empty() ? FALSE : TRUE,
		CREATE_DEFAULT_ERROR_MODE | (input.empty() ? 0 : EXTENDED_STARTUPINFO_PRESENT),
		nullptr, nullptr, &startup.StartupInfo, &started);
	const DWORD error = ok ? ERROR_SUCCESS : ::GetLastError();
	if (startup.lpAttributeList != nullptr)
		::DeleteProcThreadAttributeList(startup.lpAttributeList);
	if (readEnd != nullptr)
		::CloseHandle(readEnd);   // the child has its own now
	if (!ok) {
		if (writeEnd != nullptr)
			::CloseHandle(writeEnd);
		return refuse(error);
	}

	// The child reads its stdin to the end before anything else, and the input is small - it fits the pipe.
	if (writeEnd != nullptr) {
		DWORD wrote = 0;
		::WriteFile(writeEnd, input.data(), static_cast<DWORD>(input.size()), &wrote, nullptr);
		::CloseHandle(writeEnd);
	}

	::CloseHandle(started.hThread);
	::CloseHandle(started.hProcess);
	return (long)started.dwProcessId;
}
#else
long ibStartProcess(const wxString& command, const std::vector<unsigned char>& input)
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

	// The input goes in through a pipe whose read end becomes the child's stdin. Both ends close on exec, so a
	// child another thread starts meanwhile takes neither; the dup2 below is what the child keeps.
	int pipeEnds[2] = { -1, -1 };
	posix_spawn_file_actions_t actions;
	if (!input.empty()) {
		if (pipe(pipeEnds) != 0) {
			const int error = errno;
			posix_spawnattr_destroy(&attributes);
			ibJournalWarning(wxT("process"), wxT("%s did not start: %s"), command, wxSysErrorMsgStr(error));
			return 0;
		}
		fcntl(pipeEnds[0], F_SETFD, FD_CLOEXEC);
		fcntl(pipeEnds[1], F_SETFD, FD_CLOEXEC);
		posix_spawn_file_actions_init(&actions);
		posix_spawn_file_actions_adddup2(&actions, pipeEnds[0], STDIN_FILENO);
	}

	pid_t pid = 0;
	const int error = posix_spawnp(&pid, argv[0], input.empty() ? nullptr : &actions, &attributes, argv.data(), environ);
	posix_spawnattr_destroy(&attributes);
	if (!input.empty()) {
		posix_spawn_file_actions_destroy(&actions);
		close(pipeEnds[0]);   // the child has its own now
	}

	if (error != 0) {
		if (!input.empty())
			close(pipeEnds[1]);
		ibJournalWarning(wxT("process"), wxT("%s did not start: %s"), command, wxSysErrorMsgStr(error));
		return 0;
	}

	// ⚠ A CHILD THAT ENDS BEFORE READING TURNS THE WRITE INTO SIGPIPE, which ends THIS process. The signal is held
	// for the write, and one it raised is taken here rather than delivered.
	if (!input.empty()) {
		sigset_t pipeSignal;
		sigemptyset(&pipeSignal);
		sigaddset(&pipeSignal, SIGPIPE);
		sigset_t before;
		pthread_sigmask(SIG_BLOCK, &pipeSignal, &before);

		const unsigned char* data = input.data();
		std::size_t left = input.size();
		while (left > 0) {
			const ssize_t wrote = write(pipeEnds[1], data, left);
			if (wrote < 0) {
				if (errno == EINTR)
					continue;
				break;   // the child is gone; it reads nothing either way
			}
			data += wrote;
			left -= static_cast<std::size_t>(wrote);
		}
		close(pipeEnds[1]);

		sigset_t pending;
		sigpending(&pending);
		if (sigismember(&pending, SIGPIPE)) {
			int taken = 0;
			sigwait(&pipeSignal, &taken);
		}
		pthread_sigmask(SIG_SETMASK, &before, nullptr);
	}

	// Nobody else waits for this child, and one nobody waits for stays a zombie after it ends.
	std::thread([pid]() {
		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	}).detach();

	return (long)pid;
}
#endif

long ibStartWebServer(const wxString& command, wxString& url, const std::function<void()>& waiting)
{
	url.clear();

	// The system picks the port; the server says it, with the page's address, in this file.
	const wxString manifest = wxFileName::CreateTempFileName(wxT("oes-web-"));
	const long pid = ibStartProcess(command + wxString::Format(wxT(" --port=0 --manifest=\"%s\""), manifest));
	if (pid == 0) {
		ibRemoveFile(manifest);
		return 0;
	}

	// A file base is opened before the port is, so the first looks find the file empty. A line counts once its end
	// is there - the server may be half way through writing it.
	for (int waited = 0; waited < 5000 && url.IsEmpty(); waited += 100) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (waiting)
			waiting();
		wxFFile file(manifest, wxT("rb"));
		wxString content;
		if (!file.IsOpened() || !file.ReadAll(&content, wxConvUTF8))
			continue;
		const int at = content.Find(wxT("url="));
		const wxString rest = at != wxNOT_FOUND ? content.Mid(at + 4) : wxString();
		if (rest.Find(wxT('\n')) != wxNOT_FOUND)
			url = rest.BeforeFirst(wxT('\n')).Trim().Trim(false);
	}
	ibRemoveFile(manifest);
	return pid;
}
