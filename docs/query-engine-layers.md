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
  L1  ibDatabaseLayer ×4 drivers — each vends its ibDialectDictionary
```

The rule of the house: **one door down, many front-ends up.** Everything above L3 (the three L4
authoring tiers, both L5 composers) funnels through the **single** L3 door; L3 renders to L2-1 IR;
L2-1 renders to a driver's dialect at L1. No front-end reaches the drivers directly.

---

## 2. Floor by floor

### L1 — drivers (`ibDatabaseLayer` ×5, `databaseLayer/`)
Firebird / PostgreSQL / SQLite / ODBC. Each vends its own `ibDialectDictionary`
(`GetDialect()`) — **zero central type-switch**. See [database-layer.md](database-layer.md),
[connection-pool.md](connection-pool.md).

### L2 — the renderers (`databaseLayer/`) — TWO halves
L2 is "a structured description in, dialect-spelled SQL out". It has two halves, each with its own
dictionary, and neither knows anything about metadata:

- **L2-1 — the query IR** (`ibDatabaseQueryBuilder` / `ibQueryRenderer`, `databaseQueryBuilder`).
  A structured **`ibQueryIR`** (never raw SQL) with a full vocabulary — Scan/Filter/Project/Sort/
  Limit/Join/Aggregate/Subquery/Distinct/Union, expressions, DDL, DML — rendered generically
  through `ibDialectDictionary`.

  **A NAMED QUERY rides on the STATEMENT** (added 2026-08-21): `ibQueryIR::m_with` is a list of
  `ibQueryCte { name, query }`, and the renderer writes them ahead of the select — `WITH a AS (…),
  b AS (…) SELECT …` — which is also what puts their bind parameters first, in the order the driver
  binds them. Declaration ORDER is the caller's (an engine reads the list top to bottom). A dialect
  without CTEs is refused with a sentence, never rewritten into nested sources: that is a different
  tree, and choosing it belongs to the tier that built the query. The consumer is a package's named
  result — see query-language-arc.md § 24.4a.

  **Window functions ride as a FIELD on a Func**, not as a node kind of their own (added
  2026-08-20). `ibQueryExpr::m_over` holds an `ibQueryWindow { partitionBy, orderBy, frame }`, and
  the renderer appends the clause where the call ends — the same shape `m_distinct` already had,
  and for the same reason: the functions are the ordinary `SUM` / `COUNT` / `MIN` / `MAX` (plus
  the ranking ones), and only what they fold over changes.

  Two things are deliberate here. The **frame is an explicit choice** — `ibQueryFrame` offers
  `RangeThroughPeers` (every row sharing this row's sort key contributes, which is what a running
  BALANCE wants) and `RowsThroughCurrent` (strictly row by row, valid only where the sort key is
  unique by construction), with **no `Default` member**, because the three engines' unstated
  defaults are not required to agree and a divergence would travel silently into somebody's
  figures. `NoFrame` means a ranking call (SQL forbids a frame there) or an unordered partition —
  a share-of-total denominator. And the **clause has one speller**, `ibRenderOverClause`, which
  both this renderer and L2-2's view generator call; that function is also where the capability is
  checked, refusing with `UnsupportedNode` on an engine without windows rather than emulating one
  (every emulation turns a linear report quadratic, and a query that merely got slower is not a
  failure anyone reports).

  Support: Firebird 3+, PostgreSQL, SQLite 3.25+ — all three advertise `m_features.m_window`. The
  ANSI baseline (what ODBC gets, and what an MSSQL layer derives from) does not, and is refused.

  A write can also hand back what it wrote: `ibReturning(dml, {cols})` +
  `ExecuteReturning` yields a **cursor over the affected rows**, exactly like a SELECT. This is
  what makes "bump a counter and read the new value" ONE statement, hence atomic — no window
  between a read and a write for a concurrent session to slip into. The spelling is the dialect's
  (`m_returningClause`: Firebird, PostgreSQL, SQLite 3.35+); a driver without one **throws**
  `UnsupportedNode` rather than emulate it, because write-then-SELECT loses the atomicity that
  was the entire point. `GenerateNextIdentifier` (`sys_sequence`) is the first tenant — it was
  the last raw-L1 holdout on the write path, kept there precisely because RETURNING had no L2
  form.
  **Several rows, two spellings** (2026-08-15). `ibDmlStatement::m_extraRows` carries the rows past
  the first; how they reach the server is decided here, by `m_features.m_multiRowValues`:

  ```sql
  -- PostgreSQL / SQLite
  INSERT INTO t (c1, c2) VALUES (?, ?), (?, ?), (?, ?)
  -- Firebird: no multi-row VALUES at ANY version, but it does have UNION ALL
  INSERT INTO t (c1, c2)
       SELECT CAST(? AS TYPE OF COLUMN t.c1), CAST(? AS TYPE OF COLUMN t.c2) FROM RDB$DATABASE
    UNION ALL SELECT CAST(? AS TYPE OF COLUMN t.c1), CAST(? AS TYPE OF COLUMN t.c2) FROM RDB$DATABASE
  ```

  The `FROM` comes from the same `m_selectFromDual` the source-less SELECT already uses. A caller
  says `m_extraRows` and never learns which engine it is talking to — a second SPELLING, not a
  second mechanism. The slot pre-dates this (the temp-table manager bulk-fills through it); giving
  it the Firebird form is what let the L3 write door batch at all, and made temp bulk-fill work on
  Firebird as a side effect.

  ⚠ **THE PLACEHOLDERS HAVE TO SAY WHAT THEY ARE** (`m_batchInsertCast`, added 2026-08-16 after the
  first live write failed). In `VALUES (?)` a parameter takes its type from the target column; in
  `SELECT ? FROM …` it does NOT, because the SELECT is typed on its own first. Firebird refuses to
  prepare — `SQL error code = -804 / Data type unknown` — and every branch is typed independently:
  casting only the first branch does not help, and neither does seeding the union with a zero-row
  `SELECT … FROM t WHERE 1=0`. Both were tried against a live engine and both still fail. So every
  placeholder in every branch carries its own cast, and it casts to `TYPE OF COLUMN` so the ENGINE
  looks the type up rather than the renderer holding a second opinion about the schema. Engines that
  need nothing leave the slot empty and render the placeholder bare.

  The cost is width: ~50 characters per placeholder. The widest thing that writes this way — 21
  columns × the 50-row chunk — measures **56 548 characters and 1050 parameters, and prepares**, so
  no chunk cap was added. `tests/test_firebirdBatchInsert.cpp` holds all of it (the four spellings
  and the width) against a real Firebird, and SKIPS where no client exists.

  🛑 **Why this reached a user.** The form is Firebird's, and CI runs SQLite — which HAS multi-row
  VALUES and therefore never renders it. The only coverage was a TEXT assertion against the SQLite
  dialect. A spelling that exists for one engine has to be executed on that engine or it is not
  tested at all.

  🛑🚪 **AND THE BATCH WENT UNDER THE DOOR, NOT THROUGH IT** (fixed the same day, and the -804 above
  was only hiding it). A row's values are bound by `BindWriteValue`, which has a branch of its own
  for a **RAW** column — the parent row key a tabular section puts on every line: one field, bound
  straight by its declared `RawType`. Underneath that door sits `ibColumnCodec::WriteValue`, which
  serves METADATA columns and whose first act is always a `_TYPE` discriminator. The batch's value
  capture called the codec directly, so the raw key produced TWO values — the tag, then the key —
  and the loop that pairs values with field names, seeing one field, kept the tag.

  A number then went to a `CHAR(16)` key column. Firebird refused the bind, but the driver only
  **logged** the refusal, so the statement ran anyway and the caller was told it had succeeded: a
  tabular section that saved cleanly and reopened EMPTY, from the second row on — one row never
  reaches this path. Three things were changed, and two of them are about the silence:

  - the batch binds through `BindWriteValue`, the same door as the single row;
  - the value/field pairing RAISES on a count mismatch instead of padding and truncating — a column
    that yields a different number of values than it declares fields does not misplace its own
    value, it shifts every later value into somebody else's column;
  - `ibDatabaseParameterFirebird` raises on a refused bind (naming the SQL type the statement
    declared) instead of `wxLogError`. Its neighbouring guards already did; that one did not, and it
    is what turned a wrong bind into a write that reported success and stored nothing.

  ⭐ The general shape, worth recognising elsewhere: **a second path that reproduces a door's insides
  instead of calling the door.** It works for every case the door handles trivially and fails for the
  one case the door exists for.

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

⭐ **The dialect FACTS the declaration needs are asked THROUGH this floor too, never read off a
dictionary from above.** `ibIndexFieldCapacity` / `ibKeyNeedsHash` (`databaseMaterializeBuilder.h`)
answer *how many physical fields one index may cover here* and *is this key past that* out of
`ibDialectDictionary::m_maxIndexSegments` (Firebird 16, PostgreSQL 32, SQLite none). L3-2
must know before it emits anything, because the answer is a COLUMN — a key past the ceiling carries
its uniqueness in one hashed field instead — but a dialect is L2's to read, and the floor above has
no business knowing one exists. See [register-shared-machinery.md](register-shared-machinery.md) §4a.

### The shared technical floor (between L2 and L3) — `query/`
Not a level of its own: the **contracts every floor speaks**. `ibBackendQueryProvider`
(`query/queryProvider.h`) is literally *"the whole L3↔L2-1 layer"* — the engine a source vends that
turns the door's calls into L2-1 IR. Alongside it:
- **`ibBackendQueryable`** (`query/queryable.h`) — the source-navigation contract L3 reads a
  metaobject through (physical table, keyset tail, reference/attribute materialisation, virtual
  tables). Implemented by the two families (`…RecordDataRef`, `…RegisterData`); a pure mixin,
  **no data**, second base so `ibValue` stays at offset 0.
- **`ibBackendQueryColumn`** (`query/queryColumn.h`) — the column counterpart. Its header is
  deliberately **light** (one include, `typeDescription.h`) so the fundamental attribute metaobject
  can derive from it without dragging `queryable.h` / `tabularModel.h` in.

  ⚠ **A light header stays light only while somebody checks** (2026-08-14). It had grown
  `#include <wx/icon.h>` — three lines under the comment calling itself light — for the single return
  type of `virtual wxIcon GetColumnIcon() const`. In a DLL that is supposed to be GUI-free, reached
  by 21 direct includers and everything behind them, for one declaration. It is a **forward
  declaration** now (`class wxIcon;`): a virtual returning a class type compiles fine against an
  incomplete type, and only the two files that DEFINE such a body need the real header
  (`metaCollection/attribute/metaAttributeObject.h` and `…_res.cpp`), which they already included.
  The cost of the mistake is not correctness, it is that nothing fails — a heavy header compiles
  exactly like a light one, only slower and with the GUI boundary quietly crossed.
