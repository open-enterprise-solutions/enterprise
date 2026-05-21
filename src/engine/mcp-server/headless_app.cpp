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

#include <wx/string.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace mcpServer {
namespace {

std::mutex          g_mu;
std::atomic<bool>   g_ready{false};
std::string         g_loadedPath;
DiagSink            g_diag = nullptr;

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

} // namespace

bool Init(const HeadlessConfig& cfg, DiagSink diagSink)
{
	std::lock_guard<std::mutex> lk(g_mu);
	g_diag = diagSink;

	if (g_ready.load()) {
		return true;  // idempotent — second Init after success no-ops.
	}

	if (cfg.configPath.empty()) {
		Diag("oes-mcp: configPath is empty (set argv[1] or OES_CONFIG_PATH)");
		return false;
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
		return false;
	}
	// Strip /sys.fdb suffix when present so the operator can pass either form.
	if (EndsWithIgnoreCase(dirPath, "/sys.fdb") ||
	    EndsWithIgnoreCase(dirPath, "\\sys.fdb")) {
		dirPath = DirOf(dirPath);
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
			return false;
		}
	} catch (const ibBackendException& e) {
		Diag(std::string("oes-mcp: backend exception during init: ") +
		     std::string(e.GetErrorDescription().utf8_str()));
		ibApplicationData::DestroyAppDataEnv();
		return false;
	} catch (const std::exception& e) {
		Diag(std::string("oes-mcp: std::exception during init: ") + e.what());
		ibApplicationData::DestroyAppDataEnv();
		return false;
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
			return false;
		}
		const wxString user = wxString::FromUTF8(cfg.ibUser.c_str());
		const wxString pass = wxString::FromUTF8(cfg.ibPassword.c_str());
		if (!session->Open(user, pass)) {
			Diag("oes-mcp: session->Open rejected credentials");
			session->Close();
			ibApplicationData::DestroyAppDataEnv();
			return false;
		}
	} catch (const ibBackendException& e) {
		Diag(std::string("oes-mcp: session open threw: ") +
		     std::string(e.GetErrorDescription().utf8_str()));
		ibApplicationData::DestroyAppDataEnv();
		return false;
	}

	if (activeMetaData == nullptr) {
		Diag("oes-mcp: configuration loaded but activeMetaData is null");
		ibApplicationData::DestroyAppDataEnv();
		return false;
	}

	GrantMcpServerWildcardPolicy();
	g_loadedPath = dirPath;
	g_ready.store(true);
	Diag("oes-mcp: configuration ready at " + dirPath);
	return true;
}

void Shutdown()
{
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_ready.load()) return;

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
	g_ready.store(false);
	g_loadedPath.clear();
}

bool IsReady()
{
	return g_ready.load();
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

} // namespace mcpServer
