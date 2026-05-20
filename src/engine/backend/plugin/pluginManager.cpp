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

#include <cstdio>
#include <string>

#include "3rdparty/nlohmann/json.hpp"

#include "metaBridge.h"
#include "pluginsConfig.h"

#ifdef __WXMSW__
  #include <windows.h>
#endif

namespace {

// Lowest + highest ABI version this build accepts. Plugins built against
// a number outside [kAbiMin, kAbiMax] are skipped. v1..v3 plugins remain
// loadable; ABI v4 added new ibHostAPI slots at the struct tail.
constexpr int kAbiMin = 1;
constexpr int kAbiMax = IB_PLUGIN_ABI_VERSION;  // bumped to 4

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
	// shutdown. Route through wxLogMessage as a stable fallback.
	const wxString text = wxString::FromUTF8(msg ? msg : "");
	switch (severity) {
		case 1:  wxLogWarning("%s", text); break;
		case 2:  wxLogError  ("%s", text); break;
		default: wxLogMessage("%s", text); break;
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
	wxFileName fn(wxStandardPaths::Get().GetExecutablePath());
	fn.AppendDir("plugins");
	fn.SetFullName(wxEmptyString);
	return fn.GetPath();
}

size_t ibPluginManager::LoadAll()
{
	// Make the host-trampolines see this instance for the entire app
	// lifetime, not just the duration of LoadAll. WebPaneSend etc. fire
	// long after init returns.
	g_currentManager = this;

	UnloadAll();

	// Sandbox / kill-switch. OES_PLUGIN_SANDBOX=1 in the environment
	// skips plugin discovery entirely — useful for incident response
	// (a misbehaving plugin won't even load on the next launch) and
	// for the read-only "viewer" deployments that should never run
	// third-party code.
	const wxString sandbox = wxGetenv(wxT("OES_PLUGIN_SANDBOX"));
	if (sandbox == wxT("1") || sandbox.Lower() == wxT("true")) {
		wxLogMessage(wxT("Plugin sandbox active (OES_PLUGIN_SANDBOX=1) — skipping plugin discovery."));
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
			wxLogMessage(wxT("Plugin '%s' disabled in plugins.json5 — skipping"),
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
			const ibHostAPI* host =
			    (info->abi_version >= 2) ? &g_hostAPI : nullptr;
			const int rc = init_fn(host);
			tl_scope = prev;
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

		m_plugins.push_back(std::move(p));
	}

	return m_plugins.size();
}

void ibPluginManager::UnloadAll()
{
	// Subscriber + function tables come down first — once the DLLs are
	// gone any held function pointer would dangle.
	m_subscribers.clear();
	m_functions.clear();
	m_menuItems.clear();
	m_aiProviders.clear();
	m_defaultAIPaneId.Clear();
	m_pendingWebPaneRegs.clear();

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
	// Last writer wins — overwrite an existing binding with the same name.
	const std::string key(name);
	for (auto& f : m_functions) {
		if (f.m_name == key) { f.m_paramCount = paramCount; f.m_fn = fn; return 0; }
	}
	m_functions.push_back({ key, paramCount, fn });
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
	return rc == 0;
}

void ibPluginManager::SetWebPaneCallbacks(WebPaneRegisterFn reg,
                                            WebPaneSendFn    send,
                                            WebPaneShowFn    show)
{
	m_webPaneRegister = std::move(reg);
	m_webPaneSend     = std::move(send);
	m_webPaneShow     = std::move(show);
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
	if (!m_webPaneRegister) {
		// Designer hasn't installed callbacks yet — buffer for replay.
		// Return success so the plugin doesn't treat early-init as
		// failure; ReplayPendingWebPaneRegistrations will surface the
		// real result later once the frame wires its lambdas. The
		// default-pane slot is claimed inside the replay loop AFTER
		// the entry actually registers successfully — a buffered reg
		// that the frame later rejects must not shadow valid panes.
		m_pendingWebPaneRegs.push_back({paneId, title, htmlBundlePath, cb, userData});
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
	return rc;
}

ibPluginManager::~ibPluginManager()
{
	UnloadAll();
	// Clear the global only if we still own it — a future instance may
	// have replaced us via its own ctor / LoadAll. Single-instance is the
	// documented invariant for ABI v4 host trampolines.
	if (g_currentManager == this) g_currentManager = nullptr;
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
			wxLogMessage(wxT("[plugin-policy] %s::%s -> ALLOW (wildcard %s)"),
			             pluginId, opName, wxString::FromUTF8(PolicyName(w)));
			return true;
		}
		if (w == MutationPolicy::Deny) {
			wxLogMessage(wxT("[plugin-policy] %s::%s -> DENY (wildcard Deny)"),
			             pluginId, opName);
			return false;
		}
		// Wildcard Ask falls through to the per-op check.
	}

	const MutationPolicy p = GetMutationPolicy(pluginId, opName);
	const bool allow = (p == MutationPolicy::AllowSession ||
	                    p == MutationPolicy::AllowAlways);
	wxLogMessage(wxT("[plugin-policy] %s::%s -> %s (%s)"),
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
	}
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
