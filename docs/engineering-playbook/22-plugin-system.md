# 22. Plugin System (C++ Plugin Architecture)

## When it applies

This section is relevant for every OES component that needs to support:
- Third-party data source connectors (DBMS, REST API, file formats)
- Custom widgets and form designer components
- Report exporters (PDF, Excel, XML, HTML)
- Authentication providers (LDAP, SAML, OAuth)
- User actions (scripting hooks) in business logic

---

## Architectural principles

### The golden rule of ABI stability

> **The public C plugin interface NEVER changes without bumping the major ABI version. Violating this rule causes crashes when loading incompatible plugins.**

### Key constraints of C++ plugins

| Problem | Cause | Solution |
|----------|---------|---------|
| C++ ABI mismatch | Different compilers/versions | `extern "C"` exports |
| Different CRTs (Windows) | Static/dynamic linkage | Single `MSVCRT` in the contract |
| Exceptions across DLL boundary | Incompatible RTTI | Do not propagate exceptions |
| Different `std::string` layouts | ABI between STL implementations | Pass `const char*` only |
| wxWidgets objects across boundary | wxWidgets version | Only C primitives in the API |

### Architecture layers

```
┌────────────────────────────────────────────────────────────┐
│  OES Core (application kernel)                             │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  PluginManager                                       │  │
│  │  - Loads/unloads DLL/SO                             │  │
│  │  - Verifies ABI version                              │  │
│  │  - Registry of loaded plugins                        │  │
│  └────────────────┬────────────────────────────────────┘  │
│                   │ C ABI (extern "C")                      │
├───────────────────┼────────────────────────────────────────┤
│  Plugin Interface │ (oes_plugin_api.h — public header)      │
│  IOesPlugin (pure │ virtual through C wrappers)             │
├───────────────────┼────────────────────────────────────────┤
│  Plugin Instance  │ (DLL/SO — third-party code)             │
│  MyDataConnector  │ : IOesPlugin                            │
└────────────────────────────────────────────────────────────┘
```

---

## Public Plugin API (oes_plugin_api.h)

```cpp
// include/oes_plugin_api.h
// PUBLIC HEADER — part of the stable ABI
// Version: changes to MAJOR break compatibility
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── ABI version ─────────────────────────────────────────────────────────────
#define OES_PLUGIN_ABI_MAJOR 2
#define OES_PLUGIN_ABI_MINOR 0

// ─── Plugin types ────────────────────────────────────────────────────────────
typedef enum OesPluginType {
    OES_PLUGIN_DATA_SOURCE   = 1,  // Data source (DBMS, file, REST)
    OES_PLUGIN_REPORT_EXPORT = 2,  // Report exporter
    OES_PLUGIN_WIDGET        = 3,  // Custom UI widget
    OES_PLUGIN_AUTH_PROVIDER = 4,  // Authentication provider
    OES_PLUGIN_SCRIPT_HOOK   = 5,  // Business logic hook
} OesPluginType;

// ─── Result codes ────────────────────────────────────────────────────────────
typedef enum OesResult {
    OES_OK              = 0,
    OES_ERROR           = 1,
    OES_ERROR_NOT_FOUND = 2,
    OES_ERROR_INVALID   = 3,
    OES_ERROR_IO        = 4,
    OES_ERROR_AUTH      = 5,
} OesResult;

// ─── Plugin metadata ─────────────────────────────────────────────────────────
typedef struct OesPluginMeta {
    uint32_t    abiMajor;       // Must equal OES_PLUGIN_ABI_MAJOR
    uint32_t    abiMinor;
    OesPluginType type;
    const char* id;             // "com.example.my-connector" — unique ID
    const char* name;           // "My DB Connector"
    const char* version;        // "1.2.0"
    const char* author;
    const char* licenseType;    // "LGPL-2.1", "Commercial", "MIT"
} OesPluginMeta;

// ─── Log levels ──────────────────────────────────────────────────────────────
typedef enum OesLogLevel {
    OES_LOG_DEBUG   = 0,
    OES_LOG_INFO    = 1,
    OES_LOG_WARNING = 2,
    OES_LOG_ERROR   = 3,
} OesLogLevel;

// ─── Host context (what the core provides to the plugin) ─────────────────────
typedef struct OesHostContext {
    // Logging (plugin uses without owning a logger)
    void (*logMessage)(void* hostData, OesLogLevel level, const char* message);

    // Access plugin settings from OES storage
    const char* (*getSetting)(void* hostData, const char* key);

    void* hostData;  // Opaque pointer to the core
} OesHostContext;

// ─── Base plugin interface (function table) ──────────────────────────────────
typedef struct OesPluginVTable {
    // Initialize with the host context. Returns OES_OK on success.
    OesResult (*initialize)(void* plugin, const OesHostContext* host);

    // Release resources. Always called before unloading the DLL.
    void      (*shutdown)(void* plugin);

    // Return plugin metadata.
    const OesPluginMeta* (*getMeta)(void* plugin);
} OesPluginVTable;

typedef struct OesPlugin {
    const OesPluginVTable* vtable;
    void*                  impl;   // Opaque implementation pointer
} OesPlugin;

// ─── Required DLL exports ────────────────────────────────────────────────────

// Create a plugin instance. Called by the core after the DLL is loaded.
// The DLL owns the object until OesDestroyPlugin is called.
#ifdef _WIN32
#  define OES_EXPORT __declspec(dllexport)
#else
#  define OES_EXPORT __attribute__((visibility("default")))
#endif

typedef OesPlugin* (*OesCreatePluginFn)(void);
typedef void       (*OesDestroyPluginFn)(OesPlugin*);

// Function names for GetProcAddress / dlsym
#define OES_FN_CREATE_PLUGIN  "OesCreatePlugin"
#define OES_FN_DESTROY_PLUGIN "OesDestroyPlugin"

#ifdef __cplusplus
}  // extern "C"
#endif
```

