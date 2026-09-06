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

## What this does NOT do yet

Named rather than implied, because each is a road not started.

- **Sorting.** The header is inert (`headerSort:false`). Sorting a list is an
  ORDER BY over the whole table, committed to the composer — not a reshuffle of
  the page in hand. The desktop path (`ibValueModelTableBox::OnColumnClick` →
  composer → `RefetchAll`) is desktop-only today.
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
- **Footers.** `footer` is reported and ignored.
