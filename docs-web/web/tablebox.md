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

The mark itself is a selection fill **plus** an accent bar down the leading edge
(`--oes-table-row-selected-accent`). The fill alone is one shade off the row
beside it — `#e5f2fb` against `#ffffff` — and was reported as no selection at
all. Hover over the selected row keeps the selection colour: moving the pointer
must not look like losing the row.

## The grid

Tabulator 6.5.2 (ADR-003), vendored by `webClient/assets/tools/vendor-tabulator.py`
into `webClient/assets/tabulator/6.5.2/` — two files, a manifest of their SHA-256
digests, and the licence. Re-running the script reproduces the same digests. It
is served from our own origin under an immutable cache header, and it is imported
**lazily**: 700 KB of grid arrives only when a form actually has a table on it.

Colours come from `--oes-table-*` tokens in `webClient/assets/oes/oes-tokens.css`.
Tabulator's own palette is a grey slab with a strong blue selection, written for
a page that brings nothing with it; the tokens are what make the grid read as
part of the window. The overrides stand one class deeper than Tabulator's own
rules on purpose — its stylesheet is appended to the head when the grid loads,
so it comes after the client's and wins every tie.

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

**Double-click opens the row — or picks it.** `POST /fire/<id>/activate?value=<key>`
moves the cursor onto the row and then takes the same branch the desktop's
`ActivateRow` takes: a table in **choice mode** hands the row back to whoever
opened the list (below), and any other table asks the model to raise the row's
form (`ibValueModel::ActivateItem` → the source descriptor's `ShowValueByKey`).
The third desktop answer, an inline editor, has no web flow yet.

## Picking a value

A reference field's `…` button opens a list, and until 2026-09-07 that was all
it did: the list opened and there was no way to take anything out of it. The
machinery was whole on both sides of the gap. `ibTypeControlFactory::ChooseValue`
→ `ProcessChoice` already opens the list with the asking control as its owner and
`choiceMode` set — the JSON has carried `"choiceMode":true` all along — and
`ibValueForm::NotifyChoice` already writes the picked value into that control and
closes the picker. What was missing was the verb in between.

Two things were missing, and both were in the web build's own copy of the
tablebox's command layer (`tableBox.cpp` under `OES_USE_WEB`, because
`tableBoxAction.cpp` reaches into the wxDataView control and is not compiled
here):

- **The Select command was not composed.** The desktop band puts Select first
  when `IsChoiceMode()`; the web band listed only the model's object commands, so
  the picker's toolbar offered Add / Copy / Edit / Delete and no way to choose.
- **Activating a row did not check for it.** The web activate path went straight
  to `ActivateItem`, so double-clicking a row in a picker opened that row's own
  form — the list looking at itself.

Both bands now name the same id: the action ids moved from a file-scope enum in
`tableBoxAction.cpp` onto the class (`tableBox.h`), because a tool built in one
front and a tool built in the other have to mean the same command.
`Command_Choose` moved to the shared part of `tableBox.cpp` for the same reason —
it is the one command in that band with no window in it (the current line, its
select value, the form it goes to), so both fronts run one body.

The two gestures are one road: the double-click goes through
`CallAsAction(enTableSelect, …)`, which is exactly where a click on the Select
tool arrives.

Measured on the Goods receipt form: `Warehouse` empty → picker opens with
`["Select","Add",…]` → double-click "Основной склад" → picker closes, field
reads `Основной склад`. And through the tool: Select with no row standing does
nothing and leaves the picker open (there is no current line to hand back);
click a row, then Select, and `Counterparty` reads `ООО Ромашка`. A list that is
not a picker is unchanged — no Select on its bar, and a double-click still opens
the object.

## Editing a cell

A column says whether it carries an editor, and the answer is composed of three
that are all the desktop's:

- the **model** — `EditableColumn(col)`, the half of `EditableLine` that needs no
  row. A dynamic list says no to every column (a list is opened or picked, not
  typed into); a tabular section says no to its line number and yes to the rest;
  a register record set says no. It was split out of `EditableLine` because the
  browser has to decide whether a column carries an editor *before* it has
  fetched a single row — the desktop asks per cell, having the row in hand, and
  `EditableLine` now composes the two halves so a model overrides one, not both;
- the **table** — a dot-path or foreign-rooted column reads through something
  that is not this row, and is read-only on the desktop for the same reason;
- the **column** — its own `TextEdit` property.

