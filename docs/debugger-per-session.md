# Per-session debugger — architecture, wake protocol, and one open heisenbug

This documents the debugger's per-session migration (so the server-global
mirrors don't creep back) and, more importantly, captures an **open
freed-memory crash** with enough nuance to resume fast instead of re-deriving
it from dumps.

## Why per-session

The debug server (`ibDebuggerServer`, process-level singleton, owned by
`ibMetaDataConfiguration`) used to keep park state in process-global fields.
That breaks `wenterprise-server` (wes), where several browser tabs are
independent sessions that may each be stopped at their own breakpoint at the
same time. Park state therefore lives on the **session**:

```
ibSession::ibDebugSession            // session.h, allocated by EnableDebug()
  std::atomic<bool> m_debugLoop;     // script thread parked in DoDebugLoop's CV wait
  ibRunContext*     m_runContext;    // frame currently stopped at the breakpoint
  std::condition_variable m_cv;      // per-session — sibling tabs don't cross-wake
  std::mutex        m_mutex;
  std::map<u64, wxString> m_expressions;  // watch (still process-level on server today)
```

Which session is "the one a debug command targets" is resolved two ways, and
they must agree:

- **By thread:** on the debug-server connection thread, `ibSession::Current()`
  redirects to `GetActiveDebugTarget()` — the front of the registry's debug
  queue (`EnterDebugLoop`/`LeaveDebugLoop`, FIFO of `weak_ptr<ibSession>`).
  Eval / tooltip / autocomplete / `SetStack` all reach the parked session
  through this redirect, no explicit sid threaded through.
- **By sid:** loop-entry/leave packets carry `sess->GetId()`; the designer
  echoes it back on Continue/Step/Pause. `ibDebuggerServer::WakeDebugSession`
  resolves it via `ibSessionRegistry::Find(sid)`.

## What landed (debugger arc, 2026-06-05 → 06)

- **3b — `EvalInParkedSession`** (`debugServer.cpp`): every debug-thread eval
  site (watch / tooltip / expand / autocomplete) locks `dbg->m_mutex`, gates on
  per-session `dbg->m_debugLoop`, reads `dbg->m_runContext`, runs
  `ibProcUnit::Evaluate` **under the lock**. Replaces the old
  `IsDebugLooped() + ibSession::CurrentRunContext()` cross-thread TOCTOU that
  use-after-freed a worker-stack `ibRunContext` (the original 0xdd crash).
  `DoDebugLoop`'s leave block + `SetStack` take the same lock.

- **3c — removed server-global `ibDebuggerServer::m_runContext` mirror.** SSOT
  is per-session `dbg->m_runContext`. `SendExpressions`/`SendLocalVariables`
  take the frame as an **explicit `ibRunContext*` parameter** sourced from the
  per-session pointer at the call site — *not* an ambient `Current()->Debug()`
  lookup, which would re-lock `dbg->m_mutex` recursively on the `SetStack` path
  (= deadlock). `SendStack` was already per-session via `GetPUState()`.

- **3d — removed server-global `m_bDebugLoop`** (park flag) and the **dead**
  `m_debugLoopCV` + `m_debugLoopMutex` (only `notify_all`, zero waiters —
  `DoDebugLoop` waits on per-session `dbg->m_cv`). `IsDebugLooped()` now
  resolves per-session via `ibSession::Current()->Debug()->m_debugLoop`
  (body in .cpp; header only forward-declares `ibSession`). The wait loop is
  `while (dbg->m_debugLoop) { ... cv.wait_for(250ms, !m_debugLoop) }`.
  `m_bDoLoop` (stepping flag, distinct from park) is untouched.

- **Continue/Step/Pause regression + fix.** Removing the global flag exposed
  that `WakeDebugSession(sid)` had **always been a no-op**: it resolved through
  `ibSessionRegistry::Find()` → the **legacy `m_sessions` map**, which is
  written only by `Create(id,runMode)` — and that overload is *never called
  anywhere*, so the map is always empty and `Find` always returned null.
  Continue/Step had been resuming purely via the global flag. Real sessions
  live in **`m_own`** (keyed by `GetId()`). Fix: `Find()` rewritten to look up
  `m_own` under `m_ownMutex`; `WakeDebugSession` + `Pause` fall back to
  `GetActiveDebugTarget()` on miss so a sid drift never strands the worker.
  Pause's hard-abort (`CancelSession`) — previously silently dead — works now,
  as does `wfrontendSessionPaused` (the only other `Find` caller).

- **Dead legacy session API removed:** `Create`/`Destroy`/`List`/`Count` +
  `m_sessions` map + its `m_mutex` (all unreferenced once `Find` moved to
  `m_own`).

