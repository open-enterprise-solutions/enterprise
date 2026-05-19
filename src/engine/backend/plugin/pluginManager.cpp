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
// dispatch back into it. Set for the duration of LoadAll().
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

int  Host_RegisterWebPane(const char* /*paneId*/, const char* /*title*/,
                            const char* /*htmlBundlePath*/,
                            ibPluginWebMsgFn /*onMessage*/,
                            void* /*userData*/)
{
	return -1; // not yet implemented (Phase 1)
}

int  Host_WebPaneSend(const char* /*paneId*/, const char* /*jsonInline*/)
{
	return -1;
}

int  Host_WebPaneShow(const char* /*paneId*/)
{
	return -1;
}

int  Host_RegisterAIProvider(const ibPluginAIProvider* /*provider*/)
{
	return -1; // Phase 2
}

int  Host_AIChunkEmit(const char* /*requestId*/, const char* /*deltaJson*/)
{
	return -1;
}

int  Host_AIChunkEnd(const char* /*requestId*/, const char* /*metaJson*/)
{
	return -1;
}

int  Host_AIChunkError(const char* /*requestId*/, const char* /*errorJson*/)
{
	return -1;
}

int  Host_MetaCreate(const char* /*objectKind*/, const char* /*fullName*/,
                       const char* /*propertiesJson*/, char** /*errorMsg*/)
{
	return -1; // Phase 3
}

int  Host_MetaEdit(const char* /*fullName*/, const char* /*jsonPatch*/,
                     char** /*errorMsg*/)
{
	return -1;
}

int  Host_MetaDelete(const char* /*fullName*/, char** /*errorMsg*/)
{
	return -1;
}

int  Host_MetaQuery(const char* /*fullName*/, const char* /*fieldsFilter*/,
                      char** /*jsonOut*/, char** /*errorMsg*/)
{
	return -1;
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

	g_currentManager = this;
	struct ManagerGuard { ~ManagerGuard() { g_currentManager = nullptr; } } guard;

	const wxString dir = GetPluginsDir();
	if (!wxDirExists(dir))
		return 0;

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

void ibPluginManager::CallMenuHandler(const RegisteredMenuItem& item)
{
	if (item.m_handler == nullptr) return;
	ibPluginCallScope scope;
	ibPluginCallScope* prev = tl_scope;
	tl_scope = &scope;
	try { item.m_handler(); } catch (...) { /* plugin bug — swallow */ }
	tl_scope = prev;
}
