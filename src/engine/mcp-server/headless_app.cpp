/////////////////////////////////////////////////////////////////////////////
// headless_app — see header. Boots appData with eDESIGNER_MODE because that
// is the mode the existing metaBridge mutation paths were designed against
// (designer Ctrl+Z stack, plugin manager policy gate). Enterprise mode
// would also work for read-only flows but tightens auth and AccessRight
// checks we don't want to fight in an MCP context.
/////////////////////////////////////////////////////////////////////////////

#include "headless_app.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/plugin/pluginManager.h"
#include "backend/plugin/metaBridge.h"
#include "backend/backend_exception.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/utils/configLock.hpp"
#include "backend/migration/snapshotManager.hpp"

#include <wx/string.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace mcpServer {
namespace {

std::mutex          g_mu;
std::atomic<bool>   g_ready{false};
std::atomic<bool>   g_loading{false};
std::string         g_loadedPath;
DiagSink            g_diag = nullptr;

// MCP concurrency: id returned by ibConfigLock::TryAcquire (currently
// our pid). Zero when no lock is held — Shutdown checks this and skips
// Release in --no-config / failed-init paths.
std::int64_t        g_lockHolderId = 0;

// MCP auto-snapshot: lazy-created. Init builds it as soon as we know
// `g_loadedPath`; Shutdown clears it. nullptr when --no-config OR when
// the env var is set to a disabled value and no snapshots already
// live on disk.
std::unique_ptr<migration::snapshots::ibSnapshotManager> g_snapshotMgr;

// MCP: emit a single diagnostic line through whatever sink the caller
// installed. Falls back to stderr so unit tests + interactive shells
// see the same messages even when no sink is wired.
void Diag(const std::string& line)
{
	if (g_diag != nullptr) {
		g_diag(line.c_str());
		return;
	}
	std::fprintf(stderr, "%s\n", line.c_str());
}

bool EndsWithIgnoreCase(const std::string& s, const char* suffix)
{
	const std::size_t n = std::strlen(suffix);
	if (s.size() < n) return false;
	for (std::size_t i = 0; i < n; ++i) {
		const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - n + i])));
		const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
		if (a != b) return false;
	}
	return true;
}

// MCP: drop the filename component from a path. Mirrors the wenterprise-
// server logic — appDataCreateFile wants a directory, not a .fdb file.
std::string DirOf(const std::string& p)
{
	const auto slash = p.find_last_of("/\\");
	return (slash == std::string::npos) ? std::string() : p.substr(0, slash);
}

// MCP: pre-grant the mcp-server plugin id the AllowAlways wildcard so
// every meta.* mutation passes the policy gate without an interactive
// prompt. This is the documented "trust by configuration" path —
// designers approving an MCP integration accept that the calling
// process has full write access. Without this, every HostMetaCreate
// would return IB_PLUGIN_PERMISSION_DENIED.
void GrantMcpServerWildcardPolicy()
{
	if (appData == nullptr) return;
	auto* pm = appData->GetPluginManager();
	if (pm == nullptr) return;
	pm->SetMutationPolicy(wxT("mcp-server"), wxT("*"),
		ibPluginManager::MutationPolicy::AllowAlways);
}

// MCP concurrency: render a holders[] list into a single short line for the
// "configuration is locked" diagnostic. Tries to surface pid + program +
// since so the operator can identify which Designer to restart.
std::string FormatHolders(const std::vector<ibConfigLock::Holder>& holders)
{
	std::ostringstream ss;
	for (std::size_t i = 0; i < holders.size(); ++i) {
		if (i > 0) ss << ", ";
		ss << "{pid=" << holders[i].pid;
		if (!holders[i].program.empty()) ss << " program=" << holders[i].program;
		ss << " mode=" << (holders[i].mode == ibConfigLock::Mode::Exclusive
			? "exclusive" : "shared");
		if (!holders[i].since.empty()) ss << " since=" << holders[i].since;
		ss << "}";
	}
	return ss.str();
}

} // namespace

