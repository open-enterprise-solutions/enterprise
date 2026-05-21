/////////////////////////////////////////////////////////////////////////////
// Plugin manager implementation — ABI v1 + v2 dual-stack.
/////////////////////////////////////////////////////////////////////////////

#include "pluginManager.h"
#include "pluginValue.h"

#include "backend/appData.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/dir.h>
#include <wx/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "3rdparty/nlohmann/json.hpp"

#include "metaBridge.h"
#include "pluginsConfig.h"
#include "byokEnv.h"

#ifdef __WXMSW__
  #include <windows.h>
#endif

#ifndef _WIN32
  #include <sys/stat.h>   // SEC-P0-2: chmod to lock diag log to 0600
#endif

// SEC-P0-2: persistent diagnostic log relocated to a per-user dir with
// 0600 mode. Was /tmp/oes-diag.log which is world-readable on shared
// hosts and leaked payload slices alongside requestId. Now lives under
// the user data dir (~/Library/Logs/OES/diag.log on macOS, equivalent
// on other platforms). Path resolved once + cached.
static const std::string& DiagLogPath()
{
	static std::string cached;
	static bool        resolved = false;
	if (resolved) return cached;
	resolved = true;
	// SEC-P0-2: wxStandardPaths::Get() routes through wxAppConsole, which
	// dereferences a NULL wxApp pointer when no wxEntryStart has been
	// run (the unit-test harness case). Return an empty path so DiagLog
	// becomes a no-op rather than crashing in libc++ string construction
	// inside wxAppConsoleBase::GetAppName.
	if (wxAppConsole::GetInstance() == nullptr) {
		cached.clear();
		return cached;
	}
	const wxString dir = wxStandardPaths::Get().GetUserDataDir()
	    + wxFILE_SEP_PATH + wxT("Logs");
	if (!wxFileName::DirExists(dir)) {
		wxFileName::Mkdir(dir, 0700, wxPATH_MKDIR_FULL);
	}
	const wxString full = dir + wxFILE_SEP_PATH + wxT("diag.log");
	cached.assign(full.utf8_str().data());
	return cached;
}

static void DiagLog(const char* fmt, ...)
{
	const std::string& path = DiagLogPath();
	if (path.empty()) return;
	FILE* f = std::fopen(path.c_str(), "a");
	if (f == nullptr) return;
#ifndef _WIN32
	// SEC-P0-2: 0600 — owner-only. fopen on POSIX honours umask; chmod
	// pins the mode regardless. Cheap to call every open.
	::chmod(path.c_str(), 0600);
#endif
	std::time_t now = std::time(nullptr);
	char ts[32];
	std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&now));
	std::fprintf(f, "[%s] ", ts);
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(f, fmt, ap);
	va_end(ap);
	std::fputc('\n', f);
	std::fclose(f);
}

