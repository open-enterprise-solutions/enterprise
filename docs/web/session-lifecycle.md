# Session lifecycle

> **2026-04-20:** Classes renamed — `ibSessionContext` → `ibSession`,
> `SessionManager` → `ibSessionRegistry`. File paths: `backend/session/session.h`
> and `backend/session/sessionRegistry.h`. Behaviour unchanged; the full
> refactor (single-consumer thread, priority queue, DB-row pessimistic
> locks, `ibSessionTicket` RAII handle, unified `Connect(req)` entry) is
> in progress — see `docs/web/open-issues.md` → "Session registry
> refactor" and memory note `project_session_registry_refactor.md`.

Two-stage model since the compile/runtime split (2026-04-19):

1. **Server bootstrap** (`wfrontendInit*` → metadata load) compiles
   all module bytecode into descriptor-owned `ibByteCode` blobs on
   `ibModuleDataObject::m_byteCode` (shared across sessions). No
   `ibProcUnit` is created at this stage.
2. **Per-session runtime** (`ibWebSession::Login` →
   `registry.Connect` → `ticket.Attach` → `NotifyAuthenticated` →
   `mm->AttachRuntime(s)` → `ibWebApplication::OnInit`)
   allocates the session's `ibProcUnit` instances against the shared
   bytecode and fires the `OnStart` script on them.

Each session owns its own `ibValueModuleManagerConfiguration` via
`ibSession::m_root` (`ibValuePtr`-managed). `ibWebApplication` is the
per-session "app" analogue of `wxTheApp`. It owns the frame, the
worker thread, and a borrowed pointer to the session's `ibSession`.

## Session kind

Every `ibSession` carries an `ibSessionKind m_kind` (sessions-layer
enum, 1:1 with `ibRunMode` for the unambiguous cases) set at
creation time. The web run mode is ambiguous at this layer and
splits into two distinct session kinds inside one wes process:
`WebServer` (the technical bootstrap session, no script runtime) and
`WebClient` (per-tab / per-API-caller, runs scripts).
`SessionKindFromRunMode` returns `WebClient` for
`eWEB_RUNTIME_MODE` (the common per-tab case); the wes process's
own `WebServer` row is created with the kind passed explicitly.

`AttachRuntime` filters by kind:

```cpp
const ibSessionKind kind = session->GetKind();
const bool wantsRuntime =
       kind == ibSessionKind::Enterprise
    || kind == ibSessionKind::WebClient
    || kind == ibSessionKind::Service;
if (!wantsRuntime) return true;  // Launcher, Designer, WebServer
```

**Don't try to derive kind from state** (`userInfo`-populated, or
similar). An open-access configuration (empty `sys_user` table)
leaves `userInfo` empty even after a successful `/login`. A "empty
userInfo → skip runtime" heuristic would falsely skip that
legitimate user session. The kind is set authoritatively at
session-creation time.

## Login flow

```
POST /w/<prefix>/login
    │
    └── ibWebSession::Login(user, password)
        ├── ibConnectRequest req { computer, eWEB_RUNTIME_MODE,
        │                          address = wfrontendServerAddress(),
        │                          presetGuid = tabSid (from cookie/header) }
        │
        ├── registry.Connect(req)
        │     - Submit(Add, Normal), waits state Added
        │     - Add handler INSERTs anonymous sys_session row under
        │       row-lock, kind = ibSessionKind::WebClient
        │     - returns an ibSessionTicket (RAII; dtor submits Remove@Urgent)
        │
        ├── ticket.Attach(user, pwd)
        │     - Submit(Attach, Normal)
        │     - ProcessAttach calls appData->AuthenticateUser → InstallUser
        │       (writes user identity onto session->m_userInfo +
        │       m_sessionRawPassword)
        │     - registry.NotifyAuthenticated(s) runs the 3 phases:
        │         1. OnFirstConnect (one-shot per process)
        │         2. session->EnsureRoot() — CreateRoot(activeMetaData)
        │         3. OnAuthenticated → RunDatabase + session->CompileRoot()
        │                            + mm->AttachRuntime(s)
        │       (creates shared_ptr<ibProcUnit> for main + each non-global
        │        common module on the descriptors, executes bytecode top-
        │        level to populate globals)
        │
        ├── { ibSessionScope initScope(ticket->Session());
        │     │
        │     └── app->OnInit()                    ← ORDER MATTERS
        │           │                                (see note below)
        │           ├── new ibWebFrame(this)       ← frame first; scripts
        │           │                                need it during OnStart
        │           ├── session->SetFrame(frame)   ← publish to ibSession
        │           └── StartMainModuleSafe(mgr)
        │                 │ SEH-wrapped call into:
        │                 └── ibValueModuleManagerConfiguration::StartMainModule
        │                     ├── BeforeStart()  → CallAsProc("beforeStart", bCancel)
        │                     │     (script veto; false ⇒ abort login)
        │                     └── OnStart()      → CallAsProc("onStart")
        │                           (script opens default forms via
        │                            OpenForm(...))
        │   }
        │
        └── m_ticket = move(ticket); m_app = move(app)
```

Script dispatch through `ibModuleDataObject::GetProcUnit()` returns
the descriptor's `m_procUnit` slot. The slot is rebuilt for the
active session by `AttachRuntime` and serialised by
`m_runtimeMutex` — so concurrent web sessions calling into the same
descriptor coordinate through the runtime mutex (current scaling
ceiling; per-descriptor per-session map is the target — see
[`../runtime-facade.md`](../runtime-facade.md), step 1).

