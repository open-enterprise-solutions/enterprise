# Temp-DB — DB temporary tables as the materialisation / optimisation layer

> **Status (2026-06-11): CORE LANDED, PG live validation pending.** The capability seam, the
> temp-table manager (`query/tempTableManager.{h,cpp}` — probe / CREATE+fill via L2 / RAII drop /
> graceful RAM fallback), the DB-temp source adapter, the PostgreSQL dialect, and the
> server-side join/union promotions (`PromoteComputedLeaf` / `PromoteUnionBranches` in
> `queryProvider.cpp`, column remap incl. aggregates) are all in the tree. The decision is
> split `WorthDbTemp` (SHOULD, size) + manager `Materialise` (CAN, presence/probe/fallback).
> Still pending: a live PostgreSQL validation run (the dev FB never takes the temp path by
> design), the L4 materialise-into-temp surface, and MySQL. §9 has the current table. This doc
> is the agreed contract. Builds on [reference-as-key](#) +
> [column-based lowering](query-language-arc.md) (L3 is source-agnostic, which is what makes all
> of this nearly free).
>
> ### Update 2026-06-23 — SQLite temp dialect + ANALYZE as an L2 statement + script-join consumer
>
> - **SQLite now vends `GetTempTableDialect()`** (`AdHocCreate`, `CREATE TEMPORARY TABLE`, explicit
>   DROP via the pinning scope). It is no longer "logs-only / ignore" (§5 corrected): SQLite is the
>   **embedded correctness-validation stand** for the temp path — `oes_temp_db_sqlite_test` runs the
>   whole pipeline (CREATE TEMP → chunked fill → ANALYZE → read-back) in a gtest with a SQLite-only
>   pool. FB still stays `nullptr` (RAM) by design; PG remains the **performance** stand.
> - **ANALYZE is now a first-class L2 statement** — `ibDdlKind::Analyze` + `ibAnalyzeTable(name)`,
>   rendered from the MAIN `ibDialectDictionary::m_analyzePrefix` (PG / SQLite `"ANALYZE"`; Firebird
>   empty ⇒ renders empty ⇒ `Execute` no-ops). `Materialise` runs it after the fill so the optimiser
>   plans the JOIN against the temp's REAL cardinality, not a default estimate (the missing half of
>   "temp tables ARE the optimiser", §1). Because it is a general L2 verb (not a temp-only string), it
>   is **reusable**: the regular-table refresh after a restructure / bulk load is the same call.
> - **Script `.Join` now feeds the promotions.** The L4-2 join unification (a RAM value table joined
>   with a `Data.*` queryable, EITHER direction) lowers into the L3 join node and reaches
>   `PromoteComputedLeaf` — so a heterogeneous join temp-promotes server-side on the embedded DB too.
>   See [query-language-arc.md](query-language-arc.md) (L4-2 join push-down).
> - **Still open:** an INDEX on the temp's join key (situational — helps index-nested-loop, wasted for
>   a hash-join; build it only with a PG stand to measure); the regular-table ANALYZE seam
>   (restructure / scheduled); the live-PG performance run (SQLite proves correctness, not plans —
>   its data is too small for timing to mean anything).

## 1. Why — temp tables ARE the optimiser

OES does **not** build a cost-based query optimiser. It delegates cost-based planning to the
underlying DBMS (PostgreSQL / Firebird / … — decades of optimiser engineering) and gives the
developer (and L4) the **temp-table lever** to make that optimiser succeed:

- A complex nested subquery makes the optimiser **guess** cardinality — wrong guess → bad join
  order / method → slow.
- Materialising the intermediate into a temp table gives the DBMS a **real, cardinality-bearing,
  statistics-able** set, so the following join is planned on fact, not estimate. The temp table
  converts *"optimiser guessing"* into *"optimiser knowing"*.

So a DB temp table is not a scale-nicety; it is **the optimisation architecture** of this class
of system. L4 must therefore expose temp tables as a first-class query construct (a
"materialise-into-temp" surface) — it is the optimisation surface, the only one.

## 2. The seam — capability by presence (LAID)

