# Query engine — the floor plan (L1–L5)

> **Scope:** the one thing to read FIRST about the query engine — the **layer taxonomy**. L1–L5
> are floors of **one building**; L3 and L4 each have sub-floors; L4 sits **on** L3; a shared
> technical floor bridges L2 and L3. This map exists so anyone (human or AI) touching the query
> arc knows how the pieces relate before diving into a single-floor doc. A MAP of code that
> **already exists.**
>
> **Companions (per-floor detail):** [query-language-arc.md](query-language-arc.md) (the full
> arc — L2-1 IR, L3 door, L4-1/L4-2), [data-composer.md](data-composer.md) (L5),
> [linq.md](linq.md) (L4-2), [access-policy-rls.md](access-policy-rls.md) (L4-3),
> [database-layer.md](database-layer.md) (L1), [temp-db.md](temp-db.md) (the temp-table seam),
> [table-model.md](table-model.md) (who consumes a fetch), [metadata-lifecycle.md](metadata-lifecycle.md)
> (what L3-2 / L3-3 / L3-4 feed).
>
> **Status:** landed (experimental working copy) — the whole ladder is in the tree.

---

## 1. One building, five floors

```
  L5  composer            L5-1 DB (renders → L4-1 text)   |  L5-2 RAM (self-contained)
       │  renders/settles ▼
  L4  authoring tiers     L4-1 text query  ·  L4-2 LINQ push-down  ·  L4-3 RLS (decorator)
       │  all lower into the ONE L3 door ▼
 ┄┄┄┄┄ shared technical floor: ibBackendQueryProvider ("the whole L3↔L2-1 layer") ┄┄┄┄┄┄┄┄┄┄
       │  + ibBackendQueryable / ibBackendQueryColumn / ibQueryAst / ibTempSourceScope
  L3  source-agnostic door  L3-1 the door (read/write/aggregate/dot-walk)
                            L3-2 structure (DDL) · L3-3 data mover (rows) · L3-4 regenerate (derived)
       │  builds IR ▼
  L2  renderers   L2-1 ibDatabaseQueryBuilder — structured ibQueryIR (never raw SQL)
                  L2-2 ibDatabaseMaterializeBuilder — derived-state triggers + views
       │  ▼
  L1  ibDatabaseLayer ×5 drivers — each vends its ibDialectDictionary
```

The rule of the house: **one door down, many front-ends up.** Everything above L3 (the three L4
authoring tiers, both L5 composers) funnels through the **single** L3 door; L3 renders to L2-1 IR;
L2-1 renders to a driver's dialect at L1. No front-end reaches the drivers directly.

---

## 2. Floor by floor

### L1 — drivers (`ibDatabaseLayer` ×5, `databaseLayer/`)
Firebird / PostgreSQL / SQLite / MySQL / ODBC. Each vends its own `ibDialectDictionary`
(`GetDialect()`) — **zero central type-switch**. See [database-layer.md](database-layer.md),
[connection-pool.md](connection-pool.md).

### L2 — the renderers (`databaseLayer/`) — TWO halves
L2 is "a structured description in, dialect-spelled SQL out". It has two halves, each with its own
dictionary, and neither knows anything about metadata:

- **L2-1 — the query IR** (`ibDatabaseQueryBuilder` / `ibQueryRenderer`, `databaseQueryBuilder`).
  A structured **`ibQueryIR`** (never raw SQL) with a full vocabulary — Scan/Filter/Project/Sort/
  Limit/Join/Aggregate/Subquery/Distinct/Union, expressions, DDL, DML — rendered generically
  through `ibDialectDictionary`.

  A write can also hand back what it wrote: `ibReturning(dml, {cols})` +
  `ExecuteReturning` yields a **cursor over the affected rows**, exactly like a SELECT. This is
  what makes "bump a counter and read the new value" ONE statement, hence atomic — no window
  between a read and a write for a concurrent session to slip into. The spelling is the dialect's
  (`m_returningClause`: Firebird, PostgreSQL, SQLite 3.35+); a driver without one **throws**
  `UnsupportedNode` rather than emulate it, because write-then-SELECT loses the atomicity that
  was the entire point. `GenerateNextIdentifier` (`sys_sequence`) is the first tenant — it was
  the last raw-L1 holdout on the write path, kept there precisely because RETURNING had no L2
  form.
