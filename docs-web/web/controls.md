# The control shelf

*2026-09-07.*

What the web client can draw, and what it cannot. The list matters more than it
looks: a control the web build has no ctor for is not a control that renders
badly — it is a **form that cannot open**, because the tree is read through the
same ctor registry on both roads.

Before today the web build registered eleven control types, and one of the
missing ones was the notebook. A form with pages on it — which is most document
forms past the second field — had nothing to be built from.

## What is on it now

| Control | Type in JSON | What reaches the browser |
|---|---|---|
| Statictext | `statictext` | caption |
| Button | `button` | caption, picture, representation |
| Textctrl | `textctrl` | value, password / multiline, the three side buttons |
| Checkbox | `checkbox` | value, read-only |
| Toolbar, Tool, separator | `toolbar` / `tool` / `toolseparator` | caption, picture, action id |
| Tablebox + columns + groups | `tablebox` / `tableboxcolumn` / `tableboxcolumngroup` | [tablebox.md](tablebox.md) |
| **Notebook** | `notebook` | tab-strip edge, **which page is in front** |
| **Notebook page** | `notebookpage` | caption, picture, representation, visible |
| **Static line** | `staticline` | orientation |
| **Radio button** | `radiobutton` | caption, selected, **group** |
| **Gauge** | `gauge` | range, value, orientation |
| **Slider** | `slider` | min, max, value, orientation |
| **HTML box** | `htmlbox` | the page a script last set |

Sizers are not controls and are listed in [label-alignment.md](label-alignment.md);
`boxsizer`, `wrapsizer`, `staticboxsizer` and `gridsizer` all render.

## The notebook, and where the front page lives

A notebook draws its own children: the pages are **a strip of captions plus one
page**, not a stack of panels the browser hides with CSS. Which one is in front
is the notebook's, and it is held on the SERVER — `ibValueNotebook::m_activePage`,
emitted as `active` on the node.

That is not a preference. The control tree is written out afresh on every answer
the server gives, so a choice the browser kept to itself would be undone by the
next click on anything. And a page brought forward is an EVENT: `OnPageChanged`
runs the form's handler with the page as its argument, which is a script's only
chance to fill a page the moment it is opened rather than on form open.

So the road is: click a tab → `POST /fire/<notebook>/page?value=<page>` →
`ibWebNotebook::FirePageChanged` → `wxEVT_WEB_NOTEBOOK_PAGE_CHANGED` →
`ibValueNotebook::OnWebPageChanged` → the script event, then `UpdateForm`. The
answer carries the whole form back with the new `active`, and the client draws
the page it names.

`ResolveActivePage` answers before every render and repairs a stale choice: a
notebook with nothing in front shows nothing at all, so while there is a visible
page the answer always names one — the first visible one when nobody has picked,
and a new one when the picked page has been hidden or removed.

**A page carries a sizer of its own**, the way `ibPanelPage` does on the desktop,
and the walker was taught the general rule: a window that came with a sizer takes
its children INTO it. Without that the page's contents hang off the window
directly and every layout param they carry — proportion, border, expand — is
dropped on the floor.

An invisible page (`Visible` false) has no tab and no contents. That is what the
property means on the desktop too, where such a page is simply never added to the
notebook.

## The three that keep their state in a property

The radio button, the slider and the gauge have **no source binding and no script
member** — on either road. Nothing else holds their value, so the property is
where it lives, and a web click writes it there:

* a radio click turns its **group** off and itself on. The group is the nearest
  container above it with sizer items stepped over, and the server names it
  (`group` on the node) because the browser cannot see the control tree. Without
  the write, the next refresh would snap the choice back to the designed one.
* a slider commits **on release**, not per pixel: a drag would post a hundred
  times to say one thing.
* a gauge is read-only by nature — a quantity someone is shown.

The radio button's `Title` and `Selected` were **never serialised** — `ReadData`
and `WriteData` passed straight to the base — so a radio button drew as "Radio
button", unselected, whatever the designer had set. Fixed on both roads with the
port; it is why the control was worth having at all.

## The HTML box

`SetPage(html)` is the whole of its script surface, and on the web the text is
held in `ibValueHTMLBox` rather than in the render shim: the shim is what a
refresh re-reads.

The markup is drawn in **an iframe with `sandbox=""`** — no scripts, no origin.
It is the application's markup, not the client's, and it does not get to reach
into the page around it.

## The renderer lifecycle

The client keeps its DOM. Every answer the server gives is the whole form tree
again, and the client folds it into what is already on screen rather than
drawing it afresh — control by control, keyed by control id. What a rebuild
threw away is what survives: the scroll position, the caret, the grid with the
pages it had loaded, the row somebody was standing on.

So a renderer is three calls, not one:

| Call | When | What it answers |
|---|---|---|
| `render(node)` | no element stands for this node yet | the element |
| `update(el, node, prev)` | an element with the same key exists | `true` when it brought the element up to date in place, `false` when it cannot and the element is to be rebuilt |
| `dispose(el)` | the element is leaving the DOM | nothing; it lets go of what lives outside the DOM (a Tabulator instance) |

`BaseControl` supplies a default `update` that answers `true` when the node's
own properties are deep-equal to the previous ones and `false` otherwise — so
a class that overrides nothing is correct, just not cheap. The common
properties (`shown`, `enabled`, colours, font, tooltip, sizes, `layout`) are
left out of that comparison: `applyCommon` and `applyLayout` run on every pass
and reset everything they can set, so a change to one of them is never a
rebuild. A wrapper's `enabled` reaches the controls that are its own — those
with no managed element between them and it — and not a field on a notebook
page or a tool on a grid's bar, which are that child's to set whichever of the
two is reconciled first; what Tabulator draws inside its grid is nobody's.

