#include "core/startProcess.h"

#include "core/diagnostics/crashGuard.h"   // CurrentPidPortable — the manifest's name
#include "core/diagnostics/journal.h"
#include "core/exception.h"    // ibErrorText
#include "core/fileSystem.h"   // ibRemoveFile

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace {

// The system's words for the code a start failed with.
ibString ErrorText(long code)
{
	return ibString::FromUTF8(ibErrorText(std::error_code(static_cast<int>(code), std::system_category())).c_str());
}

} // namespace

#ifdef __IBMSW__
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

#ifdef __IBOSX__
#include <crt_externs.h>   // a shared library on macOS has no `environ` of its own
#define environ (*_NSGetEnviron())
#else
extern char** environ;
#endif
#endif

#ifdef __IBMSW__
long ibStartProcess(const ibString& command, const std::vector<unsigned char>& input)
{
	// CreateProcessW may write into the command line, so it gets a copy of its own.
	std::vector<wchar_t> line(command.wc_str(), command.wc_str() + command.Len() + 1);

	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	PROCESS_INFORMATION started{};

	const auto refuse = [&command](DWORD error) {
		ibJournalWarning(ibT("process"), ibT("%s did not start: %s"), command, ErrorText(error));
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
namespace {

// A command line in words, as a Unix shell splits it (wxCmdLineParser's SPLIT_UNIX): space between words, quotes
// around a word that has spaces — single ones take every character as it is, double ones and a bare word take a
// backslash's next character as itself.
std::vector<ibString> SplitArguments(const ibString& command)
{
	std::vector<ibString> words;
	ibString word;
	bool inWord = false;
	ibChar quote = 0;
	for (size_t i = 0; i < command.Len(); ++i) {
		const ibChar c = command[i];
		if (quote == 0 && (c == ibT(' ') || c == ibT('\t'))) {
			if (inWord)
				words.push_back(word);
			word.clear();
			inWord = false;
			continue;
		}
		inWord = true;
		if (c == quote)
			quote = 0;
		else if (quote == 0 && (c == ibT('"') || c == ibT('\'')))
			quote = c;
		else if (c == ibT('\\') && quote != ibT('\'') && i + 1 < command.Len())
			word += command[++i];
		else
			word += c;
	}
	if (inWord)
		words.push_back(word);
	return words;
}

} // namespace

long ibStartProcess(const ibString& command, const std::vector<unsigned char>& input)
{
	const std::vector<ibString> args = SplitArguments(command);
	if (args.empty())
		return 0;

	std::vector<std::string> text;
	text.reserve(args.size());
	for (const ibString& arg : args)
		text.emplace_back(arg.ToUtf8());

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
			ibJournalWarning(ibT("process"), ibT("%s did not start: %s"), command, ErrorText(error));
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
		ibJournalWarning(ibT("process"), ibT("%s did not start: %s"), command, ErrorText(error));
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

long ibStartWebServer(const ibString& command, ibString& url, const std::function<void()>& waiting)
{
	url.clear();

	// The system picks the port; the server says it, with the page's address, in this file — a name of this process's
	// and this call's in the temporary folder; the server makes the file.
	static std::atomic<unsigned> s_started{ 0 };
	std::error_code error;
	const ibString manifest = ibString((std::filesystem::temp_directory_path(error) / ibPathOf(ibString::Format(
		ibT("oes-web-%u-%u.txt"), ibCrashGuard::CurrentPidPortable(), s_started.fetch_add(1)))).wstring());
	const long pid = ibStartProcess(command + ibString::Format(ibT(" --port=0 --manifest=\"%s\""), manifest));
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
		std::ifstream file(ibPathOf(manifest), std::ios::binary);
		if (!file)
			continue;
		const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const ibString content = ibString::FromUTF8(bytes.data(), bytes.size());
		const int at = content.Find(ibT("url="));
		const ibString rest = at != wxNOT_FOUND ? content.Mid(at + 4) : ibString();
		if (rest.Find(ibT('\n')) != wxNOT_FOUND)
			url = rest.BeforeFirst(ibT('\n')).Trim().Trim(false);
	}
	ibRemoveFile(manifest);
	return pid;
}

#ifdef __IBMSW__
namespace {

// The windows of one process, told to close — what a console program without a window has none of.
BOOL CALLBACK CloseWindowOf(HWND window, LPARAM pid)
{
	DWORD owner = 0;
	::GetWindowThreadProcessId(window, &owner);
	if (owner == static_cast<DWORD>(pid))
		::PostMessageW(window, WM_CLOSE, 0, 0);
	return TRUE;
}

} // namespace

bool ibStopProcess(long pid)
{
	return ::EnumWindows(CloseWindowOf, static_cast<LPARAM>(pid)) != FALSE;
}

bool ibKillProcess(long pid)
{
	const HANDLE process = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
	if (process == nullptr)
		return false;
	const BOOL ended = ::TerminateProcess(process, static_cast<UINT>(-1));
	::CloseHandle(process);
	return ended != FALSE;
}

bool ibProcessExists(long pid)
{
	const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
	if (process == nullptr)
		return false;
	DWORD code = 0;
	const bool running = ::GetExitCodeProcess(process, &code) != FALSE && code == STILL_ACTIVE;
	::CloseHandle(process);
	return running;
}

bool ibSetEnv(const ibString& name, const ibString& value)
{
	// Both the system's block and the C runtime's copy: a library may read either (GetEnvironmentVariable, getenv).
	return ::SetEnvironmentVariableW(name.wc_str(), value.wc_str()) != FALSE
		&& ::_wputenv_s(name.wc_str(), value.wc_str()) == 0;
}

ibString ibHostName()
{
	wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
	DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
	return ::GetComputerNameW(name, &size) ? ibString(name) : ibString();
}
#else
bool ibStopProcess(long pid)
{
	return ::kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

bool ibKillProcess(long pid)
{
	return ::kill(static_cast<pid_t>(pid), SIGKILL) == 0;
}

bool ibProcessExists(long pid)
{
	return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

bool ibSetEnv(const ibString& name, const ibString& value)
{
	return ::setenv(name.ToUtf8().c_str(), value.ToUtf8().c_str(), 1) == 0;
}

ibString ibHostName()
{
	char name[256] = {};
	return ::gethostname(name, sizeof(name) - 1) == 0 ? ibString::FromUTF8(name) : ibString();
}
#endif