---

## PluginManager (OES core)

```cpp
// src/plugin_manager.h
#pragma once
#include "oes_plugin_api.h"
#include <wx/dynlib.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

struct LoadedPlugin {
    std::unique_ptr<wxDynamicLibrary> library;
    OesPlugin*        plugin    = nullptr;
    OesDestroyPluginFn destroyFn = nullptr;
    std::string       path;
    std::string       id;   // duplicated for convenient reverse traversal
};

class PluginManager {
public:
    explicit PluginManager(OesHostContext hostContext);
    ~PluginManager();

    // Load one plugin from a file (DLL/SO)
    bool LoadPlugin(const std::string& path, std::string& errorOut);

    // Load every plugin in a directory
    int  LoadDirectory(const std::string& dir);

    // Unload a specific plugin by ID
    void UnloadPlugin(const std::string& pluginId);

    // List loaded plugins
    std::vector<const OesPluginMeta*> GetAllMeta() const;

    // Find a plugin by ID and type
    OesPlugin* FindPlugin(const std::string& id,
                          OesPluginType type) const;

private:
    OesHostContext m_hostContext;
    std::unordered_map<std::string, LoadedPlugin> m_plugins;
    // Load order — needed to unload in reverse order
    std::vector<std::string> m_loadOrder;

    bool CheckAbiCompatibility(const OesPluginMeta* meta,
                                std::string& errorOut);
};
```

