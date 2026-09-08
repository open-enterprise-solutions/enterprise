# TableBox on the web

*Iteration 2. Landed 2026-09-05.*

Before this, `tablebox`, `tableboxcolumn` and `tableboxcolumngroup` all returned
`ibWebStubControl` from `Create()`. A stub carries a type string and the four
base fields, so a column reached the browser as

```json
{"type":"tableboxcolumn","id":1000032,"label":"","enabled":true,"shown":true}
```

— no caption, no width, no alignment, no value type — and the table carried no
rows at all. It was not a missing renderer: there was nothing for a renderer to
draw. Every list form in the demo base is a tablebox and nothing else, so the
three controls Iteration 1 built could not be seen on a real form either.

## The table's own command bar

A tablebox bound to a **tabular section** carries the strip the desktop puts
above it — Add / Copy / Edit / Delete, the two row moves, the two sorts — and a
tablebox bound to the form's MAIN source carries none, because the form's own
toolbar already serves those commands. That rule is the platform's, not ours:
`ibValueModelTableBox::HasCommandBar` decides it, and the web reads the same
answer the desktop does.

Three pieces, and the middle one is the reason it took a while.

**Beside the children, not among them.** `ibWebWindow` owns an optional
`ibWebToolbar` and emits it as `commandbar`. It cannot be a child: a table's
children are its COLUMNS, and the browser draws those as a header, so a toolbar
among them would be read as one more column.

**The owner is what tells two bars apart.** Action ids are unique within a bar
and not across bars — the table's *Add* and the form's *Add* are both `1`. A
tool therefore carries `owner`, the control id of the bar it belongs to, and
zero means the form's own; `POST /command/<action>` takes it as a form field and
`ibWebApplication::DispatchCommand` resolves against that control's bar instead
of the form's. Without it, clicking Add on a table added a document.

**One builder for both.** `ibWebBuildCommandBar` (`web/webCommandBar.{h,cpp}`)
turns a command store into the toolbar node; the visual host calls it for the
form's bar with owner 0, and `ibValueWindowComposite::UpdateWithLayers` calls it
for a control's with its own id. The composite's web branch is now the twin of
the desktop's layer refresh, including the suppression: `HasCommandBar()` false
means the field is simply absent.

**And the walker had to go through the wrappers.** The web walker called
`Create` / `Update` directly where the desktop calls `CreateWithLayers` /
`UpdateWithLayers`. The base forwards, so nothing without chrome noticed — but a
composite builds its bar in the wrapper, which is why the bar existed on the
value and reached nobody. That single substitution is what made the rest work.

## The height floor is on the HOST, not on the grid

`.oes-tablebox` carries `min-height: 240px`; `.oes-tablebox-grid` carries none.
The floor exists because a grid in a container of indefinite height computes to
nothing and draws no rows — but on the grid it was a size the flex parent never
heard about. A form with 86px of room left still laid out 240px of grid and drew
the difference over everything below it, outside the form's own border. On the
host the same floor is a flex minimum: the form asks for its natural height, and
`.form-host` scrolls when there is nowhere left to put it.

The stored minimum meets it through `max()`. A tablebox's `MinimumSize` is
`150x75` — set in `ibValueModelTableBox`'s constructor, chosen by nobody — and
written as a plain inline value it *replaced* the floor, leaving a header with no
rows under it. `applyCommon` writes `max(<stored>, var(--oes-min-h, 0px))`, so a
stored minimum raises a floor and never lowers one.

## Two roads

**The shape** travels with the form JSON. `ibWebTableBox`,
`ibWebTableBoxColumn` and `ibWebTableBoxColumnGroup`
(`src/engine/frontend/web/webTableBox.{h,cpp}`) hold what the browser needs
before it has a single row, pushed into them by the controls' own
`Update` / `OnUpdated` — the same way a button pushes its caption. Nothing
holds a back-pointer to a control.

```json
{"type":"tablebox","id":3,"header":true,"footer":false,
 "viewMode":"hierarchical","choiceMode":false,"pageSize":50,
 "children":[
   {"type":"tableboxcolumn","id":8,"caption":"Code","field":"c8",
    "width":80,"align":"left","headerAlign":"left","valueType":"string",
    "visible":true,"resizable":true,"readOnly":true}]}
```

