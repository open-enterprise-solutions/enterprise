# Worker pool — thread_local audit

> **Status:** LANDED. The pool shipped (`ibWorkerPool` +
> `ibWorkerPoolHeadless` in `backend/session/`).
> `ibWebApplication::WorkerLoop` / `StartWorker` / `StopWorker` are
> gone — web sessions dispatch through `ibSession::Submit` which
> forwards to the registry's pool.
>
> **What actually shipped differs from the prescription below.** This
> document's "Migration shape" (a per-session `ibProcUnitState` mirror
> with explicit *save/restore at the worker boundary*) was the original
> plan. The implementation went a simpler route: interpreter state is
> **owned by the session** (`ibSession::m_procUnitState`) and every read
> goes through `ibSession::GetPUState()` → `ibSession::Current()`. The
> worker binds the session via `ibSessionScope` + `tl_currentLease`
> (`workerPoolHeadless.cpp:16`) before draining its queue, so
> `GetPUState()` transparently resolves to the right session with **no
> swap step**. The four interpreter thread_locals named in the inventory
> below (`tl_currentRunModule`, `tl_runContext`, `s_errorPlace`,
> `s_nRecCount`) **no longer exist in `procUnit.cpp`** — they were moved
> onto `ibProcUnitState` (`backend/compiler/procUnitState.h`). Treat the
> inventory as the historical audit that drove the design; the
> "Verdict" / "Pool contract" invariants still hold.

