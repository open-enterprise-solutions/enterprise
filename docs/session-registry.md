# Session registry refactor — full picture

> **Status:** LANDED (partial) — 19 of ~22 commits shipped 2026-04-20.
> Remaining: concrete `TryProbeRowLock` for MySQL / MSSQL, snapshot
> SELECT reading new columns into `ibSessionSnapshot` accessors,
> singleton `m_userInfo` / `m_sessionRawPassword` removal, designer
> exclusive-policy verification under two concurrent designers. Full
> list in §"What remains".

Full reference for the session-registry refactor (2026-04-20). Covers the architecture, what landed in which step, every gotcha discovered, and what remains.

## Goal

Replace the old `ibApplicationDataSessionUpdater` (heartbeat thread on singleton `ibApplicationData` with 1Hz UPDATE of its own row in `sys_session`) with:

- **Per-process session registry** (`ibSessionRegistry`) with a single consumer thread and priority queue.
- **Per-session object** (`ibSession`) with a state machine (lifecycle + auth) and a cv for producers.
- **Ticket RAII** (`ibSessionTicket`) — owner handle, dtor submits Remove@Urgent.
- **Unified `Connect(req)` entry** for desktop / designer / web-server / web-cookie.
- **Policy chain** (`ibSessionPolicy`) instead of scattered veto hooks.
- **Session-aware user accessors** on `ibApplicationData` — closes the web multi-tab "last login wins" bug.

## Main components

| File | Role |
|---|---|
| `backend/session/session.{h,cpp}` | `ibSession` + state enums + `SessionScope` (legacy thread-local). |
| `backend/session/sessionRegistry.{h,cpp}` | Registry class: thread + priority queue + tick loop + DB ops. |
| `backend/session/sessionTicket.{h,cpp}` | RAII ticket — move-only owner. |
| `backend/session/sessionPolicy.h` | Pure interface for `CanAdd`-veto. |
| `backend/session/designerExclusivePolicy.{h,cpp}` | Ports `VerifySessionUpdater`. |

## Lifecycle (desktop)

The phased entry points on `ibApplicationData` are
`CreateSession()` (or typed `CreateSession<SessionT>()`) plus the
session's own `Open(user, pwd)`. The legacy monolithic `StartSession`
is gone — failed `Open` keeps the anonymous row in `sys_session`
until the caller drops the ticket explicitly, which lets the GUI
login-retry loop reuse one row across attempts.

```
ibSession* s = appData->CreateSession<ibEnterpriseSession>();
│   ├── registry.EnableSysSessionOwnership(true)         # one-shot
│   ├── (designer only) registry.AddPolicy(DesignerExclusivePolicy)
│   ├── registry.EnsureStarted()                         # spawn thread,
│   │                                                    # 2 pool checkouts
│   │                                                    # (write / probe)
│   ├── registry.CreateSessionWithFactory(...)            # builds typed ibSession
│   ├── registry.Connect(req)                             # Submit(Add, Normal) → Wait
│   │   └── ProcessAdd:
│   │       ├── policy chain
│   │       ├── m_own[guid] = session
│   │       └── InsertSessionRow (+ ext UPDATE)
│   └── OnCreateSession() fires on main thread            # GUI subclass
│                                                          # builds wx frame here
│
s->Open(user, pwd)
│   ├── submitAttach(user, pwd) → ProcessAttach
│   │   ├── appData->AuthenticateUser(…)                 # pure verifier
│   │   ├── ibSessionScope(&s) + appData->InstallUser     # session-aware writer
│   │   └── UPDATE userName / userGuid
│   ├── (on Attach fail) OnShowAuthenticate              # GUI dialog override
│   │   └── re-submitAttach with dialog creds
│   └── registry.NotifyAuthenticated(s)                   # 3-phase (see below)
```

### NotifyAuthenticated phases

`ibSessionRegistry::NotifyAuthenticated` orchestrates session bring-up in three strict phases. Listeners that depend on later state must hook the right phase.

```
NotifyAuthenticated(s):
  1. OnFirstConnect listeners (one-shot per process):
       appData lambda → metaDataCreate(runMode, flags)
                        # populates the activeMetaData singleton

  2. s->EnsureRoot()        # idempotent CreateRoot(activeMetaData)
                            # session's own root mm allocated NOW
                            # — so step 3 listeners see session->m_root != null

  3. OnAuthenticated listeners (every authenticated session):
       appData lambda:
         ├── BindSessionToThread
         ├── (Shared mode + no fallback) registry.SetFallback(s)
         ├── (one-shot) activeMetaData->RunDatabase()
         │     # fires OnBefore/AfterRunMetaObject which reach into
         │     # session->m_root and the metadata-side
         │     # ibCompileValueCache — both required to be live by now
         └── s->CompileRoot()
                            # CreateMainModule + m_root->AttachRuntime(this)
                            # — folded into CompileRoot; appData no
                            # longer calls AttachRuntime separately
```