namespace {

// Lowest + highest ABI version this build accepts. Plugins built against
// a number outside [kAbiMin, kAbiMax] are skipped. v1..v3 plugins remain
// loadable; ABI v4 added new ibHostAPI slots at the struct tail.
constexpr int kAbiMin = 1;
constexpr int kAbiMax = IB_PLUGIN_ABI_VERSION;  // bumped to 4

bool EnvHasNonEmpty(const ibPluginManager::PluginEnvMap::mapped_type& env,
                    const std::string& key)
{
	auto it = env.find(key);
	return it != env.end() && !it->second.empty();
}

std::string EnvGetFirst(const ibPluginManager::PluginEnvMap::mapped_type& env,
                        std::initializer_list<const char*> keys)
{
	for (const char* key : keys) {
		if (key == nullptr) continue;
		auto it = env.find(key);
		if (it != env.end() && !it->second.empty()) return it->second;
	}
	return {};
}

bool LooksLikeTemplateEndpoint(const std::string& endpoint)
{
	return endpoint.find("/api/oes-mcp/invoke") != std::string::npos ||
	       endpoint.find("oes-mcp") != std::string::npos;
}

void EnsureTemplateProviderEnv(ibPluginManager::PluginEnvMap& envAll)
{
	for (auto& kv : envAll) {
		auto& env = kv.second;
		if (EnvHasNonEmpty(env, "OES_TEMPLATE_PROVIDER")) continue;

		const std::string endpoint = EnvGetFirst(env, { "ENDPOINT" });
		const std::string token = EnvGetFirst(env, { "TOKEN" });
		if (endpoint.empty() || token.empty() ||
		    !LooksLikeTemplateEndpoint(endpoint)) {
			continue;
		}

		env["OES_TEMPLATE_PROVIDER"] = "1";
		if (!EnvHasNonEmpty(env, "OES_TEMPLATE_ENDPOINT")) {
			env["OES_TEMPLATE_ENDPOINT"] = endpoint;
		}
		if (!EnvHasNonEmpty(env, "OES_TEMPLATE_TOKEN")) {
			env["OES_TEMPLATE_TOKEN"] = token;
		}
		const std::string tenant = EnvGetFirst(env, { "TENANT" });
		if (!tenant.empty() && !EnvHasNonEmpty(env, "OES_TEMPLATE_TENANT")) {
			env["OES_TEMPLATE_TENANT"] = tenant;
		}
		const std::string locale = EnvGetFirst(env, { "LOCALE" });
		if (!locale.empty() && !EnvHasNonEmpty(env, "OES_TEMPLATE_LOCALE")) {
			env["OES_TEMPLATE_LOCALE"] = locale;
		}
		if (!EnvHasNonEmpty(env, "OES_TEMPLATE_LIST_TOOL")) {
			env["OES_TEMPLATE_LIST_TOOL"] = "oes_templates_list";
		}
		if (!EnvHasNonEmpty(env, "OES_TEMPLATE_GET_TOOL")) {
			env["OES_TEMPLATE_GET_TOOL"] = "oes_template_get";
		}
		if (!EnvHasNonEmpty(env, "OES_TEMPLATE_CUSTOMIZE_TOOL")) {
			env["OES_TEMPLATE_CUSTOMIZE_TOOL"] = "oes_template_customize";
		}

		if (byokEnv::Save(kv.first, env) == 0) {
			wxLogDebug(wxT("Plugin '%s': enabled template provider env aliases"),
			           wxString::FromUTF8(kv.first.c_str()));
		}
	}
}

// The plugin manager currently being loaded — the host-API trampolines
// dispatch back into it. Set for the duration of LoadAll() AND kept
// pointing at the most recently constructed manager so callbacks fired
// long after init (WebPaneSend during chat, FireEvent during save)
// still resolve. ibApplicationData owns the single instance.
ibPluginManager* g_currentManager = nullptr;

// Arena that backs Make* / Get* calls for the in-flight initialize /
// menu-handler / event-callback invocation. Cleared between invocations.
thread_local ibPluginCallScope* tl_scope = nullptr;

// =========================================================================
// Host API trampolines — every function is a C symbol with no captures,
// so wiring is a plain function-pointer table.
// =========================================================================

int Host_RegisterFunction(const char* name, int paramCount, ibPluginFunctionFn fn)
{
	if (g_currentManager == nullptr) return -1;
	return g_currentManager->HostRegisterFunction(name, paramCount, fn);
}

int Host_RegisterMenuItem(const char* label, ibPluginMenuFn handler)
{
	if (g_currentManager == nullptr) return -1;
	return g_currentManager->HostRegisterMenuItem(label, handler);
}

int Host_Subscribe(const char* event, ibPluginEventFn cb)
{
	if (g_currentManager == nullptr) return -1;
	return g_currentManager->HostSubscribe(event, cb);
}

void Host_Log(const char* msg, int severity)
{
	// Plugins log at any phase; appData may already be NULL during
	// shutdown. Route through wxLog as a stable fallback. Severity 0
	// (info) takes wxLogDebug so plugin-init chatter doesn't surface
	// in the wxLog flush modal; warnings and errors still bubble.
	const wxString text = wxString::FromUTF8(msg ? msg : "");
	switch (severity) {
		case 1:  wxLogWarning("%s", text); break;
		case 2:  wxLogError  ("%s", text); break;
		default: wxLogDebug  ("%s", text); break;
	}
}

ibPluginValue* Host_MakeString(const char* utf8) { return tl_scope ? tl_scope->MakeString(utf8) : nullptr; }
ibPluginValue* Host_MakeNumber(double n)         { return tl_scope ? tl_scope->MakeNumber(n)    : nullptr; }
ibPluginValue* Host_MakeBool  (int b)            { return tl_scope ? tl_scope->MakeBool(b)      : nullptr; }
ibPluginValue* Host_MakeNull  (void)             { return tl_scope ? tl_scope->MakeNull()       : nullptr; }
const char*    Host_GetString (const ibPluginValue* v) { return ibPluginCallScope::GetString(v); }
double         Host_GetNumber (const ibPluginValue* v) { return ibPluginCallScope::GetNumber(v); }
int            Host_GetBool   (const ibPluginValue* v) { return ibPluginCallScope::GetBool(v);   }
int            Host_IsNull    (const ibPluginValue* v) { return ibPluginCallScope::IsNull(v);    }

// --- ABI v4 stubs. Phase 1+ replaces each with a real impl wired to
//     wxWebView (RegisterWebPane / WebPaneSend / WebPaneShow), the
//     Sigma provider registry (Register/Chunk*), and the agent
//     metadata mutation surface (Meta*). Until then every entry
//     returns a recognisable failure so plugins can detect "v4 host
//     present, capability still bootstrapping" cleanly.

int  Host_RegisterWebPane(const char* paneId, const char* title,
                            const char* htmlBundlePath,
                            ibPluginWebMsgFn onMessage,
                            void* userData)
{
	if (g_currentManager == nullptr || paneId == nullptr) return -1;
	return g_currentManager->CallWebPaneRegister(
	    wxString::FromUTF8(paneId),
	    title          ? wxString::FromUTF8(title)          : wxString(),
	    htmlBundlePath ? wxString::FromUTF8(htmlBundlePath) : wxString(),
	    onMessage, userData);
}

int  Host_WebPaneSend(const char* paneId, const char* jsonInline)
{
	if (g_currentManager == nullptr || paneId == nullptr) return -1;
	return g_currentManager->CallWebPaneSend(
	    wxString::FromUTF8(paneId),
	    jsonInline ? wxString::FromUTF8(jsonInline) : wxString());
}

int  Host_WebPaneShow(const char* paneId)
{
	if (g_currentManager == nullptr || paneId == nullptr) return -1;
	return g_currentManager->CallWebPaneShow(wxString::FromUTF8(paneId));
}

int  Host_RegisterAIProvider(const ibPluginAIProvider* provider)
{
	if (g_currentManager == nullptr || provider == nullptr) return -1;
	return g_currentManager->HostRegisterAIProvider(provider);
}

int  Host_AIChunkEmit(const char* requestId, const char* deltaJson)
{
	if (g_currentManager == nullptr) return -1;
	return g_currentManager->HostAIChunkEmit(requestId, deltaJson);
}

int  Host_AIChunkEnd(const char* requestId, const char* metaJson)
{
	if (g_currentManager == nullptr) return -1;
	return g_currentManager->HostAIChunkEnd(requestId, metaJson);
}

int  Host_AIChunkError(const char* requestId, const char* errorJson)
{
	if (g_currentManager == nullptr) return -1;
	return g_currentManager->HostAIChunkError(requestId, errorJson);
}

// Caller-identity tracker — Phase 3.3 will fan this out per
// provider Query / onMessage scope. For Phase 3.2 it stays empty; the
// metaBridge policy gate then refuses (defensive default — never allow
// mutations from an unauthenticated origin).
thread_local std::string tl_currentPluginId;

int  Host_MetaCreate(const char* objectKind, const char* fullName,
                       const char* propertiesJson, char** errorMsg)
{
	return metaBridge::HostMetaCreate(tl_currentPluginId.c_str(),
	                                    objectKind, fullName,
	                                    propertiesJson, errorMsg);
}

int  Host_MetaEdit(const char* fullName, const char* jsonPatch,
                     char** errorMsg)
{
	return metaBridge::HostMetaEdit(tl_currentPluginId.c_str(),
	                                  fullName, jsonPatch, errorMsg);
}

int  Host_MetaDelete(const char* fullName,
                       const char* propertiesJson,
                       char** errorMsg)
{
	// ABI carries propertiesJson so plugin callers can ship the
	// {"force":true} opt-in for irreversible ops. ExtractForceFlag in
	// metaBridge gates the destructive path; nullptr propertiesJson
	// reads as force=false (the safe default).
	return metaBridge::HostMetaDelete(tl_currentPluginId.c_str(),
	                                    fullName, propertiesJson,
	                                    errorMsg);
}

int  Host_MetaQuery(const char* fullName, const char* fieldsFilter,
                      char** jsonOut, char** errorMsg)
{
	return metaBridge::HostMetaQuery(fullName, fieldsFilter, jsonOut, errorMsg);
}

// Cross-DLL safe free for buffers the host malloc'd inside Meta* out
// parameters. The plugin MUST call this rather than free() — on Windows
// with mismatched CRTs the heap pages live in different runtimes and
// plain free() would corrupt the host's heap. macOS / Linux share libc
// and either path is safe; we still mandate this entry point so plugin
// code is portable.
void Host_FreeBuffer(void* buf)
{
	std::free(buf);
}

// Read a key from the calling plugin's BYOK env file. Returns a
// malloc'd UTF-8 string the plugin frees via Host_FreeBuffer, or NULL
// when the key is absent / no plugin scope is active. Plugin identity
// comes from tl_currentPluginId which the host sets at oes_plugin_initialize
// and provider Query call sites — plugins cannot spoof another's id.
char* Host_ReadPluginEnv(const char* key)
{
	if (key == nullptr || *key == '\0') return nullptr;
	if (g_currentManager == nullptr) return nullptr;
	if (tl_currentPluginId.empty()) return nullptr; // no calling-plugin scope
	const std::string v = g_currentManager->ReadPluginEnv(tl_currentPluginId, key);
	if (v.empty()) return nullptr;
	const size_t n = v.size() + 1;
	void* mem = std::malloc(n);
	if (mem == nullptr) return nullptr;
	std::memcpy(mem, v.c_str(), n);
	return static_cast<char*>(mem);
}

// The single ibHostAPI instance handed to every v2+ plugin. Const after
// initialization; plugins never mutate it. Field order MUST match the
// pluginApi.h struct declaration — appends only at the tail across ABI
// bumps. v3 plugins see only the first 12 entries; v4 plugins see
// everything up to MetaQuery.
const ibHostAPI g_hostAPI = {
	&Host_RegisterFunction,
	&Host_RegisterMenuItem,
	&Host_Subscribe,
	&Host_Log,
	&Host_MakeString,
	&Host_MakeNumber,
	&Host_MakeBool,
	&Host_MakeNull,
	&Host_GetString,
	&Host_GetNumber,
	&Host_GetBool,
	&Host_IsNull,
	// --- ABI v4 ---
	&Host_RegisterWebPane,
	&Host_WebPaneSend,
	&Host_WebPaneShow,
	&Host_RegisterAIProvider,
	&Host_AIChunkEmit,
	&Host_AIChunkEnd,
	&Host_AIChunkError,
	&Host_MetaCreate,
	&Host_MetaEdit,
	&Host_MetaDelete,
	&Host_MetaQuery,
	&Host_FreeBuffer,
	&Host_ReadPluginEnv,
};

// RAII: on Windows, silence the OS-level "missing DLL" modal that
// LoadLibrary otherwise pops up for every broken plugin dependency.
// Other platforms are no-ops.
struct ScopedSilenceLoadErrors {
#ifdef __WXMSW__
	UINT m_prev;
	ScopedSilenceLoadErrors()  { m_prev = ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX); }
	~ScopedSilenceLoadErrors() { ::SetErrorMode(m_prev); }
#else
	ScopedSilenceLoadErrors()  = default;
	~ScopedSilenceLoadErrors() = default;
#endif
};

bool IsValidIdentifier(const char* name)
{
	if (name == nullptr || *name == '\0') return false;
	auto isIdStart = [](char c) {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
	};
	auto isIdCont = [&](char c) {
		return isIdStart(c) || (c >= '0' && c <= '9');
	};
	if (!isIdStart(name[0])) return false;
	for (const char* p = name + 1; *p; ++p)
		if (!isIdCont(*p)) return false;
	return true;
}

} // namespace

// =========================================================================
// Public manager surface
// =========================================================================

wxString ibPluginManager::GetPluginsDir()
{
	// OES_PLUGINS_DIR overrides the default. Lets integration tests
	// (and packaged tarball installs) point at an arbitrary directory
	// without having to copy .bundle files next to the test binary.
	const wxString override = wxGetenv(wxT("OES_PLUGINS_DIR"));
	if (!override.IsEmpty()) {
		return override;
	}
	wxFileName fn(wxStandardPaths::Get().GetExecutablePath());
	fn.AppendDir("plugins");
	fn.SetFullName(wxEmptyString);
	return fn.GetPath();
}

// Forward declarations for helpers defined later in the file. UnloadAll
// is the last LoadAll-adjacent method and we want it to call into the
// shim-worker drain + env-rollback paths without re-ordering the bigger
// helpers further down.
namespace { void JoinAndDrainShimWorkers(); }

// (SEC-P1-8: real impl of UnsetInjectedEnvKeys lives further down, see
// the helper near JoinAndDrainShimWorkers. Stub removed — was a duplicate
// inserted by an aborted security-fix pass.)

