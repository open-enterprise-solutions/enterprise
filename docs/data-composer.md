# L5 — the data composer (declarative composition over the query language)

`src/engine/backend/composition/` is the fifth tier of the query ladder
(L1 drivers → L2 IR + dialect → L3 source-agnostic engine → L4 query languages →
**L5 declarative composition**). See `docs/query-language-arc.md` for the tiers
below; this document is the canonical reference for the composer.

## The formula

**L5 is a TEXT RENDERER for L4-1.** It holds a declarative schema spoken in the
USER vocabulary — *filter*, not WHERE; *sort*, not ORDER BY; *grouping*, not
TOTALS BY — and renders it into an ordinary L4-1 query. The generated query is
indistinguishable from a hand-written one, and the text is the ONLY seam
downward:

- no private doors into L3 — the composer never touches `ibDataQueryBuilder`
  and never composes queryables;
- a dot-walk path (`Producer.Region.Name`) is plain data in the schema — the
  lowering resolves it;
- queryables are reached READ-ONLY, through the source factory, as the column
  dictionary (describe); execution always flows through the rendered text;
- any L5 bug reproduces by copy-pasting `RenderText()` into `New Query(text)`.

The result comes back through the STANDARD L4-1 machinery (`ibDataQueryResult`,
`ibSelector`) — L5 has no execution of its own. What is not expressible in the
text travels in a small **execution envelope**: parameters, the page request,
the traversal kind.

## Architecture

```
ibDataComposer (composition/dataComposer.h) — long-lived, one per consumer
  ├─ schema verbs (fluent):
  │    FromSource(ns, name) | FromSource(queryable) | FromText(verbatim query)
  │    Select(nameOrPath | column)        — projection; empty = all source columns
  │    Filter(path, op, value)            — AND-folded; value = auto-&parameter
  │    Sort(path, ascending)
  │    Total(func, path) + TotalBy(path, hierarchy)
  │    Parameter(name, value)             — the author-text variability point
  ├─ RenderText()                          — the debug view / the AI seam
  ├─ Execute(schema&, hasTotals&[, page])  — render→parse→lower→run
  └─ Run(driver) / SetDriver + Run()       — the full cycle into a driver

ibCompositionDriver — the passive output sink ("where the data goes"):
  OnColumns(schema) → OnRow(level, hasChildren, values)* → OnComplete(totals)
  + GetPageRequest(request)               — a paged driver vends the envelope
```

- **One composer, many drivers** — the realization dictionary. A flat result
  streams rows at level 0; a TOTALS result arrives as the folded tree's
  pre-order walk (`ibSelector::Next()` covers every node — drivers never
  recurse), subtotals in-place on group nodes.
- **The composer owns its caches**, consumers stay dumb:
  - *parse cache*: one AST per rendered text — a scroll tick re-renders the
    same text → no re-parse; a settings change renders different text → the
    natural invalidation;
  - *page cache (Lever 1)*: the door's build-once SQL keyed by a signature =
    rendered text + page shape + parameter values (a non-signable value — a
    reference — disables it; the anchor is NOT signed — it rebinds into the
    cached render).
- **Paged reads**: `ibQueryLowering::Execute(ast, params, schema, page[,
  cache, signature])` threads an external `ibReadPageRequest` into the door's
  terminal — no grammar change; a `TOP n` in the text still caps the page
  (the smaller positive count wins). Plain SELECT path only.

### Sources

- `FromSource(ns, name)` — any factory-registered source (metaobject families,
  register virtual tables, future external sources).
- `FromSource(const ibBackendQueryable*)` — the typed face: holding a queryable
  IS the proof the source is queryable (a metaobject overload would not be —
  reports/processors vend none); the identity is recovered through
  `GetMetaData() + GetQueryMetaID()`, read-only.