- **`ibQueryAst`** (`query/queryAst.h`) — the expression AST **shared by L4-1 and L4-2** (LINQ
  lowers a lambda body into the same AST the text parser produces).
- **`ibTempSourceScope`** (`query/queryable.h`) — the thread-local, per-query registry for
  transient queryables (RAM / temp tables). **L5 registers at L4, L4 resolves directly, L3 needs
  nothing** — the temp-table seam.
- **The column-layout tier** (`query/columnLayout.h`) — how ONE logical column spreads into physical
  fields, and the only place the lettering exists. `DescribeColumnLayout(col)` returns the slots in
  bind order — each slot a `{name, role, type}` — and `ibFieldSuffix(role)` is the single role →
  suffix table (`_TYPE`, `_N`, `_RTRef`, …). Metadata-free: the reference pair is gated by the clsid
  KIND, not a metadata lookup.

  ⭐ **The row key is the same sixteen bytes a reference key is** (2026-08-11). It used to be a
  guid's TEXT — `VARCHAR(36)` on every row of every table and in every index over them — while a
  reference stored the identical guid as `_RRRef BINARY(16)`. Two spellings of one identity could
  never be compared, so a tabular section's link to its owner, a dot-walk and a hand-written join
  had nothing to meet on. Now `ibRowKeyColumn()` / `ibRowKeyField()` name it once (`Row` + the
  reference-id suffix, spelled through the same table), the raw codec binds and reads it as bytes,
  and `uuid = <ref>_RRRef` is a real binary compare. The name was previously typed out at nine call
  sites — six of them with the WRONG kind beside it (`String`, while every declaration said `Guid`),
  which is what a repeated name costs.

  ⭐ **The role travels with the name.** A consumer that must know what a field IS — a reference id
  needs its `_RTRef` clsid as an ordering tiebreak, a counter scan wants the number field, a
  composite predicate compares tag fields with `=` whatever the caller asked for — asks
  `ColumnValueSlots` / `FirstValueSlot` and reads `m_role`. Handing out names alone left those
  consumers only one way to ask the question: match the tail of the string
  (`field.EndsWith("_RRRef")`), or rebuild the suffix from the type — the layout re-derived in a
  place that could never be told when the lettering changed. **The lettering is spelled in
  `ibFieldSuffix` and nowhere else**; a `grep` for a bare `"_RRRef"` outside this tier is a defect
  report.

  ⭐ **A stored tag is checked against the layout before it is believed** (2026-08-15). `_TYPE` decides
  which sub-field the read then names, and which sub-fields EXIST is decided by the column's type —
  two facts that must agree and, once a row is written, can no longer be made to. So `ReadValue`
  asks `TagFitsColumn` first (the same `ContainType` / `HasReference` gates the slots are built
  from) and a tag the column cannot carry reads as the column's TYPED EMPTY. Rows already written
  under a wrong tag stay readable instead of having to be rewritten. What produced them: a value
  whose type is `TYPE_VALUE` — an enumeration with no member chosen — fell into `TagForValue`'s
  "not a primitive, therefore a reference" arm and was stored as `Reference`; the next read asked
  the driver for `<fld>_RTRef`, an enum column has none, and the whole list page died on one cell.

  ⭐ **"I have no such value" is an answer, not a catastrophe.** Only one fault degrades:
  `DATABASE_LAYER_FIELD_NOT_IN_RESULTSET` makes `ReadValue` return `false` with the column's typed
  empty in hand, and the CALLER decides — a portion read swallows it and keeps the other rows, a
  write or a targeted read lets it travel. A dropped connection or a driver failure is a fault of
  the READ, not of the cell, and still propagates: degrading those would paint blank rows over a
  database that stopped answering. Costs nothing on the normal path — a handler entered only when
  the fault happens, and the ordinary form of it never reaches the handler (the tag check above).

  ⚠ **An enum column's DDL default is `emptyEnum`, not `0`** (2026-08-15). Members are numbered by
  their declaration and `0` is an ordinary member, so a column defaulting to it hands every row that
  predates the column a member as though someone had chosen it — visible exactly when a perceptible
  amount of data already exists (`ALTER TABLE ADD COLUMN` applies the default to every stored row).
  Note the diff does NOT compare `m_default` (`DiffColumnInto` returns early unless the TYPE set
  changed), so existing columns keep whatever default they were created with; the new value reaches
  newly created columns only.

  ⚠ **A raw column may declare its own WIDTH, and the tier no longer guesses a type.**
  `ibRawDBColumn::String(field, id, length)` / `Number(field, id, precision, scale)` carry it, and
  `columnLayout.cpp`'s `RawType` uses what the column said, falling back to the old defaults
  (`VARCHAR(255)`, `NUMERIC(18,0)`) only where it said nothing. Both defaults were live defects while
  this ignored them: a `VARCHAR(255)` digest column is 1020 bytes in UTF8 and passes Firebird's index
  KEY SIZE ceiling before a row exists, and a flat `NUMERIC(18,0)` under a resource declared with a
  scale drops the fraction on the way in. The same function used to ask RTTI and, on a miss, return
  `VARCHAR(255)` — a wrong-but-valid type that goes straight into a CREATE TABLE, i.e. a migration
  discovered months later by arithmetic. It now asserts the caller's own `IsRawColumn()` guard and
  static-casts: what a column IS is the column's answer, never a cast's.

