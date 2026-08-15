# Connection pool — architecture, invariants, usage

Process-wide bounded pool of `ibDatabaseLayer` connections with RAII
scope handles, holder-keyed pinning, and nested-safe transaction
counter. Replaces the pre-existing "single `m_db` + `ibTransactionGuard`
template" model.

> Connection access has two macros: `db_query` (non-session DDL /
> bootstrap, routes through the per-thread `ThreadHolder`) and
> `ses_query` (`session.h`, routes through `ibSession::Current()`'s
> own holder so the work joins the session's TX/scope). Use
> `ses_query` in descriptor / runtime code that must be transactional
> with the outer document save.

> **Status:** landed. Pool + scope + TX counter + holder-keyed TX
> pinning + all object mutation entry points migrated. Build clean on
> Debug|x86.
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
7. [Holder reservation slots](#holder-reservation-slots-post-2026-04-28)
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
   a global `fb_mutex` serialised them; on PG / MSSQL the
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
> per-conn thread-local slots. Reservations are keyed on
> `ibDatabaseConnectionHolder*` identity. Three runtime channels:
> per-`ibSession` (DML; session OWNS an `ibDatabaseConnectionHolder`
> member `m_dbHolder` — composition, not inheritance), session-registry
> (sys_session I/O — a single `m_writeHolder`; the probe holder was
> retired with the row-lock probe), per-thread `ThreadHolder` (DDL /
> `db_query`; the only remaining thread_local — `ts_holder` in
> `connectionPool.cpp:17`). Public pool API: `Init` / `Shutdown` /
> `IsInitialised` / `GetFreeConnection` / `GetDatabaseLayer` /
> `ThreadHolder` + `LiveSize` / `IdleSize` / `MaxSize` / `MinIdle`
> diagnostics. Construction is gated by `ib::AppDataCtorToken` (appData
> owns the pool).

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
│    • virtual dtor + DDL-barrier state for the current save      │
│    • EnsureConnection()     → TX/scope conn, else Checkout+bind │
│    • AcquireFreeConnection()→ wrapped Checkout, no binding      │
│    • OpenConnectionScope()  → ibConnectionScope(this)           │
│                                                                 │
│  Channels (concrete holder instances):                          │
│   ┌─ ibSession                                                  │
│   │     OWNS holder (member m_dbHolder); one identity per       │
│   │     session                                                 │
│   ├─ ibSessionRegistry::m_writeHolder  (one typed              │
│   │     ibSessionRegistryConnectionHolder — sys_session I/O)    │
│   └─ ibConnectionPool::ThreadHolder() (per-thread              │
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
│  Driver implementations (firebird/postgres/sqlite/odbc)          │
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
| `backend/databaseLayer/{firebird,postgres,sqllite,odbc}/*` | 4 drivers — `Do*` overrides only |
| `backend/session/session.h` | `ibSession` OWNS `ibDatabaseConnectionHolder m_dbHolder` (composition) |
| `backend/session/sessionRegistry.h` | `ibSessionRegistryConnectionHolder` — one (`m_writeHolder`) |
| `backend/appData.{h,cpp}` | Pool owner, `GetDatabaseLayer()` delegate |

---

## Public API

### `ibDatabaseConnectionHolder` (canonical user-facing API)

```cpp
class BACKEND_API ibDatabaseConnectionHolder {
public:
    virtual ~ibDatabaseConnectionHolder() = default;

    // Single entry point: returns a usable conn for this holder.
    // Resolution chain: TX-pinned conn → scope-bound conn → fresh
    // Checkout (bound as scope so subsequent EnsureConnection calls
    // return the same conn while the holder is alive; released by the
    // holder's dtor via ReleaseAll). nullptr only if pool not initialised.
    std::shared_ptr<ibDatabaseLayer> EnsureConnection();

    // Fresh checkout — wrapped shared_ptr, NOT bound to this holder.
    // Used for parallel side queries that must NOT join the holder's
    // current TX. Released back to the pool on shared_ptr drop.
    std::shared_ptr<ibDatabaseLayer> AcquireFreeConnection() const;

    // Convenience: ibConnectionScope bound to this holder
    // (== ibConnectionScope(this)).
    ibConnectionScope OpenConnectionScope();

    // DDL/DML restructuring-barrier state for the current save —
    // tables whose SHAPE this save changed (CREATE / ADD / DROP /
    // ALTER) + the work DEFERRED past the DDL commit.
    // Keyed on the holder so the several ibSchemaBuilder instances of
    // one save share a home. ibSchemaBuilder owns the logic.
    std::set<wxString>&                 DdlCreatedTables();
    std::vector<std::function<bool()>>& DdlDeferredWrites();
};

class BACKEND_API ibSingleConnectionHolder : public ibDatabaseConnectionHolder {
public:
    ibSingleConnectionHolder();
    ~ibSingleConnectionHolder() override;   // self-cleans pool reservations
};
```

`ibSession` OWNS a holder (member `m_dbHolder`) rather than inheriting
one; its `EnsureConnection` / `OpenConnectionScope` / `Holder()` façade
forwards to the member (see `session.h`).

Usage:

```cpp
// Session-bound conn (during script work):
ibSession::Current()->EnsureConnection()->RunQuery(...);
// or the ses_query macro, which is ibSession::DatabaseLayer():
ses_query->RunQuery(...);

// Side-channel parallel conn from a session worker:
auto fresh = ibSession::Current()->Holder()->AcquireFreeConnection();
fresh->BeginTransaction();
fresh->RunQuery(...);
fresh->Commit();

// Session-registry persistent conn (registry-internal, single channel):
m_writeConn = m_writeHolder.EnsureConnection();

// db_query global routes through the per-thread ThreadHolder():
db_query->RunQuery(...);                       // non-session DDL/CLI channel
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

    // The db_query channel's per-thread holder identity (block-local
    // thread_local ibSingleConnectionHolder). Public so subsystems that
    // key per-save state on the channel (ibSchemaBuilder's DDL/DML
    // barrier) resolve the SAME holder across a save.
    static ibDatabaseConnectionHolder* ThreadHolder();
};
```

Holder-keyed primitives (`ReserveTx`, `Bind/UnbindScopeHolder`,
`Checkout`, `ClearActiveTxConnection`, `CurrentHolder`, etc.) are
private with friend access for `ibConnectionScope`,
`ibDatabaseConnectionHolder`, `ibSingleConnectionHolder`,
`ibDatabaseLayer`. `ThreadHolder` is the exception — it is public so
`ibSchemaBuilder` can resolve the db_query channel's holder across a
save. End-user code goes through holder methods or `ibConnectionScope`.

### `ibConnectionScope`

```cpp
class BACKEND_API ibConnectionScope {
public:
    // Default ctor: holder = ibConnectionPool::CurrentHolder()
    //   (= ibSession::Current()->Holder() if bound, else the per-thread
    //    ThreadHolder() singleton).
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

    std::atomic<int>  m_txDepth   { 0 };
    std::atomic<bool> m_txAborted { false };
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
        //   scope2 inherits conn1 via the holder's scope binding
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
After the 2026-04-28 holder-keyed refactor the registry owns one typed
`ibSessionRegistryConnectionHolder` (`m_writeHolder`) and binds its
persistent write conn through `EnsureConnection()`:

```cpp
// ibSessionRegistry::Start — one persistent conn on its own thread
m_writeConn = m_writeHolder.EnsureConnection();
// Historical: a m_probeHolder/m_probeConn (NOWAIT row-lock probe) and
// a m_lockConn (pessimistic-lock liveness) were both retired together
// with HoldRowLocks / TryProbeRowLock — liveness is heartbeat-based
// now. See docs/session-registry.md "Gotchas" #4.
```

For a side conn that must NOT join the holder's TX, use
`AcquireFreeConnection` — it does NOT install scope binding; the
returned conn is the caller's sole owner until the `shared_ptr` drops,
at which point the custom deleter re-parks the entry.

### Legacy `db_query->X()` without scope

Still works. Macro resolves via `ibConnectionPool::GetDatabaseLayer()`
→ holder priority chain (holder = `Current()->Holder()` if a session is
bound, else the per-thread `ThreadHolder`):

1. Holder's active-TX pinned conn (from a bare `db_query->BeginTransaction()`)
2. Holder's scope-bound conn (from an outer `ibConnectionScope`)
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
4. **Active-TX pins conn to holder** for the TX's full duration.
   `db_query` / `ses_query` for that holder route to the TX's conn
   regardless of scope state — across worker-thread crossings, since
   the key is the holder pointer, not the thread.
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

Counter-based, uniform across all 4 drivers:

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
| ODBC / MSSQL | Increments `@@TRANCOUNT` (basically the same counter model) |

Counter on base unifies all 4 drivers to a single flat-TX semantic
with correct atomic nesting. Inner rollback propagates to outer's
commit via the aborted flag.

**What this does NOT give:** true savepoints. If a caller needs to
roll back inner work while committing outer, use `SAVEPOINT` /
`ROLLBACK TO SAVEPOINT` explicitly at the driver level. Not currently
exposed through `ibDatabaseLayer`.

---

## ⭐⭐ A conn with an OPEN TRANSACTION is not idle (2026-08-14)

`ibConnectionPool::Checkout` used to consider an entry servable when nobody had reserved it
(`txHolder == nullptr && scopeHolder == nullptr`), it was not lent out (`!inUse`), and the driver
reported no live cursor (`!conn->IsBusy()`). It now asks one more question — `!conn->IsActiveTransaction()`
(`connectionPool.cpp`, the reverse scan in `Checkout`).

**`txHolder` is the pool's bookkeeping, not the connection's state.** It records the transactions
the pool was TOLD about: the ones that went through `BeginTransaction` on a pool-owned layer while a
holder was in force. It says nothing about a transaction opened straight on the layer — bare
`db_query->BeginTransaction()`, a path that returned without closing, a refused commit before the
driver was made to roll it back (Firebird compiles views and triggers AT COMMIT, so a bad bundle is
refused there — see [register-shared-machinery.md § 4d](register-shared-machinery.md)).

Hand such a connection out and the next owner inherits somebody else's transaction. Per the nesting
rules above, 0→1 is the real driver Begin and this one is 1→2: the new owner's `BeginTransaction`
merely **nests**, its `Commit` merely **decrements**, and nothing it wrote is ever made durable. That
is the shape the restructuring trace showed on 2026-08-14 — a deferred phase failing with
`Table unknown` on tables its own apply had just created, because the DDL was sitting inside a
transaction that had never been committed.

So a conn in that state is not idle, whatever the bookkeeping says. `Checkout` leaves it where it
is: the owner will finish it, or the reaper will drop it — but nobody else gets to inherit it.

### `ReleaseAll` rolls an unfinished transaction back before parking the conn

The same rule from the other end. `ReleaseAll` already closed result sets and statements before
returning a connection to the idle pool, for exactly the reason its comment states — *the next
checkout must not inherit dangling cursors or pending statements from the previous user*. The
transaction was the one piece of state left out of that list, and it is the one that matters most.

It is **rolled back, not committed**: a holder that released its connection without finishing left
nobody who could commit it, so the work is not wanted. Committing instead would make a half-written
apply durable on the strength of a release. The rollback is wrapped in the cleanup-path `catch (...)`
beside the other two — a rollback failing on an already-dead handle leaves nothing further to
attempt, and this path must not throw ([exceptions.md § 5](exceptions.md)).

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
holder = CurrentHolder();            // ibSession::Current()->Holder() ?? ThreadHolder()
if (pool->GetReservedTx(holder))  return reserved-tx-conn;  // TX pin
if (pool->GetScopeConn(holder))   return scope-bound-conn;  // scope binding
return GetPrimaryConnection();                              // master fallback
```

**Identity scope cheat-sheet:**

| Channel | Holder type | Identity | Lifecycle |
|---|---|---|---|
| Per-session DML | `ibSession` (owns `m_dbHolder`) | one per session | session ctor → registry-managed Close |
| Session-registry I/O | `ibSessionRegistryConnectionHolder` (`m_writeHolder`) | one per registry instance | registry's lifetime |
| DDL / `db_query` | `ibSingleConnectionHolder` (`ThreadHolder`) | one per thread | block-local `thread_local`, never freed |

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
| ~~`objectListQuery.cpp`~~, `*MetadataQuery.cpp` | Read-only SELECTs. Same as above. `objectListQuery.cpp` no longer exists (2026-08-08): the whole `partial/list/` directory went when the dynamic list moved onto the composer + `RunComposerPage` — see [table-model.md](table-model.md). |

### Direct-checkout consumers (post 2026-04-28: via own holder + AcquireFreeConnection)

| File | Holder | Purpose |
|---|---|---|
| `sessionRegistry.cpp` | `m_writeHolder` | 1 persistent conn (sys_session INSERT/UPDATE/DELETE + JobRefreshSnapshot SELECT) |
| `wfrontend.cpp::CheckMetadataAndEvict` | `static ibSingleConnectionHolder` (function-local) | metadata-watcher per-tick |

---

## Pitfalls and gotchas

1. **`m_txDepth` / `m_txAborted` are `std::atomic`** (`databaseLayer.h:515,523`).
   The intended invariant is still one conn per thread at a time
   (pool's Checkout + shared_ptr); the atomics are belt-and-suspenders
   against a caller that breaks it. The counter increment/decrement is
   not a CAS loop, so concurrent same-conn use can still interleave
   wrongly — the atomics only make individual reads/writes well-defined.

2. **Primary conn as fallback for legacy `db_query`.** Call sites
   without a scope go to `m_source`. All threads share master.
   FB embedded has a global `fb_mutex` that serialises; PG/MSSQL
   may race. Migration is opt-in per call site — focus on mutation
   paths first (Write/Delete/Update).

3. **Lazy Clone latency burst.** First Checkout for a new slot pays
   Open() cost (FB embedded ~100ms, PG remote ~50-500ms).
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

9. **TX pin not cleared on bare `Begin` + crash.** If code does
   `db_query->BeginTransaction()` without scope/guard, throws, and
   the catcher doesn't Commit/Rollback, the holder's TX reservation
   stays set → next request for that holder lands on the old TX.
   **Fix:** migrate to `scope.SafeBeginTransaction` (RAII-cleaned).

10. **Pool saturation throws after `kCheckoutTimeout` (30s).** Checkout
    on a saturated pool `cv.wait_for`s up to `kCheckoutTimeout`
    (`connectionPool.h:264`, 30s); on expiry it throws instead of
    waiting forever, so a leaked / stuck borrower can't hang the worker
    indefinitely. A leak still surfaces as 30s stalls — size `maxSize`
    for expected concurrency.

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
| `eWEB_RUNTIME_MODE` (wes) | 4 | multi-tab burst absorption |
| `eSERVICE_MODE` (daemon) | 2 | background tasks, modest load |
| desktop GUI / launcher | 2 | session-manager bookkeeping conn + UI thread |

`maxSize` stays at 32 across modes for now; CLI override is a future
addition.

---

## Future work

- **CLI override for `maxSize` / `minIdle`.** Hosts pass them through
  appData ctor; expose as `--db-pool-max=N --db-pool-min=M` flags so
  ops can tune without rebuilding.

- **Savepoints API.** `ibDatabaseLayer::Savepoint(name) / RollbackTo(name)`
  for callers that need real sub-TX (inner rollback without affecting
  outer). Supported by FB/PG/MSSQL, not SQLite.

- **Pool stats endpoint.** `/admin/pool` on wes returning JSON with
  LiveSize / IdleSize / MaxSize. For ops monitoring.

- **Tiered server architecture.** See `compute-server-tiering.md` —
  the pool is ready for shared-worker + per-session-queue model;
  the server-side dispatcher is the missing piece.