**Why three phases, not two.** The pre-2026-04-26 layout fired `OnFirstConnect` then `OnAuthenticated` directly. `CreateRoot` lived in `appData`'s `OnAuthenticated` listener, and `RunDatabase` was nominally below it but ordering was easy to flip. The crash that drove this refactor was `OnBeforeRunMetaObject` reading `ibSession::Current()->m_root` while `RunDatabase` was iterating — m_root null because `CreateRoot` hadn't run yet on the very first session. Putting `EnsureRoot` between phases makes the contract explicit: every `OnAuthenticated` listener can rely on `s->m_root` being non-null when `activeMetaData` is set.

**Where `CreateRoot` lives.** In `ibSession`. The session is the owner of its root mm; the registry just calls the hook at the right moment. `appData`'s `OnAuthenticated` no longer touches `CreateRoot` — only `RunDatabase`. `AttachRuntime` is folded into `session->CompileRoot()` itself, so the listener chain ends at `CompileRoot`.

## Lifecycle (web per-cookie)

```
GET /                                            # cookie mint (id = new guid)
└── SessionManager::Create → ibWebSession(id)
    └── OnInit — metadata name; no runtime yet

POST /login { user, password }
└── ibWebSession::Login(user, pwd)
    ├── ibConnectRequest req { computer, eENTERPRISE_MODE,
    │                          address = wfrontendServerAddress() }
    ├── registry.Connect(req)                    # anonymous row INSERT
    ├── m_ticket = move(result.ticket)
    ├── ticket.Attach(user, pwd)                 # UPDATE userName
    ├── SessionScope(ticket->Session()) on HTTP thread
    ├── AttachRuntime(ticket->Session()) # ProcUnits for this cookie
    └── app->OnInit() → StartMainModule → OnStart script fires
```

## Thread model

**Registry thread** (single consumer) owns:
- `m_own : unordered_map<guid, shared_ptr<ibSession>>` — sessions this process tracks.
- 2 pool-checkout'ed connections:
  - `m_writeConn` — INSERT / UPDATE / DELETE + JobRefreshSnapshot SELECT.
  - `m_probeConn` — `TryProbeRowLock` NOWAIT probe.
  - (Historical `m_lockConn` retired together with the pessimistic-lock
    liveness model; the third pool slot is now free for productive use.)
- `m_snapshot` under RW mutex — exposed via `GetClusterSnapshot()`.
- Priority queue bins (Urgent / Normal / Low / Background).
- `m_workerPool : unique_ptr<ibWorkerPool>` — per-session task dispatcher.
  Allocated by the ctor when `maxWorkers > 0` (server modes); GUI hosts
  pass 0 and dispatch through `ibSession::Submit`'s inline fallback.

**Tick schedule:**

| Interval | Job |
|---|---|
| 1s | `JobHeartbeatOwn` (UPDATE own lastActive), `JobRefreshSnapshot` (SELECT * → snapshot). |
| 3s | `JobSweepStale` (delete zombies via probe-lock OR lastActive cutoff). |
| On-demand | drain queue (Add/Attach/Detach/Remove/SetActivity) — strict descending priority. |
| Eager on Start | one initial sweep + refresh so UI has data immediately. |

**Fatal invariant:** if ThreadBody throws — `Die()` → `std::terminate`. Registry-thread must-be-alive; otherwise sys_session no longer reflects reality and other processes will ClearLost our rows.

## Gotchas (everything that surfaced)

### 1. Start/Connect race

**Symptom:** an immediate `reg.Connect()` after `reg.Start()` returns `RegistryDown`.

**Cause:** `Start()` returned immediately after `m_thread = std::thread(...)`, before ThreadBody set `m_threadAlive = true`. Connect's top check saw `!m_threadAlive` → `RegistryDown`.

**Fix:** `Start()` does a short spin (≤2s) until the thread raises the alive flag.

### 2. Attach-empty-creds deadlock

**Symptom:** calling `ticket.Attach("", "")` (CLI with no creds) hangs on timeout → `ibAttachResult::Timeout`.

