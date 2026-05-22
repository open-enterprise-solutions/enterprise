# Worker pool — thread_local audit

> **Status:** the pool itself shipped in v1.3.0 (`ibWorkerPool` +
> `ibWorkerPoolHeadless` in `backend/session/`, per-session `ibProc
> UnitState` lease swap). `ibWebApplication::WorkerLoop` /
> `StartWorker` / `StopWorker` are gone — web sessions dispatch
> through `ibSession::Submit` which forwards to the pool. This
> document remains useful as the **audit record** that decided
> what gets saved / restored / cleared at session swap; the
> prescriptions in §"Pool contract" are the contract the shipped
> pool implements.

Prerequisite analysis for the (now-landed) replacement of the
per-session worker thread (web's former `ibWebApplication::WorkerLoop`)
with a process-level shared `ibWorkerPool` that swaps sessions on a
fixed set of worker threads. The same pool is the substrate for the
future compute server (`oes-server.exe`) and stays in single-worker
form on desktop GUI (`enterprise.exe`) to keep wxApp's main-loop
semantics.

This document enumerates every `thread_local` (and thread-bound static)
in our own code, classifies it, and prescribes the migration.
Connection pool and session registry are not in scope — they're the
prerequisite stages and are already correct.

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

### Session-bound script state — must save/restore on swap

All four live in `src/engine/backend/compiler/procUnit.cpp`. They form
the script interpreter's per-thread execution context: which `ibProcUnit`
is currently running on this thread, the run-context stack (script call
stack), the current error site, and the recursion counter.

| Symbol | Location | Type | Purpose |
|---|---|---|---|
| `tl_currentRunModule` | `procUnit.cpp:48` | `ibProcUnit*` | Currently-executing module on this thread. Read by every opcode dispatch site to resolve "which module's bytecode are we in". |
| `tl_runContext` | `procUnit.cpp:49` | `std::vector<ibRunContext*>` | Script call stack — pushed by `ibProcStackGuard` ctor on every script frame entry, popped by dtor. Read by error reporting (Raise, stack walk) and by `GetCurrentRunContext`. |
| `s_errorPlace` | `procUnit.cpp:93` | `ibErrorPlace` (small POD) | Where the most recent script exception was raised; used by ProcessError to format the rethrow. |
| `s_nRecCount` | `procUnit.cpp:106` | `short` | Recursion-depth counter, gates against runaway scripts (`MAX_REC_COUNT`). |

**Why save/restore, not "move onto `ibSession`":** every opcode dispatch
in the interpreter reads `tl_currentRunModule` / `tl_runContext.back()`.
Replacing those reads with `Current()->m_runContext.back()` adds a
shared-mutex shared-lock + map lookup per opcode, which is several
orders of magnitude slower than direct TLS access (low single-digit
nanoseconds vs hundreds). The hot path must stay TLS-direct.

**Migration shape:**

```cpp
// New struct on ibSession.
struct ibProcUnitState {
    ibProcUnit*                 currentRunModule = nullptr;
    std::vector<ibRunContext*>  runContext;
    ibErrorPlace                errorPlace;
    short                       recCount = 0;
};

class ibSession {
    ibProcUnitState m_procUnitState;
public:
    ibProcUnitState& ProcUnitState() { return m_procUnitState; }
};
```

Worker boundary:

```cpp
// Inside the worker, just before/after running a task on session S.
void Worker::EnterTask(ibSession* s) {
    if (m_pinned) {
        // Save outgoing TLS into outgoing session.
        m_pinned->ProcUnitState().currentRunModule = tl_currentRunModule;
        m_pinned->ProcUnitState().runContext.swap(tl_runContext);
        m_pinned->ProcUnitState().errorPlace = s_errorPlace;
        m_pinned->ProcUnitState().recCount   = s_nRecCount;
    }
    m_pinned = s;
    if (s) {
        // Load incoming TLS from incoming session.
        tl_currentRunModule = s->ProcUnitState().currentRunModule;
        tl_runContext.swap(s->ProcUnitState().runContext);
        s_errorPlace        = s->ProcUnitState().errorPlace;
        s_nRecCount         = s->ProcUnitState().recCount;
    }
}
```

`std::vector::swap` is two pointer swaps; cost is negligible.
`ibErrorPlace` is small POD; copy is trivial. `short` is one register.
The whole swap is O(1) per session boundary.

**When swap happens, when it doesn't:**
- Worker pulls a task for session A → enters task → drains all queued
  tasks for A while leased → leaves task. One swap-in + one swap-out
  per lease, not per task.
- Subsequent worker re-leases A → restores A's state. Subsequent worker
  picks B → swaps A out, B in.
- Single-worker desktop (`enterprise.exe`): only one session ever
  pinned, swap never fires — TLS stays untouched. Same speed as today.

**Reentrancy.** A task running on session A that synchronously
re-dispatches into A (script calls helper that needs to enqueue another
A task) **must reuse the current worker** — not enqueue and wait. Otherwise
the worker dispatcher would block on its own queue while holding the
lease, deadlocking. Solution: `pool->Submit(s, fn)` checks "am I the
worker currently leasing s?" and if so, runs `fn` inline. No swap
because state is already correct.

---

### Per-task transient flags — reset on task entry

Both live in `src/engine/backend/backend_exception.cpp:95`:

| Symbol | Purpose |
|---|---|
| `gs_evalMode` | Set during debug-watch / Eval evaluation so side-effecting calls (UpdateForm, dialogs, OLE) self-suppress. Read by every potentially-side-effecting builtin. |
| `gs_processBackendError` | Re-entrancy guard for `ibBackendException::ProcessError` — prevents a logging path from re-throwing into itself. |

These are **not session-bound** semantically — they're per-call-frame
flags that happen to live as TLS because the call frame == thread today.
After worker pool, they must reset at task entry so a leftover `gs_evalMode=true`
from session A's debug-watch doesn't make session B's regular OnWrite
silently no-op its UpdateForm.

**Migration shape:** clear at the top of `EnterTask` (or equivalently at
the bottom of `ExitTask`). No save needed — they're meant to be transient
within a script frame, and a script frame is fully drained before the
worker leaves the task.

```cpp
void Worker::EnterTask(ibSession* s) {
    // Save/restore session-bound state (above).
    // Then clear transient flags.
    ibBackendException::SetEvalMode(false);
    ibBackendException::SetProcessingBackendError(false);
}
```

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

### Per-thread DB connection pinning — keep, by design

`src/engine/backend/databaseLayer/connectionPool.cpp:16-17`:

```cpp
thread_local std::shared_ptr<ibDatabaseLayer> s_tlCurrent;
thread_local std::shared_ptr<ibDatabaseLayer> s_tlActiveTx;
```

`s_tlActiveTx` is set when `BeginTransaction` flips `m_txDepth 0→1`, cleared
when `Commit`/`RollBack` flips `1→0`. `s_tlCurrent` is set by
`ibConnectionScope` ctor (outermost) and cleared by dtor.

The contract: a TX is connection-bound; while it's open, every
`db_query` on this thread routes back to the same connection. The TX
must complete on the same worker that started it.

**Worker pool effect:** as long as a task that opens a TX completes the
TX before yielding the worker, both TLS slots are empty between tasks
and the next task on the worker starts clean. Per-session sequential
dispatch (single-in-flight) guarantees this — a task never gets
preempted mid-script. **No migration needed.**

The one case to watch: a worker pulling task for session A opens a TX,
the script *yields* (e.g. waits on an external resource — none in
current code, but plausible later), pool releases worker, worker picks
task for B. Now B sees A's TX on TLS. **Mitigation:** the pool MUST NOT
release a worker while a TX is open. `s_tlActiveTx != nullptr` is a
hard precondition for "stay leased on this session". This belongs in
the pool's contract, not in the TLS audit.

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

The audit settles four hard invariants the pool must enforce:

1. **Worker leases a session for a contiguous run.** Save/restore of
   procUnit state happens once per lease, not per task. Cheap because
   leases are short (drain queue then release) but tasks within a lease
   share state.
2. **A task with an open transaction blocks worker release.** While
   `s_tlActiveTx != nullptr`, the worker cannot yield to another
   session. This is a soft constraint — script paths today complete
   their TX synchronously; future code that wants to keep a TX open
   across awaits must explicitly use a different mechanism.
3. **Reentrant `Submit(currentSession, fn)` runs inline.** Avoids the
   self-deadlock where a worker enqueues into its own active session's
   queue and waits for itself.
4. **Transient flags clear on task entry.** `gs_evalMode`,
   `gs_processBackendError` — and any future flag that's per-call
   semantically.

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

## Migration order

The pool refactor itself comes after this audit's prescriptions land:

1. **Add `ibProcUnitState` on `ibSession`.** Empty default. No reads
   from it yet — all hot paths still go to the existing TLS.
2. **Add `Worker::EnterTask` / `ExitTask` swap helpers** that load/save
   from `m_session->ProcUnitState()`. Wire them into the existing
   per-session worker (`ibWebApplication::WorkerLoop`) as a no-op
   verification — workers today serve a single session for their
   whole life, so swap is a save/restore pair around a single-element
   sequence; the test is "behaviour unchanged".
3. **Audit pass — find any TLS/static this document missed.** Run the
   full smoke test under both `enterprise.exe` and
   `wenterprise-server.exe` with verbose logging on TLS save/restore;
   confirm no orphan TLS in script paths.
4. **Build `ibWorkerPool`** per `project_worker_pool_plan` memory note —
   GUI backend, headless backend, `Submit(session, task)` API.
5. **Swap `ibWebApplication`** off its dedicated thread onto the pool.
   Per-session FIFO queue + lease semantics enforce single-in-flight.
6. **Audit pass again** under load (concurrent sessions, mixed
   long/short scripts, debug attach, metadata reload).

Steps 1-3 are the audit's deliverable. Steps 4-6 are the actual
worker pool refactor.