- **L2-2 — the materialization renderer** (`databaseMaterializeBuilder`). An `ibMaterializeSpec` →
  the trigger / view statements that maintain a **derived** table, rendered through the driver's
  second dictionary, `ibMaterializationDialect`. It also RENDERS THE READ (`RenderMaterializedRead`
  → an `ibQueryRelPtr` the caller drops into a `FROM`), so a metaobject describes the figures it
  wants and never assembles SQL shape; and it EXECUTES what it rendered (`ibMaterializeSql::Apply`),
  the same way L2-1 runs the queries it builds — no SQL string ever leaves this level.

  Its two entry points are a pair: `ibApplyMaterialization` replaces a bundle whole — but only when
  the declaration CHANGED (it renders the baseline too and compares the text, then probes that the
  objects are really installed, so an untouched register costs nothing and a half-installed one
  heals) — and `ibDropMaterialization` removes one **by an old spec**, needed because a bundle
  OUTLIVES the declaration that made it. Triggers hang on the SOURCE table and only mention the derived one by
  name, so dropping the derived table does not take them with it; they must be named down
  explicitly, from the spec that created them.

**Why two.** A trigger body is imperative and diverges between engines in STRUCTURE, not spelling
(Firebird must `MERGE` where PostgreSQL and SQLite `ON CONFLICT`; T-SQL fires once per statement
over pseudo-tables instead of once per row). Expressing that inside the query IR would mean either
imperative nodes (`RETURN` / `IF` / `NEW` / `OLD`) in it, or a raw-SQL escape hatch — and the hatch
would dissolve L2-1's "never raw SQL" invariant for *every* caller. A second renderer with a second
dictionary keeps both halves honest.

**What they share.** Period truncation (`m_periodTrunc`) lives in the QUERY dictionary and is
spelled by the L2-1 node `ibQueryExprKind::PeriodTrunc` — grouping by month is wanted far beyond
totals (reports, the composer, user queries), so it is not a private trick of the trigger
generator. L2-2 reads the same map, which is why a trigger's stored key and a view's projected
column are the SAME expression by construction rather than by two authors agreeing.

L3-2 does not render: it DECLARES what it wants (`ibSchemaMaterialize`), lowers that to an
`ibMaterializeSpec` — expanding logical columns into physical fields, which is the metadata step —
and APPLIES what comes back.

### The shared technical floor (between L2 and L3) — `query/`
Not a level of its own: the **contracts every floor speaks**. `ibBackendQueryProvider`
(`query/queryProvider.h`) is literally *"the whole L3↔L2-1 layer"* — the engine a source vends that
turns the door's calls into L2-1 IR. Alongside it:
- **`ibBackendQueryable`** (`query/queryable.h`) — the source-navigation contract L3 reads a
  metaobject through (physical table, keyset tail, reference/attribute materialisation, virtual
  tables). Implemented by the two families (`…RecordDataRef`, `…RegisterData`); a pure mixin,
  **no data**, second base so `ibValue` stays at offset 0.
- **`ibBackendQueryColumn`** (`query/queryColumn.h`) — the column counterpart.
- **`ibQueryAst`** (`query/queryAst.h`) — the expression AST **shared by L4-1 and L4-2** (LINQ
  lowers a lambda body into the same AST the text parser produces).
- **`ibTempSourceScope`** (`query/queryable.h`) — the thread-local, per-query registry for
  transient queryables (RAM / temp tables). **L5 registers at L4, L4 resolves directly, L3 needs
  nothing** — the temp-table seam.