size_t ibPluginManager::LoadAll()
{
	DiagLog("LoadAll: enter");

	// SEC-CR-P2: drain previous load FIRST. Setting g_currentManager
	// before UnloadAll let shutdown callbacks on worker threads see
	// an instance whose tables UnloadAll was busy emptying.
	UnloadAll();

	// Make the host-trampolines see this instance for the entire app
	// lifetime, not just the duration of LoadAll. WebPaneSend etc. fire
	// long after init returns.
	g_currentManager = this;

	// Sandbox / kill-switch. OES_PLUGIN_SANDBOX=1 in the environment
	// skips plugin discovery entirely — useful for incident response
	// (a misbehaving plugin won't even load on the next launch) and
	// for the read-only "viewer" deployments that should never run
	// third-party code.
	const wxString sandbox = wxGetenv(wxT("OES_PLUGIN_SANDBOX"));
	if (sandbox == wxT("1") || sandbox.Lower() == wxT("true")) {
		wxLogDebug(wxT("Plugin sandbox active (OES_PLUGIN_SANDBOX=1) — skipping plugin discovery."));
		return 0;
	}

	// Note: g_currentManager remains set for the lifetime of *this*; the
	// dtor clears it. ABI v4 host callbacks (WebPaneSend, AIChunkEmit,
	// MetaCreate, etc.) fire from chat / worker threads well after
	// LoadAll returns — a scope-guard that reset to nullptr here would
	// break every post-init host call.

	const wxString dir = GetPluginsDir();
	if (!wxDirExists(dir))
		return 0;

	// Load persistent per-plugin overrides (enabled/disabled, mutation
	// policy persisted across sessions). Apply policies before the DLL
	// scan so plugins that call SetMutationPolicy from oes_plugin_initialize
	// see them already in place.
	const pluginsConfig::Snapshot snap = pluginsConfig::Load();
	pluginsConfig::Apply(snap, *this);

	// Phase 4.2 — load BYOK env files into the manager cache. Plugins
	// read their tokens via the ABI v4 ReadPluginEnv trampoline; the
	// host enforces caller-pluginId isolation via the tl_currentPluginId
	// scope set around each oes_plugin_initialize call below.
	m_pluginEnv = byokEnv::LoadAll();
	EnsureTemplateProviderEnv(m_pluginEnv);

	const wxString pattern = wxT("*") + wxDynamicLibrary::GetDllExt(wxDL_MODULE);

	wxArrayString files;
	wxDir::GetAllFiles(dir, &files, pattern, wxDIR_FILES);

	ScopedSilenceLoadErrors silence;

	for (const wxString& path : files) {

		auto lib = std::make_unique<wxDynamicLibrary>();
		if (!lib->Load(path, wxDL_DEFAULT | wxDL_QUIET)) {
			wxLogMessage(wxT("Plugin '%s' failed to load (missing dependency or wrong arch)"),
			             path);
			continue;
		}

		ibPluginInfoFn info_fn = nullptr;
		{
			wxLogNull noLog;
			info_fn = reinterpret_cast<ibPluginInfoFn>(
				lib->GetSymbol(wxT("oes_plugin_info")));
		}
		if (info_fn == nullptr)
			continue; // not one of ours

		const ibPluginInfo* info = info_fn();
		if (info == nullptr || info->abi_version < kAbiMin || info->abi_version > kAbiMax) {
			wxLogMessage(wxT("Plugin '%s' rejected: ABI mismatch (got %d, host supports %d..%d)"),
			             path, info ? info->abi_version : -1, kAbiMin, kAbiMax);
			continue;
		}

		// Persistent enable/disable from plugins.json5. Disabled plugins
		// stay on disk but never run their oes_plugin_initialize. The
		// Tools → Plugins dialog flips the toggle without ripping files
		// off the filesystem.
		const std::string pluginIdNarrow(info->name ? info->name : "");
		if (!pluginIdNarrow.empty() &&
		    !pluginsConfig::IsEnabled(snap, pluginIdNarrow)) {
			// Informational — wxLogDebug stays out of the
			// wxLog::FlushActive modal that appears at first event-loop
			// tick. User-visible disable status lives in the Tools →
			// Plugins dialog where it belongs.
			wxLogDebug(wxT("Plugin '%s' disabled in plugins.json5 — skipping"),
			           wxString::FromUTF8(pluginIdNarrow));
			continue;
		}

		ibPluginInitializeFn init_fn = nullptr;
		{
			wxLogNull noLog;
			init_fn = reinterpret_cast<ibPluginInitializeFn>(
				lib->GetSymbol(wxT("oes_plugin_initialize")));
		}
		if (init_fn != nullptr) {
			// v1 plugins expect a void* hostContext (NULL); v2 plugins
			// expect the ibHostAPI table. Both signatures are pointer-
			// width parameters so the same function pointer storage
			// works — gate on abi_version which we already validated.
			ibPluginCallScope scope;
			ibPluginCallScope* prev = tl_scope;
			tl_scope = &scope;
			// Phase 4.2 scope — push the calling plugin's id so its
			// ReadPluginEnv calls during init resolve to its own .env.
			// Pop on every return path so the next plugin's init sees
			// a clean scope.
			const std::string prevPluginId = tl_currentPluginId;
			tl_currentPluginId = pluginIdNarrow;

			// Phase 7.5 universal env injection. ABI v4 plugins read
			// their tokens via Host_ReadPluginEnv (which honours
			// caller-id isolation). v3 plugins (pre-trampoline) call
			// getenv() directly and expect their provider-specific keys.
			// Export this plugin's BYOK .env values into the process env
			// just before init_fn so v3 plugins see them. v4 plugins
			// also benefit (a plugin that registers shared keys via
			// ReadPluginEnv AND uses getenv for backwards-compat will
			// continue to work). The values leak into the process env
			// for the rest of the Designer session, which is acceptable
			// in single-tenant Designer; UnloadAll does not unset them
			// because v3 plugins can't be partially unloaded anyway.
			{
				auto envIt = m_pluginEnv.find(pluginIdNarrow);
				if (envIt != m_pluginEnv.end()) {
					// SEC-P1-8 / P2-2: remember each key's prior state BEFORE
					// overwriting so UnloadAll restores it instead of wiping
					// a value the user set in their shell env.
					auto& tracked = m_injectedEnvKeys[pluginIdNarrow];
					tracked.clear();
					tracked.reserve(envIt->second.size());
					for (const auto& kv : envIt->second) {
						InjectedEnv rec;
						rec.key = kv.first;
						const char* priorRaw = std::getenv(kv.first.c_str());
						rec.wasSet = (priorRaw != nullptr);
						if (rec.wasSet) rec.prior = priorRaw;
#if defined(_WIN32)
						_putenv_s(kv.first.c_str(), kv.second.c_str());
#else
						setenv(kv.first.c_str(), kv.second.c_str(), /*overwrite=*/1);
#endif
						tracked.push_back(std::move(rec));
					}
					wxLogDebug(wxT("Plugin '%s': injected %zu env keys into process env"),
					           wxString::FromUTF8(pluginIdNarrow),
					           envIt->second.size());
				}
			}

			const ibHostAPI* host =
			    (info->abi_version >= 2) ? &g_hostAPI : nullptr;
			const int rc = init_fn(host);
			tl_currentPluginId = prevPluginId;
			tl_scope = prev;

			// SEC-CR-P1-1: scope env injection to JUST this plugin's
			// init_fn call. Without per-init restore, plugin B's
			// oes_plugin_initialize could call getenv() and observe
			// plugin A's bearer tokens.
			{
				auto envIt = m_injectedEnvKeys.find(pluginIdNarrow);
				if (envIt != m_injectedEnvKeys.end()) {
					for (const auto& r : envIt->second) {
						if (r.wasSet) {
#if defined(_WIN32)
							_putenv_s(r.key.c_str(), r.prior.c_str());
#else
							setenv(r.key.c_str(), r.prior.c_str(), /*overwrite=*/1);
#endif
						} else {
#if defined(_WIN32)
							_putenv_s(r.key.c_str(), "");
#else
							unsetenv(r.key.c_str());
#endif
						}
					}
				}
			}

			if (rc != 0) {
				wxLogDebug("Plugin '%s' initialize() failed (rc=%d)",
				           path, rc);
				continue;
			}
		}

		LoadedPlugin p;
		p.m_path = path;
		p.m_lib  = std::move(lib);
		p.m_info = info;
		{
			wxLogNull noLog;
			p.m_shutdown = reinterpret_cast<ibPluginShutdownFn>(
				p.m_lib->GetSymbol(wxT("oes_plugin_shutdown")));
		}
		wxLogDebug("Loaded plugin: %s %s (ABI v%d)",
			info->name ? info->name : "<unnamed>",
			info->version ? info->version : "",
			info->abi_version);
		DiagLog("LoadAll: loaded '%s' v%s ABI%d, %zu functions registered so far",
		        info->name ? info->name : "?",
		        info->version ? info->version : "?",
		        info->abi_version, m_functions.size());

		m_plugins.push_back(std::move(p));
	}

	DiagLog("LoadAll: exit, total plugins=%zu functions=%zu providers=%zu",
	        m_plugins.size(), m_functions.size(), m_aiProviders.size());
	return m_plugins.size();
}

