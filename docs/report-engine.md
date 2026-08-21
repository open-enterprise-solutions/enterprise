# Report engine — runtime shape

> **Scope:** how a Report metaobject turns into a rendered spreadsheet at runtime.
> The document itself — cells, parameters, areas, notifiers — is
> [spreadsheet-document.md](spreadsheet-document.md).
> Companion docs: [data-composer.md](data-composer.md) (L5 — where a report's *data*
> comes from), [register-totals-strategy.md](register-totals-strategy.md).
> This document describes code that **already exists**; it is a map, not a plan.

---

## 1. The two halves

A report is two independent things that meet only at the end:

| Half | Owns | Lives in |
|---|---|---|
| **Data** | query / composition → rows | L3 door + L5 composer (`data-composer.md`) |
| **Presentation** | a spreadsheet document — cells, areas, formatting | `ibBackendSpreadsheetObject` |

Nothing in the spreadsheet half knows about queries; nothing in the data half knows
about cells. A script in the report's object module is what joins them.

---

## 2. Metaobject — `ibValueMetaObjectReport`

`backend/metaCollection/partial/dataReport.h`

- Extends `ibValueMetaObjectRecordDataExt` — a report is a **record-data object**, so it
  carries attributes and tabular sections like a Catalog/Document does. Those are the
  report's *parameters* surface, not stored table data.
- One form kind: `eFormReport` (`"FormReport"`), exposed via `GetFormType()`. The default
  form is chosen by the `DefaultFormObject` property.
- Two modules: `ObjectModule` (per-report logic) and `ManagerModule`.
- `GetCommandSection()` returns `ibInterfaceCommandSection_Report` — this is the single
  line that files every report under the **Report** section of the command interface
  (see [command-interface.md](command-interface.md)).

### External reports

`ibValueMetaObjectExternalReport` (`m_metaId = 10`) overrides `IsExternalCreate()` and is
loaded from a file rather than the configuration. Its value object,
`ibValueRecordDataObjectExternalReport`, mixes in `ibExternalOwnerHelper`, which owns the
transient external metadata container and drops it in its destructor — the RAII reason the
external variant is a separate class at all. Embedded reports use the plain
`ibValueRecordDataObjectReport`.

---

## 3. Spreadsheet document — `ibBackendSpreadsheetObject`

`backend/backend_spreadsheet.h`. A `wxRefCounter`, held through `wxObjectDataPtr`.

The document is **GUI-free** and lives in `backend.dll` (decision #4 in
[../CLAUDE.md](../CLAUDE.md)). All cell state lives in one `ibSpreadsheetDescription`
member (`spreadsheetDescription.h`); the object is a façade with behaviour over it.

### Notifier — how the UI ever sees it

`ibBackendSpreadsheetNotifier` is a pure-virtual sink. Every mutator on the document
(`SetCellValue`, `SetCellFont`, `SetRowFreeze`, `PutArea`, …) has a mirror on the notifier.
Views attach via the template `AddNotifier<T>(args…)` and detach with `RemoveNotifier`.

This is the seam that keeps the backend GUI-free: the frontend registers a notifier that
paints into a Gridbox, the web front registers a different one, and a headless run
(daemon / codeRunner) registers none at all and still produces a correct document.

`RowAreaAdded` / `ColAreaAdded` have empty default bodies **on purpose** — they were added
with the outline-group API and default to no-op so existing notifiers keep compiling.

### Areas — the template mechanism

A report is composed by copying rectangles out of a *template* and appending them to a
*result*:

```
ibSpreadsheetDescription GetArea(rowLeft, rowRight, colTop = -1, colBottom = -1);
ibSpreadsheetDescription GetAreaByName(strAreaLeftName, strAreaTopName = "");

void PutArea (const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, groupLevel = 0);
void JoinArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, groupLevel = 0);
```

`PutArea` appends below; `JoinArea` appends to the right. `GetAreaByName` is the
named-area lookup a script uses (`GetAreaByName("Header")`), so a template edit does not
break the script as long as the name survives.

### Parameters

Cells are filled by name, not by coordinate:

```
void   SetParameter(const wxString& name, const ibValue& value = ibValue());
bool   GetParameter(const wxString& name, ibValue& out) const;
wxString ComputeStringValueFromParameters(const wxString& value, ibSpreadsheetFillType type) const;
```

A cell's fill type (`ibSpreadsheetFillType`) decides whether its text is a literal, a
parameter, or a template string with embedded parameters —
`ComputeStringValueFromParameters` is what resolves the last case.

### Outline grouping

`BeginRowGroup()` / `EndRowGroup()` (and the column pair) nest freely; `End*` pops the
stack and turns the range between the matching `Begin*` and the current row count into an
outline group at depth = stack size + 1. The stacks are plain `std::vector<int>` members.

### Drill-down ("details")

`SetCellDetailsParameter(row, col, s)` / `GetCellDetailsParameter` attach a decoding
payload to a cell, and `OpenCellDetailsParameter(row, col)` acts on it. This is the
platform's cell-level drill-down hook.

### Persistence and printing

`LoadFromFile` / `SaveToFile` round-trip a document. `m_docPrinterName`, the row/col
**brake** API (`AddRowBrake` / `SetColBrake` / `GetMaxRowBrake`) and freeze panes
(`SetRowFreeze` / `SetColFreeze`) exist for the print/geometry path.

---

## 4. Templates — `ibValueMetaObjectSpreadsheetBase`

`backend/metaCollection/metaSpreadsheetObject.h`

An abstract base with two concrete kinds, differing **only** in which property category
holds the data:

| Class | Category | Meaning |
|---|---|---|
| `ibValueMetaObjectSpreadsheet` | `Template` | template owned by one metaobject |
| `ibValueMetaObjectCommonSpreadsheet` | `CommonTemplate` | configuration-wide shared template |

Both store an `ibSpreadsheetDescription` behind an `ibPropertySpreadsheet` and expose it
through `Get/SetSpreadsheetDesc`. That is the entire difference — the duplication is a
candidate for the restructuring plan, not a design statement.

---

## 4a. Composition → document — `ibSpreadsheetComposeDriver` (2026-08-18)

The two halves of §1 met. `backend/composition/spreadsheetComposeDriver.{h,cpp}` is an
`ibCompositionDriver` that writes the composition's walk **into the spreadsheet document** —
so the result is produced once, in the backend, and every renderer reads it: the desktop grid
today (through `ibBackendSpreadsheetNotifier`), the web later, a test with no window at all
(`tests/test_spreadsheetCompose.cpp`).

The layout, in one place so it can be argued with:

| | |
|---|---|
| **Heading** | `SetTitle` + `AddHeaderLine`, each merged across the table's width, then one blank row. `ibValueDataComposition::Compose` fills it — title from the source, lines from the composition's own **filters** (read back through `FilterCount` / `GetFilterAt`), so the parameters shown are not a second store |
| **Column titles** | one row, tinted, and the freeze is set below it — a long report keeps its titles |
| **Rows** | one composed row per document row, values in schema order; the first column carries the indent, `3` spaces per level |
| **Nesting** | `BeginRowGroup` is opened **after** the row that has children, so a collapsed group still shows its own caption; the walk closes groups as it comes back up, and `OnComplete` closes what is left |
| **Drill-down** | a cell whose value is a reference / object / enum is stored as a document PARAMETER named for its address and bound with `SetCellDetailsParameter`. A number or a string is not bound — there is nothing behind it to open |
| **Colour** | ⚠ provisional: header `D4E4D4`, grouping rows `E2EEE2` fading with depth, painted across the whole row width. Belongs in [ui-palette.md](ui-palette.md); it is in the driver only until the report has palette roles of its own |

**What the levels fold** comes from the composition's aggregates. Those were writable and not
readable on `ibDataComposer` — the one setting family without the `Count` / `GetAt` / `Clear`
triple, invisible while only a fetch consumed them and a hole the moment a WINDOW had to show
what it folds. `TotalCount` / `GetTotalAt` / `ClearTotals` / `RemoveTotalAt` close it.

The attribute that owns all this is **`ibValueDataComposition`** (`"DataComposition"`,
`VL_DCMPN`) — a source plus an always-present query plus the settings a user edits, selectable
as a form attribute type beside `Table` and `DynamicList`. Its settings window is its own
([settings/composer](../src/engine/frontend/win/dlgs/settings/composer/)), deliberately not the
list's: a list is browsed and leads with its query, a composition is composed and leads with
its table. Filter and sort are opened from it as separate windows over the same settings object.

⚠ Not to be confused with **`ibValueDataComposer`** (`"DataComposer"`, `VL_CMPS`,
`system/value/valueComposer.cpp`) — the script-side value that RUNS a composition from code
(`New DataComposer("Catalog.Goods")`, `.Execute()`, `.QueryText()`). One executes, the other
declares.

## 4b. The composition's settings window (2026-08-18)

`frontend/win/dlgs/settings/composer/` — a window of its own, deliberately not the list's. A list is
browsed and leads with its query; a composition is composed and leads with its **output**. Sharing
one window and branching inside it is how both end up serving neither.

**⚠ WRITTEN DOWN BECAUSE THE WEB WILL REBUILD THIS.** The layout below is not decoration — every
line of it is a decision about where a question is answered, and the web client has to make the
same ones. Copy the shape, not the widgets.

### The order of the tabs is the order of the decisions

