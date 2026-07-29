# Web-specific code conventions

> **Language:** this document (and every file under `docs/`) is written
> in English. Source-file comments are in English as well; only chat
> replies and memory/brainstorming notes may be in other languages.

## Shared clsids across desktop and web

**Design rule:** a control's `string_to_clsid(...)` identifier in
`CONTROL_TYPE_REGISTER(...)` is the same on desktop and web. When a new
control is added to the desktop `frontend.dll`, dropping the same source
file into `wfrontend.vcxproj` (and guarding any wx-only code with
`OES_USE_WEB`) is enough — metadata saved by Designer loads on both
platforms without per-platform translation tables. Do **not** invent a
parallel clsid scheme for the web build.

The web-only class (`ibWeb<Control>` in `webWindow.{h,cpp}`) is the
rendering shim, not a new control type; it attaches to the same
`ibValue<Control>` and receives the same clsid-keyed property bag.

## Control id sources

Two id spaces, with a hard rule that form-bound controls must
always carry the runtime id into the web shim:

1. **Runtime / form id** — owned by `ibValueFrame::GenerateNewID`
   (walks the owner form tree, `max + 1`). Canonical id used by
   scripts, metadata, `form->FindControlByID`, and therefore by
   the dispatcher when routing HTTP click/text/toggle events.
2. **Web-local auto-id** — a process-wide `std::atomic<int>` inside
   `web/webWindow.cpp` starting at `1'000'000`. Kicks in only when
   the ctor receives `id <= 0`. Range-disjoint from runtime ids so
   the two spaces never collide.

**Id is generated before Create**, not during or after. The walker
in `visualHost.cpp::AppendChildControls` runs:

```cpp
if (child->GetControlID() == 0 && child->GetOwnerForm() != nullptr)
    child->GenerateNewID();
wxObject* created = child->Create(createParent, host);
```

Metadata-saved ids keep their values; 0-loaded controls get fresh
form-scoped ids. The web shim then receives that id via
`new ibWebButton(GetControlID())`. Browser's `node.id` matches
`ibValueFrame::m_controlId`, so `form->FindControlByID` on POST
`/action/<id>` hits, `OnTool` / `OnButtonPressed` / `OnChange`
actually run. **Do not rely on the web-local auto-id pool for
form-bound controls** — it's a fallback for detached nodes (`/demo`
synthetic trees, standalone renders) where no dispatcher lookup
happens.

Historical pitfall (fixed 2026-04-18, see `open-issues.md` →
"Click-dispatch id mismatch"): a form loaded from metadata with
`m_controlId == 0` produced a round-trip mismatch — the browser
sent back an auto-id in the 1M+ range, server searched the form
tree by the frame-level id, miss, silent no-op. The walker-level
`GenerateNewID` call closes that gap.

## `static_cast` vs `dynamic_cast` — when the type is Create-known

Every `ibValueXxx::Create` has a deterministic return type
(ibValueButton → ibControlButton desktop / ibWebButton web;
ibValueTextCtrl → ibControlTextEditor / ibWebTextCtrl; etc.).
`OnCreated`, `Update`, `Cleanup` receive that same pointer back as
`wxObject*` — the walker doesn't substitute another object. So
inside those methods **the wxObject is guaranteed to be the concrete
subclass Create returned**: use `static_cast`, not `dynamic_cast`.

```cpp
// Desktop
ibControlTextEditor* ed = static_cast<ibControlTextEditor*>(wxobject);
// GetWxObject returns what Create stored; same invariant.
ibControlCheckbox*   cb = static_cast<ibControlCheckbox*>(GetWxObject());
```

