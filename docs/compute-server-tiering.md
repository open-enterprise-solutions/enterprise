# Compute server — 3-tier architecture plan

Roadmap for splitting OES into a 3-tier application: thin presentation
clients (GUI + web frontend) talking over a network protocol to a
dedicated compute server that owns runtime state, worker pool, and
connection pool. Same shape as other enterprise low-code platforms with a compute-server tier.

> **Status:** Phase 1 (connection pool) and Phase 2 (in-process worker
> dispatcher — `ibWorkerPool` + per-session queue/lease) are LANDED.
> Phase 3+ (transport abstraction `ibIncomingServer`, binary TCP RPC
> frame, `oes-server.exe`, thin client) are NOT started — confirmed
> absent in tree (no `ibIncomingServer` / `oes-server` / RPC frame).
> Current OES runs standalone (GUI + runtime + DB in one process) or as
> the wes web server; 3-tier is a scaling option, not a mandatory
> migration.

---

## Target architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ Presentation tier                                                │
│ ┌────────────────┐  ┌──────────────────┐  ┌────────────────┐    │
│ │ enterprise-thin│  │ nginx / Apache   │  │ HTTP-API direct│    │
│ │ (wx GUI only)  │  │ (static+proxy)   │  │ (3rd party)    │    │
│ └────────┬───────┘  └─────────┬────────┘  └────────┬───────┘    │
│          │                    │                    │             │
└──────────┼────────────────────┼────────────────────┼─────────────┘
           │     OES binary RPC protocol (TCP/TLS)   │
           ▼                    ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│ Application tier — compute server (oes-server.exe)               │
│  • Incoming protocol layer (TCP accept + frame parse)            │
│  • ibRequestDispatcher — worker pool + per-session queues        │
│  • ibSessionRegistry — session lifetime + auth                   │
│  • Per-session ibSession — runtime state (ProcUnits, locals)     │
│  • ibConnectionPool — shared DB conns                            │
│  • Metadata cache — shared across sessions                       │
│  • Debugger server (existing TCP 1650)                           │
└────────────────────────┬────────────────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ Data tier — DBMS (Firebird / PG / MSSQL)                          │
└─────────────────────────────────────────────────────────────────┘
```

**Standalone mode retention:** `enterprise.exe` (current) = thin GUI +
in-process compute. Users without network/cluster setup run it as one
binary. The compute-server module is shared code, linked into both
`enterprise.exe` (embedded) and `oes-server.exe` (network).

---

## Component inventory

| Component | Purpose | Status |
|---|---|---|
| `ibConnectionPool` | Shared DB conns | ✅ Landed |
| `ibConnectionScope` | Per-request RAII | ✅ Landed |
| Nested-TX counter | Atomicity across scopes | ✅ Landed |
| TX TL pinning | Thread-sticky TX | ✅ Landed |
| `ibSessionRegistry` | Session lifetime | ✅ Landed (DB-level) |
| `ibSession` | Session identity + state + runtime root (`m_root`) + interpreter state (`m_procUnitState`) | ✅ Landed |
| `ibWorkerPool` / `ibWorkerPoolHeadless` | Bounded worker pool + per-session FIFO queue + lease | ✅ Landed (`backend/session/`) |
| **Binary RPC protocol** | Client ↔ server frame format | 🔲 Missing |
| **`ibIncomingServer`** | Transport abstraction (TCP accept + frame parse) | 🔲 Missing |
| **`oes-server.exe`** | Standalone compute-server binary | 🔲 Missing |
| **`enterprise-thin.exe`** | Thin GUI client | 🔲 Missing |

---

## Current vs target (request-processing flow)

### Current (2-tier, one-process)

```
User click in enterprise.exe (main thread)
 └─ WriteObject on main thread
    ├─ ibConnectionScope = pool checkout (own conn from pool)
    ├─ SafeBeginTransaction ...
    └─ ~scope → repark

Web request on wenterprise-server (per-session worker thread)
 └─ one OS thread per logged-in session
    └─ 1000 sessions = 1000 threads = scaling ceiling
```

### Target (3-tier, shared workers)

```
1000 concurrent sessions on oes-server.exe
 │
 ├─ ibRequestDispatcher
 │   ├─ IncomingServer accepts TCP from 1000 clients
 │   ├─ parse → enqueue to session's ibSessionRequestQueue
 │   └─ Dispatcher submits processable sessions to workerPool
 │
 ├─ workerPool (4 × cores, clamped to 4..32 threads)
 │   ├─ pulls pending session-request pair
 │   ├─ runs: SessionScope(sid) + ibConnectionScope + Execute
 │   └─ returns to pool; next work item
 │
 └─ ibConnectionPool (maxSize ≈ N workers)
     └─ N=16 workers → 16 conns max concurrent DB use