### L3 — the source-agnostic door (`ibDataQueryBuilder`, `query/dataQueryBuilder`)
Reads **every family** (catalog / document / register / constant / tabular) through the queryable,
backing-blind (a DB cursor **or** a RAM table, chosen once in `MakeProvider`). Four sub-floors:

| Sub-floor | Is | Feeds |
|---|---|---|
| **L3-1** | **the door** — read + write-core (`Upsert`/`DeleteByKey`/`WriteRow`) + aggregation (`SelectAggregate`/`Having`) + **dot-walk** (`SelectPath` auto-joins a reference path) | queries, up to L4/L5 |
| **L3-2** | **structure** — `ibStructureBuilder` / `DiffSnapshots` / `ibSchemaBuilder` generate & migrate the **tables** (DDL) | **metadata** |
| **L3-3** | **data mover** — `ibDataMover` (`query/dataMover`) dumps / restores the **rows** | **metadata** |
| **L3-4** | **regeneration** — `ibDerivedState` (`query/derivedStateBuilder`) rebuilds a **derived** table from its source | derived state |

**L3-4 composes, it does not invent.** Regeneration is `read the source grouped by the totals key,
write the aggregate back` — the L3-1 door's aggregate read plus its write core, no new query
primitive. It groups the period by `PeriodTrunc` through the SAME dialect map the maintenance
trigger keys rows with, so a rebuilt row lands on the identical key the trigger would have
produced; two separate notions of "start of the month" would silently split rows the trigger had
merged. The aggregate runs server-side, so what crosses to the client is one row per KEY, not per
movement.

It runs when the trigger cannot have kept up — a table created over an already-populated source, a
restore, or a change to the grouping shape. `NeedsRegeneration` keeps the common case free: a
column merely ADDED has no history, so its correct value everywhere is the zero the ALTER default
already wrote. Everything else rebuilds, because skipping a needed rebuild yields silently wrong
totals while a needless one only costs time.

**The derived bit routes all four.** A table a metaobject contributes is classified source or
derived (`ibSchemaTable::m_derived`). One bit says three things: L3-2 builds it *and* its
maintenance bundle — and, when the table VANISHES from the target, tears that bundle down before
dropping it; L3-3 **never** moves it (the destination regenerates it exactly, so carrying it
would ship a redundant copy at best and a stale one at worst); L3-4 rebuilds it when the trigger
cannot have kept up. Three actors, three verbs, and conflating them is the seam that rusts — the
**trigger updates** (per movement, in the DB, unbypassable), **L3-4 regenerates** (on discrete
events, invoked, never per movement), **L3-2 builds structure**.

**L3-2 and L3-3 feed the metadata:** both are driven by the metaobject's `ContributeTables`
snapshot — one declaration on the node produces both the DDL (L3-2) and the row round-trip (L3-3).
See [metadata-lifecycle.md](metadata-lifecycle.md) §2, §6.

**When the stitch stays in RAM, the reads still shrink.** A multi-source tree that cannot co-locate
falls to the RAM stitch (materialise each leaf, join in C++). There the composer passes information
SIDEWAYS: the side it materialises first has known join-key values, which it pushes into the other
side's read as `key IN (…)`, so that leaf fetches only rows that can possibly join. It is a pure
reduction — the answer cannot change, only the read shrinks. The side that drives is chosen to shrink
the expensive one (a computed/RAM source is read first; it is built in memory anyway), and the
direction is a correctness gate on a LEFT join, never a preference. See
[query-language-arc.md — Update 2026-07-27](query-language-arc.md).

