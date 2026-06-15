# Temp-DB — DB temporary tables as the materialisation / optimisation layer

> **Status (2026-06-11): CORE LANDED, PG live validation pending.** The capability seam, the
> temp-table manager (`tempTableManager.{h,cpp}` — probe / CREATE+fill via L2 / RAII drop /
> graceful RAM fallback), the DB-temp source adapter, the PostgreSQL dialect, and the
> server-side join/union promotions (`PromoteComputedLeaf` / `PromoteUnionBranches` in
> `queryProvider.cpp`, column remap incl. aggregates) are all in the tree. The decision is
> split `WorthDbTemp` (SHOULD, size) + manager `Materialise` (CAN, presence/probe/fallback).
> Still pending: a live PostgreSQL validation run (the dev FB never takes the temp path by
> design), the L4 materialise-into-temp surface, and MySQL. §9 has the current table. This doc
> is the agreed contract. Builds on [reference-as-key](#) +
> [column-based lowering](query-language-arc.md) (L3 is source-agnostic, which is what makes all
> of this nearly free).

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
- **SQLite** — has ad-hoc temp, but in OES it is **logs-only** (the logger's own-SQLite sink); the
  application query layer never runs on it, so it is **not a temp-table target**. Ignore it here.

So **FB sits on RAM, PostgreSQL balances with temp** — the capability lands where the data scale
needs it, not where development is habitual. **Validation wrinkle:** the dev DB (FB) ⇒ RAM, so the
temp path is **never exercised in the normal dev cycle** — the temp code lives only on PG. It is
therefore a **PG-targeted optimisation**, developed / validated against a PostgreSQL instance, not
the habitual FB dev default. Two good consequences hold: (a) RAM stays a **first-class warm path**
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
| Temp-table manager (`tempTableManager.{h,cpp}`) — holder-anchored lifetime (pins the connection via an owned `ibConnectionScope`), runtime capability probe, CREATE + fill via L2 DDL/DML (columns in the metadata storage format — references/enums round-trip), RAII DROP, graceful RAM fallback (null on no-dialect / failure). Per-session probe CACHE still TODO (probes per Materialise today). | **landed** |
| Per-driver dialects — **PostgreSQL** (`AdHocCreate`, explicit DROP via the pinning scope); MySQL later; Firebird stays `nullptr` (RAM); SQLite N/A (logs-only) | **PG landed** |
| **Server-side JOIN push-down** — `PromoteComputedLeaf` (computed ⋈ DB: temp the computed side, remap columns incl. aggregates/having, rebuild the join tree → the join runs in the DBMS) and `PromoteUnionBranches` (computed UNION branches temped, the whole union server-side). Generic multi-way / DB⋈DB-cross-connection promotion = future (federation). | **landed (two shapes)** |
| RAM-composer join ORDER — `ibQueryComposer::PlanInnerJoinOrder`: smallest-first reorder of pure-INNER chains (3+ units) with EXACT materialised counts; tree-order fallback on any anomaly | **landed** |
| L4 surface — a "materialise-into-temp" query construct | pending (with L4) |

Remaining order: **prove the whole pipeline (presence → probe → create → fill → server-side join →
RAM fallback) against a live PostgreSQL instance** (the dev FB never takes the temp path, so PG is
the validation stand) → MySQL → L4 surface. Firebird stays on RAM (GTT-shape not worth it for small
deployments).