Same rule in `ibWebApplication::Dispatch` for the host-map lookup:
the web walker inserts **only `ibWebWindow*` entries** into
`m_baseObjects` (sizers skip the map — they don't receive HTTP
events, and they'd force a runtime discriminator on every dispatch).
The invariant lets the dispatcher static_cast the lookup result:

```cpp
wxObject* obj = host->GetWxObject(ctrl);
if (obj == nullptr) return false;
ibWebWindow* web = static_cast<ibWebWindow*>(obj);  // invariant-safe
```

**The walker uses the component type as discriminator, not `dynamic_cast`.**
`ibValueFrame::GetComponentType()` returns `COMPONENT_TYPE_FRAME` /
`COMPONENT_TYPE_WINDOW` / `COMPONENT_TYPE_SIZER` / `COMPONENT_TYPE_SIZERITEM`
— authoritative at compile time about what `Create()` will hand back.
`AppendChildControls` `switch`es on that and `static_cast`s. Parent is
passed as a typed pair `(ibWebWindow* parentWindow, ibWebSizer* parentSizer)`
with exactly one non-null, so the parent-attach branches also avoid
casts.

**One remaining `dynamic_cast`**: `ibNoObject` filter right after
`Create()`. The base `ibValueFrame::Create` returns `new ibNoObject`
(sentinel — non-null but neither a web window nor a sizer) for
subclasses that haven't been ported. `static_cast` would be UB on
that; one `dynamic_cast<ibNoObject*>` before the switch is the
safety check. Everything downstream is invariant-safe static_cast.

**Owner chain walk** in the "find create parent window when parent is
a sizer" branch still uses `dynamic_cast<ibWebWindow*>`/`<ibWebSizer*>`
on `ibWebSizer::GetOwner()`. Cold path — only triggered for windows
that sit inside sizers that sit inside sizers. Typed owner pointers
would remove this if it becomes hot.

## Control lifecycle on web

Mirrors the desktop `GenerateControl` + `RefreshControl` sequence so
custom controls see the same hooks on both platforms, but merged
into one walker pass because the web tree is rebuilt atomically per
HTTP response:

```
for each child:
    Create       → raw ibWebWindow shim (id + wxEvent binds, no
                   property state). Cheap; reused across refreshes.
    Update       → push property values into the shim — label,
                   enabled, shown, source-backed value, etc. This
                   is where ALL state lives on web; Create just
                   allocates an empty vessel.
    (recurse into children — full Create → Update → OnCreated →
     OnUpdated cycle for each)
    OnCreated    → post-order hook; children already built + updated.
                   Use for parent-level wiring that needs children
                   in place (e.g. toolbar action-collection snapshot
                   was once here; now moved to Update so refresh-only
                   passes cover it too).
    OnUpdated    → post-order hook; children already refreshed.

On ClearVisualHost (structural rebuild):
    Cleanup (post-order) — one per node that has a web-shim entry
                   in m_baseObjects. Symmetric to Create: fires
                   before the shim is destroyed, while siblings are
                   still reachable.
```

UpdateVisualHost walks the existing tree without re-creating shims
— calls Update + OnUpdated only, cheap refresh path. Structural
changes still go through Clear + Create + Update.

**Why Create stays dumb and Update does the work**: lets the same
Update body handle both first-paint (after Create) and subsequent
refreshes, with no allocation churn. Matches wxWidgets where
`new wxButton(parent, id)` is the raw ctor and all state (label,
bitmap, enabled) is set afterwards via setters.

## Sizer hierarchy (`ibWebSizer`, not `ibWebWindow`)

Sizers mirror desktop wxWidgets exactly: **`ibWebSizer : public wxObject`**,
**not** an `ibWebWindow` subclass. They live in `webSizer.{h,cpp}`. A sizer
is a layout container analogous to a CSS `<div>` with flex/grid — it is
not evented, has no label, no enabled/shown state, and no wxEvtHandler
baggage. The browser renders the `div`; CSS does the math.

Rules:

- **One owner per sizer** — either an `ibWebWindow` that called
  `SetSizer(sizer)`, or a parent `ibWebSizer` that called `Add(sizer)`.
  Stored as `wxObject* m_owner`. Symmetric with `wxWindow::SetSizer` /
  `wxSizer::Add` on desktop.
- **Per-child layout params via `ibWebSizer::AddParams`** —
  `{ proportion, flag, border }` mirroring
  `wxSizer::Add(child, proportion, flag, border)`. Each `Add` stores an
  `Item { child, params }`. `flag` is a bitmask of
  `wxEXPAND | wxLEFT | wxRIGHT | wxUP | wxDOWN` + `wxStretch` values
  (`wxSHRINK | wxGROW | wxALIGN_*`). Populated from
  `ibValueSizerItem` in the metadata tree — SizerItem is the metadata
  carrier for these hints, not a visual node.
- **Children are heterogeneous** — `ibWebWindow*` or nested
  `ibWebSizer*`. `Add(ibWebWindow*, params)` and
  `Add(ibWebSizer*, params)` overloads. Sizer owns its children; dtor
  deletes them.
- **Window-side hookup**: `ibWebWindow` gains a single
  `ibWebSizer* m_sizer` slot + `SetSizer/GetSizer`. Window owns the
  sizer. `ibWebWindow::ToJSON` embeds the sizer's JSON at the tail of
  its `children[]` array so the browser's existing renderers just see
  another child node — no client change needed to keep forms rendering.
- **Walker** (`visualHost.cpp::AppendChildControls`) dispatches on the
  `Create()` return type: `ibWebWindow*` → `SetParent` on nearest window
  ancestor; `ibWebSizer*` → `SetSizer` on parent window OR `Add` on
  parent sizer. `attached` is the just-created node regardless of type,
  so the recursion descends naturally.

Why this differs from the earlier layout (`ibWebBoxSizer : ibWebWindow`):
the previous design gave sizers a label, enabled/shown flags and a
wxEvtHandler vtable they never used, and conflated two distinct kinds of
tree nodes (widgets + layout). The wxObject-based split matches desktop
1:1, so code paths that already take `wxObject*` (`Create` return,
`OnCreated`, `Update`, `Cleanup`) need no special handling.

## `CONTROL_TYPE_REGISTER` vs `S_CONTROL_TYPE_REGISTER`

Two registration macros in `visualView/controlCtor.h`:

- **`CONTROL_TYPE_REGISTER`** — standalone controls (`ibCtorControlType`).
  Can be created on their own via `ibValueForm::NewObject(clsid, parent,
  false)`: button, statictext, textctrl, boxsizer, ....
- **`S_CONTROL_TYPE_REGISTER`** — **system / subordinate** controls
  (`ibCtorSystemControlType`, `IsControlSystem() == true`). Only live
  attached to a specific parent and are never top-level: `SizerItem`
  (inside any sizer), `NotebookPage` (inside a notebook),
  `TableboxColumn` (inside a tablebox), `Tool` / `ToolSeparator`
  (inside a toolbar), `ClientForm` (the root form). Instantiation goes
  through the parent control's own code path, not standalone
  `NewObject`.

Practical consequence for the stub-first port below: S-registered types
still need their `*.cpp` added to `wfrontend.vcxproj` so the static
registration fires and `clsid_to_string(clsid)` works, but the
rendering path for them isn't the general `NewObject → Create` flow —
don't bolt a "new wxObject" stub into their `Create` override without
checking how the parent actually constructs them. When in doubt, copy
the desktop constructor path first, then guard wx-only code with
`OES_USE_WEB`.

## Stub-first control registration

For every control that isn't yet fully ported, **include its source
file in `wfrontend.vcxproj` now** with a placeholder
`Create(ibFrontendWindow*, ibVisualHostClient*)` returning a throwaway
`new wxObject` (or the plainest `ibWebWindow` with only `type` set —
whichever pattern is lighter in context). This has two effects:

1. The `CONTROL_TYPE_REGISTER` static fires → `NewObject(clsid)` stops
   returning nullptr → metadata forms that use the control load cleanly
   instead of losing their subtree.
2. The JSON payload carries the control with `type: "unknown"` (or a
   stable placeholder type), which the JS side renders as a visible
   stub. That's good enough for prototype screenshots and keeps layout
   math honest.

As real rendering lands for a control, the stub is swapped for the
proper `ibWeb<Control>` + JS renderer. No vcxproj / metadata migration
needed — the clsid, class name, and property bag are already in place.

Don't add extra `#ifdef OES_USE_WEB` gates around the
`CONTROL_TYPE_REGISTER` line itself. The registration is trivially
cheap and is the whole point of the inclusion.

## `OES_USE_WEB` ifdefs

Only `wfrontend.dll` is compiled with `OES_USE_WEB` defined.
`frontend.dll` and `backend.dll` do **not** see this macro. Implications:

- Anything guarded by `#ifdef OES_USE_WEB` in a file compiled for both
  DLLs is invisible to the desktop build. Don't put a web-only fix
  there under this guard and expect it to affect a shared class —
  change the unconditional code instead. Example: when the frame
  singleton was removed in favour of session-owned frames, the change
  was made unconditionally in `backend_mainFrame.{h,cpp}` and `ibSession`,
  not gated on `OES_USE_WEB` — both desktop and web get the same
  ownership story.
- `visualView/ctrl/*.cpp` files are compiled into both `frontend.dll`
  and `wfrontend.dll`. Use `OES_USE_WEB` to pick the web branch
  (typically: construct an `ibWebWindow` subclass instead of a native
  `wxWindow`).
- Temporary instrumentation in shared files (`std::cerr` traces) should
  be wrapped in `OES_USE_WEB` so desktop builds stay clean.

## `ibValuePtr<T>` usage

Use `ibValuePtr<T>` to replace **manual IncrRef/DecrRef pairs** on
member fields holding `ibValue`-derived pointers. Clear wins:

- `ibWebApplication::m_moduleManager` (refactored).
- Anywhere else you see paired `IncrRef()` in a ctor/setter and
  `DecrRef()` in a dtor/teardown.

Don't use it for:

- Borrowed observer pointers where another object already owns the
  refcount (see `ibWebFrame::m_activeForm`) — you'd add a redundant
  reference without fixing a bug.
- Short-lived locals inside one function — raw pointer is cheaper and
  reads the same.

Header hygiene: `value_ptr.h` only forward-declares `ibValue` via
`backend_core.h`. If you add `ibValuePtr<T>` as a class member in a
header, include `backend/compiler/value.h` before `value_ptr.h` so the
base class is complete.

## JavaScript — per-type control renderers

The inline client in `wenterprise-server/main.cpp` uses a class-per-type
pattern, not a big `switch`:

```
class BaseControl { render(node){...} applyCommon(el,node){...} }
class Button extends BaseControl { render(node){ ... /action/id ... } }
class StaticText extends BaseControl { ... }
...
const renderers = { 'button': new Button(), 'statictext': ..., };
function render(node) { return renderers[node.type].render(node); }
```

New control types: add the C++ `ibWeb<Control>` (ToJSON emits
`"type":"<name>"`), then add a matching JS class and register it in the
`renderers` map. Don't branch in `render()` directly.

## Event model (script / HTTP / UI)

Button clicks (and future control events) flow through `wxEvtHandler`:

```
HTTP POST /action/<id>
  → SessionManager::FireAction
  → ibValueButton::FireClick (OES_USE_WEB-only public)
  → CallAsEvent(m_onButtonPressed, ...)     ← wxEvtHandler path
  → script (ibProcUnit bytecode)
  → side effects on form
  → ibVisualHostClient::WalkToJSON           ← rebuild tree
  → JSON response replaces DOM
```

Because scripts can run on the handler thread (not a UI thread) and
multiple sessions live concurrently, cross-thread event posting must go
through `QueueEvent` / `ProcessPendingEvents`, not direct
`ProcessEvent`. For today's synchronous HTTP-response cycle we're still
on the handler thread throughout; the `wxEvtHandler` inheritance is in
place so the rewiring is local when we go async.

## Boot overlay and tab-strip overflow

The initial page of `wfrontendClientHTML()` runs four asynchronous
fetches (`login`, `loadForms`, `refreshTabs`, `paintActiveTab`) before
the page is usable. Firing them without `await` — the original
implementation — let the browser paint each one as it returned, so the
user watched the sidebar, tab strip, and host tree pop in one by one.

The mitigation:

- A full-page `#bootOverlay` is served with the HTML; the last JS
  statement in the IIFE awaits `Promise.all([...].catch(...))` over all
  initial fetches, then fades the overlay out and removes it. Each
  fetch's error is swallowed individually so one slow endpoint can't
  keep the overlay on screen forever.
- `startLive()` (polling or EventSource) runs *after* the overlay
  clears, so its first tick doesn't race the initial paint.

The tab strip (`#tabs`) is a horizontally-scrollable flex container
(`overflow-x:auto`, default scrollbar hidden) inside `#tabs-row`, which
also holds two scroll buttons (`.tabs-scroll`). A small controller
toggles the `visible` class on the buttons whenever the strip
overflows:

```
scrollWidth > clientWidth + 1 → show ‹ and ›
```

Listeners that drive the toggle:

- `ResizeObserver(tabsBar)` — window resize, sidebar toggle, browser
  zoom.
- `MutationObserver(tabsBar, {childList:true})` — `refreshTabs` clears
  and re-adds child tabs, which triggers this.
- `scroll` on `tabsBar` — keeps the state consistent while the user
  drags the strip manually.

Scroll step per button click is `Math.max(80, clientWidth * 0.6)` —
large enough to feel responsive, small enough not to overshoot tabs.

## Tab-duplicate detection

`sessionStorage.oes_tab_sid` survives `window.open` reloads by design
and is copied verbatim when the browser duplicates a tab. Two tabs
sharing the same sid would hit the same `ibWebSession`, producing
cross-talk in the tab strip and form dispatch.

`webClient.cpp` guards against this via a `BroadcastChannel(
'oes-tab-sid')` ping/claim handshake on startup:

1. Each tab sends `{type:'ping', sid: tabSid}`.
2. Any tab already holding that sid replies `{type:'claim', sid}`.
3. After a 150 ms window, if a claim arrived the current tab rotates
   its `tabSid`, rewrites `sessionStorage`, and calls
   `location.reload()`. The server mints a fresh `ibSession` on the
   first hit with the new id, so the duplicate boots as a clean
   independent session. The original tab is unaffected.

BroadcastChannel-less browsers (older Safari) fall back to the
pre-fix cross-talk behaviour — tolerable degradation until those
versions age out.

## Logging prefixes

Keep `std::cerr` prefixes short and stable:

- `[app]` — lifecycle.
- `[session]` — session manager bookkeeping.
- `[<ClassName>]` / `[<function>]` — targeted diagnostic in a specific
  location. Remove these once the bug is closed; they're not permanent.

Avoid `std::cout`. Server's stdout is the HTTP body for a response in
some paths — stderr is safer.
