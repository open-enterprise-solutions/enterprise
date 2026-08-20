# The spreadsheet document — the server-side result

> **Scope:** `backend/backend_spreadsheet.{h,cpp}` + `backend/spreadsheetDescription.h` —
> the document itself, not the editor. Companions: [spreadsheet-editor.md](spreadsheet-editor.md)
> (the grid that edits one), [report-engine.md](report-engine.md) (what composes one),
> [localization.md](localization.md).
> **Status:** foundation code, in the tree for a long time; **pinned by tests since 2026-08-18**
> (`tests/test_spreadsheetDocument.cpp`, 27 cases).

---

## 1. What it is, and why it is in the backend

`ibBackendSpreadsheetObject` is a **`wxRefCounter` with no UI in it**. It holds the cells, their
formatting, the merges, the outline groups, the print geometry, the named parameters and the
drill-down bindings — everything a rendered report consists of.

That placement is the whole design: **the document is the RESULT, and a view is a subscriber to
it.** The desktop grid (`ibGridEditor`) subscribes today, the web client will subscribe later, and
a test subscribes to nothing at all and reads the document directly. Nothing about a composed
report needs a window to exist, which is why report numbers can be asserted headlessly.

```
ibBackendSpreadsheetObject               (backend — the result)
   ├── ibSpreadsheetDescription          the storage: cells, sizes, groups, breaks
   ├── parameters: name → ibValue        what template cells read, what drill-down decodes
   └── ibBackendSpreadsheetNotifier*     0..N subscribers, told about every mutation
            └── ibGridEditor's notifier  (frontend — one reader among several)
```

---

## 2. Storage — sparse, and the extent follows the writes

`ibSpreadsheetDescription` keeps plain `std::vector`s: cells, per-row heights, per-column widths,
row/column breaks, areas and groups. A cell exists only once something writes it
(`GetOrCreateCell`), so the grid is **sparse** — reading a cell nobody wrote answers empty rather
than failing, and that is ordinary rather than exceptional.

There is no separate "resize" step: `SetCellValue(3, 2, …)` is what makes the document that big.

⚡ **What a cell costs.** Every `ibSpreadsheetCellDescription` carries a `wxFont` and two
`wxColour` members seeded from `wxSystemSettings`, plus four border descriptions. A 10 000 × 10
report is 100 000 cells each holding that — a real cost, and the first place to look when a large
report gets heavy. It is also why a console test that writes ONE cell prints a CRT leak dump at
exit: those GDI members are freed by `wxEntryCleanup`, which a test never runs. **The dump is not
a leak** (diagnosed 2026-08-18 by narrowing to a bare `ibSpreadsheetDescription` on the stack).

---

## 3. Three fill types — and the localisation rule

`ibSpreadsheetFillType` decides how a cell's text is read:

| Type | The cell's text is | Resolved by |
|---|---|---|
| `StrText` (default) | itself | nothing — returned untouched |
| `StrParameter` | **the name of a parameter** | `ComputeStringValueFromParameters` → that parameter's value |
| `StrTemplate` | running text with `[token]`s | each token replaced by its parameter; an unknown token renders **empty** |

⚠ **A computed cell is LOCALISABLE TEXT, a plain one is not.** Both the parameter and the template
path run through `ibBackendLocalization`: the template is *translated first* (so an authored
template must itself be raw loc-text, or it translates to nothing), and both return
`CreateLocalizationRawLocText(...)` — an envelope like `en = '42';`, not a bare string. `StrText`
skips the layer entirely. The asymmetry is real and was found by the tests; a caller comparing a
computed cell against a plain string must translate it first
(`GetTranslateGetRawLocText`).

An unknown token rendering **empty** rather than staying as `[Whoever]` is deliberate: a report
showing its own placeholder is worse than a blank.

---

## 4. Parameters and drill-down

`SetParameter(name, value)` / `GetParameter(name, value)` — the document's own named values.
A missing one answers **false**, not an empty value that reads like a set one; templates depend on
telling those apart.

`SetCellDetailsParameter(row, col, name)` binds a cell to one of them, and
`OpenCellDetailsParameter(row, col)` resolves it and calls `ibValue::ShowValue()` — the platform's
cell-level drill-down. The payload is a **value that knows how to show itself**, so the document
needs no idea of what a document, a reference or a register record is.

---

## 5. Areas — the template mechanism

