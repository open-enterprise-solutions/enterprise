# Register totals — trigger-maintained strategy (proposal)

Architecture proposal for OES register storage that would move the
"totals" maintenance burden into the database via triggers, while
preserving the read latency profile of the classic denormalized
totals-table pattern (as used by comparable enterprise platforms).
Status: **proposal — design discussed 2026-04-30, extended 2026-07-22,
NOTHING implemented in code** (verified 2026-06-19: no `totals_*`/`mov_*`
tables, no trigger generation, no `ibRegisterTotalsStrategy`, no totals
views, no `RebuildTotals`). The 2026-07-22 session added the engine-
integration design (§ *Engine integration* below): the source/derived
table classification driving L3-2/L3-3/L3-4, the per-driver
materialization dictionary, reconciliation-by-cheapest-op, and totals-row
sharding as a per-register mode. Still design-only. Registers today persist movement lines only — there
is no totals/aggregation layer yet, denormalized or otherwise. This
doc describes the *intended* design for when registers/reporting get
built; treat every "OES does X" below as "OES would do X under this
proposal." Expected first target: PostgreSQL production, with FB
embedded sharing the same pattern at smaller scale.

> **TL;DR** — keep the totals table; let SQL triggers maintain it
> instead of OES C++. Read path goes through views, runtime is
> totals-agnostic. No periodic recalculation. No drift. Same read
> performance as the explicit-totals pattern. Slight write overhead
> (~5–15% per movement). Cross-DB via per-driver trigger templates.

---

## Why move maintenance into the database

The classic explicit-totals pattern (other platforms with `mov +
totals` tables — and the shape OES would otherwise grow into) writes
to `mov_X` (the movements table) and then explicitly updates
`totals_X` rows from application C++. Two problems compound — and they
are exactly the trap to avoid when OES builds its totals layer:

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

## Engine integration (design extension — 2026-07-22)

The sketch above is generic. This section is how the totals layer plugs into the OES query
engine (L1–L5, see [query-engine-layers.md](query-engine-layers.md)) so that the read path
stays coherent and the maintenance stays out of C++.

### 1. One bit: source vs derived

Every table a metaobject contributes (via its `ContributeTables` snapshot) is classified
**source** or **derived**. This single classification drives three floors correctly:

| Table | Class | L3-2 (structure) | L3-3 (data mover) | L3-4 (regeneration) | Read |
|---|---|---|---|---|---|
| `mov_X` | **source** | generate + migrate | round-trip (dump **and** load) | — | physical scan |
| `totals_X` + triggers + views | **derived** | generate + migrate | **never touched** | regenerate | co-locatable queryable |

`totals_X` is *derived state*: never dumped (regenerated at the destination), never migrated
(dropped + regenerated), never hand-maintained (the trigger owns it). One bit — "derived" —
says all three: *don't move it, regenerate it, keep it consistent by trigger.*

### 2. Three actors, three verbs — do not conflate them

- **Trigger — *updates*.** Per-movement delta, in the DB, TX-atomic, **unbypassable**
  (fires for any writer to `mov_X`: runtime, plugin, script, restore), **never invoked**.
  This is steady state.
- **L3-4 — *regenerates*.** Rebuilds totals from source, **invoked** on discrete events
  (Apply / structure-change, bulk import, migration). Not per-movement. **Not a cadence.**
- **L3-2 — *builds structure*.** Generates and migrates the whole bundle: `mov_X`, `totals_X`,
  the three triggers, the three views.

The seam that rusts: "update" is the trigger's job, never L3-4's. The moment L3-4 (or the
compute server) runs *per-movement* totals updates, you are back in the bypassable
managed-code pattern and drift returns. Keep the verbs apart: **trigger updates, L3-4
regenerates.**

### 3. Read side — totals become an ordinary queryable

Reads go through `vw_balance_X` / `vw_turnover_X` / `vw_slice_last_X`. The register's queryable
points at the view, so `IsComputedInRam()` flips **false** (was true for the RAM-computed
`ComputeRows` path). Now totals are an **ordinary physical co-locatable source**: they JOIN,
UNION, temp-promote, keyset-page and RLS-restrict through the *existing* L3 machinery, with no
special case above the view. This is the migration in one seam: `IsComputedInRam → false`,
`GetQueryable → view`; the read path (L3-1/L4/L5) does not change.

**Keep the view thin** — `SELECT … FROM totals_X`, not `GROUP BY` over `mov_X`. A thin view
co-locates and stays O(log N); a fat live-aggregation view JOINs syntactically but recomputes
every read and does not break the scale ceiling. JOINability comes from the thin table shape;
speed comes from the trigger-maintained table under it. Two separable properties, both needed.

### 4. The materialization dictionary — a *second* per-driver dictionary

