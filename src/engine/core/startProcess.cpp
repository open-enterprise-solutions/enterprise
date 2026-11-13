#include "core/startProcess.h"

#include "core/diagnostics/crashGuard.h"   // CurrentPidPortable — the manifest's name
#include "core/diagnostics/journal.h"
#include "core/exception.h"    // ibErrorText
#include "core/fileSystem.h"   // ibRemoveFile

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

// The system's words for the code a start failed with.
ibString ErrorText(long code)
{
	return ibString::FromUTF8(ibErrorText(std::error_code(static_cast<int>(code), std::system_category())).c_str());
}

// What a program wrote, as text: UTF-8, "\r\n" made "\n", the last line's end dropped - wxExecute's lines, joined.
ibString TextOf(const std::string& bytes)
{
	ibString text = ibString::FromUTF8(bytes);
	text.Replace(ibT("\r\n"), ibT("\n"));
	if (text.EndsWith(ibT("\n")))
		text.RemoveLast();
	return text;
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

namespace {

// What a pipe gives until its writer closes it.
std::string ReadToEnd(HANDLE readEnd)
{
	std::string bytes;
	char buffer[4096];
	DWORD read = 0;
	while (::ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0)
		bytes.append(buffer, read);
	return bytes;
}

} // namespace

long ibRunProcess(const ibString& command, ibString& output, ibString& error)
{
	std::vector<wchar_t> line(command.wc_str(), command.wc_str() + command.Len() + 1);

	const auto refuse = [&command](DWORD code) {
		ibJournalWarning(ibT("process"), ibT("%s did not start: %s"), command, ErrorText(code));
		return -1L;
	};

	// stdout and stderr are pipes, and their write ends are the only handles the child takes - as ibStartProcess's
	// stdin: nothing else of this process goes along.
	SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
	HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
	if (!::CreatePipe(&outRead, &outWrite, &inheritable, 0))
		return refuse(::GetLastError());
	if (!::CreatePipe(&errRead, &errWrite, &inheritable, 0)) {
		const DWORD code = ::GetLastError();
		::CloseHandle(outRead);
		::CloseHandle(outWrite);
		return refuse(code);
	}
	::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
	::SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdOutput = outWrite;
	startup.StartupInfo.hStdError = errWrite;

	HANDLE inherited[] = { outWrite, errWrite };
	SIZE_T size = 0;
	::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
	std::vector<unsigned char> attributes(size);
	startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
	const bool listed = ::InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size) != FALSE;
	PROCESS_INFORMATION started{};
	const BOOL ok = listed
		&& ::UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			inherited, sizeof(inherited), nullptr, nullptr)
		&& ::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
			CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
			nullptr, nullptr, &startup.StartupInfo, &started);
	const DWORD code = ok ? ERROR_SUCCESS : ::GetLastError();
	if (listed)
		::DeleteProcThreadAttributeList(startup.lpAttributeList);
	::CloseHandle(outWrite);   // the child has its own now: the reads end when it does
	::CloseHandle(errWrite);
	if (!ok) {
		::CloseHandle(outRead);
		::CloseHandle(errRead);
		return refuse(code);
	}
	::CloseHandle(started.hThread);

	// Both pipes at once: a child that fills the one nobody reads waits forever.
	std::string errorBytes;
	std::thread errorReader([&errorBytes, errRead]() { errorBytes = ReadToEnd(errRead); });
	const std::string outputBytes = ReadToEnd(outRead);
	errorReader.join();
	::CloseHandle(outRead);
	::CloseHandle(errRead);

	::WaitForSingleObject(started.hProcess, INFINITE);
	DWORD exitCode = 0;
	::GetExitCodeProcess(started.hProcess, &exitCode);
	::CloseHandle(started.hProcess);

	output = TextOf(outputBytes);
	error = TextOf(errorBytes);
	return static_cast<long>(exitCode);
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

// Starts the program `args` names, with `actions` done on its descriptors (nullptr - none): 0 and the child's id in
// `pid`, or the error it did not start with.
int Spawn(const std::vector<ibString>& args, const posix_spawn_file_actions_t* actions, pid_t& pid)
{
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

	const int error = posix_spawnp(&pid, argv[0], actions, &attributes, argv.data(), environ);
	posix_spawnattr_destroy(&attributes);
	return error;
}