**To keep its element, a renderer overrides `update` and changes the element.**
StaticText writes the label; Button and Tool redraw their content when the
caption key (representation, label, picture) moved; CheckBox, RadioButton,
Gauge and Slider write the value; StaticBoxSizer writes the legend; HtmlBox
sets `srcdoc` only when the page changed, because setting it reloads the
frame. A renderer answers `false` for what was decided at creation — a text
field's element kind (`input` / `textarea`) and its `type`, a tool's road, a
gauge's orientation.

**The key** is the control id, and `type@index` among its siblings for a
sizer, which has none. A different key or type in the same place is a new
element in place of the old; the old is disposed. Children of an ordinary
container are matched by key, moved into place, and the leftovers disposed. A
renderer that draws its own subtree (`ownsChildren`) reconciles it itself: the
notebook reconciles the page in front into its body, so a page switch replaces
the page and nothing else; the tablebox reconciles its command bar and compares
its column shape — a caption, a width, a sort arrow moved is a new grid, and
the same shape with a `dataVersion` past the one its rows were read at (every
page answer carries that; the previous tree's number is the fallback for a
server that does not stamp pages) is the same grid asked to re-read its rows
([tablebox.md](tablebox.md)). Its pages go out one at a time on a single
chain, and every `first` opens a new generation, so a `next` from a window the
server has thrown away cannot land on fresh rows under the same keys.

**Typing wins.** A text field's `update` writes the value while nobody is in
the field, or while the field still shows what the client last wrote there
(`oesApplied`). Otherwise the server's value waits in `oesPending` and lands on
blur when the field is left with no net change — the one case no `change`
event covers. A commit counts the sent text as applied, so the server's answer
to it is written even with the caret still in the field, under the caret rather
than after it; while an older commit is still unanswered the field counts as
being typed in, so its answer cannot overwrite the newer text.

**Focus** is captured before a pass and restored after it only when the element
that held it was rebuilt: the input of the same control id gets it back,
selection range included. An element brought up to date keeps its focus by
itself.

**Mounts.** There is one retained DOM per host, keyed by the root id the tree
carries (`mounts` in `client.html`), inside `#tab-body`; the active one is
shown and the others hidden, so a tab switch brings back the DOM it left. Each
mount remembers the last `seq` it applied, and a tree whose `seq` is not past it
is the same state again — the stream echoing a direct answer — and is dropped
([live-updates.md](live-updates.md)). The sequence is the session's and only
goes up, so the highest `seq` applied anywhere is a floor as well: a late frame
for a host that has since been closed finds no mount to compare against, and
without the floor would be mounted — a dead picker back on screen. After each
read of `/session`, a hidden mount whose host is on no tab is disposed; an
empty strip disposes every mount. An empty answer (`{}`, no active host) to any
road blanks the content area, takes a picker down, and brings the "Pick a form"
line back — except on the cell roads, where `{}` means the server refused the
value and the cell is put back instead. The picker dialog's body is a mount
like any other, and so is the harness: `?harness=1` posts fixtures through the
same `paintTree`, which is what lets a second fixture with the same root id be
reconciled into the first.

The label-alignment pass runs once per pass of a mount, in an animation frame,
as before. `.form-host` carries `overflow-anchor: none`: left on, the browser
moved the offset when content above the viewport was re-measured, and a host
shown again after its tab was hidden came back 138px above where it was left.

## What is still not on the shelf

**Combobox, Choice, Listbox** are empty shells on the DESKTOP as well: no items,
no binding, no properties, and `Update` has an empty body. Porting them would
ship three empty boxes. They need designing before they need rendering.

**TextBox** is the code editor (`textEditor`), **ChartBox** wants a charting
library the no-CDN rule would have to be answered for first, and **GridBox** is
the spreadsheet — each a piece of work in its own right rather than a port.

## Verification

The client half is measured against the harness fixture
(`webClient/assets/harness-form.json`, drawn through the real client by
`assets/harness.html`): tabs, the hidden page, both orientations of gauge,
slider and line, the sandboxed frame, and the click posting
`/fire/<notebook>/page?value=<page>` with the answer deciding what is drawn.

The server half is measured on a REAL form. The demo configuration had no
control of any of these kinds on it, so one was built through the designer's
MCP: a notebook `ExtraPages` at the foot of the Goods-receipt document form,
with four pages — *Delivery* (three radio buttons in one group, a rule),
*Progress* (a gauge and a slider), *Help* (an HTML box), and a fourth with
`Visible` false. Its `OnPageChanged` writes a line and fills the HTML box.

What that showed, in the browser, against the running server:

* three tabs, and the invisible page has none;
* a tab click answers with `active` naming the page that was clicked, and the
  page's contents are drawn from that answer;
* `OnPageChanged` runs on the SERVER — its `Message` line appears in the output
  pane the moment the tab is clicked — and what it writes reaches the browser:
  the Help page's frame carries the markup the module put there;
* the gauge draws its stored value, the slider its stored position, and the
  three radio buttons come back grouped (`group` = the page they share).

Two things the round turned up, neither of them the web client's:

**A control with no VALUE is not on `ThisForm`.** The handler was first written
`ThisForm.HelpPane.SetPage(…)`, which compiles and then fails at run time —
`ibValueForm::FillFormMembers` keeps only controls that answer
`HasValueInControl()`. `Controls.HelpPane` is the road that works.
[#149](https://github.com/open-enterprise-solutions/enterprise/issues/149).

**The radio buttons' captions read "Radio button"** on that form, because the
configuration was written by a designer binary from before the `ReadData` /
`WriteData` fix above: the property genuinely was never stored. The fix is in
this tree; a form saved by a designer built from it keeps the caption.