```cpp
// src/plugin_manager.cpp
#include "plugin_manager.h"
#include <wx/filename.h>
#include <wx/dir.h>

PluginManager::PluginManager(OesHostContext hostContext)
    : m_hostContext(hostContext)
{}

PluginManager::~PluginManager() {
    // Unload STRICTLY in reverse load order:
    // plugins may depend on others loaded earlier,
    // so iterating an unordered_map is wrong — order is undefined.
    for (auto it = m_loadOrder.rbegin(); it != m_loadOrder.rend(); ++it) {
        auto mapIt = m_plugins.find(*it);
        if (mapIt == m_plugins.end()) continue;
        LoadedPlugin& lp = mapIt->second;
        if (lp.plugin && lp.destroyFn) {
            if (lp.plugin->vtable)
                lp.plugin->vtable->shutdown(lp.plugin->impl);
            lp.destroyFn(lp.plugin);
        }
    }
    m_plugins.clear();
    m_loadOrder.clear();
    // wxDynamicLibrary is freed by the unique_ptr destructor
}

bool PluginManager::LoadPlugin(const std::string& path,
                                std::string& errorOut)
{
    auto lib = std::make_unique<wxDynamicLibrary>();
    if (!lib->Load(path, wxDL_NOW | wxDL_QUIET)) {
        errorOut = "Failed to load library: " + path;
        return false;
    }

    // Get factory functions
    auto createFn = reinterpret_cast<OesCreatePluginFn>(
        lib->GetSymbol(OES_FN_CREATE_PLUGIN));
    auto destroyFn = reinterpret_cast<OesDestroyPluginFn>(
        lib->GetSymbol(OES_FN_DESTROY_PLUGIN));

    if (!createFn || !destroyFn) {
        errorOut = "Plugin does not export " OES_FN_CREATE_PLUGIN
                   " / " OES_FN_DESTROY_PLUGIN;
        return false;
    }

    OesPlugin* plugin = createFn();
    if (!plugin || !plugin->vtable) {
        errorOut = "OesCreatePlugin returned nullptr";
        return false;
    }

    // Verify ABI compatibility
    const OesPluginMeta* meta = plugin->vtable->getMeta(plugin->impl);
    if (!CheckAbiCompatibility(meta, errorOut)) {
        destroyFn(plugin);
        return false;
    }

    // Initialize the plugin
    OesResult result = plugin->vtable->initialize(
        plugin->impl, &m_hostContext);
    if (result != OES_OK) {
        errorOut = std::string("Plugin ")
                   + meta->id + " initialization failed with code: "
                   + std::to_string(result);
        destroyFn(plugin);
        return false;
    }

    LoadedPlugin lp;
    lp.library   = std::move(lib);
    lp.plugin    = plugin;
    lp.destroyFn = destroyFn;
    lp.path      = path;
    lp.id        = meta->id;

    m_loadOrder.push_back(meta->id);        // remember the load order
    m_plugins[meta->id] = std::move(lp);
    return true;
}

bool PluginManager::CheckAbiCompatibility(
    const OesPluginMeta* meta, std::string& errorOut)
{
    if (!meta) {
        errorOut = "getMeta() returned nullptr";
        return false;
    }

    if (meta->abiMajor != OES_PLUGIN_ABI_MAJOR) {
        errorOut = std::string("Plugin ABI version mismatch for ")
                   + meta->id + ": expected major="
                   + std::to_string(OES_PLUGIN_ABI_MAJOR)
                   + ", got major="
                   + std::to_string(meta->abiMajor);
        return false;
    }

    return true;
}

int PluginManager::LoadDirectory(const std::string& dir) {
    int loaded = 0;
    wxDir wxdir(dir);
    if (!wxdir.IsOpened())
        return 0;

#if defined(_WIN32)
    const wxString mask = "*.dll";
#elif defined(__APPLE__)
    const wxString mask = "*.dylib";
#else
    const wxString mask = "*.so";
#endif

    wxString filename;
    bool found = wxdir.GetFirst(&filename, mask, wxDIR_FILES);
    while (found) {
        std::string fullPath = dir + "/" + filename.ToStdString();
        std::string err;
        if (LoadPlugin(fullPath, err))
            ++loaded;
        else
            wxLogWarning("Plugin '%s' failed to load: %s",
                         fullPath, err);
        found = wxdir.GetNext(&filename);
    }
    return loaded;
}
```

---

## Plugin implementation (third-party code)