### AttachRuntime must run before OnInit

`app->OnInit()` calls `StartMainModule` which fires `BeforeStart` /
`OnStart`. Those resolve the runtime via `GetProcUnit()`; the
descriptor must have its session-bound `m_procUnit` set by the time
`OnStart` runs. If `AttachRuntime` runs **after** OnInit, the
slot is empty at OnStart time, dispatch returns nullptr, and the
script (including every `OpenForm` it makes) is silently skipped —
symptom: `tabCount=0` after a successful login.

Order is enforced by the registry — `NotifyAuthenticated` runs
`AttachRuntime` in its OnAuthenticated phase, before
`Login()` returns and before `app->OnInit()` is called.

## Teardown flow

Destruction mirrors Login in reverse:

```
ibWebSession::OnExit
    ├── app->OnExit()
    │     ├── RunOnWorker([]{ DeleteAllViews }).get()  ← close tabs on
    │     │                                              worker thread;
    │     │                                              script events
    │     │                                              (beforeClose /
    │     │                                              onClose) fire
    │     │                                              where ibProcUnit's
    │     │                                              thread-local state
    │     │                                              is valid
    │     ├── StopWorker()
    │     ├── ExitMainModuleSafe(session->m_root)
    │     │     BeforeExit() → CallAsProc("beforeExit", bCancel)
    │     │     OnExit()     → CallAsProc("onExit")
    │     ├── { ibSessionScope exitScope(session);
    │     │     └── session->DestroyRoot()
    │     │           m_root->DetachRuntime(this) + DestroyMainModule().
    │     │           Releases this session's ProcUnit on every touched
    │     │           descriptor (under m_runtimeMutex). Bytecode stays
    │     │           on the descriptor for the next session.
    │     │   }
    │     └── delete m_frame
    └── ticket.reset() → Submit(Remove, Urgent)
            └── ProcessRemove DELETEs sys_session row, releases row-lock
```

Per-session `ibValueModuleManagerConfiguration` (`session->m_root`)
drops with the session — it's owned by the session, not metadata.
Compile state on descriptors lives for the process lifetime
regardless.

## SEH wrapper — why

`BeforeStart` / `OnStart` are wrapped in `try { ... } catch (...) { }`
inside `moduleManagerEvent.cpp`. That catches C++ exceptions, but a
**Windows structured exception** (access violation — desktop-only
singleton dereferenced by the script call path) is not a C++ exception
under `/EHsc` and bypasses the catch. Without a `__try/__except` at the
outer call site, the process dies and the HTTP server goes with it.

`StartMainModuleSafe` / `ExitMainModuleSafe` in
`src/engine/frontend/web/webApplication.cpp` wrap the call in SEH. On an
AV, the helper captures `GetExceptionCode()` + `ExceptionAddress` and
calls `ReportFaultAddress(...)` which uses dbghelp (`SymFromAddr` +
`SymGetLineFromAddr64`) to log:

```
[app] StartMainModule SEH: code=0xC0000005 at 0x6355E2C0
    (wfrontend.dll+0x34e2c0) ibValueFrame::LoadControl+0x80
    [F:\...\frame.cpp:69]
```

The helper runs in the `__except` filter expression (C++-safe, no SEH
unwinding of C++ objects under `/EHsc`).

## Worker thread

`ibWebApplication::StartWorker` spawns one thread per session at the
end of `OnInit`. That thread is the sole script-runner from then on:
HTTP handlers submit via `RunOnWorker<T>(fn).get()` (blocking),
timers via `PostWork(fn)` (fire-and-forget). `ibProcUnit` is
thread-affine to the worker — serialising through one thread removes
any per-access mutex.

The worker installs `ibSessionScope(m_sessionContext)` at the top of
`WorkerLoop` so `ibSession::Current()` returns this tab's session
(under `AccessMode::Shared`) and breakpoint / runtime lookups
resolve to its state rather than the process-level `WebServer`
fallback.

## `ibWebApplication` fields today

- `m_frame` — raw `ibWebFrame*`. Owned; `new` in OnInit, `delete` in
  OnExit. Not refcounted. Published to the session via
  `ibSession::SetFrame` so backend code can reach it through
  `ibSession::CurrentFrame()`.
- `m_sessionContext` — borrowed `ibSession*`. Set by the owning
  `ibWebSession` right after construction; the registry owns the
  session's lifetime through the ticket.
- Worker-thread state (`m_worker`, `m_workerMtx`, `m_workerCv`,
  `m_workerQueue`, `m_workerStop`).
- Live-update counter (`m_seq`, `m_seqMtx`, `m_seqCv`) for SSE.

Per-session debug state moved to `ibSession::ibDebugSession` (CV /
mutex / runContext / watch expressions) so concurrent web sessions
each enter their own debug loop. `ibDebuggerServer` stays
process-level — it dispatches commands by `sessionGuid` to the
matching session's debug state.

The former `ibValuePtr<ibValueModuleManagerConfiguration> m_moduleManager`
field on `ibWebApplication` is gone. Each session owns its own root
mm directly via `ibSession::m_root` (ibValuePtr); any caller that used
to access `app->GetModuleManager()` now goes through the session
pointer it already has in scope. `ibMetaData::GetModuleManager()` is
also gone — see `docs/metadata-mm-decoupling.md` for the full
metadata / runtime split.
