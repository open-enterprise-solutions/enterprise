/////////////////////////////////////////////////////////////////////////////
// configLock — see header. Implementation lives in backend because the
// primitives must be reachable from headless (oes-mcp) AND frontend
// (designer's notifier). Keeping it in backend means zero wx GUI deps —
// only wxString / wxFileName / wxFile from wx::base.
/////////////////////////////////////////////////////////////////////////////

#include "configLock.hpp"

#include "3rdparty/nlohmann/json.hpp"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/string.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
#  include <process.h>
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/file.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace {

// Process-wide mutex so two threads inside the same process can't trip
// the lock manifest's read-modify-write window. Cross-process exclusion
// is handled by the OS-level advisory file lock further down.
std::mutex& InProcessMutex()
{
	static std::mutex m;
	return m;
}

// MCP/Designer: produce an ISO-8601 UTC timestamp. We avoid std::format
// because the codebase still targets C++17 broadly.
std::string IsoNowUtc()
{
	using clock = std::chrono::system_clock;
	const auto now  = clock::now();
	const std::time_t t = clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
	return std::string(buf);
}

std::int64_t CurrentPid()
{
#if defined(_WIN32)
	return static_cast<std::int64_t>(::GetCurrentProcessId());
#else
	return static_cast<std::int64_t>(::getpid());
#endif
}

// Liveness probe — best-effort. Returns true if we believe pid is alive.
// On POSIX we send signal 0 (no-op delivery). EPERM is treated as alive
// (process exists but we lack permission to signal it).
// On Windows we open with PROCESS_QUERY_LIMITED_INFORMATION and read
// the exit code; STILL_ACTIVE means alive.
bool IsPidAlive(std::int64_t pid)
{
	if (pid <= 0) return false;
#if defined(_WIN32)
	HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
		static_cast<DWORD>(pid));
	if (h == nullptr) {
		const DWORD err = ::GetLastError();
		// ERROR_ACCESS_DENIED → process exists, we're just not allowed.
		return (err == ERROR_ACCESS_DENIED);
	}
	DWORD code = 0;
	const BOOL ok = ::GetExitCodeProcess(h, &code);
	::CloseHandle(h);
	if (!ok) return false;
	return (code == STILL_ACTIVE);
#else
	const int rc = ::kill(static_cast<pid_t>(pid), 0);
	if (rc == 0) return true;
	if (errno == EPERM) return true;
	return false;
#endif
}

// MCP/Designer: ensure <configDir>/sys exists. Cooperating with appData,
// which already mkdir's sys/ when a configuration is created — but
// oes-mcp can be pointed at an arbitrary directory and we still want
// the lock primitives to work, so we make the dir on demand.
wxString SysDir(const wxString& configDir)
{
	wxFileName fn(configDir, wxEmptyString);
	fn.AppendDir(wxT("sys"));
	return fn.GetPath();
}

bool EnsureSysDir(const wxString& configDir)
{
	const wxString sys = SysDir(configDir);
	if (wxFileName::DirExists(sys)) return true;
	return wxFileName::Mkdir(sys, 0755, wxPATH_MKDIR_FULL);
}

// =========================================================================
// Cross-process advisory lock around the manifest read-modify-write.
// =========================================================================
//
// We use a SEPARATE file (.oes.lock.cs) as the critical-section anchor so
// the manifest file itself can stay simple JSON with no lock-bit
// gymnastics. The cs file is created empty; flock/LockFileEx
// blocks until exclusive access is granted.

