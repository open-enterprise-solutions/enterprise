# Register totals — trigger-maintained strategy (proposal)

Architecture proposal for OES register storage that moves the
"totals" maintenance burden from C++ application code into the
database via triggers, while preserving the same read latency
profile as the classic denormalized totals table pattern (used by
OES today, and by similar enterprise platforms). Status: **design discussed 2026-04-30, not yet
implemented.** Expected first target: PostgreSQL production, with FB
embedded sharing the same pattern at smaller scale.

> **TL;DR** — keep the totals table; let SQL triggers maintain it
> instead of OES C++. Read path goes through views, runtime is
> totals-agnostic. No periodic recalculation. No drift. Same read
> performance as the explicit-totals pattern. Slight write overhead
> (~5–15% per movement). Cross-DB via per-driver trigger templates.

---

## Why move maintenance into the database

Today OES (like other platforms with explicit `mov + totals` tables)
writes to `mov_X` (the register movements table) and then explicitly
updates `totals_X` rows from C++. Two problems compound:

1. **Drift.** If the process crashes between the `mov` insert and the
   `totals` update, or if a code path forgets to update `totals`, or
   if a backdated movement is inserted without recalculating
   subsequent periods, totals diverges from movements. The traditional
   compensation is a "totals recalculation" command — periodic full
   rebuild from movements. Operationally heavy, prone to inconsistency windows.
2. **Maintenance code in C++.** Logic for "this kind of register's
   totals update for this kind of dimension/resource" lives in
   `*Query.cpp` files, hand-written per type. Schema changes
   require coordinated changes in multiple places. Bug-prone.

A trigger-maintained totals removes both:

- TX-atomic: every `mov` write fires a trigger that updates `totals`
  in the same transaction. Either both happen or neither.
- Code lives in the database. OES generates the trigger SQL at
  Apply, then forgets about it.

The periodic "totals recalculation" command becomes physically unnecessary —
nothing can drift.

---

## Architecture sketch

```
┌──────────────────┐
│   Runtime (C++)  │
└──────┬───────────┘
       │ INSERT INTO mov_X            (writes only here)
       │ SELECT FROM vw_balance_X     (reads only via views)
       ▼
┌────────────────────────┐    AFTER INSERT/UPDATE/DELETE     ┌─────────────────┐
│  mov_X                 │ ───────────  trigger ───────────► │  totals_X       │
│  (period, dim*, res*)  │                                   │  (period, dim*, │
│  PRIMARY KEY (id)      │                                   │   res*)         │
│  INDEX (dim*, period)  │                                   │  PRIMARY KEY    │
└────────────────────────┘                                   │  (period, dim*) │
                                                             └────────────────┘
                                                                     ▲
                                                                     │ SELECT
                                                ┌────────────────────┴──────────┐
                                                │ vw_balance_X / vw_turnover_X /│
                                                │ vw_slice_last_X (read API)    │
                                                └───────────────────────────────┘
```

For the runtime, `totals_X` is implementation detail. Views are the
public read API. `mov_X` is the public write API. Strategy can swap
later (live aggregation, partitioning, MSSQL indexed views, etc.)
without touching runtime code.

---

## What OES Apply generates per register

On `createMetaTable`:

1. `CREATE TABLE mov_X (id, period, dim1..dimN, res1..resM)` with
   indexes on `(dim*, period)` for write-side lookup.
2. `CREATE TABLE totals_X (period, dim1..dimN, res1..resM)` with
   `PRIMARY KEY (period, dim1..dimN)` and `fillfactor = 80` on PG
   (enables HOT updates).
3. `CREATE TRIGGER tr_mov_X_ai/au/ad` — three triggers (after
   insert / update / delete), updating `totals_X` with delta.
4. `CREATE VIEW vw_balance_X / vw_turnover_X / vw_slice_last_X` —
   read paths. Trivial views over `totals_X` initially, can grow
   smarter (multi-level snapshots, etc.) later.

On `updateMetaTable` (structure change):

