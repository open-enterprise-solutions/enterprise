#include "firebirdLocalServer.h"
#include "core/diagnostics/journal.h"   // ibJournal — this TU does not pull in backend_core.h

// OES_FB_LOCALSERVER — compile-time gate for the bitness-decoupling
// out-of-process server path. OFF by default; the only scenario that
// needs it is "x86 OES driving an x64 FB engine" for working sets
// that overflow the x86 3 GB address space (Phase 6 archive-node
// case). Embedded / leader-election / mesh modes do NOT need this.
//
// When OFF: the methods return 0 / "" / no-op, leader-mode caller's
// existing `EnsureStarted() == 0` branch handles the degrade-to-
// embedded path automatically. winsock + windows.h + CreateProcessW stay
// out of the translation unit so a minimal build doesn't pull them.
//
// When ON: define OES_FB_LOCALSERVER for the build (MSBuild
// Preprocessor Definitions or CMake -DOES_FB_LOCALSERVER=ON), and
// drop `firebird.exe` into `_fb/` next to fbclient.dll. Driver
// surfaces a clear runtime error if the binary is still missing.

#ifdef OES_FB_LOCALSERVER

#include "firebirdBootstrap.h"
#include "firebirdCommon.h"
#include "core/fileSystem.h"   // ibFileExists, ibRemoveFile, ibPathOf
#include "core/startProcess.h"   // ibStopProcess, ibKillProcess, ibProcessExists
#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::CurrentPidPortable

#ifdef __IBMSW__
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

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
#ifdef __IBMSW__
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
ibString PidFilePath() {
	std::error_code error;
	return ibString((std::filesystem::temp_directory_path(error) / ibPathOf(ibString::Format(
		ibT("oes-fb-localserver-%u.pid"), ibCrashGuard::CurrentPidPortable()))).wstring());
}

// Record the PID + port so a recovery tool can find / kill the
// child after a crash. Best-effort — failure to write isn't fatal.
void WritePidFile(long pid, int port) {
	const ibString p = PidFilePath();
	std::ofstream f(ibPathOf(p), std::ios::trunc);
	f << "pid=" << pid << "\n"
	  << "port=" << port << "\n"
	  << "parent=" << ibCrashGuard::CurrentPidPortable() << "\n";
}

void RemovePidFile() {
	const ibString p = PidFilePath();
	if (ibFileExists(p))
		ibRemoveFile(p);
}

// Wait for the spawned firebird.exe to start listening on `port`.
// Tries connecting every 100 ms for up to `timeoutMs`. Returns
// true on success.
bool WaitForListenReady(int port, int timeoutMs) {
#ifdef __IBMSW__
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
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

#ifdef __IBMSW__
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
			ibJournalWarning(ibT("db.firebird"),ibT("ibFirebirdLocalServer: CreateJobObject ")
			             ibT("failed (err=%lu); orphan-protection disabled"),
			             ::GetLastError());
			return false;
		}
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
		info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!::SetInformationJobObject(g_jobHandle,
		                               JobObjectExtendedLimitInformation,
		                               &info, sizeof(info))) {
			ibJournalWarning(ibT("db.firebird"),ibT("ibFirebirdLocalServer: SetInformationJobObject ")
			             ibT("failed (err=%lu); orphan-protection disabled"),
			             ::GetLastError());
			::CloseHandle(g_jobHandle);
			g_jobHandle = nullptr;
			return false;
		}
	}

	HANDLE hProc = ::OpenProcess(
		PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, (DWORD)pid);
	if (!hProc) {
		ibJournalWarning(ibT("db.firebird"),ibT("ibFirebirdLocalServer: OpenProcess(PID %ld) failed ")
		             ibT("(err=%lu); child won't be killed on OES crash"),
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
		ibJournalWarning(ibT("db.firebird"),ibT("ibFirebirdLocalServer: AssignProcessToJobObject ")
		             ibT("failed for PID %ld (err=%lu); child won't be ")
		             ibT("killed on OES crash. Atexit cleanup still works."),
		             pid, err);
		return false;
	}
	return true;
}
#endif // __IBMSW__

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
	const ibString fbDir = ibFirebirdBootstrap::GetFbRuntimeDir();
	if (fbDir.IsEmpty()) {
		ibJournalError(ibT("db.firebird"),ibT("ibFirebirdLocalServer: _fb/ not initialised; ")
		           ibT("call ibFirebirdBootstrap::Init first."));
		return 0;
	}

	const ibString fbExe = fbDir + ibFILE_SEP_PATH + ibT("firebird.exe");
	if (!ibFileExists(fbExe)) {
		ibJournalError(ibT("db.firebird"),ibT("ibFirebirdLocalServer: firebird.exe not found at %s. ")
		           ibT("LocalServer mode requires the FB server binary in _fb/ ")
		           ibT("(not vendored by default; operator must drop it in)."),
		           fbExe);
		return 0;
	}

	const int port = PickFreePort();
	if (port == 0) {
		ibJournalError(ibT("db.firebird"),ibT("ibFirebirdLocalServer: failed to pick free port"));
		return 0;
	}

	// IMPORTANT: the OS spawn API, straight. PromoteSelfToLeader →
	// EnsureStarted runs from our heartbeat thread, and a spawn that
	// waits on the main event loop crashes there. CreateProcessW on
	// Win32 and posix_spawn on POSIX have no such restriction.
	// POSIX path is still stubbed below — Phase 6
	// POSIX implementation deferred (would need posix_spawn +
	// PR_SET_PDEATHSIG/kqueue EVFILT_PROC for kill-on-OES-crash).
	long pid = 0;