```cpp
struct ibTempTableDialect { Strategy m_strategy; wxString m_createPrefix, m_onCommitClause, m_dropPrefix; bool m_autoDrops; };
virtual const ibTempTableDialect* ibDatabaseLayer::GetTempTableDialect() const { return nullptr; }
```

The temp-table facts are a **separate, nullable** dictionary — vended apart from the main
`ibDialectDictionary`. **Its presence IS the capability.** `nullptr` ⇒ the driver has no DB temp
tables ⇒ L3 materialises the intermediate in RAM (`ibQueryComposer`). No boolean flag, no
"is-it-empty" check — the null test *is* the strategy selection. The feature lands **additively,
driver by driver**: each inherits `nullptr` until it implements temp tables.

## 3. RAM is the floor AND the runtime insurance

The RAM composer is the always-works floor, and the fallback is **two-level**:

1. **Static** — no dialect (`nullptr`) ⇒ RAM (capability absent by design).
2. **Runtime** — dialect present, but `CREATE`/`INSERT` fails at runtime (no DDL right, read-only
   replica, FB GTT pool not provisioned, temp-space out) ⇒ the planner **catches and falls back
   to RAM** rather than erroring. The fast path is opportunistic; correctness always lands.

This is nearly free **because L3 is source-agnostic**: the logical plan is one; the materialisation
of an intermediate (temp-table vs RAM) is an internal detail that can flip on failure, and the
consumer never branches. In an execution-bound engine a fallback means rewriting the query; here
it means choosing a different backing for the intermediate. Graceful degradation is a **dividend
of the column-based / backing-blind design**.

**The floor is not flat.** Landing on RAM is not the end of optimisation: the stitch passes
information sideways, pushing the already-materialised side's join keys into the other side's read
(`key IN (…)`), so a leaf on the RAM path still fetches only rows that can join. The two levers are
complementary and independent — temp-promote moves work TO the DBMS (needs a temp dialect, so not on
Firebird today, § 5), the key reduction shrinks a read that stays where it is (needs nothing, works
on every driver, and covers the shapes temp-promote cannot reach — a cross-DB join above all). See
[query-language-arc.md — Update 2026-07-27](query-language-arc.md).

## 4. Strategy — discriminator in the dict, behaviour in the manager

The behavioural fork stays **data**, not code-in-the-dictionary:

- `AdHocCreate` — `CREATE TEMP TABLE` of any shape per query, `INSERT`, `DROP` / auto-drop.
  SQLite / PostgreSQL / MySQL / MSSQL.
- `PreDeclaredPool` — grab a **typed** `GLOBAL TEMPORARY TABLE` from a schema-declared pool,
  `INSERT`, `ON COMMIT` clears the session-private rows. Firebird — GTTs are fixed-shape schema
  objects, **not** ad-hoc; an arbitrary-shape intermediate needs a generic-wide pool or RAM.

The dictionary holds the lexical facts + the `m_strategy` enum. The **control-flow fork** lives in
the temp-table MANAGER that reads `m_strategy` — never as `if (driver)` in the dictionary. Holding
that line is what keeps "dictionary = declarative facts, behaviour elsewhere" — the invariant that
kept the dialect system fork-free.

## 5. The Firebird paradox — capability lands where scale needs it

Counter-intuitively the primary dev DB is the **hard** case here, and that is correct:

- **Firebird** — fixed-shape GTTs + embedded / small deployments (3–30 users). Small intermediates
  ⇒ RAM is the right answer **even if FB could do temp** (the size threshold would pick RAM anyway).
  Pragmatic stance: FB stays **`nullptr` (RAM)** — a generic-wide GTT pool is ugly for a marginal
  gain on a small deployment. Vend the dialect only where it pays.
- **PostgreSQL** — ad-hoc any-shape `CREATE TEMPORARY TABLE`; production / scale ⇒ the temp lever
  matters. **This is the first (and primary) real temp target.**
