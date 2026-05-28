# Connection pool — architecture, invariants, usage

Process-wide bounded pool of `ibDatabaseLayer` connections with RAII
scope handles, thread-local pinning, and nested-safe transaction
counter. Replaces the pre-existing "single `m_db` + `ibTransactionGuard`
template" model.

> **Status:** landed. Pool + scope + TX counter + TX TL pinning + all
> object mutation entry points migrated. Build clean on Debug|x86.
> Smoke-tested on `wenterprise-server.exe` (FB embedded, parallel
> sessions, login + forms) and desktop `enterprise.exe` (catalog
> save-and-close flow).

---

## Table of contents

1. [Motivation](#motivation)
2. [Architecture](#architecture)
3. [Public API](#public-api)
4. [Usage patterns](#usage-patterns)
5. [Invariants](#invariants)
6. [Nested transaction semantics](#nested-transaction-semantics)
7. [Thread-local slots](#thread-local-slots)
8. [Migration status](#migration-status)
9. [Pitfalls and gotchas](#pitfalls-and-gotchas)
10. [Future work](#future-work)

---

## Motivation

Before the refactor OES had:

- One `shared_ptr<ibDatabaseLayer>` on `ibApplicationData::m_db` —
  the master connection, shared across every thread in the process.
- `ibTransactionGuard<>` template in `commonObject.h` — per-guard
  `m_active_transaction` flag with direct driver calls. Nested guards
  on the same `m_db` were not properly coordinated: each guard's
  second `Commit()` hit the driver, breaking atomicity.
- No pool — `ibSessionRegistry` had its own ad-hoc "Checkout" against
  the master connection.

Problems:

1. **No thread isolation.** Every thread hit `m_db`. On FB embedded
   a global `fb_mutex` serialised them; on PG / MySQL / MSSQL the
   driver-level state got racey.
2. **Nested TX broken.** `Document.Write()` opens a TX, calls
   `RegisterRecordSet.Write()` which opens its own guard → both hit
   the same driver, Commit of the inner guard committed the outer's
   work prematurely. Inner rollback did not propagate to outer.
3. **No parallelism.** Web server's 10 concurrent sessions could not
   execute concurrent reads/writes — all serialised on one `m_db`.

The pool refactor addresses all three at once by making the connection
the unit of ownership and the transaction the unit of bookkeeping.

---

## Architecture

> **Update 2026-04-28**: holder-keyed model — pool no longer uses
> thread-local slots. Reservations are keyed on
> `ibDatabaseConnectionHolder*` identity. Three runtime channels:
> per-`ibSession` (DML), session-registry (sys_session I/O,
> two-holder split write/probe), process-wide singleton (DDL /
> `db_query`). Public pool API shrunk to 4 methods.

```
┌─────────────────────────────────────────────────────────────────┐
│  ibApplicationData (singleton)                                  │
│    • holds std::unique_ptr<ibConnectionPool>                    │
│    • Pool::Shutdown driven by ~ibApplicationData (RAII)         │
└──────────────────────┬──────────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────────┐
│  ibConnectionPool                                               │
│    • m_source  : shared_ptr<ibDatabaseLayer>   (master)         │
│    • m_entries : vector<ibConnectionEntry>     (single registry)│
│        each entry: { conn, txHolder*, scopeHolder*, lastUsed,   │
│                      startedAt, inUse, noWait }                 │
│        states encoded by triple (no separate idle/borrow lists) │
│    • lazy Clone() on demand up to maxSize                       │
│    • thread-safe via mutex + cv                                 │
│    • Public API: Init/Shutdown/IsInitialised/                   │
│      GetFreeConnection/GetDatabaseLayer (rest is private+friend)│
└──────────────────────┬──────────────────────────────────────────┘
                       │ holder->Get/AcquireConnection or scope ctor
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│  ibDatabaseConnectionHolder (identity tag)                      │
│    • virtual dtor only — empty marker base                      │
│    • GetConnection()        → reserved/scope-bound conn or null │
│    • AcquireFreeConnection()→ wrapped Checkout, no binding      │
│                                                                 │
│  Channels (concrete holder instances):                          │
│   ┌─ ibSession                                                  │
│   │     inherits holder; one identity per session;              │
│   │     m_resolvedLanguageCode set in SetUserInfo on auth       │
│   ├─ ibSessionRegistry::m_writeHolder  (typed                   │
│   │  ibSessionRegistry::m_probeHolder   ibSessionRegistry-      │
│   │     ConnectionHolder × 2 — sys_session I/O channel)         │
│   └─ ibConnectionPool::DbQueryHolder() (process-wide singleton  │
│        ibSingleConnectionHolder; backs db_query macro for DDL   │
│        and infra-level writes)                                  │
└──────────────────────┬──────────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────────┐
│  ibConnectionScope (RAII)                                       │
│    • ctor: holder = customHolder ?? Pool::CurrentHolder()       │
│       (= session ?? singleton)                                  │
│       1. inherit holder's existing scope-bound conn, OR         │
│       2. adopt holder's TX-pinned conn, OR                      │
│       3. fresh Checkout + BindScopeHolder                       │
│    • SafeBeginTransaction / SafeCommit / SafeRollBack            │
│    • dtor: UnbindScopeHolder + drop conn (deleter clears inUse) │
└──────────────────────┬──────────────────────────────────────────┘
                       │ scope->X() / db_query->X()
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│  ibDatabaseLayer (base)                                         │
│    • enable_shared_from_this                                    │
│    • Begin/Commit/RollBack wrappers (counter m_txDepth +         │
│      flag m_txAborted; depth 0→1 → ReserveTx, 1→0 → ReleaseTx)  │
│    • m_holder back-pointer (set by pool's ReserveTx)            │
│    • IsBusy() — true while any stmt/rs is alive (m_Statements + │
│      m_ResultSets non-empty); pool's Checkout/Reap skip busy    │
│      entries → bare db_query->PrepareStatement(...) is safe     │
└──────────────────────┬──────────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────────┐
│  Driver implementations (firebird/postgres/sqlite/mysql/odbc)   │
│    • DoBeginTransaction/DoCommit/DoRollBack — real SQL/native   │
└─────────────────────────────────────────────────────────────────┘
```

**Key files:**

| File | Purpose |
|---|---|
| `backend/databaseLayer/connectionHolder.h` | Holder base + `ibSingleConnectionHolder` |
| `backend/databaseLayer/connectionPool.{h,cpp}` | Pool + `m_entries` registry + private holder primitives |
| `backend/databaseLayer/connectionScope.{h,cpp}` | RAII scope; resolves holder via `Pool::CurrentHolder()` |
| `backend/databaseLayer/databaseLayer.{h,cpp}` | Base counter layer + `m_holder` back-pointer + `IsBusy()` |
| `backend/databaseLayer/{firebird,postgres,sqllite,mysql,odbc}/*` | 5 drivers — `Do*` overrides only |
| `backend/session/session.h` | `ibSession : public ibDatabaseConnectionHolder` |
| `backend/session/sessionRegistry.h` | `ibSessionRegistryConnectionHolder` × 2 — write + probe |
| `backend/appData.{h,cpp}` | Pool owner, `GetDatabaseLayer()` delegate |

---

## Public API

### `ibDatabaseConnectionHolder` (canonical user-facing API)

```cpp
class BACKEND_API ibDatabaseConnectionHolder {
public:
    virtual ~ibDatabaseConnectionHolder() = default;

    // Conn currently reserved (TX) or scope-bound to this holder. TX
    // pin wins over scope binding. Returns nullptr when nothing is
    // pinned. Resolves through ibConnectionPool internally.
    std::shared_ptr<ibDatabaseLayer> GetConnection() const;

    // Fresh checkout — wrapped shared_ptr, NOT bound to this holder.
    // Used for parallel side queries that must NOT join the holder's
    // current TX. Released back to the pool on shared_ptr drop.
    std::shared_ptr<ibDatabaseLayer> AcquireFreeConnection() const;
};

class BACKEND_API ibSingleConnectionHolder : public ibDatabaseConnectionHolder {
public:
    ibSingleConnectionHolder();
    ~ibSingleConnectionHolder() override;   // self-cleans pool reservations
};
```

Usage:

```cpp
// Session-bound conn (during script work):
ibSession::Current()->GetConnection()->RunQuery(...);

// Side-channel parallel conn from a session worker:
auto fresh = ibSession::Current()->AcquireFreeConnection();
fresh->BeginTransaction();
fresh->RunQuery(...);
fresh->Commit();

// Session-registry persistent conns (registry-internal):
m_writeConn = m_writeHolder.AcquireFreeConnection();
m_probeConn = m_probeHolder.AcquireFreeConnection();

// db_query global routes through DbQueryHolder():
db_query->RunQuery(...);                       // process-wide singleton channel
```

### `ibConnectionPool` (public surface — minimal)

```cpp
class BACKEND_API ibConnectionPool {
public:
    void Init(std::shared_ptr<ibDatabaseLayer> primary,
              std::size_t maxSize, std::size_t minIdle = 2);
    void Shutdown();
    bool IsInitialised() const;

    // Canonical RAII factory. Returns an ibConnectionScope by value;
    // scope's ctor resolves holder via CurrentHolder().
    static ibConnectionScope GetFreeConnection();

    // The `db_query` resolution function. Priority:
    //   1. CurrentHolder()'s reserved-TX conn
    //   2. CurrentHolder()'s scope-bound conn
    //   3. Primary master conn (fallback)
    static std::shared_ptr<ibDatabaseLayer> GetDatabaseLayer();
};
```

Holder-keyed primitives (`ReserveTx`, `Bind/UnbindScopeHolder`,
`Checkout`, `ClearActiveTxConnection`, `CurrentHolder`,
`DbQueryHolder`, etc.) are private with friend access for
`ibConnectionScope`, `ibDatabaseConnectionHolder`,
`ibSingleConnectionHolder`, `ibDatabaseLayer`. End-user code goes
through holder methods or `ibConnectionScope`.

### `ibConnectionScope`

```cpp
class BACKEND_API ibConnectionScope {
public:
    // Default ctor: holder = ibConnectionPool::CurrentHolder()
    //   (= ibSession::Current() if bound, else DbQueryHolder() singleton).
    ibConnectionScope();

    // Custom-holder ctor: target an explicit channel (e.g. registry-
    // owned holder). Same Acquire flow, just with a different holder.
    explicit ibConnectionScope(ibDatabaseConnectionHolder* customHolder);

    ~ibConnectionScope();      // unbind scope holder + drop conn (deleter clears inUse)
    // move-only

    // Driver access — scope->X() / (*scope).X()
    ibDatabaseLayer* operator->() const;
    ibDatabaseLayer* get() const;
    explicit operator bool() const;

    const std::shared_ptr<ibDatabaseLayer>& shared() const;

    void SafeBeginTransaction(const ibDatabaseLayer::ibTxOptions& = {});
    void SafeCommitTransaction();
    void SafeRollBackTransaction();

    bool HasActiveTransaction() const;
    bool Owns() const;  // true = outer-most scope for the holder
};
```

### `ibDatabaseLayer` (base)

```cpp
class ibDatabaseLayer : public std::enable_shared_from_this<ibDatabaseLayer> {
public:
    void BeginTransaction(const ibTxOptions& opts = {});
    void Commit();
    void RollBack();
    bool IsActiveTransaction();    // m_txDepth > 0

    // Holder that has this layer reserved for an active TX (set by
    // pool's ReserveTx, cleared by ReleaseTx). Identity-only.
    ibDatabaseConnectionHolder* GetHolder() const;

    // Pool's hand-out / Reap predicate consults this — true while any
    // ibPreparedStatement / ibDatabaseResultSet is alive on the
    // layer. Keeps a conn whose result set is mid-iteration from
    // being handed to another caller (cursor would race).
    bool IsBusy() const;

protected:
    virtual void DoBeginTransaction(const ibTxOptions& opts) = 0;
    virtual void DoCommit() = 0;
    virtual void DoRollBack() = 0;

    int  m_txDepth   = 0;
    bool m_txAborted = false;
    ibDatabaseConnectionHolder* m_holder = nullptr;
    friend class ibConnectionPool;
};
```

---

## Usage patterns

### Entry-point method (migrated)

```cpp
bool ibValueRecordDataObjectCatalog::WriteObject()
{
    if (!appData->DesignerMode())
    {
        ibConnectionScope scope = ibConnectionPool::GetFreeConnection();

        if (!scope || !scope->IsOpen())
            ibBackendCoreException::Error(_("Database is not open!"));

        if (!ibBackendException::IsEvalMode())
        {
            if (!m_metaObject->AccessRight_Write()) {
                ibBackendAccessException::Error();
                return false;
            }

            scope.SafeBeginTransaction();

            m_procUnit->CallAsProc(wxT("BeforeWrite"), cancel);
            if (cancel.GetBoolean()) {
                scope.SafeRollBackTransaction();
                ibBackendCoreException::Error(_("Failed to write object in db!"));
                return false;
            }

            if (!SaveData()) {
                scope.SafeRollBackTransaction();
                ibBackendCoreException::Error(_("Failed to write object in db!"));
                return false;
            }

            m_procUnit->CallAsProc(wxT("OnWrite"), cancel);
            if (cancel.GetBoolean()) {
                scope.SafeRollBackTransaction();
                ibBackendCoreException::Error(_("Failed to write object in db!"));
                return false;
            }

            scope.SafeCommitTransaction();

            if (valueForm != nullptr) valueForm->NotifyChange(GetReference());
        }
    }
    return true;
}
```

### Nested Write (Document → Registers)

```cpp
// Document.Write — outer scope
{
    ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
    scope.SafeBeginTransaction();           // counter 0→1, REAL Begin

    m_procUnit->CallAsProc("BeforeWrite"); // script may mutate state
    SaveData();                             // Document row

    for (auto& rs : m_registerRecordSets) {
        rs.Write();                         // Register.Write — inner scope
        //   scope2 inherits conn1 via TL
        //   scope2.SafeBeginTransaction → counter 1→2, SKIP driver Begin
        //   register SaveData on SAME conn1
        //   scope2.SafeCommitTransaction → counter 2→1, SKIP driver Commit
    }

    scope.SafeCommitTransaction();          // counter 1→0, REAL Commit
}                                           // ~scope → pool repark
```

If any inner `scope2.SafeRollBackTransaction()` fires, it sets
`m_txAborted = true` on the conn; the outer `scope.SafeCommitTransaction`
then silently becomes a real `DoRollBack`. Atomicity preserved.

### Registry-style (custom holders, parallel conns on one thread)

The `CheckoutIndependent` primitive shown in older drafts is gone.
After the 2026-04-28 holder-keyed refactor the registry owns two
typed `ibSessionRegistryConnectionHolder` instances and acquires
free conns through their `AcquireFreeConnection()` (which wraps
`Pool::Checkout` internally, returning a `shared_ptr<ibDatabaseLayer>`
with no holder pinning):

```cpp
// ibSessionRegistry::Start — needs 2 parallel conns on its own thread
m_writeConn = m_writeHolder.AcquireFreeConnection();
m_probeConn = m_probeHolder.AcquireFreeConnection();
// The historical m_lockConn (third connection for pessimistic-lock
// liveness) was retired together with HoldRowLocks — see
// docs/session-registry.md "Gotchas" #4.
```

`AcquireFreeConnection` does NOT install scope binding — each
returned conn is the caller's sole owner until the `shared_ptr`
drops, at which point the custom deleter re-parks the entry in the
pool.

### Legacy `db_query->X()` without scope

Still works. Macro resolves via `ibConnectionPool::GetDatabaseLayer()`
→ priority chain:

1. Active-TX TL (from a bare `db_query->BeginTransaction()` someone did)
2. Active-scope TL (from an outer `ibConnectionScope`)
3. Primary conn (m_source)

Unmigrated call sites keep working; they just don't get parallelism.

---

## Invariants

1. **Pool is the sole owner** of every live `ibDatabaseLayer`. No
   direct ownership on `ibApplicationData` or anywhere else.
2. **One conn per thread at a time** (for any given hand-out).
   `shared_ptr` is not Share-Across-Threads-And-Use-Simultaneously —
   native driver state races.
3. **Nested scopes on one thread inherit** the outer scope's conn.
   Never Checkout a second conn when one is already active — breaks
   nested TX (SQL is connection-local).
4. **Active-TX TL pins conn to thread** for the TX's full duration.
   `db_query` on this thread routes to the TX's conn regardless of
   scope state.
5. **`shared_from_this()` requires pool-owned layer.** Layers
   constructed outside the pool (test harnesses) won't work with the
   TX TL pinning — bad_weak_ptr on BeginTransaction.
6. **Scope dtor rolls back** any unmatched SafeBegin. Exception
   between SafeBegin and SafeCommit cleans up automatically.
7. **Lazy Clone** — pool only opens connections on demand. Desktop
   single-user process stays on master alone; `maxSize=32` is a cap,
   not a target.
8. **Pool Shutdown requires stopped workers.** Outstanding hand-outs
   are valid until dropped by their owner; pool's m_source is
   cleared. Shutdown order: stop all workers → join threads →
   pool.Shutdown.

---

## Nested transaction semantics

Counter-based, uniform across all 5 drivers:

- First `BeginTransaction()` on a conn: `m_txDepth 0→1`, fires
  `DoBeginTransaction` (real driver Begin).
- Subsequent nested `BeginTransaction`: `m_txDepth++`, NO driver call.
- `Commit()`:
  - If `m_txDepth > 1`: just decrement.
  - If `m_txDepth == 1`: decrement to 0, then fire real `DoCommit`
    (or `DoRollBack` if `m_txAborted` was set).
- `RollBack()`:
  - Sets `m_txAborted = true` (poisons outer Commit).
  - Decrement.
  - If `m_txDepth` now 0: fire real `DoRollBack`.

**Why counter instead of real nested TX per driver:**

| Driver | Native nested BEGIN behaviour |
|---|---|
| Firebird | Creates parallel TX (`isc_start_transaction`) — inner commits don't affect outer, breaking atomicity |
| SQLite | Errors: `cannot start a transaction within a transaction` |
| PostgreSQL | Warns + ignores — only one TX active anyway |
| MySQL | Implicit-commits outer TX when starting new — destroys outer's atomicity |
| ODBC / MSSQL | Increments `@@TRANCOUNT` (basically the same counter model) |

Counter on base unifies all 5 drivers to a single flat-TX semantic
with correct atomic nesting. Inner rollback propagates to outer's
commit via the aborted flag.

**What this does NOT give:** true savepoints. If a caller needs to
roll back inner work while committing outer, use `SAVEPOINT` /
`ROLLBACK TO SAVEPOINT` explicitly at the driver level. Not currently
exposed through `ibDatabaseLayer`.

---

## Holder reservation slots (post 2026-04-28)

> **Replaces the prior "two TL slots" model.** Both reservations
> live in `ibConnectionPool::m_entries[i]` as `txHolder` and
> `scopeHolder` fields (raw `ibDatabaseConnectionHolder*`). Pool's
> `m_mutex` synchronises both reads and writes. Identity-by-pointer:
> different holder addresses are different channels.

Each `ibConnectionEntry` carries:

| Field | Set by | Cleared by | Meaning |
|---|---|---|---|
| `txHolder` | `Pool::ReserveTx(holder, conn)` (called from `BeginTransaction` 0→1) | `Pool::ReleaseTx(holder)` (called from `Commit/RollBack` 1→0) | Active transaction pinned to holder; entry stays reserved across worker thread crossings |
| `scopeHolder` | `Pool::BindScopeHolder(holder, conn)` (called from `ibConnectionScope` ctor when it owns the borrow) | `Pool::UnbindScopeHolder(holder)` (called from `ibConnectionScope` dtor) | Outer scope marker; nested scopes for the same holder inherit by lookup, no second checkout |
| `inUse` | `Checkout` flips on, hand-out deleter flips off | — | Borrowed flag; pool's Checkout/Reap skip while true |

**Priority chain for `GetDatabaseLayer()`:**

```
holder = CurrentHolder();        // ibSession::Current() ?? DbQueryHolder()
if (holder->GetReservedTx())   return reserved-tx-conn;    // TX pin
if (holder->GetScopeConn())    return scope-bound-conn;    // scope binding
return GetPrimaryConnection();                              // master fallback
```

**Identity scope cheat-sheet:**

| Channel | Holder type | Identity | Lifecycle |
|---|---|---|---|
| Per-session DML | `ibSession` (inherits holder) | one per session | session ctor → registry-managed Close |
| Session-registry I/O | `ibSessionRegistryConnectionHolder × 2` (`m_writeHolder`, `m_probeHolder`) | two per registry instance | registry's lifetime |
| DDL / `db_query` | `ibSingleConnectionHolder` (singleton) | process-wide single | block-local static, never freed |

For a parallel non-session channel that needs its own identity (rare),
declare a static `ibSingleConnectionHolder` and pass its address to
`ibConnectionScope(&customHolder)` — see
`wfrontend.cpp::CheckMetadataAndEvict` for the canonical example.

---

## Migration status

### Migrated (scope + Safe* TX API)

| File | Methods |
|---|---|
| `catalogObject.cpp` | `WriteObject`, `DeleteObject` |
| `documentObject.cpp` | `WriteObject`, `DeleteObject` |
| `chartOfAccountsObject.cpp` | `WriteObject`, `DeleteObject` |
| `chartOfCharacteristicTypesObject.cpp` | `WriteObject`, `DeleteObject` |
| `informationRegisterObject.cpp` | `WriteRecordSet`, `DeleteRecordSet`, `WriteRegister`, `DeleteRegister` |
| `accumulationRegisterObject.cpp` | `WriteRecordSet`, `DeleteRecordSet` |
| `accountingRegisterObject.cpp` | `WriteRecordSet`, `DeleteRecordSet` |
| `constantObject.cpp` | `SetConstValue` |
| `commonObjectQuery.cpp` | `ExistData` |

### Not migrated (read-only or complex TX flow)

| File | Reason |
|---|---|
| `metadataConfigurationQuery.cpp` | Save/Load TX spans multiple methods (OnBeforeSave → OnSave → OnAfterSave). Works via TX TL pinning (thread-level). Migration possible but needs careful scope-as-member refactor. Low priority — config save blocks main thread anyway. |
| `systemManagerFunc.cpp` | Runtime `BeginTransaction/Commit/RollBack` built-ins — **intentionally bare**. These are transparent forwarders for script-side TX API; adding a scope wrapper would create a per-call checkout, breaking the thread-sticky semantic the counter layer relies on. |
| `*Manager_impl.cpp` (catalog, document, register managers) | Read-only `FindByCode` / `FindByName` etc. No TX. Migration gives parallelism but isn't correctness-critical. Low priority. |
| `objectListQuery.cpp`, `*MetadataQuery.cpp` | Read-only SELECTs. Same as above. |

### Direct-checkout consumers (post 2026-04-28: via own holder + AcquireFreeConnection)

| File | Holder | Purpose |
|---|---|---|
| `sessionRegistry.cpp` | `m_writeHolder`, `m_probeHolder` | 2 persistent conns (sys_session writes + NOWAIT row-lock probe) |
| `wfrontend.cpp::CheckMetadataAndEvict` | `static ibSingleConnectionHolder` (function-local) | 15s metadata-watcher per-tick |

---

## Pitfalls and gotchas

1. **`m_txDepth` and `m_txAborted` are plain `int`/`bool`.** Not
   atomic. Invariant: one conn used by at most one thread at a time
   (enforced by pool's Checkout + shared_ptr). If the invariant is
   broken (a caller passes conn to another thread and both use it),
   counter races. Defensive option: `std::atomic<int>` — small
   overhead, protects against debugging hell.

2. **Primary conn as fallback for legacy `db_query`.** Call sites
   without a scope go to `m_source`. All threads share master.
   FB embedded has a global `fb_mutex` that serialises; PG/MySQL/MSSQL
   may race. Migration is opt-in per call site — focus on mutation
   paths first (Write/Delete/Update).

3. **Lazy Clone latency burst.** First Checkout for a new slot pays
   Open() cost (FB embedded ~100ms, PG/MySQL remote ~50-500ms).
   Sequential burst of N new sessions → N × Open() serialised through
   pool's mutex. Acceptable for typical login pattern; mitigate with
   `minIdle` pre-warm if needed for benchmark scenarios.

4. **`shared_from_this()` requires pool-owned layer.** Layers
   constructed outside the pool (e.g., unit-test harnesses with
   `new ibDatabaseLayerFirebird()`) throw `bad_weak_ptr` when
   `BeginTransaction` tries `shared_from_this`. Test code must wrap
   in `std::shared_ptr` first.

5. **Hand-out's deleter runs on the dropping thread.** If a caller
   passes shared_ptr to another thread and drops there, the deleter
   (lambda capturing pool's sp) runs on that thread → acquires pool
   mutex → pushes to m_idle. Thread-safe, but unusual flow.

6. **Pool Shutdown vs live scopes.** `pool->Shutdown()` resets
   `m_source` and clears `m_idle`. If a scope is still alive on
   another thread, it holds the hand-out → lambda deleter on drop
   sees `m_shutdown` → releases without parking. Layer destructed.
   **Order:** stop all workers → join threads → pool.Shutdown.

7. **Scope ctor/dtor overhead.** Each `ibConnectionScope`
   construction / destruction does a holder lookup in `m_entries`
   under `m_mutex` + shared_ptr copy (atomic refcount bump). ~50-100ns
   on hot path — fine for per-method scope; avoid per-iteration
   scope in tight loops. Earlier drafts described a thread-local
   slot; the model no longer uses TLS — see §"Holder reservation
   slots" below.

8. **Scope inheritance across threads doesn't happen automatically.**
   Holders are bound by identity (`ibSession*`), and a session is
   pinned to a thread via `ibSessionScope` / `ibSessionThreadBinding`.
   If worker dispatches sub-task to another thread via a callback,
   that callback does NOT inherit the source holder's reservation.
   Must explicitly pass shared_ptr or open a new scope under the
   target session.

9. **TX TL not cleared on bare `Begin` + crash.** If code does
   `db_query->BeginTransaction()` without scope/guard, throws, and
   the catcher doesn't Commit/Rollback, TX TL stays set → next
   request on this thread lands on the old TX. **Fix:** migrate to
   `scope.SafeBeginTransaction` (RAII-cleaned) or wrap bare calls
   in `ibTransactionGuard` equivalent.

10. **Pool saturation blocks indefinitely.** Checkout on a saturated
    pool `cv.wait`s forever if no one Returns. No timeout. Worker
    leak = deadlock.  Mitigation: large enough `maxSize` for expected
    concurrency; or add a `CheckoutWithTimeout` overload.

11. **Web per-session worker thread holds scope indefinitely** (if
    applied). Every session = one persistent pool conn. 1000 sessions
    = 1000 conns → well over `maxSize=32`. Fix: scope per-request,
    not per-worker-lifetime (see `compute-server-tiering.md` for
    the 3-tier plan).

---

## Lazy growth + idle-shrink

Pool starts with the master connection (always alive — `m_source`) plus
`minIdle - 1` pre-warmed clones. Subsequent demand growth is lazy:
`Checkout` clones the master on first miss, up to `maxSize`. When load
drops back, idle clones older than `kIdleTimeout` (60s) get closed by
`ReapStaleLocked` on the next Checkout — but never below `minIdle` and
never the master itself (pinned, repositioned to the back of the idle
deque if it would otherwise be reaped).

Per-runMode `minIdle` defaults are picked by `appData::PickConnectionMinIdle`:

| runMode | minIdle | rationale |
|---|---|---|
| `eWEB_ENTERPRISE_MODE` (wes) | 4 | multi-tab burst absorption |
| `eSERVICE_MODE` (daemon) | 2 | background tasks, modest load |
| desktop GUI / launcher | 2 | session-manager bookkeeping conn + UI thread |

`maxSize` stays at 32 across modes for now; CLI override is a future
addition.

---

## Future work

- **CLI override for `maxSize` / `minIdle`.** Hosts pass them through
  appData ctor; expose as `--db-pool-max=N --db-pool-min=M` flags so
  ops can tune without rebuilding.

- **CheckoutWithTimeout.** `Checkout(std::chrono::milliseconds)`
  returning nullptr if pool saturated. Debug safety net.

- **Per-connection atomic TX counter.** `std::atomic<int> m_txDepth`
  and `std::atomic<bool> m_txAborted`. Defense in depth.

- **Savepoints API.** `ibDatabaseLayer::Savepoint(name) / RollbackTo(name)`
  for callers that need real sub-TX (inner rollback without affecting
  outer). Supported by FB/PG/MySQL/MSSQL, not SQLite.

- **FB `fb_tr_list` cleanup.** Native nested-TX stack is now dead
  (counter collapses everything to depth 1). Remove the linked-list
  code for clarity — `m_fbNode` becomes a single-slot.

- **Pool stats endpoint.** `/admin/pool` on wes returning JSON with
  LiveSize / IdleSize / MaxSize. For ops monitoring.

- **Tiered server architecture.** See `compute-server-tiering.md` —
  the pool is ready for shared-worker + per-session-queue model;
  the server-side dispatcher is the missing piece.