void ibPluginManager::UnloadAll()
{
	// SEC-P0-3: drain shim worker threads BEFORE clearing m_functions /
	// dropping plugin DLLs. Workers capture RegisteredFunction by value;
	// fn.m_fn points into the DLL we're about to unload. Joining first
	// guarantees no in-flight call can dereference a freed module.
	JoinAndDrainShimWorkers();

	// SEC-P1-8: roll back per-plugin env injections so tokens don't leak
	// to forked subprocesses or crash dumps across config reloads.
	UnsetInjectedEnvKeys();

	// Subscriber + function tables come down first — once the DLLs are
	// gone any held function pointer would dangle.
	m_subscribers.clear();
	m_functions.clear();
	m_menuItems.clear();
	m_aiProviders.clear();
	m_defaultAIPaneId.Clear();
	m_pendingWebPaneRegs.clear();
	m_paneIdToPluginId.clear();   // SEC-P1-9
	m_pluginEnv.clear();

	// Drain the agent undo stack BEFORE the configuration tree is freed.
	// Without this every undo lambda holds dangling pointers; the
	// epoch-gate inside metaBridge catches them but the shared_ptr-owned
	// MetaDelete victims would leak otherwise. NotifyConfigurationUnload
	// advances the epoch + clears the stack, firing custom deleters.
	metaBridge::NotifyConfigurationUnload();

	// Session-scoped policy decisions evaporate on UnloadAll. AllowAlways
	// entries also drop here — Designer reloads them from options.xml on
	// next LoadAll via SetMutationPolicy (Phase 4). For Phase 3.2 tests
	// the table is just fully cleared.
	m_mutationPolicy.clear();

	for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
		if (it->m_shutdown) {
			try { it->m_shutdown(); } catch (...) { /* plugin bug */ }
		}
	}
	m_plugins.clear();
}

void ibPluginManager::FireEvent(const wxString& name, ibPluginValue* payload)
{
	const std::string key(name.utf8_str());
	auto it = m_subscribers.find(key);
	if (it == m_subscribers.end()) return;

	ibPluginCallScope scope;
	ibPluginCallScope* prev = tl_scope;
	tl_scope = &scope;
	for (auto cb : it->second) {
		try { cb(key.c_str(), payload); }
		catch (...) { /* plugin bug — swallow */ }
	}
	tl_scope = prev;
}

int ibPluginManager::HostRegisterFunction(const char* name, int paramCount, ibPluginFunctionFn fn)
{
	if (fn == nullptr || !IsValidIdentifier(name)) return -1;
	// SEC-P1-9: capture the registering plugin's id from the init-thread
	// scope so CallFunction can push it on every dispatch.
	const std::string callerPid = tl_currentPluginId;
	// Last writer wins — overwrite an existing binding with the same name.
	const std::string key(name);
	for (auto& f : m_functions) {
		if (f.m_name == key) {
			f.m_paramCount = paramCount;
			f.m_fn         = fn;
			f.m_pluginId   = callerPid;
			return 0;
		}
	}
	m_functions.push_back({ key, paramCount, fn, callerPid });
	return 0;
}

int ibPluginManager::HostRegisterMenuItem(const char* label, ibPluginMenuFn handler)
{
	if (handler == nullptr || label == nullptr || *label == '\0') return -1;
	m_menuItems.push_back({ std::string(label), handler });
	return 0;
}

int ibPluginManager::HostSubscribe(const char* event, ibPluginEventFn cb)
{
	if (cb == nullptr || event == nullptr || *event == '\0') return -1;
	m_subscribers[std::string(event)].push_back(cb);
	return 0;
}

bool ibPluginManager::CallFunction(const RegisteredFunction& fn,
                                     ibValue& retOut,
                                     ibValue** paParams,
                                     long lSizeArray)
{
	if (fn.m_fn == nullptr) return false;

	ibPluginCallScope scope;
	ibPluginCallScope* prev = tl_scope;
	tl_scope = &scope;

	// SEC-P1-9: push the registering plugin's id so any host trampoline
	// (Meta*, ReadPluginEnv) called from inside fn.m_fn resolves caller
	// identity correctly — even when CallFunction runs on a worker thread
	// where tl_currentPluginId would otherwise be empty.
	const std::string prevPid = tl_currentPluginId;
	tl_currentPluginId = fn.m_pluginId;

	std::vector<ibPluginValue*> args;
	args.reserve(static_cast<size_t>(lSizeArray));
	for (long i = 0; i < lSizeArray; ++i) {
		if (paParams && paParams[i]) {
			args.push_back(scope.AdoptFromValue(*paParams[i]));
		} else {
			args.push_back(scope.MakeNull());
		}
	}

	ibPluginValue* ret = nullptr;
	int rc = 1;
	try {
		rc = fn.m_fn(args.empty() ? nullptr : args.data(),
		             static_cast<int>(lSizeArray), &ret);
	} catch (...) {
		// Plugin bug — script call site sees a script-side failure
		// rather than the host tearing down.
		rc = 1;
	}

	if (rc == 0) {
		if (ret != nullptr) {
			retOut = ret->m_value;  // copy before scope dtor releases ret
		} else {
			retOut = ibValue();
		}
	}

	tl_scope = prev;
	// SEC-P1-9: restore prior caller-pid scope.
	tl_currentPluginId = prevPid;
	return rc == 0;
}

void ibPluginManager::SetWebPaneCallbacks(WebPaneRegisterFn reg,
                                            WebPaneSendFn    send,
                                            WebPaneShowFn    show)
{
	m_webPaneRegister = std::move(reg);
	m_webPaneSend     = std::move(send);
	m_webPaneShow     = std::move(show);
	DiagLog("SetWebPaneCallbacks installed");
}

int ibPluginManager::CallWebPaneRegister(const wxString& paneId,
                                           const wxString& title,
                                           const wxString& htmlBundlePath,
                                           ibPluginWebMsgFn cb,
                                           void* userData)
{
	// Contract: RegisterWebPane must be called from the main (UI) thread.
	// Documented in pluginApi.h. Reasons: (a) the buffer m_pendingWebPaneRegs
	// is touched without locks and racing it would corrupt the vector;
	// (b) the WebView pane is a wxWindow and wxWidgets construction is
	// not thread-safe. Plugins that need worker-thread mutations must
	// post back via WebPaneSend (which IS thread-safe).
	if (!wxIsMainThread()) {
		wxLogWarning(wxT("ibPluginManager::CallWebPaneRegister called off main thread for '%s' — refused"),
		             paneId);
		return -1;
	}
	// SEC-P1-9: snapshot the registering plugin's id (set during init_fn).
	// Stashed alongside paneId so inbound pane messages can push the
	// scope around the dispatched onMessage call.
	const std::string registrantPid = tl_currentPluginId;
	if (!m_webPaneRegister) {
		// Designer hasn't installed callbacks yet — buffer for replay.
		// Return success so the plugin doesn't treat early-init as
		// failure; ReplayPendingWebPaneRegistrations will surface the
		// real result later once the frame wires its lambdas. The
		// default-pane slot is claimed inside the replay loop AFTER
		// the entry actually registers successfully — a buffered reg
		// that the frame later rejects must not shadow valid panes.
		m_pendingWebPaneRegs.push_back({paneId, title, htmlBundlePath, cb, userData, registrantPid});
		return 0;
	}
	const int rc = m_webPaneRegister(paneId, title, htmlBundlePath, cb, userData);
	if (rc == 0 && m_defaultAIPaneId.IsEmpty() && !paneId.IsEmpty()) {
		// Record the first SUCCESSFUL pane id as the Phase 2 default AI
		// chunk target. Phase 4 Settings UI lets the user re-pick. A
		// rejected registration (duplicate name, AUI refusal, etc.) must
		// NOT claim the slot — otherwise later real panes can't route.
		m_defaultAIPaneId = paneId;
	}
	if (rc == 0 && !paneId.IsEmpty()) {
		// SEC-P1-9: record paneId → pluginId so dispatchers can scope
		// tl_currentPluginId on inbound messages.
		m_paneIdToPluginId[std::string(paneId.utf8_str())] = registrantPid;
	}
	return rc;
}

std::string ibPluginManager::GetPluginIdForPane(const wxString& paneId) const
{
	auto it = m_paneIdToPluginId.find(std::string(paneId.utf8_str()));
	if (it == m_paneIdToPluginId.end()) return std::string();
	return it->second;
}

std::string ibPluginManager::PushCallerPluginId(const std::string& pluginId)
{
	std::string prev = tl_currentPluginId;
	tl_currentPluginId = pluginId;
	return prev;
}

void ibPluginManager::PopCallerPluginId(const std::string& restore)
{
	tl_currentPluginId = restore;
}

ibPluginManager::~ibPluginManager()
{
	// SEC-CR-P2: clear the trampoline pointer BEFORE UnloadAll so plugin
	// shutdown callbacks firing on worker threads can't dispatch into a
	// half-destroyed manager (m_functions cleared, m_subscribers gone).
	if (g_currentManager == this) g_currentManager = nullptr;
	UnloadAll();
}

