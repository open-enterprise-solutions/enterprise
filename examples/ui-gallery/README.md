# UI Component Gallery

A Storybook-style catalog of OES's form controls — one `NotebookPage` per control
type, built as a `Form` on the `UIComponentGallery` `DataProcessor`, inside this
folder's base. It was built live through the Designer's MCP server (an assistant
driving `metadata_create`/`form_add`/`form_bind`, not hand-authored), and is kept
here as a worked example of what that looks like, plus a record of what it found.

## Why `visualView`, not `uikit`

OES has two layers that could be called "the UI kit":

- `src/engine/frontend/uikit/` — a wxUniversal-fork, desktop-only renderer (the
  "Luna" theme). Unfinished, and the web client never calls into it at all.
- `src/engine/frontend/visualView/ctrl/` — the 19 form-control types a developer
  actually places from the Designer's palette. This is the layer a form's
  metadata is built from, and the only one with any chance of reaching the web
  client — see the gap below for how much of a chance that actually is.

A gallery built on `uikit` would showcase widgets the web client can never show
under any circumstance. This gallery catalogs `visualView` instead.

## The 19 catalogued control types

Exact registered names (`CONTROL_TYPE_REGISTER` in `visualView/ctrl/*.cpp`), one
gallery page each, plus a 20th "coming soon" page: `Button`, `Checkbox`,
`Radiobutton`, `Textbox`, `Textctrl`, `Statictext`, `Staticline`, `Slider`,
`Gauge`, `Tablebox`, `Gridbox`, `Chartbox`, `Htmlbox`, `Toolbar`, `Notebook`,
`Boxsizer`, `Gridsizer`, `Staticboxsizer`, `Wrapsizer`. Some controls (`Checkbox`,
`Textctrl`, `Tablebox`) need a bound `Attribute`/`TabularSection` to render at all
— confirmed by reading each control's own `form_control` property schema rather
than assumed; see the note on `UIComponentGallery` (metaID 1005) in the base
itself for the per-control findings.

## What each page shows

Every page opens with a heading and a two-line description, then the control's
variants, each captioned and grouped (mostly in a `Staticboxsizer`), with fixed
widths so nothing is wider than an 800-1000 px window.

| Page | Contents |
|---|---|
| Button | Default, picture and text, picture only, text only; disabled, wide (200 px), tall (40 px), with tooltip. Every button runs the `DemoAction` command (a button with no command bound is hidden by design). |
| Checkbox | Checked, unchecked, disabled (checked), title on the left - bound to `CheckboxChecked`, `CheckboxDisabled`, `CheckboxLeft`. |
| Radiobutton | A "Size" group (Small / Medium selected / Large) and a disabled one. |
| Textbox | An editable 480 x 140 box and a disabled 480 x 80 one (Textbox has no value or text property yet, so it starts empty). |
| Textctrl | Text, number, date, password, disabled and multiline fields with sample values, in one label column. |
| Statictext | Plain, bold 14 pt, coloured, italic, disabled and multi-line captions. |
| Staticline | A horizontal line between two captions, vertical lines between three. |
| Slider | Default, range 0..10, disabled, vertical - each captioned with its value and range. |
| Gauge | 0, 30, 75 and 100 of 100. |
| Tablebox | The `GalleryRows` tabular section with four sample rows (Fruit, Amount). |
| Gridbox | The `SampleSheet` template (a small price table), through the `SampleSheetValue` attribute it is bound to. |
| Chartbox | The built-in sample chart (its data is not configurable yet). |
| Htmlbox | A sample HTML page (heading, inline styles, list, table) set with `SetPage` in `OnOpen`. |
| Toolbar | New, Edit, Save, a separator, Print, and a disabled tool. |
| Notebook | A nested notebook with three pages, one with a picture. |
| Boxsizer | A horizontal and a vertical box sizer. |
| Gridsizer | A 3-column grid of six buttons. |
| Staticboxsizer | Framed groups with titles. |
| Wrapsizer | Twelve buttons that rewrap as the window narrows. |
| Coming soon | The registered-but-missing controls (Choice, ComboBox, ListBox). |

Supporting metadata on `UIComponentGallery`: the `DemoAction` command (posts
"Demo action: a gallery button was pressed." to the Messages panel), Boolean
attributes `CheckboxChecked` / `CheckboxDisabled` / `CheckboxLeft`, text
attributes `TextString` / `TextNumber` / `TextDate` / `TextPassword` /
`TextDisabled`, the spreadsheet attribute `SampleSheetValue`, and the
`SampleSheet` template. The form module's `OnOpen`
fills the sample values, the table rows and the HTML page.

## Known gaps

### `Choice`, `ComboBox`, the unregistered `ListBox` variant

These exist as C++ classes under `visualView/ctrl/` but are never passed through
`CONTROL_TYPE_REGISTER`, so they cannot appear in this gallery, or in any form on
either client, until someone separately verifies their rendering and registers
them. The gallery's 20th page names this gap instead of silently omitting them.

### The web client

The browser client is `webserver`'s: it draws the forms the server sends over the
client protocol (`docs/public/client-protocol.md`), the same the thin client
draws. List and document forms draw today; the rest of the controls, this
gallery's among them, are arriving. The old web frontend (`wfrontend`, the
`OES_USE_WEB` widgets) drew almost none of them, and no program starts it any more.

