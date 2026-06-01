# Module Manager Split (designer vs runtime)

Landed 2026-06-01. Splits the monolithic `ibValueModuleManager` into a
lightweight base, a heavy runtime branch, and an independent designer holder,
and routes every metadata object/record/module through one seam that hands back
the right manager for the current session kind.

Supersedes the intermediate "deferred compile-cache" approach
(`metadata-mm-decoupling.md`, the `project_common_module_designer_cache` arc).

## Why

Before the per-session runtime split, one config-level module manager served
both the designer (name resolution / autocomplete) and the runtime (script
execution). When the runtime moved into per-session `ibSession::m_root`, the
designer lost its manager. The first fix re-attached a designer manager but
kept it sharing runtime unit types — that dragged fragile runtime-unit lifetimes
into the editor's read path and produced a string of crashes (use-after-free in
the parent-context walk, AV in a designer-mode `Compile()` recompile, a dangling
metaobject on config reload, asserts on an empty registry, an unregistered base
unit type). The split removes the runtime from the designer's path entirely.

## Hierarchy

```
ibValueModuleManager                         — LIGHTWEIGHT base
  │   nested: ibValueModuleUnit (managerless, no ProcUnit), ibValueMetadataUnit
  │   holds: "Manager" singleton, m_metaManager, m_listGlConstValue, named context
  │   virtual CreateMainModule/DestroyMainModule (default no-op)
  │   pure virtual FindCommonModule(commonModule)        ← resolved per-branch
  │
  ├── ibValueModuleRuntimeManager              — HEAVY runtime part
  │     │   nested: ibValueRuntimeModuleUnit (+ ProcUnit, CreateCommonModule)
  │     │   m_listCommonModuleManager registry, RuntimeRegister/Unregister/Rename
  │     │   AttachRuntime / DetachRuntime, m_initialized, m_runtimeMutex
  │     │
  │     ├── ibValueModuleManagerRuntimeConfiguration  (+ ibRuntimeRoot) — session root (m_root)
  │     ├── ibValueModuleRuntimeManagerExternalDataProcessor       — external .epf
  │     └── ibValueModuleRuntimeManagerExternalReport              — external .erf
  │
  └── ibValueModuleManagerDesigner             — LIGHTWEIGHT designer holder
        own registry m_listCommonModule (vector<ibValuePtr<ibValueModuleUnit>>)
        AddCommonModule / RemoveCommonModule / RenameCommonModule / FindCommonModule
        CreateMainModule seeds Manager + ctor-context (Catalogs/Documents/Enums) + globals
        NEVER compiles units / runs script
```

`SYSTEM_TYPE_REGISTER`: the lightweight base unit (`ibValueModuleUnit`) needs its
own factory registration `SO_MODB` — the designer puts it into the compile
module's context, and `PrepareModuleData` calls `GetClassType()` → `GetTypeIDByRef`,
which asserts on an unregistered `wxClassInfo`. The runtime unit
(`ibValueRuntimeModuleUnit`) is `SO_MODL`, a distinct type.

## Designer manager lifetime

The designer manager lives in `ibCompileValueCache` (field
`ibValuePtr<ibValueModuleManagerDesigner> m_moduleManager`, accessors out-of-line
in `metadata.cpp`). The cache exists only in DesignerMode
(`ibMetaDataConfigurationStorage` ctor; external DP/Report ctors).

- **Created** in `RunDatabase` **before** `RunSubtree(flags, true)` — common
  modules register themselves into it from their `OnBeforeRunMetaObject` hook,
  which fires inside `RunSubtree(true)`. If the manager didn't exist yet they'd
  silently skip registration, and the symmetric `RemoveCommonModule` on close
  would assert on an empty registry. It binds to the **current** common
  metaobject — creating it once in the ctor would dangle across a config reload
  (the metaobject is reset/rebuilt).
- **Seeded** (`CreateMainModule`, the Manager singleton + ctor-context + globals)
  **after** `RunSubtree(true)` — the ctor-context factories register while the
  subtree runs. `CreateMainModule` ends with `PrepareNames()` so names resolve on
  the first autocomplete after load.
- **Released** in `CloseDatabase` (`DestroyMainModule()` then
  `SetModuleManager(nullptr)`), and on any `RunDatabase` bail-out path (a failed
  `RunSubtree`/`CreateMainModule`/`StartMainModule` won't reach `CloseDatabase`,
  so the bail path drops the manager or its stale registrations dangle into the
  next run).

Config, external data processor, and external report all follow this shape. The
external variants bind the manager to `m_commonObject->GetObjectModule()` (they
have no configuration common object) via the second ctor
`ibValueModuleManagerDesigner(ibMetaData*, const ibValueMetaObjectModule*)`; the
ctor-context (Catalogs/Documents/Manager) still comes from the active config
through `GetListCtorsByType`, so the editor sees the same names.

## No runtime root in the Designer

`ibSession::EnsureRoot()` is gated `if (appData->DesignerMode()) return;` — a
Designer session never creates a per-session runtime `m_root`. `CompileRoot` /
`DestroyRoot` / `ClearRoot` all early-return on null root, so the existing
authenticate/disconnect flow is a no-op for Designer. `RunDatabase` still runs
(it populates the compile cache + module storage), `AttachRuntime` self-gates by
kind. The seam below diverts the Designer to the compile-cache manager instead.

## The seam