```cpp
// my_plugin/my_data_connector.cpp
// Example plugin — a connector to an external REST API

#include "oes_plugin_api.h"
#include <string>
#include <cstring>
#include <cstdlib>

// ─── Plugin data ─────────────────────────────────────────────────────────────
static const OesPluginMeta g_meta = {
    OES_PLUGIN_ABI_MAJOR,     // abiMajor — MUST match the core
    OES_PLUGIN_ABI_MINOR,     // abiMinor
    OES_PLUGIN_DATA_SOURCE,   // type
    "com.example.rest-connector",
    "REST API Connector",
    "1.0.0",
    "Example Corp",
    "LGPL-2.1",
};

struct MyConnectorImpl {
    const OesHostContext* host = nullptr;
    std::string           baseUrl;
};

// ─── VTable functions ────────────────────────────────────────────────────────
static OesResult Initialize(void* impl, const OesHostContext* host) {
    auto* self = static_cast<MyConnectorImpl*>(impl);
    self->host = host;

    const char* url = host->getSetting(host->hostData, "rest_base_url");
    if (!url || url[0] == '\0') {
        host->logMessage(host->hostData, OES_LOG_WARNING,
            "rest_base_url not set — using the default value");
        self->baseUrl = "https://api.example.com";
    } else {
        self->baseUrl = url;
    }

    host->logMessage(host->hostData, OES_LOG_INFO,
        "REST Connector initialized");
    return OES_OK;
}

static void Shutdown(void* impl) {
    // Close connections, release resources
    auto* self = static_cast<MyConnectorImpl*>(impl);
    // ...
    (void)self;
}

static const OesPluginMeta* GetMeta(void* /*impl*/) {
    return &g_meta;
}

static const OesPluginVTable g_vtable = {
    &Initialize,
    &Shutdown,
    &GetMeta,
};

// ─── Required exports ────────────────────────────────────────────────────────
extern "C" {

OES_EXPORT OesPlugin* OesCreatePlugin() {
    auto* impl   = new MyConnectorImpl();
    auto* plugin = new OesPlugin();
    plugin->vtable = &g_vtable;
    plugin->impl   = impl;
    return plugin;
}

OES_EXPORT void OesDestroyPlugin(OesPlugin* plugin) {
    if (!plugin) return;
    delete static_cast<MyConnectorImpl*>(plugin->impl);
    delete plugin;
}

}  // extern "C"
```

---

## Versioning and compatibility

### Versioning rules

| Change type | Action | Compatibility |
|---------------|---------|---------------|
| Add a new VTable field | Bump MINOR | Old plugins still work |
| Change a VTable function signature | Bump MAJOR | Every old plugin breaks |
| Add a new plugin type (OesPluginType) | Bump MINOR | Old plugins still work |
| Change the OesPlugin struct layout | Bump MAJOR | Every old plugin breaks |
| Change plugin implementation (not API) | Bump plugin version | Core untouched |

### Backward-compatible VTable extension

```cpp
// oes_plugin_api_v2_1.h — MINOR extension
// New fields are added ONLY at the end of the struct
typedef struct OesPluginVTable {
    // v2.0
    OesResult (*initialize)(void*, const OesHostContext*);
    void      (*shutdown)(void*);
    const OesPluginMeta* (*getMeta)(void*);

    // v2.1 — new fields at the end, pointers may be NULL for older plugins
    OesResult (*configure)(void*, const char* key, const char* value);
    const char* (*getCapabilities)(void*);  // JSON capability string
} OesPluginVTable;

// In the core: safe call of the new method
inline OesResult SafeConfigure(OesPlugin* p,
                                const char* key, const char* value)
{
    if (p->vtable->configure)  // check before calling
        return p->vtable->configure(p->impl, key, value);
    return OES_ERROR_NOT_FOUND;
}
```

### Compatibility matrix

```
OES core  │  Plugin v2.0  │  Plugin v2.1  │  Plugin v1.x  │  Plugin v3.x
──────────┼───────────────┼───────────────┼───────────────┼──────────────
  v2.0    │       yes     │       yes*    │       no      │       no
  v2.1    │       yes**   │       yes     │       no      │       no
  v3.0    │       no      │       no      │       no      │       yes

* Plugin v2.1 fills new VTable fields (configure, getCapabilities).
  Core v2.0 doesn't know about them — it simply doesn't call them, fields
  past the end of the struct are invisible to the core. Plugin works fine.

** Core v2.1 calls new methods only after a nullptr check on the pointer
  (see SafeConfigure above). Plugin v2.0 has nullptr in the new VTable
  fields (zero-initialized in C), so the core correctly skips the call.
  RESPONSIBILITY for the nullptr check lies with the CORE, not the plugin.
```

