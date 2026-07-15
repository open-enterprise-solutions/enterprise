# Firebird mesh driver — distributed deployment for OES

Design plan for `ibDatabaseLayerFirebird` as the active coordination
layer for distributed OES deployments. Target sweet spot:
**3-30 users on a shared LAN, databases up to 100 GB**, accessed
through a file share or peer-to-peer mesh — the segment between
single-user embedded deployments and dedicated client-server
infrastructure.

> **FB role in OES: starter tier.** Firebird lets customers try OES
> at zero infrastructure cost — single workstation, RDP terminal
> server, or small shared-LAN deployment. When the customer
> outgrows it (>30 users or >100 GB), migration to PostgreSQL /
> MSSQL is a deliberate one-time operation, not an automatic
> upgrade gate. **Growth happens on the customer's schedule, and
> scripts/queries/reports keep working across the move** (see "SQL
> portability across drivers" below). FB is not positioned to
> compete with PG/MSSQL at the mid-market scale — serious load
> targets PG/MSSQL, while preserving SQL syntax.

> **Status (2026-05-23): Phases 1, 2, 4, 6 production-validated
> end-to-end via manual smoke (Designer + Runtime sharing one .fdb
> on UNC path through leader-election + spawned firebird.exe on
> localhost TCP).** Phase 3 partial (scheduler wired, UI panel
> pending). Phase 5 (mesh) deferred indefinitely — see "Mesh: why
> deferred" section. Phase 7 dialect-portability work reverted as
> off-priority (focus on shara, not multi-driver SQL).
>
> All Phase 1-6 code lives under
> `src/engine/backend/databaseLayer/firebird/` — the driver is the
> sole owner of leader / replication / maintenance state, per the
> architectural principle below. Engine OES sees only the
> `ibDatabaseLayer` abstraction.
>
> Cross-platform: all Win32-specific code paths (`SetDllDirectoryW`,
> `LockFileEx`, `CreateProcessW`, Job Object kill-on-OES-crash) are
> properly `#ifdef __WXMSW__`-gated; POSIX path of `firebirdLease`
> (`fcntl F_SETLK`) implemented; POSIX `firebirdLocalServer` is
> stubbed (no `posix_spawn` yet, returns 0 — graceful degrade to
> single-process embedded). CMake build on macOS / Linux compiles;
> runtime on POSIX = local-mode only.
>
> ### Deployment matrix — what mode OES picks automatically
>
> | Scenario | dbPath | Mode | Setup |
> |---|---|---|---|
> | **Single-user local** | `C:\data\db.fdb` | STANDALONE — embedded fbclient, no firebird.exe, no lease | nothing — just point OES at the file |
> | **Multi-machine shared folder** | `\\HOST\share\db.fdb` | Leader-election — first process becomes leader (spawns firebird.exe), others follow over TCP | drop .fdb on share — that's it |
> | **RDP / Terminal Server** | local path on the TS host | STANDALONE per session; FB lock manager coordinates between processes on the same host via `ProgramData/Firebird` shared memory | nothing — works out of box |
>
> Activation gate is purely path-based: `dbPath.StartsWith("\\\\") || dbPath.StartsWith("//")` triggers leader-mode. Local
> paths always run STANDALONE. The `.lease` sidecar is **not** the
> activation trigger — it would falsely trigger leader-mode every
> time someone copied a share folder to a local disk for inspection
> (the lease file travels with the .fdb).
>
> ### Failover timings (Phase 4 production)
>
> | Event | Recovery time |
> |---|---|
> | Leader graceful shutdown (user closed OES) | <5 s — leader writes `leaderPid=0` sentinel before releasing lock; followers see on next 5 s tick, instant promote |
> | Leader crash / power loss / kill -9 | ~25 s — followers detect via 20 s heartbeat staleness threshold + ~5 s tick + spawn time |
> | Driver reconnect on handoff | **Proactive at every query boundary** — `ReconnectIfLeaderChanged()` runs at the top of `DoRunQuery` / `DoRunQueryWithResults` / `DoPrepareStatement` (cached URL compare, no-op when unchanged). Mid-TX handoff still throws an explicit exception with "retry" instruction (caller must rollback their logical TX and re-do); auto-commit / direct-statement callers self-heal without caller participation. Long-lived pool clones and background-thread holders (session registry heartbeat / snapshot) all benefit — no per-callsite kick required. |
> | Soft-landing in UI (Active Users list) | <1 s blink on graceful exit, ~25 s on crash — first successful refresh after a failure triggers an immediate own-heartbeat + suspends cluster `sweep` for ~5 s so peers' `lastActive` rows don't get pruned while everyone re-attaches. |
>
> ### Regression-prevention bugs already squashed
>
> Manual smoke on `\\NOUVERBE\share\fb_test253\sys.fdb` (2026-05-23)
> surfaced these distinct issues, all landed-fixed:
>
> 1. DPB `isc_dpb_utf8_filename` is a **flag** tag (length=0), not value-bearing. Old code stuffed full URL as value → mangled DPB → silent `isc_io_error` on UNC paths only.
> 2. `wxFile::Exists(strDatabaseUrl)` checked the **URL** (`inet://localhost:54309/\\HOST\share\db.fdb`) instead of file path → always-false → always-took CREATE branch instead of ATTACH → `isc_io_error` on existing DBs.
> 3. Lease `CreateFileW` opened with `FILE_SHARE_READ` only → second-process open failed with `ERROR_SHARING_VIOLATION` before reaching `LockFileEx`. Fixed: `FILE_SHARE_READ | FILE_SHARE_WRITE`.
> 4. Lease auto-create used `CREATE_NEW` (wxFile::Create with overwrite=false) → on SMB cache mismatch could race and truncate existing 98-byte lease to 0 bytes. Fixed: `OPEN_ALWAYS` (never truncates).
> 5. Byte-range `LockFileEx` over offset 0..MAX → on Windows blocked **follower ReadFile** with `ERROR_LOCK_VIOLATION` → `ReadCurrentState` returned invalid → "Lease file unreadable or empty after retry". Fixed: sentinel-byte lock at offset 200 (past content), readable by followers.
> 6. `wxExecute` for spawning `firebird.exe` from the leader-mode self-promote path — `wxExecute` asserts main-thread on **both** Win32 and POSIX. Heartbeat / promote runs on a background thread. Fixed: direct `CreateProcessW` (Win) with a POSIX `posix_spawn` stub.
> 7. `ibValueSystemFunction::Message()` chained into the frontend Output Window (`wxStyledTextCtrl::AppendText`) and was being called from `DatabaseErrorReporter::ThrowDatabaseException`, including from background threads (session registry's snapshot job after a leader handoff). Concurrent dispatch with the main thread's `wxAuiMDIParentFrame` idle handler tripped `wxRecursionGuard` (`m_flag > 0` assert). Fixed: gate the Message call with `wxThread::IsMain()`; background errors still propagate via the exception thrown right after.
> 8. `ReconnectIfLeaderChanged()` was only called inside `DoBeginTransaction`. Long-lived holders that do direct `RunQuery` / `PrepareStatement` outside a TX (session registry's `JobHeartbeatOwn` / `JobRefreshSnapshot`) stayed bound to the dead leader's TCP and looped forever, blanking the Active Users list. Fixed: proactive call at the top of `DoRunQuery` / `DoRunQueryWithResults` / `DoPrepareStatement` (cached URL compare, no-op when unchanged).
> 9. Cluster sweep on the new leader saw peers' `lastActive` trailing because nobody could update during the handoff window → DELETEd live rows; Active Users list went empty for a couple of seconds. Fixed in `sessionRegistry`: on the recovery edge (first successful refresh after a failure) immediately re-`JobHeartbeatOwn` and suspend `JobSweepStale` for ~5 s (`m_sweepSuppressUntilMs`).
> 10. `isc_dpb_parallel_workers` was guarded by `#ifdef`, but the vendored `consts_pub.h` stops at FB 4 tags (max = 95). The block was silently dead — per-attachment parallel-workers never reached the FB 5 runtime, only the `firebird.conf` `ParallelWorkers` ceiling applied. Fixed: local `#ifndef` + `#define isc_dpb_parallel_workers 100` right next to the use site (a future header bump will surface as a clear "macro redefined" diagnostic at the same line).
>
> These together are the recurrent test cases for any future change
> touching FB driver init / lease / DPB construction / cross-thread
> work / leader handoff. See the per-bug commits in git log for the
> diagnosis trail.

---

## Table of contents

1. [Why this exists — gap in the market](#why-this-exists)
2. [Architectural principle — driver as active layer](#architectural-principle)
3. [Deployment scenarios + boundaries](#deployment-scenarios)
4. [Mesh mode — replication architecture](#mesh-mode)
5. [Leader-election mode](#leader-election-mode)
6. [DDL coordination](#ddl-coordination)
7. [Load distribution + query routing](#load-distribution)
8. [Size management — no native compression in FB 5](#size-management)
9. [UX requirements — minimize external files](#ux-requirements)
10. [Bitness handling](#bitness-handling)
11. [Firebird version requirements + vendored libraries](#firebird-version)
12. [Implementation roadmap](#implementation-roadmap)
13. [Open questions and known risks](#open-questions)

---

## Why this exists

Existing low-code ERP platforms in this market commonly offer two
deployment modes with a hard gap between them:

- **File mode** — proprietary file on a shared folder, embedded
  engine in every client, coordination via SMB advisory locks.
  Free in licensing, but practical ceiling is ~5 concurrent users
  and ~2 GB database before corruption becomes regular. Backup
  requires exclusive lock. Small businesses use it despite known
  fragility.
- **Server mode** — dedicated application server plus a separate
  RDBMS server (MSSQL / PostgreSQL / Oracle). Real MVCC, scales to
  hundreds of users. Entry cost typically several thousand dollars
  in licenses, plus ongoing server administration and dedicated
  hardware.

There is no smooth path between the two. A small accounting firm at
8 users / 5 GB has the choice: keep wrestling with file-mode
corruption, or jump to server licensing they can't justify.

The 3-30 user / up-to-100GB band is the underserved gap. The OES
mesh driver targets exactly this band, using Firebird 5.0
capabilities:

- **Native replication framework** (FB 4+) — production-grade
  master-replica streaming, no external add-on.
- **`isc_que_events`** — cross-process event notification with
  sub-second latency.
- **ParallelWorkers** (FB 5) — parallel query execution within a
  single connection.
- **Read consistency** isolation (FB 4+) — long readers don't block
  writers.
- **DECFLOAT / INT128** (FB 4+) — exact decimal at engine level.
- **Selective logical replication + per-table filters** (FB 5) —
  each peer subscribes only to what it needs.

Built on these primitives, the file-share scenario stops being
"hope SMB locks hold" and becomes a real distributed architecture
with eventual consistency, automatic failover, and zero
administration.

---

## Architectural principle

**All mesh / leader-election / replication / failover logic lives
inside `ibDatabaseLayerFirebird`. The OES engine sees only the
existing `ibDatabaseLayer` abstraction.**

### What the engine still does

```cpp
auto scope = ibConnectionPool::GetFreeConnection();
scope.SafeBeginTransaction();
db_query->RunQuery("INSERT INTO _C_42 (...) VALUES (...)");
scope.SafeCommitTransaction();
```

Identical to today. Every existing call site in `catalogObject.cpp`,
`documentObject.cpp`, the 11 metadata-object families, the form
system, the LINQ runtime — **unchanged**. The 50K+ lines of business
logic in `backend/metaCollection/` don't know about mesh.

### What the driver hides

All FB driver files live **flat** in
`src/engine/backend/databaseLayer/firebird/` — there is no `mesh/`
subtree (the per-concern directory layout in earlier drafts of this
doc was never built). The vendored FB runtime is under
`firebird/engine/`. Status column: ✅ = in tree, ⏳ = deferred (Phase 5
mesh, see below).

| Concern | File / class | Status |
|---|---|---|
| Connection mode selection | `firebirdDatabaseLayer.cpp` — `ibDatabaseLayerFirebird::Open` picks embedded / local-server / leader from path + config | ✅ |
| Lease coordination | `firebirdLease.{h,cpp}` — `ibFirebirdLease`, SMB byte-range lock on `.lease` sidecar | ✅ |
| Leader / follower orchestration | `firebirdLeaderMode.{h,cpp}` — `ibFirebirdLeaderMode` | ✅ |
| Out-of-process local server | `firebirdLocalServer.{h,cpp}` — `ibFirebirdLocalServer` (opt-in `OES_FB_LOCALSERVER`) | ✅ |
| HLC clock | `firebirdHlc.{h,cpp}` — `ibHlcClock` | ✅ (skeleton, mesh-only consumer) |
| Replication config writer | `firebirdReplicationConfig.{h,cpp}` — `ibFirebirdReplicationConfig` | ✅ (skeleton, mesh-only consumer) |
| BLOB/TEXT compression | `firebirdBlobCompression.{h,cpp}` — `ibFirebirdBlobCompression`, **zlib** (`wxZlibInputStream`/`OutputStream`, not zstd) in the param/result BLOB path (transparent) | ✅ |
| Scheduled backup-restore / adaptive sweep | `firebirdMaintenance.{h,cpp}` + `firebirdMaintenanceScheduler.{h,cpp}` | partial (primitives ✅, scheduler ⏳) |
| Peer discovery (mDNS / NAS manifest) | not built | ⏳ |
| Adaptive subscription | not built | ⏳ |
| DDL 2-phase deploy across peers | not built | ⏳ |
| Query routing (local / peer / archive) | not built | ⏳ |

The `ibDatabaseLayer` interface gains at most two optional methods:

```cpp
class ibDatabaseLayer {
    // ... existing API unchanged ...

    // Optional — for UI status bar.
    virtual ibReplicationStatus GetReplicationStatus() const { return {}; }

    // Optional — for engine to react to peer events (metadata
    // reload, peer joined, peer disconnected).
    virtual void RegisterPeerEventListener(ibPeerEventHandler* h) {}
};
```

Both default to no-op for non-mesh drivers. Engine consults them
only when surfacing distributed status.

### Why other drivers stay simple

`ibDatabaseLayerPostgres`, `ibDatabaseLayerMySQL`,
`ibDatabaseLayerODBC`, `ibDatabaseLayerSQLite` remain thin wrappers.
For PG / MySQL / MSSQL the production deployment is standard
client-server; users at the scale of these RDBMS already have
dedicated server infrastructure. SQLite is single-process by
definition.

Mesh logic in the FB driver is **not** a generic infrastructure
layer to be shared with other drivers — it's FB-specific code
using FB-specific replication primitives (`isc_que_events`, FB
replication log format, `nbackup` deltas).

---

## Deployment scenarios

### Sweet spot (the design target)

| Scenario | Users | DB size | Mode |
|---|---|---|---|
| **Small office / clinic / shop, shared LAN** | 3-30 | <100 GB | **Mesh** or **leader-election** through file share |

This is where the driver carries unique value.

### Firebird driver responsibility — bottom of the market only

The FB driver is positioned for the **low-end deployment segment
exclusively**. Past 30 users / 100 GB, the answer is not "tune FB
harder" or "give FB its own dedicated server" — it's a different
RDBMS, period.

| Scenario | Driver | Notes |
|---|---|---|
| Single user, desktop | **FB embedded** (`ibDatabaseLayerFirebird`) | Works as-is via `fbclient.dll` — no coordination needed |
| RDP / terminal server (10-30 ops on one box) | **FB Classic + lock table** (`ibDatabaseLayerFirebird`) | FB's own multi-process coordination via `%TEMP%/fb_lock_*` — works as-is |
| Small office / clinic / shop, shared LAN, **3-30 users, <100 GB** | **FB mesh or leader-election** (`ibDatabaseLayerFirebird`) | **The design target of this document** |
| 30+ users **or** >100 GB | **PostgreSQL** (`ibDatabaseLayerPostgres`) **or MSSQL** (`ibDatabaseLayerODBC`) | Out of FB driver scope. PG / MSSQL are the right tool here. Migration requires data export from `.fdb` → import to PG/MSSQL schema. |

FB doesn't try to compete with PG/MSSQL at the mid-market /
enterprise scale. They cover the upper segment, FB covers the
lower. They don't overlap.

### Boundaries (hard cap)

The mesh / leader mode is **hard-capped**:

- **Maximum 30 active users.** Mesh replication is O(N²) in peer
  count — at N=30 that's 900 simultaneous shipping flows. Past
  this point latency degrades and CPU per node grows. **The answer
  past this boundary is migration to PG or MSSQL, not extending
  FB.**
- **Maximum 100 GB total database size.** Archive-node CPU becomes
  the bottleneck for cross-cutting reports past this. Initial sync
  of a new peer via `nbackup` over Gigabit LAN takes ~15 minutes at
  100 GB; doubling the size doubles the bring-up cost. **Past 100
  GB → migrate to PG/MSSQL.**

### Migration path past the boundary

Migration **is not a config flip** in the FB driver — it's a real
RDBMS migration. But it's a **one-time operation** during company
growth, not an ongoing burden:

1. Run final `gbak -B` on the FB database, capture data dump.
2. Run schema-conversion tool (FB DDL → PG / MSSQL DDL — driven
   from OES metadata so semantics preserved).
3. Bulk-load data into the new RDBMS.
4. Update `connect.cfg` connection string — points at PG/MSSQL
   instead of `.fdb`.
5. Restart OES instances — they pick up `ibDatabaseLayerPostgres`
   or `ibDatabaseLayerODBC` automatically based on the connection
   string.

**Engine OES sees no difference** — the abstraction over
`ibDatabaseLayer` keeps all business logic identical. Forms, scripts,
reports, all 11 metadata object types work unchanged.

The migration tool is itself a roadmap item (Phase 7 below) — once
built, it makes "outgrow FB" a routine, predictable operation
rather than a manual data-engineering project.

### SQL portability across drivers

Migration is honest only if **the SQL written / generated by OES
keeps working across drivers**. Connection abstraction alone is not
enough — dialect divergence in queries breaks every report and
every list view on the day of the switch.

**What's already done:**

- `ibDatabaseLayer` + `ibPreparedStatement` — uniform connection /
  cursor / parameter-binding contract. All four drivers (FB / PG /
  MySQL / ODBC) conform. Connection-level portability ✓.
- `ibNumber` — exact-decimal type that maps to `NUMERIC(38)` on PG,
  `DECIMAL(38)` on MSSQL, `DECFLOAT/INT128` on FB. Numeric
  portability ✓.
- `ibDateTime` / `ibPreparedStatement::SetParamDate` — handles
  the per-driver TIMESTAMP/DATETIME/DATETIMEOFFSET nuances. ✓.
- Bulk of OES SQL is **generated from metadata** in
  `commonObjectQuery.cpp`, `*Manager_impl.cpp`,
  `objectListQuery.cpp`, `registerSqlBuilder.cpp`. This is the
  right place to add dialect routing — the generator emits
  per-driver SQL from a single high-level intent.

**What still leaks dialect-specific syntax:**

| Construct | FB | PostgreSQL | MSSQL | Portable form |
|---|---|---|---|---|
| Upsert | `UPDATE OR INSERT … MATCHING` | `INSERT … ON CONFLICT … DO UPDATE` | `MERGE` | `db->Upsert(table, cols, pk)` helper |
| Sequence | `GENERATOR + GEN_ID` | `SEQUENCE + nextval()` | `SEQUENCE + NEXT VALUE FOR` | `db->NextId(seqName)` helper |
| LIMIT | `ROWS n` / `FIRST n` | `LIMIT n OFFSET m` | `OFFSET … FETCH NEXT n ROWS ONLY` | `db->ApplyLimit(query, n, offset)` |
| String concat | `\|\|` | `\|\|` | `+` or `CONCAT()` | Use `CONCAT(a, b)` everywhere — works on all three |
| Boolean type | `BOOLEAN` (FB 3+) | `BOOLEAN` | `BIT` | `ibPreparedStatement::SetParamBool` masks the difference |
| BLOB | `BLOB SUB_TYPE` | `BYTEA` / large objects | `VARBINARY(MAX)` | `SetParamBlob` / `GetResultBlob` already hide this |
| Recursive CTE | `WITH RECURSIVE` | `WITH RECURSIVE` | `WITH` (recursive variant) | Common subset works on all three |
| Window functions | yes (FB 3+) | yes | yes | Safe to use unconditionally |

The remaining handwritten SQL outside the metadata generators —
`appDataQuery.cpp`, `metaAttributeObjectQuery.cpp`,
`metadataConfigurationQuery.cpp`, scattered `*Query.cpp` files —
currently uses FB-flavored syntax with ad-hoc
`if (DATABASELAYER_POSTGRESQL) … else …` branches in a few
places. The pattern exists; the work is **disciplined sweep + a
small `ibSqlDialect` helper** that emits per-driver building
blocks instead of hardcoded fragments.

**Without this layer, Phase 7 migration tooling breaks on every
query that uses dialect-specific syntax.** It's a hard
prerequisite — and the work pays off independently of migration:
it eliminates the "works on FB, fails on PG" class of bugs that
already shows up during development.

### Mode selection logic

Driver autodetects on `Open`:

```cpp
ibDatabaseLayerFirebird::Open(connStr) {
    if (HasMeshConfig(connStr)) {
        InitMeshMode(connStr);
    } else if (HasLeaseFile(connStr)) {
        InitLeaderElectionMode(connStr);
    } else if (IsRemoteUrl(connStr)) {
        InitTcpClientMode(connStr);
    } else if (config.PreferLocalServer) {
        SpawnLocalFbServerIfNeeded();
        InitTcpClientMode("localhost");
    } else {
        InitEmbeddedMode(connStr);
    }
}
```

User-facing: nothing changes. They point at a database folder, the
driver looks for `mesh.conf` next to it. If found, mesh mode
activates. If not, plain embedded.

---

## Mesh mode

### High-level topology

Every peer carries:
- **Schema** (full — small).
- **Reference tables** (Catalogs, Constants, Enumerations) — full
  replication, usually <1 GB total.
- **"My" working set** — Documents this peer's user actively edits.
- **Current-period registers** — most-recent N months of
  AccumulationRegister, InformationRegister, AccountingRegister.
- **Optional adaptive cache** — peer Documents fetched on-demand,
  promoted to local subscription on first access, demoted after M
  days idle.

One peer is designated **archive-node**. It holds **everything**:
full history of registers, all Documents regardless of owner, all
adaptive-subscription requests. Other peers fall back to the
archive-node for misses.

```
   Peer A (User)              Peer B (User)              Peer C (Archive)
┌────────────────┐         ┌────────────────┐         ┌──────────────────┐
│ enterprise.exe │         │ enterprise.exe │         │ enterprise.exe   │
│   ↓ embedded   │         │   ↓ embedded   │         │   ↓ embedded     │
│   FB 5 engine  │         │   FB 5 engine  │         │   FB 5 engine    │
│   ↓            │         │   ↓            │         │   ↓              │
│ peer_A.fdb     │         │ peer_B.fdb     │         │ archive.fdb      │
│  ~7 GB         │         │  ~7 GB         │         │  100 GB (full)   │
│  (working set) │         │  (working set) │         │                  │
│                │         │                │         │                  │
│ TCP listener   │◄──repl──┤ TCP listener   ├──repl──►│ TCP listener     │
│ TCP listener   │◄────────repl─────────────────────►│ TCP listener     │
└────────────────┘         └────────────────┘         └──────────────────┘
        ▲                          ▲                            ▲
        │                          │                            │
        └──── ad-hoc reads ────────┴────────── reports ────────┘
                            (TCP fallback for misses)
```

### Replication mechanics

FB 4+ ships every committed transaction to a replication log
(binary stream). Peers consume each other's logs via TCP. FB 5
supports per-table filters so peer X can subscribe to
`Catalogs.* + Documents WHERE owner=X` and skip everything else.

```ini
# /<peer-A>/_fb/replication.conf  — generated by driver
[peer_A]
log_directory = ./repl_logs
ship_to = peer_B:3050, peer_C:3050
filter = "table IN (cat_*, doc_my_*, reg_current_*)"

[peer_B]
consume_from = peer_A:3050
```

Driver writes `replication.conf` on startup from `mesh.conf`. User
never touches it.

### Adaptive subscription

```cpp
// DESIGN pseudo-code (mesh, Phase 5 — deferred; FetchRow does not exist)
ibValue ibDatabaseLayerFirebird::FetchRow(tableId, rowId) {
    if (IsLocallySubscribed(tableId, rowId)) {
        return ReadLocal(tableId, rowId);
    }

    // Cache miss — fetch from archive over TCP.
    auto row = FetchFromArchive(tableId, rowId);

    // Auto-subscribe if policy says so.
    if (m_subscriptionPolicy.ShouldAutoSubscribe(tableId, rowId)) {
        ExtendSubscription(tableId, rowId);
        // Sends "ship me this row + its future updates" request
        // to archive. From now on, all updates to this row land
        // locally via replication.
    }

    return row;
}
```

**Default subscription policies:**

| Table category | Policy | Demote after |
|---|---|---|
| Schema, metadata | Always replicate everywhere | Never |
| Catalogs (`_C_*`) | Always replicate everywhere | Never |
| Constants | Always replicate everywhere | Never |
| Enumerations | Always replicate everywhere | Never |
| Documents (`_D_*`) | Subscribe on first access | 30 days idle |
| InformationRegister (current period) | Subscribe on first access | 30 days |
| InformationRegister (history) | Never (archive-only) | — |
| AccumulationRegister (current period) | Always | Never |
| AccumulationRegister (history) | Never | — |
| AccountingRegister (current period) | Always | Never |
| AccountingRegister (history) | Never | — |
| sys_bytecode_cache | Always (full mesh) | Never |

Override per-user via `mesh.conf`: e.g., an analyst gets
`policy = Full` (subscribe to everything → effectively becomes
secondary archive). A field worker on bad connectivity gets
`policy = Thin` (subscribe only on explicit access, no auto-promote).

### HLC clock for conflict resolution

Hybrid Logical Clock — combines wall-clock with logical counter:

```
hlc = (physical_ms_since_epoch << 16) | counter
                                          ↑
                                       monotonic per peer
```

Every write stamps `(hlc, peer_id)` on the modified row. Conflicts
resolved by **last-writer-wins by HLC** at replication apply time:

```
INCOMING row with hlc_in, peer_in
LOCAL    row with hlc_local, peer_local

if hlc_in > hlc_local:                apply incoming
elif hlc_in < hlc_local:               drop incoming
elif hlc_in == hlc_local:              apply if peer_in > peer_local (deterministic tiebreak)
```

Per-row, deterministic, no central coordinator. Application-layer
override per object kind possible via Designer property
(`Document.X.ConflictResolution = FirstWriterWins` if business rule
needs it).

### Document numbering

Distributed IDs without coordination:

```
ID = (peer_id_4bit << 60) | (hlc_timestamp_56bit)
```

Globally unique by construction. Sortable by HLC = global causal
order. For human-readable sequential numbers (`Invoice #1234`):
range allocation per peer — peer A gets 1-10000, B gets 10001-20000.
Numbers unique but not sequentially consecutive across peers. Period
close runs a re-numbering pass on archive-node for users who want
consecutive output.

### Read-your-writes consistency

After a write, subsequent reads from the same session must see
their own write. Driver maintains per-session `last_write_hlc`:

```cpp
// DESIGN pseudo-code (mesh, Phase 5 — deferred)
ibDatabaseResultSet* ibDatabaseLayerFirebird::RunQueryWithResults(...) {
    if (this_session.last_write_hlc.has_value()) {
        // Wait up to N ms for local replica to catch up to this HLC.
        if (!WaitForLocalHlc(this_session.last_write_hlc, 200ms)) {
            // Timed out — fall back to direct query to owner peer.
            return ExecuteRemote(GetOwnerPeerOfWrite(), ...);
        }
    }
    return ExecuteLocal(...);
}
```

User sees own writes immediately. Other users see them with
replication lag (~100-1000 ms on LAN). Read-after-write contract
preserved without blocking writes.

---

## Leader-election mode

Alternative to mesh when:
- Network is slow or unstable (replication shipping costs hurt).
- Storage per peer is constrained (no room for working set + cache).
- Operational simplicity preferred over offline capability.

### Mechanism

`.fdb` lives on shared storage. Sidecar `.fdb.lease` carries
ownership info:

```
db.fdb               (the database)
db.fdb.lease         (sidecar — see below)
db.fdb.delta-*       (FB nbackup deltas if local-cache enabled)
```

Sidecar contents (binary, fixed layout):

```cpp
struct ibLeaseFileV1 {
    uint32_t magic;              // 'OESL'
    uint32_t version;            // 1
    uint64_t generation;         // bumped on every handoff
    uint64_t heartbeat_unix_ms;  // last refresh
    char     leader_host[64];    // ASCII hostname
    uint16_t leader_port;        // TCP port leader listens on
    uint32_t leader_pid;         // PID of leader process
};
```

### Acquisition protocol

```cpp
// Class in tree: ibFirebirdLease (firebirdLease.{h,cpp}).
// Sidecar opened FILE_SHARE_READ | FILE_SHARE_WRITE (so followers can
// ReadFile it), created OPEN_ALWAYS (never truncates), and the
// EXCLUSIVE lock is taken on a SENTINEL byte at kLockSentinelOffset
// (200) — past the content — so the lock doesn't block follower reads.
// (All three were squashed bugs; see "Regression-prevention" up top.)
ibFirebirdLease::AcquireResult ibFirebirdLease::TryAcquireExclusive() {
    m_hFile = CreateFileW(/*…*/, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          OPEN_ALWAYS, /*…*/);
    OVERLAPPED ov = {};
    ov.Offset = kLockSentinelOffset;            // 200, not 0..MAXDWORD
    if (LockFileEx(m_hFile, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                   0, 1, 0, &ov)) {
        return AcquireResult::Acquired;          // we are the leader
    }
    return AcquireResult::AnotherLeaderActive;
}
```

SMB byte-range locks **do** propagate between machines correctly —
this is the same primitive that file-mode coordination historically
relies on, but applied here only to leadership election — not to
data-page synchronisation.

### Leader preference (host-match heuristic)

The startup race is biased so that the OES instance running on
the **host that physically owns the share** wins the lease when
multiple peers start within a short window. Mechanism:

- Parse `\\HOST\share\db.fdb` (or `//HOST/share/db.fdb`) → extract
  `HOST`.
- Compare with `wxGetHostName()`. If equal → this OES instance is
  **preferred**: its writes will be local-disk, not over SMB.
- Non-preferred instances sleep 150 ms before calling
  `TryAcquireExclusive`. Preferred instances proceed immediately.
  Within the 150 ms window the preferred peer reaches the OS
  lock first and wins.

Effect on common deployments:

| Scenario | Outcome |
|---|---|
| Small office, share on a designated "main PC", OES runs on it | main PC becomes leader automatically |
| Same, OES not on the file-host (NAS, server without OES) | no host-match peer; first-grab wins among the laptops (same as before) |
| Peer-to-peer, share on one user's PC | share-owner becomes leader automatically when they start |
| All laptops join after another non-preferred peer is already leader | preferred late joiner stays a follower; live preemption is not yet implemented (admin can restart the existing leader to trigger handoff) |

Limitation: this only biases the initial race. Once a
non-preferred peer is established as leader (e.g. it started
before the preferred peer woke up), a preferred late joiner
becomes a follower like everyone else. Active preemption (the
preferred peer signals the existing non-preferred leader to
vacate) is deferred — adds a lease-format field +
follower-to-leader signalling. For the common pattern
("file-host PC is the first one on in the morning"), the
startup bias is sufficient.

### Lifecycle

```
Process startup:
  1. (Non-preferred only) 150 ms head-start delay for preferred peers.
  2. TryAcquireExclusive on db.fdb.lease.
     ├─ Acquired → become Leader.
     │     - Open db.fdb locally via embedded FB.
     │     - Start TCP listener on free port.
     │     - Write leader_host:port + heartbeat + bump generation.
     │     - Heartbeat-refresh every 5 sec.
     └─ Denied → read lease, become Follower.
           - Connect to leader_host:leader_port via TCP.
           - Watch lease file for generation changes
             (handoff detection).

On graceful shutdown of Leader:
  - Write "vacating" marker in lease.
  - Close .fdb.
  - Release SMB lock.
  - Followers see "vacating" → race to acquire.

On crash of Leader (kill -9 / network drop / power off):
  - SMB lock released by OS when socket closes (~30 sec typical).
  - Heartbeat staleness > threshold also signals dead leader.
  - Followers race to acquire; first wins, bumps generation, starts
    serving. Other followers reconnect via new generation.
```

### Soft-landing during handoff

Once a new leader is up, two cooperating mechanisms keep the
visible state (Active Users list, in-flight queries) from
flickering or blanking out while everyone re-attaches:

**1. Proactive driver self-heal** —
`ibDatabaseLayerFirebird::ReconnectIfLeaderChanged()` runs at the
top of every `DoRunQuery`, `DoRunQueryWithResults`, and
`DoPrepareStatement` (not just `DoBeginTransaction`). The check
is a cached-URL string compare against
`ibFirebirdLeaderMode::CurrentConnectUrl()` — single dirty bit on
the hot path. When it fires, `Close()` is best-effort, the
handles are nulled, and `Open()` re-attaches against the URL the
leader-mode singleton currently reports. Mid-TX handoff is
explicitly refused (the FB transaction handle is gone, callers
must rollback their logical TX and retry) — auto-commit and
direct-statement callers self-heal silently.

The pool implications matter: `ibConnectionPool` may hold 5-30
clones of the layer, each with its own TCP to the old leader's
`firebird.exe`. With the per-checkout reconnect, each clone
self-heals the first time it's used after a handoff, not at some
external "pool flush" event. There's also a generic
`ibDatabaseLayer::ReconnectIfStale()` virtual on the base for
non-FB callers that want to be explicit, but the proactive
in-driver path makes it optional.

**2. Sweep grace window** — sessions don't get pruned for ~5 s
after recovery, even if their `lastActive` rows trail.
`ibSessionRegistry::JobRefreshSnapshot` tracks
`m_refreshFailedLastTick` and on the false→true→false edge
(first successful refresh after a failure) it (a) immediately
calls `JobHeartbeatOwn()` so our own rows are fresh before any
sweep, and (b) sets `m_sweepSuppressUntilMs = now + 5000ms`.
`JobSweepStale` early-returns while inside the grace window.
Without this, the new leader's first sweep would see `lastActive`
trailing for every peer that couldn't write through the handoff
gap and DELETE the lot — Active Users blanks, owners panic.

Combined timing for the user-visible "blink":

| Trigger | Blink duration |
|---|---|
| Designer (leader) closed cleanly | <1 s — `leaderPid=0` sentinel; first follower promotes on next 5 s tick; soft-landing covers the gap |
| Designer (leader) `kill -9` | ~25 s — wait for heartbeat-stale detection + promote + spawn |
| Network blip (<5 s) | None — proactive reconnect catches it on next query |

### Why this works for FB but not for app-level locks

The fundamental Firebird rule is "never put .fdb on shared folder
with multiple clients accessing directly." This is correct — FB
clients can't coordinate cache, OAT, page locks over SMB.

In leader-election mode, **only one client at a time has the .fdb
open**. That client is on whichever machine won the lease. SMB is
used *only* for the lease byte-lock, not for FB data coordination.
The FB process on the leader machine reads/writes the .fdb file
that physically sits on the share — but it's the only reader and
writer. This is fully supported by FB; SMB is just network-mounted
storage at that point.

Performance suffers slightly because the leader's FB does I/O over
SMB instead of local disk. Mitigation: leader optionally
**downloads .fdb to local cache** on startup, applies all writes
locally, syncs back to share via `nbackup` incremental deltas
periodically + on graceful shutdown. SMB latency removed from hot
path.

---

## DDL coordination

Schema changes (designer deploy) are the highest-coordination
operation in either mesh or leader mode. Existing OES infrastructure
handles single-instance deploys via `sys_config.file_guid` bump +
`signal` column. Extends naturally to distributed.

### Single-instance recap (already implemented)

- Designer Save bumps `sys_config.file_guid`.
- 15-sec polling watcher (or `isc_que_events` after migration)
  detects bump.
- Each session evicted, compile cache dropped, `LoadDatabase +
  RunDatabase` reloads metadata.
- AOT bytecode cache rows for affected modules invalidated through
  dependency graph.

### Mesh extension — 2-phase deploy

```
Designer initiates Save on admin-node.

Phase 1 — quiesce:
  1. Driver acquires DDL-exclusive lease on metadata.deploy.lease
     (separate from leader-election lease).
  2. POST FB event "metadata_locking" via isc_que_events.
  3. Each peer's event handler:
     - Surface "Schema update in progress, ~5 seconds" in UI.
     - Close open forms (save-dialog grace, or force after timeout).
     - Drop compile cache (ibByteCode, ibCompileValueCache).
     - Return "deploy in progress" 503-equivalent to new sessions.
  4. Peers ACK "ready" via sys_session.signal column.
  5. Admin-node waits for all ACKs (with timeout).

Phase 2 — apply + resume:
  6. Admin-node applies DDL in a single FB transaction.
     - DDL writes to replication log.
  7. Replication stream delivers DDL to peers.
  8. Each peer applies DDL via FB native replication.
     - If apply fails (FK constraint, etc.), peer marked
       out-of-sync; admin sees in designer.
  9. POST event "metadata_unlocked".
 10. Each peer: LoadDatabase + RunDatabase + clear AOT rows +
     resume sessions.
 11. Admin-node releases deploy lease.
```

### Failure modes

| Scenario | Behaviour |
|---|---|
| Peer doesn't ACK quiesce in timeout | Admin aborts, releases lease, no DDL applied. Designer reports peer-X-timeout. |
| Peer apply fails (FK violation, missing data, etc.) | Peer marked out-of-sync, others continue. Admin tool surfaces "peer X needs manual reconciliation." |
| Admin-node crashes mid-phase-1 | Lease times out, peers exit 503 mode after timeout, deploy effectively cancelled. |
| Admin-node crashes mid-phase-2 | DDL may be partially applied (atomic on admin via FB TX, but replication may be partial). Out-of-sync flag set on affected peers; manual reconciliation tool runs `nbackup` snapshot from healthy archive-node. |
| Network split during deploy | Isolated peers stay in 503 until reconnection, then catch up via normal replication stream (DDL idempotent forms via `CREATE OR ALTER`, `ADD COLUMN IF NOT EXISTS`). |

### Designer admin-node election

DDL deploys are rare and human-initiated. Designer runs from one
designated machine in practice. Simplification: **the machine
running designer.exe is implicitly the admin-node**. It acquires
the deploy lease before any DDL. Two designers attempting deploy
simultaneously → second gets "deploy in progress, retry."

No quorum needed. No Raft. The lease primitive is sufficient.

---

## Load distribution

### Read distribution — automatic via locality

Each user's queries serve from their local FB replica. 20 users =
20 separate machine CPUs running queries in parallel. **The
fundamental property of mesh:** read load scales with the cluster,
not against a single shared server.

Per-query routing decision in driver:

```cpp
QueryRoute ibQueryRouter::Decide(const Query& q) {
    if (q.IsWrite()) {
        if (TargetIsLocallyOwned(q)) {
            return RouteLocal;
        }
        return RouteToOwner(GetOwnerPeer(q.target));
    }

    // Read.
    if (TouchesOnlyReplicatedTables(q) && LocalReplicaFresh(q)) {
        return RouteLocal;
    }

    if (TouchesArchiveOnlyTables(q)) {
        return RouteToArchive;
    }

    if (q.DesignerHint == "RunOn=ArchiveNode") {
        return RouteToArchive;
    }

    if (q.DesignerHint == "RunOn=LeastLoaded") {
        return RouteToLeastLoadedPeer();
    }

    return RouteLocal;  // default
}
```

### Heavy reports

Reports needing full history (5-year sales, full-company P&L) run
on archive-node. Designer property on the report:

```
Report.SalesAnalytics2026.RunOn = ArchiveNode
```

Driver routes the SQL to archive-node via TCP. Result streamed
back. Archive-node has ParallelWorkers enabled — uses all its cores
on the report.

Local-fast reports (current-month transactions of this user) run on
local node — no remote hop, instant.

### Background jobs / scheduled tasks

Each job has a lease. Exactly one peer holds it at a time. Failover
on lease expiry. Coordinated via the same lease primitive used for
leader-election.

(Background job framework is a separate roadmap item — not in
current OES. Listed here for forward compatibility of the driver
API.)

### Peer load advertising

Each peer broadcasts via `isc_que_events`:

```
peer_load{peer_id, cpu_percent, ram_used_mb, active_sessions, repl_lag_ms}
```

Heartbeated every 10 sec. Driver maintains `peer_load_table` for
`RouteToLeastLoaded` decisions. Cheap; doesn't drive hot path.

---

## Size management

### The problem

Firebird does **not** have native page compression. Database
file grows monotonically:

1. **MVCC versions.** Every UPDATE keeps the old row version
   until sweep. Active OLTP — base grows continuously.
2. **Lazy sweep.** Default `SweepInterval = 20000` transactions.
   Between sweeps, space occupied by dead versions is not freed.
3. **Index fragmentation.** No automatic index compaction.
4. **BLOB storage.** No deduplication, no compression.
5. **`.fdb` never shrinks.** Even after sweep, freed pages are
   reused inside the file but file size doesn't decrease. The only
   way to physically shrink: `gbak -B; gbak -R` cycle.

For active document writing on a 100 GB base, growth can be 2-3 GB
per active week without intervention.

### Driver-side mitigations (transparent to engine)

#### 1. BLOB/TEXT compression (landed) — `ibFirebirdBlobCompression`

The shipped implementation lives in `firebirdBlobCompression.{h,cpp}`
and is called from the FB param/result BLOB path. It uses **zlib**
(`wxZlibOutputStream` / `wxZlibInputStream`, default level 6), **not
zstd**, and an `OESC` 4-byte magic header — *not* the `0x00`/`0x01`
byte marker the original design sketch below assumed:

- `Wrap(data, size)` — compresses when `size >= kCompressThresholdBytes`
  (512) **and** the deflated body is smaller than the input; otherwise
  stores raw with no header. So an uncompressed payload carries **no**
  prefix byte (legacy DBs read transparently).
- `HasMagic(data, size)` / `Unwrap(...)` on the read path — magic
  present ⇒ inflate; absent ⇒ pass through.

The original (pre-implementation) sketch — kept for the design intent
only, do not treat as current code:

```cpp
// DESIGN SKETCH — superseded by ibFirebirdBlobCompression (zlib + OESC magic)
wxMemoryBuffer ibFirebirdBlobCompression::Wrap(const void* data, size_t size) {
    if (size >= kCompressThresholdBytes) {        // 512 bytes
        // wxZlibOutputStream deflate; keep only if smaller than input.
        // On keep: OESC magic header + deflated body.
        // On reject / below threshold: raw bytes, no header.
    }
}
```

Effect: BLOB columns with text content (Document descriptions,
DataProcessor body, attached files) shrink by 50-80% on disk.
Engine OES knows nothing about compression — `SetParamBlob` /
`GetResultBlob` keep their wire-level semantics.

Cost: zlib deflate/inflate on every BLOB I/O above threshold —
negligible for typical sizes; the gate skips it below 512 B.

#### 2. Scheduled backup-restore cycle

Driver schedules `gbak -B; gbak -R` (primitives in
`ibFirebirdMaintenance`; the weekly scheduler
`ibFirebirdMaintenanceScheduler` is scaffolded but not yet wired):

```cpp
// shape — see ibFirebirdMaintenance / ibFirebirdMaintenanceScheduler
class ibFirebirdMaintenanceScheduler {
    void OnSundayNight() {
        BroadcastEvent("maintenance_starting", duration_estimate);
        QuiesceAllPeers();

        gbak -B db.fdb temp.fbk
        gbak -R temp.fbk db.fdb.new
        atomic_rename(db.fdb.new → db.fdb)
        rm temp.fbk

        ResumePeers();
        BroadcastEvent("maintenance_done");
    }
};
```

Effect: reclaims 30-60% of space lost to MVCC versions.

#### 3. Adaptive sweep trigger

Driver monitors `MON$DATABASE` for OAT/OST divergence. When
delta > 50 000 transactions, fires `gfix -sweep` in background.
Engine doesn't notice.

#### 4. Archive rotation (longer-term, separate roadmap item)

When AccumulationRegister / AccountingRegister cross size
thresholds, driver offers Designer-side tooling to split: closed
periods migrate to a separate `archive_2024.fdb` accessed via
attach. Main `.fdb` shrinks to current-period data only. Mesh
configuration adjusts — archive .fdb is read-only, mounted only on
archive-node.

---

## UX requirements

User expectation: **point at a folder, get a working database. No
plumbing in the user's view.** The goal below is "everything FB in one
hidden `_fb/` folder, nothing next to the exes" — **achieved**. The
stricter "zero config files even inside `_fb/`" is **not** achieved
and, for `firebird.conf` (which carries `ServerMode = SuperClassic`),
not achievable via DPB alone.

### Actual `_fb/` layout (build output, verified 2026-06-19)

```
C:\Programs\OES\
├── enterprise.exe  designer.exe  backend.dll  frontend.dll  wfrontend.dll
└── _fb\                              ← single hidden folder
    ├── fbclient_{x64,x86}.dll
    ├── firebird_{x64,x86}.exe        out-of-process server
    ├── ib_util_{x64,x86}.dll
    ├── icudt63_{x64,x86}.dll  icuin63_{x64,x86}.dll  icuuc63_{x64,x86}.dll
    ├── icudt63l.dat                  (27 MB ICU data)
    ├── security5.fdb                 (pre-baked security DB)
    ├── firebird.conf                 (ServerMode = SuperClassic — load-bearing)
    ├── firebird.msg
    ├── plugins\                      engine13 / srp / legacy_auth /
    │                                 legacy_usermanager / chacha /
    │                                 default_profiler / fbtrace / udr_engine
    └── intl\  fbintl_{x64,x86}.dll + fbintl.conf

<user-chosen folder>\
├── sys.fdb
├── mesh.conf        (only in a multi-user share scenario)
└── sys.fdb.lease    (only in a leader-election scenario)
```

### What landed vs what was dropped

| File | Goal | Reality |
|---|---|---|
| FB DLLs / exe / plugins | Hidden in `_fb/`, nothing next to exes | ✅ done — `backend.vcxproj` `CopyFileToFolders` → `_fb/`; `ibFirebirdBootstrap` calls `SetDllDirectory(_fb)` |
| `firebird.conf` | **Eliminate** (push all via DPB) | ❌ **kept** — it sets `ServerMode = SuperClassic`, which a per-attachment DPB cannot. DPB carries the per-attach knobs (lc_ctype, page_size, force_write, parallel_workers, …); ServerMode is engine-global. |
| `firebird.msg` | **Eliminate** (driver maps errors) | ❌ **kept** — still copied to `_fb/`. `firebirdInterpretError` covers OES-surfaced GDS codes but the runtime still references the catalogue. |

Net visible-to-user FB footprint: zero files outside `_fb/`. Inside
`_fb/`, the config/message files remain.

### Runtime log redirection

FB writes `firebird.log` next to `fbclient.dll` by default. Driver
sets `FIREBIRD_LOG` env var to `%LOCALAPPDATA%\OES\logs\firebird.log`
on startup. Lock files (`fb_lock_*` in `%TEMP%`) auto-managed by
FB; invisible to user.

### Driver bootstrap

The real bootstrap is `ibFirebirdBootstrap`
(`firebirdBootstrap.{h,cpp}`), idempotent, called once before the
first `fbclient.dll` symbol resolves. The sketch below shows the
shape; the live code computes `_fb/` relative to the exe and sets
`FIREBIRD` + `FIREBIRD_LOG`.

```cpp
// shape — see ibFirebirdBootstrap for the live version
void ibFirebirdBootstrap::EnsureRuntimeBootstrapped() {
    if (g_bootstrapped.exchange(true)) return;

    // Tell Windows where to find fbclient.dll dependencies.
    wxFileName fbDir(wxStandardPaths::Get().GetExecutablePath());
    fbDir.AppendDir("_fb");
    SetDllDirectory(fbDir.GetFullPath().t_str());

    // Redirect FB log to user-local path.
    wxString logDir = wxStandardPaths::Get().GetUserLocalDataDir() + "\\logs";
    wxMkdir(logDir);
    SetEnvironmentVariable(L"FIREBIRD_LOG",
                           wxString(logDir + "\\firebird.log").wc_str());

    // FB itself looks at FIREBIRD env var for plugin/msg root.
    SetEnvironmentVariable(L"FIREBIRD", fbDir.GetFullPath().wc_str());
}
```

Called once at process start, before any `fbclient.dll` symbol is
resolved.

### DPB-based connection settings

The live driver builds the DPB **inline** in
`ibDatabaseLayerFirebird::Open` (a raw `std::vector<char>` of DPB tags,
not an `ibDPB` helper type) — it pushes `isc_dpb_set_db_charset` +
`isc_dpb_lc_ctype` (both UTF8), `isc_dpb_force_write`,
`isc_dpb_session_time_zone` (UTC), `isc_dpb_page_size` (16384, CREATE
only), `isc_dpb_num_buffers`, and `isc_dpb_parallel_workers` (locally
`#define`d to 100 — the vendored `consts_pub.h` is FB-4-era). The split
sketch below is illustrative, not the actual code shape:

```cpp
// DESIGN SKETCH — actual DPB is built inline in Open(), not via ibDPB
ibDPB BuildAttachDpb(const Config& cfg) {
    ibDPB dpb;
    dpb.set(isc_dpb_lc_ctype,             "UTF8");
    dpb.set(isc_dpb_session_time_zone,    "UTC");
    dpb.set(isc_dpb_num_buffers,          cfg.cachePages);   // default 2048
    dpb.set(isc_dpb_parallel_workers,     cfg.parallelWorkers); // FB 5
    return dpb;
}

ibDPB BuildCreateDpb(const Config& cfg) {
    ibDPB dpb = BuildAttachDpb(cfg);
    dpb.set(isc_dpb_set_db_charset,       "UTF8");
    dpb.set(isc_dpb_page_size,            16384);
    dpb.set(isc_dpb_force_write,          0);     // async writes on CREATE
    return dpb;
}
```

No external configuration. Driver owns the policy.

---

## Bitness handling

### The constraint

OES default build is `Debug|x86` per project convention
(`feedback_default_build_platform.md`). Production builds typically
also x86 for compatibility breadth. This means:

- `enterprise.exe` x86 → loads x86 `fbclient.dll` → in-process FB
  engine runs in x86 address space → ~3 GB usable memory limit on
  Windows.
- Page cache + working memory + OES UI all compete for that ~3 GB.

For mesh archive-node — where one machine holds the full database,
serves heavy reports, and consumes replication logs from all peers
— in-process x86 FB easily hits this ceiling. The local-server
mode below decouples FB engine memory from the OES process limit.

### Solution: optional out-of-process FB server

Driver supports two FB hosting modes:

**Embedded mode** (default — current behaviour):

```
enterprise.exe (x86 or x64)
   └── loads _fb/fbclient.dll (matching bitness)
         └── loads engine13.dll plugin in-process
               └── opens db.fdb directly
```

Bitness of `enterprise.exe` = bitness of FB engine = same memory
space.

**Local-server mode** (new):

```
enterprise.exe (any bitness, e.g., x86)
   └── on first start, spawns child process: _fb/firebird.exe x64
   │     (separate process, own ~64GB address space, own cache)
   └── connects via TCP to localhost:<auto-port>
```

FB engine runs in its own x64 process with its own memory budget,
independent of OES process bitness.

### When to use which

Driver decides per `mesh.conf` setting `FBHost = Embedded | LocalServer`:

| Scenario | Recommended | Reason |
|---|---|---|
| Single user | Embedded | No process overhead, fastest |
| RDP / terminal server | Embedded (FB Classic) | FB lock table handles multi-process on one box natively |
| Mesh peer (not archive) | Embedded | Light workload, ~5-15 GB working set |
| Mesh archive-node | **LocalServer (x64)** | Heavy reports + full data + N inbound replication streams |
| Leader-election leader | Embedded (or LocalServer if x86 OES) | Up to 30 TCP clients fit in 3 GB on x86; x64 OES has no issue at all |

### Implementation

```cpp
ibDatabaseLayerFirebird::Open(connStr) {
    if (config.FBHost == LocalServer) {
        EnsureLocalFbServerStarted();  // idempotent
        InitTcpClientMode("localhost",
                          config.LocalServerPort);
    } else {
        InitEmbeddedMode(connStr);
    }
}

void ibDatabaseLayerFirebird::EnsureLocalFbServerStarted() {
    // Lazy spawn child process. Held until enterprise.exe exits.
    // PID + port recorded in lock file for crash cleanup.
}
```

Requires `firebird.exe` (x64) to be added to `_fb/` for users who
need this mode. Not required for embedded-only deployments — keeps
the default install lightweight.

---

## Firebird version

### Minimum: Firebird 4.0

FB 4 introduced:
- Built-in replication framework (first-class engine feature).
- DECFLOAT(16/34), INT128 — needed for clean `ibNumber` mapping.
- Time zones in TIMESTAMP / TIME.
- Read consistency isolation level.
- Long object names (63 chars).

Without FB 4, the mesh design requires external replication tooling
which adds another moving part and isn't supported through the
plugin interface.

### Strongly preferred: Firebird 5.0

FB 5 adds:
- **ParallelWorkers per attachment** — multi-core execution for
  the specific ops FB 5 parallelises: sweep, backup/restore,
  CREATE INDEX / ALTER INDEX ACTIVE / index rebuild. Faster sweep
  shrinks the page-cache thrash window on the leader; faster
  backup/restore fits the off-hours maintenance window better;
  faster index DDL helps designer deploys. Regular SELECT / DML
  is **not** parallelised in FB 5 (planned for FB 6), so per-form
  OLTP latency is unchanged.
- **Per-attachment `isc_dpb_parallel_workers`** — letting driver
  override defaults without `firebird.conf`. Required to actually
  reach the runtime; the vendored `consts_pub.h` is FB-4-era, so
  we define the tag locally next to the use site (see bug 10 in
  the regression list).
- **Connection idle timeout** — dead peers cleaned up automatically.
- **Statement-level cancel** — abort stuck replication-apply or
  runaway report.
- **Improved replication log format** — less overhead per row.
- **Dynamic replication reconfigure** — change subscriptions
  without restarting FB.
- **Schema-level routines (packages)** — modular SP organisation.

The vendored FB libraries are already 5.0
(`backend/databaseLayer/firebird/engine/dll/firebird.conf` header:
"Firebird version 5.0 configuration file"). No additional version
upgrade required.

### Vendored library inventory

Currently in tree under
`backend/databaseLayer/firebird/engine/`:

```
dll/
├── fbclient_{x64,x86}.dll          Y-valve client
├── firebird_{x64,x86}.exe          out-of-process server (local-server / leader mode)
├── ib_util_{x64,x86}.dll           BLOB filter / UDF runtime
├── icudt63_{x64,x86}.dll           ICU char-class data shim
├── icuin63_{x64,x86}.dll           ICU i18n
├── icuuc63_{x64,x86}.dll           ICU common
├── icudt63l.dat                    ICU data (27 MB)
├── intl/
│   ├── fbintl.conf                 INTL config
│   └── fbintl_{x64,x86}.dll        INTL drivers
├── plugins/
│   ├── engine13_{x64,x86}.dll      Database engine
│   ├── srp_{x64,x86}.dll           SRP auth (TCP-incoming)
│   ├── legacy_auth_{x64,x86}.dll   Legacy_Auth (default-creds fallback)
│   ├── legacy_usermanager_{x64,x86}.dll
│   ├── chacha_{x64,x86}.dll        wire crypt
│   ├── default_profiler_{x64,x86}.dll
│   ├── fbtrace_{x64,x86}.dll
│   ├── udr_engine_{x64,x86}.dll + udr_engine.conf
├── security5.fdb                   security database (pre-baked)
├── firebird.conf                   ServerMode = SuperClassic (OES override)
└── firebird.msg                    FB error-message catalogue

ibase.h                             Legacy C API (FB_API_VER 40)
iberror.h                           Error code constants
ib_util.h                           Util types
firebird/impl/                      Internal C API headers
gen/iberror.h                       Generated error codes
```

`firebird.conf` sets `ServerMode = SuperClassic` (OES override of the
FB 5 `Super` default — see `firebird-driver-hardening.md` and the
memory note `reference_fb_servermode_superclassic`: two OES processes
on one `.fdb` under `Super` would collide with `isc_io_error`).
`SuperClassic` gives the RDP / multi-process-on-one-machine scenario
for free.

**Correction (verified 2026-06-19):** `firebird.conf` and
`firebird.msg` are **NOT** eliminated. `backend.vcxproj` copies both
into `_fb/` at build time (`CopyFileToFolders` entries), alongside the
bitness-suffixed DLLs, `security5.fdb`, `firebird_{x64,x86}.exe`, and
the full `plugins/` set. The "push everything via DPB, ship zero
config files" goal in the UX section below is **aspirational**, not
the current build output.

### Already vendored (was previously listed as "to add")

`srp_{x64,x86}.dll` (TCP-incoming auth), `firebird_{x64,x86}.exe`
(local-server / leader mode), `legacy_auth_{x64,x86}.dll` (the
default-creds fallback the hardening doc relies on), and the
`replication.conf` writer (`ibFirebirdReplicationConfig`) are all in
tree. The earlier "what still needs to be added" list is superseded.

### Still genuinely missing

| Item | Purpose | Where to get |
|---|---|---|
| **OO API headers** (`firebird/Interface.h`, `firebird/Provider.h`, `firebird/Message.h`) | Modern interface-based API for replication management, parallel attachment work, transparent crypt plugin loading | From FB 5 SDK zip → `include/firebird/` |

The driver currently uses only the legacy C API (`ibase.h`,
`FB_API_VER 40`). Replication-management and parallel-attachment work
through the OO API is mesh-mode (Phase 5, deferred), so the OO headers
are not blocking anything shipped.

---

## Positioning summary

The mesh / leader driver delivers, in its sweet spot:

- **Real MVCC** in file-share deployments (no corruption under
  concurrent writes — the classic failure mode of file-mode RDBMS
  embedded scenarios).
- **Offline operation** — peers carry local working sets; a laptop
  off the network still functions for own-data writes, syncs on
  reconnect.
- **Automatic conflict resolution** via HLC last-writer-wins, with
  per-object overrides for cases where business rules require
  different policy.
- **No exclusive-lock backup** — `gbak -B` runs on archive-node
  without affecting peers; `nbackup` deltas enable incremental hot
  backup.
- **Sub-second cross-node reactivity** through `isc_que_events`
  rather than batch / cron synchronization.
- **Three deployment modes that share one driver** — embedded,
  mesh / leader, server. Transition between them is config-only,
  engine sees no difference.

### Where the mesh does NOT fit

Be honest about the boundaries:

- **>30 concurrent users.** Replication becomes O(N²). Use server
  mode (FB or PG) on dedicated hardware.
- **>100 GB total.** Archive-node CPU saturates on cross-cutting
  reports. Use server mode.
- **High-frequency transactional workloads** (trading, ticketing).
  Eventual consistency too loose. Use server mode with synchronous
  replication.
- **Strict centralized policy environments** (banks, regulated
  industries). Auto-discovery / adaptive subscription may not pass
  audit. Use server mode with explicit configuration.

In all these cases the FB driver falls back to standard
client-server, engine OES sees no difference, **migration path is
zero-code**.

---

## Implementation roadmap

### Phase 0 — already done

| Item | Status |
|---|---|
| FB 5.0 client libraries vendored | ✅ x86 + x64 in `engine/dll/` |
| `ibDatabaseLayerFirebird` embedded mode | ✅ Working |
| `ibDatabaseLayerFirebird` remote TCP mode | ✅ Working |
| `firebird-driver-hardening` patches (UTF8 DPB, lock_timeout, force_write, page_size, read_consistency) | ✅ Shipped in v1.3.0 |
| `ServerMode = SuperClassic` in `firebird.conf` (covers RDP + two-OES-on-one-`.fdb`) | ✅ Configured (`firebird.conf:1327`) |
| `ses_query` / `db_query` macro split | ✅ Done |
| Session registry + connection pool | ✅ Done |
| `isc_que_events` infrastructure | partial — used by debug protocol; available for metadata reload |

### Phase 1 — UX cleanup (low risk, high value) — ✅ landed

| Item | Status | Where |
|---|---|---|
| Move FB DLLs to `bin/.../_fb/` at build | ✅ landed | `backend.vcxproj` `CopyFileToFolders` → `$(OutDir)\_fb` (per-config) |
| `SetDllDirectory(_fb)` bootstrap (`ibFirebirdBootstrap`) | ✅ landed | `firebird/firebirdBootstrap.{h,cpp}` — computes `_fb/` relative to the exe; also `SetEnv FIREBIRD` / `FIREBIRD_LOG` |
| DPB-side connection settings in driver | ✅ landed | `ibDatabaseLayerFirebird::Open` builds the DPB inline (UTF8 `lc_ctype` + `set_db_charset`, lock_timeout, force_write, page_size, read_consistency, parallel_workers) |
| Audit `firebirdInterpretError`, complete error code table | ✅ landed | covers OES-surfaced GDS codes; falls back to `isc_status` decode for the rest |
| Redirect FB log via `FIREBIRD_LOG` env var | ✅ landed | `ibFirebirdBootstrap` (also sets `FIREBIRD=<_fb dir>`) |
| ~~Delete `firebird.conf` / `firebird.msg` from build output~~ | ❌ **NOT done** | both are still `CopyFileToFolders`-copied into `_fb/` by `backend.vcxproj` (lines 783, 827). `firebird.conf` is in fact load-bearing — it carries `ServerMode = SuperClassic`, which a pure-DPB attach cannot set. Treat this row as a **dropped goal**, not a landed item. |

After Phase 1 the FB runtime lives in a single `_fb/` subfolder rather
than sprawled next to the exes. It is **not** file-free: `_fb/` still
contains `firebird.conf`, `firebird.msg`, `security5.fdb`, the
bitness-suffixed DLLs, `firebird_{x64,x86}.exe`, and `plugins/`. The
"zero config files" target in the UX section is aspirational.

### Phase 2 — BLOB compression (low risk, high value) — ✅ landed

| Item | Status | Where |
|---|---|---|
| Vendor zlib (already in tree) | ✅ landed | via `wx/zstream.h` (`wxZlibInputStream`/`OutputStream`) |
| `ibDatatabaseParameterFirebirdCollection::SetParamBlob` — compress > 512 B | ✅ landed | `ibFirebirdBlobCompression::CompressIfWorthwhile` (threshold = 512 B) |
| `ibDatabaseResultSetFirebird::GetResultBlob` — decompress | ✅ landed | `ibFirebirdBlobCompression::DecompressIfNeeded` |
| Magic byte (`OESC` header) for forward compatibility | ✅ landed | uncompressed → no header (legacy DBs read transparently); compressed → 4-byte magic + uncompressed size |
| Unit tests: round-trip large TEXT, mixed-content BLOBs, edge sizes | ✅ landed | `tests/test_blobCompression.cpp` — 16 round-trip tests |

Effect: documents with descriptions / comments / attached files
shrink 50-80% on disk. Database growth slowed proportionally.
Legacy uncompressed BLOBs in existing DBs continue to read without
migration — the magic-byte gate is one-way.

### Phase 3 — scheduled maintenance — partial

| Item | Status | Where |
|---|---|---|
| Services-API `gbak` BR cycle | ✅ landed | `ibFirebirdMaintenance::RunBackupRestoreCycle` (uses `isc_service_attach` + `isc_service_start`) |
| Adaptive sweep via `MON$DATABASE` | ✅ landed | `ibFirebirdMaintenance::AdaptiveSweep` (triggers when OAT/OST gap > threshold) |
| `ibFirebirdMaintenanceScheduler` background job (weekly cron) | ⏳ pending | scaffolding exists (`firebirdMaintenanceScheduler.{h,cpp}`) but not wired to scheduler |
| UI surface in Designer: "DB size X, last maintenance Y" | ⏳ pending | |

Effect once fully landed: file size stays bounded over time without
manual ops attention. Current state: the maintenance primitives are
available for manual invocation, but the scheduler that calls them
automatically is still to be wired.

### Phase 4 — leader-election mode — ✅ foundations + driver wiring landed

| Item | Status | Where |
|---|---|---|
| `ibFirebirdLease` — SMB byte-range lock + heartbeat | ✅ landed | `firebirdLease.{h,cpp}` — fixed binary layout (`magic 'OESL'`, `version`, `generation`, `heartbeat_unix_ms`, leader host/port, leader pid); sentinel-byte `LockFileEx` at offset `kLockSentinelOffset = 200` (`#ifdef __WXMSW__`) / `fcntl F_SETLK` (POSIX) |
| HLC clock — `ibHlcClock` | ✅ landed | `firebirdHlc.{h,cpp}` — `[millis 48][counter 12][nodeId 4]` |
| `ibFirebirdLeaderMode` — leader / follower orchestrator | ✅ landed | `firebirdLeaderMode.{h,cpp}` — heartbeat thread, lease renewal, generation-bump watch |
| Driver-side wiring in `ibDatabaseLayerFirebird::Open` | ✅ landed | Calls `InitForDatabase` on every open; uses returned role-aware `connectUrl` (`inet://localhost:<port>/<path>` for self-leader, `inet://leader-host:<port>/<path>` for follower) instead of bare file path |
| FB 4+ replication.conf writer — `ibFirebirdReplicationConfig` | ✅ landed | `firebirdReplicationConfig.{h,cpp}` |
| Generation-bump → URL refresh on follower | ✅ landed | `HeartbeatLoop` detects generation change, rewrites `connectUrl` so next `CurrentConnectUrl()` sees the new leader |
| Failover — heartbeat-timeout → self-promote | ✅ landed | `HeartbeatLoop` checks `ibFirebirdLease::kHeartbeatStaleMs` (**20 s**) → `PromoteSelfToLeader` tries `TryAcquireExclusive`; on win, spawns local server + `WriteSelfAsLeader`; on race-loss, refreshes cached URL from the winner |
| Driver reconnect-on-leader-handoff | ✅ landed (proactive at every query boundary) | `ibDatabaseLayerFirebird::ReconnectIfLeaderChanged` runs at the top of `DoBeginTransaction`, `DoRunQuery`, `DoRunQueryWithResults`, **and** `DoPrepareStatement` (all four call sites verified) — cached `m_currentConnectUrl` vs `ibFirebirdLeaderMode::CurrentConnectUrl()` compare; on mismatch closes the FB handle and re-`Open`s against the new URL. Mid-TX connection loss still surfaces as a regular exception; the *next* boundary triggers the reconnect. Skipped for remote (`server:db`) mode — no leader-mode there. |
| Optional local-cache mode — leader copies .fdb to local disk, syncs via nbackup | ⏳ pending (separate roadmap item) |
| Lease-level unit tests | ✅ landed | `tests/test_firebirdLease.cpp` — 8 tests covering acquire / re-acquire / write/read state round-trip / generation bump / heartbeat refresh / non-holder read / handoff generation persistence / staleness invariant |
| Orchestrator-level 2-node integration test (self-promote loop) | ⏳ pending | Requires injectable clock or a >20 s real-time wait (the `kHeartbeatStaleMs` window); deferred. Lease-level coverage above is the foundation any orchestrator test would build on. |

The lease + HLC + leader orchestrator + replication-config writer
are all in tree and unit-tested in isolation. End-to-end
follower-side wiring and 2-node integration testing is what's
left.

### Phase 5 — mesh mode — DEFERRED INDEFINITELY (2026-05-23)

**Decision:** mesh-mode (each peer with a local .fdb replica + FB
native replication shipping deltas) is **strictly redundant** for
our stated sweet spot (3-30 users on one LAN, ≤100 GB). Phase 4
leader-election + Phase 6 local server cover this scenario
end-to-end (production-validated). Mesh would only win for:

- **Multi-office WAN** (peers across cities, no shared folder
  reachable from all sites)
- **Geographic read distribution** (>50 readers needing local-fast
  reads, can't tolerate TCP roundtrip to a leader)
- **Zero-downtime requirement** (60-second handoff unacceptable —
  banking core, exchange platform)

These are **a different market segment**, not our target. For
multi-office the natural answer is PG/MSSQL master-master
replication (decades-mature, supported), not rolling our own mesh
on top of FB.

Mesh foundations already landed (`firebirdHlc`,
`firebirdReplicationConfig`) stay in tree as a skeleton — if a
pilot customer ever shows up with a genuine mesh need, the
primitives are ready. Active development = STOP.

Remaining ~100 hr (mDNS, query router, conflict resolver, DDL
2-phase, 3-node tests) — not scheduled.

### Phase 6 — local FB server mode (out-of-process, bitness-decoupling) — ✅ landed (opt-in)

| Item | Status | Where |
|---|---|---|
| Compile-time gate `OES_FB_LOCALSERVER` | ✅ landed | `firebirdLocalServer.cpp` — OFF by default, header API stays for callers; methods stub to 0 / "" when flag undefined |
| CMake option `OES_FB_LOCALSERVER` | ✅ landed | `CMakeLists.txt` — adds `add_compile_definitions(OES_FB_LOCALSERVER)` inside the FB block |
| Spawn child process on demand | ✅ landed | `firebirdLocalServer.{h,cpp}` — `wxExecute` (Win32 path; POSIX path stubbed) |
| Auto-port selection | ✅ landed | `firebirdLocalServer::PickFreePort` (bind-to-0 + getsockname) |
| Pid file + lock | ✅ landed | `%TEMP%/oes-fb-localserver.pid` with pid/port/parent fields |
| Graceful shutdown + PID cleanup | ✅ landed | atexit hook → `wxSIGTERM` (2 s grace) → `wxSIGKILL` |
| Config switch — `OES_FB_LOCAL_SERVER` env or per-DB flag | ✅ landed | activation hook in `ibFirebirdLeaderMode`; spawn via `ibFirebirdLocalServer::EnsureStarted` |
| Bundle `firebird.exe` under `_fb/` | ⚠️ opt-in by operator | binary **not** vendored; operator drops it in when flag is ON. Driver surfaces clear error if missing. |

**Why opt-in.** Embedded / leader-election / mesh modes don't need
the out-of-process server — they attach via `fbclient.dll` directly.
The local-server path is justified **only** when x86 OES must drive
an x64 FB engine (working sets that overflow the 3 GB user-mode
address space — the archive-node case). Keeping it gated saves
~5 MB on the distributable and excludes `<windows.h>` / winsock /
`wxProcess` from default builds.

Callers (`ibFirebirdLeaderMode`) already handle
`ibFirebirdLocalServer::EnsureStarted() == 0` as graceful
degrade-to-embedded — no `#ifdef` leaks past the translation unit
boundary.

**Not** for scaling FB past 30 users — that scenario graduates to
PG/MSSQL via a manual `gbak` export + schema rebuild (Phase 7 tooling
is reverted; see below).

### Phase 7 — RDBMS migration tooling (graduation path) — REVERTED

> **Reverted 2026-05-23, off-priority (focus is shara).** An earlier
> draft of this table marked an `ibSqlDialect` helper + upsert/CREATE-
> INDEX sweep as "landed". That work was **reverted** — verified
> 2026-06-19: `sqlDialect.{h,cpp}` does not exist, and none of
> `BuildUpsert` / `BuildUpsertDynamic` / `BuildCreateIndex` /
> `MakeExcludedAssignList` are in the tree. Treat the dialect layer as
> **not started**. The cross-driver SQL-portability analysis under
> "SQL portability across drivers" above is still the design of record
> for when this resumes.

Nothing in this phase is currently shipped. When migration tooling
resumes, the build order is: dialect-emission helper → sweep
hand-written SQL through it (upsert ladders, `CREATE INDEX`) → schema
converter (OES metadata → PG / MSSQL DDL) → bulk exporter / importer →
sanity-check tool → `connect.cfg` switch → end-to-end smoke test.
Until then, graduation past the 30-user / 100 GB boundary is a manual
`gbak` export + schema rebuild, not an automated path.

### Estimated remaining effort

| Phase | Status | Remaining |
|---|---|---|
| 1 — UX cleanup | ✅ landed | — |
| 2 — BLOB compression | ✅ landed | — |
| 3 — Scheduled maintenance | partial | ~3-4 hr (Designer maintenance-status UI panel) |
| 4 — Leader-election | ✅ production-validated | ~8-10 hr (orchestrator-level 2-node integration test with injectable clock; nice-to-have) |
| 5 — Mesh mode | **DEFERRED** | — (not scheduled; see "Phase 5 DEFERRED" above) |
| 6 — Local FB server | ✅ landed | — |
| 7 — RDBMS migration tooling | **REVERTED** | — (off-priority, focus is shara; dialect work removed) |

**Current production-ready scope:** Phases 1, 2, 4, 6. This covers
the entire stated sweet spot — single-user local + multi-machine
shared folder + RDP/terminal-server. Anything else (multi-office WAN,
heavy read-distribution) → graduate to PG/MSSQL via manual export +
schema rebuild (no tooling automation right now since Phase 7 is
off-priority).

---

## Open questions

### Replication log retention

How long do we keep the FB replication log on each peer before
truncation? Trade-off:
- Longer log = peers can recover from longer offline periods.
- Shorter log = less disk usage.

Default: 7 days. Configurable per `mesh.conf`. Peer offline >7
days → falls back to full nbackup re-sync.

### HLC drift bound

HLC requires loosely-synced clocks. If two peers' wall clocks
diverge by hours, HLC values become inconsistent (one peer's "now"
is in the future relative to another's). NTP sync to the LAN's
gateway is sufficient (±100 ms typical). On Windows joined to a
domain, the domain controller's clock service is enough.

What if NTP isn't available? Driver detects clock skew > 30 sec
between peers (via heartbeat-attached timestamps) → surface warning
to admin: "peer X clock skew is dangerous, results may be
inconsistent."

### Designer multi-deploy collision

If two designers simultaneously click Save:
- Both try to acquire the deploy lease.
- One wins, the other gets "deploy in progress, retry."
- After winner releases lease, runner-up retries.

This is acceptable for a designer tool — schema deploys aren't
"fire and forget." Designer surfaces a clear message, user clicks
Retry, second deploy applies on top of first. Engine handles cascading
DDL safely (FB transactions are atomic per statement).

### Selective replication audit

Some industries require audit logging that **all** writes are
preserved everywhere. Selective replication breaks this — Document
X written on peer A may not exist on peer C unless C subscribes.

Mitigation: in `mesh.conf` per-user setting `audit_subscribe =
true` for the auditor role → that user's node gets full
replication. Audit log queries always route to that node (or to
archive-node, which also has full replication by definition).

### When does growth force a server-mode migration?

Driver should advise the user **before** the boundary is hit, not
after. Heuristic:

```
if (database_size_gb > 80 || active_users > 25 ||
    replication_lag_ms_p95 > 1500) {
    SurfaceWarningInDesigner(
        "Database is approaching the limits of mesh deployment. "
        "Consider migrating to server mode within the next 6 months.");
}
```

Migration tool: `gbak -B` on archive-node, ship `.fbk` to dedicated
server, set up FB 5 as service, point client `mesh.conf` to
`FBHost = Remote, RemoteHost = <server>`. Engine OES sees the
same database. Mesh metadata config archived for posterity.

### What if archive-node dies?

In mesh mode, archive-node holds the master copy of full data. If
it dies:
1. Other peers continue operating on their working sets.
2. Read misses (not in working set) fail with "archive unreachable."
3. Driver attempts to elect a new archive-node from peers that
   have full subscription (e.g., the role-tagged auditor node).
4. If no peer has full data, manual recovery — restore archive
   from `nbackup` snapshot.

Mitigation: run two archive-nodes in mesh, both subscribed to
everything. They replicate to each other like ordinary peers. Loss
of one is transparent.

---

## References

- [`firebird-driver-hardening.md`](firebird-driver-hardening.md) —
  FB 5 DPB / TPB settings already applied (UTF8, lock_timeout,
  force_write, page_size, read_consistency).
- [`connection-pool.md`](connection-pool.md) — pool architecture,
  `ses_query` / `db_query` macros, scope-based TX.
- [`session-registry.md`](session-registry.md) — session lifecycle,
  3-phase `NotifyAuthenticated`, admin signal channel.
- [`runtime-facade.md`](runtime-facade.md) — per-session module
  manager root, why descriptor lock is the current concurrency
  ceiling (relevant for query routing in mesh).
- [`compute-server-tiering.md`](compute-server-tiering.md) — the
  longer-term 3-tier compute-server vision; mesh driver is one
  building block toward that direction.
- [`paging-design.md`](paging-design.md) — `Get*Fetch` data
  paging; some queries here become network-aware in mesh mode.

External:
- Firebird 5.0 documentation —
  https://firebirdsql.org/file/documentation/release_notes/Firebird-5.0.0-ReleaseNotes.pdf
- Firebird replication setup guide —
  `doc/README.replication` in the FB SDK zip.
