#include "firebirdLocalServer.h"
#include "backend/diagnostics/journal.h"   // ibJournal — this TU does not pull in backend_core.h

// OES_FB_LOCALSERVER — compile-time gate for the bitness-decoupling
// out-of-process server path. OFF by default; the only scenario that
// needs it is "x86 OES driving an x64 FB engine" for working sets
// that overflow the x86 3 GB address space (Phase 6 archive-node
// case). Embedded / leader-election / mesh modes do NOT need this.
//
// When OFF: the methods return 0 / "" / no-op, leader-mode caller's
// existing `EnsureStarted() == 0` branch handles the degrade-to-
// embedded path automatically. winsock + windows.h + wxExecute stay
// out of the translation unit so a minimal build doesn't pull them.
//
// When ON: define OES_FB_LOCALSERVER for the build (MSBuild
// Preprocessor Definitions or CMake -DOES_FB_LOCALSERVER=ON), and
// drop `firebird.exe` into `_fb/` next to fbclient.dll. Driver
// surfaces a clear runtime error if the binary is still missing.

#ifdef OES_FB_LOCALSERVER

#include "firebirdBootstrap.h"
#include "firebirdCommon.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <wx/utils.h>

#ifdef __WXMSW__
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <atomic>
#include <mutex>

namespace {

// Process-wide state. Once `EnsureStarted` succeeds, `s_port` carries
// the bound TCP port; `s_childPid` is the spawned `firebird.exe`
// process id. Both reset to 0 on `Stop` / failed start.
//
// `s_spawnMu` serialises the spawn path so a pool of cloned drivers
// can't both race past the `s_port == 0` fast-path check and end up
// spawning two firebird.exe processes (one orphan eating a port).
std::atomic<int>  s_port{0};
std::atomic<long> s_childPid{0};
std::mutex        s_spawnMu;
bool              s_atexitInstalled = false;   // guarded by s_spawnMu

// Pick a free TCP port by binding to port 0 and asking the OS what
// it assigned. Race window between close-and-spawn is acceptable
// for this use case — child grabs the same port immediately, and
// even if another process snatches it the child fails fast.
int PickFreePort() {
#ifdef __WXMSW__
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET) {
		WSACleanup();
		return 0;
	}

	sockaddr_in addr = {};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port        = 0;  // OS picks
	if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
		closesocket(s);
		WSACleanup();
		return 0;
	}

	sockaddr_in bound = {};
	int boundLen = sizeof(bound);
	if (getsockname(s, (sockaddr*)&bound, &boundLen) != 0) {
		closesocket(s);
		WSACleanup();
		return 0;
	}
	const int port = ntohs(bound.sin_port);
	closesocket(s);
	WSACleanup();
	return port;
#else
	// POSIX path — same idea via plain BSD sockets. Not wired here
	// because FB-on-OES on POSIX uses the server mode directly, not
	// our local-server spawn. Stub returns 0 so caller surfaces the
	// "not started" error.
	return 0;
#endif
}

// PID file location — per-OES-process so multiple OES instances
// (designer + enterprise on the same host, or two enterprises on
// the same install) don't stomp each other's recovery record.
// Filename embeds our own PID so each instance owns its own file.
wxString PidFilePath() {
	wxFileName fn(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes-fb-localserver-%ld.pid"),
			(long)wxGetProcessId()));
	return fn.GetFullPath();
}

// Record the PID + port so a recovery tool can find / kill the
// child after a crash. Best-effort — failure to write isn't fatal.
void WritePidFile(long pid, int port) {
	const wxString p = PidFilePath();
	wxTextFile f(p);
	if (f.Exists()) f.Open(); else f.Create();
	f.Clear();
	f.AddLine(wxString::Format(wxT("pid=%ld"), pid));
	f.AddLine(wxString::Format(wxT("port=%d"), port));
	f.AddLine(wxString::Format(wxT("parent=%ld"), (long)wxGetProcessId()));
	f.Write();
	f.Close();
}

