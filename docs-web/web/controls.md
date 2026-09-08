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