### Desktop: a page-selection bug and a tab-rendering bug

Both are fixed in a separate change (PR #528, "Notebook: inactive tabs vanish on
macOS when a control's Font is empty"); without it this gallery opens on an
arbitrary page and shows only the active tab.

Opening this gallery in `enterprise.exe` (or the Designer's own form editor) used
to show only one `NotebookPage` at a time, picking a different, seemingly
arbitrary page on every fresh run — with the visible tab's own label sometimes
not even matching what was drawn under it. Two distinct bugs were involved, both
in `src/engine/frontend/visualView/ctrl/notebook.cpp`:

1. **Selection — fixed.** A notebook built wholesale in one pass (every
   `NotebookPage::OnCreated` calls `AddPage(..., select=false)`, since no page
   has a reason to prefer itself over a sibling) never explicitly selects a
   page — but wx's standard "focusing a descendant switches its notebook to
   that page" behaviour fires anyway, for whichever control happens to receive
   initial keyboard focus, landing on an arbitrary page. Confirmed by tracing:
   `OnCreated` still reads the correct initial selection; the very next
   `OnUpdated` pass already reads a different one. Fixed by pinning the
   selection to page 0 exactly once, in `ibValueNotebook::OnUpdated`, guarded by
   a new `m_selectionSettled` member so a tab the user later switches to is
   never stolen back. Verified with a pristine-vs-fixed rebuild comparison: the
   unmodified code consistently mis-selects (confirmed across multiple cold
   restarts); the fixed build consistently selects page 0.
2. **Rendering — fixed.** Only the active tab painted in the strip; the other
   19 took their place (the active one sat where it belonged, e.g. mid-strip for
   `Textbox`) but drew nothing. Tracing `wxAuiLunaTabArt::DrawTab` showed every
   inactive tab being drawn with an invalid normal font. The gallery's controls
   carry a stored but unset `Font` (point size 0): the property is non-null, so
   `ibValueWindow::ApplyLook` passed `typeConv::FontToWxFont()`'s `wxNullFont` on
   to `SetFont()`. For a plain window that just resets the font, but
   `wxAuiNotebook::SetFont` copies it into the tab art as the normal-tab font
   (only the bold selected font comes out valid), and macOS draws text in an
   invalid font as nothing. `ApplyLook` now leaves the inherited font alone when
   the stored font is unset. The page contents were never the problem: a page
   whose controls are unbound (e.g. `Button`) is empty by design (see above).

   A third, layout bug is fixed here, not in #528: the `Coming soon` page's
   static text was one line about 2150 px wide, so the notebook was laid out
   wider than the form window, the strip believed every tab fit (no scroll
   arrows), and the tabs past the window edge (after `Gridbox` in an 800 px
   window) could not be reached. The text is now broken into lines with explicit
   `\n`, and the static text's caption is measured line by line
   (`ibControlStaticTextValue::CaptionExtent`), as it was already drawn, so a
   multi-line caption gets the height of all its lines.

### Desktop gaps found while building the showcase

Engine fixes, each on its own branch from `develop` and its own pull request:

- #543 `fix/radiobutton-title-saved` - a Radiobutton's `Title` and `Selected`
  were not serialised, so every radio button came back untitled and unselected.
- #544 `fix/htmlbox-setpage-before-create` - `SetPage` called from `OnOpen` was
  lost, because the window did not exist yet and `Create` always showed the
  dummy page.
- #545 `fix/label-column-per-page` - the label-alignment pass treated the whole
  form as one column (and counted unbound Statictext captions as labels), so on
  a notebook every field was pushed right by the widest caption on any page and
  title-left checkboxes ended up past the window edge.
- #546 `fix/textctrl-multiline-create` - a multiline Textctrl showed one centred
  line on macOS (`wxTE_MULTILINE` cannot be added after creation there, and the
  layout sized the editor to one line).
- #548 `fix/gridbox-object-spreadsheet-source` - a Gridbox bound to a
  spreadsheet field of the form's object did not resolve its Source at runtime,
  counted as unbound and was hidden.
- #549 `fix/tablebox-focus-hidden-page` - the table took the focus on a hidden
  notebook page, so the gallery opened on its Tablebox page.
- #550 `fix/wrapsizer-no-extend-last` - a wrap sizer stretched the last control
  of each line (`wxEXTEND_LAST_ON_EACH_LINE`), whatever its `MaximumSize`.
- #551 `fix/button-picture-logical-size` - buttons with a picture were sized
  with the bitmap's physical (2x) pixels on a Retina screen.

Still open:

- Every control's `BackgroundColour` defaults to the cream form colour and has
  no "unset" state, so captions draw cream boxes on the grey notebook page and
  inside groups (written up as a proposal; it needs a decision on the property).
- A vertical `Gauge` draws nothing on macOS, so the page shows horizontal ones only.
- `Textbox` has no value/text property and `Chartbox` no data binding, so neither
  can show configured sample content; toolbar tools have no action binding.

## Opening it

- Desktop: `designer.exe --file=examples/ui-gallery` /
  `enterprise.exe --file=examples/ui-gallery`, then Operations → Data processors
  → UI Component Gallery.
- Web: `webserver --file=examples/ui-gallery`, then the publication's address it
  prints (`http://127.0.0.1:8080/ui-gallery/`). Per the section above, expect what
  the browser client draws so far.
