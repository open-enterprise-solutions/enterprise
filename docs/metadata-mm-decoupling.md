# Decoupling `ibValueModuleManager` from `ibMetaData`

**Status:** landed. `ibMetaData::GetModuleManager()` removed; runtime
root lives on `ibSession::m_root`. Doc kept as a record of why the
split exists and where the residual edges still are.

**Related:** `runtime-facade.md`, `session-registry.md`,
`connection-pool.md`.

---

## What this was about

`ibMetaData` once carried a pure virtual `GetModuleManager() →
ibValueModuleManager*`. That made the configuration look like a
singleton owner of running code, which contradicted the
connection-pool / session-registry direction (per-session runtime,
N workers, no process-wide runtime singleton).

The refactor moved runtime ownership from metadata to session:

- `ibMetaData` keeps only **compile-tree** data (descriptors, source
  text, designer-side compile cache).
- `ibSession` owns the **runtime root** through `m_root :
  ibValuePtr<ibValueModuleManagerConfiguration>`.

---

## Current shape (verified in tree)

### `ibMetaData` (`backend/metadata.h`)

- No `GetModuleManager` method, no `ibValueModuleManager` reference
  in the header.
- `ibCompileValueCache* GetCompileCache() const` — designer-side
  compile-value cache for autocomplete / property previews. Allocated
  only on `ibMetaDataConfigurationStorage` (designer-edit
  configuration); other configurations return nullptr. Callsites use
  `if (auto* cc = metaData->GetCompileCache()) ...` instead of
  `appData->DesignerMode()`.
- `ibModuleStorage` — read-only list of init common-module
  descriptors. Held by value, never null. Iterated by per-session
  runtime when populating common modules.

### `ibSession` (`backend/session/session.{h,cpp}`)

- `m_root : ibValuePtr<ibValueModuleManagerConfiguration>` — the
  session's own runtime root. Created via `CreateRoot(metaData)`
  (idempotent) or its façade `EnsureRoot()`. Stays null for sessions
  that never run scripts (Designer, WebServer technical, Launcher).
- `CompileRoot()` — runs `m_root->CreateMainModule()` then
  `m_root->AttachRuntime(this)`. Also bootstraps the lambda runtime
  shim (`m_lambdaRuntime`) parented at `m_root`'s procUnit.
- `DestroyRoot()` / `ClearRoot()` — detach + destroy in correct
  order (lambda shim before root's procUnit goes away).

### Auth bring-up (3-phase `NotifyAuthenticated`)

```
1. OnFirstConnect listeners (one-shot per process)
     appData → metadataCreate(runMode, flags)
                 # populates activeMetaData

2. session->EnsureRoot()
     # per-session CreateRoot(activeMetaData) — guarantees
     # GetModuleManager()-like access is non-null for phase 3

3. OnAuthenticated listeners
     appData → BindSessionToThread
             → activeMetaData->RunDatabase()   (one-shot per process)
             → session->CompileRoot()
                 # CreateMainModule + AttachRuntime(this)
```

The middle phase exists specifically because step 3's listeners
(`OnBefore/AfterRunMetaObject`) reach into the session's root via
their own paths; if step 1 ran straight into step 3 on the very
first session, root would be null at `OnBeforeRunMetaObject`.

### Access patterns now in use

Where the legacy code wrote `m_metaData->GetModuleManager()`, the
new code uses one of:

- `ibSession::Current()->m_root` (or session pointer in scope) — for
  runtime paths under a bound session.
- `ibSession::Current()->GetLambdaRuntime()` — lambda dispatch shim.
- Metadata side: `metaData->GetCompileCache()` — designer-side
  compile cache (replaces every old `if (appData->DesignerMode())
  metaTree->AddCompileModule(...)` guard).

Grep over `src/engine` for `GetModuleManager` returns 0 occurrences.

---

## Residual edges (not blockers, tracked for record)

1. **Per-descriptor single `m_procUnit` slot.** Each module
   descriptor (`ibRuntimeModuleDataObject`) holds one ProcUnit
   shared_ptr serialised by `m_runtimeMutex`. `AttachRuntime(s)`
   builds it for the active session, `DetachRuntime(s)` tears it
   down. Concurrent web sessions on the same descriptor coordinate
   through that mutex.

   This is the actual scaling ceiling — see `runtime-facade.md`
   Step 1: per-descriptor `m_runtimes : map<ibSession*,
   shared_ptr<runtime>>` is the target end-state. The
   `ibCompileValueCache` extraction here is the design template
   (same null-or-set pattern, same callsite shape `if (auto* x =
   md->GetX())`).

2. **`ibValueModuleManagerExternal*` template-dedup.** Two
   subclasses (DataProcessor / Report) duplicate near-identical
   bodies. `runtime-facade.md` Step 0 calls for a template over
   `<TMeta, TDataObject>`. Independent of this doc's scope, left
   for the runtime-facade pass.

3. **`ibMetadataRef{guid}` cross-bc encoding.** When bytecode
   refers to a metadata object (`Catalogs.Products`), it currently
   stores a descriptor pointer / name. AOT blobs survive only
   while names are stable. Step 12 of runtime-facade — not done.

4. **External DP/Report `m_objectValue`** still composed on the
   mm subclass rather than a separate shared_ptr. Cosmetic.

---

## How to verify in tree

- `Grep GetModuleManager src/engine` → 0 hits.
- `Grep "ibValueModuleManager" backend/metadata.h` → 0 hits.
- `ibSession::m_root` is the only owner of
  `ibValueModuleManagerConfiguration` in non-codeRunner builds;
  codeRunner uses `ibValueModuleManagerExternal*` directly as its
  root because it's standalone (no session bring-up).

---

## What it cost

- 50+ call sites migrated from `m_metaData->GetModuleManager()` to
  either session-bound access or `metaData->GetCompileCache()`.
- `ibCompileValueCache` extracted from designer's GUI tree
  (`ibBackendMetadataTree`) onto `ibMetaData` as a lifecycle-
  independent struct (2026-04-26).
- 3-phase `NotifyAuthenticated` shape (was 2-phase) — added the
  middle `EnsureRoot` phase so step-3 listeners always see a non-
  null `session->m_root`.
- Auth-flow setters (`SetUserInfo`, `EnableDebug`, `SetSessionRaw
  Password`) became private under `friend ibSessionRegistry`;
  registry façades `InstallUser` / `EnableDebugForSession` are the
  only public entry points.

---

## Lessons (kept for future similar refactors)

- **Two trees, not one.** Compile-tree on metadata (process-wide,
  immutable per metadata generation) vs runtime-tree on session
  (per-bound thread, owns ProcUnits). The temptation to keep them
  collapsed on metadata is what produced the original singleton
  trap.
- **Same null-pattern as compile cache.** Anywhere a runtime
  artifact attaches to metadata, prefer `if (auto* x = md->GetX())
  ...` over `appData->DesignerMode()` / `if (runMode == ...)`. The
  null state is meaningful (this metadata kind doesn't have that
  artifact) and the callsite reads the same way everywhere.
- **Bring-up phases must guarantee readiness.** When listener A
  reaches state that listener B installs, those listeners belong in
  different phases — not in registration-order coincidence. The
  middle `EnsureRoot` phase between `OnFirstConnect` and
  `OnAuthenticated` is the explicit version of that contract.