The trigger bodies are imperative and **per-engine divergent in structure**, not just in
spelling. They therefore do **not** fit the L2 **query** dialect dictionary (which substitutes
tokens over a *shared* structure — `LIMIT`, quoting, type mapping). Forcing them into L2 would
either pollute the query IR with imperative nodes (`RETURN`/`IF`/`NEW`/`OLD`) or add a raw-SQL
escape hatch that breaks L2's "never raw SQL" invariant.

Instead, each driver vends a **second dictionary — the materialization dialect** — parallel to
`GetDialect()`:

- **L2 query dictionary** renders the *declarative skeleton*: `CREATE TABLE mov_X`/`totals_X`,
  indexes, the view's inner `SELECT`, the rebuild `INSERT … SELECT … GROUP BY`, `ALTER`.
- **Materialization dictionary** renders the *imperative delta*: trigger shell + upsert idiom +
  hot-update hints. Used only at Apply / regeneration, never on a read.

Shape it as a 2-D table, **`kind × engine`**:

- **`kind`** (turnover `+=` / running-balance `+=` + bump-later-periods / slice-last) is the
  **delta-clause parameter** — the same concept across engines.
- **`engine`** is the **key**, and it splits into two template **families** by *execution model*:
  - **per-row** — Firebird PSQL, PostgreSQL PL/pgSQL, MySQL SP, SQLite triggers. All are
    `FOR EACH ROW`, use `NEW`/`OLD`, and reduce to a handful of plain SQL statements. Design to
    the **SQLite floor** (SQL-statements-only, no procedural block) and the same logic fits all
    four; only the upsert idiom (`ON CONFLICT DO UPDATE` for PG/SQLite, `ON DUPLICATE KEY` for
    MySQL, `MERGE` for Firebird) and the shell (PG needs a separate `FUNCTION`; the rest inline)
    differ — those are the per-engine *slots*.
  - **set-based** — MSSQL/T-SQL (`inserted`/`deleted` pseudo-tables, batch `MERGE`). This is a
    *different algorithm*, not a different word, which is exactly why a separate dictionary
    holds it where the query dictionary could not. **Deferred to the MSSQL port** — not one of
    the four native engines; adding it later is one more engine key, not a reshape.

Both families implement one interface (`RenderTotalsTrigger(kind, dims, res) → SQL`); the L3-2
generator is engine-agnostic and only fills parameters. The dictionary is also the natural home
for the future pluggable strategy (MSSQL `INDEXED VIEW`, Oracle `MV REFRESH ON COMMIT`) — a
different entry per engine, no generator change.

> **ODBC has no materialization entry.** ODBC is generic — it does not know the backend, so it
> cannot template engine-specific triggers. An ODBC-backed register stays on the
> live-aggregation / RAM fallback (`IsComputedInRam` stays true); it never gets the trigger
> upgrade. Not a blocker — ODBC is a marginal target — but it is the one driver the dictionary
> does not cover.

### 5. Reconciliation by the cheapest sufficient operation

The "What OES Apply generates" section below always full-rebuilds on a structure change. That
is correct and simple, but on large registers the full `mov_X` scan is minutes-to-hours. Most
structure changes do **not** need it — their effect on totals is *derivable*:

| Change | Reconciliation | Full `mov` scan? |
|---|---|---|
| Add a registrar | nothing (future movements → trigger) | no |
| Remove a registrar | delete its `mov` rows → trigger reverses their deltas | no (delta) |
| New resource column | `ALTER totals ADD res DEFAULT 0` (no history → 0) | no |
| New dimension, uniform default | `ALTER totals ADD dim = default` (grouping unchanged) | no |
| Drop a dimension | **coarsen** — re-aggregate from the *existing* `totals`, not `mov` | no |
| Full data load (restore / migrate) | totals start empty → rebuild | **yes** |
| Grouping changes un-derivably (backfill dim with per-row values, finer period) | rebuild | **yes** |
| Bulk import | aggregate **only the imported batch** and merge (`… GROUP BY … FROM batch`) | no (delta of the batch) |

So reframe L3-4: it is not "the full-rebuild door", it is **"reconcile derived state by the
cheapest sufficient operation"**, and the full scan is its *fallback mode*. Preference order:
trigger delta (steady state) → subset delta-aggregate (import, removed registrar) → in-place
totals transform (add-default-column, coarsen) → full `mov` scan (empty totals / un-derivable
regroup).

**Correctness gate (same asymmetry as everywhere): a delta / in-place path is valid ONLY for
change kinds whose totals-effect is provably derivable.** Misclassify — delta where a scan was
needed — and you get *silently wrong totals*. When unsure, full-scan. Full-scan is the safe
default; deltas are the optimization applied to provably-derivable kinds. Ship the full-scan
fallback first; add the delta paths when a real big-register Apply window forces it.