---

## Plugin directories

### File system layout

```
%PROGRAMFILES%\OES\
├── oes.exe
├── oes_core.dll
├── plugins\                       ← system plugins
│   ├── oes_firebird_connector.dll
│   ├── oes_postgres_connector.dll
│   ├── oes_sqlite_connector.dll
│   └── oes_pdf_exporter.dll
└── ...

%APPDATA%\OES\plugins\            ← user plugins
    ├── my_custom_widget.dll
    └── rest_api_connector.dll
```

```cpp
// Load plugins at app startup
void OesApp::LoadPlugins() {
    OesHostContext host = CreateHostContext();
    m_pluginManager = std::make_unique<PluginManager>(host);

    // 1. System plugins
    wxString sysPluginsDir = wxStandardPaths::Get().GetPluginsDir()
                             + "/plugins";
    int sysLoaded = m_pluginManager->LoadDirectory(
        sysPluginsDir.ToStdString());

    // 2. User plugins (AppData)
    wxString userPluginsDir = wxStandardPaths::Get().GetUserDataDir()
                              + "/plugins";
    int userLoaded = m_pluginManager->LoadDirectory(
        userPluginsDir.ToStdString());

    wxLogInfo("Plugins loaded: system=%d, user=%d",
              sysLoaded, userLoaded);
}
```

---

## Plugin security

### Principles

1. **Plugins run in the same process** — no memory isolation. A plugin crash = the whole app crashes.
2. **Trust only signed plugins** (for enterprise deployments).
3. **Limit the API:** the plugin only gets what it needs, through `OesHostContext`.
4. **No direct pointers to internal core structures** — only through OesHostContext callbacks.

### Plugin signing (Enterprise)

```cpp
// Before loading a plugin — verify the digital signature
#ifdef OES_ENTERPRISE_PLUGIN_SIGNING
bool PluginManager::IsPluginTrusted(const std::string& path) {
#ifdef _WIN32
    return VerifyCodeSignature(
        std::wstring(path.begin(), path.end()));
#else
    // On Linux/macOS — verify the GPG signature of the .so file
    return VerifyGpgSignature(path + ".sig", path,
                               OES_TRUSTED_PUBLIC_KEY);
#endif
}
#endif
```

### Handling plugin failures

```cpp
// SEH protection around plugin calls (Windows)
// Isolates a plugin crash from the core
OesResult SafeCallPlugin(OesPlugin* plugin,
                          std::function<OesResult()> call)
{
#ifdef _WIN32
    __try {
        return call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        wxLogError("Plugin '%s' raised exception 0x%08X",
                   plugin->vtable->getMeta(plugin->impl)->id, code);
        return OES_ERROR;
    }
#else
    // On Linux — signal handler or setjmp/longjmp (limited safety)
    return call();
#endif
}
```

---

## Plugin developer checklist

### Before publishing a plugin:

- [ ] `abiMajor` matches the target OES core version
- [ ] All `extern "C"` exports present: `OesCreatePlugin`, `OesDestroyPlugin`
- [ ] No C++ exceptions escape the DLL boundary (all caught inside)
- [ ] `Shutdown()` releases every resource (RAII)
- [ ] No `std::string`, `std::vector` in the public ABI — only `const char*`, primitives
- [ ] Plugin doesn't keep pointers to core memory past `Shutdown()`
- [ ] Tested on every supported platform (Win/Linux/macOS)
- [ ] Logging via `OesHostContext::logMessage`, not `printf` / `std::cout`
- [ ] DLL/SO signed for enterprise distribution
- [ ] `licenseType` in metadata matches the actual license
- [ ] Documentation: description, settings (`getSetting` keys), dependencies

### Core checklist when adding a new API:

- [ ] New VTable fields appended ONLY at the end of the struct
- [ ] Bump MINOR ABI version on addition, MAJOR on breaking changes
- [ ] Core verifies `abiMajor` at load and rejects incompatible plugins
- [ ] New VTable methods called only after a `nullptr` check
- [ ] Public Plugin API CHANGELOG updated