| | |
|---|---|
| **Query** | what is READ. Designer-only: an end user configures the output of a composition somebody else authored, and nothing they did to its query would survive as their setting |
| **Resources** | what is FOLDED — the aggregates |
| **Output** | how it is LAID OUT |

**A composition has no source the way a dynamic list does — its source IS the query.** The list
stands on a main table and refuses to serialise without one (its commands, its icon and the value
a choice hands back all come from there); a composition is complete the moment the text exists.
The `Source` property survives as an *optional* convenience: picking one is what lets a row know
how to open itself and where its commands come from.

The query is edited in the **same styled editor** the query constructor and the list settings use
(SQL lexer, the language's own keywords, line numbers) and the **Query constructor** button opens
on that very text and writes back what it renders. A query is query text wherever it is shown.

### The Output page

```
┌──────────┬─────────────────────────────────────────┐
│ Variants │  toolbar: add grouping / delete / up/dn │
│ +toolbar │                                         │
│          │  the OUTPUT STRUCTURE — Report → levels │
│          │  THE CENTRAL AREA — it takes the room   │
│          ├──────────────── splitter ───────────────┤
│          │  Settings of: <node>          (a BAND)  │
│          │  Available fields │ Selected / Filter / │
│          │  (ONE list, theirs) │ Sort              │
└──────────┴─────────────────────────────────────────┘
```

Rebuilt to the reference layout on 2026-08-19 (Max). Every boundary is a
**splitter**: variants against the rest, the structure against the settings.

- ⭐ **The structure is the CENTRE** (Max: "the group goes on top, it is the central area"). The
  window is about the OUTPUT, so the tree gets the space and the settings sit under it as a band.
  Mechanically: the inner splitter's sash is set from the BOTTOM (the band's height is a known
  quantity, the window's is not yet) with **gravity 1.0**, and the variants' splitter has gravity
  0.0 — so every pixel a resize adds goes to the centre rather than to the two edges.
- **Variants on the left**, with a toolbar of their own, because choosing one reloads everything to
  its right — its own structure, its own filter, its own sort. Groundwork today: one variant exists
  and it *is* the settings; a second needs the settings store first, or the store's arrival becomes
  a migration of somebody's saved variants.
- **The structure above, the settings below.** The settings are about the result the structure
  describes, and how much room a filter needs is the user's business.
- 🛑 **ONE FIELD LIST ON SCREEN, and it is the shared field tree's.** The structure pane has none: a
  field for a new grouping is picked in a **dialog** raised from its toolbar — the same picker the
  editors below use (`ibSettingsFieldTree::ChooseField`). The shape this page had
  earlier the same day — fields + buttons + output, above a panel that also lists fields — put two
  field lists side by side, which read as clutter because it *was* clutter: the same question
  answered twice.
- **The structure is a TREE, not a list**: `Report → level → level`, one child per level, because
  the order IS the nesting and a report grouped by warehouse *then* item is not the other one. It
  is an `ibDataViewCtrl` over a small tree model — everything list- or tree-shaped goes through the
  fork, which is the only place rendering is ours to set.
- A **level is a field AND how it unfolds** — `Elements` / `Elements and hierarchy` /
  `Hierarchy only` (`ibQueryDimUnfold`). Both are cells on the level's own row and both are edited
  as **values**: the field through the picker, the kind through the runtime's quick choice over the
  `GroupKind` enumeration. Neither is a hand-built drop-down — a list of strings beside a registered
  type is a second copy of that enumeration. (Writing the kind into the row's *caption* was the
  earlier attempt and a dead end: a caption cannot be sorted, measured or edited.)
- ⚠ **One buffer, one commit.** The structure edits the settings panel's *transactional buffer*
  (`GetGroupList()`), not the composition's live settings. The panel's `Commit()` clears the
  composer's Filter / Sort / Group and re-applies that buffer, so a level written straight onto the
  live settings would be wiped by the very commit that saves everything else. Cancel discards the
  whole window, structure included.
- ⚠ **The settings below are still the COMPOSITION's, not the node's** — the engine holds one
  filter, one sort and one set of totals for the composer as a whole. The header over the panel
  says which node is selected *and* that the settings are composition-wide, so the window does not
  promise what the engine cannot keep. When totals move to the node, this is where they arrive: the
  layout already asks the question.
- One table and one axis today. That is the **degenerate case of the shape**, not a different
  shape: columns become nodes beside the row levels, a second table a node beside the first.

### Resources — a field, and what is done to it (2026-08-19)

The Resources page speaks the same layout language as Output: a **splitter** between the field tree
and the list, and a **toolbar** over the list. (The column of `>` / `<` / `...` buttons floating
between two panes was the odd one out — three unlabelled arrows, and a proportion fixed at half.)

- **The aggregate the engine admits, per field type**: `ibQueryLowering::AggregatesFor` — number →
  `COUNT/SUM/MIN/MAX/AVG`, date and string → `COUNT/MIN/MAX`, a reference and everything else →
  `COUNT`, an unknown or composite type → all of them (narrowing on a guess would take away a choice
  the query can make). The same door refuses a wrong one at `CheckNames`, so nothing is offered that
  the query then rejects.
- ⭐ **The expression is edited in the ROW**, through the very cell the query constructor's Totals
  tab uses (`queryctor::ibExpressionCellRenderer`): a drop-down of the ready calls over that row's
  field — `SUM(Amount)`, `COUNT(Amount)` — and `...` beside it opening the full expression editor
  for everything the ready calls do not cover (a ratio, a restricted measure). Not a lookalike: one
  answer about which aggregates fit a type, one cell over it.
- Adding a field lands the FIRST aggregate its type admits; **which** one it should be is answered
  in that row. The toolbar therefore carries no aggregate chooser — that was a second place stating
  what a type may be folded by, one gesture away from the cell that states it properly.
- `ibDataComposer::SetTotalAt` — change a line in place, keeping its position. A resource that can
  be added and removed but not edited sends a person round the houses to turn `SUM` into `AVG`.
- ⏳ Missing from the offer: **`COUNT(DISTINCT …)`**. The language parses it (the parser reads
  DISTINCT inside the call); it is not a keyword of its own, so it is not in `AggregatesFor`'s list.

### The constructor is opened with a mask of EXCLUSIONS

`ibShowQueryConstructor(parent, text, metaData, readOnly, exclude)` — `exclude` defaults to zero,
which shows everything; a host names the tabs it owns itself
(`ibQueryExclude_Totals | ibQueryExclude_Order | …`). Max's rule for the shape: *"make it a flag so
they can be combined and extended later, instead of a new boolean each time."*

Two hosts exclude **Totals** today: a composition (its totals ARE its resources and its levels ARE
its groupings) and a dynamic list's arbitrary query (it folds through its own settings).

⚠ **Excluded is not merely hidden.** A `TOTALS` clause in the text — pasted, or written before the
rule existed — is DROPPED from the package on the way in, because what no tab can show, nobody can
remove either. On the composition's side the same refusal is stated a second time, where the text is
described: a `TOTALS` there raises the query error rather than being quietly ignored.

### What the inspector shows

**A composition: "Settings…" and nothing else.** `Source` and `Query text` are hidden: a
composition's source IS its query, and the query is edited inside the settings window on the tab
that exists for it — with the styled editor, the constructor button and the engine's verdict. Two
doors to one text, one of them a bare string field, is how the two drift.

**A dynamic list: Source, Settings, Dynamic data read.** The arbitrary-query flag and its text are
hidden for the same reason; the Source stays, because a list stands on a main table and its settings
work over that table. That difference is the whole difference between the two.

Both keep the properties — serialised, settable from a generated form — they are simply not offered.

⚠ **The mechanism had a hole worth remembering.** A held value's properties surface through the form
attribute HOLDER (`AttachPropertyObject`), and the inspector selects the holder: it refreshed only
that, so an attached value's `OnPropertyRefresh` was never called — and `HideProperty` on an attached
object talked to an empty notifier list, because the notifier lives on the owner. Both were fixed
where they belong, in `ibPropertyObject`: `OnRefresh` now asks every attached object, and
`HideProperty` walks UP the attach chain. Anything attached can now decide what it shows.

⚠ A table, a dynamic list and a composition are offered by the attribute's type **drop-down** AND by
the "Select data type" dialog. They were missing from the dialog — two lists of what a table-shaped
attribute may be, and the one a person reaches for second was the incomplete one. `ibTypesForKind`
vends them now, so both roads answer the same.
### Parameters — what the query asks for, and who fills it in (2026-08-19)

**Two authors, one list.** A parameter is either written into the query (`&Period` — the lexer finds
it, in first-appearance order, repeats collapsed) or added by hand. The list follows the text: names
appear as soon as the query is applied, auto ones the text stopped mentioning disappear, hand-made
ones stay — and anything already filled in survives a re-parse. An auto parameter cannot be removed
on the page: it is in the text, and the next parse would put it straight back.

**Four separate questions, four columns** — name, value, **type**, expression, "for user". Value and
expression are one answer given twice, so choosing a value clears the expression; the row shows what
will actually be sent.

- **The declared type** is edited through the product's own type picker (the dialog an attribute's
  Type opens). Empty reads as `<any>` — a real answer, meaning "whatever the expression produces".
  When a type IS declared it wins: the value is adjusted to it (`AdjustValue`), otherwise the result
  of the evaluation decides. A parameter has no type of its own until somebody gives it one.
- **The expression is evaluated once, before the read** — and that is exactly why a call into a
  common module is legitimate here while a computed FIELD is not: a field would run per row and take
  the read into memory with it.

