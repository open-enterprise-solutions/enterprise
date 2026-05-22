# Firebird mesh driver — distributed deployment for OES

Design plan for `ibFirebirdDatabaseLayer` as the active coordination
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

> **Status:** design, not implemented. FB 5.0 client libraries are
> already vendored in `src/3rdparty`-equivalent path
> (`backend/databaseLayer/firebird/engine/`). Driver-side code lives
> in `firebirdDatabaseLayer.cpp` and currently implements the
> embedded / remote modes; the mesh / leader / adaptive-subscription
> paths described here are to be added.

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
inside `ibFirebirdDatabaseLayer`. The OES engine sees only the
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

| Concern | Where it's implemented in driver |
|---|---|
| Connection mode selection | `firebirdDatabaseLayer.cpp::Open` — picks embedded / local-server / TCP-remote / mesh-peer / leader from config |
| Peer discovery | `mesh/discovery/` — mDNS on LAN, manifest on shared NAS |
| Lease coordination | `mesh/lease/` — SMB byte-range lock on `.lease` sidecar |
| Replication shipping | `mesh/replication/` — FB log producer/consumer per peer |
| Adaptive subscription | `mesh/subscription/` — per-table policy + on-demand fetch + idle-demote |
| HLC clock | `mesh/hlc/` — hybrid logical clock + per-session read-your-writes barrier |
| DDL 2-phase deploy | `mesh/coordinator/` — `isc_que_events` broadcast + ACK protocol |
| Query routing | `mesh/routing/` — local vs peer vs archive, per-query decision |
| Failover | shared across the above — lease timeout, replication catch-up via nbackup |
| Compression of BLOB/TEXT | `firebirdParameterCollection.cpp` — zstd in `SetParamBlob` / `GetResultBlob` (transparent) |
| Scheduled backup-restore | `mesh/maintenance/` — driver schedules `gbak -B; gbak -R` weekly |

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