// =========================================================================
// Mutation policy (Phase 3.2)
// =========================================================================
namespace {

std::string PolicyKey(const wxString& pluginId, const wxString& opName)
{
	std::string k(pluginId.utf8_str());
	k += "::";
	k += std::string(opName.utf8_str());
	return k;
}

const char* PolicyName(ibPluginManager::MutationPolicy p)
{
	switch (p) {
	case ibPluginManager::MutationPolicy::Ask:          return "Ask";
	case ibPluginManager::MutationPolicy::AllowSession: return "AllowSession";
	case ibPluginManager::MutationPolicy::AllowAlways:  return "AllowAlways";
	case ibPluginManager::MutationPolicy::Deny:         return "Deny";
	}
	return "?";
}

} // namespace

void ibPluginManager::SetMutationPolicy(const wxString& pluginId,
                                          const wxString& opName,
                                          MutationPolicy policy)
{
	m_mutationPolicy[PolicyKey(pluginId, opName)] = policy;
}

ibPluginManager::MutationPolicy
ibPluginManager::GetMutationPolicy(const wxString& pluginId,
                                     const wxString& opName) const
{
	auto it = m_mutationPolicy.find(PolicyKey(pluginId, opName));
	if (it != m_mutationPolicy.end()) return it->second;
	// Defaults: read-only Auto; everything else Ask.
	if (opName == wxT("meta.query")) return MutationPolicy::AllowAlways;
	return MutationPolicy::Ask;
}

std::string ibPluginManager::ReadPluginEnv(const std::string& pluginId,
                                              const std::string& key) const
{
	auto pit = m_pluginEnv.find(pluginId);
	if (pit == m_pluginEnv.end()) return std::string();
	auto kit = pit->second.find(key);
	if (kit == pit->second.end()) return std::string();
	return kit->second;
}

void ibPluginManager::UnsetInjectedEnvKeys()
{
	// SEC-P1-8 / P2-2: restore each env entry to its pre-injection state.
	// Bearer tokens shouldn't outlive the plugin's load cycle, but if the
	// user had set TOKEN themselves before launching Designer we MUST put
	// the original value back — unconditional unsetenv() wiped it.
	for (const auto& [pluginId, recs] : m_injectedEnvKeys) {
		for (const auto& r : recs) {
			if (r.wasSet) {
#if defined(_WIN32)
				_putenv_s(r.key.c_str(), r.prior.c_str());
#else
				setenv(r.key.c_str(), r.prior.c_str(), /*overwrite=*/1);
#endif
			} else {
#if defined(_WIN32)
				_putenv_s(r.key.c_str(), "");
#else
				unsetenv(r.key.c_str());
#endif
			}
		}
	}
	m_injectedEnvKeys.clear();
}

bool ibPluginManager::CheckMutationAllowed(const wxString& pluginId,
                                             const wxString& opName)
{
	// Wildcard tier first: a (pluginId, "*") entry overrides per-op
	// defaults so the user can "trust all from this plugin" once and
	// stop being re-prompted. Mirrors the "session allowlist by tool
	// family" pattern modern IDE assistants ship.
	auto it = m_mutationPolicy.find(PolicyKey(pluginId, wxT("*")));
	if (it != m_mutationPolicy.end()) {
		const MutationPolicy w = it->second;
		if (w == MutationPolicy::AllowSession || w == MutationPolicy::AllowAlways) {
			wxLogDebug(wxT("[plugin-policy] %s::%s -> ALLOW (wildcard %s)"),
			             pluginId, opName, wxString::FromUTF8(PolicyName(w)));
			return true;
		}
		if (w == MutationPolicy::Deny) {
			wxLogDebug(wxT("[plugin-policy] %s::%s -> DENY (wildcard Deny)"),
			             pluginId, opName);
			return false;
		}
		// Wildcard Ask falls through to the per-op check.
	}

	const MutationPolicy p = GetMutationPolicy(pluginId, opName);
	const bool allow = (p == MutationPolicy::AllowSession ||
	                    p == MutationPolicy::AllowAlways);
	wxLogDebug(wxT("[plugin-policy] %s::%s -> %s (%s)"),
	             pluginId, opName, allow ? wxT("ALLOW") : wxT("DENY"),
	             wxString::FromUTF8(PolicyName(p)));
	return allow;
}

void ibPluginManager::ReplayPendingWebPaneRegistrations()
{
	if (!m_webPaneRegister) return;
	std::vector<PendingWebPaneReg> pending;
	pending.swap(m_pendingWebPaneRegs);
	for (const auto& r : pending) {
		const int rc = m_webPaneRegister(r.paneId, r.title, r.htmlBundlePath,
		                                   r.onMessage, r.userData);
		if (rc != 0) {
			wxLogWarning(wxT("ibPluginManager: deferred RegisterWebPane('%s') returned %d"),
			             r.paneId, rc);
			continue;
		}
		// Same success-only default-pane assignment as the direct path
		// in CallWebPaneRegister. Without this guard, a buffered entry
		// the frame later rejects could permanently shadow valid panes.
		if (m_defaultAIPaneId.IsEmpty() && !r.paneId.IsEmpty()) {
			m_defaultAIPaneId = r.paneId;
		}
		// SEC-P1-9: deferred regs land here on the same code path; mirror
		// the paneId→pluginId stash so dispatch can scope tl_currentPluginId.
		if (!r.paneId.IsEmpty()) {
			m_paneIdToPluginId[std::string(r.paneId.utf8_str())] = r.pluginId;
		}
	}
}

// =========================================================================
// Legacy LLMQuery shim (Phase 7.5)
//
// ABI v3 plugins (pre-WebPane era) expose chat capability through the
// script-callable `LLMQuery(prompt, locale)` function. They cannot
// register an AI provider or WebView pane because those host trampolines
// only exist in ABI v4. The shim closes that gap: it inspects the
// registered-function table, and when LLMQuery is present and no v4
// chat provider has claimed the slot, it stands up a synthetic AI
// provider + WebView pane that:
//
//   - speaks the same chat.send / editor.skill envelope contract as
//     sample.html and aiBridge
//   - dispatches each user prompt through CallFunction(LLMQuery, ...)
//     on the main thread (the v3 plugin owns its own transport)
//   - emits a single chat.delta with the full response, then chat.end
//
// This is a one-way bridge: streaming chat.delta chunks aren't possible
// because LLMQuery returns the final string in one shot. For the v3
// ecosystem the UX trade-off is acceptable — Anvil typical first-
// token-to-final is ~2-8s, the pane shows the answer in one block.
// =========================================================================
namespace {

// Single-instance shim state. Holds the LLMQuery function pointer and
// the pane id we registered so OnPaneMessage can route back. Mutex
// guards concurrent re-registration during plugin reload sequences.
struct LegacyShimState {
	ibPluginManager*                                  manager     = nullptr;
	ibPluginManager::RegisteredFunction               llmFn;
	std::string                                       paneId;
	std::mutex                                        mu;
};

LegacyShimState& Shim()
{
	static LegacyShimState s;
	return s;
}

// SEC-P0-3: track every spawned shim worker so UnloadAll / shim reset
// can quiesce them before the manager pointer (or LLMQuery fn pointer)
// dies. Workers poll g_shimShutdown between emits + skip remaining work
// when set. Mirrors the pattern aiBridge uses for g_workers + g_shuttingDown.
std::mutex                g_shimWorkersMu;
std::vector<std::thread>  g_shimWorkers;
std::atomic<bool>         g_shimShutdown{false};

// Join + drain every spawned shim worker. Called from ibPluginManager
// when the shim is about to be replaced (re-Scan) or torn down (UnloadAll).
// Must NOT be called from a worker thread itself — would self-deadlock.
void JoinAndDrainShimWorkers()
{
	g_shimShutdown.store(true);
	std::vector<std::thread> drain;
	{
		std::lock_guard<std::mutex> lk(g_shimWorkersMu);
		drain.swap(g_shimWorkers);
	}
	for (auto& t : drain) {
		if (t.joinable()) t.join();
	}
	g_shimShutdown.store(false);
}

// Per-request cancellation tokens. Stop Generation: the pane fires
// `agent.cancel` with the original requestId; the worker thread polls
// the atomic between chunk emits and bails out early. shared_ptr lets
// the worker capture the token by value — the map entry can be erased
// the moment the worker's still-alive copy is the only owner, no
// dangling reference.
//
// Synchronous LLMQuery cannot itself be interrupted (single C call,
// blocks on httplib socket), so cancellation only stops the chunked
// emit phase that follows. That's still the user-visible "Stop" —
// chunks stop rendering immediately and chat.end carries cancelled:true.
std::mutex                                                          g_cancelMu;
std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> g_cancelTokens;

std::shared_ptr<std::atomic<bool>> AllocCancelToken(const std::string& requestId)
{
	auto tok = std::make_shared<std::atomic<bool>>(false);
	std::lock_guard<std::mutex> lk(g_cancelMu);
	g_cancelTokens[requestId] = tok;
	return tok;
}

void EraseCancelToken(const std::string& requestId)
{
	std::lock_guard<std::mutex> lk(g_cancelMu);
	g_cancelTokens.erase(requestId);
}

bool TripCancelToken(const std::string& requestId)
{
	std::lock_guard<std::mutex> lk(g_cancelMu);
	auto it = g_cancelTokens.find(requestId);
	if (it == g_cancelTokens.end()) return false;
	it->second->store(true);
	return true;
}

// (DiagLog defined at file scope above — see top of file)

// Escape a UTF-8 string for embedding inside a JSON string literal.
// Same shape the editor.skill encoder produces — keeps the parser on
// the sample.html side happy without pulling nlohmann into the wire-up.
std::string JsonEscape(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (char c : s) {
		const auto u = static_cast<unsigned char>(c);
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '"':  out += "\\\""; break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:
			if (u < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", u);
				out += buf;
			} else {
				out += c;
			}
		}
	}
	return out;
}