```

**Scaling invariants:**

- Threads scale with **worker-pool size**, not user count.
- DB connections scale with **worker-pool size**, not session count.
- Memory scales with **session state**, which can be paged/evicted.

---

## Per-session request queue

Each `ibSession` gets a queue. Dispatcher guarantees FIFO per session,
single-in-flight per session (only one worker processes a given
session's request at a time). Different sessions run on different
workers in parallel.

```
Worker pool: [W1, W2, W3, W4]

Session A:  req1 → req2 → req3        (queued FIFO)
Session B:  reqX → reqY
Session C:  reqZ

Scheduling:
  W1 ← A.req1     W2 ← B.reqX     W3 ← C.reqZ     W4 idle
  (W1 finishes A.req1)
  W4 ← A.req2     W3 idle         ...
```

Benefits:
- Session state doesn't race — never 2 workers on same session at once.
- User's click-click doesn't execute out of order.
- Long-running script on session A doesn't block other sessions.

---

## Binary RPC protocol (sketch)

Between thin client and compute server. Characteristics:
- **Stateful** — session established on Connect, maintained until
  Disconnect / timeout.
- **Binary framing** — header + payload length + payload.
- **Request/response** — correlation IDs for async responses.
- **Event stream** — server-push events (form update, notification)
  via long-polled or server-push framed messages.

Candidate frame layout:

```
 0        4        8                16
 │ MAGIC  │ TYPE   │ LEN   │ CORR   │ PAYLOAD (LEN bytes)
 └────────┴────────┴───────┴────────┘

TYPE: Request | Response | Event | Ping | Error | Auth
CORR: 0 for Event; correlates Req↔Resp otherwise
```

Reuses existing `ibReaderMemory` / `ibWriterMemory` for payload
serialisation (same as debug protocol + bytecode cache). Could layer
on top of cpp-httplib's WebSocket support, or raw TCP.

**Not** an HTTP-JSON API — OES data model is too rich (ibValue types,
metadata CLSIDs, form descriptions) for natural JSON round-trips.
Internal clients use the native binary format; an HTTP-JSON facade
can be added later for external integrations (similar to the
existing web frontend's request path).

---

## Phase plan (incremental, non-breaking)

### ✅ Phase 1 — Connection pool + scope + TX counter

Landed. See `connection-pool.md`.

- Pool owns all conns, shared_ptr hand-outs, lazy Clone
- Scope with merged SafeBeginTransaction / SafeCommitTransaction / SafeRollBackTransaction
- Counter-based nested TX (correct across 4 drivers)
- TL TX pinning (thread sticks to conn during active TX)
- Mutation entry points (object Write/Delete) migrated

### ✅ Phase 2 — Request dispatcher (in-process, same binary)

Largely landed (commit `97344bbf`). Per-session worker threads on the
web side are gone; sessions share a process-wide `ibWorkerPool` owned
by `ibSessionRegistry`. Components as built:

- `ibWorkerPool` (abstract, in `backend/session/`) +
  `ibWorkerPoolHeadless` (concrete, lazy-spawn up to `maxWorkers`,
  idle-shrink at 30 s (`workerPoolHeadless.cpp:21` — note 60 s is the CONNECTION
  pool's timeout, a different constant), per-session FIFO + lease, reentrant Submit
  inline). `ibWorkerPoolGUI` scaffold lives in `frontend/session/`,
  not auto-installed.
- The "session request queue" lives inside the pool (per-session
  `ibSessionQueue`). No separate `ibRequestDispatcher` class — the
  pool itself is the dispatcher; `ibSession::Submit(task)` is the
  public entry.
- `ibWebApplication` migrated: `StartWorker` / `StopWorker` /
  `WorkerLoop` deleted; `PostWork` / `RunOnWorker` forward through
  `m_sessionContext->Submit(...)`.
- Desktop side keeps the wx main thread as its sole script executor
  (no need to dispatch through the pool). The `ibWorkerPoolGUI`
  scaffold is ready when async dispatch becomes a concrete need.

**Testable outcome:** web server handles 100+ concurrent sessions on
16 threads without per-session thread overhead. Desktop UI stays
responsive during long writes.

**Pitfalls (see also `connection-pool.md#pitfalls-and-gotchas`):**
- SSE / long-polled endpoints need a separate thread-pool or async
  mechanism — a worker blocked on `wait_for_event` is a worker lost.