`valueType` is read from the binding's type description — `number`, `date`,
`boolean`, `string`, `reference`, `enum` — and decides the cell alignment the
same way the desktop renderer decides it per cell (`CheckedGetValue` sets right
for a number, left for everything else). `visible` folds in the desktop's two
reasons for hiding a column: the property, and a column with no source bound.

**The rows** travel on their own, one page per request:

```
GET /fetch/<controlId>?dir=first|next|prev&count=N
```

```json
{"ok":true,"control":3,"reset":true,"count":2,"hasMore":true,
 "rows":[{"key":0,"container":false,"cells":{"c8":"00000001","c9":"Кофе…"}}]}
```

A list is paged in the engine by architecture (`ibValueModel::GetFirstFetch` /
`GetNextFetch` / `GetPrevFetch`), and folding a page into the form tree would
have thrown that away — a catalogue of fifty thousand rows would be read to draw
twenty lines of it.

## Row keys

The browser cannot hold an `ibDataViewItem`: it is a refcounted handle on a live
row. So the server keeps a **window** of the rows it has handed out and mints a
key per row. A key stays valid for the life of that window, which is what makes
scroll-driven paging possible at all — a page fetched later does not renumber
what is already on screen. `dir=first` throws the window away and starts again
(`"reset":true` says so); nothing else renumbers it.

The window stops growing at 5000 rows. Past that `next` answers
`{"ok":true,"hasMore":false,"reason":"window limit"}` — every row in the window
pins a live row of the model, and a page showing five thousand rows has already
stopped being a page.

Asking to continue from a window whose rows the model has since dropped answers
`{"ok":false,"reason":"stale window"}` rather than silently returning the top of
the list, which the client would append as if it were the next page.

## The cursor

`POST /fire/<controlId>/row?value=<key>` moves the **server's** current line —
the row a command runs against — through `ibValueModelTableBox::ApplyCurrentLine`.
Selection in the grid is only how that is shown. An unknown key is refused
(`{}`), not guessed at.

`/fire/<id>/<kind>` gained an optional `value` parameter for this; a kind that
carries no payload, like a button's `click`, sees an empty string as before.

**And the grid is told which row that is.** Every page carries `currentKey` when
the current line falls inside the window it is answering with, and the client
selects that row rather than remembering which `<tr>` was last clicked. So the
mark survives anything that rebuilds the grid — a sort, a command, a re-render —
and it means *the row the server is standing on*, which is the only thing a
person can act on. Absent from the payload when the control has no current line,
or when the line is outside the window in hand.

A click still marks the row before the round trip finishes: the server decides,
but a click that shows nothing until the answer comes back reads as a click that
did not land.

The mark itself is the theme's selection fill **plus** an accent bar down the
leading edge (`--oes-table-row-selected-accent`, UI5's
`sapList_SelectionBorderColor`). The fill alone is one shade off the row beside
it — `#ebf8ff` against `#ffffff` in light — and was reported as no selection at
all. Hover over the selected row keeps the selection colour: moving the pointer
must not look like losing the row.

## The grid

Tabulator 6.5.2 (ADR-003), vendored by `webClient/assets/tools/vendor-tabulator.py`
into `webClient/assets/tabulator/6.5.2/` — two files, a manifest of their SHA-256
digests, and the licence. Re-running the script reproduces the same digests. It
is served from our own origin under an immutable cache header, and it is imported
**lazily**: 700 KB of grid arrives only when a form actually has a table on it.

Colours come from `--oes-table-*` tokens in `webClient/assets/oes/oes-tokens.css`,
which map onto UI5's list theming parameters — so both themes and both densities
follow with no second rule. Measured on the Goods list: text on rows 16.86:1 in
light, 14.64:1 in dark.

The renderer sits in the **shared** map, not the UI5-only one: a grid is not a
UI5 component, and leaving `ui=legacy` with a dashed `[tablebox]` box would have
made the side-by-side comparison useless.

## The bar above it, and opening a row

Two things a list is useless without, both landed with the grid.

