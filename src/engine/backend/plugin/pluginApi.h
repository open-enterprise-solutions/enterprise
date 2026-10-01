/////////////////////////////////////////////////////////////////////////////
// OES plugin ABI — minimal C interface a third-party DLL must export to be
// recognised as an OES plugin. Plain C so the boundary stays ABI-stable
// across MSVC/Clang/GCC and different STL versions.
//
// A candidate DLL is considered a plugin when:
//   1. GetProcAddress(dll, "oes_plugin_info") returns non-null.
//   2. The returned ibPluginInfo's abi_version == IB_PLUGIN_ABI_VERSION.
//
// Anything else (system DLL, vendor runtime, ibBackendException catch etc.)
// fails one of these two checks and is skipped silently.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_API_H_
#define _IB_PLUGIN_API_H_

#ifdef __cplusplus
extern "C" {
#endif

// Bump when the ABI changes incompatibly. Plugins compiled against an older
// number are rejected by the loader.
//
// 2 (2026-08-04) — hostContext stopped being NULL. A plugin now receives
// ibPluginHost and asks it for the capabilities it needs by name and version.
#define IB_PLUGIN_ABI_VERSION 2

// WHAT THE HOST HANDS A PLUGIN — a bootstrap, not a surface.
//
// The whole platform is C++ and none of it can cross a DLL boundary safely:
// wxString, std::vector and virtual layouts all depend on the compiler and the
// build flags, and this project ships three toolchains. So the boundary is
// exactly this struct — plain C, one function — and everything beyond it is
// requested explicitly:
//
//   const void* diag = host->query(host, "diagnostics", 1);
//
// `query` returns NULL when the capability is unknown or the requested version
// is not the one the host implements. A plugin that asked for something it did
// not get is expected to refuse to initialise, not to guess.
//
// The pointer it returns is a C++ abstract class from pluginHost.h. That is
// deliberate and it is the ONE unsafe step, confined to one place: a plugin
// that uses a capability must be built with the same toolchain as backend.
// Capabilities are named and versioned precisely so this is checkable — the
// host can retire "metadata" v1 and offer v2 without touching any plugin that
// only ever asked for "diagnostics".
typedef struct ibPluginHost_s ibPluginHost;

struct ibPluginHost_s {
	int abi_version;   // equals IB_PLUGIN_ABI_VERSION of the HOST

	// Ask for a capability. NULL when unknown / version mismatch.
	const void* (*query)(const ibPluginHost* self, const char* capability, int version);
};

typedef struct ibPluginInfo_s {
	int         abi_version;   // must equal IB_PLUGIN_ABI_VERSION
	const char* name;          // short identifier, e.g. "Excel Export"
	const char* version;       // free-form, e.g. "1.0.0"
	const char* description;   // user-facing summary, one line
	const char* vendor;        // optional, may be NULL
} ibPluginInfo;

// Required exports. Prototypes are typedefs so the host can GetProcAddress
// them without including wx/anything.
typedef const ibPluginInfo* (*ibPluginInfoFn)(void);

// Optional: called once after a successful info check. `host` is an
// ibPluginHost* — ask it for capabilities (see above). Never NULL as of ABI 2.
// Return 0 on success; non-zero aborts plugin load, and the plugin is unloaded
// without its shutdown being called (it never finished starting).
typedef int  (*ibPluginInitializeFn)(void* host);

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