### 6. Sharding — "разделение итогов" (a per-register mode)

Hot-row write contention on a single `(period, dim)` (a popular SKU in the current period,
dozens of concurrent postings) is mitigated by **sharding the totals row** N-ways. This is the
same concept as 1C's *разделение итогов* (feature parity) — but on OES's **drift-proof trigger
substrate**, where 1C maintains its split totals in managed code (bypassable, needs *пересчёт
итогов*).

A per-register spec parameter, `sharded: N`:

- `N > 1` → the totals PK gains a shard column `(period, dim1..dimN, shard)`; the trigger's
  upsert writes `shard = hash(connection / tx)` chosen **locally**; the view sums shards
  (`SELECT … SUM(res) … GROUP BY period, dim`).
- **The view absorbs sharding** — the read path above the view is unchanged, co-locates like any
  table. Sharding does not leak upward; it is contained in `table + trigger + view`.

Two properties make it as autonomous as the base deltas — no coordination, no extra state:

1. **Local shard key.** `hash(current connection/tx)` — not a shared counter (which would itself
   contend). Concurrent writers hash to different shards.
2. **Sum-invariance.** An insert's `+delta` and a *later* delete's `−delta` may land in
   **different** shards; because the read **sums all shards**, the total is exact regardless of
   distribution: `Σ shards = Σ inserts − Σ deletes = net`. So the trigger stores **no memory**
   of which shard a movement used — it just hashes the current context. Individual shards hold
   partial/odd values; the read-sum is always exact. Each shard row is still trigger-maintained
   TX-atomically → **drift-proof is preserved.**

**Tradeoff:** read `O(1)` → `O(N)` (sum N shards). Sharding is **per-register** — the read tax
hits *all* its rows, not just the hot one — so shard **only** registers profiled hot on write,
never by default. Cleanest for turnover (`+=`); running-balance shards ride the same
sum-invariance but the subsequent-period bump is inherently fiddlier (it is fiddly without
shards too). Capture `sharded:N` in the spec now so the form accepts it; implement when a real
hot register pulls it. Regeneration with shards is trivial: bulk-rebuild into `shard 0` (no
contention under Apply), steady-state redistributes via the hash.

### 7. Who invokes L3-4 (deployment)

Same L3-4 operation; different **dispatcher**:

- **Server mode** (compute server present): a **compute-server job** — chunked, resumable,
  under exclusive/Apply. Fits the [compute-server-tiering.md](compute-server-tiering.md) arc:
  heavy server-side regeneration is exactly such a tier.
- **File / embedded mode** (no compute-server process; single session → already exclusive): an
  in-process **background task**. Keep it a **janitor** (detect-and-repair an *incomplete*
  bulk/crash: trigger missing / totals stale after an interrupted operation → rebuild), **not a
  patrol** (periodic recalc of trigger-maintained totals — that is the *пересчёт итогов* ghost
  the trigger makes unnecessary by construction). Fire it on **events**, not on a cadence.

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

## What this would give OES

(All prospective — none of this exists yet; see status note at top.)

- `mov_X` ↔ `totals_X` cannot drift; no `RebuildTotals` command ever
  needs to be written.
- Periodic "totals recalculation" is unnecessary by construction.
- Backup / restore: `mov_X` is the source of truth. `totals_X` is
  cache, regenerable with one SQL statement.
- The totals-maintenance logic never enters business-path C++ — the
  trigger owns it, so the explicit-totals UPSERT code OES would
  otherwise hand-write per register kind is avoided up front.
- Schema changes go through `INSERT INTO totals_X SELECT ... GROUP
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

- **Seed / predefined data on Apply.** Whatever idempotent seed path
  the schema-driven data layer settles on (`ContributeTables` is the
  current single source of DDL + data + seed — see
  `query-language-arc.md §23`), trigger-maintained registers fit it
  cleanly: initial population is `INSERT INTO totals_X SELECT ...
  GROUP BY ... FROM mov_X`, naturally idempotent. The two seed paths
  could unify around a single "regenerate derived state from
  source-of-truth" idiom. (The earlier draft cited a
  `ProcessPredefinedValue` probe — no such symbol exists in code;
  ignore that reference.)
- **Multi-level snapshots** (per-month checkpoints + live
  aggregation for current period) is a midpoint between current
  totals + live, useful for very large registers where even
  totals_X grows huge. Not in scope for v1; document as a future
  optimisation.
- **Cross-register transactions** that touch multiple registers in
  one document posting work as today (one TX, multiple
  triggers fire, all atomic). No new concurrency surprises beyond
  the existing explicit-totals approach already had.