### L3 — the source-agnostic door (`ibDataQueryBuilder`, `query/dataQueryBuilder`)
Reads **every family** (catalog / document / register / constant / tabular) through the queryable,
backing-blind (a DB cursor **or** a RAM table, chosen once in `MakeProvider`). Four sub-floors:

| Sub-floor | Is | Feeds |
|---|---|---|
| **L3-1** | **the door** — read + write-core (`Upsert`/`DeleteByKey`/`WriteRow`) + aggregation (`SelectAggregate`/`Having`) + **dot-walk** (`SelectPath` auto-joins a reference path) | queries, up to L4/L5 |
| **L3-2** | **structure** — `ibStructureBuilder` / `DiffSnapshots` / `ibSchemaBuilder` generate & migrate the **tables** (DDL) | **metadata** |
| **L3-3** | **data mover** — `ibDataMover` (`query/dataMover`) dumps / restores the **rows** | **metadata** |
| **L3-4** | **regeneration** — `ibDerivedState` (`query/derivedStateBuilder`) rebuilds a **derived** table from its source | derived state |

**THE READ IS TWO STEPS, AND THE SECOND IS A CURSOR OVER ONE LEVEL.** `Execute(page)` hands back the
RAW data (`ibDataQueryResult`); `result.Select(kind)` hands back a TRAVERSAL over it (`ibSelector`,
`query/querySelector.h`). `kind` is `ibSelectKind` — `Direct` (the rows as they are) / `ByGroups` /
`ByGroupsHierarchy` — and it says HOW to walk, never what to read: one read is one snapshot and every
subtotal rolls from that snapshot, so a detail row and the total above it cannot disagree.