**Cause:** `Attach` did `TransitionAuth(Anonymous)` reset → Submit Attach → `WaitForAuth(from=Anonymous, ...)`. The empty-creds early-return branch in ProcessAttach did another `TransitionAuth(Anonymous)` — state didn't change, predicate `state != Anonymous` never flipped, 20s waiting.

**Fix:** removed the early-return in ProcessAttach. Always routes through `AuthenticateUser` — on open-access (empty sys_user, empty creds) it transitions to `Authenticated` with empty info.

### 3. Launcher/designer stuck in tasklist after close

**Symptom:** after closing enterprise.exe / designer.exe the process stays in tasklist, holding backend.dll.

**Cause:** `ibApplicationData::Disconnect()` called `registry.Stop()` only if `m_created_metadata` + all intermediate checks passed. An early `return false` (e.g. failed `CloseDatabase`) skipped Stop → thread alive → process alive.

**Fix:** Stop() is now called unconditionally at the end of Disconnect. Success flag is accumulated but doesn't gate the Stop call.

### 4. HoldRowLocks self-deadlock (CRITICAL)

**Symptom:** HTTP requests hang on infinite timeout, Active Users empty, registry thread appears dead.

**Cause:** the "row-lock = liveness" scheme tried to hold `SELECT ... WITH LOCK` long-TX on our own rows through `m_lockConn`. Simultaneously `JobHeartbeatOwn` tried `UPDATE lastActive` on the same rows through `m_writeConn`. FB sees two independent TXs — the first holds an update-intent lock → the second waits. The second runs on the registry thread. Thread hangs → nothing updates.

