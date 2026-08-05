# Web UI shell — window chrome, dialogs, navigation

Foundational UI structure for the web build added across a single
session (2026-05-12). Replaces the prior bare sidebar + main split
with: titlebar at top, sidebar + main area in the middle, optional
output panel just above the statusbar, statusbar at bottom. Adds a
modal dialog framework (drag, declarative fields, shortcuts), backend
→ client message channel, and subsystem-style navigation (Interfaces
+ "All functions").

Lives entirely in `src/engine/frontend/web/webClient.cpp` (CSS + JS
embedded in the HTML shell) plus matching backend additions in
`wfrontend.cpp` / `wfrontend.h` and HTTP routes in
`wenterprise-server/main.cpp`.

---

## Layout

```
+---------- #titlebar -------------------+   gradient strip, app caption
|  OES Enterprise  | Все функции |       |   + menubar slot for menu buttons
+---- #sidebar ----+--- #main -----------+
|                  | #tabs-row           |   tabs strip
| #interfaces      +---------------------+
|  (subsystem      | #tab-body           |   form host (active doc tree)
|   buttons)       |                     |
|                  |                     |
+---- #outputpanel (hidden by default) --+   collapsible log strip
+---- #statusbar (thin, sb-tabs) --------+   status + Output toggle
```

`body` is flex-column so the middle row stretches; titlebar /
output panel / statusbar are auto-sized strips. `.form-host` uses the
flex chain (body → app-row → main → tab-body → form-host) to fill
remaining height — no hardcoded `100vh - N`.

## Dialog framework — `OES.showDialog`

```js
OES.showDialog({
  title: 'Filter',
  body:  'Plain text OR an HTMLElement',
  fields: [                                       // optional declarative form
    {name:'q', type:'text',     label:'Search', value:''},
    {name:'n', type:'number',   label:'Count',  value:0},
    {name:'a', type:'checkbox', label:'Active', value:true},
    {name:'k', type:'select',   label:'Kind',   value:'a',
                                options:[{value:'a',label:'A'},{value:'b',label:'B'}]},
    {name:'note', type:'textarea', label:'Note', value:''},
  ],
  buttons: [
    {label:'OK',     primary:true, onClick:(d)=>{ console.log(d.values); d.close(); }},
    {label:'Cancel',              onClick:(d)=>d.close()}
  ],
  onClose: ()=>{},
  dismissible: true,                              // Esc + backdrop click → close
});
```

Features:

- **Backdrop dim** (`rgba(15,20,35,.42)`) + centered card with rounded
  corners and shadow.
- **Drag by header** — mousedown on the header switches to
  `position:absolute`, drag clamps inside viewport so the close X
  stays reachable.
- **Stacking** — each layer pushes z-index +10. Esc closes the
  topmost.
- **Auto-focus** on the primary button so Enter commits the default
  action.
- **Idempotent close** — clicking the same button twice or closing
  via Esc after a button click is safe.

Shortcuts:

| Wrapper | Maps to |
|---|---|
| `OES.alert(text, title?)` | Single OK |
| `OES.confirm(text, onYes, title?)` | Yes / No, runs `onYes` on Yes |
| `OES.prompt(text, onSubmit, opts?)` | Single text field, OK / Cancel |

## Output panel — `OES.output`

Bottom strip between `#app-row` and `#statusbar`. Hidden until the
first message arrives or `OES.output.show()` is called. Status-bar
"Output" tab (sb-tab) toggles visibility; appending a line
re-opens automatically (matches `Message()` semantics — backend pushes,
client surfaces).

```js
OES.output('plain info')          // appended with level=info
OES.warn('caution')               // yellow
OES.error('failed')               // red
OES.debug('trace')                // gray
OES.output.clear()
OES.output.show() / hide() / toggle()
```

Drag the top edge (4-px handle) to resize. Empty panel doesn't render
the body; first append also fills the timestamp.