void RemovePidFile() {
	const wxString p = PidFilePath();
	if (wxFileExists(p))
		wxRemoveFile(p);
}

// Wait for the spawned firebird.exe to start listening on `port`.
// Tries connecting every 100 ms for up to `timeoutMs`. Returns
// true on success.
bool WaitForListenReady(int port, int timeoutMs) {
#ifdef __WXMSW__
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	bool ok = false;
	for (int elapsed = 0; elapsed < timeoutMs; elapsed += 100) {
		SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
		if (s != INVALID_SOCKET) {
			sockaddr_in addr = {};
			addr.sin_family      = AF_INET;
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			addr.sin_port        = htons((u_short)port);
			if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
				ok = true;
				closesocket(s);
				break;
			}
			closesocket(s);
		}
		wxMilliSleep(100);
	}
	WSACleanup();
	return ok;
#else
	(void)port; (void)timeoutMs;
	return false;
#endif
}

// Atexit hook — invoked when OES process exits cleanly, ensures we
// don't leave an orphan firebird.exe behind. NOT the only safety net:
// the Job Object below kills the child even on hard OES crashes where
// atexit doesn't run.
void AtExitHook() {
	ibFirebirdLocalServer::Stop();
}

#ifdef __WXMSW__
// Job Object for hard-binding firebird.exe to our process lifetime.
// `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` means: when the last handle to
// the job is closed (i.e. when OES process dies — even on crash /
// TerminateProcess / OOM / power loss in the user sense), Windows
// kernel terminates every process assigned to the job. This is the
// only reliable way to guarantee no orphan firebird.exe survives.
//
// Created lazily on first AssignChildToJob call; kept as a static
// HANDLE for the OES process lifetime. Intentionally never closed
// explicitly — letting the OS close it on process exit is the
// trigger that kills the child.
HANDLE g_jobHandle = nullptr;

bool AssignChildToJob(long pid) {
	if (!g_jobHandle) {
		g_jobHandle = ::CreateJobObjectW(nullptr, nullptr);
		if (!g_jobHandle) {
			ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLocalServer: CreateJobObject ")
			             wxT("failed (err=%lu); orphan-protection disabled"),
			             ::GetLastError());
			return false;
		}
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
		info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!::SetInformationJobObject(g_jobHandle,
		                               JobObjectExtendedLimitInformation,
		                               &info, sizeof(info))) {
			ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLocalServer: SetInformationJobObject ")
			             wxT("failed (err=%lu); orphan-protection disabled"),
			             ::GetLastError());
			::CloseHandle(g_jobHandle);
			g_jobHandle = nullptr;
			return false;
		}
	}

	HANDLE hProc = ::OpenProcess(
		PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, (DWORD)pid);
	if (!hProc) {
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLocalServer: OpenProcess(PID %ld) failed ")
		             wxT("(err=%lu); child won't be killed on OES crash"),
		             pid, ::GetLastError());
		return false;
	}

	const BOOL ok = ::AssignProcessToJobObject(g_jobHandle, hProc);
	const DWORD err = ::GetLastError();
	::CloseHandle(hProc);

	if (!ok) {
		// Common failure: child process is already in another job
		// (e.g. running under a debugger that wraps us in its own
		// job without JOB_OBJECT_LIMIT_BREAKAWAY_OK). Nested jobs
		// require Windows 8+; usually works. If it fails, fall back
		// to atexit-only — orphan possible on crash.
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLocalServer: AssignProcessToJobObject ")
		             wxT("failed for PID %ld (err=%lu); child won't be ")
		             wxT("killed on OES crash. Atexit cleanup still works."),
		             pid, err);
		return false;
	}
	return true;
}
#endif // __WXMSW__

} // namespace

