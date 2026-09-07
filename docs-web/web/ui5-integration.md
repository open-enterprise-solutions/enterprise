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

**The group is layout, not a frame.** It used to paint a field background and a
1px border in `sapField_BorderColor` so that the input and its side buttons would
read as one control, the way a UI5 combo does. But `ui5-input` draws a themed
field of its own inside it, so what came out was a box around a box — and in the
dark theme `sapField_BorderColor` is a light grey meant for a single underline,
not for a full rectangle. The result was a pale frame, taller than the field it
held, around a white slab. Reported 2026-09-07. The input owns the field's look;
the group owns the row.

No OES counterpart: `valueStateMessage`, `showSuggestions`, `maxlength`,
`placeholder`, `required`, `name`.

## Staticboxsizer -> `fieldset` + `legend`

The group box is not a UI5 component: a `fieldset` with a `legend` is what the
platform's static box means, and the theme reaches it through tokens like every
other strip that stayed ours.

Its caption arrives as **`title`**, not `label` -- a sizer names its caption
`title` (`ibWebStaticBoxSizer::ToJSON`), and only a *window* carries `label`. The
renderer read `label` alone, so every group box on every form drew an empty
legend and the group's name was simply not on the screen. Found 2026-09-07 on the
first hand-laid form; nothing before that had a group box on it.

## What the renderer does not draw yet

Twelve of the platform's twenty-six control classes have a renderer here:
`boxsizer`, `wrapsizer`, `staticboxsizer`, `gridsizer`, `sizeritem` (as layout on
its child), `statictext`, `button`, `textctrl`, `checkbox`, `tablebox`,
`toolbar` / `tool` / `toolseparator`.

The rest reach the browser as a node nothing claims, and `BaseControl` draws
them as an empty element: `Notebook` / `NotebookPage`, `Radiobutton`, `Textbox`,
`Gridbox`, `Chartbox`, `Htmlbox`, `Gauge`, `Slider`, `Staticline`, `ClientForm`.
A form using one of those has a hole in it rather than a placeholder, which is
worth knowing before a demo is built on one. Filed as
[#104](https://github.com/open-enterprise-solutions/enterprise/issues/104), with
the placeholder and the build-out named as separable pieces of work.

**A button with no command is not a gap.** `ibValueButton::Update` hides a button
whose bound command does not resolve -- deliberately, and on the desktop too:
"a button carries ONLY a command", so an unbound one has nothing to do. It shows
up here as three buttons that render `display: none`, which reads like a
renderer fault and is not one.

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

## When the theme is not there

450 module requests land on a cold cache before the theme does, and until
then — or if it never arrives — every `--sap*` is undefined. An unresolved
`var()` does not fall back to anything sensible: `border-color` collapses to
`currentColor`, which is the text colour, which is black. That is what an
unthemed page looked like when it was photographed: heavy black table borders,
a black tab underline, fields with no box at all. It read as a broken screen
rather than a plain one.

Three things now stand between that and the user:

- **Every token carries a fallback.** `var(--sapList_BorderColor, #d9d9d9)`, and
  so on for all nineteen. A page without the theme looks like the page without
  the theme.
- **The boot waits for it.** `await Promise.race([OES.ui5Ready, 8s])` before the
  first paint, so half a window in the theme and half in the fallback is not a
  state anyone sees. Bounded, because a theme that never arrives must not hold
  the client hostage.
- **A failure says so.** `OES.ui5Ready` catches, writes
  `data-oes-ui5="failed"` on `<html>` and logs one line. `"ready"` when it
  landed. Before this it failed silently and the only evidence was the colour of
  a border.

Verified by aborting every `parameters-bundle.css.js` in the browser: the flag
reads `failed`, `--sapSelectedColor` is unset, and `--oes-chrome-selected`
answers `#3b50a0` instead of nothing — the tab underline and the table borders
stay in the plain palette.

**And a package's parameters have to be asked for.** `Assets.js` is what
registers a package's theme bundle, and it is loaded by name at run time, not
imported by any component — so the vendoring's import-graph walk cannot find it.
`@ui5/webcomponents-fiori`'s bundle was missing for exactly that reason (found
2026-09-07, on the same hunt), and its components were quietly rendering with
the base package's parameters. Both `Assets.js` files are entrypoints now.

## Colour a form chose, and colour it inherited

Two rules, both about telling one from the other.

**The platform's paper is not a choice.** Every control's colour properties start
at `wxDefaultStypeBGColour` (#FAF7F0 cream) and `wxDefaultStypeFGColour` (#3F5C77
dusty blue) — the desktop's palette — so a form that was never given a colour
still arrives carrying them. `ibWebWindow::ToJSON` and the sizer's now emit a
colour only when it is something else (`ibWebIsPlatformPaper` / `…Ink` in
`webWindow.cpp`). On the desktop those colours ARE the window; in a browser they
are a foreign surface over whatever theme the page is wearing.

**Plain white is on that list too.** It was read as a choice at first — the demo's
text controls all carry `#FFFFFF`, so it looked like something saved in the
configuration. It is not: `ibValueTextCtrl`'s constructor sets it, on every text
control ever built, because on the desktop a field is white. Under a dark theme
that came out as a white slab per field, and no form had asked for it. So
`ibWebIsPlatformPaper` answers to white as well. The cost is that an author who
genuinely wants a white field does not get one — and on a light theme the field
is white regardless.

**A surface a form DID choose still needs ink.** A background that survives the
test above was named by somebody, and named against the desktop's dark-on-light.
Honouring it under a dark theme, while the theme supplies the text colour, is how
white ends up on white. So when a node names a background and no foreground, the
client puts the readable end of the theme's own text scale on it (`readableInkOn`,
WCAG relative luminance, resolving to `--oes-ink-on-light` / `--oes-ink-on-dark`).
Only in ui5 mode: legacy has no second palette to get this wrong in.

Both rules apply to the *control*, not to the row around it. A text control
renders as a label plus a field, and the colour was set on the field —
`applyCommon` paints whatever the renderer names in `oesLookTarget`, so a white
field no longer puts a white box behind its caption.

## What every renderer has to do

Custom elements are `display: inline` until told otherwise, which quietly breaks
them as flex children of a sizer (ADR-004). Each renderer sets its own display —
`inline-block` for button and checkbox, `flex` for the text group.
