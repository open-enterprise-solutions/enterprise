/////////////////////////////////////////////////////////////////////////////
// Plugin manager — scans <exe-dir>/plugins for .dll/.so files and loads any
// that expose the OES plugin ABI (see pluginApi.h).
//
// ABI v2 brings the ibHostAPI bridge: registered BSL builtins, menu items,
// lifecycle event subscribers, and log messages all flow through the
// manager. Host code drives broadcasts through FireEvent("<name>", payload)
// and consumes the registered set via the getters below.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_MANAGER_H_
#define _IB_PLUGIN_MANAGER_H_

#include "backend/backend.h"
#include "backend/plugin/pluginApi.h"

#include <wx/dynlib.h>
#include <wx/string.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class BACKEND_API ibPluginManager {
public:
	ibPluginManager() = default;
	ibPluginManager(const ibPluginManager&) = delete;
	ibPluginManager& operator=(const ibPluginManager&) = delete;

	struct LoadedPlugin {
		wxString               m_path;
		std::unique_ptr<wxDynamicLibrary> m_lib;
		const ibPluginInfo*    m_info = nullptr;
		ibPluginShutdownFn     m_shutdown = nullptr;

		LoadedPlugin() = default;
		LoadedPlugin(LoadedPlugin&&) = default;
		LoadedPlugin& operator=(LoadedPlugin&&) = default;
		LoadedPlugin(const LoadedPlugin&) = delete;
		LoadedPlugin& operator=(const LoadedPlugin&) = delete;
	};

	// Registered BSL/CES builtin — name + function pointer. The system
	// manager iterates this set after plugin load and binds each as a
	// callable identifier inside the script tokenizer.
	//
	// SEC-P1-9: m_pluginId carries the registering plugin's identity so
	// CallFunction can push it into tl_currentPluginId for the duration
	// of the call. Without it, worker threads invoking host trampolines
	// (Host_MetaCreate / Edit / Delete / ReadPluginEnv) saw an empty
	// scope and were denied by the policy gate.
	struct RegisteredFunction {
		std::string         m_name;
		int                 m_paramCount = -1;
		ibPluginFunctionFn  m_fn = nullptr;
		std::string         m_pluginId;
	};

	// Registered Designer Tools → Plugins submenu item. The Designer
	// reads this list after plugin load and appends a menu entry per
	// element with the supplied handler.
	struct RegisteredMenuItem {
		std::string         m_label;
		ibPluginMenuFn      m_handler = nullptr;
	};

	// Scan <exe-dir>/plugins/ and load every DLL that exports the OES
	// ABI. Returns how many plugins were successfully loaded.
	size_t LoadAll();

	// Call each plugin's shutdown export (if any) and unload the library.
	void UnloadAll();

	const std::vector<LoadedPlugin>&        Loaded()       const { return m_plugins;        }
	const std::vector<RegisteredFunction>&  Functions()    const { return m_functions;      }
	const std::vector<RegisteredMenuItem>&  MenuItems()    const { return m_menuItems;      }

	// Fire a lifecycle event. The host calls this from the relevant
	// broadcaster (docManager.OnDocumentSaved, debugServer events, etc).
	// `payload` may be null. Dispatches to every plugin that
	// subscribed to `name` via ibHostAPI::Subscribe. Failures inside
	// plugin callbacks are swallowed — a misbehaving plugin must not
	// take the host process down.
	void FireEvent(const wxString& name, ibPluginValue* payload = nullptr);

	// Directory that will be scanned (<exe-dir>/plugins).
	static wxString GetPluginsDir();

	~ibPluginManager();

	// Internal — registration entry points invoked through ibHostAPI
	// callbacks. Public so the host-bridge translation unit can call
	// them; not part of the plugin-facing surface.
	int  HostRegisterFunction(const char* name, int paramCount, ibPluginFunctionFn fn);
	int  HostRegisterMenuItem(const char* label, ibPluginMenuFn handler);
	int  HostSubscribe(const char* event, ibPluginEventFn cb);

	// AI provider entry — stored copy of the plugin-supplied struct.
	// Function pointers are valid for the plugin lifetime (host clears
	// the registry in UnloadAll). The map is keyed by providerId so a
	// plugin that re-registers replaces its prior entry cleanly.
	struct RegisteredAIProvider {
		std::string                                                providerId;
		std::string                                                displayName;
		std::string                                                iconPath;
		std::vector<std::string>                                   supportedModes;
		int  (*Query)(const char* requestJson, const char* requestId, void* userData);
		int  (*Cancel)(const char* requestId, void* userData);
		int  (*ListModels)(char** jsonOut);
		void*                                                      userData;
	};

	// Register an AI provider. Plugin calls this once from
	// oes_plugin_initialize. Duplicate providerId replaces. Returns 0
	// on success, -1 if provider or providerId is null.
	int HostRegisterAIProvider(const ibPluginAIProvider* provider);

	// Lookup table for Settings UI (Phase 4) and dispatch.
	const std::vector<RegisteredAIProvider>& AIProviders() const { return m_aiProviders; }

	// Phase 6 editor-integration gate. Returns true when at least one
	// AI provider is registered AND advertises the requested mode (e.g.
	// "chat" / "agent"). Used to grey out context-menu skills, ghost-
	// text completion triggers, and the AI marker auto-fix lightbulb
	// when no usable provider has been installed.
	bool HasAIProviderFor(const std::string& mode) const;

	// Phase 6 universal routing: paneId of the active AI provider's
	// WebView. Editor's right-click skills, ghost-text completion, and
	// the AI Assistant menu all dispatch to this id rather than hard-
	// coding individual pane names. Returns empty when no AI plugin has
	// registered a pane yet — callers should treat that as "no AI" and
	// grey out / fall back to a demo pane.
	wxString GetDefaultAIPaneId() const { return m_defaultAIPaneId; }

	// Phase 7.5 legacy-shim: when an ABI v3 plugin exposes the
	// LLMQuery(prompt, locale) script function but doesn't speak the
	// v4 chat-pane protocol, synthesise an AI provider entry + chat
	// pane that forwards chat.send / editor.skill envelopes through
	// LLMQuery and emits the response as a single chat.delta + chat.end
	// pair. Lets the v3 ecosystem and internal vendor plugins work
	// inside the Designer chat UI without a rebuild. No-op when a v4
	// AI provider already covers the "chat" mode.
	//
	// Must be called AFTER SetWebPaneCallbacks + ReplayPending so the
	// shim pane registration succeeds rather than buffering. Designer
	// calls this from WirePluginWebPaneCallbacks once the WebView wiring
	// is live and the sample.html bundle path has been resolved.
	void ScanForLegacyLLMShim(const wxString& htmlBundlePath);

	// Phase 6.2.b inline completion. Runs an LLM call in a worker
	// thread and invokes `onResult` on the main UI thread with the
	// plain-text completion (no markdown, no envelopes). Routes through
	// the same v3-shim path as chat so any plugin exposing LLMQuery
	// (or a v4 AI provider in the future) backs ghost-text without
	// extra wiring.
	//
	// Callback signature:
	//   void onResult(bool ok, const wxString& completionText, const wxString& errMessage);
	//
	// onResult is invoked exactly once. If no AI provider is registered
	// or LLMQuery is missing, onResult fires with ok=false, errMessage
	// describing the gap, completionText empty.
	using CompleteCodeCallback = std::function<void(bool ok,
	                                                 const wxString& text,
	                                                 const wxString& err)>;
	void CompleteCodeAsync(const wxString& prompt,
	                        const wxString& locale,
	                        CompleteCodeCallback onResult);

	// Plugin chunk-delivery helpers — wrap deltaJson / metaJson / errorJson
	// in the chat.delta / chat.end / error envelope and forward to the
	// active pane. Phase 2 routing: target = the first registered pane;
	// Phase 4 refines this to per-request routing once the Settings
	// dialog ships and provider selection becomes user-driven.
	int HostAIChunkEmit (const char* requestId, const char* deltaJson);
	int HostAIChunkEnd  (const char* requestId, const char* metaJson);
	int HostAIChunkError(const char* requestId, const char* errorJson);

	// Per-plugin BYOK env cache populated at LoadAll. Accessible via the
	// ABI v4 ReadPluginEnv trampoline; the calling plugin's id is
	// resolved from the host-side thread-local scope.
	using PluginEnvMap =
	    std::unordered_map<std::string,
	                       std::unordered_map<std::string, std::string>>;
	const PluginEnvMap& PluginEnv() const { return m_pluginEnv; }
	void SetPluginEnvForTests(PluginEnvMap env) { m_pluginEnv = std::move(env); }

	// SEC-P1-8: unset every env key that LoadAll exported into the process
	// env (so tokens don't leak to forked subprocesses + crash dumps across
	// config reloads). Called from UnloadAll. Public so tests can observe.
	void UnsetInjectedEnvKeys();

	// Read a plugin env value. Returns empty string when absent.
	std::string ReadPluginEnv(const std::string& pluginId,
	                            const std::string& key) const;

	// Frontend hookup for ABI v4 WebView pane entries. Designer
	// registers concrete callbacks on init; backend trampolines
	// (Host_RegisterWebPane / Host_WebPaneSend / Host_WebPaneShow)
	// forward through these so the layering stays clean (backend
	// cannot depend on wxWebView, but plugins drive WebView from
	// inside the host ABI). Returns -1 when no callbacks have been
	// installed yet — common in headless run modes.
	using WebPaneRegisterFn = std::function<int(
	    const wxString& paneId, const wxString& title,
	    const wxString& htmlBundlePath,
	    ibPluginWebMsgFn onMessage, void* userData)>;
	using WebPaneSendFn = std::function<int(
	    const wxString& paneId, const wxString& jsonInline)>;
	using WebPaneShowFn = std::function<int(const wxString& paneId)>;

	void SetWebPaneCallbacks(WebPaneRegisterFn reg,
	                          WebPaneSendFn    send,
	                          WebPaneShowFn    show);

	// Plugin RegisterWebPane calls fired from oes_plugin_initialize land
	// here. If callbacks have not been installed yet (plugins loaded
	// before the Designer frame existed — the normal startup order),
	// the registration is buffered and returns 0; ReplayPendingWebPaneRegistrations
	// drains the buffer once the frame installs its callbacks.
	int CallWebPaneRegister(const wxString& paneId, const wxString& title,
	                         const wxString& htmlBundlePath,
	                         ibPluginWebMsgFn cb, void* userData);
	int CallWebPaneSend(const wxString& paneId, const wxString& jsonInline) const;
	int CallWebPaneShow(const wxString& paneId) const;

	// Designer calls this immediately after SetWebPaneCallbacks so plugins
	// that registered before callbacks were ready still get their panes.
	void ReplayPendingWebPaneRegistrations();

	// SEC-P1-9: resolve the registering plugin for a given paneId. Empty
	// when the paneId wasn't seen at register-time (host-synthesised pane,
	// stale post-UnloadAll lookup, or hostile inbound message id). Frontend
	// dispatch uses this to push tl_currentPluginId around the onMessage
	// call so meta-mutation trampolines see the correct caller identity.
	std::string GetPluginIdForPane(const wxString& paneId) const;

	// SEC-P1-9: thread-safe push of caller-plugin identity for the duration
	// of an off-main-thread dispatch (pane message, deferred callback, AI
	// worker). Returns the previous value so callers can restore it on the
	// way out via PopCallerPluginId(). Use the ScopedPluginIdGuard RAII
	// helper from inside dispatcher lambdas — exception-safe.
	static std::string PushCallerPluginId(const std::string& pluginId);
	static void        PopCallerPluginId(const std::string& restore);

	// ---------------------------------------------------------------------
	// Mutation policy (Phase 3.2). Mirrors the 4-mode permission UX modern
	// IDE assistants converged on:
	//
	//   Ask          — every call prompts user (default for mutations).
	//   AllowSession — silent until restart; user said "allow this one".
	//   AllowAlways  — persisted approval; survives restarts.
	//   Deny         — never allowed; explicit block.
	//
	// Policy is keyed by (pluginId, opName) where opName is one of:
	//   "meta.create"  "meta.edit"  "meta.delete"  "meta.query"
	// Query defaults to AllowAlways; create/edit/delete default to Ask.
	//
	// Even when create/edit are AllowAlways, the meta.delete op carries
	// an extra safeguard: callers must opt in to irreversible ops via
	// `force=true` in the propertiesJson — matching the `rm --no-preserve-
	// root` convention so accidental tab-completion doesn't nuke a
	// Catalog.
	// ---------------------------------------------------------------------
	enum class MutationPolicy : int { Ask = 0, AllowSession = 1, AllowAlways = 2, Deny = 3 };

	void SetMutationPolicy(const wxString& pluginId, const wxString& opName,
	                        MutationPolicy policy);
	MutationPolicy GetMutationPolicy(const wxString& pluginId,
	                                   const wxString& opName) const;

	// Convenience: the agent layer calls this from inside each Meta*
	// trampoline. Returns true when the mutation may proceed; false
	// returns IB_PLUGIN_PERMISSION_DENIED to the plugin. Logs every
	// allow/deny decision through wxLogMessage for audit.
	bool CheckMutationAllowed(const wxString& pluginId, const wxString& opName);

	// Dispatch a registered plugin function from the script call site.
	// Sets up the arena scope so Make* / GetString calls inside the
	// plugin callback see a live ibPluginCallScope; marshals args by
	// adopting each ibValue into the arena; copies the plugin's return
	// value out before the arena tears down. Returns false when the
	// plugin's callback returns non-zero (mapped to a script error).
	bool CallFunction(const RegisteredFunction& fn,
	                   class ibValue& retOut,
	                   class ibValue** paParams,
	                   long lSizeArray);

	// Same trampoline for menu items — sets up an empty arena so the
	// handler can still call host->Log / MakeString (rare but legal).
	void CallMenuHandler(const RegisteredMenuItem& item);