int ibFirebirdLocalServer::EnsureStarted() {
	// Lock-free fast path — most callers (every db_query checkout
	// from a warm pool) hit this and avoid the mutex.
	if (s_port.load(std::memory_order_acquire) != 0)
		return s_port.load(std::memory_order_acquire);

	// Slow path — serialise spawn so concurrent pool-clone callers
	// don't both pass the fast path and double-spawn. Re-check
	// under the mutex (classic double-checked init).
	std::lock_guard<std::mutex> g(s_spawnMu);
	if (s_port.load(std::memory_order_acquire) != 0)
		return s_port.load(std::memory_order_acquire);

	// Locate firebird.exe in the vendored runtime dir.
	const wxString fbDir = ibFirebirdBootstrap::GetFbRuntimeDir();
	if (fbDir.IsEmpty()) {
		ibJournalError(wxT("db.firebird"),wxT("ibFirebirdLocalServer: _fb/ not initialised; ")
		           wxT("call ibFirebirdBootstrap::Init first."));
		return 0;
	}

	wxFileName fbExeFn(fbDir, wxT("firebird.exe"));
	const wxString fbExe = fbExeFn.GetFullPath();
	if (!wxFileExists(fbExe)) {
		ibJournalError(wxT("db.firebird"),wxT("ibFirebirdLocalServer: firebird.exe not found at %s. ")
		           wxT("LocalServer mode requires the FB server binary in _fb/ ")
		           wxT("(not vendored by default; operator must drop it in)."),
		           fbExe);
		return 0;
	}

	const int port = PickFreePort();
	if (port == 0) {
		ibJournalError(wxT("db.firebird"),wxT("ibFirebirdLocalServer: failed to pick free port"));
		return 0;
	}

	// IMPORTANT: not using wxExecute. wxExecute asserts
	// `wxThread::IsMain()` on BOTH Win32 and POSIX — it's a wx
	// fundamental constraint, not a platform-specific one (uses
	// the main event loop's pipe machinery to wait for the child).
	// PromoteSelfToLeader → EnsureStarted runs from our heartbeat
	// thread; a wxExecute path crashes there with "wxExecute() can
	// be called only from the main thread". OS-level spawn APIs
	// (CreateProcessW on Win32, posix_spawn on POSIX) have no such
	// restriction. POSIX path is still stubbed below — Phase 6
	// POSIX implementation deferred (would need posix_spawn +
	// PR_SET_PDEATHSIG/kqueue EVFILT_PROC for kill-on-OES-crash).
	long pid = 0;
#ifdef __WXMSW__
	{
		// Spawn: `firebird.exe -a -p <port>` — `-a` = standalone (no service manager required);
		// `-p <port>` = bind to TCP port. Built HERE: the POSIX branch below is still a stub and
		// spawns nothing, so off MSW this string has no reader.
		const wxString cmd = wxString::Format(
			wxT("\"%s\" -a -p %d"), fbExe, port);

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};

		// CreateProcessW may modify the command-line buffer in place;
		// make a mutable copy. Sized to cover quoted path + flags.
		std::vector<wchar_t> mutableCmd(cmd.wc_str(),
			cmd.wc_str() + cmd.length() + 1);

		const BOOL ok = ::CreateProcessW(
			nullptr,                  // application name — taken from cmdline[0]
			mutableCmd.data(),
			nullptr, nullptr,
			FALSE,                    // don't inherit handles
			CREATE_NO_WINDOW,         // equivalent of wxEXEC_HIDE_CONSOLE
			nullptr, nullptr,
			&si, &pi);
		if (!ok) {
			ibJournalError(wxT("db.firebird"),wxT("ibFirebirdLocalServer: CreateProcessW failed for %s ")
			           wxT("(err=%lu)"), cmd, (unsigned long)::GetLastError());
			return 0;
		}
		pid = (long)pi.dwProcessId;
		// Don't need the thread handle. The process handle stays open
		// only long enough to assign it to our Job Object below;
		// closing it doesn't kill the child (handle ≠ process).
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
	}
#else
	ibJournalError(wxT("db.firebird"),wxT("ibFirebirdLocalServer: POSIX spawn not implemented; ")
	           wxT("LocalServer requires Win32"));
	return 0;