**Derived-state regeneration is *not* a fourth query floor.** Rebuilding a derived table (register
`totals_X` from its source `mov_X`) is a **compute-server job** that *composes* L3-2 (drop/recreate
the totals bundle) with a bulk aggregate transfer (an L3-1 `SelectAggregate` read into a bulk write)
— orchestration (chunked, resumable, under exclusive), not a new query primitive. The `ContributeTables`
snapshot classifies each table **source** or **derived**, and that one bit routes all three floors:
L3-2 builds both, L3-3 moves **source only**, the regeneration job rebuilds **derived only**. Steady-
state per-movement maintenance is a DB trigger, never a floor. See
[register-totals-strategy.md § Engine integration](register-totals-strategy.md).

### L4 — the authoring tiers (each has a human writing something)
Three front-ends, all lowering into the **one** L3 door — L4 **lives on L3** (L3 is its parent):

- **L4-1 — text query language** (`query/queryLexer|Parser|Lowering`): the greenfield query text
  → AST → lowering. The foundation the others reuse.
- **L4-2 — LINQ push-down** (`compiler/lambdaQueryAst`, `ibValueQueryable`, the `Data.*` root):
  a script `.Where(...).Join(...)` lambda body is re-parsed into a ready **L4-1 `ibQueryAstExpr`**
  and lowered through the same L4-1 builders — **no second engine**. See [linq.md](linq.md).
- **L4-3 — access-policy RLS** (`OnAccessRead`/`OnAccessWrite` on the Role): **not a source** — a
  **decorator** over any query (text / LINQ / composer), enforced on the L3 door. This is the
  coherence dividend: RLS is free because there is one door to gate. See
  [access-policy-rls.md](access-policy-rls.md).

### L5 — the declarative composer (`composition/`)
Holds settings (source + filter + sort + group + TOTALS BY) and produces a query. Two realisations:
- **L5-1 — DB** (`ibDataDBComposer`): renders the settings into **L4-1 query text**, then the
  standard parse → lower → walk. *"L5 is a text renderer for L4-1."*
- **L5-2 — RAM** (`ibDataRamComposer`): **self-contained** — filters/sorts the live rows in place,
  **no tie to L5-1 / L4-1**. This is the RAM side of the table model.

See [data-composer.md](data-composer.md); the two composers are the DB/RAM split behind
`RunComposerPage` in [table-model.md](table-model.md).

---

## 3. How the floors connect (the "one house" wiring)

- **One door, many front-ends.** L4-1, L4-2, and both L5 composers all end at the **single** L3
  door. Add a query front-end by lowering into L3 — never by touching L2-1 / the drivers.
- **L4 sits on L3.** The L4 tiers are authoring surfaces *over* L3; L3 is the parent they lower into.
- **L4-3 decorates, it does not source.** L4-1/L4-2/L5 *produce* queries; L4-3 *wraps* any of them
  on the L3 door — so RLS composes with everything above it for free.
- **L5-1 renders to L4-1.** The DB composer emits L4-1 text; L5-2 (RAM) is independent.
- **Temp tables ride the L4 seam.** A RAM/temp source has no registered name, so L5 registers it in
  `ibTempSourceScope` at the L4 resolve step; L4 resolves it directly; **L3 runs it unchanged** (it
  already IS a complete L3 queryable).
- **L3-2 / L3-3 / L3-4 all read ONE declaration.** The structure builder, the data mover and the
  regenerator are driven by the node's `ContributeTables`: the same declaration produces the schema,
  moves the rows, and rebuilds what is derived from them. Three consumers, one source of truth —
  which is what stops a trigger, a view and a rebuild from each having their own idea of the shape.

---

## 4. Reading order

Start here (the floor plan), then drop to the floor you need: L1 → [database-layer.md](database-layer.md),
L2-1/L3/L4-1/L4-2 → [query-language-arc.md](query-language-arc.md), L4-3 → [access-policy-rls.md](access-policy-rls.md),
L5 → [data-composer.md](data-composer.md), the temp seam → [temp-db.md](temp-db.md), and who consumes
a fetched page → [table-model.md](table-model.md).
