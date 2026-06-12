# uikit — the OES custom-drawn UI engine

`src/engine/frontend/uikit/` is a full fork of the wxUniversal theme engine
(wx → ib prefixes applied mechanically) plus our own Luna theme. It makes the
widget layer of OES owner-drawn: wxWidgets below this layer is a
cross-platform *host* only — window handles, DCs, fonts/DPI, raw input,
clipboard, native system dialogs. Everything the user sees inside a window is
drawn by our renderer, so a pixel debugged on one platform looks the same on
every platform, and custom controls are not limited to the native widget set.

The native build of wx stays untouched: uikit does **not** require the
`__WXUNIVERSAL__` build. After the fork the wxUniv upstream is not a
dependency anymore.

## Architecture

```
ibThemeEngine (theme.h)            — theme registry/factory; lazy default
  ├── ibRenderer (renderer.h)      — ~80 Draw*/Get* primitives, pure interface
  │     └── ibThemeRenderer        — reusable flat implementation base
  │           └── ibLunaRenderer   — the Luna theme (lunaTheme.cpp, private)
  ├── ibColourScheme               — palette roles (docs/ui-palette.md tiers)
  └── ibInputHandler chain         — events → named actions → PerformAction
ibWindow : public wxWindow         — the cross-platform seam; buffered paint,
  │                                  own border pipeline, own scrollbars
  └── ibControl : ibWindow + ibInputConsumer — base of every control
```

Key mechanics:

- **Buffered single-pass paint.** `ibWindow` sets `wxBG_STYLE_PAINT` and
  paints background + control through one `wxAutoBufferedPaintDC` (the canon
  used a separate erase pass — it flickered). A control with its *own*
  `OnPaint` (e.g. the tree) must paint its background itself: the erase pass
  does not exist. The system caret is suspended around the whole pass
  including the buffer blit.
- **Borders are ours.** The native window is always created with
  `wxBORDER_NONE` — the univ border style is restored at the wx level and
  drawn by the NC pass (`RefreshBorder()`); otherwise MSW adds a second 3-D
  `WS_EX_CLIENTEDGE` frame and steals client pixels. A focused (or default)
  control gets the accent border.
- **Scrollbars are ours.** `ibWindow::SetScrollbar` creates `ibScrollBar`
  children placed by `PositionScrollbars()`; with Luna they live *inside* the
  control frame. `wxScrollHelper` consumers with dynamic content must call
  `AdjustScrollbars()` explicitly after `SetScrollbars()` — the wx helper
  only does that under `__WXUNIVERSAL__`.
- **Input.** Keyboard/mouse go through the `ibInputHandler` chain into named
  actions (`ibACTION_*`) handled by `PerformAction`. Interactive controls set
  `wxWANTS_CHARS` and re-implement what the dialog navigation used to do
  (Tab via `Navigate()`, Enter via the default-button lookup) — on a native
  host `IsDialogMessage` would eat those keys before the control sees them.

## Controls

Live (in `frontend.vcxproj`, all with per-file `ObjectFileName uikit_*.obj`
to avoid clashes with `visualView`):

| Group | Controls |
|---|---|
| buttons | ibButton (primary = terracotta), ibToggleButton, ibBitmapButton |
| switches | ibCheckBox (3-state), ibRadioButton, ibRadioBox |
| statics | ibStaticText, ibStaticLine, ibStaticBox, ibStaticBitmap |
| text | ibTextCtrl (single/multiline, wrap, undo/redo, password) |
| lists | ibListBox, ibCheckListBox, ibComboBox, ibChoice, ibTreeCtrl |
| misc | ibGauge, ibSlider, ibSpinButton, ibNotebook, ibToolBar, ibScrollBar |
| menu | ibMenu / ibMenuItem + popup window (context menus) |

Waiting for the univ frame: ibMenuBar, statusBar, dialog/frame/topLevel
(staged in the tree, gated with `#if 0 // UIKIT-REVIVE`).

Notable designs:

- **ibComboBox** is our own composition (not the generic `wxComboCtrl`):
  the value always lives in an `ibTextCtrl` child (read-only for
  `wxCB_READONLY`), items live in an `ibListBox` inside a transient popup,
  the host draws only the drop button. `ibChoice` derives from it.
- **ibMenu / ibMenuItem derive from the NATIVE wxMenu/wxMenuItem.** The
  generic `wxMenuBase` machinery traffics in the native types, so deriving
  from them keeps the bookkeeping (item list, `SendEvent`, invoking window,
  stock labels) and makes the downcasts safe; the HMENU the base creates is
  idle dead weight. The creating `Append*` overloads are hidden by factories
  producing `ibMenuItem`. Menus are shown by `ibWindow::PopupMenu(ibMenu*)`
  through a modal event loop; never queue events from inside that loop —
  set a flag and fire after it exits.
- **ibTreeCtrl** is forked from the *generic* `wxGenericTreeCtrl` (univ has
  no tree). Its lost base is re-declared as `ibTreeCtrlBase` on the ib chain
  with the same pure-virtual interface, so the generic implementation keeps
  every `override`. The in-place label editor is an `ibTextCtrl`.

## Sizing rules

- One control-row height: `ibButton::GetDefaultSize().y =
  max(charHeight*11/10 + 2, charHeight + 6)`.
- Button width follows the label (+2 average chars); the standard width
  applies only to an empty button.
- Text field default is compact — the text line + theme paddings
  (height = line + 4), width 8 average chars; it does not depend on the
  current value and stretches freely under a sizer.

## Password fields

- The mask has a **fixed length** (8 asterisks) — the real password length
  never shows. While typing the mask is live (one asterisk per char) and
  collapses back to the fixed length after 800 ms.
- Cut/Copy are completely disabled for password fields.

## Porting workflow (how a control comes alive)

1. The staged canon file (`FORKED ...` header) gets included into
   `frontend.vcxproj` with a `uikit_<name>.obj` object name.
2. Fix compile errors only — do not rewrite the canon. The recurring seams:
   - the lost `wx*Base` rode the native `wxControl` chain → rebase on
     `ibControl` (+ a shim block for the lost fields/inlines, or a full
     re-declared `ib*Base` for big interfaces like the tree);
   - `wxBEGIN_EVENT_TABLE(X, base)` must name the **ib** base, not `wx*Base`;
   - overloads silently resolving into `wxWindowBase` versions
     (`HitTest`, `GetClientData`, `SetSelection`);
   - dllimport inlines of `FRONTEND_API` classes never instantiate inside
     the DLL — bodies go to the .cpp;
   - RTTI `wxIMPLEMENT_DYNAMIC_CLASS` lived in wx common files — add it;
   - friend-access of the generic code (`wxTreeEvent` et al.) → use the
     public setters.
3. Theme contract: check that the Luna methods the control calls are not
   stubs — some contracts have invisible side effects (e.g.
   `GetMenuGeometry` *must* call `SetGeometry` on every item).
4. Add the control to the demo form (designer-side, currently parked) —
   every control needs a visual regression surface.

## Files

- `uikit/` — theme.h/.cpp, renderer.h, controlRenderer.cpp, colourScheme.h,
  inputConsumer, inputHandler, window, menu, menuItem, scrollTimer, uikit.h
  (umbrella header listing the revived part only)
- `uikit/ctrl/` — one file pair per control
- `uikit/theme/` — themeRenderer (the reusable base), lunaTheme (ours),
  theme_win32.cpp (staged canon reference, not in the build)