## Backend → client wiring

`ibBackendDocFrame` virtual surface on `ibWebFrame` (`web/webFrame.{h,cpp}`):

| Method | Web behaviour |
|---|---|
| `SetTitle(s)` | Stored; surfaced via `/session` JSON as `title`; client applies to `#app-title` + `document.title` |
| `SetStatusText(s)` | Stored; surfaced as `statusText`; client applies to `#status` |
| `Message(msg, status)` | Pushed onto `m_pendingMessages` queue under mutex; drained by `/session` as `messages[]` with level (1=info, 2=warn, 3=error); client appends to `OES.output` |
| `ClearMessage()` | Drops the pending queue + sets a one-shot `clearMessages` flag; client wipes its panel before applying new lines |
| `BackendError(...)` | Pushed as level=3 (error); rest as `Message` |
| `ShowModalMessage(msg, caption, style)` | Allocates an id, parks the worker on a `std::promise<int>` future; queued in `m_pendingModals`. `/session` surfaces `{id, message, caption, style}`. Client renders via `OES.showDialog` with buttons derived from the wx style mask (`wxOK` / `wxCANCEL` / `wxYES` / `wxNO`), POSTs `/modal-reply/<id>?result=<code>` which calls `ResolveModal` → promise unblocks → worker returns the code. Esc / × falls back to `wxCANCEL` (or `wxNO`, or 0). |

`wxIsMainThread()` guards were removed from
`ibValueSystemFunction::{Message,Alert,SetStatus,ClearMessage,SetError,Raise}`
in `backend/system/systemManagerFunc.cpp` — the frame override owns
thread safety now (web queues under mutex; desktop pops a wx dialog
directly). The guard was blocking legitimate calls from the per-
session worker thread on web.

Delivery is 2-second polling on `/session` plus SSE piggyback: every
SSE event (form mutation) also triggers a `pollChrome()` so chrome
updates arrive within one HTTP round-trip of the trigger instead of
waiting for the next poll tick.

## Navigation

### "All functions" (titlebar menu button)

Gated by `activeMetaData->AccessRight_ModeAllFunction()`. When
allowed, `accessAllFunctions:true` arrives in `/session`; client
lazily appends a `Все функции` button to `#menubar`.

Click → `GET /functions` returns groups (Constants, Catalogs,
Documents, Data processors, Reports, Information registers,
Accumulation registers) with each item carrying `id / name /
synonym / icon` (icon as base64 PNG data URI). Rendered as
collapsible `<details>` per group with `▸/▾` markers + icons next to
captions.

Item click → `POST /open-meta/<metaID>` →
`metaObject->ShowFormByCommandType()` (same path desktop's
`ibDialogFunctionAll` double-click uses). Active host JSON is
returned for immediate re-render.

### Interfaces (sidebar)

Loaded once at boot via `GET /interfaces`. Each top-level
`ibValueMetaObjectInterface` becomes a button in `#interfaces`. The
section stays hidden when the configuration has no interfaces.

Click on interface button → popup (via `OES.showDialog`) with
command sections — `Default` / `Create` / `Combined` / `Reports` /
`Service` — each a collapsible group of metadata items. Click on an
item → `POST /open-meta/<metaID>?cmd=<section>` so backend's
`ShowFormByCommandType(ibInterfaceCommandType)` lands in the right
flow:

- 100 (`Default`) — browse / list form
- 150 (`Create`) — new-record form
- 151 (`List`) — list form explicitly
- 152 (`Select`) — selector

## BuildForm runs on web (stubs for non-ported controls)

`ibValueForm::BuildForm` previously was a no-op under `OES_USE_WEB`.
It now runs on both builds; controls used by the auto-build that
weren't ported get web-side stub Create methods:

- `ibValueModelTableBox` / `ibValueModelTableBoxColumn`
  (`visualView/ctrl/tableBox.{h,cpp}` + `tableBoxColumn.cpp` +
  `*_res.cpp`) — declared in `tableBox.h` with wx-event handlers
  guarded by `#ifndef OES_USE_WEB`. `Create` on web returns
  `ibWebStubControl("tablebox")` / `("tableboxcolumn")`. Runtime
  surface (`LoadData` / `SaveData` / `PrepareNames` / `SetPropVal`
  / `GetPropVal`) stays shared — properties + events serialise the
  same way on both builds, scripts can read/write `Value` /
  `CurrentRow` on web too.
- All wx-touching method bodies (`Create` / `OnCreated` / `Update` /
  `OnUpdated` / `Cleanup` / `CreateColumnCollection` / event
  handlers / `CalculateColumnPos`) carry inline
  `#ifndef OES_USE_WEB` ... `#endif` guards. Per-method ifdefs were
  chosen over a single big region split so each function stays
  readable top-to-bottom.

This unblocks "All functions" / Interfaces opening any Catalog
without dropping into an empty form — the toolbar + properties pane
render fully, the tabular area shows a placeholder until the real
TableBox web port lands.

## Visual polish

- **TextCtrl side-buttons don't change outer width.** `.texted` is
  `flex:0 0 200px` — input shrinks via `min-width:0` to make room
  for any combination of `…` / `▷` / `×` side buttons. Mirrors
  desktop where adding a Clear button doesn't make the TextCtrl
  wider.
- **Disabled controls propagate.** `applyCommon` flips `disabled`
  on native form elements directly and, for wrapper `<div>`s,
  walks descendants and disables every `input/button/select/textarea`
  inside + adds `pointer-events:none`. Mirrors desktop's
  `Enable(false)` behaviour where the whole row reads as inert.
- **Tab icons reflect source meta-object.** `ibFormVisualDocument::
  OnCreate` calls `SetIcon(genericObject->GetIcon())` for the source
  (Catalog gets catalog icon, Document gets document icon).
  `ibWebFrame::CreateChildFrame` prefers `doc->GetIcon()` over the
  built-in `form->GetIcon()` fallback so tabs match desktop.
- **Side-button tab refresh.** Clicking `…` (Select) or `▷` (Open)
  on a TextCtrl spawns a selector / related form in a new tab;
  the client calls `refreshTabs()` immediately after the response
  so the new tab appears in the strip without waiting for the next
  2-second `/session` poll.

## Limitations / future work

- **TableBox real rendering on web.** Stub returns a placeholder
  `<div class="stubcontrol">tablebox</div>` on the client. List /
  hierarchical / tree view modes need a JS data-view renderer plus
  paged fetch endpoints.
- ~~**"All functions" missing kinds.** `ChartOfAccounts`,
  `ChartOfCharacteristicTypes`, `AccountingRegister`~~ — added
  2026-05-12 to both `ibDialogFunctionAll::BuildTree` (runtime
  desktop) and `wfrontendAllFunctionsJSON`'s `specs[]` (web).
- **Save (Записать) toolbar action.** Reported not visibly updating
  the form after click in some scenarios — investigation pending.
  Path: toolbar → `wxEVT_TOOL` → `ibValueToolbar::OnTool` → form
  `ExecuteAction` → catalog `WriteObject` → `NotifyChange` →
  `UpdateForm` → `UpdateVisualHost`. All steps available on web;
  needs a runtime trace to localise the gap.
- **`linq.log`-style diagnostics for the web action pipeline.**
  Would help nail down save-flow / refresh issues; not added yet
  to avoid spam in production logs.
- **Output panel formatted text.** Currently plain text + level
  coloring. A future iteration may accept inline `{html:'…'}` for
  formatted output (links, bold) — gated when a real use case
  appears.

## Files touched