```cpp
ibSelector s = result.Select(ibSelectKind::ibSelectKind_ByGroups);
while (s.Next()) {                                                   // THIS level, and no other
    s.GetValue(col); s.Level(); s.Kind();                            // Group or Detail — asked of the NODE
    ibSelector under = s.Select(ibSelectKind::ibSelectKind_ByGroups);  // descend INTO the visit
    while (under.Next()) { … }
}
```

**A selection walks ONE level** (2026-08-22). `Next()` visits the direct children of the node the
selection was made over, and what hangs under a visit is reached by descending into it — so **three
groupings are three nested loops**, which is what a grouping is. It used to flatten the whole subtree
pre-order into a single cursor: a reader could tell one level from another only by asking `Level()`
on every visit, and code written the way the language reads (loop the groups, loop their rows) saw
the rows twice, once mixed into the outer loop. A `Direct` walk is unaffected — its tree is one flat
layer of leaves under the root, so its direct children ARE the rows.

**`Select(kind)` descends into what is already in hand.** The fold built the children, so a descent
hands the current node's subtree over as a tree of its own (`ibSelectorTree::SubtreeOf` →
`WithReadyTree`): no query, no re-fold, and it works for a fold with no live source behind it, which
is the ordinary case for a scripted `TOTALS`. The RE-EXECUTION road stays for the one thing it was
built for — a **lazy drill**, where a node is expandable but its children were deliberately not read:
`MakeChild` re-runs the source scoped to the node's key with the ORIGINAL filter inherited, so a
sub-selection shows the node's children under the same condition the first read admitted.

Two invariants on the cursor, both learned from readers taking `Next()` literally:

| invariant | why |
|---|---|
| a visit never has NO node — an empty slot is skipped | `Next()` answers *is there a row here*, and the caller reads columns straight after; a node-less visit would report a row made entirely of NULLs |
| a refusal does not MOVE the cursor | `false` means *there is no next row*, not *you are now nowhere* — an exhausted selection keeps standing on the last row it returned, so reading a column after the loop gives that row's values |

`Reset()` rewinds the same tree (no re-query, no re-fold). `WalkOverall()` puts the tree's ROOT in
the walk as **the level that groups by nothing** — one node, aggregates filled, every dimension
column NULL — after which the first dimension level is reached the way every level is, by descending
into the one above it. The tree may also arrive ALREADY BUILT, when the DBMS folded it
(`WithReadyTree`, `GROUP BY ROLLUP`); the walk over it is identical, which is the point.

**ONE ANSWER, ONE STATE OF THE DATA** (`query/queryReadState.h`) — ⛔ **built, and opened by nothing
since 2026-08-23**: the design first, the switch-off below it. What an author writes as ONE query is
several statements down here: a join the server would not take is stitched from two reads, a
subquery is promoted to a DB temp table and read back, a page is fetched at a time,
and every printed reference fetches its own row. Under read-committed each of those reads whatever
has committed by the moment it starts, so records written while an answer is being drawn land in some
parts of it and not others — and nothing in the result says which parts.

`ibQueryReadState::Open()` hands back a `shared_ptr` holding a transaction opened with
`ibDbTxOptions::snapshot`, or **null** when there is nothing to open: a transaction is already
active, or there is no session at all. Null is not a failure and needs no branch — a null holder
simply holds nothing. When the last holder releases it, the destructor commits.

**It was `ibQueryReadSnapshot` (`query/queryReadSnapshot.{h,cpp}`) until 2026-08-23.** The word was
taken twice here already — the L3 flat RAM materialisation, and configuration-compare's
`DiffSnapshots` — so a third meaning read as one of the other two. The name now says what the object
holds: the state of the data one answer is read in.

⭐ **IT IS HELD, NOT SCOPED TO A CALL** — the reason it is an object rather than a guard at the top of
the L3 execute functions. A query does not FINISH when its execute returns: `ExecuteRead` hands back
a LIVE cursor and the caller draws rows from it afterwards. The guard version was written first and
killed the cursor its own result depended on — measured as Firebird `-504, cursor is not open` on the
first row of the first query. So the read state travels WITH the result. Four read doors declare a
holder for it — ⛔ all four unopened today — and the shape of each holder is set by who outlives the
cursor:

| door (holder declared, not opened) | shape |
|---|---|
| `ibDataDBComposer::Run` (`composition/dataComposer.cpp`) | a local: a build draws its rows before it returns |
| `ibValueQueryExec::RunPackage` (`system/value/valueQuery.cpp`) | before the first statement of the package; each `ibValueQueryResult` takes a share and `Select()` passes it on to `ibValueQuerySelect`, so the transaction would end when the last reader let go |
| `ibValueQueryable::DispatchLinqMethod` (`system/value/valueQueryable.cpp`) | a local for the terminal, plus a share on `ibQueryableIteratorState` — a `Foreach` draws rows after the pipeline returned; that member is declared BEFORE the cursor, so it would open first and close last |
| `ibValueQueryDecorator::DispatchLinqMethod` (`system/value/valueQueryable.cpp`) | the same, one door lower: whichever door is entered first opens it and the inner one finds it already open |

**It defers to a transaction already open.** `Open()` returns null when one is active — the caller's
window is wider and gives the guarantee instead. That is how a caller says *every query in this build
reads one state*: open a transaction first, and each query then finds it and adds nothing.

**WRITE mode, though nothing here means to write.** A read is allowed to write on our behalf — a
source computed in RAM is promoted into a DB temp table and filled
(`ibTempTableManager::Materialise`) — and a read-only transaction refuses that, failing a query that
used to work.

⚠ **It is not free**, which is why it belongs to the answer and not to the screen: holding one state
means the server keeps the record versions that state needs. An answer is read and released; a list
left open and scrolled for minutes must NOT hold one, because between its pages the data legitimately
moves. What it does **not** add is the transaction itself — a live cursor already keeps one open
today (with no outer transaction the prepared statement starts and manages its own), so what changes
is the ISOLATION of a transaction that existed either way.

⛔ **AND NOTHING OPENS ONE — 2026-08-23.** Opened at all four doors, the application hung within
minutes of starting. The journal shows a list reading under one of them, and the thread that then
stopped was WRITING — a DELETE and an INSERT into `sys_bytecode_cache`; after that only the session
heartbeat kept logging. The state pins the connection to the session for the length of an answer
(`ibDatabaseLayer::BeginTransaction` → `ibConnectionPool::SetActiveTxConnection`), so an incidental
write on the same session mid-answer meets that WRITE-mode transaction, and the Firebird TPB waits
(`isc_tpb_wait`).