⚠ **Where the evaluation happens: at the moment the query runs, and for BOTH readers.** A
composition is also a MODEL, so a fetch reads it too; a parameter filled only on the report path
would read as "works in the report, empty in the list". `PrepareParametersForRun()` is the one point,
called from `Compose` and from `RunComposerPage`. Not when the settings are edited, either:
`CurrentDate()` must mean the day the report is BUILT.

⚠ **And it evaluates against the session ROOT.** `Evaluate` runs against the CURRENT run context, and
pressing "Generate" is not a script — the context stack is empty and the call returns false without
a word. With no frame of its own, the root module's is borrowed (its ProcUnit, its bytecode), which
is what makes common modules and the whole environment answer.

### The syntax check is the TRANSLATOR's job, not the executor's

`OK` on the expression editor — and `OK` on the settings window — **compile** the expression; they
never run it. The check is one function used in both places, so they cannot disagree about what is
valid.

🛑 The first version checked by trial evaluation and passed everything. `ibProcUnit::Evaluate` starts
with `if (text empty || runContext == nullptr) return false;` — **silently**, no exception — and in
the Designer there is no runtime at all (a session that runs no scripts has no root module). The
lesson generalises: *a function that answers `false` when a precondition is missing turns any check
built on it into "all fine".*

🛑 And a bare compile knows **no names**: `CurrentDate()` came back as "procedure or function not
detected", which is true about an empty world and useless about the expression. The parent comes from
the compile cache of the ROOT descriptor — `metaData → GetCompileCache() → FindCompileModule(root) →
GetCompileModule()`, the same road the code editor's intellisense takes — because `SetParent` wires
the BYTECODE chain, and that chain is what name resolution walks.

The expression is compiled as the **body of a function** (`function __check(){ return <expr>; }` in
CES, `Function … Return … EndFunction` in VES) so that it is judged as an expression; loose, a stray
assignment would pass for a module statement.

The window **asks** rather than refuses on the way out ("some expressions do not compile — close
anyway?"): a half-written expression is a legitimate state to leave behind, and losing the rest of
the settings over it would be worse. It checks every parameter, because a cell can be typed into
without ever opening the editor.

### Cells and menus

- The expression and type cells are **plain text with a "..."** — no drop-down. The query
  constructor's cell is a combo because a totals expression is nearly always one of the ready calls;
  a parameter has no such list, so the arrow opened a menu of one entry: the text already there.
- The "..." opens an **Expression editor** built on the product's code editor, given the shared font
  / colour and editor settings — that pair is what supplies highlighting and line numbers, and
  without it the control is plain black text (highlighting is not built in: it colours what it lexes
  with the colours it was handed).
- **Every list in the window answers the right hand**: structure, variants, resources and parameters
  all carry a context menu whose items are the toolbar's own ids and end in the toolbar's handlers.

### What is stored

The composition writes its **variants**, its **parameters** and its **resources** into the same node,
each as its own child kind, so the reader takes what it recognises and the three lists do not disturb
each other. A parameter travels whole: value (packed through the value's own serialisation, so a
reference stays a reference), expression, declared type, and who fills it in.

🛑 **The resources were not written anywhere at all (found 2026-08-20).** A resource lives in the
STORE — `ibDataComposer`'s totals — and a variant packs only its `ibValueListSettings`: filter, order,
group. That object has no totals, so no writer on any path ever put one on a node. The report was
saved with its structure and came back with the SHAPE of a report and **none of its numbers**: the
levels were there, the folds were gone. It is a data-loss defect, not a missing feature, and it was
found sideways — while asking why a resource edit was not *announced*, it turned out it was not even
*kept*. `ibValueDataComposition::WriteTotals` / `ReadTotals` close it; **why they write at
composition level and not per variant** is in [serialization-io.md § 4c](serialization-io.md).

🛑 **What is on screen IS the composition — the window has no "unapplied" state.** The query
text used to reach the composition only through the **Apply** button on the Query page: a query
typed and then OK'd stayed behind in the editor, so the report saved with an EMPTY source, and the
runtime opened on a composition with no fields, no parameters and `Composer: no source is set` on
Generate (Max, 2026-08-19: *"the runtime sees nothing"*).

The lesson generalises past this window: *an editor that only publishes on a button press is a
second copy of the data, and the copy is the one the user is looking at.* The file was what settled
it — the saved `.orp` carried the composition's keys (`Query`, `ActiveVariant`) with an empty string
behind them, which says "written faithfully, never filled in" and rules the serialisation out.

⭐⭐ **So the button is gone (2026-08-20), and that is a collapse rather than a feature removed.**
Apply existed to reconcile two copies of one fact; with one copy there is nothing to reconcile.
`wxEVT_STC_MODIFIED` — filtered to `wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT`, exactly as
`ibCodeEditor::OnTextChange` filters, because `wxSTC_MOD_*` also reports styling, folding and
selection — stores the text on **every keystroke**, and the SOURCE is re-read on **idle**, when the
typing stops. Idle rather than a timer: it fires when the queue empties, which IS "the user paused",
and there is nothing to start, stop or cancel.

That split is why `SetQueryText` no longer calls `RebuildSource`. **Storing what the text IS and
working out what it MEANS became two moments** — the second costs a describe, a parameter sync and
a settings prune, which per character would make a long query unusable to type in. The verb for it
already existed and is public: `ApplySource()`. One call moved out to the caller that always made
it anyway; leaving the page and accepting the window still force it (`ApplyPendingQueryText`), so
neither can act on a stale source.

⚡ **And the CHANGE SIGNAL rides the same split.** `SetQueryText` deliberately raises nothing — it
is the one writing door on the composition that does not (§ 4f). What hears the signal may be a form
attribute, and its reaction is to re-render the whole form editor: control tree, object tree,
attribute tree, drop targets. Raised per character, that redrew a form per keystroke. The
announcement lives instead where the source is re-read — `RefreshFromQueryText`, the idle pass after
the typing stops — which to a person is still immediate. The rule generalises:
**the cost of a signal belongs to its LISTENER, so a signal raised per character has to be measured
against the most expensive listener, not the cheapest**
([property-system.md § 5.2](property-system.md)).

⭐ **The handler is bound AFTER the initial `SetText`** — order instead of a flag. The fill is not
somebody typing, and a handler bound before it would report the composition as modified the instant
the tab was opened.

⚠ The **gridbox** stores its `Source` too — that was missed at first, and everything else about the
control survived a save while the binding did not, so a reopened form pointed at nothing and the
report looked like it had lost its composition.

⏳ Still open: parameters are not part of a VARIANT's snapshot (switching variants changes the filter
and the structure but leaves the period), the page-cache signature is not taken from the EVALUATED
value, and "for user" is stored but nothing yet reads it — quick settings on a form do not exist.

### Variants — N snapshots of the settings, one of them active (2026-08-19)

> "A variant is a snapshot — its own groupings, its own filter, its own sort, as if every variant
> were a page of settings of its own. One report answers *sales* and *sales plus turnover*; the
> person just picks another variant." (Max)

- **The store is the composition** (`ibValueDataComposition`): a vector of `{ name, settings }`
  where the settings object is a **buffer**-mode `ibValueListSettings` — own storage, because a
  snapshot that wrote through to the composer would not be a snapshot.
- 🛑 **There is always at least one.** The constructor makes it, `RemoveVariant` refuses the last
  one, and both live in the STORE rather than in a window — a composition built from script has the
  invariant too.
- ⚠ **The composer holds exactly one set of settings, deliberately.** The fetch, the compose driver
  and every reader below stay unaware variants exist. A variant becomes real by being *loaded into*
  the composer, and the two doors that do it are the pair the settings dialog already uses:
  `ibCommitSettingsToComposer` (snapshot → store) and `ibLoadSettingsFromComposer` (store →
  snapshot). No third road, so there is no second copy of the rules about what a setting is.
- **Switching costs**: commit what is on screen → `CaptureActiveVariant` (composer → the variant
  being left) → `SetActiveVariant` (the other snapshot → composer) → the panel and the structure
  tree re-read. Miss the capture and switching away silently discards an edit.
- **Add / Copy / Delete** on the variants toolbar. A copy copies *everything* — it is made by the
  store through the node the settings serialise into, so it carries whatever a settings object
  consists of today and whatever is added to it later. Delete greys out on the last variant.
- **Serialisation is the same node tree** (`WriteVariants` / `ReadVariants` over `ibDataNode`) — so
  the whole set gets JSON, diffing and the AI door for free, and a variant shows up as its own
  branch of the structure rather than as an opaque blob. A record written *before* variants existed
  has no variant children: it is read the way it was written and becomes the first variant's
  snapshot, so nobody's saved settings turn into a migration.
- ⭐ The **active** variant is written from the COMPOSER, not from its snapshot: a script that adds
  a filter writes to the composer, so the snapshot beside it is the stale copy. That is what makes
  "save" mean the same thing however the change was made.
- **Cancel restores every variant.** Switching inside the window writes (the composer holds one set
  at a time), so the whole set is snapshotted on open and put back if the window is cancelled.

⏳ Not yet: parameters are not part of the snapshot (the composition has none yet); a variant has no
"show this one to the user" flag — the layer it belongs to (author → defaults → variant → quick
settings) is decided, the flag is not built.
### The row-value cell is shared, and lives outside both windows