| File | Role |
|---|---|
| `src/engine/frontend/web/webClient.cpp` | HTML shell rewrite (titlebar / statusbar / output panel / interfaces sidebar) + CSS for `.modal-*` / `.labelrow` / `#interfaces` / sb-tab / output panel; `showDialog` (drag, fields, stacking) + `alert`/`confirm`/`prompt`; `OES.output` + level shortcuts; `applyChromeFromSession` (title/status/messages/modal); `pollChrome` + SSE piggyback; `loadInterfaces` + `showInterfacePopup`; "Все функции" titlebar button + dialog. |
| `src/engine/frontend/web/webFrame.{h,cpp}` | `Message` / `ClearMessage` / `BackendError` / `ShowModalMessage` overrides with mutex-guarded queues; tab-icon preference (doc first, form fallback). |
| `src/engine/frontend/wfrontend.{h,cpp}` | `/session` JSON extended with `title` / `statusText` / `messages` / `clearMessages` / `modal` / `accessAllFunctions`; `wfrontendAllFunctionsJSON` (with icons + sections), `wfrontendInterfacesJSON` (interfaces + command sections + items), `wfrontendOpenMetaObject(cmdType)` (Create/Default/List/Select routing), `wfrontendModalReply` (resolve modal promise). |
| `src/engine/wenterprise-server/main.cpp` | HTTP routes `/functions`, `/interfaces`, `/open-meta/<id>?cmd=`, `/modal-reply/<id>?result=`. |
| `src/engine/backend/system/systemManagerFunc.cpp` | Dropped `wxIsMainThread()` early-returns from `Message` / `Alert` / `SetStatus` / `ClearMessage` / `SetError` / `Raise` — frame override owns thread safety. |
| `src/engine/frontend/visualView/ctrl/formObject.cpp` | `BuildForm` ifdef removed (runs on both); `toolBar.h` / `tableBox.h` now unconditionally included. |
| `src/engine/frontend/visualView/ctrl/tableBox.{h,cpp}` + `tableBoxColumn.cpp` + `tableBox_res.cpp` + `tableBoxColumn_res.cpp` | Web stubs added under per-method `#ifdef OES_USE_WEB`; added to `wfrontend.vcxproj`. |
| `src/engine/frontend/visualView/ctrl/tableBox.h` | Sidebar stubs for `ibDataViewSelectionMode` / `ibDataViewViewMode` enums under `OES_USE_WEB`; direct include of `compiler/enumUnit.h` (no longer transitively pulled in via `tableView.h`). |

---

## The client is loaded, not compiled in (2026-08-04)

It used to be a 2 300-line raw string literal inside `webClient.cpp`, split into five pieces to get
under MSVC's ~16 KB per-literal cap. Now it lives in `webClient/client.html` and `webClient.cpp`
finds it, using the same resolution the syntax helper uses for its corpus:

1. `<exe>/web/client.wpk` — a zip; what a release ships.
2. `<exe>/web/client.html` — unpacked beside the binary; what an administrator edits on a live
   installation without touching the platform.
3. **Walk up from `<exe>` looking for `webClient/`** — the development tree, bounded at six levels.

The third is the one that matters day to day: a dev run reads the file being edited, so changing
the client is a save and a reload rather than a rebuild of `backend` + `wfrontend`. During the web
phase — when this file is edited constantly — that is the difference between an afternoon and a
week.

**Packing** happens in `wfrontend.vcxproj`, target `StageWebClient`, modelled on
`StageSyntaxHelperContent`: `webClient/` → `$(OutDir)web/client.wpk`, DEFLATE, stamped so it re-runs
only when the sources change. The pack wins over a loose directory when both are present.

**A missing client is said out loud** — the loader serves a page naming the three places it looked,
rather than a blank screen. A deployment that forgot to copy the pack is a mistake worth reading,
not a mystery.

⚠️ The result is cached for the process's lifetime: the client is served on every page load and
re-reading 100 KB per request would be a self-inflicted slowdown. A dev run therefore picks up
changes on restart — the same granularity a rebuild used to give.
