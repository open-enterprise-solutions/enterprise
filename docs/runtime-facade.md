# Runtime facade — per-session ibValueModuleManager

Design of the OES runtime layer after the refactor: `ibValueModuleManager`
becomes the per-session root, nested runtimes hang off descriptor-
owned mixins, bytecode decoupling + AOT persistent cache as follow-up.
This document records the architectural decisions of the 2026-04-22
session and the implementation phase plan. Verified against tree on
2026-05-22.

> **Status:** 16 of 17 steps landed; only Step 12 (cross-bc metadata
> refs by guid) is still open.
>
> **Verified-in-tree resolution (one map, not two).** Reading "session's
> ProcUnit map" in earlier drafts of this doc was misleading. The
> shipped shape is simpler:
> - The session owns its root via `ibValuePtr<ibValueModuleManager
>   RuntimeConfiguration> m_root` (`session.h:744`); the public accessor
>   is `ibSession::GetManagerModule()` (NOT the aspirational
>   `activeMetaData->GetModuleManager(session)` used in the design
>   narrative below). `AttachRuntime` / `DetachRuntime` live on
>   `ibValueModuleManager` (`moduleManager.h:360`/`:363`), serialised by
>   `m_runtimeMutex` (`moduleManager.h:377`).
> - Each descriptor (`ibRuntimeModuleDataObject` in `moduleInfo.h:26`)
>   owns a single `std::shared_ptr<ibProcUnit> m_procUnit`.
> - `ibValueModuleManager::AttachRuntime(session)` walks the
>   descriptors and (re)builds their `m_procUnit` for that session,
>   serialised by `m_runtimeMutex`. `DetachRuntime(session)` is the
>   symmetric tear-down.
> - `GetProcUnit()` returns the descriptor's current slot; the
>   shared_ptr pins it against fast-F5 UAF
>   (`project_refresh_execute_crash.md`). There is no session-keyed
>   `std::map<ibSession*, ...>` per descriptor today.
>
> This is the actual scaling ceiling for concurrent web sessions on
> the same descriptor — they coordinate through the runtime mutex
> rather than each holding their own ProcUnit. The session-keyed-
> map form remains the design target for high-concurrency web; for
> the current single-session-per-descriptor-at-a-time workload the
> mutex form is sufficient.
>
> Done:
> - **1-2** — descriptors compose `ibRuntimeModuleDataObject`
>   (`backend/moduleInfo.h:26`) with `m_compileModule`, `m_procUnit`
>   (shared_ptr — pins against fast-F5 UAF), `m_binder`, `m_parent`.
>   `appData::Connect` / `ibWebSession::Login` route through
>   `ibSession::EnsureRoot` + `CompileRoot`; `CompileRoot` itself
>   calls `m_root->AttachRuntime(this)` (folded in 2026-05-XX — was
>   formerly an explicit `mm->AttachRuntime(s)` from the caller). The
>   `appData/webSession` legacy hop is removed.
> - **3** — `InitRuntimeForSession` / `ExitRuntimeForSession` renamed
>   to `AttachRuntime(session)` / `DetachRuntime(session)` on
>   `ibValueModuleManager`. Privatisation behind the descriptor stays
>   open — methods remain public on mm because the registry's
>   3-phase `NotifyAuthenticated` still calls them through
>   `session->CompileRoot()` directly. A private form would require
>   a façade on `ibSession::Start/Stop` first; not blocking.
> - **4** — Start/Stop pair lives on `ibValueModuleManager`; descriptor
>   subclasses inherit through `ibRuntimeModuleDataObject`.
> - **5** — Facade is `ExecAsProc` / `ExecAsFunc` on
>   `ibRuntimeModuleDataObject` itself (variadic sugar at
>   `moduleInfo.h:38`/`:44`; protected array form at `:246`/`:254`).
>   Mirrors
>   `ibProcUnit::CallAsProc/Func` shape — variadic packs into
>   `ibValue*[]` and forwards. shared_ptr pin against fast-F5 UAF,
>   null-safety. Better placement than the original `mm->CallAsProc(
>   obj, method, ...)` sketch — each descriptor pins its own
>   ProcUnit, no mm hop. 38 object call-sites in 9 files
>   (catalog/document/charts/registers/common/constant) migrated from
>   raw `m_procUnit->CallAsProc` to variadic `ExecAsProc`; 8 dispatch
>   callers (3 mm + 4 record-data + frontend's `form.cpp`) qualified-
>   call the protected array form via
>   `ibRuntimeModuleDataObject::ExecAsProc`.
> - **6-7** — `m_parent` on the runtime base (raw ptr; invariant
>   "parent outlives child" enforced by owning containers — equivalent
>   to weak_ptr without cycle risk). `m_compileModule` + `m_byteCode`
>   live on the descriptor's runtime mixin, not on a singleton mm.
> - **8** — bytecode self-contained. Grep confirms zero accesses to
>   `byteCode->m_compileModule` / `bc->m_compileModule` in tree. AOT
>   `Serialize` / `Deserialize` round-trip carries every field the bc
>   needs (`m_dependencyIds`, `m_dependencyVersions`,
>   `bc.CreateBinder()` factory, process-wide bytecode registry — all
>   on bc). What's NOT decoupled yet is cross-bc metadata reference
>   encoding — that's Step 12.
> - **9** — object-value composed via descriptor's runtime mixin; no
>   shared mm field.
> - **10** — Designer = compile + intellisense only.
>   `InitializeRuntime` / `Compile` / `Run` short-circuit in Designer
>   mode at the descriptor level. Designer-side compile cache extracted
>   to `ibMetaData::GetCompileCache()` (`metadata-mm-decoupling.md`)
>   so a headless designer compile path works without a wx tree.
> - **11** — extern binder factory (`bc.CreateBinder()`) on bytecode;
>   per-descriptor `m_binder` with `SetVar` for per-execute binding.
>   Replaces compile-time `AddContextVariable` staging.
> - **13-17** — AOT cache pipeline landed. The blob (de)serialiser is
>   `backend/compiler/byteCodeAOT.cpp` (`kAOTFormatVersion = 20`
>   currently — `byteCodeAOT.cpp:136`; bumped on each opcode-layout
>   change, e.g. CES + closure-capture + LINQ shifts); the cache-row /
>   table layer is `backend/compiler/cache/byteCodeCache.{h,cpp}`:
>   `sys_bytecode_cache` table with UPSERT semantics,
>   three-arm `Compile` hook (cache-hit / miss / drift via magic +
>   format-version reject), dependency registry +
>   `ResolveAndVerifyDependencies`, Designer
>   `OnSaveMetaObject`/`OnDeleteMetaObject` cascading invalidate.
>   Step 5 of the AOT subplan (automated invalidation gtests) is the
>   only remaining piece — manual verification was the bring-up
>   tactic; tests can backfill.
>
> Remaining:
> - **12** — `ibMetadataRef{guid}` encoding for cross-bc metadata refs
>   (`Catalogs.Products` etc). Today bytecode stores names; AOT blobs
>   survive recompiles only while descriptor names are stable.
>   Renaming a descriptor invalidates dependent caches — works, just
>   not optimal. Cross-bc ref → resolve-by-guid is the closure here.
> - **Adjacent (not numbered in original plan):** per-descriptor
>   per-session map form of Step 1. Today's single-slot +
>   `m_runtimeMutex` works; high-concurrency web is the use case that
>   would actually need the map. Not blocked by anything except
>   profiling evidence that the mutex is the bottleneck.
> - **locale cleanup** — `_("AttachRuntime main: %s")` strings need an
>   xgettext + msgmerge pass for ru/uk .po files.

---

## Table of contents

1. [Goal](#goal)
2. [Class structure](#class-structure)
3. [Ownership / lifecycle](#ownership--lifecycle)
4. [Startup chains](#startup-chains)
5. [Parent chain rules](#parent-chain-rules)
6. [Current() semantics](#current-semantics)
7. [Context vars and AOT (future)](#context-vars-and-aot-future)
8. [Phase plan — 17 steps](#phase-plan)
9. [What this refactor closes](#what-this-refactor-closes)
10. [Related documents and memory notes](#related-documents-and-memory-notes)

---

## Goal

> **Reading note.** Sections from here through "Phase plan" describe
> the **target** architecture as designed in 2026-04-22, including
> API shapes (`m_runtimes` map, `activeMetaData->GetModuleManager(
> session)`, etc.) that did **not** land literally. The Status banner
> at the top of this document records what actually shipped: a
> simpler descriptor-owned single-slot model with `m_runtimeMutex`,
> driven from `ibSession::CompileRoot`. Treat the design narrative
> below as the direction-of-travel reference, not API documentation.

Today the runtime layer is spread across several singleton paths:

- `activeMetaData->GetModuleManager()` — one mm per process (desktop) /
  first-entry on web (bug: multi-tab last-login-wins).
- `ibModuleDataObject::m_procUnit` — descriptor-level field, shared between
  sessions, resolved through a thread_local session fallback.
- `ibValueModuleManager::AttachRuntime / DetachRuntime` —
  called from appData.cpp + webSession.cpp; the session layer knows about
  the module manager and reaches inside it.
- Form/eval/external-DP — each compiles its own compileModule ad-hoc with
  a back-pointer to parent through `m_compileModule`.

Goal of the refactor:

1. **Session layer does not know about runtime** — `session.Start()` /
   `session.Stop()` + `activeMetaData->GetModuleManager(session)` are
   the only interaction points. No `AttachRuntime`.
2. **Per-tab isolation on web** — every WebClient session has its own
   root mm + a fully independent runtime tree. Multi-tab
   last-login-wins is closed architecturally.
3. **Runtime tree explicit** — object/form/external runtime are explicit
   tree nodes with explicit parents, not implicit through procUnit parent
   chain on singletons.
4. **Descriptor-owned runtime** — every module descriptor holds its
   per-session runtime instances in an `m_runtimes` map; the session
   tracks touched descriptors for O(N) teardown.
5. **Bytecode self-contained** (prerequisite for AOT) — no
   `byteCode->m_compileModule` back-pointers.
6. **AOT bytecode cache** (long-term) — `sys_bytecode_cache` table,
   blobs with hash-based invalidation. Startup X-50× faster, production
   security (source need not be present).

---

## Class structure

### One `ibValueModuleManager` for the root, existing subclasses for specifics

`ibValueModuleManager` is a **class for the session root only**.
Singleton per session. Existing subclasses stay:

```cpp
class BACKEND_API ibValueModuleManager : public ibModuleDataObject, public ibValue {
    // singleton-only fields:
    ibSession*        m_session;
    ibRuntimeContext* m_context;

    // existing fields — compile state, common modules, etc.

public:
    virtual bool Start() = 0;   // was CreateMainModule + StartMainModule
    virtual void Stop()  = 0;   // was ExitMainModule + DestroyMainModule

    // facade for C++→script calls (replaces 38 direct m_procUnit->CallAsProc call-sites):
    bool CallAsProc(const wxString& name, ibValue** args, long n);
    bool CallAsProc(ibValue* obj, const wxString& method, ibValue** args, long n);
    bool CallAsFunc(const wxString& name, ibValue& result, ibValue** args, long n);
    bool CallAsFunc(ibValue* obj, const wxString& method, ibValue& result, ibValue** args, long n);

    ibSession*        GetSession() const override { return m_session; }
    ibRuntimeContext* GetContext() const override { return m_context; }
};

class ibValueModuleManagerConfiguration      : public ibValueModuleManager { ... };
class ibValueModuleManagerExternalDataProcessor : public ibValueModuleManager { ... };
class ibValueModuleManagerExternalReport     : public ibValueModuleManager { ... };
```

### Nested runtimes — through `ibModuleDataObject + ibValue`

Nested nodes (common module, catalog/document object instance, form,
external DP instance) inherit from `ibModuleDataObject + ibValue` —
**not** from `ibValueModuleManager`. The module manager is a singleton
only for the root.

```cpp
class BACKEND_API ibModuleDataObject {
    // existing:
    ibCompileModule*            m_compileModule;
    std::shared_ptr<ibProcUnit> m_procUnit;

    // NEW — tree membership:
    std::weak_ptr<ibModuleDataObject> m_parent;

    // NEW — ambient-info virtuals (default walks up to the root):
    virtual ibSession*        GetSession() const {
        auto p = m_parent.lock();
        return p ? p->GetSession() : nullptr;
    }
    virtual ibRuntimeContext* GetContext() const {
        auto p = m_parent.lock();
        return p ? p->GetContext() : nullptr;
    }

    // legacy — goes away in step 3-4:
    mutable std::string m_attachedSessionId;
    void AttachToCurrentSession() const;
    void DetachFromCurrentSession() const;
};
```

Existing subclasses of `ibModuleDataObject` (we keep them, adding
integration with the new tree model):

| Node type | Class | Position in the tree |
|---|---|---|
| Root (Configuration) | `ibValueModuleManagerConfiguration` | root per session |
| Root (External DP) | `ibValueModuleManagerExternalDataProcessor` | standalone (codeRunner) — itself the root |
| Root (External Report) | `ibValueModuleManagerExternalReport` | standalone — itself the root |
| Common module | `ibValueModuleUnit` (existing) | child of root |
| Catalog/Document/Register instance | `ibValueRecordDataObject*` subclasses (existing) | child of root |
| Form | `ibValueForm` (existing) | child of object mm or root |
| External DP (opened from Configuration) | `ibValueRecordDataObjectDataProcessor` | child of root (parent = configuration root) |
| Eval | `ibProcUnitEvaluate` (existing — special case) | **NOT in the tree**, see below |

### Descriptor — per-session runtime storage

```cpp
class BACKEND_API ibValueMetaObjectModule {
    // compile state — shared between sessions:
    std::shared_ptr<ibByteCode>                m_byteCode;

    // runtime state — per-session, polymorphic:
    std::unordered_map<ibSession*, std::shared_ptr<ibModuleDataObject>> m_runtimes;
    mutable std::mutex                                                   m_runtimesMutex;

    std::shared_ptr<ibModuleDataObject> GetRuntime(ibSession*) const;
    std::shared_ptr<ibModuleDataObject> AttachRuntime(ibSession*, ibModuleDataObject* parent);
    void _DropRuntime(ibSession*);   // called from session teardown
};
```

The descriptor holds a **shared_ptr** (strong ownership). Type is
heterogeneous — `shared_ptr<ibModuleDataObject>` may be an mm (for the
root descriptor), unit (common), record object (catalog instance), or form.

### Session — lightweight, no runtime fields

```cpp
class BACKEND_API ibSession {
    // existing identity / state machine

    // NEW — only for teardown:
    std::unordered_set<ibValueMetaObjectModule*> m_touchedDescriptors;
    void _TrackDescriptor(ibValueMetaObjectModule*);
};
```

The session does **not** hold a shared_ptr on runtimes — ownership is on
the descriptor. The session keeps a set of touched descriptors so that on
Stop() it iterates and calls `desc._DropRuntime(this)` on each.

---

## Ownership / lifecycle

### Who holds what

| Artefact | Owner (strong) | Weak refs |
|---|---|---|
| `ibByteCode` (compile output) | `descriptor.m_byteCode` | `procUnit.m_pByteCode` (through shared) |
| `ibProcUnit` (runtime state) | `runtime node.m_procUnit` (shared) | — |
| Root `ibValueModuleManager` | `root_descriptor.m_runtimes[session]` | `session.m_touchedDescriptors` contains the descriptor. |
| Nested `ibModuleDataObject` (object/form/unit) | `descriptor.m_runtimes[session]` | `child.m_parent` (weak on parent) |
| `ibProcUnitEvaluate` | caller's frame / eval cache | `m_parent` (weak) |

### Flow for creating a nested runtime

```cpp
// script encountered catalog method call: obj.OnWrite()
auto session    = ibSession::Current();
auto objectDesc = obj->GetMetaObject();

auto objectRt = objectDesc->GetRuntime(session);   // lookup in descriptor's map
if (!objectRt) {
    // lazy attach: first call to descriptor in this session
    auto root = activeMetaData->GetModuleManager(session).get();
    objectRt = objectDesc->AttachRuntime(session, root);
    //   ↑ creates ibValueRecordDataObject*, puts shared in m_runtimes[session],
    //     registers descriptor in session.m_touchedDescriptors,
    //     sets objectRt->m_parent = weak(root)
}

// dispatch — objectRt uses parent (root) through chain for scope resolution
objectRt->ExecAsProc(wxT("OnWrite"), args, nArgs);
```

### Teardown flow

```cpp
// Session.Stop() called from Disconnect / Logout / pagehide
void ibSession::Stop() {
    std::lock_guard lock(m_mutex);
    for (auto* desc : m_touchedDescriptors) {
        desc->_DropRuntime(this);
        // descriptor removes its shared_ptr<runtime> for this session →
        // ~ibModuleDataObject / ~ibValueModuleManager cascade →
        // procUnit dropped, bytecode shared_ptr refcount--,
        // m_parent weak expires (if parent alive — only the child's slot expires)
    }
    m_touchedDescriptors.clear();
}
```

User phrase: **"when the session ends, the descriptor is cleaned up,
and with it the runtime"** — `descriptor.m_runtimes[session]` entry
goes away, which drops the runtime through shared_ptr.

### Eval — outside the tree model

Eval (`ibProcUnitEvaluate`) is the only exception:
- **No descriptor** (ad-hoc compile of expression bytecode)
- **NOT in `m_runtimes` map** (nothing to index)
- **Parent** — weak on `ibValueModuleManager::Current()` at the moment
  of compile, only for scope chain during Execute
- **Ownership** — caller's stack (resulting ibValue holds the eval-unit
  as long as the value lives) or eval cache

---

## Startup chains

### Desktop — `enterprise.exe`

```
launcher.exe / CLI creds
     │
     ▼
ibApplicationData::Connect(user, pwd):
  db.Open(pool checkout)
  registry.Start() + registry.Connect(req)  → ticket
  ticket.Attach(user, pwd)                  → session Authenticated
  │
  ├─ activeMetaData->Attach(session, appDataAsContext)   ← NEW
  │    • creates root mm in m_runtimes[session] of root descriptor
  │    • root.m_session = session, m_context = appData
  │
  ├─ activeMetaData->GetModuleManager()->Start()         ← NEW
  │    • compile main module (through descriptor cache or in place)
  │    • procUnit.Execute top-level
  │    • fanout common modules — each attached as a child with parent=root
  │    • fire BeforeStart / OnStart events through CallAsProc

     │
     ▼
wx main loop — user-interaction
  Open form → formDesc->AttachRuntime(session, parent=object or root)
  Execute OnWrite → objectRt->ExecAsProc
  Eval("...") → ibProcUnitEvaluate created in place, parent = Current()

     │
     ▼
ibApplicationData::Disconnect():
  activeMetaData->GetModuleManager()->Stop()             ← NEW
    • fire BeforeExit / OnExit
    • walk m_children common modules (dropped through shared on root teardown)

  activeMetaData->Detach(session)                        ← NEW
    • session.Stop() iterates touchedDescriptors
    • each _DropRuntime(session) → ~runtime cascade

  ticket.reset → Remove@Urgent; registry.Stop; db.Close
```

### Web server bootstrap — `wenterprise-server.exe`

```
wfrontendInitFile / wfrontendInitServer
     │
     ▼
activeMetaData->LoadDatabase()    (compile cache in descriptors filled)
     │
     ▼
registry.Start + registry.Connect(sysReq, kind=WebServer) → ticket
ticket.Attach (empty creds)

⚠ NO activeMetaData->Attach(sysSession, ...) — the wes process does
  NOT run scripts itself. The system session is a tracking row, no runtime.

     │
     ▼
svr.listen(address, port)   (cpp-httplib)
SetConsoleCtrlHandler for clean shutdown
```

### Web client — per-tab session inside the wes process

```
Browser opens tab → GET /w/<db>/
  ibWebSession created (no runtime yet — just cookie-less identity)

     │
     ▼
POST /w/<db>/login (user + pwd):
  tabSid from X-OES-Session header
  wfrontendCreateSessionWithId(tabSid) if cookie unknown
  ibWebSession::Login:
    registry.Connect(req, kind=WebClient)  → ticket
    ticket.Attach(user, pwd)               → Authenticated

    activeMetaData->Attach(session, webAppAsContext)      ← NEW
    SessionScope scope(session)  // HTTP handler pin (already exists)
    activeMetaData->GetModuleManager()->Start()           ← NEW

    StartWorker — per-session worker thread

     │
     ▼
Subsequent HTTP requests (GET /active, POST /change, ...):
  HTTP worker-thread pin SessionScope(session)
  through worker — executes object events:
    mm->CallAsProc(objInstance, wxT("OnWrite"), args)    ← NEW (migrates 38 call-sites)

     │
     ▼
Tab close → pagehide beacon → POST /logout?sid=<tabSid>:
  ibWebSession::Destroy:
    RunOnWorker DeleteAllViews (close open forms)
    StopWorker

    mm->Stop()                                            ← NEW
    activeMetaData->Detach(session)                       ← NEW

  ticket.reset → Remove@Urgent; sys_session DELETE
```

---

## Parent chain rules

Parent is explicit, resolved by node type (not through Current()):

| Type | Parent |
|---|---|
| Root (Configuration) | none (expired) |
| Common module | root (static attach on Root.Start()) |
| Object (catalog/document/register) | **always root** |
| External DP / Report (opened from Configuration) | **always root** |
| Form | object mm if opened on an object (owner), otherwise root |
| Eval | `Current()` at compile time — the only call-stack-dependent case |

Attach is done through the descriptor's `AttachRuntime(session, parent)`,
and the parent is passed explicitly from the call-site:

```cpp
// Object / External:
auto root = activeMetaData->GetModuleManager(session).get();
auto mm   = objectDesc->AttachRuntime(session, root);

// Form:
auto parent = ownerObject
    ? ownerObject->GetMetaObject()->GetRuntime(session).get()
    : activeMetaData->GetModuleManager(session).get();
auto formRt = formDesc->AttachRuntime(session, parent);

// Eval — the only exception (not through descriptor):
auto evalBc   = CompileExpr(exprText, parentBc->m_rootContext);
auto evalUnit = make_shared<ibProcUnitEvaluate>();
evalUnit->m_parent = ibValueModuleManager::Current()->shared_from_this();
```

Parent = **weak** — no cycles, teardown cascades top-down
(descriptor drops → runtime ~dtor → children's m_parent expires).

---

## Current() semantics

Thread_local stack + session fallback:

```cpp
static ibValueModuleManager* ibValueModuleManager::Current() {
    thread_local std::vector<ibValueModuleManager*> s_stack;

    // role 1: currently-executing node (push/pop around CallAsProc)
    if (!s_stack.empty()) return s_stack.back();

    // role 2: ambient — session's root (fallback)
    if (auto* s = ibSession::Current())
        return activeMetaData->GetModuleManager(s).get();

    return nullptr;
}
```

**Desktop**: `ibSession::Current()` is the single session (pinned in
Connect), map lookup O(1) from one entry.

**Web**: `ibSession::Current()` is pinned in HTTP handler through
`SessionScope(tabSession)`, map lookup O(1) from N entries.

Code is identical — only the size of `descriptor.m_runtimes` map differs.

### Two roles

| Caller | What it expects |
|---|---|
| Dispatch inside the script: compile eval parent, Current() for scope | **currently-executing node** — stack top |
| `activeMetaData->GetModuleManager()` (legacy call-sites, ~100) | **session's root** — fallback through Session::Current() |

---

## Context vars and AOT (future)

Full specification — `memory/project_bytecode_aot_cache.md`.

Brief summary:

1. **Bytecode self-contained** (step 8): `m_moduleName`, `m_rootContext`,
   `m_parent`, `m_frameSize`, `m_externSlots`, `m_evalBindings` inside
   the bytecode itself. Back-pointer `byteCode->m_compileModule` removed.

2. **Context vars late-binding through `ibExternBinder`** (step 11):
   bytecode contains `m_externSlots` — a list of (name, CLSID, optional).
   Runtime creates a binder through factory `bytecode.CreateBinder()`,
   auto-populates known vars (ThisObject, Reference, Manager,
   ThisForm, ...) from the descriptor's contract, validation on the
   pre-Execute gate. Mutable context vars are substituted by the runtime
   on each Execute — the blob stays session- and instance-independent.

3. **Persistent AOT cache** (steps 13-17): a separate
   `sys_bytecode_cache` table keyed by `descriptor_guid`, containing blob +
   hash header. Runtime batch-loads on Attach, validates hashes,
   HIT → deserialize, MISS → compile + persist. Designer Save
   updates the row + cascading invalidate of dependents through the
   dependency graph. Startup X-50× faster, production security (source
   need not be present).

---

## Phase plan

17 steps, incremental. Risk estimates — medium-high after step 5.

> **Progress (verified 2026-05-22).**
> - **Step 1 — landed (single-slot form, not literal map).**
>   Descriptors compose `ibRuntimeModuleDataObject`
>   (`backend/moduleInfo.h:26`) — owns `m_compileModule`, `m_procUnit`
>   (shared_ptr, pins against UAF), `m_binder`, `m_parent`. The
>   descriptor's slot is rebuilt per session by `AttachRuntime(s)` /
>   `DetachRuntime(s)` under `m_runtimeMutex`. No
>   `std::map<ibSession*, ...>` per descriptor today; the map form
>   stays as the design target if multi-session-per-descriptor
>   concurrency becomes a measured bottleneck.
> - **Step 2 — landed.** `ibSession::EnsureRoot` / `CompileRoot` are
>   the entry; `CompileRoot` itself calls `m_root->AttachRuntime(this)`
>   (folded in — was formerly an explicit caller-side hop from
>   `appData::Connect` / `ibWebSession::Login`). 3-phase
>   `NotifyAuthenticated` (OnFirstConnect → EnsureRoot →
>   OnAuthenticated) is the registry-side contract.
> - **Step 3 — landed (privatisation deferred).** `AttachRuntime` /
>   `DetachRuntime` are public on mm because the session calls them
>   from `CompileRoot` / `DestroyRoot`. Privatising would require a
>   session-side façade; cosmetic, not blocking.
> - **Step 8 — fully landed.** Zero `bc->m_compileModule` accesses in
>   tree (`backend/compiler/`). bytecode carries
>   `m_dependencyIds` / `m_dependencyVersions`, `bc.CreateBinder()`
>   factory, process-wide registry + `Find`. AOT round-trip
>   demonstrates self-containment (cache row is the only payload).
> - **Adjacent precursors (2026-04-26):** `ibCompileValueCache`
>   extracted from designer's GUI tree onto `ibMetaData` (precursor
>   for step 10 — designer without runtimes); `ibSession::EnsureRoot()`
>   + 3-phase `NotifyAuthenticated` — root-mm ownership on session.
> - **Steps 13-17 — AOT cache pipeline landed** (blob serialiser
>   `backend/compiler/byteCodeAOT.cpp`, `kAOTFormatVersion = 20`;
>   cache-row layer `backend/compiler/cache/byteCodeCache.{h,cpp}`).
>   UPSERT row per descriptor, dependency registry,
>   Designer Save / Delete cascading invalidate. Only automated
>   invalidation gtests remain.


| Step | What | Risk |
|---|---|---|
| 0 | Template-dedup `ibValueModuleManagerExternalDataProcessor/Report` over `<TMeta,TDataObject>` | low |
| 1 | Descriptor `m_runtimes` map + `GetRuntime(session)/AttachRuntime/_DropRuntime`. `ibMetaDataConfiguration.GetModuleManager(session=nullptr) / Attach / Detach` as sugar over the root module descriptor. | low |
| 2 | `appData.cpp Connect/Disconnect` + `webSession.cpp Login/Destroy` → `Attach/Detach` + `mm->Start/Stop()` | low |
| 3 | `AttachRuntime/DetachRuntime` → private detail inside Attach/Detach | low |
| 4 | Merge `CreateMainModule+StartMainModule → Start()`, `ExitMainModule+DestroyMainModule → Stop()`. Subclasses merge bodies, remove duplication | medium |
| 5 | `CallAsProc/Func` facade on mm (4 overloads). Migrate 38 object call-sites from `m_procUnit->CallAsProc` to `mm->CallAsProc(obj, method, ...)` | medium |
| 6 | Add to `ibModuleDataObject`: `m_parent: weak_ptr`, virtual `GetSession()/GetContext()`. `ibValueModuleManager` overrides — returns its own fields. `ibValueModuleManager::Current()` + thread_local stack. No new subclasses | low-medium |
| 7 | Compile cache on descriptor: `shared_ptr<ibCompileCode>` + `shared_ptr<ibByteCode>` fields on `ibValueMetaObjectModule`. Remove `m_compileModule` from mm, replace with a ref | medium |
| 8 | Bytecode decoupling: self-contained `m_moduleName/m_rootContext/m_parent/m_evalBindings/m_frameSize/m_externSlots` in `ibByteCode`; remove `byteCode->m_compileModule` back-pointer; `~ibProcUnitEvaluate` on `unique_ptr<ibCompileCode>` | high |
| 9 | Object-value decoupling from mm (External DP/Report `m_objectValue` as a separate shared_ptr, not a field of mm) | medium |
| 10 | Designer = compile + intellisense only. `HasRuntime() == false` for Designer sessions. `activeMetaData->GetModuleManager(designerSession) == nullptr` | low |
| 11 | Context vars late-binding through `ibExternBinder` factory. Remove `compileModule.AddContextVariable` from compile-time. Pre-Execute gates (required slot not bound → `ibBackendCoreException`) | high |
| 12 | Stable metadata refs — `Catalogs.Products` in bytecode encoded as `ibMetadataRef{guid}`, resolved on load | medium |
| 13 | `ibByteCode::SaveToBinary / LoadFromBinary` (chunk-based through `ibWriterMemory`) | medium |
| 14 | `sys_bytecode_cache` table + `MigrateTableBytecode` per driver. Unified `ibPreparedStatement::SetParamBlob/GetBlob` API | low |
| 15 | Attach flow: batch-read cache → validate (format_version / compiler_version / source_hash / metadata_version / context_contract_hash) → HIT deserialize / MISS compile+persist | medium |
| 16 | Designer Save: write cache row atomically with the source change | low |
| 17 | Dependency graph → cascade invalidate cache rows of dependent modules on Save | medium |

**Rough phase boundaries:**

- Steps 1-5 — **facade**. After this the session layer does not know about
  the moduleManager, per-tab isolation, 38 object call-sites through the facade.
- Steps 6-10 — **tree + compile/runtime split**. After this the runtime
  tree is explicit, compile is fully in descriptors, Designer works
  without runtimes.
- Steps 11-17 — **AOT**. A separate large piece of work, conceptually
  independent of 1-10, but prerequisite to self-contained bytecode
  (step 8).

---

## What this refactor closes

- **`project_web_session_bug`** — multi-tab last-login-wins. Closed by
  steps 1-2 (per-session root mm, no shared singleton).
- **`project_refresh_execute_crash`** — fast-F5 UAF on teardown
  concurrent with executing procUnit. Closed by steps 7-8 (bytecode
  self-owned through shared_ptr, runtime paths do not touch compileModule).
- **`project_mainframe_singleton_variants`** — `backend_mainFrame`
  thread_local pinning fragility. **Already closed** by the
  ibBackendDocFrame rename + frame ownership move onto `ibSession`
  (no process-level singleton on the backend side anymore). The
  runtime-tree refactor is parallel: runtime gets session/context
  through parent walk, not through thread_local ambient lookup every
  time.
- **`project_runtime_owner_refactor`** — previous iteration of the plan.
  Superseded by this document.
- **`project_bytecode_compile_decoupling`** — step 8, refined in the
  `reference_extern_binder` note (an assistant memory outside this repo — the
  name is kept for provenance, there is no file to open here)
  to specify what exactly migrates into the bytecode.

---

## Related documents and memory notes

### Docs

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — overall picture (sections
  "Sessions and Runtime Ownership" obsoleted by this document)
- [`session-registry.md`](session-registry.md) — session layer,
  registry refactor (completed, adjacent layer)
- [`backend-frontend-split.md`](backend-frontend-split.md) —
  adjacent architectural review (chunky `ibBackendDocFrame` interface,
  successor of the historical `ibBackendDocMDIFrame`), partially
  covered by the runtime session-
  owned model

### Memory notes

- `project_runtime_facade_plan` — full plan, technical deep-dive
- `reference_runtime_tree_design` — TL;DR spec + parent chain details
- `reference_extern_binder` — context vars binding + CLSID contracts
- `project_bytecode_compile_decoupling` — step 8 — bytecode
  self-containment
- `project_bytecode_aot_cache` — steps 11-17 — AOT cache (sys_bytecode_cache)
- `project_refresh_execute_crash` — symptom closed by steps 7-8
- `project_web_session_bug` — symptom closed by steps 1-2

---

## Decision history (for review later)

| Date | Decision | Why |
|---|---|---|
| 2026-04-22 | Keep existing `ibValueModuleManager` name instead of renaming to `ibRuntime` | Migration-friendly: ~100 legacy call-sites work without changes through default-arg `GetModuleManager(session=nullptr)` + Current() fallback |
| 2026-04-22 | No Kind enum, no new subclasses — type info from descriptor CLSID | Fewer coupling points, less maintenance. Existing subclasses (Configuration/ExtDP/Report) already cover mm-root specifics |
| 2026-04-22 | Nested nodes inherit from `ibModuleDataObject + ibValue`, NOT from `ibValueModuleManager` | mm = singleton per session (root only). Existing pattern — Manager (root) vs Unit (nested). Nested classes are already ibModuleDataObject-derived |
| 2026-04-22 | Ownership: descriptor owns `shared_ptr<mm>`, session keeps `m_touchedDescriptors` for teardown | Ownership on the descriptor is natural (next to the compile cache). Session is lightweight identity, with no runtime fields. Teardown O(touched_descs) |
| 2026-04-22 | `m_parent` — weak on ibModuleDataObject base, NOT children accumulation on mm | mm does not accumulate children; parent-only link for scope walks. Teardown cascades through descriptor.m_runtimes drops, not through m_children |
| 2026-04-22 | Parent is resolved explicitly (not through Current()) for Object/External/Form — only Eval through Current() | Predictability: parent is determined from Attach parameters. Eval by definition inherits scope from where it was called — the only exception |
| 2026-04-22 | AOT cache in a separate table `sys_bytecode_cache` (not inline BLOB in module text) | Clean source/compiled separation, TRUNCATE for forced recompile, production DB without sources is possible (security) |
