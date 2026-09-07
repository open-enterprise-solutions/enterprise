# UI5 integration — control mapping

One section per OES control: which UI5 element renders it, how each JSON field
reaches a property, and which UI5 properties have no OES counterpart. The last
list matters: those are candidates for the core, and none is added without an ADR.

The renderers live in `webClient/client.html` as a second map merged over the
legacy one when `ui=ui5` (ADR-011). No `if (ui5)` branch exists inside a legacy
renderer.

The form JSON is unchanged by any of this (ADR-007), and no vendor property name
appears in it (ADR-006): the JSON says `semantic`, the renderer says `design`.

## Button → `ui5-button`

| JSON | UI5 | Note |
|---|---|---|
| `label` | text child | |
| `semantic` | `design` | `default`→`Default`, `primary`→`Emphasized`, `transparent`→`Transparent` |
| `enabled: false` | `disabled` | |
| `picture` + `representation` | `<img>` child | `representation` 1 = picture only, 2 = text only |
| click | `POST /action/<id>` | the existing endpoint, unchanged |

No OES counterpart: `icon` / `endIcon` (UI5's own icon slot — OES carries a
picture instead), `tooltip`, `accessibleName`, `submits`, `type`.

## CheckBox → `ui5-checkbox`

| JSON | UI5 | Note |
|---|---|---|
| `label` | `text` | must be the property; a text child renders nothing |
| `value` | `checked` | |
| `value: null` / `indeterminate` | `indeterminate` + `checked` | the third state |
| `readOnly` | `readonly` | |
| `enabled: false` | `disabled` | |
| change | `POST /toggle/<id>` | |

No OES counterpart: `valueState`, `wrappingType`, `required`, `name`.

## TextCtrl → `ui5-input` inside a fixed-width group

| JSON | UI5 | Note |
|---|---|---|
| `value` | `value` | |
| `passwordMode` | `type="Password"` | |
| `readOnly` | `readonly` | distinct from disabled, visually and behaviourally |
| `enabled: false` | `disabled` | |
| `error` | `valueState="Negative"` | the JSON says `error`, never `valueState` |
| `showSelectButton` | a `…` `ui5-button` | `design="Transparent"` |
| `showOpenButton` | a `▷` `ui5-button` | |
| `showClearButton` | a `×` `ui5-button` | |
| input / change | `POST /change/<id>` | |

**The side buttons do not change the outer width.** The group is a flex box of a
fixed width; the field takes `flex: 1 1 0` with `width: 0` so it shrinks and the
buttons take their space from it. A field with three buttons and a field with none
have the same right edge — visible in the harness, where the long-caption row sits
under four others and all five box edges line up.

No OES counterpart: `valueStateMessage`, `showSuggestions`, `maxlength`,
`placeholder`, `required`, `name`.

## The window's own chrome

Iteration 1 put UI5 on three form controls. On a catalog list — a command bar
and a table — that came to **one** `ui5-*` element on the page, and that one was
`ui5-announcement-area`, which UI5 injects for itself. The theme's colours were
there and nothing else was, which is exactly how it read: the same screen as
legacy. So the chrome went across too.

| Piece | Renders as | Why |
|---|---|---|
| Title bar | `ui5-shellbar` | Title, the "All functions" menu and the dev switcher move into its `content` slot — the nodes themselves, so what was wired to them stays wired |
| Command bar | `ui5-bar design="Subheader"` | Takes arbitrary slotted content, which `ui5-toolbar` does not |
| Command | `ui5-button design="Transparent"` | The metadata's raster icon goes in the button's own slot |
| Separator | `ui5-toolbar-separator` | Renders standalone; no `ui5-toolbar` needed around it |
| Sections | `ui5-side-navigation` + `ui5-side-navigation-item` | |
| Tab strip, output panel, status bar | ours, painted from `--oes-chrome-*` | see below |

**Why a `ui5-bar` and not a `ui5-toolbar`.** A `ui5-toolbar-button` takes its
icon by NAME out of UI5's own set, and a command here brings its picture with it
— a PNG out of the metadata, encoded by `ibBackendPicture::CreateBase64Image`.
`ui5-toolbar` accepts only `ToolbarItem` subclasses, so there is no slot to put
that picture in. `ui5-bar` slots anything, and a `ui5-button` renders an `<img>`
child, so the bar is a real UI5 surface and every command keeps its own icon.

**What the tab strip would have cost.** `ui5-tabcontainer` has no closable tab
and no slot that renders in the strip, so a tab there can carry neither the ×
nor the metatype icon that says what the tab holds. Both are things the user
acts on. The strip stays ours and takes the theme's colours instead — the
selected tab is `--sapSelectedColor` under a 3px rule, which is what UI5 draws.

**What the side navigation did cost.** `ui5-side-navigation-item` takes an icon
by name as well, and its default slot is for sub-items — it does not render in
the row. So a subsystem's own picture is not shown in ui5 mode. Decoration on a
handful of entries against a navigation list that behaves, and the way back is
our button list under the same tokens.

Measured on the Goods list and one object form, per mode:

| | list screen | object form |
|---|---|---|
| `ui=ui5` | 15 UI5 elements | 22 |
| `ui=legacy` | 0 | 0 |

## Colour a form chose, and colour it inherited

Two rules, both about telling one from the other.

**The platform's paper is not a choice.** Every control's colour properties start
at `wxDefaultStypeBGColour` (#FAF7F0 cream) and `wxDefaultStypeFGColour` (#3F5C77
dusty blue) — the desktop's palette — so a form that was never given a colour
still arrives carrying them. `ibWebWindow::ToJSON` and the sizer's now emit a
colour only when it is something else (`ibWebIsPlatformPaper` / `…Ink` in
`webWindow.cpp`). On the desktop those colours ARE the window; in a browser they
are a foreign surface over whatever theme the page is wearing.

**A surface a form DID choose still needs ink.** The demo's text controls carry
`#FFFFFF`, saved in the configuration, and that is a real choice — made against
the desktop's dark-on-light. Honouring it under a dark theme, while the theme
supplies the text colour, is how white ends up on white. So when a node names a
background and no foreground, the client puts the readable end of the theme's own
text scale on it (`readableInkOn`, WCAG relative luminance, resolving to
`--oes-ink-on-light` / `--oes-ink-on-dark`). Only in ui5 mode: legacy has no
second palette to get this wrong in.

Both rules apply to the *control*, not to the row around it. A text control
renders as a label plus a field, and the colour was set on the field —
`applyCommon` paints whatever the renderer names in `oesLookTarget`, so a white
field no longer puts a white box behind its caption.

## What every renderer has to do

Custom elements are `display: inline` until told otherwise, which quietly breaks
them as flex children of a sizer (ADR-004). Each renderer sets its own display —
`inline-block` for button and checkbox, `flex` for the text group.