`ibPostgresDatabaseLayer`, `ibMysqlDatabaseLayer`,
`ibOdbcDatabaseLayer`, `ibSqliteDatabaseLayer` remain thin wrappers.
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
| Single user, desktop | **FB embedded** (`ibFirebirdDatabaseLayer`) | Works as-is via `fbclient.dll` — no coordination needed |
| RDP / terminal server (10-30 ops on one box) | **FB Classic + lock table** (`ibFirebirdDatabaseLayer`) | FB's own multi-process coordination via `%TEMP%/fb_lock_*` — works as-is |
| Small office / clinic / shop, shared LAN, **3-30 users, <100 GB** | **FB mesh or leader-election** (`ibFirebirdDatabaseLayer`) | **The design target of this document** |
| 30+ users **or** >100 GB | **PostgreSQL** (`ibPostgresDatabaseLayer`) **or MSSQL** (`ibOdbcDatabaseLayer`) | Out of FB driver scope. PG / MSSQL are the right tool here. Migration requires data export from `.fdb` → import to PG/MSSQL schema. |

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
5. Restart OES instances — they pick up `ibPostgresDatabaseLayer`
   or `ibOdbcDatabaseLayer` automatically based on the connection
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
ibFirebirdDatabaseLayer::Open(connStr) {
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
// Driver pseudo-code in firebirdDatabaseLayer::FetchRow
ibValue ibFirebirdDatabaseLayer::FetchRow(tableId, rowId) {
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
order. For human-readable sequential numbers (`Накладная №1234`):
range allocation per peer — peer A gets 1-10000, B gets 10001-20000.
Numbers unique but not sequentially consecutive across peers. Period
close runs a re-numbering pass on archive-node for users who want
consecutive output.

### Read-your-writes consistency

After a write, subsequent reads from the same session must see
their own write. Driver maintains per-session `last_write_hlc`:

```cpp
ibResultSet* ibFirebirdDatabaseLayer::RunQueryWithResults(...) {
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
ibLeaseAcquireResult ibLeaseFile::TryAcquireExclusive() {
    HANDLE h = CreateFileW(L"db.fdb.lease", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ, ...);
    OVERLAPPED ov = {};
    if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                   0, MAXDWORD, MAXDWORD, &ov)) {
        // We are the leader.
        return Acquired;
    }
    return AnotherLeaderActive;
}
```

SMB byte-range locks **do** propagate between machines correctly —
this is the same primitive that file-mode coordination historically
relies on, but applied here only to leadership election — not to
data-page synchronisation.

### Lifecycle

```
Process startup:
  1. TryAcquireExclusive on db.fdb.lease.
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

#### 1. BLOB/TEXT compression in `SetParamBlob` / `GetResultBlob`

```cpp
void ibParameterCollection::SetParamBlob(int idx, const void* data, size_t size) {
    if (size >= kCompressThreshold) {       // 512 bytes
        auto compressed = zstd::Compress(data, size);
        if (compressed.size() < size * 0.85) {
            // Worth compressing.
            std::vector<uint8_t> wrapped;
            wrapped.push_back(0x01);  // marker: zstd-compressed
            wrapped.insert(wrapped.end(),
                           compressed.begin(), compressed.end());
            SetRawBlob(idx, wrapped.data(), wrapped.size());
            return;
        }
    }
    // Raw — prefix 0x00 marker for symmetry on read path.
    std::vector<uint8_t> wrapped;
    wrapped.push_back(0x00);
    wrapped.insert(wrapped.end(), (uint8_t*)data, (uint8_t*)data + size);
    SetRawBlob(idx, wrapped.data(), wrapped.size());
}

std::vector<uint8_t> ibResultSet::GetResultBlob(int idx) {
    auto raw = GetRawBlob(idx);
    if (raw.empty()) return {};
    if (raw[0] == 0x01) {
        return zstd::Decompress(raw.data() + 1, raw.size() - 1);
    }
    return std::vector<uint8_t>(raw.begin() + 1, raw.end());
}
```

Effect: BLOB columns with text content (Document descriptions,
DataProcessor body, attached files) shrink by 50-80% on disk.
Engine OES knows nothing about compression — `SetParamBlob` /
`GetResultBlob` keep their wire-level semantics.

Cost: zstd encode/decode on every BLOB I/O — single-digit
microseconds for typical sizes. Negligible.

#### 2. Scheduled backup-restore cycle

Driver schedules `gbak -B; gbak -R` weekly during off-hours:

```cpp
class ibMaintenanceScheduler {
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
configuration files visible, no `firebird.conf` to edit, no
plumbing.**

### Target directory layout

```
C:\Programs\OES\
├── enterprise.exe          ← launches
├── designer.exe
├── backend.dll
├── frontend.dll
├── wfrontend.dll
└── _fb\                    ← single hidden folder, user never opens
    ├── fbclient.dll
    ├── ib_util.dll
    ├── icudt63.dll, icuin63.dll, icuuc63.dll
    ├── icudt63l.dat        (27 MB ICU data)
    ├── plugins\
    │   └── engine13.dll
    └── intl\
        ├── fbintl.dll
        └── fbintl.conf

C:\Users\Иванов\Documents\МояБухгалтерия\    ← user-chosen
└── sys.fdb
└── mesh.conf               (only if multi-user share scenario)
└── sys.fdb.lease           (only if leader-election scenario)
```

### What gets eliminated

| File | Currently | After cleanup | How |
|---|---|---|---|
| `firebird.conf` | ~43 KB next to fbclient | **Gone** | All settings passed via DPB on `isc_attach_database`. FB 5 supports per-attachment `parallel_workers`, `lc_ctype`, `session_time_zone`, `force_write`, `page_size`, etc. |
| `firebird.msg` | ~145 KB | **Gone** | FB error codes mapped through `firebirdInterpretError` (already exists in driver); error text in OES's own translation table (Russian + English). |
| FB DLLs sprawled next to `.exe` | 15 files | **Hidden in `_fb/`** | `Common.props` updates `<PostBuildEvent>` to copy into subfolder. `SetDllDirectory(L"_fb")` in `appData::Init`. |

### What stays (FB fundamental requirements)

| File | Why | Hidden? |
|---|---|---|
| `fbclient.dll` | Y-valve entry point | yes, in `_fb/` |
| `plugins/engine13.dll` | Pluggable engine architecture; not statically linkable | yes |
| `icudt63l.dat` (27 MB) | UTF-8 collation data; mandatory for non-ASCII content | yes |
| `icudt63 / icuin63 / icuuc63.dll` | ICU runtime | yes |
| `ib_util.dll` | BLOB filter runtime | yes |
| `intl/fbintl.{dll,conf}` | INTL collations (UNICODE_CI, RUSSIAN, etc.) | yes (one tiny `.conf`, but inside `_fb/intl/`, not next to exe) |

Net visible-to-user FB footprint: zero files outside `_fb/`.

### Runtime log redirection

FB writes `firebird.log` next to `fbclient.dll` by default. Driver
sets `FIREBIRD_LOG` env var to `%LOCALAPPDATA%\OES\logs\firebird.log`
on startup. Lock files (`fb_lock_*` in `%TEMP%`) auto-managed by
FB; invisible to user.

### Driver bootstrap

```cpp
void ibFirebirdDatabaseLayer::EnsureRuntimeBootstrapped() {
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

```cpp
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
ibFirebirdDatabaseLayer::Open(connStr) {
    if (config.FBHost == LocalServer) {
        EnsureLocalFbServerStarted();  // idempotent
        InitTcpClientMode("localhost",
                          config.LocalServerPort);
    } else {
        InitEmbeddedMode(connStr);
    }
}

void ibFirebirdDatabaseLayer::EnsureLocalFbServerStarted() {
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
- **ParallelWorkers** per statement — multi-core query execution.
  Reports run 4-8× faster on archive-node.
- **Per-attachment `isc_dpb_parallel_workers`** — letting driver
  override defaults without `firebird.conf`.
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
├── ib_util_{x64,x86}.dll           BLOB filter / UDF runtime
├── icudt63_{x64,x86}.dll           ICU char-class data shim
├── icuin63_{x64,x86}.dll           ICU i18n
├── icuuc63_{x64,x86}.dll           ICU common
├── icudt63l.dat                    ICU data (27 MB)
├── intl/
│   ├── fbintl.conf                 INTL config
│   └── fbintl_{x64,x86}.dll        INTL drivers
├── plugins/
│   └── engine13_{x64,x86}.dll      Database engine
├── firebird.conf                   (to be eliminated — driver pushes via DPB)
└── firebird.msg                    (to be eliminated — driver maps errors)

ibase.h                             Legacy C API (FB_API_VER 40)
iberror.h                           Error code constants
ib_util.h                           Util types
firebird/impl/                      Internal C API headers
gen/iberror.h                       Generated error codes
```

`firebird.conf` has `ServerMode = Classic` set, which gives the
RDP / multi-process-on-one-machine scenario for free without our
intervention.

### What still needs to be added

| Item | Purpose | Where to get |
|---|---|---|
| **OO API headers** (`firebird/Interface.h`, `firebird/Provider.h`, `firebird/Message.h`) | Modern interface-based API for replication management, parallel attachment work, transparent crypt plugin loading | From FB 5 SDK zip → `include/firebird/` |
| **`Srp.dll` auth plugin** | Required for TCP-incoming auth (leader-mode listener, mesh peer-to-peer) | From FB 5 SDK zip → `plugins/` |
| **`firebird.exe`** (optional) | For local-server mode (bitness decoupling) | From FB 5 SDK zip → root |
| **`replication.conf`** template | FB 4+ replication framework requires this file | Generated by driver on first run; check-in shape skeleton for reference |

`Legacy_Auth.dll` is **not** required for our use cases — embedded
mode bypasses auth, and TCP-incoming should always use SRP. Skip.

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
| `firebirdDatabaseLayer` embedded mode | ✅ Working |
| `firebirdDatabaseLayer` remote TCP mode | ✅ Working |
| `firebird-driver-hardening` patches (UTF8 DPB, lock_timeout, force_write, page_size, read_consistency) | ✅ Shipped in v1.3.0 |
| `ServerMode = Classic` in `firebird.conf` (covers RDP) | ✅ Configured |
| `ses_query` / `db_query` macro split | ✅ Done |
| Session registry + connection pool | ✅ Done |
| `isc_que_events` infrastructure | partial — used by debug protocol; available for metadata reload |

### Phase 1 — UX cleanup (low risk, high value)

| Item | Effort | Value |
|---|---|---|
| Move FB DLLs from `bin/.../` to `bin/.../_fb/` via `Common.props` | 30 min | User sees one hidden folder instead of 15 files |
| `SetDllDirectory(L"_fb")` bootstrap in `appData::Init` | 15 min | DLL loading works from subfolder |
| Move all settings from `firebird.conf` to DPB in driver | 1-2 hr | `firebird.conf` no longer needed; runtime self-configures |
| Delete `firebird.conf` from vendored + from build output | 5 min | Less visible plumbing |
| Audit `firebirdInterpretError`, complete error code table | 2-3 hr | `firebird.msg` no longer needed; OES translates errors itself |
| Delete `firebird.msg` from vendored + from build output | 5 min | Less visible plumbing |
| Redirect FB log via `FIREBIRD_LOG` env var | 30 min | No `firebird.log` next to exe |

After Phase 1, the user-visible footprint is: `enterprise.exe`,
`backend.dll`, `frontend.dll`, `wfrontend.dll`, `_fb/` hidden
folder, user-chosen database folder. **No FB-plumbing files visible
anywhere.**

### Phase 2 — BLOB compression (low risk, high value)

| Item | Effort | Value |
|---|---|---|
| Vendor zstd (header-only or small lib) | 30 min | Compression library available |
| `ibParameterCollection::SetParamBlob` — zstd compress > 512 B | 1 hr | Transparent compression on write |
| `ibResultSet::GetResultBlob` — zstd decompress | 30 min | Transparent decompression on read |
| Marker byte (0x00 raw, 0x01 zstd) for forward compatibility | included | Allows future format changes |
| Unit tests: round-trip large TEXT, mixed-content BLOBs, edge sizes | 2 hr | Confidence |

Effect: documents with descriptions / comments / attached files
shrink 50-80% on disk. Database growth slowed proportionally.

### Phase 3 — scheduled maintenance (medium effort)

| Item | Effort | Value |
|---|---|---|
| `ibMaintenanceScheduler` background job | 4 hr | Weekly `gbak -B; gbak -R` cycle automatically |
| `MON$DATABASE` monitor → adaptive sweep | 2 hr | Auto-sweep when OAT/OST diverge |
| UI surface in Designer: "DB size X, last maintenance Y" | 2 hr | User awareness without manual checking |

Effect: file size stays bounded over time without manual ops
attention.

### Phase 4 — leader-election mode (medium-high effort)

| Item | Effort | Value |
|---|---|---|
| `ibLeaseFile` — SMB byte-range lock + heartbeat | 6 hr | Coordination primitive |
| `ibLeaderModeDriver` — open .fdb locally + spawn TCP listener | 8 hr | Leader role implementation |
| `ibFollowerModeDriver` — read lease, connect TCP, watch for handoff | 6 hr | Follower role |
| Failover logic — heartbeat-timeout → re-acquire | 4 hr | Resilience |
| Optional local-cache mode — leader copies .fdb to local disk, syncs via nbackup | 16 hr | Performance optimization for slow SMB |
| Integration tests with 2-node setup | 8 hr | Confidence |

Effect: file-share deployments work without corruption risk; one
machine acts as transparent server for the others.

### Phase 5 — mesh mode (high effort)

| Item | Effort | Value |
|---|---|---|
| `mesh.conf` parser + schema | 4 hr | Configuration |
| Peer discovery (mDNS + manifest fallback) | 12 hr | Auto-join |
| FB replication.conf generation per peer | 4 hr | Replication setup |
| `ibHlcClock` + per-session barrier | 8 hr | Causal ordering |
| Adaptive subscription engine | 24 hr | Working-set caching |
| `ibQueryRouter` — local vs peer vs archive | 12 hr | Smart routing |
| Conflict resolver (HLC last-writer-wins + per-object overrides) | 8 hr | Conflict handling |
| DDL 2-phase coordinator | 16 hr | Schema deploy across mesh |
| `isc_que_events` event broadcast | 4 hr | Sub-second signaling |
| Integration tests with 3+ node setup | 24 hr | Confidence |

Effect: full peer-to-peer mesh for the design's sweet spot.

### Phase 6 — local FB server mode (low-medium effort, narrow scope)

| Item | Effort | Value |
|---|---|---|
| Add `firebird.exe` to `_fb/` | 5 min | Server binary available |
| Spawn child process on demand | 2 hr | Lazy boot |
| Auto-port selection + lock file | 2 hr | Multi-instance safety |
| Graceful shutdown + PID cleanup | 2 hr | No zombies |
| Config switch `FBHost = LocalServer` | 30 min | Mode selection |

Effect: enables x86 OES to drive x64 FB engine for the **archive-node
case** within mesh mode — where heavy reports plus replication-log
consume need more than 3 GB address space. **Not** for scaling FB
past 30 users; that scenario migrates to PG/MSSQL via Phase 7.

### Phase 7 — RDBMS migration tooling (graduation path)

| Item | Effort | Value |
|---|---|---|
| **`ibSqlDialect` helper** — per-driver emit primitives (`Upsert`, `LimitClause`, `NextIdSql`, `ConcatOp`, `BlobType`, `BoolType`) | 16 hr | **Hard prerequisite for portability** |
| **Sweep hand-written SQL** in `appDataQuery.cpp` / `metaAttributeObjectQuery.cpp` / `metadataConfigurationQuery.cpp` / scattered `*Query.cpp` — route through `ibSqlDialect` helpers | 24 hr | Eliminate "works on FB, fails on PG" bug class |
| Schema converter — OES metadata → PG / MSSQL DDL generator | 16 hr | Recreate schema in target RDBMS faithfully |
| Bulk data exporter — `.fdb` → CSV / native bulk format per target | 24 hr | Hot/cold data dump |
| Bulk data importer — push into PG `COPY` / MSSQL `bcp` | 16 hr | Fast load (millions of rows/min) |
| Sanity-check tool — row counts, key constraints, indexes match | 8 hr | Confidence after migration |
| Connection-string switch in `connect.cfg` — runtime detect | 2 hr | Engine auto-picks new driver |
| End-to-end migration smoke test on real configurations | 16 hr | Catch edge cases (BLOB types, custom domains, etc.) |

Effect: "outgrow FB" becomes a **routine, scriptable, predictable**
operation rather than a manual data-engineering project. This is
what makes the "30 user / 100 GB hard cap" honest — there's a
defined exit ramp instead of a wall.

The first two items (`ibSqlDialect` + sweep) **pay off independently
of migration** — they eliminate dialect-leak bugs that already
surface during multi-driver development. Worth landing earlier
than the rest of Phase 7 if the team feels the pain.

### Estimated total

| Phase | Effort | Cumulative |
|---|---|---|
| 1 — UX cleanup | ~6 hr | 6 hr |
| 2 — BLOB compression | ~4 hr | 10 hr |
| 3 — Scheduled maintenance | ~8 hr | 18 hr |
| 4 — Leader-election | ~40-48 hr | 60 hr |
| 5 — Mesh mode | ~120 hr | 180 hr |
| 6 — Local FB server | ~7 hr | 187 hr |
| 7 — RDBMS migration tooling | ~120 hr | 307 hr |

Phases 1-3 are **safe early wins** — should land first.
Phase 4 (leader-election) gives the biggest UX win for shared-file
deployments. Phase 5 (mesh) is the heaviest investment and should
only follow once Phase 4 proves stable in production. Phase 7's
first two items (`ibSqlDialect` + sweep) are worth pulling forward
as a Phase 3.5 — they pay off in current multi-driver dev work, not
just in eventual migration. Rest of Phase 7 lands when the first
real customer is approaching the boundary.

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