```cpp
// session.cpp — two roads off one seam, keyed on THIS session's kind:
ibValueModuleManager* ibSession::GetEditModuleManager(const ibMetaData* metaData) const {
    if (m_kind == ibSessionKind::Designer) {
        if (auto* cc = metaData ? metaData->GetCompileCache() : nullptr)
            return cc->GetModuleManager();          // lightweight designer holder
        return nullptr;
    }
    return m_root;                                   // per-session runtime root
}

// static convenience — resolves against ibSession::Current(), null-safe:
static ibValueModuleManager* ibSession::EditModuleManagerFor(const ibMetaData* metaData);
```

Every `InitializeObject` / manager-module callsite that previously wrote
`ibSession::Current()->GetManagerModule()` now calls `EditModuleManagerFor`. The
return type is the lightweight base; `FindCommonModule` is virtual on the base
(designer and runtime each resolve from their own registry), and the other
methods used (`GetCompileModule`, `GetProcUnit`, `GetContextVariables`,
`GetObjectManager`) live on the base / `ibRuntimeModuleDataObject`.

`DesignerMode()` (process-global) and `m_kind == Designer` (per-session) partition
identically given the 1:1 process↔kind topology — designer.exe hosts exactly one
Designer-kind session and no runtime-kind session; wes/enterprise/daemon never
host a Designer-kind session. `EnsureRoot` uses the process predicate, the seam
uses the per-session one; they select the same sessions today. (Forward caveat:
if a future process ever hosts both a Designer and a runtime session, the two
gates would diverge — switch `EnsureRoot` to `m_kind` then.)

## Editor read path

`ibPrecompileCode::PrepareModuleData` (codeEditorInterpreter.cpp): manager =
`cc->GetModuleManager()`. Module being edited resolves through
`cc->FindCompileModule`; named context via `GetContextVariables()`; global common
modules iterate `moduleManager->GetCommonModules()` (designer units) and read
exports by parsing the live module text (`ibParserModule`) — no runtime unit,
no compilation, predictable designer-owned lifetime.

## Tests

`tests/test_module_manager_split.cpp` — structural + seam smoke tests
(`ModuleManagerSplit.*` hierarchy/abstractness, `ModuleManagerSeam.*` null-safety
across kinds). Pure unit, no database. The behavioural half (which manager a real
Catalog/Document parents to, common-module registration through
`OnBeforeRunMetaObject`, reload lifetime) is exercised by the designer at runtime.

## Crash log (closed during the arc)

1. UAF (`0xDDDDDDDD`) in the editor's parent-context walk over runtime units →
   stopped indexing live runtime units into the compile cache.
2. AV in `GetFullName` via `ibCompileModule::Compile` parent-recompile → the
   designer never compiles units (`Compile()` is a designer no-op anyway; the
   raw `cm->Compile()` was removed).
3. Garbage (`5`) in `m_listContextValue` after config reload → designer manager
   moved from the ctor (dangling metaobject) to `RunDatabase`/`CloseDatabase`.
4. `assert(int3)` in `RemoveCommonModule` on close (empty registry) → manager
   created **before** `RunSubtree`, so common modules register into it.
5. `assert` in `GetTypeIDByRef` closing a module editor → base unit registered
   under `SO_MODB`.
6. `assert(moduleManager)` creating a new external DP/Report → external metadata
   gets its own designer manager in `RunDatabase`; manager-module delegates and
   the editor read it through the seam.

## Bind-API (descriptor binding, follow-up arc)

The descriptor base `ibRuntimeModuleDataObject` exposes a single lazy
`EnsureCompileModule()` plus three intent-named bind methods (replacing one
`BindContextVariable(name, value, bool)` overload), split by storage + editor
visibility:

| Method | Storage | Name in autocomplete |
|---|---|---|
| `BindContextVariable` | `m_listContextValue`, `scopeContext=false` | visible (`ThisObject` / `ThisForm`) |
| `BindScopeVariable` | `m_listContextValue`, `scopeContext=true` | hidden — only members surface (`Manager` / `EnumManager` / `SystemManager`) |
| `BindExportVariable` | `m_listExternValue` | visible (globals: `Metadata`, common modules) |
| `UnbindVariable` | erases from BOTH maps (+ nulls the binder slot) | — |

The scope flag lives **on the context entry**, not on `ibValue`:
`m_listContextValue` is now `map<wxString, ibContextVar>` where
`ibContextVar { ibValue* m_value; bool m_scopeContext; }`. The editor's
parent-walk skips entries with `m_scopeContext` (so a transparent container's
name never enters the identifier list — fixes the regression where
`EnumManager`/`SystemManager` leaked into the completion list), while their
members are still inlined via the `GetContextVariables()` pass.

**Globals unification:** the manager's `m_listGlConstValue` registry was removed.
The single source for globals is the compile module's `m_listExternValue`
(per-descriptor, persistent across `Reset`). `Metadata` binds in the ctor via
`BindExportVariable`; common modules bind at registration. `CreateMainModule`/
`DestroyMainModule` no longer materialise/dematerialise globals each cycle —
ownership stays in `m_metaManager` / `m_listCommonModuleManager`, the extern map
only references. `GetGlobalVariables()` now returns `map<wxString, ibValue*>&`
(the extern map).

**Cross-metadata delegation:** for an external DP/Report, both the runtime
managers and the designer holder (`ibValueModuleManagerDesigner`, flagged
`m_external` from the object-module ctor) override `GetGlobalVariables()` /
`GetContextVariables()` to delegate to the configuration root via
`EditModuleManagerFor(appEnv::ActiveMetaData())`, so the external module's editor
sees the root's globals (`Metadata` + common modules), not its own empty map.

DDL diagnostics (`ddlLogLine`, hardcoded `ddl.log`) were stripped from the
restructure path during this arc.