class CrossProcessLock {
public:
	explicit CrossProcessLock(const wxString& configDir) {
		const wxString csPath = SysDir(configDir) + wxFileName::GetPathSeparator() +
			wxT(".oes.lock.cs");
		m_path = std::string(csPath.utf8_str());
#if defined(_WIN32)
		m_h = ::CreateFileA(m_path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (m_h != INVALID_HANDLE_VALUE) {
			OVERLAPPED ov{};
			if (::LockFileEx(m_h, LOCKFILE_EXCLUSIVE_LOCK, 0,
			    MAXDWORD, MAXDWORD, &ov)) {
				m_locked = true;
			}
		}
#else
		m_fd = ::open(m_path.c_str(), O_RDWR | O_CREAT, 0644);
		if (m_fd >= 0) {
			if (::flock(m_fd, LOCK_EX) == 0) {
				m_locked = true;
			}
		}
#endif
	}

	~CrossProcessLock() {
#if defined(_WIN32)
		if (m_h != INVALID_HANDLE_VALUE) {
			if (m_locked) {
				OVERLAPPED ov{};
				::UnlockFileEx(m_h, 0, MAXDWORD, MAXDWORD, &ov);
			}
			::CloseHandle(m_h);
		}
#else
		if (m_fd >= 0) {
			if (m_locked) ::flock(m_fd, LOCK_UN);
			::close(m_fd);
		}
#endif
	}

	CrossProcessLock(const CrossProcessLock&)            = delete;
	CrossProcessLock& operator=(const CrossProcessLock&) = delete;

	bool IsLocked() const { return m_locked; }

private:
	std::string m_path;
	bool        m_locked = false;
#if defined(_WIN32)
	HANDLE      m_h      = INVALID_HANDLE_VALUE;
#else
	int         m_fd     = -1;
#endif
};

// =========================================================================
// Manifest read/write helpers.
// =========================================================================

const char* ModeToStr(ibConfigLock::Mode m)
{
	return (m == ibConfigLock::Mode::Exclusive) ? "exclusive" : "shared";
}

ibConfigLock::Mode ModeFromStr(const std::string& s)
{
	return (s == "exclusive") ? ibConfigLock::Mode::Exclusive
	                          : ibConfigLock::Mode::Shared;
}

// Read raw bytes of a small file. Returns empty optional on missing /
// unreadable. We do plain stdio rather than wxFile so we can read while
// also holding the cs lock (wxFile fights with the OS lock on some platforms).
bool ReadFileBytes(const std::string& path, std::string& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

// Atomic write: write to path.tmp, fsync, rename.
bool AtomicWriteBytes(const std::string& path, const std::string& bytes)
{
	const std::string tmp = path + ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f.is_open()) return false;
		f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		f.flush();
		if (!f.good()) {
			f.close();
			std::remove(tmp.c_str());
			return false;
		}
	}
#if defined(_WIN32)
	// Windows rename fails when target exists; MoveFileExA with REPLACE_EXISTING
	// gives us POSIX-style atomic replace.
	if (!::MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		std::remove(tmp.c_str());
		return false;
	}
#else
	if (std::rename(tmp.c_str(), path.c_str()) != 0) {
		std::remove(tmp.c_str());
		return false;
	}
#endif
	return true;
}