// Build the user-facing prompt from an editor.skill envelope. Mirrors
// the right-click submenu labels in codeEditor.cpp so the v3 plugin
// receives a self-explanatory natural-language request instead of a
// raw opcode + code blob.
std::string BuildEditorSkillPrompt(const std::string& op,
                                     const std::string& language,
                                     const std::string& code)
{
	std::string label;
	if      (op == "explain") label = "Explain the following code:";
	else if (op == "review")  label = "Review the following code for bugs and improvements:";
	else if (op == "fix")     label = "Fix the issues in the following code and return the corrected version:";
	else if (op == "doc")     label = "Write a documentation comment for the following code:";
	else if (op == "send")    label = "Discuss the following code with me:";
	else                       label = "Process the following code (" + op + "):";

	std::string out = label;
	out += "\n\n```";
	out += language.empty() ? "ces" : language;
	out += "\n";
	out += code;
	out += "\n```";
	return out;
}

// Pane message dispatcher. Parses inbound envelopes on the caller
// thread (UI), then offloads the blocking LLMQuery call to a detached
// std::thread so the Designer stays responsive while the provider waits
// for a reply. Emits chat.delta in small chunks for a typewriter
// effect — gives the user immediate visual feedback that the request
// is alive even while the same TCP round-trip is in flight.
//
// Threading note: ibPluginManager::CallWebPaneSend is thread-safe — it
// routes through ibPluginWebPane::PushMessage which queues a wxThreadEvent
// when called off the main thread. So the worker can emit chat.delta
// directly without an extra wxTheApp->CallAfter step. CallFunction is
// also safe in a worker because the tl_scope inside the manager is
// thread_local; each worker gets a fresh scope.
void LegacyShimOnPaneMessage(const char* paneId, const char* jsonInline, void* /*userData*/)
{
	// SEC-P0-2: never log the inbound payload — it carries the raw user
	// prompt and (in editor.skill envelopes) source code. kind+requestId
	// suffice for forensics; parse them out below before logging.
	DiagLog("OnPaneMessage: pane='%s' bytes=%zu",
	        paneId ? paneId : "?",
	        jsonInline ? std::strlen(jsonInline) : 0);
	auto& shim = Shim();
	if (shim.manager == nullptr || shim.llmFn.m_fn == nullptr) {
		DiagLog("  FAIL: shim not initialised (manager=%p fn=%p)",
		        (void*)shim.manager, (void*)shim.llmFn.m_fn);
		wxLogMessage(wxT("LegacyLLMShim: OnPaneMessage fired but shim not initialised"));
		return;
	}
	if (paneId == nullptr || jsonInline == nullptr) return;

	nlohmann::json env;
	try {
		env = nlohmann::json::parse(jsonInline);
	} catch (const std::exception& e) {
		DiagLog("  FAIL: malformed envelope: %s", e.what());
		return;
	}

	const std::string kind = env.value("kind", "");

	// Stop Generation: pane sends agent.cancel with the original
	// requestId. Trip the per-request atomic; the detached worker polls
	// it between chunk emits and bails. Handled before the chat.send /
	// editor.skill gate so a cancel of a still-pending request doesn't
	// trigger the "ignoring kind=" diag noise.
	if (kind == "agent.cancel") {
		const std::string rid = env.value("requestId", "");
		const bool tripped = TripCancelToken(rid);
		DiagLog("agent.cancel received for rid=%s tripped=%d", rid.c_str(), tripped ? 1 : 0);
		return;
	}

	if (kind != "chat.send" && kind != "editor.skill") {
		DiagLog("  ignoring kind='%s'", kind.c_str());
		return;
	}

	const std::string requestId = env.value("requestId", "");

	// Build the prompt + locale on the caller thread, snapshot to
	// owned std::string so the worker captures by value (no string
	// view lifetimes crossing thread boundary).
	std::string prompt;
	if (kind == "chat.send") {
		prompt = env.value("text", env.value("prompt", std::string{}));
	} else { // editor.skill
		const std::string op       = env.value("op",       std::string{});
		const std::string language = env.value("language", std::string{});
		const std::string code     = env.value("code",     std::string{});
		prompt = BuildEditorSkillPrompt(op, language, code);
	}
	if (prompt.empty()) return;

	std::string locale = env.value("locale", std::string{});
	if (locale.empty()) locale = "uk-UA";

	DiagLog("  spawning worker: kind=%s rid=%s locale=%s prompt_size=%zu",
	        kind.c_str(), requestId.c_str(), locale.c_str(), prompt.size());

	// Capture by value into the worker. shim.manager + llmFn are static
	// process-lifetime pointers — safe to reference from the detached
	// thread for the entire Designer session.
	ibPluginManager*                   mgr = shim.manager;
	ibPluginManager::RegisteredFunction fn  = shim.llmFn;
	const std::string                   paneIdOwned(paneId);
	const std::string                   ridOwned   (requestId);
	std::string                         promptOwned(std::move(prompt));
	std::string                         localeOwned(std::move(locale));

	// Allocate the cancel token BEFORE spawning so a near-instant
	// agent.cancel from the pane can still find a registered entry. No
	// requestId == nothing to cancel (legacy callers); skip the token in
	// that case so the map doesn't grow unbounded keyed by "".
	std::shared_ptr<std::atomic<bool>> cancelTok =
	    ridOwned.empty() ? nullptr : AllocCancelToken(ridOwned);

	// SEC-P0-3: track the worker so UnloadAll can quiesce it before
	// `mgr` (and the captured RegisteredFunction's m_fn pointer, which
	// lives in the soon-to-be-unloaded plugin DLL) become invalid.
	auto worker = std::thread([mgr, fn, paneIdOwned, ridOwned, promptOwned, localeOwned, cancelTok]() {
		auto emit = [&](const std::string& body) {
			// SEC-P0-3: don't touch `mgr` after shutdown trip — it may be
			// mid-destruction. Worker exits silently; UnloadAll wins.
			if (g_shimShutdown.load()) return;
			mgr->CallWebPaneSend(wxString::FromUTF8(paneIdOwned.c_str()),
			                       wxString::FromUTF8(body.c_str()));
		};

		// LLMQuery is synchronous; it owns its own httplib client which
		// blocks the worker thread, not the UI thread. That is the
		// whole point of doing this off-main.
		ibValue vPrompt; vPrompt.SetString(wxString::FromUTF8(promptOwned.c_str()));
		ibValue vLocale; vLocale.SetString(wxString::FromUTF8(localeOwned.c_str()));
		ibValue* args[2] = { &vPrompt, &vLocale };
		ibValue ret;
		const auto t0 = std::chrono::steady_clock::now();
		const bool ok = mgr->CallFunction(fn, ret, args, 2);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		    std::chrono::steady_clock::now() - t0).count();

		if (!ok) {
			DiagLog("  worker: LLMQuery FAILED after %lld ms", (long long)ms);
			std::string err =
			    "{\"kind\":\"error\",\"error\":{\"code\":\"shim_call_failed\","
			    "\"detail\":\"LLMQuery returned failure — check plugin env vars\"}";
			if (!ridOwned.empty()) {
				err += ",\"requestId\":\"" + JsonEscape(ridOwned) + "\"";
			}
			err += "}";
			emit(err);
			if (!ridOwned.empty()) EraseCancelToken(ridOwned);
			return;
		}

		const std::string rawResponse = std::string(ret.GetString().utf8_str().data());

		// Some v3 plugins return the full MCP transport envelope as a
		// JSON string. Extract the user-facing text.
		std::string response = rawResponse;
		{
			auto parsed = nlohmann::json::parse(rawResponse, nullptr, /*allow_exceptions=*/false);
			if (!parsed.is_discarded() && parsed.is_object()) {
				if (parsed.contains("result")) {
					const auto& res = parsed["result"];
					if (res.is_object() && res.contains("content") && res["content"].is_string()) {
						response = res["content"].get<std::string>();
					} else if (res.is_string()) {
						response = res.get<std::string>();
					}
				} else if (parsed.contains("content") && parsed["content"].is_string()) {
					response = parsed["content"].get<std::string>();
				} else if (parsed.contains("error")) {
					const auto& errNode = parsed["error"];
					if (errNode.is_string()) {
						response = "[error] " + errNode.get<std::string>();
					} else if (errNode.is_object()) {
						response = "[error] " + errNode.dump();
					}
				}
			}
		}

		DiagLog("  worker: CallFunction OK %lld ms, raw=%zu, unwrapped=%zu",
		        (long long)ms, rawResponse.size(), response.size());

		// Typewriter streaming. Legacy LLMQuery is one-shot so we have
		// no real upstream chunks; split the response into roughly
		// sentence-sized pieces and emit chat.delta one at a time with
		// a short sleep between so the user sees the text appear
		// progressively rather than all at once. ~24 chunks/sec feels
		// natural without overloading the pane re-render.
		const size_t kChunkChars = 96;
		const std::chrono::milliseconds kChunkDelay(40);
		size_t i = 0;
		size_t chunksEmitted = 0;
		bool   cancelled = false;
		while (i < response.size()) {
			// SEC-P0-3: shutdown trumps everything — bail without emitting
			// further chat.delta + skip the trailing chat.end (manager may
			// already be tearing down its callbacks).
			if (g_shimShutdown.load()) {
				DiagLog("  worker: shutdown flag observed, aborting after %zu chunks",
				        chunksEmitted);
				return;
			}
			// Cancellation poll — set by agent.cancel from the pane. We
			// check BEFORE each chunk emit so a flag flipped between
			// sleeps is honoured on the very next iteration.
			if (cancelTok && cancelTok->load()) {
				cancelled = true;
				DiagLog("  worker: cancel flag observed, stopping after %zu chunks",
				        chunksEmitted);
				break;
			}

			// Try to break on a UTF-8 codepoint boundary so we don't
			// split a multibyte character down the middle (Cyrillic
			// is two bytes per glyph).
			size_t end = std::min(i + kChunkChars, response.size());
			while (end < response.size() && (static_cast<unsigned char>(response[end]) & 0xC0) == 0x80) {
				++end;
			}
			std::string chunk = response.substr(i, end - i);
			i = end;

			std::string delta = "{\"kind\":\"chat.delta\"";
			if (!ridOwned.empty()) {
				delta += ",\"requestId\":\"" + JsonEscape(ridOwned) + "\"";
			}
			delta += ",\"delta\":\"" + JsonEscape(chunk) + "\"}";
			emit(delta);
			++chunksEmitted;

			if (i < response.size()) {
				std::this_thread::sleep_for(kChunkDelay);
			}
		}

		std::string endEnvelope = "{\"kind\":\"chat.end\"";
		if (!ridOwned.empty()) {
			endEnvelope += ",\"requestId\":\"" + JsonEscape(ridOwned) + "\"";
		}
		endEnvelope += ",\"meta\":{\"model\":\"legacy-llm-shim\"";
		if (cancelled) endEnvelope += ",\"cancelled\":true";
		endEnvelope += "}}";
		emit(endEnvelope);
		if (!ridOwned.empty()) EraseCancelToken(ridOwned);
		DiagLog("  worker: done, emitted %zu chars in %zu chunks cancelled=%d",
		        response.size(), chunksEmitted, cancelled ? 1 : 0);
	});
	// SEC-P0-3: register worker for shutdown drain (replaces detach()).
	{
		std::lock_guard<std::mutex> lk(g_shimWorkersMu);
		g_shimWorkers.push_back(std::move(worker));
	}
}

} // namespace