Prerequisite analysis for the (now-landed) replacement of the
per-session worker thread (web's former `ibWebApplication::WorkerLoop`)
with a process-level shared `ibWorkerPool`. The same pool is the
substrate for the future compute server (`oes-server.exe`) and a
`ibWorkerPoolGUI` scaffold exists in `frontend/session/` for desktop.

This document enumerates every `thread_local` (and thread-bound static)
in our own code as of the audit, classifies it, and records the
migration that landed. Connection pool and session registry are not in
scope — they're the prerequisite stages and are already correct.

> **Scope note.** Third-party TLS (Firebird `fb_*` thread state, MySQL
> `pthread_key_*` shims, wxWidgets log target, Win32 `TlsAlloc` etc.)
> is intentionally excluded. Those are connection-bound or
> library-internal; they'll be revisited only if a worker swap surfaces
> a concrete bug.

---

## Verdict per category

| Category | Migration policy |
|---|---|
| **Session-bound script state** | **Save/restore at session swap.** Worker boundary loads from the prev session, stores into the new one. Hot-path access stays TLS-direct (no `Current()` lookup per opcode). |
| **Per-task transient flags** | **Reset on task entry.** Not session-bound; should not survive across tasks even on the same thread. |
| **Per-thread scratch buffers** | **Keep as-is.** Reused across tasks on the same worker, no session affinity, no cross-task leakage. |
| **Per-thread DB connection pinning** | **Keep as-is.** Already correctly cleared at task end (TX commit/rollback). The pool's TLS slot was designed for this exact ownership model. |
| **Per-thread session binding** | **Keep as-is.** This is the swap mechanism we'll *use*, not a thing to migrate. |

---

## Inventory

### Session-bound script state — moved onto the session (not swapped)

All four lived in `src/engine/backend/compiler/procUnit.cpp` at audit
time as thread_locals. They form the script interpreter's execution
context: which `ibProcUnit` is currently running, the run-context stack
(script call stack), the current error site, and the recursion counter.

| Symbol (audit-era) | Type | Purpose |
|---|---|---|
| `tl_currentRunModule` | `ibProcUnit*` | Currently-executing module. Read by every opcode dispatch site to resolve "which module's bytecode are we in". |
| `tl_runContext` | `std::vector<ibRunContext*>` | Script call stack — pushed on every script frame entry, popped on exit. Read by error reporting (Raise, stack walk). |
| `s_errorPlace` | `ibErrorPlace` (small POD) | Where the most recent script exception was raised; used by ProcessError to format the rethrow. |
| `s_nRecCount` | `short` | Recursion-depth counter, gates against runaway scripts. |

**What landed:** all four moved into `struct ibProcUnitState`
(`backend/compiler/procUnitState.h`), one instance owned by each
`ibSession` (`ibSession::m_procUnitState`). The interpreter reads them
through `ibSession::GetPUState()` (`session.cpp:452`), which returns
`&Current()->m_procUnitState`, or a `thread_local` fallback
`ibProcUnitState` when no session is bound (codeRunner / CLI ad-hoc
scripts). The four interpreter thread_locals named above **no longer
exist in procUnit.cpp** — grep confirms the only references are in
comments. Methods like `GetLambdaRuntime` / `Raise` / `AddRunContext`
live on `ibProcUnitState` (out-of-line in procUnit.cpp because they
need the full `ibRunContext` / `ibByteCode` types).

**Why session-owned, not boundary-swap (the original plan was swap):**
because the session is pinned to the worker thread for the whole lease
(`ibSessionScope` + `tl_currentLease`), `Current()` already resolves to
the right session inside every task. State lives where the session
lives, so there is nothing to save or restore at the boundary — the
swap helpers the original plan called for were never needed. The cost
moves from "two vector swaps per lease" to "one `Current()` resolve per
`GetPUState()` call"; in Single access-mode (desktop) `Current()` is a
single load of the lone session, and in Shared mode (wes) it is a
shared-lock map lookup keyed by thread id.

**Reentrancy.** A task running on session A that synchronously
re-dispatches into A runs **inline** on the current worker rather than
enqueuing — see `ibWorkerPoolHeadless::Submit`
(`workerPoolHeadless.cpp:90`, the `tl_currentLease == session` check).
Otherwise the worker would block on its own queue while holding the
lease, deadlocking.

---

### Per-task transient flags — reset on task entry

Both live in `src/engine/backend/backend_exception.cpp:95`:

| Symbol | Purpose |
|---|---|
| `gs_evalMode` | Set during debug-watch / Eval evaluation so side-effecting calls (UpdateForm, dialogs, OLE) self-suppress. Read by every potentially-side-effecting builtin. |
| `gs_processBackendError` | Re-entrancy guard for `ibBackendException::ProcessError` — prevents a logging path from re-throwing into itself. |

**What landed:** instead of clearing per-task flags, both moved onto
`ibSession` as atomics: `m_evalMode` / `m_processingBackendError`, with
`IsEvalMode` / `SetEvalMode` / `IsProcessingBackendError` /
`SetProcessingBackendError` accessors (`session.h:431-438`). Per-session
storage is stricter than the original "reset on task entry" plan — a
debug-watch on tab 1 setting eval-mode can never leak into tab 2's
OnWrite, regardless of which worker runs which task. The thread_local
`gs_evalMode` / `gs_processBackendError` in backend_exception.cpp are
gone; `ibBackendException::IsEvalMode()` resolves per-session.

**Caveat — debug eval.** When the debugger evaluates a watch expression
mid-breakpoint, the script thread is parked in `DoDebugLoop`'s CV wait;
the debug command thread sets `gs_evalMode = true`, runs the eval, clears
it. With per-session debug (`ibSession::ibDebugSession`) already in place,
the debug command runs on a separate thread — `gs_evalMode` on that
thread is independent. No regression. But the *script* thread that's
parked must not have `gs_evalMode` set when it resumes. Today it doesn't
(eval thread is a different OS thread); after pool, eval-on-pooled-worker
needs to ensure `gs_evalMode` is local to its task.

---

### Per-thread scratch buffers — keep

All in `src/engine/backend/backend_localization.cpp`. These are output
buffers for translation routines, declared `thread_local` so common-case
calls don't grab a mutex:

| Line | Symbol | Type |
|---|---|---|
| 69 | `entry` | `ibBackendLocalizationEntry` |
| 112, 223 | `array` | `ibBackendLocalizationEntryArray` |
| 155 | `strRawTranslate` | `wxString` |
| 180 | `code, data` | `wxString, wxString` |
| 236, 244, 267 | `strResult, result` | `wxString` |

These hold per-call output, are populated and consumed within a single
function call, never observed across calls. A worker swapping sessions
doesn't see leakage because the next translation call overwrites the
buffer fully before it reads. Migration is **no action**.

---

### DB connection pinning — holder-keyed, not thread_local

> **Superseded by the holder-keyed pool.** The audit-era plan kept two
> thread_local conn slots (`s_tlCurrent` / `s_tlActiveTx`). The pool was
> refactored (2026-04-28) to a holder-keyed model: reservations live in
> `ibConnectionPool::m_entries` keyed on `ibDatabaseConnectionHolder*`
> identity, not on the thread. See `connection-pool.md`.

A TX is connection-bound, and the binding follows the **holder**
(an `ibSession` is a holder; non-session work uses the per-thread
`ThreadHolder` singleton). While the holder's TX is open, every
`db_query` / `ses_query` for that holder routes back to the same
connection — across worker-thread crossings, because the key is the
holder pointer, not the thread.

**Worker pool effect:** the session that owns the conn is pinned to the
worker for the whole lease, so the holder identity and the executing
thread agree for the duration of the TX. Per-session single-in-flight
dispatch guarantees a script completes (and closes its TX) before the
worker is released. **No migration needed.**

The pool's own remaining thread_local is `ts_holder` in
`connectionPool.cpp:17` — a per-thread `ibSingleConnectionHolder` that
backs `ThreadHolder()` (the `db_query` channel for non-session DDL/CLI
work). It is connection-lifetime infrastructure, not interpreter state.

---

### Per-thread session binding — keep, this IS the mechanism

`src/engine/backend/session/session.cpp` (file-static, namespace-scope):

```cpp
std::unordered_map<std::thread::id, std::weak_ptr<ibSession>> s_currentByThread;
std::shared_mutex s_currentMutex;
```

Read by `ibSession::Current()`, written by `BindSessionToThread` /
`UnbindThread` / `ibSessionScope`. **This is the swap primitive itself.**
The worker pool calls `BindSessionToThread(newSession)` at task-enter
and `UnbindThread()` at task-leave (or `BindSessionToThread(prevSession)`
to swap rather than unbind).

`weak_ptr` storage means a destroyed session expires harmlessly even if
the worker forgets to unbind — landed 2026-04-27.

---

## Pool contract — derived from the audit

The audit settled four hard invariants the pool enforces (how each
landed in parentheses):

1. **Worker leases a session for a contiguous run** — drain its queue
   under one atomic lease, then release. Because interpreter state is
   session-owned (`m_procUnitState`), there is nothing to save/restore at
   the lease boundary; the lease just guarantees single-in-flight per
   session.
2. **A task with an open transaction must finish it before the worker
   yields.** The holder's TX reservation is connection-bound and the
   session is pinned to the worker for the lease, so a synchronous script
   completes its TX before release. (No yield-mid-TX path exists in
   current code; future await-style code would need a different
   mechanism.)
3. **Reentrant `Submit(currentSession, fn)` runs inline** — implemented
   via `tl_currentLease` in `workerPoolHeadless.cpp:90`. Avoids the
   self-deadlock where a worker enqueues into its own active session's
   queue and waits for itself.
4. **Transient flags are session-owned** — `m_evalMode` /
   `m_processingBackendError` atomics on `ibSession` (stricter than the
   audit's "clear on task entry" plan; the `gs_*` thread_locals are
   gone).

---

## Out of scope (for the audit, not for the worker pool overall)

- **Cancellation.** `ibProcUnit` needs a `m_cancelRequested` atomic the
  interpreter checks per N opcodes, settable by the dispatcher to abort
  a runaway script. Independent change.
- **GUI dual-pool.** `enterprise.exe` should keep wxTheApp main-thread
  as the sole script worker (single-worker pool) AND dispatch heavy
  ops to a separate background pool to avoid UI freeze. Separate
  design from the script-side TLS migration.
- **Connection lifecycle for thin client.** A thin client's TCP
  connection going dormant, reconnecting later, and the server's
  outbox of buffered events — this is in
  [`compute-server-tiering.md`](compute-server-tiering.md) Phase 5,
  not in TLS scope.

---

## Migration order (as it actually landed)

1. **`ibProcUnitState` on `ibSession`** — interpreter state moved off
   the four procUnit thread_locals onto a session-owned struct
   (`backend/compiler/procUnitState.h`), read through
   `ibSession::GetPUState()` (session-resolved, with a thread_local
   fallback for sessionless hosts). No boundary swap helpers — the
   original `EnterTask`/`ExitTask` save/restore plan was dropped because
   the session is already pinned to the worker for the lease.
2. **Transient flags onto `ibSession`** — `m_evalMode` /
   `m_processingBackendError` as atomics; the `gs_*` thread_locals in
   backend_exception.cpp removed.
3. **`ibWorkerPool` abstract + `ibWorkerPoolHeadless`** in
   `backend/session/`. `Submit(session, task)` API, per-session FIFO
   queue + atomic lease, lazy worker spawn to `maxWorkers`, idle-shrink
   at 60s, reentrant inline Submit. `ibWorkerPoolGUI` scaffold in
   `frontend/session/` (not auto-installed).
4. **`ibWebApplication` migrated** off its dedicated per-session thread
   onto the registry's pool — `StartWorker`/`StopWorker`/`WorkerLoop`
   deleted; `PostWork`/`RunOnWorker` forward through `Submit`.

Connection-pool and session-registry were the prerequisite stages and
were correct before this work started.