A report is composed by copying rectangles out of a template and appending them.

| Verb | What it does |
|---|---|
| `GetArea(rowL, rowR, colT, colB)` | lifts a rectangle out and **re-origins it at (0,0)** — an area is a document in its own right, not a view onto its parent |
| `GetAreaByName(left, top)` | the same by named area, so editing a template does not move a script's coordinates |
| `PutArea(area, groupLevel = 0)` | **appends BELOW**, starting at the current row count |
| `JoinArea(area, groupLevel = 0)` | **appends to the RIGHT** — the other axis of the same verb |

`PutArea` does three further things, and each is load-bearing:

1. **Template and parameter cells are resolved as they land**, against the **area's** parameters,
   and the landed cell becomes `StrText`. That is what makes a template a template: fill the
   area's parameters, put it, repeat. Resolving twice would look for parameters the receiving
   document does not have.
2. **Drill-down names are made unique per landing position** (`<name><row><col>`), and the value is
   copied over. Without this the second copy of a row would decode to the first copy's value.
3. It sets a **print break** at the last landed row, and — when `groupLevel > 0` — records an
   outline group over exactly the rows that landed.

---

## 5a. The spreadsheet MODEL — what a gridbox is looking at (2026-08-20)

A gridbox shows a sheet. **Where that sheet comes from** differs — a hand-filled document IS one, a
composer BUILDS one — and the control must not sort those out: that would be a cast per kind inside
a general-purpose control, and the day a third thing shows a sheet the control would be the place
that had to learn about it. So there is a base, `ibValueSpreadsheetModel`
(`backend/spreadsheetModel.h`), and both derive it **directly** — no wrapper in between:

```
runtime (ibValueDynamicMembers)
  └── ibValueSpreadsheetModel          abstract — one question, plus a command store and a fetch
        ├── ibValueSpreadsheetDocument    the hand-filled sheet
        └── ibValueDataComposition        the composer
```

⭐⭐ **THE MODEL HOLDS THE SHEET AND HANDS IT BACK** (`GetSpreadsheetDocument`). That is what the two
kinds have in common and the whole reason the base exists: the rendering control asks for the sheet
and draws it, and never learns whether a query ran. The sheet is created in the base's constructor —
**always**, not by each descendant — because holding one is what makes something a model of a
spreadsheet: a composer that has never run and a document nobody typed into both have an empty one.

⭐ **One act differs: `Compose()` — produce the data.** A composer reads its query, lays the result
out and installs the finished sheet as its own; a hand-filled document already IS its result.

⚠ **The fetch here is not the table's, and that is why this base exists at all.** A list reads in
PORTIONS — anchor, direction, page — and lives by them; a spreadsheet does not: the request goes out
and the whole result comes back, one sheet, for a person to read.