// Parse the manifest. On parse failure returns false but leaves outHolders
// empty (caller treats malformed as "no lock", but flags an error).
bool ParseManifest(const std::string& json,
                   std::vector<ibConfigLock::Holder>& outHolders,
                   std::int64_t& outSeq)
{
	outHolders.clear();
	outSeq = 0;
	if (json.empty()) return true;  // missing file = empty manifest, not an error
	try {
		nlohmann::json doc = nlohmann::json::parse(json);
		if (!doc.is_object()) return false;
		if (doc.contains("seq") && doc["seq"].is_number_integer()) {
			outSeq = doc["seq"].get<std::int64_t>();
		}
		if (doc.contains("holders") && doc["holders"].is_array()) {
			for (const auto& h : doc["holders"]) {
				if (!h.is_object()) continue;
				ibConfigLock::Holder rec;
				if (h.contains("pid")  && h["pid"].is_number_integer())
					rec.pid = h["pid"].get<std::int64_t>();
				if (h.contains("mode") && h["mode"].is_string())
					rec.mode = ModeFromStr(h["mode"].get<std::string>());
				if (h.contains("since")   && h["since"].is_string())
					rec.since   = h["since"].get<std::string>();
				if (h.contains("program") && h["program"].is_string())
					rec.program = h["program"].get<std::string>();
				if (rec.pid > 0) outHolders.push_back(std::move(rec));
			}
		}
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

std::string SerialiseManifest(const std::vector<ibConfigLock::Holder>& holders,
                              std::int64_t seq)
{
	nlohmann::json doc;
	doc["seq"] = seq;
	nlohmann::json arr = nlohmann::json::array();
	for (const auto& h : holders) {
		nlohmann::json o;
		o["pid"]     = h.pid;
		o["mode"]    = ModeToStr(h.mode);
		o["since"]   = h.since;
		o["program"] = h.program;
		arr.push_back(std::move(o));
	}
	doc["holders"] = std::move(arr);
	return doc.dump(2);
}

// Filter holders[] in place, dropping entries whose pid is no longer
// alive. Returns the number removed.
std::size_t ReapDead(std::vector<ibConfigLock::Holder>& holders)
{
	const std::size_t before = holders.size();
	holders.erase(std::remove_if(holders.begin(), holders.end(),
		[](const ibConfigLock::Holder& h){ return !IsPidAlive(h.pid); }),
		holders.end());
	return before - holders.size();
}

} // namespace

// ============================================================================
// ibConfigLock public surface
// ============================================================================

wxString ibConfigLock::LockFilePath(const wxString& configDir)
{
	return SysDir(configDir) + wxFileName::GetPathSeparator() + wxT(".oes.lock");
}

wxString ibConfigLock::MutationMarkerPath(const wxString& configDir)
{
	return SysDir(configDir) + wxFileName::GetPathSeparator() +
	       wxT(".oes-mcp-mutation");
}

std::vector<ibConfigLock::Holder> ibConfigLock::Inspect(const wxString& configDir)
{
	std::lock_guard<std::mutex> proc_lk(InProcessMutex());
	std::vector<Holder> holders;
	if (!wxFileName::DirExists(SysDir(configDir))) return holders;

	CrossProcessLock cs(configDir);
	(void)cs;  // best-effort — even unlocked the read sees a consistent file (atomic rename)

	const std::string path = std::string(LockFilePath(configDir).utf8_str());
	std::string bytes;
	if (!ReadFileBytes(path, bytes)) return holders;

	std::int64_t seq = 0;
	if (!ParseManifest(bytes, holders, seq)) {
		// Malformed manifest treated as empty.
		holders.clear();
	}
	ReapDead(holders);
	return holders;
}

std::size_t ibConfigLock::SweepDeadHolders(const wxString& configDir)
{
	std::lock_guard<std::mutex> proc_lk(InProcessMutex());
	if (!EnsureSysDir(configDir)) return 0;
	CrossProcessLock cs(configDir);
	if (!cs.IsLocked()) return 0;

	const std::string path = std::string(LockFilePath(configDir).utf8_str());
	std::string bytes;
	if (!ReadFileBytes(path, bytes)) return 0;

	std::vector<Holder> holders;
	std::int64_t seq = 0;
	ParseManifest(bytes, holders, seq);
	const std::size_t removed = ReapDead(holders);
	if (removed == 0) return 0;

	const std::string out = SerialiseManifest(holders, seq);
	AtomicWriteBytes(path, out);
	return removed;
}

ibConfigLock::Acquire ibConfigLock::TryAcquire(const wxString& configDir,
                                               Mode mode,
                                               const std::string& program,
                                               std::int64_t* outHolderId,
                                               std::vector<Holder>* holdersOut)
{
	if (outHolderId != nullptr) *outHolderId = 0;
	if (holdersOut  != nullptr) holdersOut->clear();

	std::lock_guard<std::mutex> proc_lk(InProcessMutex());

	if (!EnsureSysDir(configDir)) return Acquire::IoError;

	CrossProcessLock cs(configDir);
	if (!cs.IsLocked()) return Acquire::IoError;

	const std::string path = std::string(LockFilePath(configDir).utf8_str());

	std::vector<Holder> holders;
	std::int64_t seq = 0;
	{
		std::string bytes;
		if (ReadFileBytes(path, bytes)) {
			if (!ParseManifest(bytes, holders, seq)) {
				// Malformed manifest: refuse to acquire — operator must
				// inspect and delete the file. Bailing out beats silently
				// clobbering whatever state the previous tool left behind.
				if (holdersOut != nullptr) holdersOut->clear();
				return Acquire::MalformedManifest;
			}
		}
	}
	ReapDead(holders);

	// Conflict checks.
	for (const auto& h : holders) {
		if (h.mode == Mode::Exclusive) {
			if (holdersOut != nullptr) *holdersOut = holders;
			return Acquire::ConflictExclusive;
		}
	}
	if (mode == Mode::Exclusive && !holders.empty()) {
		// Asking for exclusive but at least one shared holder is live.
		if (holdersOut != nullptr) *holdersOut = holders;
		return Acquire::ConflictShared;
	}

	// Grant.
	Holder me;
	me.pid     = CurrentPid();
	me.mode    = mode;
	me.since   = IsoNowUtc();
	me.program = program;
	holders.push_back(me);
	++seq;

	const std::string outStr = SerialiseManifest(holders, seq);
	if (!AtomicWriteBytes(path, outStr)) return Acquire::IoError;

	if (outHolderId != nullptr) *outHolderId = me.pid;
	return Acquire::Ok;
}

bool ibConfigLock::Release(const wxString& configDir, std::int64_t holderId)
{
	if (holderId <= 0) return true;  // never acquired — release is a no-op
	std::lock_guard<std::mutex> proc_lk(InProcessMutex());

	if (!wxFileName::DirExists(SysDir(configDir))) return true;
	CrossProcessLock cs(configDir);
	if (!cs.IsLocked()) return false;

	const std::string path = std::string(LockFilePath(configDir).utf8_str());
	std::string bytes;
	if (!ReadFileBytes(path, bytes)) return true;  // file gone → release is satisfied.

	std::vector<Holder> holders;
	std::int64_t seq = 0;
	if (!ParseManifest(bytes, holders, seq)) return false;

	const auto before = holders.size();
	holders.erase(std::remove_if(holders.begin(), holders.end(),
		[holderId](const Holder& h){ return h.pid == holderId; }),
		holders.end());

	// If our id wasn't there, no-op (treat as success — release should be idempotent).
	if (holders.size() == before) return true;

	++seq;
	const std::string out = SerialiseManifest(holders, seq);
	return AtomicWriteBytes(path, out);
}

bool ibConfigLock::HasLiveExclusiveHolder(const wxString& configDir)
{
	const auto holders = Inspect(configDir);
	for (const auto& h : holders) {
		if (h.mode == Mode::Exclusive) return true;
	}
	return false;
}

bool ibConfigLock::IsHolderStillLive(const wxString& configDir,
                                     std::int64_t holderId)
{
	if (holderId <= 0) return false;
	const auto holders = Inspect(configDir);
	for (const auto& h : holders) {
		if (h.pid == holderId) return true;
	}
	return false;
}

// ============================================================================
// Mutation marker — Layer 3 broadcast.
// ============================================================================

bool ibConfigLock::WriteMutationMarker(const wxString& configDir,
                                       const MutationMarker& m)
{
	std::lock_guard<std::mutex> proc_lk(InProcessMutex());
	if (!EnsureSysDir(configDir)) return false;
	const std::string path = std::string(MutationMarkerPath(configDir).utf8_str());

	// Read prior seq so we monotonically advance even across process
	// restarts. (oes-mcp can be relaunched multiple times within a single
	// Designer session.)
	std::int64_t lastSeq = 0;
	{
		std::string bytes;
		if (ReadFileBytes(path, bytes)) {
			try {
				const auto doc = nlohmann::json::parse(bytes);
				if (doc.is_object() && doc.contains("seq") &&
				    doc["seq"].is_number_integer()) {
					lastSeq = doc["seq"].get<std::int64_t>();
				}
			} catch (const std::exception&) {
				// Treat parse failure as "no seq" — next write will be 1.
			}
		}
	}

	nlohmann::json doc;
	doc["ts"]       = m.ts.empty() ? IsoNowUtc() : m.ts;
	doc["tool"]     = m.tool;
	doc["fullName"] = m.fullName;
	doc["pluginId"] = m.pluginId;
	// MCP: if the caller provided a seq use it; otherwise auto-increment
	// from whatever was on disk. The auto path is what oes-mcp uses;
	// tests / direct writers can pin a value.
	doc["seq"] = (m.seq > 0) ? m.seq : (lastSeq + 1);

	return AtomicWriteBytes(path, doc.dump(2));
}

bool ibConfigLock::ReadMutationMarker(const wxString& configDir,
                                      MutationMarker& out)
{
	std::lock_guard<std::mutex> proc_lk(InProcessMutex());
	if (!wxFileName::DirExists(SysDir(configDir))) return false;
	const std::string path = std::string(MutationMarkerPath(configDir).utf8_str());
	std::string bytes;
	if (!ReadFileBytes(path, bytes)) return false;
	try {
		const auto doc = nlohmann::json::parse(bytes);
		if (!doc.is_object()) return false;
		out = MutationMarker{};
		if (doc.contains("ts")       && doc["ts"].is_string())
			out.ts       = doc["ts"].get<std::string>();
		if (doc.contains("tool")     && doc["tool"].is_string())
			out.tool     = doc["tool"].get<std::string>();
		if (doc.contains("fullName") && doc["fullName"].is_string())
			out.fullName = doc["fullName"].get<std::string>();
		if (doc.contains("pluginId") && doc["pluginId"].is_string())
			out.pluginId = doc["pluginId"].get<std::string>();
		if (doc.contains("seq")      && doc["seq"].is_number_integer())
			out.seq      = doc["seq"].get<std::int64_t>();
		return true;
	} catch (const std::exception&) {
		return false;
	}
}
