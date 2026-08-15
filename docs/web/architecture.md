# Web runtime — architecture

## Compile/runtime split

The module manager is **per-session**. Each session owns its own
`ibValueModuleManagerRuntimeConfiguration` via `ibSession::m_root`
(`ibValuePtr`-managed, intrusive-refcounted). Bytecode (`ibByteCode`)
is per-descriptor (`ibRuntimeModuleDataObject`), shared across sessions
because compile is metadata-driven and immutable; the runtime
ProcUnit is rebuilt on each descriptor for the active session by
`ibValueModuleManager::AttachRuntime(s)` and torn down by
`DetachRuntime(s)` — both serialised by `m_runtimeMutex`.

Bring-up sequence (web): `ibWebSession::Login` → registry
anonymous-create → `session->Open(user, pwd)` (submits Attach via
`shared_from_this`, no ticket) → `ibSessionRegistry::NotifyAuthenticated`
runs the 3 phases (OnFirstConnect → `EnsureRoot` → OnAuthenticated). The
OnAuthenticated listener drives `RunDatabase` (one-shot per process),
`session->CompileRoot()`, then `mm->AttachRuntime(session)`.
`app->OnInit()` runs after that under an `ibSessionScope` bound on
the HTTP handler thread.

There is no per-session `ProcUnit` map on `ibSession` itself; the
descriptor's single `m_procUnit` slot is the current scaling
ceiling. Per-descriptor per-session map (`m_runtimes`) is the target
end-state of [`runtime-facade.md`](../runtime-facade.md), not yet
landed.

No `ibValueModuleManagerWebConfiguration` — web specifics live in
session lifecycle (frame ownership, worker thread, debug server),
not in the module manager.

## Binary split

```
wenterprise-server.exe       <- HTTP host (cpp-httplib), one per deployment
└── wfrontend.dll             <- web-built frontend (OES_USE_WEB)
    ├── backend.dll           <- unchanged, shared with desktop
    └── fbclient.dll + plugins
```

`wfrontend.dll` is compiled from the **same source tree** as `frontend.dll`
but with `OES_USE_WEB` defined. UI-specific call sites take the web branch
(HTML/JSON) instead of the native wx branch. There is no `wxApp` loop.

Build in Visual Studio: `src/engine/frontend/wfrontend.vcxproj` (Win32
Debug). SolutionDir must be the enterprise root — props at the root
(`Common.props`, `ConfigurationDefs.props`) resolve `$(SolutionDir)`.

## Per-session isolation

Each HTTP session gets its own `ibWebSession` owning an `ibWebApplication`.
The session itself owns:

- `m_root : ibValuePtr<ibValueModuleManagerRuntimeConfiguration>` — the
  per-session module manager root.
- `m_frame : ibBackendDocFrame*` (set via `SetFrame`) — published by
  `ibWebApplication` after it builds the `ibWebFrame`.
- `m_userInfo`, `m_sessionRawPassword`, `m_workDate` — per-session
  identity and runtime knobs that used to be process-wide singletons.
- Optional `ibDebugSession` for per-session breakpoint loops.

Process-level frame singleton on the backend side is gone
(`ms_mainFrame` / `t_mainFrame` / `GetDocMDIFrame` /
`backend_mainFrame` macro removed). Frame access goes through
`ibSession::Current()->GetFrame()` (or the convenience
`ibSession::CurrentFrame()`). On wenterprise-server, `Current()`
runs in `AccessMode::Shared`: per-thread bindings via
`ibSessionScope` / `ibSessionThreadBinding` for tab worker threads;
the wes process's `WebServer` system session is registered as the
fallback for unbound threads.

## Frame / tabs / form tree

```
ibWebFrame : ibBackendDocFrame, ibWebWindow
├── m_tabs : vector<unique_ptr<ibWebDocChildFrame>>
│   └── each owns:
│       ibVisualHostClient           <- ibValuePtr<ibValueForm> m_valueForm
│       └── ibValueForm
│           └── control tree (ibValueFrame subtree)
└── m_activeForm : ibBackendValueForm*   (borrowed; host owns the ref)
```

Closing a tab = `vector::erase` on `m_tabs`. Dtor chain drops
`ibVisualHostClient` → `ibValuePtr<ibValueForm>` → when no other ref, the
form and its control tree are freed.

## JSON walk

`ibVisualHostClient::WalkToJSON` (in `visualHost.cpp`) walks the form's
control tree and, for every `ibValueFrame`, calls `Create(webParent, host)`
which constructs a matching `ibWebWindow` node (`ibWebBoxSizer`,
`ibWebButton`, `ibWebStaticText`, ...). The walker then serialises that
parallel `ibWebWindow` tree into JSON. The browser's JS layer
(`wenterprise-server/main.cpp`, inline `<script>`) renders each node
through a per-type `BaseControl` subclass registered in a `renderers{}`
map.

## Control registry (name-based factory)

Metadata-backed form deserialisation (`ibValueForm::LoadForm` →
`ibValueForm::NewObject(clsid, ...)`) resolves control classes by
`ibClassID` using the same registry as desktop, populated through
`CONTROL_TYPE_REGISTER` statics in `visualView/ctrl/*.cpp`. At time of
writing, wfrontend.dll has ~34 ctors visible (`/diag/ctors` to enumerate).

The typed path (`CreateAndPrepareValueRef<T>`) used by `CreateNewForm` and
the `/demo` endpoint bypasses the registry entirely — it does not load
metadata and does not populate property defaults. That's why direct API
form creation works even when the registry is missing control types.

## DB connection pool

`ibApplicationData` no longer holds a direct `m_db`. The pool
(`ibConnectionPool` in `src/engine/backend/databaseLayer/connectionPool.{h,cpp}`)
is the sole owner of every live connection. The `db_query` macro
resolves through `ibApplicationData::GetDatabaseLayer()` which uses
the priority chain: active-TX TL → active-scope TL → primary
(master) connection. See [`../connection-pool.md`](../connection-pool.md)
for the full architecture (RAII `ibConnectionScope`, counter-based
nested transactions across 4 drivers, TL pinning, migration
status).

Lifecycle: `Init(primary, maxSize=32)` at `CreateFile` /
`ServerAppDataEnv` time, `Shutdown` at `DestroyAppDataEnv`. Clones
are allocated lazily on first `Checkout` so a single-threaded run
(designer) opens zero extra FB handles. A
`shared_ptr`'s custom deleter re-parks the clone on the pool's idle
list when the last borrower drops it.

The session registry uses `CheckoutIndependent` (no TL touch) for
its three persistent connections (lock / write / probe) on the
registry thread.

The metadata-change watcher uses the same pool: every sweep tick
borrows one connection, runs `SELECT file_guid FROM sys_config`, and
returns the clone. On guid change it evicts all sessions and does
`CloseDatabase(forceCloseFlag) + LoadDatabase + RunDatabase` on
`activeMetaData` to recompile against the deployed config; the next
login spins up user runtime on the fresh bytecode.