- `FromText(query)` — the author's verbatim L4-1 text, never edited. Settings
  over an author's text are not rendered yet (they error clearly); the next
  seam is the subquery wrap `SELECT … FROM (<text>) AS src WHERE …` — the
  language already supports subquery sources. Deep variability (e.g. a filter
  that must reach a virtual table's source args) is an ordinary `&parameter`
  in the author's text + a mapping in the schema — never `{}`-style markup.

### The script face

`DataComposer` (`system/value/valueComposer.{h,cpp}`):

```
c = New DataComposer("Catalog.Goods");      // a registered source — rendered
c = New DataComposer("SELECT … FROM …");    // verbatim text (whitespace ⇒ text)
text = c.QueryText();                        // the rendered L4-1 text
res  = c.Execute();                          // a STANDARD QueryResult
```

## The first consumer — every list is composed

All four list families fetch through the composer
(`metaCollection/partial/list/`): the **enum** list, the flat **catalog** list,
the **FolderRef tree**, and the **register** list. The pattern:

- the SOURCE is wired once in the model's ctor (a list IS metaobject-bound);
- the SETTINGS (user filters/sorts) are re-applied per fetch (the
  `ApplyList*` translation helpers are transitional scaffolding — see the
  queue below);
- the PAGE ENVELOPE rides on **`ibListFetchDriver`**
  (`composition/listFetchDriver.h`) — a stack object built per `Get*Fetch`
  call: the envelope in (direction/anchor/count), meta-keyed rows out
  (`map<metaID, ibValue>` off `OutputColumn::GetModelID()`).
- the tree passes an **`ibTreeScope`** (the parent COLUMN — the queryable's
  `GetParentColumn()` — plus the browsed node): the driver assembles the
  hierarchy envelope, and the PROVIDER derives the physical field
  (`ibReadPageRequest::m_parentCol`; the model knows no field names). The
  whole First/Next/Prev triple is ONE fetch body — direction is envelope
  state (Backward = inverse scan + buffer reverse).

Row identity: the uuid identity column is a RAW DB column **outside the query
language** by design. The row key is the data-reference attribute (named,
language-visible); the guid is pulled from its VALUE
(`ibValueReferenceDataObject::GetGuid()`). A register has no single row key —
its composite identity (recorder+line / period+dims) is carried by the named
identity columns themselves; the door appends the identity tail to the
rendered ORDER BY (one source of truth). Navigation utilities that filter by
raw uuid (`LoadRowsByGuids`, `GetAncestorChain`) intentionally stay on the
L3 door.

## The agreed output model (design, next increments)

- **Everything is a grouping** — no separate "view shape": an EMPTY grouping
  set = detail records (flat list); `Group(Parent, Hierarchy)` = the catalog
  tree; groupings + resources (`Total`) = a report with in-place subtotals.
  Switching list↔hierarchy = adding/removing one grouping on a live composer.
- **Two axes**: `RGroups().GroupRow(field, mode)…` +
  `CGroups().GroupCol(field)…` — the pivot. The query renders identically
  (all dimensions of both axes); the difference is the FOLD: row groups =
  tree levels, column groups = values unfolded into dynamic output columns
  (RAM pivot from the same snapshot).
- **Multiple outputs**: `comp.Output(name)` × N — shared settings (source +
  filters + parameters = the data) vs per-output settings (groupings + totals
  = the fold). ONE read → N folds → N drivers (a dashboard), or N outputs
  bound to ONE driver, rendered one after another in declaration order (a
  compound report document; `OnOutputBegin(name)` marks the sections).
  Requires a shared snapshot (`ibSelector` currently takes it by move).

## Queue (after the current state)

1. **Settings-as-data on the composer** (per Max's call, deferred): filters /
   sorts get identity + a `use` flag + presentation; `Fields()` (describe)
   feeds the pickers; the UI mutates the composer directly — the view-layer
   stores (`m_filterRow` / `m_sortOrder`) and the `ApplyList*` bridges retire.
2. **Generic row**: one row class (values map + identity via
   `ibValue::GetHashKey()` + a container flag) replaces the four per-family
   row classes; the driver emits final model rows — the conversion loops,
   `AdoptAndCount`, and the manual refcount dance retire.
3. **Group verbs** (row axis) on the composer; the tree becomes a declared
   grouping; lazy branch browse via the selector's sub-selection.
4. **The report provider** (the crown): the TOTALS walk written into a
   spreadsheet document by the rules of the structure; then the column axis /
   pivot, and the L5-2 appearance tier (conditional formatting, layouts).

Known language gaps L5 will press on (by-demand): boolean WHERE over a
RAM-stitched JOIN, dot-walk filters/sorts over a JOIN, TOTALS over a subquery
source, reference-awareness of subquery columns, the shared virtual-table
companion under concurrency.