The consistency argument stands; the placement does not. What has to be settled before it is switched
on again: WHICH writes may happen inside a read, and whether they belong in the same transaction or
must be kept out of it. Until then every read reads under whatever transaction it happens to find, as
it did before — and `query/queryReadState.h` carries the long form of this note.

**A DOOR MAY DECLARE A NAMED QUERY** (2026-08-21). `.With(name, innerDoor)` records one; the spec
carries the list; `ibDbTableProvider::AttachNamedQueries` lowers each through the ordinary road (its
own spec through `BuildPageIR`) and puts the resulting relation on the SAME IR. The source that
reads it is `ibCteQueryable` — a NAME in SQL with declared columns, read by the ordinary physical
scan, the third member of the family beside `ibDbTempTableQueryable` and `ibSchemaTableQueryable`.

It is deliberately **not** `ibSubqueryQueryable` with a flag: that one is computed in RAM by
construction (the inner query runs, its rows come back, the join happens here), which is the right
answer for a source the DBMS cannot see and the wrong one for a named result of the same package on
the same connection. Same shape, opposite execution — two classes, not one with a switch.

Both mint columns of their own, so the base queryable grew one question — `ShareColumn(col)`: "did
you mint this, and may I keep it?" A metadata-backed source answers nothing (its columns outlive
every query); a per-run source hands over the storage, so a schema that names one of them keeps it
by a refcount rather than by a copy.

**A WRITE IS A SET OF ROWS, AND ONE ROW IS THE DEGENERATE CASE** (2026-08-15). `SetValue` fills the
row being assembled; `NextRow()` opens another; `Insert()` writes them all. A caller that never calls
`NextRow` behaves exactly as before and pays nothing for the dimension, which is why no existing
callsite changed.

```cpp
q.From(queryable);
for (long row = 0; row < count; row++) {
    if (row > 0) q.NextRow();
    q.SetValue(col, value);   // …the row's columns
}
q.Insert();                   // one statement per chunk, not one per line
```

A register's record set and a document's tabular section write this way; a thousand lines used to
cost a thousand doors, a thousand renders and a thousand round trips.

- **Only a plain INSERT batches.** An UPSERT needs the dialect's own match form (Firebird's
  `UPDATE OR INSERT` takes no SELECT source) and UPDATE/DELETE address rows by key — asking for a
  batch there raises rather than writing the first row and reporting success for N.
- **Under a row policy the batch turns itself off**: WITH CHECK decides per row and answers with a
  count, and folded into a pack "3 of 1000 refused" and "1000 written" become the same number. An
  optimisation may not change who may write what.
- **Rows must name the same columns in the same order** — the bind is positional once the statement
  exists. Checked by FIELD NAME, not by column pointer: a raw column is handed over by value and the
  door owns a copy, so the same logical column staged twice is two addresses.
- **Chunked at 50**, the number the temp-table manager already settled on; a register row is wider
  than a temp row.

How the rows are spelled is L2's business — see § L2, `m_extraRows`. Note what the batch does **not**
change: a `FOR EACH ROW` trigger counts ROWS, so totals maintenance fires exactly as often as before.
Reducing that needs a set-based trigger (`ibTriggerFamily::SetBased`), which the batch is the
prerequisite for — without one statement there is nothing for a statement-level trigger to fire on.

**A TERMINAL AND A NON-TERMINAL ENDING** (2026-08-13). `Execute()` runs the query and hands back
rows; `BuildRelation()` stops one step earlier and hands back the **relation** the same assembly
produced. There are **two** lowerings under it, chosen by the SHAPE of the query exactly as the
terminal endings are (`SelectAggregate()` against `Execute()`): a query naming aggregates or group
keys folds → `BuildAggregateRelation` (`BuildAggregateQuery` → `ibDatabaseQueryBuilder::Build()`);
one naming neither projects → `BuildReadRelation`, which is `BuildPageIR` **with the page taken off**.
That absence is the decision — paging belongs to whoever composes with the relation, and a `LIMIT`
baked in would cap a join's input to one screenful and read as "the table has forty rows". Two
lowerings rather than one with a flag, because a `GROUP BY` and a paged read are different trees.
That is what
lets a source answer `ibBackendQueryable::GetSourceRelation`, so its whole aggregate lands inside
somebody else's `FROM` as a derived table and the join, the filter and the paging over it stay ONE
statement — instead of the result being materialised into RAM before anything can be composed with
it. Two properties keep it honest: it is the **same** lowering as the execute path (a hand-rolled
`ibQueryRel` beside it would be a second way of building the same SQL, free to disagree about a join
or a HAVING), and a query carrying an **access policy** gets `nullptr` — a policy folds its
restriction into the read it guards, and a relation handed out unrestricted could be composed past
it. The first callers are the accounting register's readings
([accounting-register-arc.md](accounting-register-arc.md) § 8.3a).