**Fix:** switched to **heartbeat-based liveness**.
- `HoldRowLocks` is no longer called (`RebuildLockHold` removed from ProcessAdd/Remove).
- `JobHeartbeatOwn` UPDATEs lastActive every second on its own inserted rows.
- `JobSweepStale` uses a hybrid: `TryProbeRowLock` as fast path (clean exits), `lastActive < now - 60s` as fallback (force-killed processes whose orphan TXs FB hasn't rolled back).

### 5. Force-killed processes → persistent zombies

**Symptom:** after `kill -9 enterprise.exe` its row stays in sys_session; designer's Active Users shows a "dead" user.

**Cause:** FB embedded doesn't orphan-rollback on every operation. A killed process's TX may stay "active" until the next full DB reopen / gfix. Our probe-lock tries WITH LOCK NOWAIT on the held row → conflict → we think the owner is alive.

**Fix:** `JobSweepStale` uses a 60s lastActive cutoff as fallback. After 60s without heartbeat a row is zombie regardless. Eager sweep on Start() clears zombies from previous runs.

### 6. Schema migration for existing DBs

**Symptom (expected):** on a production DB (Configuration on PG) Active Users is empty after cutover. The new pid / address / currentActivity columns don't exist in the legacy schema; a 9-column `InsertSessionRow` fails → no row written.

**Fix:** `InsertSessionRow` does a 6-column INSERT (core columns are always present), then a separate `UPDATE SET pid=?, address=? WHERE session=?` — if the schema is legacy the UPDATE silently fails, the INSERT remains valid. `MigrateTableSession` is called from CreateFile/ServerAppDataEnv and tries `ALTER TABLE ADD COLUMN` for each missing column; each ALTER is wrapped in try/catch — drivers that don't support it (rare) simply skip.

### 7. Session-aware user accessors

**Idea:** the web HTTP worker thread runs under `SessionScope(cookieSession)`. The singleton `appData->GetUserName()` used to read `m_userInfo` — last-login-wins on multi-tab. The transitional shape (now historical — the singleton field has since been removed) looked like:

```cpp
// HISTORICAL (transitional shape pre-removal of singleton):
const wxString& ibApplicationData::GetUserName() const {
    return GetUserInfo().m_strUserName;
}
const ibUserInfo& ibApplicationData::GetUserInfo() const {
    if (auto* ctx = ibSession::Current())
        return ctx->GetUserInfo();   // per-session mirror
    return m_userInfo;                // fallback (pre-auth, codeRunner)
}
```

25+ call-sites picked up the behaviour automatically. Singleton `m_userInfo` / `m_sessionRawPassword` fields are now **fully removed** from `ibApplicationData` (verified 2026-05-28 — grep over `appData.{h,cpp}` returns 0 occurrences); accessors resolve through `ibSession::Current()` only.

### 8. Per-driver NoWait plumbing

`ibTxOptions::noWait = true` in `BeginTransaction` is now honoured in all 4 drivers:
- **FB**: `isc_tpb_nowait` in TPB.
- **PG**: `SET LOCAL lock_timeout = 0` after BEGIN.
- **MySQL/InnoDB**: `SET SESSION innodb_lock_wait_timeout = 1`.
- **ODBC/MSSQL**: `SET LOCK_TIMEOUT 0`.
- **SQLite**: no-op (single-process).

Concrete `HoldRowLocks` / `TryProbeRowLock` impls — FB only so far. On other drivers the base class returns false → registry runs on the heartbeat-cutoff scheme without the probe-lock fast path.

### 9. Cookie / session guid unification

**Note:** the web cookie value is currently a separate 32-hex random from `wfrontend.cpp::newSessionId()`, not equal to the ibSession guid. Legacy behaviour. Future: hand out the ibGuid directly in the cookie — single id across all layers.

### 10. Debugger parked-session eval UAF (2026-06-05)

Watch / tooltip / **expand `thisForm`** / autocomplete evals run on the **debug-server
connection thread**, but `ibRunContext` lives on the **worker thread's** `Execute`
stack. The old pattern `if (ms_debugServer->IsDebugLooped()) Evaluate(ibSession::
CurrentRunContext(), …)` was a cross-thread TOCTOU: `IsDebugLooped()` reads the
**server-global** `m_bDebugLoop` (sticky-true — Continue clears only the per-session
`dbg->m_debugLoop`), while `CurrentRunContext()` reads the **per-session**
`dbg->m_runContext`. When the worker resumed (Continue/Step/Cancel/destroy) and unwound
its frame mid-eval, `ibProcUnit::Evaluate` dereferenced a freed `ibRunContext` at
`pRunContext->m_listEval` → `0xdddddddd` AV.

**Fix** (`debugServer.cpp`): file-local `EvalInParkedSession()` locks `dbg->m_mutex`,
gates on the **per-session** `dbg->m_debugLoop`, reads `dbg->m_runContext`, and runs
`Evaluate` **under the lock**. `DoDebugLoop`'s leave block (and `SetStack`) take the same
mutex to stamp `m_debugLoop=false` + null `m_runContext`. Invariant: an eval either
acquires first (the worker blocks on the teardown lock until the eval finishes against a
still-live frame, then unwinds) or acquires after (sees `m_debugLoop==false` → skips).
`ibSession::CurrentRunContext()` had no other callers and was removed. Same-thread script
`Eval()`/`Execute()` (systemManagerFunc.cpp) use own-stack `GetCurrentRunContext()` — not
racy. Latent since the worker-pool / per-session-debug model (eval thread ≠ execution
thread); surfaced under heavy debugger use.

## What landed (commit list)

1. **Skeleton rename** — `ibSessionContext → ibSession`, `SessionManager → ibSessionRegistry` (18 files).
2. **DB row-lock API** — `HoldRowLocks / TryProbeRowLock / ReleaseRowLocks` on `ibDatabaseLayer`; `BeginTransaction(const ibTxOptions&)`; FB concrete impl.
3. **Registry thread + priority queue** — Submit/DrainAll strict descending, fatal invariant, `IsThreadAlive/IsFatal`.
4. **Ticket + Connect + state machine** — `ibSession::Transition/WaitForState`, `ibSessionTicket`, `ibConnectRequest/Result`, unified entry.
5. **Auth split** — `AuthenticateUser` pure + `InstallUser` side-effect; `ProcessAttach` routes through them.
6. **DB ops in handlers (gated)** — INSERT/UPDATE/DELETE through `m_writeConn`; `m_ownsSysSession` flag.
7. **Cutover (desktop)** — StartSession through registry; `ibApplicationDataSessionUpdater` deleted (~370 lines); `GetSessionArray` → `GetClusterSnapshot`.
8. **Bugfixes** — Start/Connect race (#1), Attach-empty-creds deadlock (#2).
9. **Web wiring** — `ibWebSession::Login` through ticket; smoke test end-to-end (GET /, POST /login, GET /session).
10. **DesignerExclusivePolicy** — probe-lock based replacement for `VerifySessionUpdater`.
11. **Session-aware accessors** — `GetUserInfo / GetUserName / GetUserPassword / GetUserRoleArray / GetUserLanguageGuid / GetUserLanguageCode / ComputeMd5` out-of-line, Current()-first.
12. **Schema extension** — pid / address / currentActivity columns + `MigrateTableSession` + `ProcessSetActivity` real UPDATE + `ibSessionTicket::SetActivity`.
13. **Per-driver NoWait** — PG / MySQL / ODBC plus FB.
14. **wfrontend server address plumbing** — `wfrontendSetServerAddress/ServerAddress` exports; `ibWebSession::Login` stamps `sys_session.address`.
15. **Disconnect force-Stop** — Stop() is called unconditionally; a force-kill of the process no longer leaves a zombie thread.
16. **HoldRowLocks removed** (#4 fix) — liveness = heartbeat + 60s cutoff + probe as fast path. Hybrid sweep (JobSweepStale).
17. **Split timers** — 1s refresh (UI responsiveness), 3s sweep (cluster cleanup).
18. **Eager initial sweep + refresh** — Active Users isn't empty at startup.
19. **INSERT split (6-col + ext-UPDATE)** — legacy-schema tolerant.

## What remains (priority ↓, verified 2026-05-22)

- Concrete `HoldRowLocks / TryProbeRowLock` for **MySQL / MSSQL**. FB and PG are landed (see "TryProbeRowLock across drivers" section below).
- Snapshot SELECT reading the new columns (`pid` / `address` / `currentActivity` / `kind`) into `ibSessionSnapshot` accessors. Columns are written by `InsertSessionRow` / `ProcessSetActivity`; consumer-side accessors on the snapshot are the gap.
- Remove `SessionScope::Current()` legacy thread-local (after migrating `AppUser()`-style built-ins onto explicit session pointers via `ibProcUnit::GetSession()`).
- Interactive verification: designer-exclusive policy under two simultaneous designer.exe processes.

**Closed since the original list:**
- ~~Full removal of singleton `m_userInfo` / `m_sessionRawPassword` fields on `ibApplicationData`~~ — done; grep over `appData.{h,cpp}` returns 0 occurrences. The Gotcha-7 "session-aware user accessors" code sketch below still shows the dual-write transitional shape for historical readability.
- ~~Designer Active-Users UI: a "Kind" column from `ibSessionKind`~~ — `JobRefreshSnapshot` reads the `kind` column and `GetSessionKind(idx)` exposes it; UI column lives in `ibDialogActiveUser`.

**Closed since the original list:**

- ~~`signal` column + admin kick/reload dispatcher + `/admin/sessions` endpoint~~ — landed; see "Admin signals (kick / reload)" section.
- ~~Cookie / ibGuid unification on web~~ — landed; see "Unified session id across web layers".
- ~~`m_sessionGuid` singleton on `ibApplicationData`~~ — gone; session guid lives in `ibSessionIdentity::m_guid`.
- ~~Per-driver NoWait plumbing (PG / MySQL / MSSQL)~~ — landed for transaction-options; concrete row-lock probe still pending for MySQL / MSSQL only.
- ~~Web login real-auth~~ — open-access mode passes through `AuthenticateUser` like populated sys_user; same code path.

## ibSessionKind (landed 2026-04-20)

`ibSession` no longer carries `m_runMode` — process-level info lives on `appData` only. The session-level axis is a separate enum:

```cpp
enum class ibSessionKind : int {
    Launcher   = eLAUNCHER_MODE,
    Designer   = eDESIGNER_MODE,
    Enterprise = eENTERPRISE_MODE,
    Service    = eSERVICE_MODE,
    WebServer  = eWEB_ENTERPRISE_MODE,   // wes process technical row
    WebClient  = 100,                     // per-tab / API caller
};
```

- Desktop `appData->CreateSession()` → `SessionKindFromRunMode(m_runMode)` (wes process passes `WebServer` explicitly via the typed factory).
- `ibWebSession::Login` → `ibSessionKind::WebClient` + `m_appMode = eWEB_ENTERPRISE_MODE`.
- `sys_session.kind` is added via `MigrateTableSession` (best-effort ALTER TABLE); legacy schemas without it still work (kinds read as 0).
- `moduleManager::AttachRuntime` filters by kind: runtime runs for `Enterprise / WebClient / Service`, skipped for the rest.

## wes console-close cleanup

`wenterprise-server/main.cpp` installs `SetConsoleCtrlHandler` (Windows) and `SIGINT/SIGTERM` (POSIX) so the sys_session row is DELETEd when the console closes. For `CTRL_CLOSE_EVENT / LOGOFF / SHUTDOWN` (Windows grants 5s before force-kill) shutdown runs inline in the handler thread (`wfrontendShutdown()` + `ExitProcess(0)`) rather than via main's return — `listen_after_bind` on Windows doesn't always unblock fast enough after `svr.stop()`.

## HTTP worker threads: SessionScope + per-tab session id

Two independent foot-guns, landed 2026-04-20:

**1. SessionScope on worker threads.** cpp-httplib dispatches handlers on worker threads where `ibSession::Current()` is null by default. `ibModuleDataObject::GetProcUnit()` then returns nullptr, the form gets `SetParent(nullptr)`, and `Execute` throws `ibBackendCoreException("compilation failed (#2)")`. Any handler that touches forms/modules must pin the session explicitly:
```cpp
SessionScope scope(webSession->Session());
```
Current sink is `OpenFormInSession` in `frontend/wfrontend.cpp`. Other per-session handlers (/action, /change, /fire, /toggle, /tab, /form) should be checked if "compilation failed" recurs on web.

**2. Per-tab session id routing.** JS in `webClient.cpp` generates a `tabSid` in sessionStorage (per-browser-tab), independent of the server's `oes_session` cookie. netFetch attaches it via the `X-OES-Session` header. Two subtleties:
- **POST /login must create a session for unknown ids** (`wfrontendCreateSessionWithId`). A fresh tab can first hit the server through /login instead of GET /; strict 401 on unknown id breaks login.
- **`<img src>` / `<script src>` cannot carry custom headers** — only cookies. For plain-GET per-tab endpoints (e.g. `/tab/<i>/icon`) the URL must include `?sid=<tabSid>`. `SessionIdFromReq` reads the query param as a fallback between header and cookie.

## PNG handler registration

`wenterprise-server.exe` itself doesn't link against the PNG codec (wxBase-only). The PNG handler is registered inside `wfrontendInitFile` / `wfrontendInitServer` via `wxImage::AddHandler(new wxPNGHandler)`. Without it, `wxImage::SaveFile(..., wxBITMAP_TYPE_PNG)` in `TabIconPNG` returns false, `/tab/<i>/icon` replies 404, and the browser `<img>` renders as broken. The backend's `picturePredefined.cpp` also calls `wxInitAllImageHandlers` for its own paths.

## Tab-close beacon

`frontend/web/webClient.cpp` adds a `pagehide` listener that fires `navigator.sendBeacon(API + '/logout?sid=' + encodeURIComponent(tabSid))` when the tab is closing or navigating away. The browser queues the beacon and delivers it even after the page unloads, so the server DELETEs the session row immediately rather than waiting for the 2-minute idle sweep in `SessionManager::SweepLoop`.

The historic concern — reload-GET racing a beacon-destroyed session — no longer applies: `GET /` re-mints sessions for unknown ids, so beacon+reload lands on a clean same-id session. `sendBeacon` runs through a try/catch so throwing browsers don't break the rest of pagehide, and the 2-min idle cutoff remains as a fallback for Safari edge cases, Task Manager kill, or tab crashes where the beacon never fires.

## Admin signals (kick / reload)

`sys_session.signal VARCHAR(32)` is a cross-process control channel
added by `MigrateTableSession` (legacy schemas without it are tolerated —
the column simply stays `NULL` and `JobCheckSignal` skips work). Every
owning process's registry thread polls `signal` for its own rows on the
sweep tick (~3s), acts on non-empty values, and clears the cell:

- `"kick"` — submits `Remove@Urgent` for that specific session; ticket
  drops → row DELETE.
- `"reload"` — flips `m_reloadRequested` on the registry.
  `ConsumeReloadRequest()` is a one-shot consume used by embedders
  (wfrontend's `SweepLoop`) to evict every web-client session the owner
  holds, forcing clients through `/login` against the freshly-loaded
  metadata.

Three entry points share one UPDATE path (`WriteSessionSignal` in
`sessionRegistry.cpp`):

- C++ from any thread: `ibSessionRegistry::Instance().Kick(guid)` /
  `Reload(guid)`.
- HTTP on wes: `POST /admin/sessions/<guid>/kick` and
  `POST /admin/sessions/<guid>/reload`.
- Designer dialog: right-click a row in Active Users → `Kick session`
  / `Reload clients`.

## Web auth form

`GET /auth-info` returns `{"hasUsers": bool}` — a lightweight metadata
probe that the web client uses on boot to choose between the
open-access fast path (empty `sys_user` → `POST /login` with empty
creds) and a credentials prompt. The prompt lives in `#authOverlay`
inside `webClient.cpp`; it loops until `/login` accepts the creds, then
reveals the rest of the UI together with the bootOverlay.

The server-side auth chain is the same whichever path fires:
`/login` → `wfrontendLogin` → `ibWebSession::Login` → `reg.Connect` +
`ticket.Attach` → `ProcessAttach` → `appData->AuthenticateUser` (PBKDF2
with MD5 fallback for legacy databases) → `InstallUser` (writes to
both the session's `m_userInfo` and the legacy singleton mirrors).

## Unified session id across web layers

`sessionStorage.oes_tab_sid` (browser), `oes_session` cookie, the
`X-OES-Session` header, `SessionManager::m_sessions` key,
`ibSession::m_id`, and the `sys_session.session` PK all carry the same
dashed-UUID string (`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`).
`ibConnectRequest::m_presetGuid` lets the producer hand a specific id
to `Connect`; the web session path fills it with the client-supplied
tab id so the cluster's row ids visibly match what the browser sees.

## TryProbeRowLock across drivers

The designer-exclusive policy calls
`ibSessionRegistry::ProbeSessionRowLock(guid)`, which delegates to the
driver's `TryProbeRowLock`. Implementations:

- **Firebird** — `SELECT ... WITH LOCK` inside a NOWAIT-configured TX.
- **PostgreSQL** — `SELECT ... FOR UPDATE NOWAIT`.
- **MySQL** — session-level `innodb_lock_wait_timeout = 1` plus
  `SELECT ... FOR UPDATE NOWAIT` (NOWAIT keyword on MySQL 8+, timeout
  is the fallback).
- **ODBC** (targeting MSSQL) — `SET LOCK_TIMEOUT 0` plus
  `SELECT ... WITH (UPDLOCK, ROWLOCK)`.
- **SQLite** — file-level locks only, probe stays a no-op; policy
  falls back to the `lastActive` cutoff.

All implementations wrap the probe in their own TX and ROLLBACK before
returning, so no lock survives the call.

`IsActiveTransaction` now reads `m_transaction_is_active` on the base
class; every driver flips the flag in `BeginTransaction` /
`Commit` / `RollBack`. Firebird keeps its native-handle override as a
belt-and-suspenders check.

## Public API surface — registry as single mutator (2026-04-27)

`ibSession` exports a deliberately narrow public surface; all state
mutation runs through `ibSessionRegistry`. The split lets the
single-consumer registry thread own the state machine without
external code sneaking writes that race with it.

Public on `ibSession` (~15 methods):
- Identity / state reads: `GetId`, `GetKind`, `Identity`, `State`,
  `Auth`, `Reason`, `GetUserInfo` (const), `GetWorkDate`,
  `GetSessionRawPassword` (const), `GetLanguageCode` (const ref).
- Lifecycle for clients: `Open(user, pwd)`, `Close(force)`,
  `Detach`, `SetActivity`.
- Control flags: `RequestCancel` / `IsCancelRequested` /
  `ClearCancel`, `RequestForceExit` / `IsForceExit`, `IsEvalMode` /
  `SetEvalMode`, `IsProcessingBackendError` /
  `SetProcessingBackendError`.
- Public hooks: `Submit(task)`, `IsExclusive` / `SetExclusive`,
  `Server` / `SetServer`, `IsDebug` / `Debug` (debugServer reads
  the debug-loop CV through this).
- Static lookup: `Current`, `CurrentFrame`, `CurrentRunContext`,
  `GetByThread`, `BindSessionToThread` / `UnbindThread`,
  `SetAccessMode` / `GetAccessMode`, `SetFallback` / `ClearFallback`.

Private under `friend ibSessionRegistry`:
- State machine: `Transition`, `TransitionAuth`,
  `WaitForState`, `WaitForAuth`.
- Identity / row tracking: `SetIdentity`, `Inserted`, `SetInserted`.
- Auth-flow mutators (driven only through the façades below):
  `SetUserInfo`, `SetSessionRawPassword`, `ClearSessionRawPassword`,
  `EnableDebug`, `DisableDebug`.

Registry façades — the only public entry points for auth bring-up:

```cpp
ibSessionRegistry::InstallUser(s, info, rawPassword);
//   Writes m_userInfo + raw-password cache. Caller (appData /
//   login dialog) must already be under SessionScope(s).
ibSessionRegistry::EnableDebugForSession(s);
//   Allocates per-session debug slot when --debug starts the session.
```

`appData::Login` / `appData::Connect` route through these instead of
calling session setters directly, so the registry remains the only
mutator of session state.

## Per-session configuration language (2026-04-27)

`ibSession::m_languageCode` (explicit override) plus
`m_resolvedLanguageCode` (cache) replace the old process-static
`ms_strUserLanguage` for *configuration* localization (synonym
translations, form-label picks). Platform localization (wxLocale via
`--locale=`) stays process-wide — those are gettext .mo catalogs and
Web tabs share the same UI translation.

Resolution chain (read-only, hit per metadata-synonym lookup —
millions of times on a 10k-row report):

```
ibSession::GetLanguageCode() const      // inline, single field load
  → m_resolvedLanguageCode               // pre-computed cache
  ← refreshed by SetLanguageCode / SetUserInfo only

ibBackendLocalization::GetActiveLanguage()  // const wxString&
  → if (auto* s = ibSession::Current())
        return s->GetLanguageCode();
    return ms_strUserLanguage;            // process-default fallback
```

`SetActiveLanguage(code)` writes the bound session's language code
when a session is scoped, otherwise updates the process default.
Used by:
- `metadataConfiguration.cpp` — after auth, picks user's preferred
  language or the configuration's default.
- `metaObjectMetadataProperty.cpp` — designer's "Default Language"
  property change.
- Bootstrap (`appData::CreateAppDataEnv`) keeps using
  `SetUserLanguage` directly — no session is bound yet.

## ibUserInfo as the sys_user gateway (2026-04-27)

`ibUserInfo` (renamed from `ibApplicationDataUserInfo`) owns every
sys_user query and serialization path. `appData` is no longer
involved in user CRUD.

```cpp
// Per-record DB I/O
static ibUserInfo Read(const ibGuid&);
static ibUserInfo Read(const wxString& userName);
static bool       Save(const ibUserInfo&);

// Table-wide queries
static bool                HasAny();              // open-access check
static std::vector<Brief>  ListAll();             // login dialog source
                                                  // Brief = guid+name+fullName

// Buffer I/O (configuration export/import)
void              Serialize(ibWriterMemory&) const;
static ibUserInfo Deserialize(ibReaderMemory&);
```

`Brief` replaces the historical `ibApplicationDataShortUserInfo` —
projection of guid/name/fullName, no binaryData blob crack.

## ibSessionSnapshot (2026-04-27)

Cluster-wide sys_session snapshot — renamed from
`ibApplicationDataSessionArray` and relocated to
`backend/session/sessionSnapshot.{h,cpp}`. Producer is the registry's
`JobRefreshSnapshot`; consumers call
`ibSessionRegistry::Instance().GetClusterSnapshot()` directly
(`appData::GetSessionArray` removed — it was a thin proxy).

Aggregate helpers added on top of per-row accessors:

```cpp
bool         HasActiveUsers();           // any non-empty userName
                                         // (excludes technical wes-server row)
bool         IsUserActive(name);          // user logged in anywhere in cluster
unsigned int CountByKind(ibSessionKind);  // rows of given kind
```

Header stays independent of `session.h` — `ibSessionKind` is
forward-declared with explicit `int` underlying type, full enum
needed only in `.cpp` for `static_cast`.

## Session list label

`ibApplicationDataSessionArray::GetApplication(idx)` must disambiguate WebServer vs WebClient on web because both share `ibRunMode::eWEB_ENTERPRISE_MODE`. Implementation: when the stored run-mode is `eWEB_ENTERPRISE_MODE`, pick "Web client" if `m_kind == ibSessionKind::WebClient (100)`, otherwise "Web server". Other run modes fall through to `GetRunModeDescr`. `m_kind` is kept as `int` in `ibApplicationDataSessionUnit` to avoid pulling `session.h` into `appData.h`.

## Testing

**Smoke test on embedded FB (fb_test251):**

```bash
cd bin/Win32/Debug
./wenterprise-server.exe --file=F:/projects/oes-bin/examples/fb_test251 --port=8080 &
# expect: "OES wenterprise-server listening on http://localhost:8080/w/fb_test251/"

curl -s -c /tmp/ck http://127.0.0.1:8080/w/fb_test251/                      # 200, HTML shell
curl -s -b /tmp/ck -X POST http://127.0.0.1:8080/w/fb_test251/login \
  -d "user=&password="                                                       # 200, "ok (<cookie> / )"
curl -s -b /tmp/ck http://127.0.0.1:8080/w/fb_test251/session                # 200, authenticated=true tabCount=2
```

**Smoke test on server-mode PG (Configuration):** same pattern, swap `--file=` for `--server=... --db=Configuration`.

## Related memory notes

- `project_session_registry_refactor.md` — design history + full commit list.
- `project_web_session_bug.md` — old bug (multi-tab last-login-wins), closed by commit 11.
- `feedback_no_passwords_in_db.md` — policy: hash only, plain-text in-memory only.
- `reference_session_raw_password.md` — why we still need `m_sessionRawPassword`.
- `reference_empty_username_meanings.md` — 3 cases of empty userName in sys_session.