1. `DROP TRIGGER` on `mov_X` (old column list is stale).
2. `ALTER TABLE mov_X` for added/dropped/renamed/retyped columns.
3. `DROP TABLE totals_X` (it's derived state, regenerable from `mov`).
4. `CREATE TABLE totals_X` with new schema.
5. `INSERT INTO totals_X SELECT period, dim1..dimN, SUM(res1)... FROM
    mov_X GROUP BY period, dim1..dimN` — one-time rebuild.
6. `CREATE TRIGGER` with new column list.
7. `CREATE OR REPLACE VIEW` for the three views.

The rebuild step is the only "expensive" part of a structure change.
Cost on PG with proper indexes:

| `mov_X` size  | Rebuild time |
|---------------|--------------|
| 1 M           | ~1–3 s       |
| 10 M          | ~10–30 s     |
| 100 M         | ~5–10 min    |
| 1 B           | ~1–2 h (needs partitioning + parallel) |

Apply runs in OES exclusive mode anyway, so taking that wall-clock
for a structure change is acceptable.

---

## Trigger shape (PostgreSQL example)

For an accumulation register (additive resources):

```sql
CREATE FUNCTION fn_mov_X_ai() RETURNS trigger AS $$
BEGIN
  INSERT INTO totals_X (period, dim1, dim2, qty)
    VALUES (date_trunc('month', NEW.period), NEW.dim1, NEW.dim2, NEW.qty)
    ON CONFLICT (period, dim1, dim2)
    DO UPDATE SET qty = totals_X.qty + EXCLUDED.qty;
  RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tr_mov_X_ai AFTER INSERT ON mov_X
  FOR EACH ROW EXECUTE FUNCTION fn_mov_X_ai();
```

Update / delete triggers symmetric — apply `OLD.qty` reversed,
then `NEW.qty` (update) or just reverse `OLD.qty` (delete).

For Firebird the same shape with `UPDATE OR INSERT MATCHING`
instead of `INSERT ... ON CONFLICT`.

---

## Performance profile

### Read path (balance / turnover / slice-last queries)

Identical to the existing explicit-totals path — `SELECT * FROM totals_X
WHERE period <= ? AND dim1 = ?` is an index lookup, O(log N).
Fitness for ~10 K concurrent readers asking for current balance:

- ~1000–2000 SELECT/sec sustained even on a single PG instance —
  buffer cache holds the index hot, each lookup is 4–6 page reads.
- Hot index pages live in `shared_buffers` permanently for an active
  register. Cold reads of historical periods touch disk but are rare.
- 10 K nominal users → ~500–1000 active at peak; well below PG's
  ceiling for indexed-lookup workloads.

### Write path (movement INSERT triggering totals UPDATE)

For 100 active writers (typical document-posting workload):

- ~5–10 movements per posted document, ~1 doc/min per writer →
  500–1000 mov-writes/min → ~10–20 trigger fires/sec.
- Per-row UPDATE on `totals_X` with `fillfactor = 80` triggers PG
  HOT update path: same page, no index modification. ~0.1–0.5 ms.
- Total CPU/IO on totals_X: trivial. Hot-row contention only
  becomes visible if many writers concentrate on a single
  `(period, dim1, dim2)` combination — unusual for typical
  accounting workloads, lethal for warehouse-stock single-SKU
  scenarios. Mitigation if needed: shard the totals row (N rows
  per combination, hash by connection id, aggregate on read).

### Backdated movement

INSERT with old period → trigger updates the totals row for that
period. For running-balance registers the trigger also bumps
`qty` on subsequent periods; that's O(K) where K = number of
later periods affected (typically tens, not millions). Same cost
as an app-side rebuild for the same scenario.

### Bulk import

Each row fires a trigger. 1 M-row import = 1 M trigger calls. Slow
(50–500× slower than triggerless append). Workaround: temporarily
`DROP TRIGGER`, bulk insert, `INSERT INTO totals_X SELECT ... GROUP
BY ... FROM mov_X`, recreate trigger. This is exactly the
"manual rebuild" we wanted to avoid — but only for bulk-import,
not steady state.

---

## Cross-DB strategy

| Engine     | Materialisation strategy                  | Notes                                          |
|------------|-------------------------------------------|------------------------------------------------|
| PostgreSQL | Triggers + totals table + views           | Production target. PG `MV` is manual REFRESH only. |
| Firebird   | Triggers + totals table + views           | Embedded / SMB. Same pattern as PG, PSQL syntax. |
| MSSQL      | Same — or `INDEXED VIEW WITH SCHEMABINDING` | Native indexed view = best, future optimisation. |
| Oracle     | Same — or `MV REFRESH FAST ON COMMIT`     | Native MV with log = best, future optimisation.  |
| MySQL      | Triggers + totals table + views           | No native MV; triggers are the only path.        |
| SQLite     | Triggers + totals table + views           | No materialised views. Single-process scope only. |

Implementation plan:

1. **Step 1 — single strategy**: triggers + totals + views,
   per-driver SQL templates. Works everywhere, cleaner code.
2. **Step 2 (optional)**: pluggable `ibRegisterTotalsStrategy`
   interface. MSSQL strategy emits `INDEXED VIEW`, Oracle emits
   `MV REFRESH ON COMMIT`. Other drivers stay on the trigger
   path. Add only if profiling shows it matters.

Don't proliferate strategies prematurely — the trigger pattern
covers all five engines uniformly.

---

## What this gives OES

- ✅ `mov_X` ↔ `totals_X` cannot drift; `RebuildTotals` command goes
  away.
- ✅ Periodic "totals recalculation" is unnecessary by construction.
- ✅ Backup / restore: `mov_X` is the source of truth. `totals_X` is
  cache, regenerable with one SQL statement.
- ✅ OES C++ register-maintenance code shrinks substantially —
  `ProcessAttribute`-style ALTER TABLE handling stays, but
  hand-written totals UPSERT logic in business paths is removed.
- ✅ Schema changes go through `INSERT INTO totals_X SELECT ... GROUP
  BY ...` — one SQL statement, no per-case rebuild logic per
  register kind.

## What this costs

- ⚠️ Per-driver trigger SQL templates — 5 dialects to maintain.
  Single template per register kind, parameterised by dimension /
  resource list. Maybe 200–500 lines of generator code total.
- ⚠️ Bulk import paths need to opt out (drop-trigger / rebuild)
  pattern. One helper, used by import-specific code paths.
- ⚠️ Trigger errors surface as obscure SQL exceptions instead of
  C++ stack traces. Operational investigation cost goes up slightly.
- ⚠️ Schema-change `INSERT...SELECT` rebuild is an Apply-window
  long-task on big tables (minutes for 100 M rows). Acceptable for
  Apply, painful if accidentally triggered at runtime.

---

## What this is NOT

- **Not "remove the totals table".** Pure live aggregation through
  `CREATE VIEW ... GROUP BY` works only for tiny registers (< 1 M
  movements). For OES production scale (10⁷+ movements over years,
  10 K active readers), live aggregation requires CPU resources that
  scale with data volume; it doesn't make sense as the primary
  storage model. The trigger-maintained totals proposal is a
  refactoring of *who maintains totals*, not a removal of totals.
- **Not "use materialised views" on PG.** PG's materialised views
  have only manual `REFRESH MATERIALIZED VIEW`. They're snapshots,
  not auto-updating. Triggers are the actual auto-updating
  equivalent on PG.
- **Not 1000 user scale on FB embedded.** Heavy production deploys
  on PostgreSQL. FB embedded targets ~20–50 users. The
  architecture is the same on both; scale comes from the engine
  choice, not from the schema.

---

## Required PG-side configuration for production scale

Independent of this architecture, but essential to get the
performance numbers above:

- `fillfactor = 80` on `totals_X` — enables HOT updates, avoids
  index-update cost on every UPDATE. 3–5× write throughput
  improvement.
- Tuned autovacuum on `totals_X`:
  ```sql
  ALTER TABLE totals_X SET (
    autovacuum_vacuum_scale_factor = 0.02,
    autovacuum_analyze_scale_factor = 0.01
  );
  ```
- `pgbouncer` in transaction-mode — 10 K logical sessions →
  50–100 PG backends. PG will not handle thousands of direct
  connections without pooling.
- Optional: partition `mov_X` (and `totals_X`) `BY RANGE (period)`
  on registers that grow past ~50 M rows. Current-period queries
  see only the active partition; historical archive can DETACH +
  cold-store.
- Optional: streaming read replicas if reporting dashboards push
  the master beyond 70 % CPU. Standard PG ops, orthogonal to OES
  schema design.

---

## Implementation roadmap (when this gets picked up)

1. **PoC on `accumulationRegisterQuery::CreateAndUpdateTableDB`
   for the simplest register type** (one dimension, one resource,
   per-period sums — i.e. turnovers, no running balance). Generate
   `mov_X`, `totals_X`, three triggers, three views. Smoke-test on
   PG and FB.
2. **Read-path migration**: replace `RegisterSet::GetBalance`-style
   methods to read through `vw_balance_X` instead of computing
   from `mov_X` + existing totals.
3. **Generalise to other register kinds**: balance registers
   (running totals), accounting registers, information registers
   (slice-last). Same trigger pattern, different aggregate
   columns.
4. **Schema-change handling**: extend `updateMetaTable` to drop
   triggers / drop totals / rebuild. Handles every kind of column
   change uniformly.
5. **Per-driver SQL diallect**: generator outputs PSQL for FB,
   PL/pgSQL for PG, T-SQL for MSSQL, etc. Templated.
6. **Migration tool**: existing OES installations need a one-shot
   "rebuild totals from current mov_X, swap to trigger-maintained"
   pass. One SQL per register, runs at first Apply after upgrade.

---

## Open questions

- **Predefined-record changes via Apply** currently round-trip
  through `ProcessPredefinedValue` (idempotent probe added
  2026-04-30). For trigger-maintained registers, the seed pattern
  changes shape — initial population is `INSERT INTO totals_X
  SELECT ... GROUP BY ... FROM mov_X` which is naturally
  idempotent. The two seed paths might unify around a single
  "regenerate derived state from source-of-truth" idiom.
- **Multi-level snapshots** (per-month checkpoints + live
  aggregation for current period) is a midpoint between current
  totals + live, useful for very large registers where even
  totals_X grows huge. Not in scope for v1; document as a future
  optimisation.
- **Cross-register transactions** that touch multiple registers in
  one document posting work as today (one TX, multiple
  triggers fire, all atomic). No new concurrency surprises beyond
  the existing explicit-totals approach already had.
