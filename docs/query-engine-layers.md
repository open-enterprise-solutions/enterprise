# Query engine — the floor plan (L1–L5)

> **Scope:** the one thing to read FIRST about the query engine — the **layer taxonomy**. L1–L5
> are floors of **one building**; L3 and L4 each have sub-floors; L4 sits **on** L3; a shared
> technical floor bridges L2 and L3. This map exists so anyone (human or AI) touching the query
> arc knows how the pieces relate before diving into a single-floor doc. A MAP of code that
> **already exists.**
>
> **Companions (per-floor detail):** [query-language-arc.md](query-language-arc.md) (the full
> arc — L2 IR, L3 door, L4-1/L4-2), [data-composer.md](data-composer.md) (L5),
> [linq.md](linq.md) (L4-2), [access-policy-rls.md](access-policy-rls.md) (L4-3),
> [database-layer.md](database-layer.md) (L1), [temp-db.md](temp-db.md) (the temp-table seam),
> [table-model.md](table-model.md) (who consumes a fetch), [metadata-lifecycle.md](metadata-lifecycle.md)
> (what L3-2 / L3-3 feed).
>
> **Status:** landed (experimental working copy) — the whole ladder is in the tree.

---

## 1. One building, five floors

```
  L5  composer            L5-1 DB (renders → L4-1 text)   |  L5-2 RAM (self-contained)
       │  renders/settles ▼
  L4  authoring tiers     L4-1 text query  ·  L4-2 LINQ push-down  ·  L4-3 RLS (decorator)
       │  all lower into the ONE L3 door ▼
 ┄┄┄┄┄ shared technical floor: ibBackendQueryProvider ("the whole L3↔L2 layer") ┄┄┄┄┄┄┄┄┄┄
       │  + ibBackendQueryable / ibBackendQueryColumn / ibQueryAst / ibTempSourceScope
  L3  source-agnostic door  L3-1 the door (read/write/aggregate/dot-walk)
                            L3-2 structure (DDL)  ·  L3-3 data mover (rows)   → feed metadata
       │  builds IR ▼
  L2  ibDatabaseQueryBuilder — structured ibQueryIR, dialect-driven renderer (never raw SQL)
       │  ▼
  L1  ibDatabaseLayer ×5 drivers — each vends its ibDialectDictionary
```

The rule of the house: **one door down, many front-ends up.** Everything above L3 (the three L4
authoring tiers, both L5 composers) funnels through the **single** L3 door; L3 renders to L2 IR;
L2 renders to a driver's dialect at L1. No front-end reaches the drivers directly.

---

## 2. Floor by floor

### L1 — drivers (`ibDatabaseLayer` ×5, `databaseLayer/`)
Firebird / PostgreSQL / SQLite / MySQL / ODBC. Each vends its own `ibDialectDictionary`
(`GetDialect()`) — **zero central type-switch**. See [database-layer.md](database-layer.md),
[connection-pool.md](connection-pool.md).

### L2 — the IR builder (`ibDatabaseQueryBuilder`, `databaseLayer/databaseQueryBuilder`)
A structured **`ibQueryIR`** (never raw SQL) with a full vocabulary — Scan/Filter/Project/Sort/
Limit/Join/Aggregate/Subquery/Distinct/Union, expressions, DDL, DML — rendered generically through
the dialect dictionary.

### The shared technical floor (between L2 and L3) — `query/`
Not a level of its own: the **contracts every floor speaks**. `ibBackendQueryProvider`
(`query/queryProvider.h`) is literally *"the whole L3↔L2 layer"* — the engine a source vends that
turns the door's calls into L2 IR. Alongside it:
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
backing-blind (a DB cursor **or** a RAM table, chosen once in `MakeProvider`). Three sub-floors:

| Sub-floor | Is | Feeds |
|---|---|---|
| **L3-1** | **the door** — read + write-core (`Upsert`/`DeleteByKey`/`WriteRow`) + aggregation (`SelectAggregate`/`Having`) + **dot-walk** (`SelectPath` auto-joins a reference path) | queries, up to L4/L5 |
| **L3-2** | **structure** — `ibStructureBuilder` / `DiffSnapshots` / `ibSchemaBuilder` generate & migrate the **tables** (DDL) | **metadata** |
| **L3-3** | **data mover** — `ibDataMover` (`query/dataMover`) dumps / restores the **rows** | **metadata** |

**L3-2 and L3-3 feed the metadata:** both are driven by the metaobject's `ContributeTables`
snapshot — one declaration on the node produces both the DDL (L3-2) and the row round-trip (L3-3).
See [metadata-lifecycle.md](metadata-lifecycle.md) §2, §6.

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
  door. Add a query front-end by lowering into L3 — never by touching L2 / the drivers.
- **L4 sits on L3.** The L4 tiers are authoring surfaces *over* L3; L3 is the parent they lower into.
- **L4-3 decorates, it does not source.** L4-1/L4-2/L5 *produce* queries; L4-3 *wraps* any of them
  on the L3 door — so RLS composes with everything above it for free.
- **L5-1 renders to L4-1.** The DB composer emits L4-1 text; L5-2 (RAM) is independent.
- **Temp tables ride the L4 seam.** A RAM/temp source has no registered name, so L5 registers it in
  `ibTempSourceScope` at the L4 resolve step; L4 resolves it directly; **L3 runs it unchanged** (it
  already IS a complete L3 queryable).
- **L3-2 / L3-3 feed metadata.** The structure builder and the data mover are driven by the node's
  `ContributeTables` — the same declaration produces the schema and moves the rows.

---

## 4. Reading order

Start here (the floor plan), then drop to the floor you need: L1 → [database-layer.md](database-layer.md),
L2/L3/L4-1/L4-2 → [query-language-arc.md](query-language-arc.md), L4-3 → [access-policy-rls.md](access-policy-rls.md),
L5 → [data-composer.md](data-composer.md), the temp seam → [temp-db.md](temp-db.md), and who consumes
a fetched page → [table-model.md](table-model.md).
