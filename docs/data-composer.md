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
- **`TotalBy` without a `Total` is valid** — pure grouping / hierarchy with no
  aggregate to roll (`TOTALS BY <dim>` alone). The parser and composer allow zero
  aggregates; the selector folds the dimension levels and emits group nodes only.
  Used by the dynamic list's grouping drill (see `dynamic-list.md`).
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
  `GetMetaData() + GetQueryTableId()`, read-only.
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
  (`map<metaID, ibValue>` off `OutputColumn::GetColumnId()`).
- the tree scope rides on the request itself: the model fills
  `ibReadPageRequest::m_hierarchyCol` (the parent COLUMN — the queryable's
  `GetHierarchyColumn()`; the model knows no field names) and
  `m_hierarchyKey` (the browsed parent node's KEY — the parent reference
  VALUE, empty = roots). The PROVIDER derives the physical field and encodes
  the key like any keyset key (a reference → its `_RRRef` blob, self-describing;
  no bare guid, no same-table assumption). The whole First/Next/Prev triple is
  ONE fetch body — direction is envelope state (Backward = inverse scan +
  buffer reverse).

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
   the value itself via `ibValueHash` + a container flag) replaces the four per-family
   row classes; the driver emits final model rows — the conversion loops,
   `AdoptAndCount`, and the manual refcount dance retire.
3. **Group verbs** (row axis) on the composer; the tree becomes a declared
   grouping; lazy branch browse via the selector's sub-selection.