- **SQLite** — has ad-hoc temp and **now vends the dialect** (`AdHocCreate`). In production it is
  logs-only (the logger's own-SQLite sink), so it is not a *deployment* temp target — but it is the
  **embedded correctness-validation stand**: a gtest stands up a SQLite-only pool and runs the whole
  temp pipeline (CREATE TEMP → fill → ANALYZE → server-side read) without a live server
  (`oes_temp_db_sqlite_test`). SQLite proves the path is **correct**; it cannot prove the **plan** (its
  data is too small and its optimiser simple — timing is noise). That is still PG's job.

So **FB sits on RAM, PostgreSQL balances with temp** — the capability lands where the data scale
needs it, not where development is habitual. **Validation wrinkle:** the dev DB (FB) ⇒ RAM, so the
temp path is **not exercised on the FB dev default** — its **correctness** is now exercised on SQLite
in gtest, and its **performance** is validated against a live PostgreSQL instance. Two good consequences hold: (a) RAM stays a **first-class warm path**
on FB (the floor / insurance never bit-rots); (b) FB is immune to any temp regression by
construction (it never takes the path). Crack the FB GTT-shape problem **separately, later, if ever**
— for small FB deployments it is likely never worth it.

## 6. Lifecycle — pin, probe, unwind

A temp table is **connection-scoped**: `create → fill → query → drop` must all run on the **same
pinned connection** (create on A, query on B after the pool rotates ⇒ table invisible). This is the
**same mechanism** as the write-scope / `FOR UPDATE` lock — a scope that holds the connection for
the duration (see [record-locks.md](record-locks.md), and the L1 "connection flows but is pinned
inside a scope" property).

- **Fail-fast, not fail-deep** — probe the runtime capability **cheaply and early** (DDL right /
  read-only flag) and **cache per session**; do not discover incapability by failing mid-bulk-insert
  of a million rows (you would pay for the failed attempt and *then* redo in RAM). Static probe =
  dialect presence; dynamic probe = a once-and-remember session flag.
- **Clean unwind** — a half-built temp table must be **dropped on fallback** (RAII via the pinning
  scope; scope-end drops), or the next attempt collides on the name.

## 7. The decision — CAN vs SHOULD

- Dialect present = **CAN** use DB temp.
- A **size threshold** = **SHOULD** — small intermediates stay RAM (the `CREATE`+`INSERT`
  round-trips do not pay off for a handful of rows); large ones go to a temp table for server-side,
  index-backed join.
- Absence = **hard floor**, RAM regardless.

So `nullptr ⇒ RAM` is the floor; *not* `present ⇒ always temp`.

## 8. Why it is *mostly* free — and the one genuinely hard part

A DB temp table is **not a new backing** — it is the **ordinary DB provider pointed at a temporary
table**. Reading it back is reused 100% (`ibDbTempTableQueryable` inherits the DB provider, raw
columns). Generating the temp `CREATE (…)` DDL from the intermediate's column set is trivial
**because columns self-describe** (name + `GetTypeDesc` + `GetValueFields`) after column-based lowering.

But there is **one genuinely hard part**, and it must not be glossed: the **server-side JOIN
push-down**. Today an explicit `.Join` across two sources goes through the **RAM composer**
(`ibQueryComposer::JoinRamTables`) — that path exists *because* a source could not be SQL-joined.
Materialising an intermediate into a DB temp table makes it SQL-joinable, but the join only becomes
server-side if the **DB provider learns to emit a SQL `JOIN` to the temp table** (BuildPageIR
currently emits joins only for dot-walk references, not arbitrary `.Join` nodes — those still go to
the composer). **Without that push-down, materialising into a temp gains nothing — you still
RAM-join.** So the temp-table feature is really two coupled pieces: (1) the lifecycle manager
(create/fill/drop/pin/probe/unwind — mechanical), and (2) the **provider/composer change to push a
two-DB-source join to SQL** (the hard, flow-changing part — it reshapes how the composer decides
RAM-compose vs SQL-pushdown). (2) needs a PostgreSQL instance + a build + iteration; it will lurch
like the L3 abstraction did, because it changes the compose flow. Do not lay it blind.

## 9. What is laid vs pending

| Piece | State |
|---|---|
| `ibTempTableDialect` + `GetTempTableDialect()` nullable seam (L1) | **laid** |
| Planner decision — split in two, each owned where its inputs live: `WorthDbTemp(rowCount)` = the SHOULD size-gate (§7), called at the promote sites with the EXACT materialised row count; the CAN-gate (dialect presence + runtime probe + graceful fallback) inside `ibTempTableManager::Materialise`. The generic RAM-composer `MaterialiseLeaf` seam deliberately stays RAM — temping a RAM-stitched leaf gains nothing (§8); new promotable shapes extend the promote family. | **landed** |
| `ibDbTempTableQueryable` — DB-temp source adapter (L3, sibling of the RAM `ibTempTableQueryable`); raw columns, inherits the DB provider, read-only scan | **landed** |
| Temp-table manager (`query/tempTableManager.{h,cpp}`) — holder-anchored lifetime (pins the connection via an owned `ibConnectionScope`), runtime capability probe, CREATE + fill via L2 DDL/DML (columns in the metadata storage format — references/enums round-trip), RAII DROP, graceful RAM fallback (null on no-dialect / failure). Per-session probe CACHE still TODO (probes per Materialise today). | **landed** |
| Per-driver dialects — **PostgreSQL** (`AdHocCreate`, explicit DROP via the pinning scope) + **SQLite** (`AdHocCreate`; the embedded correctness-validation stand, `oes_temp_db_sqlite_test`); MySQL later; Firebird stays `nullptr` (RAM) | **PG + SQLite landed** |
| **ANALYZE after fill** — `ibDdlKind::Analyze` + `ibAnalyzeTable(name)`, rendered from the main `ibDialectDictionary::m_analyzePrefix` (PG / SQLite `"ANALYZE"`; FB empty ⇒ `Execute` no-ops). `Materialise` runs it so the planner has the temp's REAL cardinality (not a default estimate). A general L2 verb — reusable for regular-table refresh after a restructure / bulk load. | **landed** |
| Temp-key INDEX (index-nested-loop) — situational; build only with a PG stand to measure (wasted for a hash-join) | pending |
| **Server-side JOIN push-down** — `PromoteComputedLeaf` (computed ⋈ DB: temp the computed side, remap columns incl. aggregates/having, rebuild the join tree → the join runs in the DBMS) and `PromoteUnionBranches` (computed UNION branches temped, the whole union server-side). Generic multi-way / DB⋈DB-cross-connection promotion = future (federation). | **landed (two shapes)** |
| **Server-side AGGREGATE push-down (SINGLE computed source)** — `PromoteSingleComputed` (queryProvider.cpp): a single computed source (register slice / balance / subquery) with a big result (≥ `kTempTableMinRows`) whose read AGGREGATES (`GROUP BY` / `SUM(expr)` / `HAVING`) is materialised into a temp table and run as ONE server-side SQL `GROUP BY` — instead of the RAM fold. A **reference GROUP key** rides its full blob spread (grouped + reconstructed server-side); a **dot-walk key / input** (`Balance.Item.Name`, `SUM(Item.Weight)`) remaps its FIRST path segment onto the temp and the deeper segments join the target catalog SERVER-SIDE through the ordinary dot-walk join chain (`ibRefJoinChain`) — the temp is a real DB source, so it inherits the physical dot-walk machinery. ⚠️ The DB aggregate result is a LAZY cursor over the temp; the manager DROPs the temp when the promote block exits, so the injection **drains the cursor into a RAM table WHILE the manager is alive** (a RAM-backed result outlives the temp — the same shape the JOIN/UNION promotes yield). The provider projects every group key by its FULL spread (`ColumnFieldNames`), so the drain reads it back uniformly by `GetValue` — metadata-blind, the reference-typing stays in the provider. Only small results (< `kTempTableMinRows`) / no-temp-dialect stay RAM (correct). SQLite-validated (`tests/test_queryComputedServer.cpp`). | **landed** |
| RAM-composer join ORDER — `ibQueryComposer::PlanInnerJoinOrder`: smallest-first reorder of pure-INNER chains (3+ units) with EXACT materialised counts; tree-order fallback on any anomaly | **landed** |
| L4 surface — a "materialise-into-temp" query construct | pending (with L4) |

Remaining order: **prove the whole pipeline (presence → probe → create → fill → server-side join →
RAM fallback) against a live PostgreSQL instance** (the dev FB never takes the temp path, so PG is
the validation stand) → MySQL → L4 surface. Firebird stays on RAM (GTT-shape not worth it for small
deployments).