void ibPluginManager::CompleteCodeAsync(const wxString& prompt,
                                          const wxString& locale,
                                          CompleteCodeCallback onResult)
{
	DiagLog("CompleteCodeAsync: prompt_size=%zu locale=%s",
	        prompt.length(), locale.utf8_str().data());

	// Locate LLMQuery — same path the shim uses. v4 native provider
	// support is a follow-up: aiBridge's Query is currently a no-op so
	// the shim's LLMQuery is the only live route to an LLM.
	const RegisteredFunction* llmFn = nullptr;
	for (const auto& f : m_functions) {
		if (f.m_name == "LLMQuery") { llmFn = &f; break; }
	}
	if (llmFn == nullptr) {
		DiagLog("  CompleteCode: no LLMQuery — onResult(false)");
		if (onResult) onResult(false, wxEmptyString,
		                         _("AI completion provider is not available"));
		return;
	}

	// Capture by value for the worker. The function table doesn't
	// shuffle live entries (LoadAll is the only mutator and tests
	// confirm it doesn't run mid-session), so storing a copy of the
	// RegisteredFunction is safe.
	RegisteredFunction         fnCopy = *llmFn;
	ibPluginManager*           mgr    = this;
	const std::string          promptOwned(prompt.utf8_str().data());
	const std::string          localeOwned(
	    locale.IsEmpty() ? std::string("uk-UA")
	                     : std::string(locale.utf8_str().data()));
	auto                       cb      = std::move(onResult);

	auto worker = std::thread([mgr, fnCopy, promptOwned, localeOwned, cb]() {
		// SEC-P0-3: short-circuit when shutdown trips before we even
		// dispatch the call — fnCopy.m_fn points into the plugin DLL.
		if (g_shimShutdown.load()) return;
		ibValue vPrompt; vPrompt.SetString(wxString::FromUTF8(promptOwned.c_str()));
		ibValue vLocale; vLocale.SetString(wxString::FromUTF8(localeOwned.c_str()));
		ibValue* args[2] = { &vPrompt, &vLocale };
		ibValue ret;
		const auto t0 = std::chrono::steady_clock::now();
		const bool ok = mgr->CallFunction(fnCopy, ret, args, 2);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		    std::chrono::steady_clock::now() - t0).count();

		wxString text;
		wxString err;
		if (!ok) {
			err = _("LLM call failed — check plugin credentials");
		} else {
			const std::string raw(ret.GetString().utf8_str().data());
			// Unwrap MCP envelope or use raw text. Same logic the
			// chat shim uses — keeps response shape behaviour identical.
			std::string content = raw;
			auto parsed = nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/false);
			if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("result")) {
				const auto& r = parsed["result"];
				if (r.is_object() && r.contains("content") && r["content"].is_string()) {
					content = r["content"].get<std::string>();
				} else if (r.is_string()) {
					content = r.get<std::string>();
				}
			} else if (!parsed.is_discarded() && parsed.is_object() &&
			           parsed.contains("error")) {
				err = wxString::FromUTF8(parsed["error"].dump().c_str());
				content.clear();
			}
			text = wxString::FromUTF8(content.c_str());
		}

		DiagLog("  CompleteCode: %lld ms ok=%d text_size=%zu",
		        (long long)ms, ok ? 1 : 0, text.length());

		// Marshal back to the UI thread before invoking the callback.
		// Callers (codeEditor) touch wx widgets which are not
		// thread-safe; CallAfter queues a wxEvent that the wxApp drains
		// on the main thread.
		// SEC-P0-3: skip the UI callback when shutting down — wxTheApp
		// may already be tearing down its event queue.
		if (cb && wxTheApp != nullptr && !g_shimShutdown.load()) {
			wxTheApp->CallAfter([cb, ok, text, err]() {
				cb(ok && err.IsEmpty(), text, err);
			});
		}
	});
	// SEC-P0-3: register worker for shutdown drain (replaces detach()).
	{
		std::lock_guard<std::mutex> lk(g_shimWorkersMu);
		g_shimWorkers.push_back(std::move(worker));
	}
}