4. ~~**The report provider** (the crown)~~ — **the first half LANDED 2026-08-18…20**: the TOTALS walk
   is written into a spreadsheet document by `ibSpreadsheetComposeDriver`, and the composition it
   walks is now a declared metatype inside the report (`Composer`), held by the report OBJECT as a
   field like a tabular section, shown by a gridbox through the SPREADSHEET MODEL. Full account:
   [report-engine.md](report-engine.md) §4a / §4f. Still ahead: the column axis / pivot, and the
   L5-2 appearance tier (conditional formatting, layouts).

   ⚠ Two things wear the name *composer* and they NEST: the value a person holds is the SHELL
   (`ibValueDataComposition` — settings, variants, parameters, and the report's verbs), and inside
   it sits `ibDataDBComposer`, the L5 store this document is about. The shell is not on the model
   base: filling a sheet is not the same as reading one, and a hand-filled spreadsheet document has
   no query at all.

Known language gaps L5 will press on (by-demand): a dot-walk LEAF inside a boolean
WHERE over a non-co-located JOIN (the plain boolean WHERE itself now runs as a
post-compose `RamFilter`), dot-walk filters/sorts over a JOIN, TOTALS over a subquery
source, reference-awareness of subquery columns, the shared virtual-table
companion under concurrency.

---

## OUTPUTS — what a composition produces (2026-08-21)

A composition no longer holds one set of settings; it holds **outputs**, and there is always at
least one. A list declares one, a report declares several, and each is read and handed to its own
driver.

```
Composer
├── source · parameters · RESOURCES · the composition-wide set of selected fields
├── the filter and sort that stand ABOVE the outputs
└── Outputs (at least one)
    └── Output: name (= the ONTO name of a result) · its own query package · ITS OWN DRIVER
        ├── m_rowGroups    — levels down the page, in order (the order IS the nesting)
        ├── m_columnGroups — the same levels across it; non-empty ⇒ this output is a CROSS-TABLE
        ├── its own filter / sort / selected fields (or "auto", taken from above)
        └── a level with NO fields is the DETAIL rows — not a second kind of output
```

### What is read off the data rather than stored

`Output::Kind()` answers *grouping* or *table* by looking at the column axis. Nothing stores "what
this output is", so nothing can disagree with the fields. A CHART is deliberately not a third kind:
it reads what a cross-table reads and differs only in being drawn — that is the driver's business.

### One level, several fields

A level holds one or more fields and groups by the TUPLE of them: `BY (Partner, Contract)` is one
heading, not two nested ones. Which of them actually divide the rows is the data's answer. The
UNFOLD (elements / hierarchy / hierarchy-only) belongs to each FIELD; a hierarchy walks one parent
chain, so a level of several fields carrying one is refused where it is written.

### The driver belongs to the output

Whoever declares the outputs also says who draws each. The composer never routes and never asks a
driver what it understands. **An output with no driver is not read at all** — nobody would take its
rows, so the query is not run. `Run()` (no argument) reads every output that has somewhere to go;
`Run(driver)` stays the short way in for a caller holding one, which is what a list is.

### The node language of the driver

`OnOutputBegin(kind, schema, name)` → `OnGroup(level, hasChildren, values)` /
`OnDetail(level, values)` → `OnOutputEnd(totals)`. Stated ON TOP of the row verbs, so a driver that
only understands rows keeps working; a group and a detail row are no longer the same sentence the
printer has to tell apart by depth.

### Detail records — a level of the ladder (2026-08-21)

*"A detail record is an empty grouping"* (Max) — the rows themselves sit at the bottom of the ladder,
under the deepest heading, and the settings tree writes them as a node like any other
(`ibCompositionLevelKind::Details`, `GroupNode::m_kind`).

**Adding a level is a FORM, not a field picker.** A grouping is a LIST of fields welded into one
heading, so the act of adding it asks for the list: `ibComposerGroupingDialog` (the same
`ibGroupingFieldsModel` the Grouping tab uses, over the node being made) with add / delete / move
and the per-field unfold. **OK with an empty list is an answer** — that node is the detail records,
and the dialog stamps the kind so nothing downstream reads it off the emptiness. One verb makes a
level; what the level is gets decided inside it.

**The kind is a type, not an emptiness.** A level with no fields happens by accident as well — one
whose fields stopped resolving loses them, and `CollapseEmptyLevels` drops it so a nameless heading
cannot swallow every row. One emptiness, two opposite meanings; the node says which it is.

**What it costs, and where that is paid.** A report that prints every row holds every row — that is
what printing them means. So the read changes in exactly two ways when an output asks for details
(`WantsDetails`): `ExecuteTotals(…, withDetails)` adds one more level with NO FIELDS to the door's
config (`ibDataQueryBuilder::TotalsDetails`, its own verb so an accidental empty level can still be
refused), and the server-side fold is declined — `GROUP BY ROLLUP` returns aggregated rows and would
leave nothing to hang. `TryTotalsPushdown` refuses an empty level itself, so no other road can lose
the rows quietly.

**What the fold does with it.** `FoldDimLevel` reads a fieldless level as "no group here" and hangs
one node per source row under the last heading, marked `ibSelectorNodeKind::Detail`. The rows are
already in the snapshot the headings were folded from, so it costs nodes, not a second read.

**How it reaches the printer.** The walk asks the NODE (`ibSelector::Kind()`) and calls
`OnDetail(level, values)` instead of `OnGroup` — the printer is told in its own words rather than
inferring it from the depth, which a depth cannot answer once the tree holds both.

**Where the figures are printed** is the driver's business and follows one rule: a heading names the
group, a TOTAL LINE closes it, and the grand total is the last line of the output's section. See
report-engine.md § 6bb.

⏳ Not yet: the query TEXT cannot ask for detail records — there is no keyword, so the constructor
has nothing to offer either. It is deliberately open: a new global word steals an identifier
(`reference_query_keyword_steals_a_name`), and the naming is Max's call. Today the request travels
as an argument of the read, which is where "how much of the tree do you want to look at" belongs.

### Where a filter sits decides what it does

| Where | What it does |
|---|---|
| above the outputs | EXCLUDES, for the whole composition — no output sees more than it admits |
| on an output | EXCLUDES, within that output |
| on a level | HIDES the heading, and its rows keep counting (applied on the walk, never in the WHERE) |

Cutting data at a level would move every total above it, and a total that shifts because somebody
tidied a sub-heading is a figure nobody can defend. A hidden heading hides what is under it, since
printing a child of an unprinted parent would file it under the wrong one.

### Available and selected fields inherit

Composition → output → level, each may take its own set or say "auto". The flag is explicit because
an empty list already means something: *show nothing*.

Two sets, two questions, one inheritance (`AvailableFor` / `SelectedFor`, both on `ibDataComposer`):

| set | question | who reads it |
|---|---|---|
| **available** | what this node MAY use — group, filter, sort, show | the settings window: it narrows every field pane |
| **selected** | what this node DOES show | the render — the projection written into the query |

**Available narrows the PICKERS, and that is the whole of it** (2026-08-22). It changes no query and
no read: what a node may be *offered* is a UI statement, and offering a person a field the report
forbids is the defect it exists to prevent. `ibSettingsFieldTree::SetVisibleTest` takes the
predicate; the composer window answers it from the SELECTED node's set, so the panes re-fill when
the selection moves (`ReloadFieldTrees`). Empty means everything, as it does everywhere here.

⚠ **A road is never hidden.** The test is asked of a field's path, and answering "no" for `Producer`
would take `Producer.Region` with it — so only LEAVES are narrowed and a reference stays walkable.

⚠ **The top of the inheritance is the REPORT row**, and its set lives in the window's own buffer
until Apply. Only the *selected* buffer was being loaded and written back, so everything a person
narrowed on the report was read from an empty list and saved nowhere (found by audit, 2026-08-22 —
`LoadStructure` / `ApplyStructure` now carry both).
