# Plugins — the capability boundary

> **Status (2026-08-04): ABI 2 landed.** A plugin no longer just loads and says hello — it receives
> the host and asks it for what it needs. Three capabilities exist today: `diagnostics`, `script`,
> `metadata`. The reference plugin (`simplePlugin`) uses two of them and refuses to load without
> them.

---

## 1. What a plugin is here

A DLL (or `.so`) in `<exe-dir>/plugins/` that exports three C functions. `ibPluginManager` scans
that directory at start-up, and a candidate is treated as a plugin when
`GetProcAddress("oes_plugin_info")` answers and the returned `abi_version` matches the host's.
Everything else — a system DLL, a vendor runtime — fails one of those two checks and is skipped
silently.

```c
OES_PLUGIN_EXPORT const ibPluginInfo* oes_plugin_info(void);
OES_PLUGIN_EXPORT int                 oes_plugin_initialize(void* host);   // 0 = loaded
OES_PLUGIN_EXPORT void                oes_plugin_shutdown(void);
```

Only the first is required. A non-zero `initialize` aborts the load, and **shutdown is then not
called** — the plugin never finished starting, so it has nothing to tear down.

---

## 2. The boundary — one C struct, everything else by request

Before ABI 2 the host passed `NULL` and the comment said "future versions may pass
ibApplicationData". Passing `ibApplicationData` was never going to work: it is a C++ class, and a
C++ class crossing a DLL boundary means the plugin must be built with the same compiler, the same
standard library and the same flags — while this project ships three toolchains.

So the boundary is exactly this:

```c
struct ibPluginHost_s {
    int abi_version;
    const void* (*query)(const ibPluginHost* self, const char* capability, int version);
};
```

Plain C, one function. Everything a plugin actually uses is **requested by name and version**:

```cpp
auto* diag = static_cast<const ibPluginDiagnostics*>(
    host->query(host, ibCapabilityDiagnostics, 1));
```

`query` answers `NULL` for an unknown name or a version the host does not implement. A plugin that
asked for something and did not get it is expected to **refuse to initialise**, not to degrade: a
half-loaded plugin fails later, somewhere else, as behaviour nobody can explain.

**Why named capabilities and not one host-services class.** One class means every plugin links
against every part of the platform, and adding a method changes the layout for all of them. Named
capabilities make the dependency explicit and versioning honest — `metadata` can reach v2 while
`diagnostics` stays v1, and a plugin that only ever asked for the second keeps working untouched.

⚠️ **The one unsafe step, deliberately confined.** What `query` returns is a C++ abstract class
(`pluginHost.h`). A plugin that uses a capability therefore links `backend.lib` and must be built
with the same toolchain as `backend.dll`. That is the price of not hand-writing a C wrapper for
every method; the naming and versioning exist so the price is *checkable* rather than discovered at
run time.

---

## 3. The capabilities

### `diagnostics` (v1)

Subscribe to failures as data — `ibDiagnostic` (see
[compiler-pipeline.md §7](compiler-pipeline.md)): kind (compile / runtime), module guid, module
name, line, position, error code, message, source line, call stack.

⚠️ Unsubscribe in `shutdown`, before the DLL goes away. The host does not track which sink came
from which plugin, and a sink that outlives its code is a call into freed memory on the next error.

### `script` (v1)

```cpp
std::vector<ibDiagnostic> Check(const wxString& text, const wxString& moduleName) const;
```

Compiles the text with the same compiler the designer uses and **throws the result away**: nothing
is registered, no module is replaced, the open configuration never learns it happened. An empty
answer means the text compiles.

This is the call that closes the loop for a code-generating assistant: it can find its own mistakes
in a language that exists nowhere but here, without running anything and without a person in the
middle.

### `metadata` (v1)

`IsConfigurationOpen()` · `List(kind)` · `Describe(kind, name)` → JSON.

The *kind* is resolved through the registry every metatype registers itself in
(`METADATA_TYPE_REGISTER` → `ibValue::RegisterCtor`), so `"Catalog"` here is the same `"Catalog"` a
configuration writes — by construction, not by a table somebody keeps in step. A name that is not a
metatype (a value class, a control) resolves to nothing: asking for `"Array"` must not return every
array in the tree.

⚠️ **Read only.** The JSON view is lossy by design (see the note on `ibJsonProvider`) and is not a
way back into the metadata.

---

## 4. Traps found while building this

- **`s_host` is a winsock macro.** `winsock2.h` defines `s_host` as `S_un.S_un_b.s_b2`, and wx
  drags winsock in. A file-static named `s_host` compiles into someone else's struct member, and
  the errors point at the Windows SDK rather than at your line.
- **Include order in a plugin.** `pluginHost.h` pulls in wx → winsock2; a plugin's own header
  usually pulls in `windows.h` → the original winsock. Whichever arrives second redefines the
  first. Platform headers last.
- **Export follows the declaration the definition can see.** `ibPluginHostInstance` was declared
  `BACKEND_API` in one header and defined in a file that did not include it — so it compiled
  without the export attribute and nothing outside `backend.dll` could link it. The declaration now
  lives in `pluginHost.h`, next to what it returns.

---

## 5. What this is for

The first consumer is the language service an AI assistant talks to: `script` to check generated
code, `metadata` to know what exists, `diagnostics` to hear failures as they happen. Building it as
a plugin rather than inside the engine is deliberate — it keeps the assistant-facing surface
optional, replaceable, and out of the core.