- Script execution is synchronous to completion; need timeout +
  interrupt to prevent infinite-loop scripts from DoS'ing a worker.
- Session state access must be serialised — the per-session
  single-in-flight invariant handles this, but audit any
  `thread_local` assumptions elsewhere.

### Phase 3 — Protocol abstraction (same binary, pluggable transport)

Dispatcher gains an abstract "transport" interface — today's cpp-httplib
HTTP transport is one implementation. Add a TCP transport alongside.

- `ibIncomingServer` interface: accept, parse frame, hand request to
  dispatcher; send response/event back.
- HTTP transport keeps existing JSON-ish web payloads.
- TCP transport uses the new binary frame layout (for thin clients).

Still in-process — `enterprise.exe` can optionally start a TCP
listener to accept thin-client connections.

### Phase 4 — Standalone compute server binary

New binary `oes-server.exe`:
- Links the same backend.dll + compute-server module.
- No GUI. TCP listen + HTTP listen.
- Configured via CLI: `--file=... --port=... --tcp-port=...`
- Can replace `daemon.exe` (which is the current headless service mode)
  or live alongside.

### Phase 5 — Thin GUI client

`enterprise-thin.exe`:
- Links frontend.dll (wxWidgets) but NOT backend.dll's runtime.
- Has a thin backend-stub that speaks the TCP binary protocol.
- Connects to `oes-server.exe` on startup, authenticates, receives
  metadata snapshot (or streams on demand).
- Forms, controls, events — all RPC round-trips.

**Risk:** network latency per UI operation. Must be batched / cached
aggressively (e.g., form open returns controls + initial data in one
response).

### Phase 6 — Web frontend decoupling

nginx / Apache in front:
- Static assets served by nginx.
- Dynamic requests proxied to `oes-server.exe`'s HTTP transport
  (either keeps current JSON-ish path or switches to FastCGI / HTTP1
  proxy).
- SSL termination + load balancing at nginx.

### Phase 7 — Cluster (optional)

Multiple `oes-server.exe` instances behind a load balancer:
- Sticky sessions (client → same server, based on cookie / routing
  header).
- Shared metadata cache (optional) — either each server reloads on
  metadata change, or a distributed invalidation.
- Shared `sys_session` table already exists — registry cooperation
  via DB row locks.

Not MVP. Relevant at 10k+ user scale.

---

## How the pool fits each phase

| Phase | Pool's role |
|---|---|
| 1 (landed) | Per-request Checkout; per-thread TL pinning |
| 2 | Per-request Checkout from worker; `maxSize ≈ worker-count` |
| 3 | Unchanged — pool is transport-agnostic |
| 4 | Same, in `oes-server.exe` process |
| 5 | Thin client has no pool (no DB); all DB via RPC |
| 6 | nginx → `oes-server.exe` → pool (single-server model) |
| 7 | Each cluster node has its own pool; no cross-node sharing needed |

The pool abstraction is **already correct** for the target architecture —
no redesign needed. What's missing is the higher-level dispatcher
that decouples threads from sessions.

---

## Sizing rules of thumb

| Component | Formula | Typical |
|---|---|---|
| Worker pool | `4 × CPU_cores`, clamped to [4, 32] (`PickWorkerCount`, `appData.cpp:128-133`) | 16-32 |
| Connection pool `maxSize` | ~ worker-pool size + 4 (registry/watcher slack) | 20-36 |
| Per-session runtime state | ~ 10-50 MB depending on config size | 20 MB |
| Active sessions per server | RAM / session state → say 500-2000 per 32GB server | |
| TCP connections | ~ active sessions × 1 | |

Above figures assume workloads dominated by short DB operations, not
long-running scripts or reports.

---

## Related docs

- [`connection-pool.md`](connection-pool.md) — pool, scope, TX counter,
  migration status
- [`session-registry.md`](session-registry.md) — session lifecycle and
  registry (provides the identity layer for the dispatcher)
- [`runtime-facade.md`](runtime-facade.md) — per-session runtime state
  (ibValueModuleManager) — the "runtime" part that workers will
  dispatch into
- [`backend-frontend-split.md`](backend-frontend-split.md) — the
  backend/frontend DLL boundary; thin-client would replace frontend's
  in-process backend stub with a network stub