private:
	std::vector<LoadedPlugin>        m_plugins;
	std::vector<RegisteredFunction>  m_functions;
	std::vector<RegisteredMenuItem>  m_menuItems;
	std::unordered_map<std::string,
	    std::vector<ibPluginEventFn>> m_subscribers;

	WebPaneRegisterFn m_webPaneRegister;
	WebPaneSendFn     m_webPaneSend;
	WebPaneShowFn     m_webPaneShow;

	// Buffer of RegisterWebPane calls fired before the Designer installed
	// its callbacks. Drained by ReplayPendingWebPaneRegistrations.
	struct PendingWebPaneReg {
		wxString          paneId;
		wxString          title;
		wxString          htmlBundlePath;
		ibPluginWebMsgFn  onMessage;
		void*             userData;
		std::string       pluginId; // SEC-P1-9: register-time identity
	};
	std::vector<PendingWebPaneReg> m_pendingWebPaneRegs;

	// SEC-P1-9: paneId → pluginId of the registering plugin. Used by the
	// frontend dispatch to push tl_currentPluginId for the duration of the
	// onMessage call so inbound pane messages can hit Meta* trampolines
	// without falling foul of the policy gate's "no caller-pluginId scope"
	// short-circuit. Populated by CallWebPaneRegister + ReplayPending.
	std::unordered_map<std::string, std::string> m_paneIdToPluginId;

	// Registered AI providers. Indexed by providerId for re-registration
	// replacement; iteration order is registration order so Phase 4
	// Settings UI can show plugins in the order they loaded.
	std::vector<RegisteredAIProvider> m_aiProviders;

	// First-registered pane id — Phase 2 default target for AI chunk
	// delivery. Cleared by UnloadAll. Phase 4 replaces with explicit
	// per-request routing via Settings.
	wxString m_defaultAIPaneId;

	// Mutation policy table (Phase 3.2). Key = "<pluginId>::<opName>";
	// absent entries inherit the per-op default — Ask for create/edit/
	// delete, AllowAlways for query. AllowSession entries are wiped
	// here on every UnloadAll; AllowAlways entries are persisted via
	// plugins.json5 (Phase 4.1).
	std::unordered_map<std::string, MutationPolicy> m_mutationPolicy;

	// Per-plugin BYOK env cache (Phase 4.2). Populated by LoadAll from
	// byokEnv::LoadAll; cleared on UnloadAll.
	PluginEnvMap m_pluginEnv;

	// SEC-P1-8 / P2-2: env keys exported into the process env per plugin via
	// the universal v3 env-injection path in LoadAll. UnloadAll iterates
	// this map and restores the prior value (or unsets if there was none)
	// so tokens don't survive in the process env across config reloads AND
	// pre-existing user-set values aren't wiped by the unload.
	struct InjectedEnv {
		std::string key;
		bool        wasSet = false;   // was the key set in the env before injection
		std::string prior;            // its value at that moment
	};
	std::unordered_map<std::string, std::vector<InjectedEnv>> m_injectedEnvKeys;
};

// SEC-P1-9: RAII guard around tl_currentPluginId. Use inside dispatcher
// lambdas / worker threads when calling into a plugin from a thread other
// than the one that ran oes_plugin_initialize. Restores the prior value
// even if the dispatched call throws.
class BACKEND_API ScopedPluginIdGuard {
public:
	explicit ScopedPluginIdGuard(const std::string& pluginId)
	    : m_prev(ibPluginManager::PushCallerPluginId(pluginId)) {}
	~ScopedPluginIdGuard() { ibPluginManager::PopCallerPluginId(m_prev); }
	ScopedPluginIdGuard(const ScopedPluginIdGuard&) = delete;
	ScopedPluginIdGuard& operator=(const ScopedPluginIdGuard&) = delete;
private:
	std::string m_prev;
};

#endif // _IB_PLUGIN_MANAGER_H_