InitOutcome Init(const HeadlessConfig& cfg, DiagSink diagSink)
{
	std::lock_guard<std::mutex> lk(g_mu);
	g_diag = diagSink;

	if (g_ready.load()) {
		return InitOutcome::Ok;  // idempotent — second Init after success no-ops.
	}

	g_loading.store(true);
	struct LoadingGuard {
		~LoadingGuard() { g_loading.store(false); }
	} loadingGuard;

	if (cfg.configPath.empty()) {
		Diag("oes-mcp: configPath is empty (set argv[1] or OES_CONFIG_PATH)");
		return InitOutcome::GenericFailure;
	}

	// MCP: route on path shape. A directory (or a path ending in /sys.fdb)
	// is the canonical "live configuration" — appDataCreateFile takes the
	// containing directory. A .OES-DB file is a zip snapshot; we still
	// need a transient empty DB directory to back the load, but for v1
	// we refuse that path and ask the operator to unpack first. This
	// keeps the lifecycle clear; the unpack flow can be added in a later
	// commit when there's demand.
	std::string dirPath = cfg.configPath;
	if (EndsWithIgnoreCase(dirPath, ".OES-DB")) {
		Diag("oes-mcp: .OES-DB snapshot loading is not supported yet — "
		     "extract the configuration to a directory containing sys.fdb "
		     "and pass that directory instead");
		return InitOutcome::GenericFailure;
	}
	// Strip /sys.fdb suffix when present so the operator can pass either form.
	if (EndsWithIgnoreCase(dirPath, "/sys.fdb") ||
	    EndsWithIgnoreCase(dirPath, "\\sys.fdb")) {
		dirPath = DirOf(dirPath);
	}

	// =====================================================================
	// MCP concurrency — Layer 1: probe the configuration directory lock.
	// =====================================================================
	// Acquire a SHARED lock before bringing appData up. Designer (legacy)
	// holds an exclusive lock by default; if we see one we refuse to start
	// and instruct the operator to switch Designer to shared mode. If
	// nothing's there or only shared holders, we add our shared entry —
	// many MCP servers + a shared-mode Designer can coexist this way.
	//
	// Lock is RELEASED in Shutdown() (or on failure paths inside Init).
	// We do not block on the lock — TryAcquire is non-blocking; an
	// exclusive holder is a hard "go away" signal, not a wait-for-retry
	// signal (the operator action is to restart Designer).
	{
		const wxString wxDir = wxString::FromUTF8(dirPath.c_str());
		// Sweep dead-pid entries first so a crashed Designer doesn't
		// poison every subsequent MCP launch. SweepDeadHolders is a no-op
		// when nothing's stale.
		const std::size_t reaped = ibConfigLock::SweepDeadHolders(wxDir);
		if (reaped > 0) {
			Diag(std::string("oes-mcp: reaped ") + std::to_string(reaped) +
			     " stale lock holder(s) (process(es) gone)");
		}

		std::vector<ibConfigLock::Holder> blockers;
		std::int64_t holderId = 0;
		const auto outcome = ibConfigLock::TryAcquire(wxDir,
			ibConfigLock::Mode::Shared,
			std::string("oes-mcp"),
			&holderId,
			&blockers);
		switch (outcome) {
		case ibConfigLock::Acquire::Ok:
			g_lockHolderId = holderId;
			break;
		case ibConfigLock::Acquire::ConflictExclusive:
			Diag("oes-mcp: configuration is locked exclusively by another "
			     "process (likely Designer GUI in exclusive mode).");
			Diag("oes-mcp: to use MCP concurrently, restart Designer in "
			     "shared mode: File -> Open Shared (or via menu setting).");
			Diag(std::string("oes-mcp: existing holders: ") +
			     FormatHolders(blockers));
			return InitOutcome::LockedExclusive;
		case ibConfigLock::Acquire::ConflictShared:
			// Asked shared, got shared conflict → impossible per the lock
			// state machine, but defensive: treat as generic failure.
			Diag("oes-mcp: lock manager reported a shared-mode conflict for a "
			     "shared-mode acquisition; aborting");
			return InitOutcome::GenericFailure;
		case ibConfigLock::Acquire::IoError:
			Diag("oes-mcp: could not access lock file under " + dirPath +
			     "/sys (permissions / disk full?)");
			return InitOutcome::GenericFailure;
		case ibConfigLock::Acquire::MalformedManifest:
			Diag("oes-mcp: lock manifest at " + dirPath +
			     "/sys/.oes.lock is malformed; please delete the file and retry");
			return InitOutcome::GenericFailure;
		}
	}

	try {
		const bool ok = ibApplicationData::CreateFileAppDataEnv(
			ibRunMode::eDESIGNER_MODE,
			wxString::FromUTF8(dirPath.c_str()),
			wxString::FromUTF8(cfg.locale.c_str()));
		if (!ok) {
			const wxString last = ibBackendException::GetLastError();
			std::string msg = "oes-mcp: CreateFileAppDataEnv failed";
			if (!last.IsEmpty()) {
				msg += ": " + std::string(last.utf8_str());
			}
			Diag(msg);
			ibApplicationData::DestroyAppDataEnv();
			// Release the lock we acquired moments ago — appData failed to
			// boot, so we never actually attached to the configuration.
			ibConfigLock::Release(wxString::FromUTF8(dirPath.c_str()), g_lockHolderId);
			g_lockHolderId = 0;
			return InitOutcome::GenericFailure;
		}
	} catch (const ibBackendException& e) {
		Diag(std::string("oes-mcp: backend exception during init: ") +
		     std::string(e.GetErrorDescription().utf8_str()));
		ibApplicationData::DestroyAppDataEnv();
		ibConfigLock::Release(wxString::FromUTF8(dirPath.c_str()), g_lockHolderId);
		g_lockHolderId = 0;
		return InitOutcome::GenericFailure;
	} catch (const std::exception& e) {
		Diag(std::string("oes-mcp: std::exception during init: ") + e.what());
		ibApplicationData::DestroyAppDataEnv();
		ibConfigLock::Release(wxString::FromUTF8(dirPath.c_str()), g_lockHolderId);
		g_lockHolderId = 0;
		return InitOutcome::GenericFailure;
	}

	// MCP: appData lives, but activeMetaData is only attached after a
	// session is opened in designer-mode (the OnFirstConnect listener
	// runs metadataCreate from the registry). For an MCP server we want
	// the metadata tree without spinning a session frame; the cheapest
	// path is to use the existing designer-mode listeners — but those
	// require a session.Open. Since file-mode configs usually don't have
	// sys_user rows, an anonymous Open should land. If the caller
	// supplied creds we honour them.
	//
	// Implementation note: we deliberately do NOT instantiate
	// ibDesignerSession here — that would drag in frontend's GUI hooks.
	// CreateSession (the base type) is enough; OnFirstConnect still
	// fires and metadataCreate populates activeMetaData.
	try {
		ibSession* session = appData->CreateSession();
		if (session == nullptr) {
			Diag("oes-mcp: CreateSession returned nullptr — registry refused");
			ibApplicationData::DestroyAppDataEnv();
			ibConfigLock::Release(wxString::FromUTF8(dirPath.c_str()), g_lockHolderId);
			g_lockHolderId = 0;
			return InitOutcome::GenericFailure;
		}
		const wxString user = wxString::FromUTF8(cfg.ibUser.c_str());
		const wxString pass = wxString::FromUTF8(cfg.ibPassword.c_str());
		if (!session->Open(user, pass)) {
			Diag("oes-mcp: session->Open rejected credentials");
			session->Close();
			ibApplicationData::DestroyAppDataEnv();
			ibConfigLock::Release(wxString::FromUTF8(dirPath.c_str()), g_lockHolderId);
			g_lockHolderId = 0;
			return InitOutcome::GenericFailure;
		}
	} catch (const ibBackendException& e) {
		Diag(std::string("oes-mcp: session open threw: ") +
		     std::string(e.GetErrorDescription().utf8_str()));
		ibApplicationData::DestroyAppDataEnv();
		ibConfigLock::Release(wxString::FromUTF8(dirPath.c_str()), g_lockHolderId);
		g_lockHolderId = 0;
		return InitOutcome::GenericFailure;
	}

	if (activeMetaData == nullptr) {
		Diag("oes-mcp: configuration loaded but activeMetaData is null");
		ibApplicationData::DestroyAppDataEnv();
		ibConfigLock::Release(wxString::FromUTF8(dirPath.c_str()), g_lockHolderId);
		g_lockHolderId = 0;
		return InitOutcome::GenericFailure;
	}

	GrantMcpServerWildcardPolicy();
	g_loadedPath = dirPath;
	g_ready.store(true);

	// MCP auto-snapshot: build the per-config manager. Mode honours the
	// env var; OES_MCP_AUTO_SNAPSHOT=false leaves the manager live but
	// in Disabled mode so List/Load still surface any prior snapshots.
	{
		const char* envVal = std::getenv("OES_MCP_AUTO_SNAPSHOT");
		const auto mode = migration::snapshots::ParseCaptureModeFromEnv(envVal);
		g_snapshotMgr.reset(new migration::snapshots::ibSnapshotManager(
			wxString::FromUTF8(dirPath.c_str()), mode));
	}

	Diag("oes-mcp: configuration ready at " + dirPath +
	     " (shared lock holderId=" + std::to_string(g_lockHolderId) + ")");
	return InitOutcome::Ok;
}