**A CONDITION THE SOURCE CONSUMES ITSELF** (2026-08-13). An argument slot declared `m_condition` is
sugar: the predicate written there is ANDed into the WHERE around the reading, and the source never
learns of it. That is right while the condition only SELECTS rows, and wrong when the source has to
**act** on it — an accounting register asked for accounts «in hierarchy» reports the subordinates
UNDER the account that was named, and a filter applied around the reading can only remove rows, never
fold them. Under `HIERARCHYONLY` it removes exactly the rows the fold made.

So a slot may declare `m_consumedBySource`. Then the lowering resolves the predicate against the
descriptor's `GetConditionScope()` — the source's stable side, because the call-scoped companion
being built does not exist yet — and hands it to `CreateQueryable(params, size, conditions)` by slot
position **instead of** into the WHERE. Three properties make it safe:

- **the word survives.** `keepUnfold` stops the lowering resolving `IN HIERARCHY` into the subtree it
  stands for: the leaf keeps the values AS NAMED plus `ibQueryCondition::m_unfold`, because a fold
  cannot be reconstructed from twenty expanded values — none of them says which one they roll into.
  A provider must never see such a leaf, and never does: everything on the ordinary road is expanded
  at the lowering and arrives as `Elements`;
- **it is not applied twice.** The condition leaves the WHERE entirely, which is the point;
- **a source that consumes must apply.** A slot marked and then ignored loses its filter silently, in
  the direction of reporting MORE than was asked — so the mark and the source-side reading land
  together or not at all.

The three unfold words live in `query/queryUnfold.h` for this: an L3 condition names them, and
`queryAst.h` stays L2/L3-free.

**A rewrite that hit nothing is a MISS, and the door says so.** `Update()` reports `affected >= 0`,
so nought rows once read as success — and a write keyed on a row that cannot be found is not "zero
rows written", it is a key that did not match. The door now separates the two by what the caller
asked: a rewrite keyed on the PRIMARY KEY alone addresses ONE named row, so nought raises; a caller
that narrowed with `.Where(...)` asked a question about a SET, and an empty set is a legitimate
answer. The distinction is not decoration — the silent version let a form report "saved" while the
object advanced its in-memory `DataVersion` and the row stayed untouched, so the NEXT save accused
another user of a change nobody had made. See `ibDataQueryBuilder::RaiseIfKeyedRewriteMissed`.

**Which config a by-name source resolves through is always the CALLER's answer.**
`ibSourceMetaDataScope` carries it for one execution; `GetFactory()` consults that scope and then
the global base factory, and **nothing process-wide in between** — metaobject sources register into
the factory of the configuration they belong to, so "whichever config is active" is a different
question from "which config does this query mean". Four callers answer it: the composer and the
dynamic list from their own metadata, the constructor from the open one, and a hand-written
`New Query("… FROM Catalog.X")` from the SESSION it runs in (the session was opened for a
configuration and holds it fixed — `GetManagerModule()->GetMetaManager()->GetMetaData()`). One place
asks, so a new caller that forgets fails identically everywhere instead of quietly resolving against
somebody else's configuration.

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

**The door owns the DDL/DML barrier, and the barrier is about a table's SHAPE — not its creation.**
Some engines keep DDL transactional (Firebird; the dialect fact
`ibDialectDictionary::m_ddlCommitBeforeData`), so a statement issued in the transaction that reshaped
a table prepares against the shape the engine can still see. `ibSchemaBuilder::Execute`
(`query/schemaBuilder.cpp`) therefore records EVERY shape-changing statement — CreateTable, AddColumn,
DropColumn, AlterColumn, AlterTable — and `RunOrDefer(table, work)` parks work naming such a table
until after the DDL commit (`ibStructureBuilder::FlushDeferredFirebird` drains the queue in its own
transaction). It used to record CREATE alone, and what exposed the difference was an ordinary edit:
adding an analytic to a register emits `ALTER TABLE … ADD fld…`, the totals rebuild then READS the
movements naming that column, and the prepare answers *"Column unknown FLD…"* about a column the same
apply added three statements earlier. The table existed, so nothing deferred; only its shape was new.

**Which makes regeneration (L3-4) a DATA phase**, through the same door the seed writes go through:
`DiffSnapshots` hands `ibDerivedState::Regenerate` to `RunOrDefer` instead of calling it inline. A
special case used to stand there — *skip regeneration when the source is created in this apply* — and
it was a dodge: it silenced one symptom of the barrier and left the other armed for the first person
to add a dimension. The rule is general: **a statement that READS a table this apply reshaped belongs
to the data phase, whatever the reshaping was.** The deferred closure takes a COPY of the table
declaration, because the snapshot is ephemeral — built at save, diffed, discarded — while the deferred
work runs past the point the caller still holds it.

⚠ **This class of defect is invisible to the test suite by construction:** the only engine the suite
executes against is SQLite, which has no transactional DDL, so `BarrierActive()` is false there and
nothing ever defers. A barrier bug is found on a live Firebird apply or not at all.

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