`win/dlgs/rowValueCell.h` — `ibRowValueCellRenderer`, lifted out of `listSettings.cpp` on
2026-08-19. It is the cell every settings grid edits its lines through: it asks the row for a
VALUE and lets the value decide how it is chosen (a composition field opens the source-tree picker,
an enumeration opens the runtime's quick choice). The composition's structure tree edits exactly
the same two things a grouping line does, so it uses that cell rather than a smaller lookalike —
two cells over the same values would be two sets of rules. What did **not** move is the picker:
knowing which fields exist belongs to whoever owns the source, so the cell takes it as a callback.

### TWO WORLDS, and exactly two shared editors (2026-08-20)

A dynamic list's settings and a composition's settings are **two different windows for two
different questions**, and neither is a case of the other (Max: *"do not mix them — two different
worlds. A dynamic list has its own grouping; what is really shared is the filter and the sort. And
a composer uses those only as particular cases for each of its own lines"*).

They live one folder apart, with what they share at its root:

```
win/dlgs/settings/
  settingsFieldTree.h/.cpp     WHICH FIELDS EXIST — the left pane of every editor, and the picker
  settingsFilterEditor.h/.cpp  the FILTER editor  (tree of And/Or/Not groups + the value cell)
  settingsSortEditor.h/.cpp    the SORT editor
  settingsStyle.h              art / grid styling / "stand on the last row"
  filterTreeModel.h/.cpp       the filter tree's model
  list/     listSettings.*     the LIST's world: arbitrary query + its own flat grouping
  composer/ composerSettings.* the REPORT's world: structure, resources, parameters, variants
```

Why those two and nothing else: the filter is a **tree** with a type-driven value cell and the sort
a model-driven grid with its own verbs — a smaller lookalike beside either would be a second set of
rules about one settings object. Everything ELSE differs by nature: a list folds by a flat ordered
list, a composition by a STRUCTURE of levels.

- Each editor is a `wxPanel` over a **buffer** (`ibValueListSettings`) and a **field tree**, and
  knows nothing about a model, a list or a report. Each world owns its buffer and commits it.
- The list's window embeds them as tabs. The composition's window embeds the same pair over its own
  buffer — whose GROUP list is the report's structure ladder.
- `Commit()` stays with the host: it returns **false** when a setting is half-written (it has
  already said so) and the window stays open on the objection. The composition splits its own into
  `CommitSettings()` — the settings half alone — because a VARIANT SWITCH does exactly that much.

🛑 **What this replaced, and how the mixing showed itself.** The composition's window used to embed
`ibListSettingsPanel` with some tabs switched off, reached by `static_cast<ibValueModel*>(composer)`
— a cast to a base the composition does not have. And its STRUCTURE was read out of that panel's
buffer (`m_settingsPanel->GetGroupList()`), so a report's own ladder lived inside the list window's
transaction. Both are the same defect seen twice: **a cast to a base a type does not derive, and a
type's own data held in another type's buffer, are the shape of two worlds welded together.**

🪤 **A shared header only shares if it is INCLUDED.** `settingsStyle.h` was written by the same split,
to spell the look ONCE for both worlds — one picture per verb, the command-append helper, the grid
styling, "stand on the last row". `composerSettings.cpp` then re-declared two of those helpers **byte
for byte** under composer-flavoured names and called `ibStyleSettingsGrid` on none of its four grids,
so the composition's tables kept the dialog's flat grey while the list's did not — the drift the
header exists to prevent, restarted by the file it was written for. Collapsed onto the header
(2026-08-20). A folder split says where a thing belongs; only the `#include` makes it so.

⚠ The projection page (`Page_Select`) is gone from the list's panel: its members and four handlers
were declared and **never written**, while the composition's window asked for it — a window asking
for a tab that does not exist. What is OUTPUT is the composition's question and belongs to its own
window when it is built. Reading it back already exists on `ibDataComposer` (`SelectCount` /
`GetSelectAt` / `ClearSelected` / `RemoveSelectAt`) — the **third** setting family found write-only,
and worth remembering as a shape: a setting only a fetch consumes never needs reading back, so the
gap stays invisible until a WINDOW has to show what it is doing.

### Everything moves by mouse

A drag from the field tree and a press of the `>` button are the **same verb reached two ways**, so
every drop target raises the handler the button raises (`ibCallbackDropTarget`, the mechanism the
query constructor already uses). The drag carries no payload of its own — what is being moved is
what the source tree has selected.

⚠ The begin-drag event is deliberately **not** `Allow()`ed: allowing it starts wxTreeCtrl's own
native drag beside ours, and MSW then refuses the second `BeginDrag` with an assert.

⚠ Where there is no field tree there is no drag, and that is the point: the Output page's structure
pane deliberately has none, so "add grouping" is a toolbar button that opens the picker. The drag
stays where a field list is already on screen — the Resources page and the settings panel's tabs.

### The shape L5 is growing into (Max, 2026-08-19 — direction, not yet built)

**One entry point for a list and for a report.** A composition takes a QUERY on the way in — the
same way in both cases. What differs is only which parts of it the user interface offers:

| | list | report |
|---|---|---|
| input | a query | the same query |
| grouping | a **flat ladder** — levels added one under another | a **table**: rows *and* columns |
| several tables | — | yes, each with its own axes and its own totals |
| resources | none today (and the reason is the UI, not the engine — grouped rows could carry totals) | the point of the thing |

So the cross-table is a **variety of the table**, not a separate mechanism, and the list's flat
ladder is a table whose column set is empty. The engine has to be able to do all of it; a list
withholding it is a decision about what to OFFER, which is a different thing from not being able —
and the difference always surfaces later if it is left implicit.

⚠ **What that implies and is not true yet:** totals are held by the composer as a whole
(`m_totals`), while several tables each computing their own totals means totals belong to the
NODE. Same for filter and sort, which the settings panel edits per composition today. The output
tree already has the right shape (`Report → Table → Rows`); the engine is the half that still
assumes one of everything.

### Why a resource is a TRIPLE, and what is deliberately absent

A resource here is **(expression, stage, fold)**. The *stage* says where the value lives — on the
row, or on the group node — so "which groupings should this be recomputed over" is not a question
the author has to answer, and the classic wrong answer (a sum of ratios) is not expressible at all.
Which aggregates a field may take is asked of the engine, once: `ibQueryLowering::AggregatesFor` is
read as a REFUSAL by `CheckNames` and as WHAT-TO-OFFER by a window, so a string is never offered
`SUM` in one place and rejected for it in another.

Not built, and on purpose: a chart (later), a nested report (N+1 reads where nested groupings answer
the same question in one), and settings held **per node** — ours are global to the composition for
now. That last one shows up with the second node, not the first.

## 4c. Where a composed report appears — the GRIDBOX (2026-08-19)

A composition composes INTO a spreadsheet document (§4a). The control that shows one is the
**gridbox** (`ibValueGridBox`), and on 2026-08-19 it grew from a decorative sheet into a real
control — Max's words: *"the gridbox has to become a control; like the tablebox, it can carry a
command interface"*.

### It shows a VARIABLE, not a document of its own

- `SpreadsheetDocument` is now a **form-attribute type** (drop-down and type dialog both offer it),
  with a named clsid instead of one derived from its registration key.
- The gridbox has a **Source** property, and it takes exactly **two** types: a **spreadsheet
  document** — the hand-filled sheet, a printable form — and a **composition** — the report.
  Everything else the picker refuses, so the binding cannot be pointed at something the box could
  not show.
- What the box holds is a **model**, taken from the binding at the three moments it can change (see
  *The spreadsheet model* below) and never cached beyond them: an attribute's value is replaced
  whenever the form re-reads its data, and a control holding the previous object would paint a sheet
  nobody writes into any more. With no source it falls back to a document of its own — a bare gridbox
  is still a usable sheet.
- Dropping a **composition** or a **document** onto a form builds a gridbox bound to it; adding a
  bare gridbox **provisions its own attribute** (`AutoBindNewSource`), the same door the tablebox
  and the checkbox use. Which TYPE that attribute gets is now the control's own answer
  (`GetDefaultSourceType`) rather than a case in the shared "kind → default type" switch.

### Two verbs, and only for a report

With a composition as its source the box carries **Compose** and **Settings**: Compose fills the
sheet the box is showing, Settings opens the composition's own window — the one the designer property
opens. With a plain document it carries none: printing and saving belong to the document's own
surface, and repeating them here would be a second set of the same commands.

Being a command source at all required the box to become an `ibValueWindowComposite` — that is what
carries a command bar and what the form's command walk descends into.

🪤 **And that base change silently invalidated two casts.** A composite's `GetWxObject()` is the layer
CANVAS, not the widget: the grid sits behind `GetInnerWx()`. Two sites in `gridBox.cpp` went on casting
the outer object to `ibGridEditor*` and were null from that day on — `CreatePrintout()` printed
nothing and `SetControlValue` never handed the new document to the window, neither of them saying so
([form-engine.md § 5a](form-engine.md) for the rule).

⭐ **The verbs are the MODEL's, not the control's** (2026-08-20). What can be done is a fact about
what is bound — a composition offers those two, a spreadsheet document offers none — so the model is
a command STORE (`GetCommandCollection` / `CallAsModelCommand`) and the gridbox only lays the store
out into real actions, exactly as a tablebox does with a table model. The same verbs therefore appear
wherever a composition is shown, and their ids are named once (`ibSpreadsheetModelCommand`).

### The SPREADSHEET MODEL — what the box actually holds (2026-08-20)

The gridbox holds **one reference, and it is a model** — `ibValuePtr<ibValueSpreadsheetModel>`
(`backend/spreadsheetModel.h`), which is to a sheet what `ibValueModel` is to a table:

```
runtime
  └── spreadsheet model            (abstract — nobody instantiates it)
        ├── spreadsheet document   (the hand-filled sheet)
        └── composition            (builds one)
```

- **The model holds the backend sheet and hands it back** (`GetSpreadsheetDocument`). There is no
  second place a document could live, so the box cannot end up showing one sheet while a run fills
  another. With nothing bound the box is born holding a document of its own — which is what keeps a
  bare gridbox a usable sheet.
- **One act differs: `Compose()`** — produce the data. A composition reads its query, lays the result
  out and installs the finished sheet; a hand-filled document already IS its result.
- ⚠ **The fetch is an EVENT on the base, nothing more** (Max: *"make the async fetch just an event on
  the base model and let the composer override it itself"*). `SubmitFetchAsync` on the base simply
  runs the work; the **composition** overrides it, rents the background run and keeps the handle, so
  the job manager is named in exactly ONE place — the thing that actually READS. A document rents
  nothing because it reads nothing, and `CancelFetch` on the base has nothing to stop.
- A **second Compose while one is in flight cancels the first**: one sheet, one slot. The composition
  waits its run out in its destructor, so a read cannot outlive what it is filling.
- The model arrives through **one door** (`SetControlValue`, which `SetPropVal(Value)` forwards to),
  and the binding is taken by `RefreshModel()` at the three moments it can differ from what is held —
  the form is built (`InitializeControl`), the window is created, the Source is set. That is the
  tablebox's mechanism ported, as one function instead of its `CreateModel` / `RefreshModel` pair.
- The compose runs off-thread and reports back through `CallAfter`, which can land after the form
  closed — so the delivery hop carries an **alive token** (`std::shared_ptr<bool>`), the same one the
  paged dataview carries, and for the same reason: a `wxWeakRef` writes into the control as it dies
  and would race the copy the worker holds.

`Compose` has **two entrances and one routine**: `Compose()` builds into a fresh document and swaps
it in (writing into the shown one would fire its notifiers from a worker thread), while
`Compose(document)` — the script's `Compose(Document)` — fills a document the caller owns. Same read,
same layout; only where the result lands differs, so no finished sheet is ever copied afterwards.

### 🛑 A composition is NOT a table — and that was answered TWICE

The composition used to read as a list everywhere in the designer: dragging it built a **tablebox**,
the tablebox's picker offered it, and the gridbox's picker — the control that actually shows a
report — offered nothing. Two independent places answered "is this a table":

| | |
|---|---|
| `ibSourceDataObject::IsTableSource()` | the **tableSection flag** on the source explorer's root |
| `ibValue::IsTableValue()` | a **static on the class**, which `ibValueModel` declares true for every model — asked by the class factory **by clsid**, before any instance exists |

Both are now false for `ibValueDataComposition`. It stays a model inside (the walk still reads its
rows); as a VALUE on a form it is shown by the gridbox, and what appears there is the document.

⚠ The general lesson, worth more than the fix: **clearing one flag is not clearing the answer** —
look for the second place that answers the same question, especially a static one keyed by clsid.

⚠ And a control that declares a `Source` **must** derive `ibTypeControlFactory`:
`ibPropertySource::CreateVariantData` dynamic_casts its owner to the source factory and returns
NULL otherwise, so the property ends up with no variant at all and the first question asked of it
asserts. That is how the first drop crashed (designer_5084) — the field was copied, the contract
was not.

## 4d. How a composed report is LAID OUT (2026-08-19)

The shape: a narrow empty margin, the groupings read DOWN one column, the resources as columns of
their own with the figures to the right.

**The layout is the report's, not the query's.** A schema is a flat list of columns; a report is
three different things laid out three different ways. So the lowering now STAMPS what each output
column is for — `ibQueryLowering::ibColumnRole`: `Dimension` (a `TOTALS BY` level), `Measure` (a
`TOTALS` aggregate — what the settings call a RESOURCE), `Detail` (an ordinary projected field) —
and the driver lays out by role:

| role | where it goes |
|---|---|
| Dimension | ALL levels share ONE column, each indented under the one above; the header carries one line per level, so the header says what the groupings CONSIST OF |
| Measure | a column each, numbers right-aligned — the figures the eye scans and compares |
| Detail | a column each (a query with no TOTALS at all is the ordinary flat case) |

⭐ The roles are stamped where they are KNOWN — `ExecuteTotals` builds the dimensions from
`TOTALS BY` and the measures from the aggregate list, so nothing downstream has to re-derive them
from position or from type. A consumer handed only names gets it wrong the first time a query
projects a number that is not a measure.

**Groupings come from the top.** The header first (what the groupings are), then group 1 with its
elements folded under it, then group 2, 3, 4 — as many as the structure declares.

⭐⭐ **THE DRIVER SAYS A LEVEL, AND NOTHING ELSE.** Each row is built as its own one-row document and
handed over with the depth it sits at — `PutArea(row, level)`. That is the whole contract: where the
row lands, how far the group it opens reaches, which line carries the fold marker all follow from the
ORDER the rows arrived in, and belong to the document and the grid.

What the grid makes of the sequence (`ibGrid::NormalizeRowGroups`):

- a row followed by DEEPER rows is a **heading** — it stays visible, and what folds is the run below
  it;
- a row nothing deeper follows is a **leaf**: it heads nothing, so it gets no marker at all;
- level 0 is the sheet itself and is never drawn.

The marker sits on the HEADING, not on the first row it hides — drawn on the first hidden row it
lands on a line that may have no value of its own, which is exactly what "the plus is in the wrong
place" looks like.

🛑 A group is a range of row NUMBERS, so it dies with the rows: clearing the sheet clears the
outline. Leaving them behind crashed the outline pane on the second compose — it painted from ranges
whose rows no longer existed.

⚠ The shaping is not idempotent: it turns headings into the ranges they fold, so it runs once over
the WHOLE list (on load), never per incoming group.

⏳ **DETAIL RECORDS** — a structure node with NO field is the remaining portion written out row by
row: everything the groupings above it have not folded away, spelled in full under its parent. Named
here because the layout already has a place for it (the `Detail` role); the node itself is not built
yet.

A grouping row
opens its outline group AFTER itself, so the caption stays visible when the group is collapsed, and
it is painted (a paler tint per level) AND bold — the tint reads while scrolling, the weight still
reads in print. **Totals belong to whatever was named a RESOURCE** (Max): dimensions are not summed.

**Column 0 is an empty margin** (narrow, never written into) — the margin a printed report has, and
where the outline's fold markers sit without crowding the first value. Every other column is sized
from its own content: there is no device context in the backend, so the width is counted in
CHARACTERS times an average glyph, clamped so one long string cannot push the rest off screen.

🔒 **The document comes up read-only** (`EnableEditing(false)` at the end of the walk). Max:
"by default the table is read-only, to guarantee it stays as composed and that drill-down works" —
typing into a composed cell would leave numbers that no longer follow from the query, and a cell
that no longer matches the value bound behind it.

## 4e. Composing is a READ, and it runs like one (2026-08-20)

Pressing **Generate** does not freeze the window. The composition IS a model, so it uses the door a
list already uses for its pages — `SubmitFetchAsync`, which rents a background run with a connection
of its own and serialises one read at a time. Three rules hold it together:

- **It composes into a document of its own.** The shown document notifies the grid on every cell; a
  worker writing into it would be touching GUI from another thread, and the report would be watched
  being built. The finished document REPLACES the value in one step
  (`ibValueSpreadsheetDocument::SetSpreadsheetDocument`) and the grid re-reads it.
- 🛑 **The whole document travels, not its description.** What a report is made of lives on the
  OBJECT: the drill-down parameters (the values behind the cells) and the read-only mode. Publishing
  the description alone left every cell bound to a parameter name nothing answered to — the report
  looked right and stopped opening anything.
- **The window says it is busy** (a spinner over the sheet) and **a closing form says stop**:
  `ibValueModel::CancelFetch` raises the same cooperative flag the interpreter obeys and waits the
  run out, so a report nobody is waiting for stops holding a connection.

⚠ Do NOT poll that flag inside the composition walk. It was tried on 2026-08-20 and taken out the
same day: the flag belongs to a SESSION, a stale one left over from an earlier cancel aborted the
very next compose on its first row, and an interrupt is not an error — so the report simply never
appeared, silently.

### What the tests pin

`tests/test_spreadsheetCompose.cpp` drives the driver with the same calls the composer makes and
reads the DOCUMENT back — no window, no database. That is the point of putting the output in the
backend: the shape of a report is assertable with nothing on screen.

Pinned as of 2026-08-20: groupings share one column while resources take their own and the header
is as tall as the grouping is deep; the indent rides on the grouping column; a row's LEVEL becomes
an outline group; the composed document comes up read-only; columns are sized from their content;
every non-empty cell carries its value (what that means is `ibValue::ShowValue`'s answer, not a list
of "openable" types here); composing twice replaces rather than appends.

### A refusal has to be SEEN

`RebuildSource` asks the engine to describe the query and keeps its refusal verbatim in
`m_queryError`. That field was read only by the settings window, so pressing Generate over a query
the engine had already refused did nothing at all — no rows, no message. `Compose` now refuses with
the engine's own words, and the gridbox shows them where they cannot be missed rather than in a log
panel that may not be open (Max, 2026-08-20: "I press compose and it quietly dies").

## 4f. A COMPOSER is declared in the report — the metatype (2026-08-20)

The composition stopped being something a person adds as a form attribute and became what a report
DECLARES. `ibValueMetaObjectComposer` (`"Composer"`, `g_metaComposerCLSID`) sits inside the report
the way a form, a template or a tabular section does — and that is the whole difference (Max: *"an
attribute you add by hand; this type lives in the object"*).

| | |
|---|---|
| **declared** | a Composers group under the report, beside Forms and Templates |
| **edited** | its own TAB — `docViewComposer`, registered in the docManager like the template editor — showing the composition's settings panel |
| **reached** | `Report.<Name>`: the object publishes each declared composer as a field and holds a LIVE composition per composer, seeded from the metaobject |
| **default** | `DefaultComposer` on the report, by the same shape `DefaultFormObject` has, with the same pair of hooks — the FIRST composer declared becomes it, removing it clears the property |

⭐ **A composer is also a COLUMN of its report** (`ibBackendSourceColumn`), and that is not
decoration: the source-binding walk returns a source column as the LEAF, so a node with no descriptor
cannot be walked to. Without it the composer showed in the picker and nowhere else — the inspector
fell back to the owner's type (`ReportObject.Report1` instead of the composition) and DRAGGING one
onto a form created nothing, because the drop asks the walk for the leaf's type first.

⚠ **The settings a metaobject holds are the DEFAULT of the user's settings**, not a separate author's
copy: the object gets its own composition seeded from it, so running a report never writes into the
configuration. Saved settings (the next arc) replace that default as a SNAPSHOT; what no longer
resolves is dropped by `ibDataComposer::PruneUnresolvedSettings`, which answers HOW MUCH went — so a
person can be told rather than left with a report that quietly lost a level.

### What a composer STORES — a PROPERTY, not children (2026-08-20)

`ibValueMetaObjectComposer::ReadData` / `WriteData` are the same two symmetric lines
`ibValueMetaObjectSpreadsheet` has — one property out, one property in:

```cpp
m_propertyComposition->SetNodeValue(node.GetProperty(m_propertyComposition->GetName()));
node.SetProperty(m_propertyComposition->GetName(), m_propertyComposition->GetNodeValue());
```

The composition lives in **`ibPropertyComposition`**
(`backend/propertyManager/property/propertyComposition.{h,cpp}`), modelled on `ibPropertySpreadsheet`:
it OWNS the `ibValueDataComposition`, hands it out through `GetComposition()` — never a copy, because
the settings window edits that very object — and packs it whole into ONE `ibDataValue::Child` node.
There is no `m_composition` member beside it any more: the property is the single owner, so what is
edited and what is saved cannot become two things. `SetDataValue` refuses — a composition is
configured, not assigned over.

🛑 **What this replaced, and it cost whole files.** The metatype used to hand its OWN node to
`ibValueDataComposition::WriteProperty`, which writes one child node per VARIANT and one per
PARAMETER — keyed by the LOOP INDEX and clsid'd `CompositionVariant` / `CompositionParameter`,
synthetic ids of kind `None`. Those landed among the metaobject's CHILDREN, the area where a
`(clsid, metaId)` pair means an object's identity. The save succeeded; on the next open the factory
had no such metatype and the report never came back — *"Error creating object
'67799176431653306'"*, which is exactly `make_clsid("CompositionVariant", ibClassKind_None)` read as
a number. The law it broke is now enforced one floor down, for every metatype
([serialization-io.md §4b](serialization-io.md)).

⚠ **The on-disk layout changed with the fix.** A report written by an earlier build carried its
composition in the children area; nothing reads it there now, so those files do not load. The
platform is pre-release and no gate was added for them.

### Editing a composer marks the CONFIGURATION modified (2026-08-20)

Everything a person did in the composer's tab left the configuration looking untouched — Save had
nothing to do, and the work was gone on the next open (Max: *"absolutely anything that changes in
the composer — it does not react at all"*). Nothing new was invented to fix it. The signal already
existed and was already being raised: `ibPropertyObject::OnChildChanged()`, payload-free, bubbling
up the attach-owner chain ([property-system.md § 5.2](property-system.md)).

**The listener was the missing half.** The settings panel raised the signal on Commit, and it
bubbled to `nullptr`, because nothing above the composition listened. `ibValueMetaObjectComposer`'s
constructor now names itself the composition's attach owner, and its `OnChildChanged()` override
does the one thing there is to do:

```cpp
if (ibValueDataComposition* composition = GetComposition())
    composition->SetAttachOwner(this);            // in the ctor — a STRUCTURAL owner
...
void ibValueMetaObjectComposer::OnChildChanged() {
    if (m_metaData != nullptr) m_metaData->Modify(true);
    ibValueMetaObject::OnChildChanged();          // keep bubbling — a composer nested under something
}                                                 // that also cares stays heard
```

Modified-ness lives on the **metadata**, not on the metaobject and not on the tab: a metaobject
carries no dirty flag of its own, and `ibMetaDocument::Modify` delegates to `ibMetaData::Modify`
too ([metadata-lifecycle.md § 6](metadata-lifecycle.md)). ⚠ The owner is set **here and nowhere
else**: at runtime a composition has no metaobject above it, so the same value stays silent by
construction rather than by a flag.

⭐ One thing comes free with it. `ibValueDataComposition::GetMetaData()` — *which config do I resolve
names against* — walks that same upward edge, so a composition edited in the designer now answers
with its own container's metadata instead of falling back to the ACTIVE one. For an EXTERNAL report,
whose metadata is its own file, that is the difference between resolving against itself and
resolving against whatever configuration happens to be open.

🛑 **And Commit was the wrong moment to raise it from.** Only the filter/sort/group buffer is
transactional; the resources, the parameters and the variants are written LIVE and survive Cancel,
so a commit-time signal missed every one of them. Every door on `ibValueDataComposition` that
writes now raises the signal itself — `SetSource` / `SetSourceQueryable`, the
variant verbs (name, add, remove, activate — ⚠ *switching* a variant IS writing, this composition
holds one set of settings at a time), the six parameter verbs, `AddFilter` / `AddSort` / `AddGroup`,
and the resource verbs. `SetQueryText` is the single deliberate exception, and for a cost reason
rather than a correctness one — see § *What is stored*, ⚡.

⚠ **One deliberate silence:** the query-parameter SYNC. `SyncParametersWithQuery` pushes its own
entries and does not come through `AddParameter`, so re-reading the text adds and drops auto
parameters without announcing anything — *a composition catching up with its own text is not a
change anybody made*.

🛑 **Resources were edited PAST the composition.** Three call sites — the resource grid's cell
write, the toolbar's edit and its delete — reached into the store it holds
(`ibDataComposer::SetTotalAt` / `RemoveTotalAt`), so the composition could not hear of the edit even
in principle. It gained `SetTotal` / `RemoveTotal` beside the `AddTotal` it already had and all
three sites were repointed. The general shape is worth keeping: **a holder that can be reached past
cannot be responsible for what happens to it.**

⚠ The tab also opens **read-only** (`docViewComposer` makes the same `SetReadOnly(flags ==
ibDOC_READONLY)` house call every other designer editor makes), and that is the one case where the
panel commits nothing and still answers *yes* — otherwise a look-only session would write itself
back on close and raise this very signal doing it
([designer-editors.md § 4b](designer-editors.md)).

#### 🛑 A commit that announces unconditionally also announces a window that was only OPENED

The read-only case above is the loud half of a quieter defect that outlived it. **Closing a designer
tab IS accepting it**, so `ibComposerSettingsPanel::Commit()` runs every single time a composer is
merely *looked at* — and it landed the buffer and called `CaptureActiveVariant()` every time. A
composer opened and closed with nothing touched came back as *"the configuration changed"*, asterisk
and all; view-only hid it only for the tabs that were explicitly read-only.

Both acts are now gated on one fact — `m_settingsDirty`, set by `MarkSettingsTouched()`, which is
**also** what raises the signal. Keeping the two together is the point: the change is announced where
it is MADE (the shared editors' `SetOnChanged`, the structure commands), and Commit is left with the
only thing it does on its own — landing the filter / sort / structure buffer that was actually
edited. With nothing edited there is nothing to land, so it lands nothing and says nothing.

⚠ **The asymmetry is deliberate.** `ibFilterEditor` / `ibSortEditor` carry `SetOnChanged` because
they are the shared pair, but only the COMPOSER panel wires it; the dynamic list's world is
untouched. It can afford to be: `ibListSettingsPanel::Commit` is reached only from a modal **OK**, so
running it at all already means somebody accepted something. The composer's panel has no such luxury
— its host is a designer TAB, where closing is accepting. The two worlds get the same editors, not
the same policy about them ([list-settings.md § 7](list-settings.md)).

#### ⭐ Guard the HANDLER, not the road

There are four ways into *add a resource* — the toolbar, the context menu, a double-click on the
field tree, and a drop on the pane — and view-only guarded two of them. The field-tree double-click
and the drop were open, so a read-only tab added resources by mouse. The same shape sat in the two
shared editors (`ibFilterEditor::AddFilterForField`, `ibSortEditor::AddForField`), where the same
four roads meet.

All four roads end in one function, and that is where `m_readOnly` is now asked —
`ibComposerSettingsPanel::OnAddResource` and its two twins. Guarding roads is precisely how the
fourth one gets forgotten; guarding where they meet has no list to keep in step. (The field tree
itself stays browsable: reading what a filter or a resource COULD name is reading.)

🛑 And the road nobody thought of as a road: **switching a VARIANT writes.** `ActivateVariant`
commits the buffer, captures it into the variant being left and makes another snapshot the
composer's settings — the grid stays selectable in a read-only tab, so a look-only session could
rewrite the composition by clicking a row. It carries the guard too.

### The DEFAULT composer is written down — by guid, like the default form (2026-08-20)

`ibValueMetaObjectReport::ReadData` / `WriteData` (`partial/dataReportMetadata.cpp`) write it beside
`DefaultFormObject` and **by guid, not by metaID**: an id is only unique inside the container that
stamped it, and an external report is carried between configurations — the same reason the form
beside it travels that way.

It had everything else and nothing here: a property (`DefaultComposer`), a choice in the tree's
header, and a first-one-wins rule (`OnCreateComposerObject`) — so it was set, used, and saved
nowhere, and came back `<not selected>` on the next open. That is not a cosmetic loss. The generated
form lays out its gridbox only when the report HAS a default composer (`ibValueForm::BuildForm`:
`if (defaultComposer != wxNOT_FOUND)`), so a report whose default came back empty showed **no table
at all** — in the designer and at runtime.

### A composition is a FIELD of the report object (2026-08-20)

The live composition is **created where every other field of an object is created** — in
`PrepareEmptyObject`, into the one store `m_listObjectValue`, keyed by the composer's metaID, beside
the attributes and the tabular sections (Max: *"the data composer lives IN the object, as a runtime
value; it is created, initialised, the whole cycle, like a tabular section"*).

```cpp
void ibValueRecordDataObjectReport::PrepareEmptyObject()
{
    ibValueRecordDataObjectExt::PrepareEmptyObject();      // attributes + tabular sections
    for (auto* metaComposer : metaObject->GetComposerArrayObject())
        m_listObjectValue.insert_or_assign(metaComposer->GetMetaID(), composition);
}
```

Everything else then follows by construction and cannot drift: `GetValueByMetaID` finds it where it
finds every field (**no override**), the member surface publishes it, and `InitializeObject` rebuilds
it with the rest. `GetComposition(id)` is the one-line twin of `GetTableByMetaID` — read the field,
say what it is.

🛑 **What this replaced:** a second store beside that one — a `std::map<ibMetaID, composition>` filled
lazily by an `EnsureCompositions()` of its own, with a `GetValueByMetaID` override to reach into it.
It worked and was still wrong: **a parallel store is a parallel lifecycle** — nobody cleared it, and
it survived a re-read that resets every other field of the object.

### External reports have composers too

`ibValueMetaObjectExternalReport` derives the very same metaobject, so it always HELD its composers
and answered for them — its designer tree simply never showed the group, and what a tree does not
show cannot be added to. The group is now in `s_reportGroups` (`treeDataReport_impl.cpp`), **last**,
exactly where the configuration tree puts it (`AddReportItem` appends composers after everything a
data processor has). One report, one order, wherever it is opened from.

**And the header pane asks BOTH questions.** "Default composer:" sits beside "Default form:"
(`treeDataReport.{h,cpp}`), filled from the same walk that fills the form choice
(`UpdateChoiceSelection`) and writing back through `OnChoiceDefComposer`. A report declares two
things about itself — the form it opens with and the composer it composes by — so both are answered
in the one place a person is already looking.

⚠ **Creation was the road that did not refresh them.** Removal and rename already called
`UpdateChoiceSelection`; creation did not, and the first composer becomes the default BY ITSELF, so
the report already had a main composer while the field above still read `<not selected>`. Adding a
form or a composer changes what "default" may point at, so every road that creates one has to say so.

⭐ **And it is said from the METATYPE, not from the tree's create command.** The first fix hung the
refresh on the report tree's `CreateItem` — one road, and the second tree, paste and undo are three
more. The form metatype had answered this correctly all along (`ibValueMetaObjectForm::OnCreateMetaObject`
calls `UpdateChoiceSelection` right after `OnCreateFormObject`), so
`ibValueMetaObjectComposer::OnCreateMetaObject` now does the same and every other road is covered by
construction rather than by three more copies of the call. The metatype knows it was born; everything
that creates one goes through it ([metadata-tree.md § 7](metadata-tree.md)).

### The form is not made — it is generated from the composer

A report that declares a composer needs no form at all: `BuildForm` sees a report object and lays out
a **gridbox bound to the DEFAULT composer** (`{mainAttr, DefaultComposer}`). Everything else follows
from shapes that already existed:

- the box reports `IsMainSourceBound` — a single hop, or a second hop that names the source's own
  MAIN node (`ibSourceDataObject::IsMainSourceNode`, which for a report is its default composer);
- so the form's command provider resolves to it (`FindMainCommandView`), its verbs appear on the
  FORM's toolbar, and the box carries no bar of its own (`HasCommandBar` → false) — one button in one
  place, exactly as a tablebox behaves on the main attribute;
- the form then adds a **separator** and its own chrome (Close / Update / Help / Change form): what
  the provider gave is about the DATA, what follows is about the WINDOW.

A second grid over another composer (`Report.Composer2`, added by hand) is a distinct view and keeps
its own bar — it is not the form's subject.

🛑 **The object's own Compose command was removed.** With a composer declared it was the SAME button
twice (the model already names `Compose`), and two buttons doing one thing is how they start meaning
slightly different things. The script seam is untouched: `Composing()` / `DoStandardCompose()` are
still the report's twin of a document's `Posting` — what went is a second doorway to them.

---

## 5. Honest remainder

- ~~**No platform-level report action.**~~ **Landed 2026-08-02.** `dataReportAction.cpp`
  now carries one standard command, **Compose** (`g_picGenerateCLSID`, `SetModify(false)`
  so it stays live on a view-only form). `CallAsAction` routes it to
  `ibValueRecordDataObjectReport::Composing()`, which calls the object module's
  `Composing(StandartProcessing)` handler — the report's twin of a document's
  `Post` → `Posting`.

  **Compose, not "generate".** In this tree *Generation* already means entering one object
  ON THE BASIS of another (`ibValueRecordDataObjectRef::Generate`, the document's Generate
  command). A report generates nothing; it COMPOSES a result — the word the rest of the
  stack already uses ([data-composer.md](data-composer.md), `ibQueryComposer`).

  The handler is a **declared default procedure** — `ibValueMetaObjectReport`'s ctor calls
  `SetDefaultProcedure("Composing", eProcedureHelper, { "StandartProcessing" })`, the same
  way a document declares `Posting` — so the designer offers it in the object module's
  handler list rather than the developer having to know the name.

  **The argument is `StandartProcessing`, not a cancel flag**, following the
  `Filling` / `SetNewCode` / `SetNewNumber` contract:

  | Flag after the handler | Who composes |
  |---|---|
  | left **TRUE** (including: no handler written) | the PLATFORM — `DoStandardCompose()` |
  | set **FALSE** | the SCRIPT already did; the platform stands down |

  ⚠ **Updated 2026-08-20 — the schema this waited for arrived, and it is the COMPOSER (§4f).**
  What a report reads and how it is laid out is now declared: the metatype exists and one of
  them is the report's default. What `DoStandardCompose` still lacks is the OTHER half —
  a document to compose INTO. A composition composes into a spreadsheet document, and the
  document belongs to the control that shows it: the gridbox owns it, rents the background
  run and publishes the finished sheet in one step. The object has no window and no document,
  so composing there would produce a sheet nobody holds — it answers `false` and that is the
  honest answer, not a stub.

  So the RUNNING path is the model's `Compose`, raised from the gridbox's command band; the
  object's own Compose command was removed as a second doorway to the same act. **A report
  with no composer still composes in its handler** — data through the query / composer layer,
  presentation through the spreadsheet's areas (§1) — exactly as before.
- **Export is two branches, one document.** Geometry (printout → PDF) and semantics
  (walk → Excel / Word) are separate consumers of the same `ibBackendSpreadsheetObject`;
  Excel is deliberately *not* produced through the printout path. Cell typing is the
  known risk in the semantic branch.
- `IsCellReadOnly(int row, int col, bool isReadOnly = true)` takes an `isReadOnly`
  argument it never uses — a getter carrying a setter's signature. Harmless, and a
  cleanup candidate.

---

## 6. The composition settings window, rebuilt around NODES (2026-08-21)

The window edits a SNAPSHOT of the composition's outputs and applies it whole on accept. The flat
buffer it used before (filter / sort / a flat grouping ladder) cannot describe a level made of
several fields, a second output, or an axis of columns — and papering over that with a "same level
as the one above" flag would have carried the lie into every saved variant.

```
Report                       ← available fields · filter · sort that reach EVERY output
└── Output / Table           ← empty outputs are not shown at all
    ├── Grouping             ← its elements, side by side; a level with none = detail records
    └── Grouping …
```

- **"Add grouping" on the report starts a NEW OUTPUT**; on a grouping it nests one under it. That is
  the whole gesture: levels added one under another belong to one output, going back to the root
  begins the next.
- **Deleting a grouping breaks the chain** — it and everything under it go, together with their node
  buffers. Pulling the deeper levels up would silently re-parent them into a different report.
- **The node's own pages**: *Grouping* (its elements, each with its own unfold — shown only when a
  grouping is selected, since nothing else has one), *Available fields* (its own set or "auto" from
  above, with add / delete / copy / move), *Filter*, *Sort*. The filter and sort editors are the
  SHARED ones, re-pointed at the selected node's buffer.
- **By mouse**: double-click or DRAG a field from the tree into the list (the same drop target the
  filter and sort use). Moving groupings by dragging is still to come; the toolbar's arrows are the
  standing road.

### What the printer does with a level of several fields

They are WELDED: written side by side on one row, not one under another — one heading, several
columns. The dimension area is as wide as the widest level; the indent rides on the level's first
field; the header carries one line per LEVEL. Before this the printer counted dimension columns as
if each were a level, so the second field of one level took the next level's place and the last
level fell off the page ("the date disappears").

### A report with a composer needs no form

`StartMainModule` builds the generated form (a gridbox over the default composer) when the report
has NO default form but HAS a default composer. Before, the module started and quietly died. With
neither, the refusal stands: an empty window is a worse answer than none.

### What a variant now carries

Its structure, beside its settings: outputs, their levels, the fields inside them with their
unfolds, and the per-node field sets with their "auto" flags. Captured when the variant is left,
applied when it is entered (AFTER the flat settings, which rebuild the ladder from a list that
cannot hold a multi-field level), and written to the file. A file written before this simply has no
structure section — absence reads as absence.

⏳ Not serialised yet: a level's own filter and sort. They live as an expression and need a format
of their own.

### 6a. Several outputs on one sheet (2026-08-21)

The composition hands every output to the SAME driver and runs once; each prints as a SECTION below
the previous one. Three things that had to be said out loud for that to read as a report:

- the document is cleared for the FIRST section only (clearing per section made the second output
  erase the first);
- the title and its parameter lines are the REPORT's, written once above everything;
- sections are separated by a clear gap — two blank, untinted lines, because one reads as a row that
  failed to print;
- the header freeze belongs to the PAGE, not to a section. Freezing again on the next output pinned
  everything printed so far and the sheet stopped scrolling ("with two reports the scroll does
  nothing").

The grand-total row prints only when the output declares MEASURES: at depth 0 there is no dimension
value, so without figures it is an empty tinted stripe above the first heading — which reads as a
drawing fault rather than as a total.

### 6b. Detail records — the rows under the headings (2026-08-21)

*"A detail record is an empty grouping"* (Max) — the rows are a LEVEL of the ladder, the last one, and
the settings tree writes them as a node with no fields (`ibCompositionLevelKind::Details`).

**How one is added.** "Add grouping" opens a FORM — the level's field list, with add / delete / move
and the per-field unfold — and **OK with an empty list makes it the detail level**. One verb makes a
level; what the level IS gets decided inside the form. (There is no second command for the empty
case: two verbs for one node is how two ways of making it start to disagree.)

**What the printer gets.** The walk asks the NODE (`ibSelector::Kind()`) and calls `OnDetail` rather
than `OnGroup`, so the spreadsheet driver lays a row out as a row: faint fill, no bold, no indent of
its own. It needed no change — a detail row was already its other case.

**What the reading costs.** A report that prints every row holds every row; that is what printing
them means, and it is why this is opt-in per output rather than a mode. Two things follow when an
output asks for them (`ibDataComposer::WantsDetails`):

- the server-side fold is REFUSED — `GROUP BY ROLLUP` returns aggregated rows, so there would be
  nothing left to hang under the last heading;
- the SELECTed fields join the totals schema as `Detail` columns. Without that a detail row could
  print only the resources: a `TOTALS` read projects the levels and the measures and nothing else.

Each detail node carries the row's own values, and its resources are rolled over that ONE row
through the same `ApplyAggregates` every heading uses — so a figure on a line and the figure it adds
up into cannot be computed two different ways.

**A level's own filter and sort are saved now.** The filter is kept as the TREE it was written as
(that is what the editor reopens on) and the expression the engine reads is DERIVED from it —
rebuilt when a file is loaded or a variant switched (`RebuildLevelFilters`). An expression can be
run but not taken apart back into the lines a person wrote, which is why the tree is what travels.

⏳ The query TEXT still cannot ask for detail records — there is no keyword, so the constructor has
nothing to offer either. Deliberately open: a new global word takes an identifier away from every
configuration, and the naming is Max's. Today the request rides as an argument of the READ
(`ExecuteTotals(…, withDetails)`), which is where "how much of the tree do you want to see" belongs.

### 6bb. Where the totals are printed (2026-08-21, settled 2026-08-22)

Every level's figures were computed all along — the fold rolls `Sum / Count / Min / Max / Avg` at
every node, so each subgroup has its own. The question was only WHERE to write them, and the answer
Max gave after seeing both shapes on screen is:

**A GROUP IS ONE ROW — its name and its figures. Only the GRAND total stands on its own.**

```
Warehouse A                380.00   ← the heading IS the group's total
    Apple                  120.00   ← its subgroup, same rule
        …rows, if the output asks for detail records…
    Pear                   260.00
Grand total              1 240.00   ← the whole output, at the bottom of its section
```

**The shape that was tried and rejected** put a `Total …` line under every group. It is the
reference platform's layout and it reads badly here, because our heading already carries the
group's resources beside its name: the line below repeats, word for word, the line above it (Max,
2026-08-22 — *"that is nonsense, you already have the resource there… by every grouping it is not
readable"*). The per-group machinery (`m_openGroups` / `CloseGroupsFrom`) is gone; `WriteTotalLine`
stayed, with exactly one caller.

**Where the grand total comes from — nothing computes it twice.** It is the ROOT of the folded
tree, and the fold has always rolled the whole result into it (`ApplyAggregates(tree.Root(), …)` —
"grand total in-place"). What decides whether anyone SEES it is the walk, not the arithmetic:
`ibSelector::EnsureWalk` puts the root in the visit list only when the totals are `OVERALL`.

So `OVERALL` is a WALK setting, and the reader may ask for it in its own words:

| who asks | how |
|---|---|
| the query text | `TOTALS SUM(x) BY OVERALL, Warehouse` — the level above every dimension |
| the READER | `ibSelector::WalkOverall()` — the same flag, set by whoever walks |
| the REPORT | `ibCompositionDriver::WantsGrandTotal()` → true on `ibSpreadsheetComposeDriver` |

The composer asks the driver (`RunOutput`) and sets the flag; a list's fetch answers `false`, so a
grouped list does not grow a stray top-level row. When the settings eventually get an explicit
"grand totals" switch of their own, that is the seam it lands on.

**And it is written LAST though it arrives FIRST.** The walk is pre-order and the root has nothing
above it, so `OnRow(0, hasChildren=true, …)` is the first call the driver receives. It is held
(`m_grandTotal`) and written by `OnComplete`, at the bottom of the output's own section. Its caption
goes inside the DIMENSION area (`m_dimWidth`); an output with no dimensions at all has no such area,
so that line is figures only — column 0 there is a number, and the word "Grand total" would replace
it. Pinned: `SpreadsheetCompose.GrandTotal_ArrivesFirstAndIsPrintedLast`,
`…GrandTotalWithNoDimensions_WritesFiguresOnly`, `…NoPerGroupTotalLine_TheHeadingCarriesTheFigures`.

⚠ **A server-folded tree had to be taught to say it has children.** `GROUP BY ROLLUP` builds its
nodes by finding each parent through a key map — nothing ever descends — so `m_hasChildren` stayed
false on every node and the printer drew headings as ordinary rows. `MarkRollupFolders` states it
once the shape is complete (`RunRollupTotals`). The RAM fold sets it as it descends and always did.

### 6c. What the live run changed (2026-08-21)

Five things came out of Max running the window, and all five are the same class: **the mechanism was
there, the door was wrong.**

- **The "…" on a level's Field cell opens the GROUPING FORM**, not the single-field picker. A level
  groups by a LIST of fields (the cell plainly shows `Ref, DataVersion`), and a picker could only
  ever edit the head one. The shared row-value cell grew one hook for this — `SetExpand`: set it and
  the button opens the ROW's own window. Same button, same meaning ("open what edits this cell"),
  which is why it is one button and not two. The form is the same one *Add grouping* opens, so a
  grouping is made and changed in one place.
- **The available-fields list is edited by the picker too.** Drawn as plain text, a wrong line could
  only be re-made — delete, add — which is a verb this window offers nowhere else.
- **A parameter's expression compiles against the OBJECT'S OWN config**, through the attach chain
  (`GetMetaData`), not against the query's SOURCE config and not against whatever configuration is
  open globally. The expression is SCRIPT: the names it may call are the ones its configuration
  declares.
- **…and its parent is the MODULE MANAGER, not the configuration's root module.** `CurrentDate()` is
  not a function of any module — it is a METHOD of a scope-context value (`SystemManager`, bound
  through `BindScopeVariable`), and those live on the module manager. Parented to the configuration's
  own module, the check compiled against a world with no built-ins at all and reported "procedure or
  function not detected (currentdate)" about a function every module can call.
  `ibSession::EditModuleManagerFor(metaData)` is the one seam, with both roads already inside it
  (the Designer's lightweight manager from the compile cache, the session's root at run time) — so
  this window neither branches on the mode nor names either of them.
- **ASCII only in UI literals** in this file — it has no BOM, so MSVC reads it as ANSI and an em
  dash reaches the screen as mojibake. The rule was already written at the top; this is what
  forgetting it looks like.