#endif
	if (pid <= 0) return 0;

	// Wait for the child to bind and accept connections. 5 seconds
	// is generous — typical bind takes <500 ms on a warm machine.
	if (!WaitForListenReady(port, 5000)) {
		ibJournalError(wxT("db.firebird"),wxT("ibFirebirdLocalServer: child PID %ld did not bind to ")
		           wxT("port %d within 5 s; killing"), pid, port);
		wxKill(pid, wxSIGKILL);
		return 0;
	}

	// firebird.exe binds the listening socket EARLY in startup —
	// before the engine plugin (engine13), auth plugins (srp,
	// chacha, legacy_auth), and security DB are fully loaded. A
	// caller that attaches immediately after WaitForListenReady
	// races with plugin init and can get an opaque error
	// (e.g. isc_io_error / plugin-not-loaded) instead of a clean
	// timeout. 500 ms is enough on every machine I've measured;
	// the cost is paid once per OES process at first attach.
	wxMilliSleep(500);

#ifdef __WXMSW__
	// Bind child to a Job Object so it dies with us — even on crash,
	// TerminateProcess, OOM, or other hard exits where AtExitHook
	// doesn't fire. AssignChildToJob best-effort: a failure is
	// logged but doesn't abort startup (atexit cleanup still runs
	// on graceful exit).
	AssignChildToJob(pid);
#endif

	s_port.store(port, std::memory_order_release);
	s_childPid.store(pid, std::memory_order_release);
	WritePidFile(pid, port);

	// Atexit registration — guarded by s_spawnMu so two cloned
	// drivers can't both std::atexit() the same hook (would call
	// Stop twice on shutdown; idempotent but noisy).
	if (!s_atexitInstalled) {
		std::atexit(AtExitHook);
		s_atexitInstalled = true;
	}

	// Info-only — visible to attached debugger only (OutputDebugString),
	// not in user-facing Output Window. wxLogDebug would surface here
	// in Debug builds and add startup noise without telling the user
	// anything actionable.
	ibFb::LogThreadSafe(wxString::Format(
		wxT("ibFirebirdLocalServer: spawned PID %ld on port %d"), pid, port));
	return port;
}

int ibFirebirdLocalServer::GetPort() {
	return s_port.load();
}

void ibFirebirdLocalServer::Stop() {
	const long pid = s_childPid.exchange(0);
	if (pid > 0) {
		// SIGTERM equivalent — wxSIGTERM on POSIX; on Windows wxKill
		// maps SIGKILL to TerminateProcess, SIGTERM to a graceful
		// console-close. FB server traps console-close and shuts
		// down cleanly; fall back to SIGKILL if it doesn't exit
		// within 2 s.
		wxKill(pid, wxSIGTERM);
		for (int waited = 0; waited < 2000; waited += 100) {
			if (!wxProcess::Exists(pid)) break;
			wxMilliSleep(100);
		}
		if (wxProcess::Exists(pid)) {
			wxKill(pid, wxSIGKILL);
		}
	}
	s_port.store(0);
	RemovePidFile();
}

wxString ibFirebirdLocalServer::MakeConnectUrl(const wxString& dbPath) {
	const int port = s_port.load();
	if (port == 0) return wxEmptyString;
	// FB inet URL form: `inet://host:port/<absolute-path>`. The
	// child server has the OS file system view, so absolute paths
	// work directly.
	return wxString::Format(wxT("inet://localhost:%d/%s"), port, dbPath);
}

#else // !OES_FB_LOCALSERVER — stub everything to a no-op.

// Disabled build: the public API stays so callers compile, but each
// method reports "not started" / empty. Leader-mode's existing
// `EnsureStarted() == 0` branch already handles this as graceful
// degrade-to-embedded.

int ibFirebirdLocalServer::EnsureStarted() {
	return 0;
}

int ibFirebirdLocalServer::GetPort() {
	return 0;
}

void ibFirebirdLocalServer::Stop() {
	// No-op.
}

wxString ibFirebirdLocalServer::MakeConnectUrl(const wxString& /*dbPath*/) {
	return wxString();
}

#endif // OES_FB_LOCALSERVER