#ifdef __IBMSW__
	{
		// Spawn: `firebird.exe -a -p <port>` — `-a` = standalone (no service manager required);
		// `-p <port>` = bind to TCP port. Built HERE: the POSIX branch below is still a stub and
		// spawns nothing, so off MSW this string has no reader.
		const ibString cmd = ibString::Format(
			ibT("\"%s\" -a -p %d"), fbExe, port);

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};

		// CreateProcessW may modify the command-line buffer in place;
		// make a mutable copy. Sized to cover quoted path + flags.
		std::vector<wchar_t> mutableCmd(cmd.wc_str(), cmd.wc_str() + cmd.Len() + 1);

		const BOOL ok = ::CreateProcessW(
			nullptr,                  // application name — taken from cmdline[0]
			mutableCmd.data(),
			nullptr, nullptr,
			FALSE,                    // don't inherit handles
			CREATE_NO_WINDOW,         // no console of its own
			nullptr, nullptr,
			&si, &pi);
		if (!ok) {
			ibJournalError(ibT("db.firebird"),ibT("ibFirebirdLocalServer: CreateProcessW failed for %s ")
			           ibT("(err=%lu)"), cmd, (unsigned long)::GetLastError());
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
	ibJournalError(ibT("db.firebird"),ibT("ibFirebirdLocalServer: POSIX spawn not implemented; ")
	           ibT("LocalServer requires Win32"));
	return 0;
#endif
	if (pid <= 0) return 0;

	// Wait for the child to bind and accept connections. 5 seconds
	// is generous — typical bind takes <500 ms on a warm machine.
	if (!WaitForListenReady(port, 5000)) {
		ibJournalError(ibT("db.firebird"),ibT("ibFirebirdLocalServer: child PID %ld did not bind to ")
		           ibT("port %d within 5 s; killing"), pid, port);
		ibKillProcess(pid);
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
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

#ifdef __IBMSW__
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
	// not in user-facing Output Window. A debug log line would surface here
	// in Debug builds and add startup noise without telling the user
	// anything actionable.
	ibFb::LogThreadSafe(ibString::Format(
		ibT("ibFirebirdLocalServer: spawned PID %ld on port %d"), pid, port));
	return port;
}

int ibFirebirdLocalServer::GetPort() {
	return s_port.load();
}

void ibFirebirdLocalServer::Stop() {
	const long pid = s_childPid.exchange(0);
	if (pid > 0) {
		// A request to close first — SIGTERM on POSIX, WM_CLOSE to its windows on
		// Windows. FB server traps it and shuts down cleanly; fall back to
		// ibKillProcess (TerminateProcess / SIGKILL) if it doesn't exit within 2 s.
		ibStopProcess(pid);
		for (int waited = 0; waited < 2000; waited += 100) {
			if (!ibProcessExists(pid)) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		if (ibProcessExists(pid)) {
			ibKillProcess(pid);
		}
	}
	s_port.store(0);
	RemovePidFile();
}

ibString ibFirebirdLocalServer::MakeConnectUrl(const ibString& dbPath) {
	const int port = s_port.load();
	if (port == 0) return ibEmptyString;
	// FB inet URL form: `inet://host:port/<absolute-path>`. The
	// child server has the OS file system view, so absolute paths
	// work directly.
	return ibString::Format(ibT("inet://localhost:%d/%s"), port, dbPath);
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

ibString ibFirebirdLocalServer::MakeConnectUrl(const ibString& /*dbPath*/) {
	return ibString();
}

#endif // OES_FB_LOCALSERVER