## Wake / drain protocol (single source of truth now)

Every exit from a parked `DoDebugLoop` flips the **per-session** flag + kicks
the **per-session** CV. No global flag remains:

| Trigger | Path |
|---|---|
| Continue / StepInto / StepOver | `WakeDebugSession(sid)` → `Find(m_own)` (+ active-target fallback) → `d->m_debugLoop=false; d->m_cv.notify_all()` |
| Detach / Destroy | `WakeAllDebugSessions()` (iterates `SnapshotByThread()`/`m_own`) |
| ResetDebugger (connection loss) | `WakeAllDebugSessions()` |
| ShutdownServer (server recreate / teardown) | `WakeAllDebugSessions()` — **added in 3d**; it used to rely on the global flag and did *not* drain per-session |

A 250 ms safety tick in the wait loop guarantees the worker re-checks
`dbg->m_debugLoop` even if a notify is missed — so a correctly-cleared flag
always resumes within 250 ms. A worker that hangs forever therefore means the
**flag was never cleared** (i.e. wake didn't reach the session) — that was the
Continue regression's signature.

## OPEN BUG — floating 0xdd freed-memory in the debugger at startup

**Status:** open, deferred. Not a 3c/3d regression (the faulting code paths
were not touched). Floating / timing-dependent — does not reproduce every run.

A breakpoint hit during the **startup form build** (BeforeStart → main module →
`ShowCommonForm` → `GetCommonForm` → `GetObjectForm` → `CreateAndBuildForm` →
`ibValueForm::InitializeFormModule` → module `Run` → `EnterDebugger` →
`DoDebugLoop`) crashes reading `0xdddddddd` (MSVC freed-heap fill). Two faces
seen so far — same family (the debugger touches an object freed out from under
it while a session is parked at the first startup breakpoint):

**Face A — watch eval.** `SendExpressions` / expand → `ibProcUnit::Evaluate` →
`backend!CopyValue` with `ecx = 0xdddddddd` (`mov dl,[ecx+8]`). The `runContext`
itself is valid; the dangling pointer is *inside the value the watch resolves
to*. First seen 2026-06-05 expanding `thisForm`.
Dump: `bin/Win32/Debug/crashdumps/enterprise_10436_t20556_20260605T234300_0.dmp`.

**Face B — loop packet build.** `DoDebugLoop+0x344` → `ibWriter::w_stringZ`,
`ecx=edx=0xdddddddd`. `DoDebugLoop(strDocPath, strModuleName, ...)` is called
as `DoDebugLoop(byteCode.m_strFileName, byteCode.m_strDocPath, ...)` — i.e. the
two `wxString` params are **`const&` references into `byteCode`'s fields**,
held across the CV wait. The fault is in the **LeaveLoop** block (after the
wait, large offset) → the `byteCode` owner was freed while the session was
parked, so the references dangle. Dump:
`bin/Win32/Debug/crashdumps/enterprise_22408_t12920_20260606T002418_0.dmp`.

**Root hypothesis:** the startup form module's bytecode (or its owning
`ibByteCode`/module) is freed/rebuilt while the worker is parked at the first
breakpoint. Whoever frees it is not yet identified — needs a free-stack.

### How to pin it (don't re-derive — do this)

1. **PageHeap** turns the heisenbug deterministic:
   `gflags /p /enable enterprise.exe /full`, run with `--debug`, break at
   startup, hit the path. The AV then lands on the *real* dereference with a
   live allocation stack; `!heap -p -a <addr>` gives alloc + free stacks →
   the freer. `gflags /p /disable enterprise.exe` when done.
2. **cdb gotcha:** `dx` / `dv` on STL types (e.g. dumping `m_listExpression`
   or frame locals) **hangs** offline (pulls STL natvis/symbols over the
   network even with a local-only sympath). Use `.ecxr; kn; r` and raw memory
   reads instead; keep symbol path local (`-y <bin\Win32\Debug>`), no srv*.

### Cheap targeted mitigation (Face B only, ~3 lines)

Copy `strDocPath` / `strModuleName` into local `wxString` values at the top of
`DoDebugLoop` (while `byteCode` is still alive) and use the copies in both the
EnterLoop and LeaveLoop packet blocks. That makes the packet send survive a
`byteCode` free during the park — kills Face B without knowing the freer. It
does **not** fix Face A (watch eval) or the underlying premature free, so it's
hardening, not a root-cause fix.

See also memory note `reference_debugger_parked_eval_uaf` and
[debug protocol multiplex](#) (`debugServer.cpp` / `debugClient.cpp`).
