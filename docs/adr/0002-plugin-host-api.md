# ADR 0002 — Plugin ABI v3 host API (`ibHostAPI`)

- Status: **Accepted**
- Date: 2026-05-19
- Supersedes: ABI v1 / v2 passive plugin contract (no host callback table)

## Context

ABI v1 (`pluginApi.h:23`, `IB_PLUGIN_ABI_VERSION = 1`) shipped as a load-
or-skip contract — the host invoked `oes_plugin_info` + `oes_plugin_initialize`
with a NULL `hostContext` and then unloaded the library at shutdown. Plugins
could ship a banner string and nothing else; they had no way to register a
BSL builtin, hook a menu item, subscribe to a lifecycle event, or even log a
message into the designer's output pane.

That made plugins useless for the integration scenarios that motivated
shipping plugin support in the first place — most notably the Pugi
commercial AI integration that needs to register a `LLMQuery(...)` builtin
callable from configuration script, listen for `BeforePublish` so a
Σ-Check acid-test can gate the database write, and expose menu items
for the operator-facing migration tools.

## Decision

Bump to ABI v3 with a host-API callback table.

```c
#define IB_PLUGIN_ABI_VERSION 3

typedef struct ibHostAPI_s {
    int (*RegisterFunction)(const char* name, int paramCount,
                            ibPluginFunctionFn fn);
    int (*RegisterMenuItem)(const char* label, ibPluginMenuFn handler);
    int (*Subscribe)(const char* event, ibPluginEventFn cb);
    void (*Log)(const char* msg, int severity);

    /* Value marshalling — opaque ibPluginValue handles allocated by the
       host and freed by the host when the surrounding callback returns. */
    ibPluginValue* (*MakeString)(const char* utf8);
    ibPluginValue* (*MakeNumber)(double n);
    ibPluginValue* (*MakeBool)(int b);
    ibPluginValue* (*MakeNull)(void);
    const char*    (*GetString)(const ibPluginValue*);
    double         (*GetNumber)(const ibPluginValue*);
    int            (*GetBool)(const ibPluginValue*);
    int            (*IsNull)(const ibPluginValue*);
} ibHostAPI;

typedef int (*ibPluginInitializeFn)(const ibHostAPI* host);
```

### v1 → v3 migration path

The loader (`ibPluginManager::LoadAll`) accepts `info->abi_version` in
`[1 .. IB_PLUGIN_ABI_VERSION]`. v1/v2 plugins receive a NULL host and
remain functional in passive mode — they simply lack access to the new
surface. v3 plugins receive a non-NULL `const ibHostAPI*` and may stash
it in plugin-local static state. v2 plugins compiled against the prior
`RegisterFunction(const char*, ibPluginFunctionFn)` signature would
mis-call the v3 slot, which is why the version bump from v2 → v3 is
not source-compatible — Codex caught this on the v2 review and the bump
is a hard ABI break by design.

### Lifecycle

1. `oes_plugin_info()` — required, returns the manifest. Loader rejects on
   `abi_version` outside the accepted range.
2. `oes_plugin_initialize(host)` — optional. v3 plugins call
   `host->Register*` / `Subscribe` / `Log` from inside this call.
3. Plugin runs passively until the host invokes registered callbacks.
4. `oes_plugin_shutdown()` — optional, called before the library is
   unloaded. After this returns, no host callback fires for the plugin.

### Threading

All host callbacks fire on the main UI thread (designer's event loop).
Long-running plugin work (HTTP fetches, LLM calls) must spawn a worker
and post results back through `wxQueueEvent` or an equivalent thread-safe
mechanism. Synchronous blocking inside a callback freezes the UI.

### Memory ownership

`ibPluginValue*` returned from `MakeString` / `MakeNumber` / etc lives in
an `ibPluginCallScope` arena managed by the host. The arena is torn down
when the surrounding callback (initialize / event / function / menu handler)
returns. Plugins must NOT free the pointer, must NOT call `delete`, and must
NOT carry a pointer across callbacks. To return a value, assign the result
to `*ret` inside the `ibPluginFunctionFn` and the host will copy it into the
script call site before tearing down the arena.

### Recognised event names

- `DocumentSaved` — wxDocument::OnSaveDocument succeeded
- `BeforeRun` — debugger session about to start (any of the four debug paths)
- `AfterCompile` — bytecode compilation succeeded
- `ConfigLoaded` — configuration finished loading
- `BeforePublish` — designer about to push configuration changes to DB
- `AfterAcidTest` — RESERVED; concept lives on the Pugi side, host has no
  broadcaster yet. Subscribing is legal but no callback fires.
- `MetadataMutated` — designer property-grid edit on a metaobject

Unrecognised event names are accepted at subscription time and silently
never fire. Forward-compat: future host versions may add event names without
bumping ABI.

### Sandbox / kill-switch

`OES_PLUGIN_SANDBOX=1` in the environment skips plugin discovery entirely
in `ibPluginManager::LoadAll`. Used for incident response (a misbehaving
plugin won't load on the next launch) and for read-only viewer deployments
that should never run third-party code. The flag is checked before the
plugins/ directory is scanned — no `LoadLibrary`, no `dlopen`, no plugin
code runs.

## Rejected alternatives

- **Lua/Python scripting layer.** Overhead, dependency bloat, marshalling
  cost between OES `ibValue` and the host language's value system. Plain
  C ABI is enough for the integration scenarios we care about.
- **WASM plugin runtime.** Toolchain complexity for wxWidgets app, no clear
  benefit for a desktop integration target. Revisit if browser-side OES
  ships.
- **COM-style IUnknown.** Windows-only, not cross-platform.
- **Single appended slot per release without an ABI bump.** Considered;
  rejected because the v2 → v3 change altered an existing slot's calling
  convention (RegisterFunction grew `paramCount`). Appending only would have
  required a separate v3 RegisterFunction slot leaving v2 callers connected
  to a now-deprecated entry point — fragile.

## Consequences

Positive:
- Plugins can extend OES — register BSL builtins, menu items, event
  subscribers, log messages.
- Pugi integration unblocked: `LLMQuery(...)` becomes a script-callable,
  `BeforePublish` gates Σ-Check, menu surfaces operator commands.
- Stable C surface with explicit version gate; future ABI bumps stay
  cheap to reason about.
- Sandbox flag gives ops a kill-switch independent of the plugin code.

Negative:
- ABI not source-compatible between v2 → v3. The only shipping v2 plugin
  (`simplePlugin`) was rebuilt; external v2 plugins (none exist yet) would
  need a rebuild against the v3 header.
- `ibPluginCallScope` arena adds a small allocation per callback. Acceptable
  for human-paced events; high-frequency events (`AfterCompile` fires per
  module compile) may need batching if profiling shows pressure.
- Plugin authors must respect the main-thread-only rule; documented in this
  ADR and in `pluginApi.h`.

## Open follow-ups

- ADR-0003 will document the structured-payload type for events (currently
  payload pointer is always NULL).
- Plugin signing / manifest hash — not in scope for v3; revisit when
  shipping plugin marketplace.
- Per-plugin read-only mode bit on `ibHostAPI` — sandbox flag is currently
  process-wide; fine-grained per-plugin sandboxing is a v4 concern.
