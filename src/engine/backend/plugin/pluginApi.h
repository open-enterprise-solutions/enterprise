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
#define IB_PLUGIN_ABI_VERSION 3

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