**The form's command bar.** Desktop builds it in `CreateFormLayers` through
`BuildCommandBarToolBar`, which is a `wxAuiToolBar` and has no web twin. What
both roads share is the model — `ibValueCommandBar::BuildCommands()` returns the
entries, `ExecuteCommand` runs one — so the web host reads the same list and
emits the toolbar nodes the browser already knew how to draw. The nodes hold no
back-pointer: a tool names its command by action id, and `POST /command/<id>`
asks the form for its bar. The bar is chrome, not a control, so it has no entry
in the (frame → wxObject) map and the control dispatcher could never reach it.

A table also contributes to that bar, and on web it contributed nothing: the
`GetStandardCommands` / `CallAsAction` pair lives in `tableBoxAction.cpp`, which
the web build does not compile — it reaches into the wxDataView control for the
drill anchor and the current column, and half its verbs open a dialog. The web
pair answers the part with no window in it: the model's own command set, which
is the source descriptor's, and which is what a list needs — Add, Add folder,
Copy, Edit, Delete, Mark as delete. The table's view-state band (Filter,
ViewMode, saved settings, Output list) is absent rather than present and dead.

**Double-click opens the row.** `POST /fire/<id>/activate?value=<key>` moves the
cursor onto the row and then asks the model to raise its form
(`ibValueModel::ActivateItem` → the source descriptor's `ShowValueByKey`). The
desktop road, `ActivateRow`, decides between choice, an inline editor and
opening the value; two of those three have no web flow yet.

## The font every control was drawn in

Worth recording because it looked like a table problem and was not.
`ibValueWindow::UpdateWindow` pushes the control's font, and the stored family
is `Segoe UI` — a Windows-era name for "the UI font". Named alone in
`style.fontFamily`, it matches nothing on a Mac or a Linux box and the browser
falls back to its default, which is a **serif**. Every label, every cell and
every header came out in Times. `applyCommon` now puts `--oes-font-ui` behind
it, which resolves to the UI5 theme's family and, with UI5 not loaded, to the
system stack.

## Sorting

`POST /fire/<id>/sort?value=<column control id>`. The shape is the desktop's
`OnColumnClick` verbatim, minus the header arrow it sets on the widget: read the
column's own bound field (`GetSourceFieldName`), toggle it against what the
composer already says, `ClearSorts()` + `Sort(field, ascending)`, then
`RefetchAll()`. Tabulator's own sort is never used — it would reorder the page
in hand and call that a sorted list.

Two consequences worth stating.

The paged keyset anchor was built for the old ORDER BY, so the window is thrown
away rather than continued: the client re-renders and asks for `first`. And the
arrow has to be pushed onto the column nodes by hand — every other property
reaches a node through its control's `Update`, and the tree is serialised from
the nodes without running that again, but a sort touches no control at all. That
is what `ibWebTableBox::SyncSortOrders` is for; without it the rows came back in
the new order under an arrow still pointing the old way. The two trees pair by
the key both sides derive from the same control id, so neither holds the other.

A column reports `sortable:false` when the model has no `Sorting` feature or the
column has no resolvable bound field (a whole-attribute or foreign column) —
the header then does nothing rather than inviting a click that would.

The first click on a column the form already ordered by does not appear to
change anything: the author's order is not the reader's, and clicking adopts it
as theirs. The second flips it. That is the desktop's behaviour, from the same
lines.

## What this does NOT do yet

Named rather than implied, because each is a road not started.

- **Tree drill.** `container` is reported per row, but expanding one does not
  fetch its children. The fetch always asks the top level (or, in `list` view
  mode, every row in one order through `s_constIgnoreParent`).
- **Column groups other than horizontal.** Tabulator groups columns under one
  header caption, which is exactly OES's *horizontal* group. A *vertical* or
  *in-cell* group is a row BAND — the row grows taller instead of wider — which
  Tabulator has no notion of; those flatten, and their columns stay side by side.
  See `docs/column-groups.md` for what the desktop does.
- **Editing.** Every column reports `readOnly:true`. Iteration 2 is read-only
  throughout.
- **Choice mode.** A table opened as a picker has no web flow, so activating a
  row there falls through to opening the object rather than returning it.
- **Footers.** `footer` is reported and ignored.