The result rides on the column node as `readOnly`, and the browser puts a plain
text editor on the ones that are false. One editor for every type: the server
coerces the typed string through the type the cell already holds — a number, a
date, a reference found by its name — which is the road
`ibValueModelTableBoxColumn::TextProcessing` takes on the desktop. One parser,
on the side that owns the types.

**A commit is two posts, in order and awaited.** `POST /fire/<table>/row` moves
the cursor to the edited row, then `POST /fire/<column>/cell?value=<text>` tells
the column what was typed. The column writes through `SetControlValue` — the
current line, the source-object update, `RefreshForm`, then its `OnChange` — so
nothing about the write is web-specific. The cursor move is a precondition and
not a convenience: the column writes to the line the table is standing on, which
is the desktop's rule too (a cell cannot be edited without the cursor on its
row), and if the move fails nothing is sent.

The answer is the whole form again, because a committed cell can move anything —
a price recalculating an amount, a script on the column — and the row that comes
back is the row the server holds rather than the text that was typed at it.

**A refused edit puts the cell back.** The server answers a form tree when it
took the value and nothing when it did not (a name no goods answer to, a number
that will not parse). The browser restores the cell's previous text on the empty
answer; left alone it would go on showing something the row does not hold, which
is worse than the refusal. The desktop's editor restores the old text in exactly
this case.

**Double-click on an editable row opens nothing.** Its first click already
opened the cell's editor, and asking the server for the form again would replace
the grid — and the editor with it — while somebody is typing. Both sides take
that branch: the client does not send `activate` for an editable row, and the
web activate path checks `EditableRow` before raising anything, which is the
desktop's `if (!EditCurrentRow(item)) ActivateItem(...)`.

Measured on a fresh Goods receipt line: `Goods` by typing "Кофе в зёрнах, 1 кг",
`Quantity` 3, `Price` 480; a second line added and its quantity set to 9 — the
cursor follows, and each row keeps its own. Typing a name nothing answers to
leaves the cell reading "Кофе в зёрнах, 1 кг". Clicking the line-number column
opens no editor. A list is unchanged: no editors, and a double-click still opens
the object's form.

### The buttons inside a cell

A cell's editor carries the same three a form field's does — Select (`…`), Open
(`▷`), Clear (`×`) — read from the same three column properties the desktop
renderer reads onto its inline editor (`GetSelectButton` / `GetOpenButton` /
`GetClearButton`).

Select and Open are narrowed to a **reference**, and the narrowing is about what
exists rather than about what is declared. Both properties default to true on
every column, and the desktop can honour that on a number: its Select opens the
quick-choice popup, its Open shows the value. Neither has a web road, and a
button that does nothing when pressed is worse than one that is not there — the
same reason the table's view-state band is absent here rather than present and
dead. Clear needs no road: an empty value of the type the cell holds is a value.

A button is the act, not the text beside it. The edit is cancelled first, so the
editor's own commit road and the button's cannot both reach the same line; then
the cursor moves to the row and the kind goes to the column —
`cellSelect` / `cellOpen` / `cellClear`, the same shape as `cell`.

`…` walks the one route a form field walks: `ChooseValue` → `ProcessChoice`
opens the list as a picker with the COLUMN as its owner, and the row comes back
through `ibValueModelTableBoxColumn::ChoiceProcessing` — which is a real body on
the web now, and was a no-op stub. It writes the value onto the current line and
fires `OnChange`; it does not call `SetControlValue`, because the choice
machinery refreshes the owner form itself (`ibValueForm::ChoiceDocForm`), which
is what the desktop relies on too.

Measured on a fresh Goods receipt line: the Goods cell's editor carries `…` and
`×`; `…` opens the Goods list as a third tab; picking a row — by double-click or
by the picker's own Select tool — closes it and the cell reads the goods. `×`
empties the cell and opens nothing. A number cell carries `×` alone. Typing a
name still works beside all of it.

## The font every control was drawn in

Worth recording because it looked like a table problem and was not.
`ibValueWindow::UpdateWindow` pushes the control's font, and the stored family
is `Segoe UI` — a Windows-era name for "the UI font". Named alone in
`style.fontFamily`, it matches nothing on a Mac or a Linux box and the browser
falls back to its default, which is a **serif**. Every label, every cell and
every header came out in Times. `applyCommon` now puts `--oes-font-ui` behind
it, which resolves to the system stack.

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
- **Footers.** `footer` is reported and ignored.
