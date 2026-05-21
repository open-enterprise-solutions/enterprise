/////////////////////////////////////////////////////////////////////////////
// OES plugin ABI — minimal C interface a third-party DLL must export to be
// recognised as an OES plugin. Plain C so the boundary stays ABI-stable
// across MSVC/Clang/GCC and different STL versions.
//
// A candidate DLL is considered a plugin when:
//   1. GetProcAddress(dll, "oes_plugin_info") returns non-null.
//   2. The returned ibPluginInfo's abi_version is recognised by the host
//      (currently 1 or 2).
//
// ABI v1 plugins receive a NULL host pointer on initialize and live in
// passive mode (load / info / shutdown only). ABI v2 plugins receive a
// const ibHostAPI* host giving them access to BSL builtin registration,
// menu items, lifecycle events, and the message log.
//
// Anything else (system DLL, vendor runtime, etc.) fails one of these
// checks and is skipped silently.
//
// See docs/adr/0002-plugin-host-api.md for the ABI v2 spec, migration
// path, threading rules, and ownership semantics.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_API_H_
#define _IB_PLUGIN_API_H_

#ifdef __cplusplus
extern "C" {
#endif

// Bump on every incompatible change. Plugins compiled against a number
// the host does not recognise are rejected by the loader.
//
//   1 — initial release. initialize(void* host) called with host = NULL.
//   2 — initialize(const ibHostAPI* host) gets a non-NULL callback
//       table. RegisterFunction(const char*, ibPluginFunctionFn).
//   3 — RegisterFunction grew an `int paramCount` parameter so the
//       host's script-side dispatch can bind a builtin with a known
//       arity. Calling-convention change at the same struct slot;
//       v2 plugins must be rebuilt against the v3 header.
//   4 — Additive: appended WebView pane host, AI-provider registry,
//       and metadata mutation entries to ibHostAPI. v3 plugins keep
//       loading; they just don't see the new slots. Existing fields
//       are NOT reordered — additions live strictly at the struct
//       tail per ABI rule.
#define IB_PLUGIN_ABI_VERSION 4

typedef struct ibPluginInfo_s {
	int         abi_version;   // must equal a value the host recognises
	const char* name;          // short identifier, e.g. "Excel Export"
	const char* version;       // free-form, e.g. "1.0.0"
	const char* description;   // user-facing summary, one line
	const char* vendor;        // optional, may be NULL
} ibPluginInfo;

// Opaque ibValue handle. Allocated by host via Make*; ownership stays
// with the host. Function-call arguments + return slot are valid only
// for the duration of a single RegisterFunction callback — callers must
// not stash pointers across calls.
typedef struct ibPluginValue_s ibPluginValue;

// Callback signatures.
//
// ibPluginFunctionFn:
//   `args`   — array of length `argc`; each element host-managed.
//   `argc`   — argument count.
//   `ret`    — out-parameter; assign a Make* return value here.
//   Returns 0 on success; any other value translates to a script error
//   at the call site (ibBackendException).
typedef int  (*ibPluginFunctionFn)(ibPluginValue** args, int argc, ibPluginValue** ret);

// ibPluginEventFn — `payload` may be NULL for parameterless events.
// Both pointers are valid only inside the callback.
typedef void (*ibPluginEventFn)(const char* event, ibPluginValue* payload);

// ibPluginMenuFn — no parameters; plugin reads its own static state.
// Called from the Designer UI thread.
typedef void (*ibPluginMenuFn)(void);

// ibPluginWebMsgFn — invoked by the host when a WebView posts a
// message to the plugin via `window.sigma.postMessage(json)`. The
// callback is called from the Designer UI thread; the plugin must
// spawn its own worker for blocking I/O.
//   `paneId`     — id registered via RegisterWebPane.
//   `jsonInline` — UTF-8 string payload (no terminator length, NUL-terminated).
typedef void (*ibPluginWebMsgFn)(const char* paneId, const char* jsonInline, void* userData);

// ibPluginAIProvider — fed to RegisterAIProvider once per plugin.
// `supportedModes` is a NULL-terminated array of strings drawn from
// {"chat", "agent", "helper"}.
typedef struct ibPluginAIProvider_s {
	const char*  providerId;        // unique stable id, e.g. "vendor.sigma"
	const char*  displayName;       // e.g. "Sigma"
	const char*  iconPath;          // optional absolute path
	const char** supportedModes;    // NULL-terminated
	int  (*Query)(const char* requestJson,
	              const char* requestId,
	              void*       userData);
	int  (*Cancel)(const char* requestId, void* userData);
	int  (*ListModels)(char** jsonOut); // host frees via free()
	void* userData;
} ibPluginAIProvider;

// Metadata-mutation entry points — Mode 2 (Agent) only. All four run
// inside the active Designer document's wxCommandProcessor so a
// single Ctrl+Z reverts the whole agent turn.
//   `objectKind`     — "Catalog", "Document", "Form", …
//   `fullName`       — e.g. "Catalog.Контрагенти"
//   `propertiesJson` — serialised property bag
//   `jsonPatch`      — RFC 6902 JSON Patch document
//   `errorMsg` / `jsonOut` — out-parameters; host allocates, caller frees via free().
typedef int (*ibPluginMetaCreateFn)(const char* objectKind, const char* fullName,
                                     const char* propertiesJson, char** errorMsg);
typedef int (*ibPluginMetaEditFn)(const char* fullName, const char* jsonPatch,
                                    char** errorMsg);
// MetaDelete ABI signature carries propertiesJson so plugin callers
// can ship the {"force":true} opt-in required for irreversible ops
// (rm-without-preserve-root convention). Without it the call refuses
// regardless of policy elevation. propertiesJson may be NULL when the
// caller has no extra payload — equivalent to {"force":false}.
typedef int (*ibPluginMetaDeleteFn)(const char* fullName,
                                      const char* propertiesJson,
                                      char** errorMsg);
typedef int (*ibPluginMetaQueryFn)(const char* fullName, const char* fieldsFilter,
                                     char** jsonOut, char** errorMsg);

// Plugin lock-denied error code returned by Meta* fns when the
// target object is exclusive-locked by another user.
#define IB_PLUGIN_LOCK_DENIED 0x0001

// Permission-denied error code returned by Meta* fns when the active
// per-(plugin,op) mutation policy is Ask or Deny. Plugins should show
// the user a confirmation prompt and either re-call with the policy
// promoted to AllowSession/AllowAlways, or back off entirely.
#define IB_PLUGIN_PERMISSION_DENIED 0x0002

// Returned by Meta* fns whose policy gate passed but whose body is
// still a Phase 3.3 stub. Distinct from -1 so plugins can treat it as
// "feature pending" rather than retrying the policy elevation loop.
#define IB_PLUGIN_NOT_IMPLEMENTED 0x0003

// Host-side API table. Function pointers are valid for the lifetime of
// the plugin (from initialize until just before shutdown). The struct
// layout is part of the ABI: fields MUST NOT be reordered or removed
// between releases; new fields are appended and gated by the
// IB_PLUGIN_ABI_VERSION bump.
typedef struct ibHostAPI_s {
	// Register a BSL/CES builtin callable from script as `<name>(...)`.
	// Identifier rules: ASCII letters / digits / underscore, must not
	// start with a digit. Duplicate names overwrite the prior binding.
	// `paramCount` is the exact arity the script call must satisfy;
	// use -1 to mark variadic. Returns 0 on success.
	int (*RegisterFunction)(const char* name, int paramCount, ibPluginFunctionFn fn);

	// Append an item to the Designer's Tools → Plugins submenu.
	// Plugins running outside Designer (codeRunner, daemon,
	// wenterprise-server) accept the call and return 0 without effect.
	int (*RegisterMenuItem)(const char* label, ibPluginMenuFn handler);

	// Subscribe to a lifecycle event. Recognised names:
	//   "DocumentSaved"     — wxDocument::Save returned success
	//   "BeforeRun"         — debugger session about to start
	//   "AfterCompile"      — bytecode compilation finished
	//   "ConfigLoaded"      — configuration finished loading
	//   "BeforePublish"     — designer about to push config to DB
	//   "AfterAcidTest"     — Σ-Check / acid-test pass completed
	//   "MetadataMutated"   — metaobject field changed at runtime
	// Unrecognised event names are accepted but never fire (forward
	// compat). Returns 0 on success.
	int (*Subscribe)(const char* event, ibPluginEventFn cb);

	// Emit a message to the Designer Messages output (and the
	// equivalent log target in other run modes).
	//   severity: 0 = info, 1 = warning, 2 = error.
	// `msg` is interpreted as UTF-8.
	void (*Log)(const char* msg, int severity);

	// ibValue marshalling. Make* allocate a host-owned value that stays
	// alive until the surrounding callback returns. Plugins must not
	// free the returned pointer and must not carry it across callbacks.
	ibPluginValue* (*MakeString)(const char* utf8);
	ibPluginValue* (*MakeNumber)(double n);
	ibPluginValue* (*MakeBool)(int b);
	ibPluginValue* (*MakeNull)(void);

	// Read accessors. GetString returns a NUL-terminated UTF-8 buffer
	// valid for the rest of the callback. GetNumber / GetBool coerce
	// on type mismatch (matching script call-site behaviour). IsNull
	// returns non-zero when the value is Undefined or Null.
	const char*    (*GetString)(const ibPluginValue*);
	double         (*GetNumber)(const ibPluginValue*);
	int            (*GetBool)(const ibPluginValue*);
	int            (*IsNull)(const ibPluginValue*);

	// --- ABI v4 additions — appended; existing v3 layout preserved -----------

	// WebView host. Plugins implementing the Sigma chat UI register
	// a pane id + initial HTML; the kernel creates a wxAuiPane around
	// a wxWebView, loads the HTML, and routes script messages back to
	// `onMessage`. Returns 0 on success, non-zero when the pane id is
	// already registered or the host couldn't allocate.
	int  (*RegisterWebPane)(const char* paneId,
	                         const char* title,
	                         const char* htmlBundlePath,
	                         ibPluginWebMsgFn onMessage,
	                         void* userData);

	// Plugin → WebView push channel. `jsonInline` is serialised once
	// and forwarded to the JS side via `wxWebView::RunScript`. Safe
	// from any thread; the host posts a wxThreadEvent if not on UI.
	int  (*WebPaneSend)(const char* paneId, const char* jsonInline);

	// Force the pane visible (creates the wxAuiPane on first call if
	// it isn't shown yet). Returns 0 on success.
	int  (*WebPaneShow)(const char* paneId);

	// AI provider registry. The plugin announces itself as a query
	// provider; the kernel routes user prompts to whichever provider
	// the customer has enabled in Settings.
	int  (*RegisterAIProvider)(const ibPluginAIProvider* provider);

	// Streaming response channel — provider drives these on its
	// worker thread; the host marshals to UI via wxQueueEvent.
	//   AIChunkEmit  — assistant produced `deltaJson` payload.
	//   AIChunkEnd   — end of stream; `metaJson` carries model + tokens.
	//   AIChunkError — fatal error; `errorJson` is rendered inline.
	int  (*AIChunkEmit)(const char* requestId, const char* deltaJson);
	int  (*AIChunkEnd)(const char* requestId, const char* metaJson);
	int  (*AIChunkError)(const char* requestId, const char* errorJson);

	// Metadata mutation surface — Mode 2 (Agent). Each mutation runs
	// inside the active wxDocument's wxCommandProcessor so Ctrl+Z
	// reverts the whole agent turn atomically.
	ibPluginMetaCreateFn  MetaCreate;
	ibPluginMetaEditFn    MetaEdit;
	ibPluginMetaDeleteFn  MetaDelete;
	ibPluginMetaQueryFn   MetaQuery;

	// Free a buffer the host allocated via MetaQuery/MetaCreate/MetaEdit/
	// MetaDelete out-parameters (jsonOut, errorMsg). The plugin MUST call
	// this — calling plain free() crashes on Windows when host and plugin
	// link different CRTs (the canonical mismatched-heap footgun). Safe
	// to call with NULL.
	void (*FreeBuffer)(void* buf);

	// Read a BYOK secret from the plugin's own <pluginId>.env file.
	// The calling plugin's id is resolved from a host-side thread-local
	// scope (set during oes_plugin_initialize + provider Query); plugins
	// cannot read each other's keys. Returns a malloc'd UTF-8 buffer
	// the plugin MUST free via FreeBuffer; returns NULL when the key
	// is absent or the caller has no resolvable plugin scope.
	char* (*ReadPluginEnv)(const char* key);
} ibHostAPI;

// Required export. The DLL announces itself via this fn — abi_version
// inside the returned struct controls which initialize signature is
// invoked.
typedef const ibPluginInfo* (*ibPluginInfoFn)(void);

// Optional. Called once after a successful info check.
//
//   v1 plugins (abi_version == 1): the host pointer is NULL — same
//   contract as ABI v1's `void* hostContext` parameter.
//   v2 plugins (abi_version == 2): the host pointer is non-NULL and
//   points to an ibHostAPI table the plugin may capture in static
//   state for later use.
//
// Return 0 on success; non-zero aborts plugin load.
typedef int  (*ibPluginInitializeFn)(const ibHostAPI* host);

// Optional: called once before the DLL is unloaded.
typedef void (*ibPluginShutdownFn)(void);

#ifdef _WIN32
  #define OES_PLUGIN_EXPORT __declspec(dllexport)
#else
  #define OES_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif

#endif // _IB_PLUGIN_API_H_