// What a pipe gives until its writer closes it.
std::string ReadToEnd(int readEnd)
{
	std::string bytes;
	char buffer[4096];
	for (;;) {
		const ssize_t got = read(readEnd, buffer, sizeof(buffer));
		if (got > 0)
			bytes.append(buffer, static_cast<std::size_t>(got));
		else if (got < 0 && errno == EINTR)
			continue;
		else
			break;
	}
	return bytes;
}

} // namespace

long ibStartProcess(const ibString& command, const std::vector<unsigned char>& input)
{
	const std::vector<ibString> args = SplitArguments(command);
	if (args.empty())
		return 0;

	// The input goes in through a pipe whose read end becomes the child's stdin. Both ends close on exec, so a
	// child another thread starts meanwhile takes neither; the dup2 below is what the child keeps.
	int pipeEnds[2] = { -1, -1 };
	posix_spawn_file_actions_t actions;
	if (!input.empty()) {
		if (pipe(pipeEnds) != 0) {
			const int error = errno;
			ibJournalWarning(ibT("process"), ibT("%s did not start: %s"), command, ErrorText(error));
			return 0;
		}
		fcntl(pipeEnds[0], F_SETFD, FD_CLOEXEC);
		fcntl(pipeEnds[1], F_SETFD, FD_CLOEXEC);
		posix_spawn_file_actions_init(&actions);
		posix_spawn_file_actions_adddup2(&actions, pipeEnds[0], STDIN_FILENO);
	}

	pid_t pid = 0;
	const int error = Spawn(args, input.empty() ? nullptr : &actions, pid);
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

long ibRunProcess(const ibString& command, ibString& output, ibString& error)
{
	const std::vector<ibString> args = SplitArguments(command);
	if (args.empty())
		return -1;

	const auto refuse = [&command](int code) {
		ibJournalWarning(ibT("process"), ibT("%s did not start: %s"), command, ErrorText(code));
		return -1L;
	};

	// stdout and stderr are pipes, closed on exec as ibStartProcess's stdin is; the dup2s below are what the child
	// keeps. Its stdin is /dev/null.
	int outPipe[2] = { -1, -1 };
	int errPipe[2] = { -1, -1 };
	if (pipe(outPipe) != 0)
		return refuse(errno);
	if (pipe(errPipe) != 0) {
		const int code = errno;
		close(outPipe[0]);
		close(outPipe[1]);
		return refuse(code);
	}
	for (const int end : { outPipe[0], outPipe[1], errPipe[0], errPipe[1] })
		fcntl(end, F_SETFD, FD_CLOEXEC);

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, errPipe[1], STDERR_FILENO);

	pid_t pid = 0;
	const int code = Spawn(args, &actions, pid);
	posix_spawn_file_actions_destroy(&actions);
	close(outPipe[1]);   // the child has its own now: the reads end when it does
	close(errPipe[1]);
	if (code != 0) {
		close(outPipe[0]);
		close(errPipe[0]);
		return refuse(code);
	}

	// Both pipes at once: a child that fills the one nobody reads waits forever.
	std::string errorBytes;
	std::thread errorReader([&errorBytes, readEnd = errPipe[0]]() { errorBytes = ReadToEnd(readEnd); });
	const std::string outputBytes = ReadToEnd(outPipe[0]);
	errorReader.join();
	close(outPipe[0]);
	close(errPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

	output = TextOf(outputBytes);
	error = TextOf(errorBytes);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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
		const ibString rest = at != ibNOT_FOUND ? content.Mid(at + 4) : ibString();
		if (rest.Find(ibT('\n')) != ibNOT_FOUND)
			url = rest.BeforeFirst(ibT('\n')).Trim().Trim(false);
	}
	ibRemoveFile(manifest);
	return pid;
}

namespace {

std::atomic<void (*)()> s_stopProcess{ nullptr };

} // namespace

void ibOnStopProcess(void (*stop)())
{
	s_stopProcess.store(stop);
}

bool ibStopProcess()
{
	void (*const stop)() = s_stopProcess.load();
	if (stop == nullptr)
		return false;
	stop();
	return true;
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
#endif