bool InitLegacy(const HeadlessConfig& cfg, DiagSink diagSink)
{
	return Init(cfg, diagSink) == InitOutcome::Ok;
}

void Shutdown()
{
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_ready.load()) {
		// Edge case: a failed Init may have stamped a lock entry then
		// rolled back to GenericFailure; the lock was released in-place
		// in those paths, so g_lockHolderId is already 0. Nothing to do.
		return;
	}

	try {
		// metaBridge holds an undo stack keyed by config epoch; clearing
		// it before tearing appData down stops the dtor of activeMetaData
		// from walking dangling pointers if a recent mutation hasn't been
		// flushed.
		metaBridge::NotifyConfigurationUnload();
		if (appData != nullptr) {
			auto* registry = appData->GetSessionRegistry();
			if (registry != nullptr) registry->Stop();
		}
	} catch (...) {
		// MCP: shutdown is best-effort — never throw from here.
	}

	ibApplicationData::DestroyAppDataEnv();

	// MCP concurrency: release the shared lock LAST. If Release fails the
	// holder entry stays in the manifest with our pid, but our pid is now
	// dead — the next process to call SweepDeadHolders will reap it.
	if (g_lockHolderId != 0 && !g_loadedPath.empty()) {
		const wxString wxDir = wxString::FromUTF8(g_loadedPath.c_str());
		const bool ok = ibConfigLock::Release(wxDir, g_lockHolderId);
		if (!ok) {
			Diag("oes-mcp: lock release failed (entry will be reaped by next "
			     "process via SweepDeadHolders)");
		}
		g_lockHolderId = 0;
	}

	g_ready.store(false);
	g_loading.store(false);
	g_loadedPath.clear();
	g_snapshotMgr.reset();
}