⭐ **And on the base the fetch is an EVENT, nothing more** (Max, 2026-08-20: *"make the async fetch
just an event on the base model and let the composer override it itself"*): `SubmitFetchAsync` simply
runs the work it is handed, and `CancelFetch` has nothing to stop. The **composer** overrides both,
rents a background run with a connection of its own and keeps the handle — so the job manager is
named in exactly one place, the thing that actually READS. A hand-filled document rents nothing
because it reads nothing. There is no door lock either way: one request, one sheet, nothing to
serialise. A second Compose while one is in flight cancels the first (one sheet, one slot), and the
composer waits its run out in its destructor so a read cannot outlive what it is filling.

⚠ **The query engine is NOT on the base.** Two things wear the name "composer" and they nest: the
value a person holds is the SHELL (settings, variants, parameters), and inside it sits
`ibDataDBComposer`, the L5 store that builds the query for the driver. Filling a sheet is not the
same as reading one — a hand-filled document has no query at all.

⭐ **The model is also the COMMAND STORE**, exactly as a table's model is: it only LISTS what can be
done (`GetCommandCollection`) and the control lays that list out into real actions. A composer names
`Compose` and `Settings` — they are facts about a composition, not about whichever control shows it,
so the same verbs appear everywhere it appears; a document names none, because it is already its own
result. The ids are declared once, beside the base (`ibSpreadsheetModelCommand`), so the store and
whoever runs a verb cannot disagree. Unknown ids go back to the model (`CallAsModelCommand`).

🛑 **The composer used to derive `ibValueModelCursor`** — it was copied from the dynamic list, and it
brought a table's fetch and a table's dataview surface with it: column collections, a return line,
`RunComposerPage`, `GetRowAt`, `GetFeatures`, row keys, `ActivateItem`. None of that is a
composition's: it is read ONCE and written into a SHEET. All of it is gone (2026-08-20). What the
query produces is kept as the output SCHEMA and laid out by the compose driver, by ROLE
([report-engine.md](report-engine.md) §4d). The drill-down did **not** go with it — it never went
through the dataview: a composed cell carries its value as a document parameter and the click ends
in `ibValue::ShowValue`.

⚠ The model is deliberately **not registered** as a type: it is machinery, not a value anybody names.
A script holds a document or a composer, never "a spreadsheet model".

---

## 6. Outline groups, freeze, print breaks

`BeginRowGroup()` / `EndRowGroup()` (and the column pair) bracket a range; the open positions live
on a **stack**, so nesting is expressed by nesting the calls rather than by arithmetic. Groups are
read back through `GetGroupNumberRows()` / `GetRowGroupByIdx()`. This is what makes a composed
report fold and unfold with no extra state — the group tree IS the grouping.

`SetRowFreeze` / `SetColFreeze` keep headings in place; `AddRowBrake` / `DeleteRowBrake` /
`GetMaxRowBrake` carry the print geometry; `SetRowSize` / `SetColSize` the extents.
`SetCellSize(row, col, nRows, nCols)` merges — a title spanning the table's width is one merged
cell rather than text in the first column.

### The pane that draws them (2026-08-20)

The document says only that a range is a group; what a reader SEES is the grid's outline pane
(`ibGrid::DrawRowOutline` / `DrawColOutline`, the button geometry in `GetRowGroupButtonRect`). Four
things it was getting wrong, and all four are the same mistake in different clothes — **chrome that
did not follow the sheet**:

- **It did not zoom.** The pane's width, the per-level step and the button were raw constants while
  the positions they are drawn against (`GetRowTop`, `GetRowHeight`) are scaled. Off 100% the
  markers drifted from their headings, and at a small zoom they were taller than the rows and
  smeared into one another. Every length now goes through `ibCalcGridScale`, the same helper the
  labels and the areas use.
- **The button is sized from its CELL**, a few pixels smaller than the row it sits on (bounded by
  its own level's lane so a deep marker never crosses into its neighbour's), and centred on the
  heading rather than pinned under its top edge.
- **The frozen strip had no pane at all.** The row pane was placed *below* the freeze, so beside
  the frozen rows there was neither a marker nor a background — a white notch in the frame. It now
  has the FROZEN TWIN the labels and the areas have always had
  (`ibGridRowFrozenOutlineWindow` / `ibGridColFrozenOutlineWindow`, `IsFrozen()` = no scroll offset
  in paint and in hit-test), and the corner the two panes do not reach is a plain chrome-coloured
  window (`ibGridOutlineCornerWindow`). ⚠ The panes were also missing from
  `SetLabelBackgroundColour` — chrome that never learned the chrome colour.
- **The rail closes.** A bare vertical line says where a group starts and never where it ends, so
  the last row of a group carries a tick out to the pane's edge (Max, 2026-08-20).

---

## 7. Notifiers — the contract a renderer subscribes to

`AddNotifier<T>(args…)` constructs a subscriber and returns a `wxSharedPtr`; `RemoveNotifier`
detaches it. **Every mutation is announced** — the document calls each notifier in turn (the
`spreadsheetNotify` macro loops the list). Several views on one document all hear it, and a
removed one hears nothing more, which is what lets a view close while the document outlives it.

`RowAreaAdded` / `ColAreaAdded` default to no-ops so an existing notifier keeps compiling when the
block API grows.

---

## 8. Identity and files

Each document is born with its own `ibGuid` (`GetDocGuid`) — two documents are never the same one.
`LoadFromFile` / `SaveToFile` round-trip a document through the byte layer
([serialization-io.md](serialization-io.md)).

---

## 8a. Audit — 2026-08-18

Found while writing the tests. The first three are **fixed**; the rest are open and ordered by
what would bite first.

### 1. ✅ FIXED — filling cells was QUADRATIC

`GetCell` / `GetOrCreateCell` did a `std::find_if` over the **whole cell vector** for every
access, and `GetNumberRows` / `GetNumberCols` walked every cell on every call. Composing a report
does both in a loop, so the cost grew as `O(cells²)` — the curve, not the constant.

Now: an index `(row << 32) | col → position` (`std::unordered_map`) answers where a cell is, and
the extent is maintained on insert. The vector still OWNS the cells, so insertion order — which
the serializer and `operator==` depend on — is untouched. Only two places touch the vector
(`GetOrCreateCell` inserts, `ClearSpreadsheet` drops), which is what makes one index sufficient.

Measured (Debug, `bench_spreadsheet.cpp`):

| cells | 1 000 | 2 000 | 4 000 | 8 000 | 16 000 | 32 000 |
|---|---|---|---|---|---|---|
| **before**, ms | 5 | 12 | 33 | 88 | — | — |
| **after**, ms | 5 | 10 | 24 | 44 | 87 | 186 |

After the change, doubling the cells doubles the time. Extrapolated to a 10 000 × 10 report
(100 000 cells): from roughly **14 s** to roughly **0.6 s**.

### 2. ✅ FIXED — `erase(std::remove(…))` was half the idiom

`DeleteRowBrake`, `DeleteColBrake` and `RemoveNotifier` erased a single position from the value
`std::remove` returns. That accidentally works while exactly one match exists and is **undefined
when there is none** — deleting a break that was never added, or detaching a view twice. All
three now erase from the new end to the real one. Pinned by
`DeletingAMissingBreak_IsANoOp` and `RemovingANotifierTwice_IsANoOp`.

⚠ **`RemoveNotifier` came back** and was fixed again on 2026-08-20 — the test had been failing
(the debug runtime aborts the whole suite: *"vector erase iterator outside range"*), which is
what a pinned rule is for. Worth reading as a warning about the idiom rather than about the file:
half of `erase(remove(…))` looks finished and is wrong only on the path nobody exercises by hand.

### 3. ✅ FIXED — seven index accessors were off by one

The bound was `idx > size()`, so the one-past-the-end index returned a pointer into nothing
instead of null: `GetCellByIdx`, both `*BrakeByIdx`, both `*AreaByIdx`, both `*SizeByIdx`. Only
the group accessors — written later — had it right. Pinned by `IndexAccessors_RefusePastTheEnd`.

### 4. A cell costs 432 bytes

`sizeof(ibSpreadsheetCellDescription) == 432` — a `wxFont`, two `wxColour`s and four border
descriptions, every one of them per cell whether or not it was ever formatted. 100 000 cells is
**~43 MB** of cell structures alone, before the strings. Formatting is overwhelmingly uniform in a
real report, so the shape that fits is a shared style referenced by cells (flyweight), with
per-cell overrides only where something actually differs.

### 5. Notifiers are called on whatever thread writes

Every mutation calls the subscribers inline. A background composition (the intended direction for
reports and scheduled jobs) would therefore drive a GUI notifier from a worker thread. Today
nothing composes off-thread, so this is latent rather than live — but it decides how background
report generation has to be built: compose into a detached document and hand it over, or make the
notification path marshal.

### 6. Localisation asymmetry (see § 3)

A computed cell silently renders empty when its template is not raw loc-text. It behaves
correctly, but the failure mode is a blank cell rather than a complaint — worth a diagnostic when
this layer is next touched.

## 9. What the tests pin, and what they do not

`tests/test_spreadsheetDocument.cpp` — 27 cases over: the empty document, extent growth, sparse
reads, clear, parameters (including the missing-parameter verdict), all three fill types and the
localisation envelope, drill-down round-trip, merge, freeze, breaks, sizes, outline groups
(including nesting), `GetArea` re-origin, `PutArea` (append, template resolution, per-position
drill-down names, group level), `JoinArea`, the notifier contract (heard / removed / two of them),
and per-document identity.

`tests/test_spreadsheetCompose.cpp` covers the layer above — a composition laid out INTO a
document ([report-engine.md § 4a](report-engine.md)).

**Not covered yet**, and worth knowing before relying on them:

- `LoadFromFile` / `SaveToFile` round-trip;
- the `GetArea` overloads with a **negative** column bound (whole-row selection) — the code paths
  exist and are unexercised;
- printing geometry beyond the break bookkeeping (`ibGridPrintout` lives in the frontend);
- anything about how a notifier's receiver draws — that is the editor's own story.
