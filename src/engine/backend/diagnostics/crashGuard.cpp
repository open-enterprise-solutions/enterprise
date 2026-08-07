#include "crashGuard.h"

#include "backend/backend_exception.h"

#include <wx/datetime.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/utils.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>

#ifdef __WXMSW__
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
// POSIX backtrace — glibc + macOS libSystem. FreeBSD same header.
// Wrap behind a feature check if a future port lacks it.
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ibCrashGuard {

namespace {

// Where dumps land. Resolved once on Install so the SEH/signal
// handlers agree on a single path even after cwd changes.
wxString s_crashDir;
wxString s_exeName;

unsigned int CurrentPidPortable()
{
	return static_cast<unsigned int>(wxGetProcessId());
}

unsigned long CurrentTidPortable()
{
	return static_cast<unsigned long>(wxThread::GetCurrentId());
}

#ifdef __WXMSW__
LPTOP_LEVEL_EXCEPTION_FILTER s_prevSehFilter = nullptr;
#endif
std::terminate_handler       s_prevTerminate = nullptr;
std::atomic<bool>            s_installed{ false };

void EnsureCrashDir()
{
	if (s_crashDir.IsEmpty()) {
		const wxString exePath = wxStandardPaths::Get().GetExecutablePath();
		s_crashDir = wxFileName(exePath).GetPath()
			+ wxFILE_SEP_PATH + wxT("crashdumps");
	}
	wxFileName::Mkdir(s_crashDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

wxString MakeDumpPath(const wxString& kindSuffix, const wxString& extension)
{
	EnsureCrashDir();
	// Stamp + pid + tid + counter — two threads crashing in the same
	// second used to land on the same filename and the second overwrote
	// the first. tid disambiguates the common case; the atomic counter
	// handles two faults on the same thread within one second.
	static std::atomic<unsigned> s_seq{ 0 };
	const wxString stamp = wxDateTime::Now().Format(wxT("%Y%m%dT%H%M%S"));
	const unsigned seq = s_seq.fetch_add(1, std::memory_order_relaxed);
	return wxString::Format(wxT("%s%c%s_%s%u_t%lu_%s_%u.%s"),
		s_crashDir, wxFILE_SEP_PATH,
		s_exeName,
		kindSuffix,
		CurrentPidPortable(),
		CurrentTidPortable(),
		stamp,
		seq,
		extension);
}

void LogTerminateReason(const wxString& reason)
{
	// Multiple background threads can hit terminate concurrently and
	// each one's reason should land in the log, not clobber the previous.
	// Mutex guards format-then-write — OS-level append-write is atomic
	// but a wxString::Format followed by wxFile::Write is not.
	static std::mutex s_logMtx;
	std::lock_guard<std::mutex> lk(s_logMtx);

	EnsureCrashDir();
	const wxString logPath = wxString::Format(wxT("%s%c%s_terminate_%u.log"),
		s_crashDir, wxFILE_SEP_PATH,
		s_exeName,
		CurrentPidPortable());
	wxFile f(logPath, wxFile::write_append);
	if (f.IsOpened()) {
		const wxString line = wxString::Format(wxT("%s  tid=%lu  %s\n"),
			wxDateTime::Now().FormatISOCombined(),
			CurrentTidPortable(),
			reason);
		f.Write(line);
		f.Close();
	}
}

#ifdef __WXMSW__
LONG WINAPI PersistentCrashDumpFilter(EXCEPTION_POINTERS* ep)
{
	// Persistent minidump fires before any wx-level dialog. wx wipes
	// its temp directory on dialog close; our dumps survive.
	const wxString dumpPath = MakeDumpPath(wxEmptyString, wxT("dmp"));

	HANDLE hFile = ::CreateFileW(dumpPath.wc_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mei = {};
		mei.ThreadId = ::GetCurrentThreadId();
		mei.ExceptionPointers = ep;
		mei.ClientPointers = FALSE;

		const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithDataSegs |
			MiniDumpWithHandleData |
			MiniDumpWithUnloadedModules |
			MiniDumpWithThreadInfo |
			MiniDumpWithFullMemory);

		::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(),
			hFile, type, ep ? &mei : nullptr, nullptr, nullptr);
		::CloseHandle(hFile);
	}

	// Chain to the previous filter (wx's, if frontend installed it).
	return s_prevSehFilter ? s_prevSehFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}
#else
// POSIX signal handler — async-signal-safe. open + write + close +
// backtrace_symbols_fd are explicitly allowed by POSIX; wxString /
// wxFile / wxLog are NOT.
void PosixCrashSignalHandler(int sig)
{
	char path[1024];
	const char* dir = s_crashDir.empty()
		? "crashdumps"
		: s_crashDir.ToUTF8().data();
	std::snprintf(path, sizeof(path), "%s/%s_signal_%u_t%lu.log",
		dir,
		s_exeName.IsEmpty() ? "oes" : (const char*)s_exeName.ToUTF8(),
		CurrentPidPortable(),
		CurrentTidPortable());

	const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		char hdr[256];
		const int n = std::snprintf(hdr, sizeof(hdr),
			"signal=%d  pid=%u  tid=%lu\n",
			sig, CurrentPidPortable(), CurrentTidPortable());
		if (n > 0) ::write(fd, hdr, static_cast<size_t>(n));

		void* frames[64];
		const int nf = ::backtrace(frames, 64);
		::backtrace_symbols_fd(frames, nf, fd);
		::close(fd);
	}

	// Re-raise with the default handler — produces a core dump and lets
	// any chained handler (frontend's wx hook) still run on main thread.
	std::signal(sig, SIG_DFL);
	std::raise(sig);
}
#endif

#ifdef __WXMSW__
void __cdecl OesTerminateHandler()
#else
void OesTerminateHandler()
#endif
{
	// Worker / registry / debug-listener threads bypass wxApp's
	// OnUnhandledException — that only catches escapes from the wx
	// main event loop. A C++ throw unwinding off any other thread
	// hits std::terminate, which default-aborts without firing any
	// of our diagnostic chains. We capture the in-flight exception
	// text into a log, then raise an OS-level fault so the platform's
	// crash-dump path produces an artefact against this thread.
	//
	// Outer try / catch: if any step inside the handler itself throws
	// (bad_alloc on rethrow_exception, etc.) we MUST NOT let it
	// re-enter std::terminate — that's an infinite recursion on most
	// platforms. Swallow and fall through to the raise below.
	try {
		wxString reason = wxT("std::terminate (no in-flight exception)");
		try {
			auto p = std::current_exception();
			if (p) std::rethrow_exception(p);
		}
		catch (const ibBackendException& e) {
			reason = wxT("ibBackendException: ") + e.GetErrorDescription();
		}
		catch (const std::exception& e) {
			reason = wxT("std::exception: ") + wxString::FromUTF8(e.what());
		}
		catch (...) {
			reason = wxT("unknown C++ exception");
		}

		LogTerminateReason(reason);
	}
	catch (...) {
	}

#ifdef __WXMSW__
	// Synthetic non-continuable SEH so PersistentCrashDumpFilter writes
	// a dump on this thread. 0xE0E50001 — 'OE' magic + sub-code.
	::RaiseException(0xE0E50001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
	std::raise(SIGABRT);
#endif

	if (s_prevTerminate) s_prevTerminate();
	std::abort();
}

} // namespace

void Install(const wxString& exeName)
{
	// Update the label even on repeat install — frontend might call
	// after console / web layer already armed the handlers.
	s_exeName = exeName;

	if (s_installed.exchange(true, std::memory_order_acq_rel))
		return;

	if (s_prevTerminate == nullptr)
		s_prevTerminate = std::set_terminate(&OesTerminateHandler);

#ifdef __WXMSW__
	if (s_prevSehFilter == nullptr)
		s_prevSehFilter = ::SetUnhandledExceptionFilter(&PersistentCrashDumpFilter);
#else
	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &PosixCrashSignalHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	::sigaction(SIGSEGV, &sa, nullptr);
	::sigaction(SIGABRT, &sa, nullptr);
	::sigaction(SIGFPE,  &sa, nullptr);
	::sigaction(SIGILL,  &sa, nullptr);
	::sigaction(SIGBUS,  &sa, nullptr);
#endif
}

void LogStartupError(const wxString& exeName, const wxString& message)
{
	const wxString logPath = exeName + wxT("_startup.log");
	wxFile f(logPath, wxFile::write_append);
	if (f.IsOpened()) {
		const wxString line = wxDateTime::Now().FormatISOCombined()
			+ wxT("  ") + message + wxT("\n");
		f.Write(line);
		f.Close();
	}
}

void LogUnhandledException(const wxString& exeName, const wxString& diag)
{
	const wxString logPath = exeName + wxT("_unhandled.log");
	wxFile f(logPath, wxFile::write_append);
	if (f.IsOpened()) {
		f.Write(wxDateTime::Now().FormatISOCombined()
			+ wxT("  ") + diag + wxT("\n"));
		f.Close();
	}
}

void PlatformNativeMessage(const wxString& caption, const wxString& message)
{
#ifdef __WXMSW__
	// MessageBoxW doesn't depend on the wx event loop being up. Safer
	// than wxMessageBox during startup / on background threads / after
	// teardown.
	::MessageBoxW(NULL, message.wc_str(), caption.wc_str(),
		MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
	// stderr — async-signal-safe enough, no GUI to drive in console mode.
	std::fprintf(stderr, "[%s] %s\n",
		(const char*)caption.ToUTF8(),
		(const char*)message.ToUTF8());
	std::fflush(stderr);
#endif
}

void TerminateProcessFast(int exitCode)
{
#ifdef __WXMSW__
	::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(exitCode));
#else
	std::_Exit(exitCode);
#endif
}

int WrapStartup(const wxString& exeName, std::function<int()> body)
{
	auto report = [&exeName](const wxString& msg) {
		LogStartupError(exeName, msg);
		PlatformNativeMessage(wxT("OES ") + exeName + wxT(" - startup error"), msg);
	};
	try {
		return body();
	}
	catch (const ibBackendException& e) {
		report(wxT("ibBackendException during startup: ") + e.GetErrorDescription());
		return 1;
	}
	catch (const std::exception& e) {
		report(wxT("std::exception during startup: ") + wxString::FromUTF8(e.what()));
		return 1;
	}
	catch (...) {
		report(wxT("Unknown exception during startup (non-std, non-ibBackend)"));
		return 1;
	}
}

wxString GetCrashDir()
{
	EnsureCrashDir();
	return s_crashDir;
}

} // namespace ibCrashGuard