**A structure change can be REFUSED, and the question is asked BEFORE the diff.** `DiffSnapshots`
(`query/schemaSnapshot.cpp`) is three passes over the target tables:

```
PASS 1   every table's m_beforeChange(ibRestructureInfo*) -> bool    nothing emitted yet
 diff    drop vanished · create / alter · seed · materialise · regenerate
PASS 3   every table's m_afterChange(ibRestructureInfo*)  -> void    structure settled, same TX
```

The guard is a PASS and not a branch inside the differ because **a check inside the diff never fires
when the diff finds nothing to do**: lowering a declared limit alters no column, so the per-table
loop would never reach that table and the rule would never be asked. Every guard still runs after one
refuses — the user sees all the objections at once rather than one per attempt — and a single refusal
returns 0 without a statement emitted, because half a structure is not a state anyone asked for. The
after-event is the pair of the first and deliberately weaker: by then the tables are as declared, so
there is nothing left to decline. It returns `void` and exists to SAY things (or to do follow-up work
the new shape needs), inside the same transaction, so a rollback takes it along.

Both passes walk the TARGET tables only. A table that has VANISHED from the declaration carries no
rule with it, so a DROP is not something a guard can refuse — only a change to a table somebody still
declares.

A refusal is not a return code anybody tests: `ibMetaDataConfigurationStorage::OnSaveDatabase`
deliberately ignores the differ's result (0 is also what a pure-DDL delta returns). It travels as
ERRORS IN THE LEDGER — `ibRestructureInfo::HasErrors()` disables the Apply button of the change
dialog (`frontend/win/dlgs/applyChange.cpp`), leaving Cancel as the only exit, which rolls the save
transaction back. Which rules exist, who attaches them and why they live with the declaration:
[accounting-register-arc.md](accounting-register-arc.md) §5c.

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
  **The way back exists too** — `query/queryRender.{h,cpp}`, AST → query text, and it is what
  makes a query constructor a tool rather than a one-shot generator: a constructor that can only
  GENERATE text stops being useful the moment somebody edits the query by hand. Complete over the
  AST — every field of `ibQuerySelect` (DISTINCT / SELECT * / TOP / projections / FROM / joins /
  WHERE / GROUP BY / HAVING / ORDER BY / TOTALS with its levels and `OVERALL` / UNION) and all 15
  expression kinds, subqueries, `HIERARCHY` and `CAST` included. Two rules hold the trip together: keywords come from
  the active keyword table (so a localized table renders what it parses), and nothing is invented
  or dropped. Pinned by 36 round-trip cases in `tests/test_queryL4Parser.cpp`, checked by
  re-rendering rather than struct comparison; `composition/listFilter.cpp` already relies on it.
- **L4-2 — LINQ push-down** (`compiler/lambdaQueryAst`, `ibValueQueryable`, the `Data.*` root):
  a script `.Where(...).Join(...)` lambda body is re-parsed into a ready **L4-1 `ibQueryAstExpr`**
  and lowered through the same L4-1 builders — **no second engine**. See [linq.md](linq.md).
- **L4-3 — access-policy RLS** (`OnAccessRead`/`OnAccessWrite` on the Role): **not a source** — a
  **decorator** over any query (text / LINQ / composer), enforced on the L3 door. This is the
  coherence dividend: RLS is free because there is one door to gate. See
  [access-policy-rls.md](access-policy-rls.md).

**AND ALL THREE HAND BACK THE SAME PAIR** — a result and a selection over it. The script surface is
three types mirroring the two-step read (`system/value/valueQuery.h`): `Query` →
`QueryResult` → `QuerySelect`, with the traversal named by the `QueryResultIteration` enumeration
(`Direct` / `ByGroups` / `ByGroupsHierarchy`) and the output columns read straight off the cursor
(`s.ColumnName`). Because a selection walks ONE level (L3 above), a walk written in the language is
the nested loops it reads as:

```
q   = New Query(text);
res = q.Execute();
s   = res.Select(QueryResultIteration.ByGroups);
while (s.Next()) {                         // the first grouping
    d = s.Select(QueryResultIteration.ByGroups);
    while (d.Next()) { … }                 // what hangs under this heading — its groups, or its rows
}
```

Who does the descending is the only difference between the readers: a script writes the loops, and
at **L5** the composer writes them for a passive driver (`ibDataDBComposer::RunOutput` recurses and
hands each visit over as `OnGroup` / `OnDetail`, so no driver ever recurses).

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
- **The read state is held ABOVE the door it protects** — ⛔ and held by nobody today.
  `ibQueryReadState` lives at L3, but no L3 code would hold it: the four read doors above it do
  (L5's `ibDataDBComposer::Run`, the script query door, both LINQ dispatchers), because they are the
  ones that outlive the cursor. All four declare the holder unopened since 2026-08-23 — see § L3 for
  why. A caller that opens a transaction of its own gets a null holder and its own wider window
  instead.
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