void ibPluginManager::ScanForLegacyLLMShim(const wxString& htmlBundlePath)
{
	// SEC-P0-3: re-Scan replaces shim state. Drain prior workers so they
	// can't fire WebPaneSend with stale captures (paneId/manager).
	JoinAndDrainShimWorkers();
	DiagLog("ScanForLegacyLLMShim: invoked, bundle='%s', exists=%d",
	        htmlBundlePath.utf8_str().data(), (int)wxFileExists(htmlBundlePath));
	DiagLog("  plugins loaded: %zu", m_plugins.size());
	for (const auto& p : m_plugins) {
		DiagLog("    - %s ABI v%d",
		        p.m_info ? p.m_info->name : "?",
		        p.m_info ? p.m_info->abi_version : -1);
	}
	DiagLog("  functions registered: %zu", m_functions.size());
	for (const auto& f : m_functions) {
		DiagLog("    - %s/%d", f.m_name.c_str(), f.m_paramCount);
	}
	DiagLog("  AI providers: %zu", m_aiProviders.size());
	for (const auto& a : m_aiProviders) {
		DiagLog("    - %s (modes:%zu)", a.providerId.c_str(), a.supportedModes.size());
	}
	DiagLog("  WebPaneCallbacks installed: register=%d send=%d show=%d",
	        (int)(bool)m_webPaneRegister, (int)(bool)m_webPaneSend, (int)(bool)m_webPaneShow);

	// Skip when a real v4 plugin already covers chat mode — no point
	// stacking a shim on top of a working provider.
	if (HasAIProviderFor("chat")) {
		DiagLog("  SKIP: v4 chat provider already registered");
		wxLogDebug(wxT("LegacyLLMShim: v4 chat provider already registered — skipping shim"));
		return;
	}

	// Locate the LLMQuery function in the post-load registry.
	const RegisteredFunction* found = nullptr;
	for (const auto& f : m_functions) {
		if (f.m_name == "LLMQuery") { found = &f; break; }
	}
	if (found == nullptr) {
		DiagLog("  SKIP: no LLMQuery function registered (v3 plugin failed to init?)");
		wxLogDebug(wxT("LegacyLLMShim: no plugin exposes LLMQuery — nothing to bridge"));
		return;
	}
	if (htmlBundlePath.IsEmpty() || !wxFileExists(htmlBundlePath)) {
		DiagLog("  SKIP: bundle missing or empty path");
		wxLogWarning(wxT("LegacyLLMShim: sample.html bundle not found — shim disabled"));
		return;
	}

	auto& shim = Shim();
	std::lock_guard<std::mutex> lk(shim.mu);
	shim.manager = this;
	shim.llmFn   = *found;
	shim.paneId  = "legacy.llm-shim.chat";

	// Synthesise an AI provider entry so HasAIProviderFor("chat") flips
	// true and the editor right-click submenu enables. Cancel/ListModels
	// stay null — the shim's only path is via the pane.
	RegisteredAIProvider prov;
	prov.providerId     = "legacy.llm-shim";
	prov.displayName    = "AI Assistant (legacy bridge)";
	prov.iconPath       = "";
	prov.supportedModes = { "chat", "agent" };
	prov.Query          = nullptr;
	prov.Cancel         = nullptr;
	prov.ListModels     = nullptr;
	prov.userData       = nullptr;

	// Drop any prior synthetic entry under the same providerId so
	// reload sequences stay clean.
	for (auto it = m_aiProviders.begin(); it != m_aiProviders.end(); ) {
		if (it->providerId == prov.providerId) it = m_aiProviders.erase(it);
		else                                   ++it;
	}
	m_aiProviders.push_back(std::move(prov));

	const wxString paneId(wxString::FromUTF8(shim.paneId.c_str()));
	const int rc = CallWebPaneRegister(paneId,
	                                     _("AI Assistant"),
	                                     htmlBundlePath,
	                                     &LegacyShimOnPaneMessage,
	                                     nullptr);
	if (rc != 0) {
		DiagLog("  FAIL: CallWebPaneRegister returned %d (likely no callbacks installed)", rc);
		wxLogWarning(wxT("LegacyLLMShim: CallWebPaneRegister('%s') returned %d"),
		             paneId, rc);
		// Tear down the synthetic provider on registration failure so the
		// menu doesn't pretend the shim is live.
		for (auto it = m_aiProviders.begin(); it != m_aiProviders.end(); ) {
			if (it->providerId == "legacy.llm-shim") it = m_aiProviders.erase(it);
			else                                       ++it;
		}
		return;
	}
	DiagLog("  OK: bridged LLMQuery -> pane '%s'. defaultAIPaneId='%s'",
	        paneId.utf8_str().data(), m_defaultAIPaneId.utf8_str().data());
	wxLogMessage(wxT("LegacyLLMShim: bridging LLMQuery -> pane '%s'"), paneId);
}

int ibPluginManager::CallWebPaneSend(const wxString& paneId,
                                       const wxString& jsonInline) const
{
	if (!m_webPaneSend) return -1;
	return m_webPaneSend(paneId, jsonInline);
}

int ibPluginManager::CallWebPaneShow(const wxString& paneId) const
{
	if (!m_webPaneShow) return -1;
	return m_webPaneShow(paneId);
}

// =========================================================================
// AI provider registry + chunk dispatch (ABI v4 Phase 2)
// =========================================================================
namespace {

// Drain a NULL-terminated array of C strings into a std::vector<std::string>.
// Safe against a NULL outer pointer (returns empty vector).
std::vector<std::string> CollectStrings(const char** arr)
{
	std::vector<std::string> out;
	if (arr == nullptr) return out;
	for (const char** p = arr; *p != nullptr; ++p) {
		out.emplace_back(*p);
	}
	return out;
}

// Tiny JSON-string escaper for fields we control (requestId is opaque
// provider data, deltaJson/metaJson/errorJson are already JSON we splice
// in literally). Mirrors the WrapAsJsString helper on the frontend side
// but is backend-local; the backend cannot link against frontend code.
std::string EscapeJsonString(const char* s)
{
	std::string out;
	if (s == nullptr) return out;
	for (const char* p = s; *p; ++p) {
		unsigned char c = static_cast<unsigned char>(*p);
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '"':  out += "\\\""; break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		case '\b': out += "\\b";  break;
		case '\f': out += "\\f";  break;
		default:
			if (c < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += static_cast<char>(c);
			}
		}
	}
	return out;
}

// Validate `payload` parses as JSON. On invalid input we wrap the raw
// bytes as a JSON string so the WebView still receives something
// structurally valid instead of crashing JSON.parse. nlohmann's accept()
// runs a parser in validation-only mode (no AST construction) so the
// cost is just lexing + balancing — acceptable on hot stream paths.
// A first-letter heuristic ("t/f/n means valid literal") false-passes
// inputs like `tabs`, `funky`, `north`, broken `{`, unterminated `"` —
// those then crash JSON.parse on the WebView side.
std::string SanitiseJsonValue(const char* payload)
{
	if (payload == nullptr || *payload == '\0') return std::string("null");
	if (nlohmann::json::accept(payload)) {
		return std::string(payload);
	}
	std::string s = "\"";
	s += EscapeJsonString(payload);
	s += "\"";
	return s;
}

} // namespace

bool ibPluginManager::HasAIProviderFor(const std::string& mode) const
{
	if (m_aiProviders.empty()) return false;
	if (mode.empty()) return true; // any provider counts
	for (const auto& p : m_aiProviders) {
		for (const auto& m : p.supportedModes) {
			if (m == mode) return true;
		}
	}
	return false;
}

int ibPluginManager::HostRegisterAIProvider(const ibPluginAIProvider* p)
{
	if (p == nullptr || p->providerId == nullptr || *p->providerId == '\0') {
		return -1;
	}
	// A provider with no Query is useless and would NPE-crash the kernel
	// when Phase 4 settings dispatch routes a prompt to it. Refuse early.
	if (p->Query == nullptr) {
		return -1;
	}

	RegisteredAIProvider entry;
	entry.providerId     = p->providerId;
	entry.displayName    = p->displayName ? p->displayName : p->providerId;
	entry.iconPath       = p->iconPath    ? p->iconPath    : "";
	entry.supportedModes = CollectStrings(p->supportedModes);
	entry.Query          = p->Query;
	entry.Cancel         = p->Cancel;
	entry.ListModels     = p->ListModels;
	entry.userData       = p->userData;

	// Replace on duplicate providerId so a plugin re-init lands cleanly
	// (e.g. the user toggled the plugin off/on in Settings).
	for (auto& existing : m_aiProviders) {
		if (existing.providerId == entry.providerId) {
			existing = std::move(entry);
			return 0;
		}
	}
	m_aiProviders.push_back(std::move(entry));
	return 0;
}

int ibPluginManager::HostAIChunkEmit(const char* requestId, const char* deltaJson)
{
	if (m_defaultAIPaneId.IsEmpty() || !m_webPaneSend) return -1;
	std::string payload = "{\"kind\":\"chat.delta\",\"requestId\":\"";
	payload += EscapeJsonString(requestId);
	payload += "\",\"delta\":";
	payload += SanitiseJsonValue(deltaJson);
	payload += "}";
	return m_webPaneSend(m_defaultAIPaneId, wxString::FromUTF8(payload.c_str()));
}

int ibPluginManager::HostAIChunkEnd(const char* requestId, const char* metaJson)
{
	if (m_defaultAIPaneId.IsEmpty() || !m_webPaneSend) return -1;
	std::string payload = "{\"kind\":\"chat.end\",\"requestId\":\"";
	payload += EscapeJsonString(requestId);
	payload += "\",\"meta\":";
	payload += SanitiseJsonValue(metaJson);
	payload += "}";
	return m_webPaneSend(m_defaultAIPaneId, wxString::FromUTF8(payload.c_str()));
}

int ibPluginManager::HostAIChunkError(const char* requestId, const char* errorJson)
{
	if (m_defaultAIPaneId.IsEmpty() || !m_webPaneSend) return -1;
	std::string payload = "{\"kind\":\"error\",\"requestId\":\"";
	payload += EscapeJsonString(requestId);
	payload += "\",\"error\":";
	payload += SanitiseJsonValue(errorJson);
	payload += "}";
	return m_webPaneSend(m_defaultAIPaneId, wxString::FromUTF8(payload.c_str()));
}

void ibPluginManager::CallMenuHandler(const RegisteredMenuItem& item)
{
	if (item.m_handler == nullptr) return;
	ibPluginCallScope scope;
	ibPluginCallScope* prev = tl_scope;
	tl_scope = &scope;
	try { item.m_handler(); } catch (...) { /* plugin bug — swallow */ }
	tl_scope = prev;
}