bool IsLockStillHeld()
{
	// MCP concurrency Layer 2: re-probe the manifest. Returns true iff our
	// entry is still listed AND no exclusive holder appeared. We tolerate
	// arbitrary shared holders (other MCP servers, codeRunner, ...) — they
	// are not conflicts.
	if (g_lockHolderId == 0 || g_loadedPath.empty()) {
		// Never acquired (--no-config or pre-Init). Treat as "no lock to
		// worry about" — the per-tool guard short-circuits cleanly.
		return true;
	}
	const wxString wxDir = wxString::FromUTF8(g_loadedPath.c_str());
	if (ibConfigLock::HasLiveExclusiveHolder(wxDir)) return false;
	return ibConfigLock::IsHolderStillLive(wxDir, g_lockHolderId);
}

void NotifyMutation(const std::string& toolName, const std::string& fullName)
{
	// MCP concurrency Layer 3: change broadcast. Designer's notifier polls
	// the marker file via wxTimer; the seq counter dedupes across reads.
	if (g_loadedPath.empty()) return;  // --no-config — nothing to broadcast.
	const wxString wxDir = wxString::FromUTF8(g_loadedPath.c_str());
	ibConfigLock::MutationMarker m;
	m.tool     = toolName;
	m.fullName = fullName;
	m.pluginId = "mcp-server";
	// seq is auto-assigned by WriteMutationMarker (reads prior seq + 1).
	ibConfigLock::WriteMutationMarker(wxDir, m);
}

bool IsReady()
{
	return g_ready.load();
}

bool IsLoading()
{
	return g_loading.load();
}

bool SaveConfiguration(const std::string& path, std::string& errOut)
{
	if (!g_ready.load() || appData == nullptr) {
		errOut = "no configuration loaded";
		return false;
	}
	// MCP: empty path = "save to last loaded directory". For directory-
	// backed configs the live writes are already flushed through sys.fdb,
	// so the only thing SaveDatabase does here is write a redundant zip
	// snapshot. We still expose it because operators rely on the snapshot
	// for version-controlled checkpoints.
	const std::string target = path.empty() ? (g_loadedPath + "/config.OES-DB") : path;
	try {
		const wxString wpath = wxString::FromUTF8(target.c_str());
		const bool ok = appData->SaveDatabase(wpath);
		if (!ok) {
			const wxString last = ibBackendException::GetLastError();
			errOut = last.IsEmpty()
				? std::string("SaveDatabase returned false")
				: std::string(last.utf8_str());
			return false;
		}
		return true;
	} catch (const ibBackendException& e) {
		errOut = std::string(e.GetErrorDescription().utf8_str());
		return false;
	} catch (const std::exception& e) {
		errOut = e.what();
		return false;
	}
}

const std::string& LoadedConfigPath()
{
	return g_loadedPath;
}

migration::snapshots::ibSnapshotManager* GetSnapshotManager()
{
	return g_snapshotMgr.get();
}

} // namespace mcpServer
