# Open issues and active TODOs

## Active

### Session registry refactor — **landed** (canonical: [`../session-registry.md`](../session-registry.md))

The refactor is in tree and the web path runs through it. The full
architecture, step-by-step landing log, and every gotcha live in the
canonical doc — this section only records the current shape and what
the web path still owes.

**Current shape (verified against `backend/session/`):**
- `ibSession` (`session.{h,cpp}`) is the lifecycle unit. It inherits
  `std::enable_shared_from_this<ibSession>`; the registry owns it in
  `m_own` and removes it on `ibSession::Close(bool force)`. **There is
  no `ibSessionTicket`** — that RAII owner was part of the early
  skeleton and was dropped; sessions are driven by `Open()` / `Close()`
  directly (`session.h:219,298`).
- `ibSessionRegistry` (`sessionRegistry.{h,cpp}`) keeps the unified
  `Connect(const ibConnectRequest&) → ibConnectResult` entry point
  (`sessionRegistry.h:252`). `ibWebSession::Login` connects through it;
  the desktop / designer paths go through
  `EnsureStartedForCreateSession`.
- **Liveness is heartbeat-based, not pessimistic row locks.** The
  registry owns ONE `sys_session` write connection, `m_writeConn`
  (`sessionRegistry.h:644`). `JobHeartbeatOwn` UPDATEs `lastActive` on
  the process's own rows every ~1s; `JobSweepStale` treats any row
  whose `lastActive` trails `now` by more than `kStaleCutoffSec = 10`
  seconds as a zombie and deletes it (`sessionRegistry.cpp:1392`).
- **The row-lock probe scheme is retired.** `HoldRowLocks` /
  `TryProbeRowLock` / `ibTxOptions::noWait` were tried as a liveness
  fast path and abandoned (self-deadlock on the registry's own write
  connection); they survive only as no-op base virtuals and
  retrospective comments. There is no separate `m_lockConn` /
  `m_probeConn` and no `ibSessionRegistry::ProbeSessionRowLock`. Do not
  reintroduce per-driver `HoldRowLocks` / `TryProbeRowLock` impls — see
  `../session-registry.md` §4 for why the approach was dropped.
- Per-session user identity (`m_userInfo`, `m_sessionRawPassword`,
  `m_workDate`) lives on `ibSession`; the singleton `m_userInfo`
  fallback on `ibApplicationData` is gone. Session-aware accessors on
  `ibApplicationData` forward to `ibSession::Current()` so legacy
  callers picked up per-tab behaviour without call-site churn. This
  closed the web multi-tab "last login wins" bug
  (`project_web_session_bug.md`).
- `ibDesignerExclusivePolicy` (`designerExclusivePolicy.{h,cpp}`) vetoes
  a second designer Add by consulting the cluster snapshot — now via the
  heartbeat staleness check, not a row-lock probe.
- `sys_session` carries `pid` / `address` / `currentActivity` columns
  (added via `MigrateTableSession`).

**Web path still owes:**
- `m_address` populated from wenterprise-server's host:port (currently
  left blank by the web caller).
- Snapshot SELECT reading `pid` / `address` / `currentActivity` / `kind`
  into `ibSessionSnapshot` accessors for the Active Users dialog.
- `signal` column + admin kick/reload dispatcher.
- Remove the `ibSessionScope::Current()` shim after `AppUser()`-style
  built-ins move onto explicit session pointers via
  `ibProcUnit::GetSession()`.

**Related memory notes:** `project_session_registry_refactor.md`,
`project_unified_session_architecture.md`, `project_web_session_bug.md`.

### Metadata reload on designer-deploy — **partial** (polling + evict + banner; no recompile yet)

**Landed (2026-04-20):**
- `ibConnectionPool` (`src/engine/backend/databaseLayer/connectionPool.{h,cpp}`) — bounded clone pool with lazy allocation, shared_ptr custom deleter that re-parks on drop, `ibConnectionScope` RAII guard. Init'd at `CreateFileAppDataEnv` / `CreateServerAppDataEnv` with maxSize=8; Shutdown at `DestroyAppDataEnv`.
- Session heartbeat writer migrated to the pool (`appDataQuery.cpp`: ctor Checkouts, dtor drops → reparks; Close() removed from dtor).
- Metadata watcher in frontend `SessionManager::SweepLoop` (`wfrontend.cpp`): 15s tick, reads `sys_config.file_guid` via pool connection, compares against the guid observed at server start. On change: logs `[metadata] file_guid changed: OLD -> NEW — evicting all user sessions`, bumps `g_metaGeneration`, destroys every user session.
- `g_metaGeneration` surfaced in `/session` JSON (authenticated responses and the unauthenticated stub). Client JS captures `initialMetaGen` on first `/session` read; `showSessionLost` asks `/session` again on 401 and shows `"Конфигурация обновлена. Обновите страницу..."` instead of the generic "Сессия потеряна..." when the gen advanced.

**Landed 2026-04-20:**
- **Server-side recompile after detect.** Watcher now does `CloseDatabase(forceCloseFlag) + LoadDatabase() + RunDatabase()` after evicting sessions. Next login sees a fresh compile. Race-safe without a global lock: evict's `Destroy` joins session workers before reload starts; HTTP handlers without a cookie only touch the 401 stub; heartbeat + watcher pool through their own pool connection and don't touch the metadata tree.

**Still to do:**
- **FB event-based notifier** (`isc_que_events`) replaces polling. Designer's `SaveDatabase` posts `metadata_deployed`; server's per-DB event listener thread receives and triggers the same evict + bump path. Avoids 15s latency and the per-tick pool checkout.
- **SSE / per-session worker usage** of the pool. Today only the heartbeat writer takes a checkout; future streams and per-session script workers should route through the pool too.
- **Per-instance `DetachProcUnit` in `~ibModuleDataObject`** — landed 2026-04-20; Catalog/Document/Form instances drop their entry from the current session's ProcUnit map on destruction so the map no longer accumulates dangling descriptor keys.

### Unregistered control types fail silently during metadata loading

**State:** **No longer a crash.** `StartMainModule: ok` on login against
`fb_test251/sys.fdb`; OnStart script opens two forms; `/session` returns
`tabCount=2, activeTab=1`. But every metadata form with a non-ported
control loses that subtree.

**Trace signature** (per form load, for every unregistered clsid):

```
[LoadChildForm] NewObject returned nullptr for clsid=0x43545f5458544320
    — unregistered control type; skipping subtree
```

**Clsid decoding:** bytes are ASCII, MSB first. `0x43545f5458544320` =
`"CT_TXTC "` (space-padded to 8) = `string_to_clsid("CT_TXTC")` =
`ibValueTextCtrl` (see
`src/engine/frontend/visualView/ctrl/textctrl.cpp`:
`CONTROL_TYPE_REGISTER(ibValueTextCtrl, "Textctrl", "Widget",
string_to_clsid("CT_TXTC"))`). Python quick-decode:
`bytes.fromhex('%x' % clsid).decode('latin1').rstrip()`.

**Root cause:** `visualView/ctrl/textctrl.cpp` (and siblings —
`textctrlEvent.cpp`, `textctrlProperty.cpp`, `textctrl_res.cpp`) are in
`frontend.vcxproj` but **not** in `wfrontend.vcxproj`. The file that
houses `CONTROL_TYPE_REGISTER` isn't compiled into wfrontend.dll, so the
static ctor never runs and `NewObject(clsid)` returns nullptr.

**Fix** — bulk stub-first port (see `conventions.md` → "Stub-first
control registration"). Don't wait until each control is fully rendered
on the web:

1. Add every outstanding `visualView/ctrl/*.cpp` to
   `src/engine/frontend/wfrontend.vcxproj`, including companion files
   (`*Event.cpp`, `*Property.cpp`, `*_res.cpp`).
2. Guard any wx-only code paths with `OES_USE_WEB` so the files compile
   in the web build. `Create(ibFrontendWindow*, ibVisualHostClient*)`
   returns a stub object (`new wxObject` / minimal `ibWebWindow`) for
   the not-yet-implemented controls.
3. `CONTROL_TYPE_REGISTER` statics fire automatically → `NewObject` no
   longer hits nullptr → metadata forms render as far as the ported
   controls allow and placeholder everything else.
4. Verify via `/diag/ctors` that all clsids appear. The
   `[LoadChildForm] NewObject returned nullptr` log lines should go
   silent for the configurations under test.
5. Promote stubs to real `ibWeb<Control>` + JS renderer one control at
   a time as UI work progresses.

Priority order for **promoting stubs → real renderers**: textctrl,
checkbox, combobox, choice, listbox, radiobutton, notebook, tablebox /
gridbox.

**Subordinate controls** (`S_CONTROL_TYPE_REGISTER` — see
`conventions.md`): SizerItem, NotebookPage, TableboxColumn, Tool /
ToolSeparator, ClientForm. Their `*.cpp` also needs to be in
`wfrontend.vcxproj` so registration statics fire, but the
rendering/creation code path goes through the owning parent, not
`NewObject`, so a generic `new wxObject` stub in `Create(...)` won't
necessarily match the call shape — port each one alongside its parent
control (notebook + notebookPage together; tablebox + column together).

**Cross-platform guarantee:** because clsids are shared between desktop
and web (see `conventions.md` → "Shared clsids across desktop and web"),
every new control added to `frontend.dll` becomes automatically
available in the web runtime once its source file is added to
`wfrontend.vcxproj` — no metadata migration, no per-platform translation
layer, Designer-saved forms open unchanged on both sides.

**Note:** the null-check in `formMem.cpp::LoadChildForm` stays. It's a
correctness fix regardless (desktop assert alone wasn't enough — the
deref happened anyway once asserts are disabled), and lets partial
subtrees render while controls are ported incrementally.

### Remaining desktop-only singletons in script call path

Currently **no observed issue** — `StartMainModule` completes cleanly.
When the OnStart script of a more-involved configuration is tested, any
remaining desktop-only singleton dereferences (doc manager, wxTheApp
accessors reachable from scripts) will surface as SEH traces via the
same `StartMainModuleSafe` wrapper. Leave the SEH infrastructure in
place until a few configurations have been exercised.

### Controls still to port

Priority order by apparent use:

- `textctrl` (incl. passwordMode)
- `checkbox`
- `combobox` / `choice`
- `listbox`
- `radiobutton`
- `notebook`
- `tablebox` / gridbox (non-trivial — model-driven)
- `frame` children of the above

`webStubs.cpp` has the running list of desktop-only stubs that need real
web implementations as they become reachable.

### Root sizer + SetOrientation on web — **fixed**

Desktop's `CreateVisualHost` seeds the host's background window with
a `new wxBoxSizer(wxVERTICAL)` so the host always has a root sizer
that `SetOrientation` can mutate (governs overall stacking axis for
all child elements). Web lacked this — `SetOrientation` on a form
without an explicit top-level sizer was a no-op because
`GetSizer()` returned null.

Fix in `visualHost.cpp::CreateVisualHost`: when no sizer is present,
seed a `new ibWebBoxSizer(wxVERTICAL)` on the host before the walker
runs. Top-level WINDOW children still go through `SetParent(this)`
(walker's preferred branch). A top-level SIZER child still replaces
the seed via `parentWindow->SetSizer(siz)` — the existing behaviour.
Net effect: `SetOrientation` always has a target, nothing else
changes about layout flow.

**Subsidiary fix (exposed by the above):** `ibWebWindow::SetSizer`
had a re-entrant double-delete. `delete m_sizer` calls the sizer's
dtor; `~ibWebSizer` notifies its owner via
`parentWindow->GetSizer() == this → SetSizer(nullptr)`; that
re-entry hit `delete m_sizer` *again* on the same pointer (crash).
Changed to swap-before-delete: null-out `m_sizer` first, delete the
captured old pointer second — so the recursive `SetSizer(nullptr)`
sees `m_sizer == nullptr` and short-circuits. Latent bug, surfaced
now because the seeded root sizer gets replaced mid-walk whenever a
form has a real top-level sizer as its first child.

### Network-status indicator (top-right) — **done**

Small banner in the top-right corner of the page:

- **Loading** (spinner + "Загрузка…") — shown when at least one
  request has been in flight > 500ms. Sub-second fetches stay
  invisible so every click doesn't flicker the banner.
- **Error** ("Нет связи с сервером") — sticky until the next
  successful fetch clears it. Set by any `fetch` that throws or
  returns `!r.ok`.

Implementation: thin `netFetch(url, opts)` wrapper around `fetch`
in `webClient.cpp` tracks an `inflight` counter + `lastError`
flag. All 14 client-side call sites (login, forms, session, tab,
action, change, toggle, active, poll) go through it — one chokepoint
for status updates. The 2-second polling loop drives the banner
under sustained network failures: each failed poll renews the
error state, the next successful one hides the banner.

### Initial page load shows empty body — **fixed**

Symptom: after login the sidebar, tab strip and status line rendered,
but the main body stayed on "Pick a form on the left" until the user
clicked a tab. OnStart had already auto-opened two tabs — the server
knew about them, `/session` reported them — yet the body was blank.

Root cause: OnStart-opened tabs attach their host to the frame
*before* the control-tree walker runs. A GET `/active` at that point
serialises `ibWebWindow::ToJSON` for a host that still has no
children (the walker only runs when explicitly poked). The poll loop
sees `{}` / bare host and skips — so nothing paints until something
forces a rebuild.

Fix on the client: after `login()` + `refreshTabs()`, call
`paintActiveTab()` which POSTs `/tab/<activeTab>` once. That endpoint
runs `host->CreateAndUpdateVisualHost()` on the server side, returns
the freshly-walked tree, and the body paints on first load. The
polling loop takes over from the next tick onward. No-op if the
session opens with zero tabs.

The server's `/active` endpoint is still intentionally read-only
(heartbeat semantics — no rebuild, no side effects); first-render
uses `/tab/<n>` precisely because it needs a walk.

### Windows "localhost" IPv6-first DNS adds ~200ms per TCP connect — **worked around**

**Symptom:** every HTTP request from a browser pointed at
`http://localhost:8080/...` took ~230ms to connect (plus the ~2ms
actual request processing). Tab switching, clicks, and polling all
stacked this latency. Accessing the same endpoint via
`http://127.0.0.1:8080/...` dropped connect time to ~1.4ms.

**Root cause:** Windows resolves `localhost` via its hosts file to
BOTH `::1` (IPv6) and `127.0.0.1` (IPv4). Browsers/HTTP clients
Happy-Eyeballs the IPv6 address first; when our `cpp-httplib`
server binds on `0.0.0.0` (IPv4 only), the IPv6 connect attempt
waits for a Windows-specific fallback timer (~200ms) before
retrying IPv4. Every new TCP connection pays this tax, and with
keep-alive disabled (see separate workaround), every single
request does a new connect.

**Workaround (in `wenterprise-server/main.cpp` GET `/` handler):**
server checks the `Host:` header and issues a `302` redirect to
the same URL with `localhost` swapped for `127.0.0.1`. Browser
follows the redirect once; subsequent page resources, XHR and
polling all flow through the IPv4 literal from then on.

```cpp
if (host starts with "localhost") {
    res.status = 302;
    res.set_header("Location", "http://127.0.0.1" + rest + prefix + "/");
    return;
}
```

**Permanent fix (future):** bind the HTTP server on `::` (dual-stack
IPv6 + IPv4) so the IPv6 connect attempt succeeds directly,
obviating the redirect. Requires cpp-httplib API support for a v6
bind and some sanity around the user-facing URL style.

### cpp-httplib keep-alive stall on Windows — **worked around**

**Symptom:** browser-side polling of `GET /active` (2s interval)
occasionally stalled for exactly 5s — later 30s after the timeouts
were bumped — hanging the spinner. curl showed fast responses, so the
issue was specific to keep-alive connection reuse.

**Root cause:** cpp-httplib serves each accepted TCP connection from
a single worker thread that loops on `select()` with read_timeout
between keep-alive requests. On Windows, when the browser sends a
second request on that same connection, the server-side `select()`
apparently doesn't wake up reliably — it sits waiting for the full
read_timeout before returning, and the browser's fetch completes only
after the whole timeout elapses. The timing (exactly equal to
read_timeout) is what gave it away.

**Workaround (in `wenterprise-server/main.cpp`):**

```cpp
svr.set_keep_alive_max_count(1);  // one request per TCP connection
svr.set_read_timeout(5);
svr.set_write_timeout(5);
```

Each browser request now opens a fresh TCP socket. Windows loopback
cold-connect is ~200ms (IPv4/IPv6 fallback quirk), but that's a
bounded, consistent cost — no more multi-second stalls.

**Permanent fix (future):** patch cpp-httplib's keep-alive loop to
use a notification-driven approach (or event-based) instead of
blocking `select()` with a long timeout on Windows. Until then, this
workaround is load-bearing — don't re-enable keep-alive without a
fix in place.

### Click-dispatch id mismatch — **fixed**

Symptom: tool / button clicks came back HTTP 200 but scripts didn't
fire. Root cause: metadata could load a control with
`ibValueFrame::m_controlId == 0`. `Create()` then built
`ibWebXxx(GetControlID())`, and `ibWebWindow`'s own auto-id pool
(starts at 1'000'000) stepped in. The browser saw `node.id =
1000000+`; server's `Dispatch` did `form->FindControlByID(controlId)`
which searches by `ibValueFrame::m_controlId` → miss → early return.

Fix: `visualHost.cpp::AppendChildControls` now calls
`child->GenerateNewID()` *before* `child->Create(...)` whenever the
frame's id is still 0. Metadata-saved ids keep their values;
0-loaded controls get freshly minted ids from the form-scoped
counter, and those are what `ibWebWindow` adopts and the browser
sees. Round-trip matches, `FindControlByID` hits, `OnTool` /
`OnButtonPressed` / `OnChange` run.

Verified against `fb_test251` on 2026-04-18: POST
`/action/<toolId>` returns the rebuilt form tree with no error
field; previously the response was `{}` (silent miss).

### Unified lifecycle surface across builds — **done**

Per-build `#ifdef` skew on the host API is gone for the main
lifecycle verbs. `visualHost.h` now declares the same set of
methods on both builds; desktop bodies do the wx-tree work, web
bodies either forward through the walker or are no-ops that let
the next HTTP response rebuild pick up the change:

- `Create / OnCreated / OnSelected / Update / OnUpdated / Cleanup`
  — thin forwarders in `visualHostEvent.cpp` (now compiled into
  both `frontend.vcxproj` and `wfrontend.vcxproj`). Parent type is
  `ibFrontendWindow*` (tyedef → `wxWindow*` desktop,
  `ibWebWindow*` web).
- `CreateControl / UpdateControl / RemoveControl / ClearControl` —
  desktop does the incremental wx-edit path; web is a no-op. Caller
  guards (`#ifndef OES_USE_WEB`) removed from `formObject.cpp`;
  the calls compile identically now.
- `GetParentBackgroundWindow / GetBackgroundWindow` — unified to
  return `ibFrontendWindow*`. `ibVisualHostClient` returns `this`
  on both builds (desktop: `wxWindow*`, web: `ibWebWindow*`).
- `ShowForm / ActivateForm / UpdateForm / CloseForm` — now one
  inline null-guarded body per build. `visualHostClient_impl.cpp`
  emptied (file retained in vcxproj to avoid build-config churn).
- `SetCaption / SetOrientation` on web are real now:
  SetCaption pushes to the owning `ibWebDocChildFrame::SetTitle`
  (surfaces in `/session`'s `formName`); SetOrientation mutates
  the root `ibWebBoxSizer`'s orientation so the client's rebuild
  lays the group out on the correct axis.

Still desktop-only (internal walker plumbing, wx-specific):
`GenerateControl`, `RefreshControl`, `ResolveControlContext`,
`CalculateLabelSize`, `UpdateVirtualSize`, `UpdateHostSize`.
Unifying those would require merging `AppendChildControls` (web
walker) with desktop's `GenerateControl/RefreshControl` path —
tracked as a separate refactor, not currently in flight.

### Client asset sharing across entry points — **done**

The single-page HTML/CSS/JS client now lives in
`src/engine/frontend/web/webClient.cpp` (in `wfrontend.dll`) and is reached
via `extern "C" WFRONTEND_API const char* wfrontendClientHTML()`
(declared in `wfrontend.h`).

`wenterprise-server`'s GET `/` handler calls `wfrontendClientHTML()`;
future Apache/IIS/CGI hosts reuse the same bytes via the same export.
Changes to CSS/JS/renderers touch `webClient.cpp` only — the host
binaries don't need rebuilds beyond a relink against the updated DLL.

MSVC's ~16KB per-raw-string-literal cap still applies inside
`webClient.cpp`; the `</style></head>` split via adjacent string
concatenation (C++ standard) is retained so the blob can keep
growing without further surgery.

### Toolbar port — iteration 2+ (tool captions)

Tool buttons now display their action-collection captions
("Close", "Update", "Help", "Change form" for a generic form).
Resolution chain in `ibValueToolBarItem::Create` web branch:

1. `GetItemCaption(owner->GetActionArray())` — owner's action
   collection keyed by the tool's system-action id.
2. `m_propertyTitle` — Designer-set per-tool title, if any.
3. `GetControlName()` — internal fallback so a truly unlabelled
   tool is at least addressable in the DOM ("MainToolbarClose").

The collection is populated in `ibValueToolbar::Create` web branch
via `sourceElement->GetActionCollection(typeForm)`. Source
resolution: prefer saved `m_actSource`, fall back to `GetOwnerForm()`
when the property is `wxNOT_FOUND` — test forms saved by older
Designer versions skip the SetActionSrc call (desktop's BuildForm
sets it explicitly at auto-build time; web never runs BuildForm).

### Toolbar port — iteration 2 landed

Tool items + separators render on web. `CT_TLITM` and `CT_TLSP` are
registered; forms with a toolbar now show the full tool chain, not
just an empty toolbar block.

What landed on top of iteration 1:
- `ibWebToolBarItem` + `ibWebToolBarSeparator` in `webWindow.h` —
  render shims. Tool item has a `FireClick()` that emits `wxEVT_TOOL`
  and a `HandleRequest("click")` override; separator is a marker
  node only.
- `ibValueToolBarItem::Create` + `ibValueToolBarSeparator::Create`
  overrides (added to `toolBar.h`, implemented in `toolBarItem.cpp`).
  Web returns the matching ibWebToolBar* shim; desktop falls back to
  `new ibNoObject` to preserve the existing AddTool-via-OnCreated
  flow (the desktop path never actually Create'd a wx widget — the
  toolbar's OnCreated on the parent takes the SetUserData key).
- `toolBarItem.cpp` — signatures changed from `wxWindow*` to
  `ibFrontendWindow*` (desktop's typedef, web's ibWebWindow). Bodies
  guarded under `#ifndef OES_USE_WEB`; web sides are no-ops because
  the walker's `COMPONENT_TYPE_ABSTRACT` branch already produces a
  live web node via Create.
- `visualHost.cpp` walker — added `COMPONENT_TYPE_ABSTRACT` to the
  window case (toolbar items are ABSTRACT on desktop but render as
  web windows on web). static_cast still applies.
- `webToolbar.cpp` — new `ibValueToolBarItem::GetToolAction` stub
  (pointer-to-member referenced by the Action property initializer;
  linker needs the symbol even though the web build never exercises
  the desktop-only action-source walker).
- `wfrontend.vcxproj` — `toolBarItem.cpp` + `toolBarItem_res.cpp`
  added.

Browser JS renderers for `"tool"` / `"toolseparator"` are still the
fallback box render — tools show as a labelled placeholder. Follow-up:
proper JS renderer per type + wire `/action/<id>` click on tool.

Still deferred (companion source files not in vcxproj):
- `toolBarEvent.cpp` — wx native event routing.
- `toolBarProperty.cpp` — designer-only property handlers.
- `toolBarMenu.cpp` — wx context menu.
- `toolBarAction.cpp` — action-source list walker (designer).
- `toolBarItemAction.cpp` — stubbed via `webToolbar.cpp`.

### Toolbar port — iteration 1 landed (stub companions deferred)

**State:** `CT_TLBR` now registers. Forms with a toolbar render an
empty `ibWebToolbar` block — the toolbar node itself renders,
children (CT_TLITM / CT_TLSP) still log "unregistered clsid"
warnings until the companion files land.

What landed:
- `ibWebToolbar : ibWebWindow` in `webWindow.h` — render shim,
  emits `"type":"toolbar"`.
- `toolBar.cpp` web `Create` returns `new ibWebToolbar(EnsureControlID())`
  instead of the old `ibWebStubControl(wxT("toolbar"))`.
- `toolBar.h` — `#include "frontend/win/ctrls/toolBar.h"` and the
  `OnToolDropDown(wxAuiToolBarEvent&)` decl guarded under
  `#ifndef OES_USE_WEB` so the header compiles on web without
  pulling in `<wx/aui/auibar.h>`.
- `webToolbar.cpp` (new, web-only) — empty stubs for
  `OnPropertyCreated / OnPropertySelected / OnPropertyChanged /
  PrepareDefaultMenu / ExecuteMenu / GetActionSource` so the vtable
  links without touching the desktop companion sources.
- `toolBar.cpp` `AddToolItem / AddToolSeparator` guarded under
  `#ifndef OES_USE_WEB` (desktop uses `g_visualHostContext` which
  is designer-only).
- `toolBar.cpp` + `toolBar_res.cpp` added to `wfrontend.vcxproj`.

Deferred companions (still `<!-- -->` in vcxproj):
- `toolBarEvent.cpp` — wxAuiToolBar event routing.
- `toolBarProperty.cpp` — uses `g_visualHostContext->CutControl/
  InsertControl/RefreshEditor`. Designer-only paths.
- `toolBarMenu.cpp` — wxMenu context menu.
- `toolBarAction.cpp` — uses `ibPropertyList` action source walker;
  needs re-examination on web.
- `toolBarItem.cpp`, `toolBarItem_res.cpp`, `toolBarItemAction.cpp` —
  would register `CT_TLITM` / `CT_TLSP`. `toolBarItem.cpp` uses
  `ibAuiToolBar::InsertSeparator(int, id)` and accesses
  `ibValueFrame::m_controlId` directly (pre-existing suspect code).

**Next pass:** add an `ibWebToolBarItem` render shim + port
`toolBarItem.cpp` through similar ifdef surgery (or a parallel web
stub file). Tool events hook into the dispatcher via the standard
HandleRequest pattern once items have runtime ids.

### Toolbar port — needs broader ifdef coverage

**State:** `toolBar.cpp` has had its `Create/Update/Cleanup` bodies
ifdef'd and returns an `ibWebStubControl(wxT("toolbar"))` for the web
path. But the companion files it depends on for the linker
(`toolBarEvent.cpp`, `toolBarProperty.cpp`, `toolBarMenu.cpp`,
`toolBarAction.cpp`) reference Designer-only globals that aren't
defined in the web build:

- `g_visualHostContext` — Designer's active visual host pointer.
- `ibFrontendVisualEditorNotebook` — Designer's editor notebook type.
- `ibAuiToolBar::InsertSeparator(int)` — overload only on desktop.
- `ibVisualHost::GetWxObject()` — desktop-only accessor.
- Static access to `ibValueFrame::m_controlId` in `toolBarItem.cpp`
  (looks like a pre-existing bug that only the desktop linker tolerates
  due to some inlining quirk).

Sources have been **kept out of `wfrontend.vcxproj`** (see commented
lines around `<!-- Toolbar port deferred -->`). Result: the
`CT_TLBR` / `CT_TLITM` / `CT_TLSP` clsids log the `[LoadChildForm]`
skip-subtree warning and the toolbar doesn't render, but the rest of
the form loads cleanly.

**Fix plan** (next session):

1. Wrap every `g_visualHostContext` reference in
   `toolBar{Event,Property,Menu,Action}.cpp` with
   `#ifndef OES_USE_WEB`.
2. Same for `ibFrontendVisualEditorNotebook` inside `toolBarEvent.cpp`.
3. Add `toolBarItem.cpp` with the `m_controlId` static access converted
   to `this->m_controlId` (the static access is genuinely wrong — look
   at git blame before changing in case it's a conscious trick).
4. If `ibAuiToolBar::InsertSeparator(int)` is only referenced from
   desktop event paths, the whole method stays in the `#else` branch
   — no shim needed on the web side.
5. Add the files to `wfrontend.vcxproj`, build, verify `CT_TLBR` shows
   in `/diag/ctors` and the `[LoadChildForm]` warning goes away.

## Resolved

### Session + tab lifecycle on F5 / close (2026-04-19)

Browser reload and tab-close semantics are now stable. Notes for future
sessions picking this up:

- **F5 reuses the session** by default (cookie → `wfrontendSessionExists`
  → reuse). Destroying on every reload races with the parallel polls
  the browser fires against the old cookie (`/active`, `/session` can
  land after GET / processed Set-Cookie but before the browser applied
  it), which used to surface as "Pick a form on the left" flashing.
- **Exception**: when the reused session has `TabCount() == 0`, GET /
  destroys + creates so `OnStart` re-runs and the default forms
  re-populate — otherwise the placeholder would be sticky.
  (`wfrontendSessionTabCount` is the new API for that check.)
- **Orphan sessions** from actual tab-close are **not** reaped yet —
  TODO idle-timeout eviction in SessionManager (see "Session idle
  timeout" below).
- **DELETE /tab/<i> index drift** — client now re-derives the index
  from `Array.from(tabsBar.children).indexOf(el)` at click time.
  Captured-in-forEach `i` goes stale after the first close since
  `m_tabs.erase` shifts later indices down by one on the server;
  symptom was "close all tabs, F5 shows a ghost tab". See
  `feedback_stale_indices.md` in the memory system for the full
  writeup.
- **`~ibWebFrame` cleanup** — OnExit closes every tab via
  `RunOnWorker([]{ DeleteAllViews }).get()` BEFORE `StopWorker` so
  script events (beforeClose/onClose from view->Close →
  `CloseDocForm`) run on the worker thread where `ibProcUnit`'s
  thread-local state is valid. Running them on the HTTP thread crashed
  the server. `~ibWebFrame` itself just `m_tabs.clear()` — it must not
  dereference dangling doc/view pointers left after the worker-side
  DeleteAllViews cascade.
- **`OnSaveModified` override on web** — `ibFormVisualDocument::
  OnSaveModified()` returns true unconditionally on web. Default
  `wxDocument::OnSaveModified` pops a `wxMessageDialog`; without a wx
  event loop it blocks / returns garbage and `OnChangedViewList` skips
  its `delete this`, leaking the doc (and its `ibValuePtr<ibValueForm>`
  → form never destructs either).
- **Tab title fallback chain** — at `CreateChildFrame` time the doc's
  title is still empty (set later by `ibFormVisualDocument::OnCreate`),
  so web pulls the title from `valueForm->GetControlTitle()` directly,
  then from `metaForm->GetName()` if that's empty, finally `"#N"`.
  Avoids the `#0 / #1 / #2` flash on first serialize.
- **Tab icons** — `ibWebMDIChildFrame::SetIcon(const wxIcon&)` stores
  the form's icon; `GET /tab/<i>/icon` encodes it as PNG via
  `wxImage::SaveFile(… wxBITMAP_TYPE_PNG)` and serves with `Cache-Control:
  max-age=300`. `/session` JSON gains `hasIcon` and `modified` flags;
  client renders icon on the left of the tab title and applies bold
  + leading `*` when modified, bold when active.
- **Worker-thread debug server pin** — `ibWebApplication::WorkerLoop`
  calls `ibDebuggerServer::SetCurrent(m_debugServer)` at the top so
  `procUnit`'s breakpoint hook routes to THIS session's listener
  rather than the singleton fallback. `SetCurrent` + `CurrentSlot`
  are out-of-line in `debugServer.cpp` (always emitted; desktop build
  just never calls them) so `wfrontend.dll` can link against
  `backend.dll` regardless of `OES_USE_WEB`.
- **`eWEB_RUNTIME_MODE = 5`** added to `ibRunMode`. `EnterpriseMode()`
  returns true for both thick and web; new `WebEnterpriseMode()`
  separates them when needed. `wfrontend.cpp` passes it to
  `appDataCreateFile/Server`.
- **UTF-8 argv on Windows** — `wenterprise-server/main.cpp` converts
  `GetCommandLineW()` output to UTF-8 via `WideCharToMultiByte` before
  parsing. Without this, Cyrillic `--file=` / `--url=` paths arrive
  as CP1251 and `wxString::FromUTF8` silently empties them.
- **Launcher / Designer launch paths** — launcher's Web button + the
  new Designer menu "Start debugging ▸ Web client" both spawn
  `wenterprise-server.exe` through `cmd /C "<exe> > <tempfile> 2>&1"`
  so the server's stdout/stderr land in a tail-able temp log (launcher
  and designer are GUI apps, no inherited console). Launcher prints
  the log path via `wxMessageOutput::Get()->Printf`. Path template:
  `%TEMP%\wenterprise-server-*.log`.

### Session idle timeout — TODO

Orphaned sessions (user closes the browser tab without a `/logout`)
stay in `SessionManager::m_sessions` until process exit. Their runtime
+ forms + `ibValue`s accumulate. Plan:

- Add `ibWebSession::LastActiveMs()` bump on every HTTP handler hit
  (the `Touch()` method already exists — just wire it into the
  routes).
- Background sweep thread in `SessionManager` (or a tick on
  `ibWebTimer`) that iterates `m_sessions` every ~30s and
  `Destroy`s any session where `now - LastActiveMs > N*60*1000`
  (N=30 as a first pick).
- Skip the sweep for the currently-held session (in-progress request
  serialisation). The per-request `Find` already hands out a raw
  pointer — widen to shared_ptr if the sweep can race an active
  handler; otherwise the sweep must take the same mutex as Find and
  verify the session isn't being used.

### Per-session appData state — DONE (2026-04-20)

The previous "ibWebSession::Login mutates singleton appData state →
multi-tab last-login-wins" issue is closed by the session-registry
refactor. Concrete changes that landed:

- `AuthenticationAndSetUser` split into pure `AuthenticateUser(user,
  pwd, outInfo)` (verifier) plus `InstallUser(info, rawPassword)`
  (side-effect writer that targets `ibSession::Current()`).
- User-identity fields (`m_userInfo`, `m_sessionRawPassword`,
  `m_workDate`) live on `ibSession`. The legacy `m_sessionUpdater` /
  `m_sessionGuid` singletons on `ibApplicationData` are gone — session
  guid lives in `ibSessionIdentity::m_guid`, the heartbeat is run by
  `ibSessionRegistry`'s registry thread.
- `ibWebSession::Login` goes through the registry's anonymous-create
  plus `m_session->Open(user, pwd)` (no ticket); `sys_session` sees one
  row per tab through the registry's INSERT.
- Singleton accessors on `ibApplicationData` (`GetUserName`,
  `GetUserInfo`, `GetUserPassword`, …) forward to
  `ibSession::Current()->GetUserInfo()` when a scope is active, so
  25+ legacy callers picked up per-tab behaviour automatically.

Memory note `project_web_session_bug.md` and
[`../session-registry.md`](../session-registry.md) carry the full
post-mortem.

### Web MDI refactor to `ibWebDocParent/ChildFrameAny<Base>` — TODO

User pointed to `wxDocParentFrameAny<BaseFrame>` / `wxDocChildFrameAny`
as the pattern our web MDI should follow. Current `ibWebFrame` +
`ibWebDocChildFrame` are concrete classes, which locks us into tabs as
the only layout. The templates compose doc/view behaviour onto any
base window so future tabular/text/report browsers can reuse the same
integration on their own base. Full writeup: `project_doc_frame_any.md`
in memory.

### Designer submenu cloning into child frames — landed (2026-04-19)

`ibAuiDocChildFrame::SetMenuBar` (in
`src/engine/frontend/mainFrame/mainFrameChild.cpp`) rebuilds the parent
menu bar for each MDI child (e.g. form editor). Its `ibProcSubMenu::
ConstructMenu` walker used to call `src->Append(id, label, …)` then
`menuItem->SetSubMenu(subMenu)` for submenu items — on MSW that leaves
the submenu orphaned and the child's menu bar loses anything under a
`AppendSubMenu` parent (e.g. "Start debugging ▸ Web client" vanished as
soon as the user opened a form editor). Fixed by routing submenu items
through `src->AppendSubMenu(subMenu, label, help)` and separators
through `src->AppendSeparator()`.

If you add a new AppendSubMenu anywhere in the Designer menu bar, verify
it survives a child-frame menu re-build (open a form → "Debug" menu
should still show the submenu intact). The copy walker is the single
bottleneck — everything goes through it.

### Tab strip — stale client index on close — landed (2026-04-19)

`DELETE /tab/<i>` handlers on the web client used a captured forEach
`i` inside `close.onclick`. After the first close, `m_tabs.erase`
shifted subsequent server indices down by one, but the captured `i`
stayed frozen → second close targeted a non-existent slot, server
returned "{}" silently, tab survived. Symptom: close all tabs, F5
resurrects a "ghost" one. Fixed by re-resolving the index at click
time via `Array.from(tabsBar.children).indexOf(el)` and calling
`refreshTabs()` after the delete completes. See
`feedback_stale_indices.md` in memory for the general lesson.

### F5 on empty session → re-run OnStart — landed (2026-04-19)

GET / keeps the session on reload when the cookie is still valid,
EXCEPT when that session has zero tabs. In the empty case it destroys
+ creates a fresh session so OnStart re-opens the default forms.
Without this, after the user closed every tab, the next F5 showed the
placeholder forever. `wfrontendSessionTabCount` is the new API for
the check (wfrontend.h).

### Session idle-timeout sweep — landed (2026-04-19)

`SessionManager` now spawns a background sweep thread in its ctor
(`SweepLoop`) that wakes every 60s and evicts any session whose
`LastActiveMs` is older than 30min. `Touch(id)` is called at the top
of every `wfrontend*` API entry point (/active, /session, /fire*,
/tab/*, etc.) so an actively-polling client keeps its session alive.
Dtor signals stop via `m_sweepStop` + cv and joins.

If extending this, keep the sweep's map-level work under `m_mutex`
(collect victim ids) and do the actual `Destroy` calls OUTSIDE the
lock — `OnExit` runs scripts + teardown that otherwise would
serialise every other handler.

### Tab icons + active/modified visual state — landed (2026-04-19)

- `ibWebMDIChildFrame::SetIcon/GetIcon` stores `wxIcon` per tab;
  `GET /tab/<i>/icon` encodes as PNG via
  `wxImage::SaveFile(…, wxBITMAP_TYPE_PNG)` and serves with
  `Cache-Control: max-age=300`.
- `ibWebFrame::SetIcon/GetIcon` for app-level icon (future favicon
  endpoint).
- `/session` JSON gains `hasIcon` + `modified`.
- `webClient.cpp` CSS: icon on the LEFT of the title (row flex);
  active tab bold; modified = bold + leading `*` (matches MDI
  convention). Close button is a centred 16×16 `×` that turns red
  on hover.
- Tab title fallback chain in `webFrame.cpp::CreateChildFrame`:
  `doc->GetTitle()` → `valueForm->GetControlTitle()` →
  `metaForm->GetName()` (extended fallback in `/session` serialiser
  too). Prevents the "#0/#1" flash on first serialize.

### Per-session debug server — landed (2026-04-19)

`ibWebSession::OnInit` creates an `ibDebuggerServer` per session and
hands it to `ibWebApplication::SetDebugServer`. The app's
`WorkerLoop` calls `ibDebuggerServer::SetCurrent(m_debugServer)` at
the top so `ibProcUnit`'s breakpoint hook resolves to THIS session's
listener rather than the singleton fallback. `SetCurrent` and
`CurrentSlot` are out-of-line in `debugServer.cpp` and always
emitted (no `#ifdef OES_USE_WEB` guard) so wfrontend.dll can link
against a single backend.dll build regardless of which frontend the
designer launches.

### Debug counter for `ibValue` (cross-thread aware) — landed (2026-04-19)

`src/engine/backend/compiler/value.cpp`: `s_nCreateCount` is now
`std::atomic<unsigned int>` so concurrent Create/Delete on the HTTP
and worker threads don't race. `DEBUG_VALUE_CREATE/DELETE` both print
`Create <n> tid=<id>` / `Delete <n> tid=<id>` via `wxLogDebug` —
cross-platform (MSW → OutputDebugString visible in VS Output pane;
GTK → stderr; OSX → NSLog). No stderr duplication now that wxLog's
default sink also lands on stderr when no wxApp is attached.

---

- `/bigobj` missing on `commonObject.cpp` — added globally to
  `backend.vcxproj`.
- `ms_mainFrame == nullptr` assert when two concurrent sessions created
  frames — fixed by removing the backend-side frame singleton entirely.
  Frame is now owned by the `ibSession` that created it; backend code
  reaches it through `ibSession::Current()->GetFrame()` (or the
  convenience `ibSession::CurrentFrame()`). Per-thread session
  bindings for the web build use `AccessMode::Shared` with
  `ibSessionScope` / `ibSessionThreadBinding` plus a process-wide
  fallback for the `WebServer` system session.
- `m_moduleManager` raw pointer + manual IncrRef/DecrRef —
  replaced by `ibValuePtr<ibValueModuleManagerConfiguration>` in
  `webApplication.{h,cpp}`.
- `StartMainModule` crash was unobservable under `/EHsc` —
  `StartMainModuleSafe` SEH wrapper with dbghelp symbolication now
  prints module+RVA+symbol+file:line.
- "localhost works, 0.0.0.0 doesn't" display — server log rewrites the
  bind host to `localhost` for the printed URL when binding to
  `0.0.0.0` / `::`.
- `LoadChildForm` crash on unregistered clsid — null-check added with
  trace log; unblocks further prototype work while controls are ported.
- OnStart veto semantics — `StartMainModuleSafe` returning false tears
  down `m_moduleManager` (via `ibValuePtr::Reset`) and frame, then
  returns false from `ibWebApplication::OnInit`, which surfaces as
  `LOGIN=401`.
- `textctrl` (clsid `CT_TXTC`) ported end-to-end — `textctrl.cpp` +
  `textctrlEvent.cpp` + `textctrlProperty.cpp` + `textctrl_res.cpp`
  added to `wfrontend.vcxproj`, `Create()` returns `new ibWebTextCtrl`
  with value / password-mode / multiline-mode populated from
  properties. Pattern for the rest of the controls lives here.
- `ibWebStubControl(type)` — new placeholder web node type in
  `webWindow.h` carrying a type string; use it as the `Create()`
  return for controls whose rendering isn't implemented yet.
- Client-side polling delivery landed — `GET /active` returns the
  active tab's visual-host JSON without side effects (no tree
  rebuild, no worker dispatch). Browser runs `setInterval(2s)`
  against it, diffs by JSON string, replaces DOM only on change.
  `document.visibilitychange` pauses polling when the tab is
  hidden. Together with the worker-thread dispatch (2d), timer
  ticks now surface in the UI within ~2 s of firing without
  requiring user action — first working "live" update path on
  web. SSE / long-poll can swap in later if 2s polling becomes
  too chatty.
- Event dispatcher iteration 2d (per-session worker thread) — the
  session now owns a dedicated worker thread that is the sole
  script-runner. Two event sources — HTTP handlers and timer ticks
  — funnel through one transport: HTTP shims wrap their Dispatch
  call in `app->RunOnWorker(lambda).get()` (blocking submit +
  future-wait); timer ticks call `app->QueueEvent(new wxTimerEvent)`
  + `app->PostWork([app]{ app->ProcessPendingEvents(); })` so the
  drain happens on the worker. `ibWebTimer` is now std::thread-based
  (native wxTimer needs a message pump that wfrontend doesn't have).
  Each timer has a unique wxNewId(); `Bind(wxEVT_TIMER,
  &ibWebTimer::OnTimer, timer, id)` in RegisterIdleHandler routes
  ticks to the timer's own handler. `ibProcUnit` is thread-affine
  to the worker — no mutex needed. Worker starts at end of OnInit,
  stops at start of OnExit after timers are stopped + unbound.
  Client-side live-update delivery (SSE / polling / long-poll)
  still orthogonal — server state converges on schedule, browser
  sees it on its next request.
- Event dispatcher iteration 2b refined — `ibWebTimer : wxEvtHandler`
  class in `webTimer.{h,cpp}` encapsulates one (form, procedureName)
  timer: owns its `wxTimer`, binds `wxEVT_TIMER` to its own
  `OnTimer`, fires the script + rebuilds the active host on tick.
  `ibWebApplication` holds `std::vector<std::unique_ptr<ibWebTimer>>`
  — no central dispatch map, no id-to-handler lookup. Mirrors the
  pattern already used for controls (web class handles its own
  events internally). Two event sources — HTTP requests and
  wxTimer ticks — unify into one transport (`QueueEvent` on a
  wxEvtHandler + `ProcessPendingEvents` drain).
  `ibValueModuleManagerConfiguration* GetModuleManager()` moved
  out-of-line (was inline in webApplication.h, tripping an
  `ibValuePtr<T>` static_cast in TUs that forward-declared T only).
- Event dispatcher iteration 2b (server-side timer infrastructure) —
  `ibValueForm::AttachIdleHandler` / `DetachIdleHandler` are no
  longer web-only no-ops. The web branch delegates to
  `ibWebApplication::RegisterIdleHandler / UnregisterIdleHandler`,
  which allocate a `wxTimer` with `SetOwner(this, id)` so
  `wxEVT_TIMER` lands on the session app's handler queue via wx's
  own `QueueEvent` (thread-safe — this is the first real
  cross-thread use in the web runtime). `OnTimer` drains on the
  next `ProcessPendingEvents` call (HTTP handler thread today) and
  invokes the registered procedure on the form's `ibProcUnit`,
  then rebuilds the visual host. `currentWebApp()` free function
  in `webApplication.cpp` reaches the session's app from arbitrary
  script context via the active session's frame
  (`ibSession::CurrentFrame()` → cast to `ibWebFrame*` → stored
  back-pointer `m_app`). `ibWebFrame` ctor now takes
  `ibWebApplication*`. `Bind(wxEVT_TIMER, ...)` wired in
  `OnInit`, unwound in `OnExit` along with timer stop+clear.
  Delivery (SSE / polling / long-poll) still not implemented —
  timer ticks advance server state but the client only sees them
  on its next request. Build passes; existing button / textctrl
  regressions verified clean.
- Event dispatcher iteration 2c (polymorphic dispatch) — dynamic_cast
  cascade on the ibValue-side is gone. Single `ibWebApplication::Dispatch(id, kind, value)`
  resolves the control via `form->FindControlByID(id)` and the paired
  render object via `host->GetWxObject(ctrl)` — reusing the existing
  `m_baseObjects` map un-guarded from `#ifndef OES_USE_WEB`, populated
  by the web walker the same way desktop's `GenerateControl` already
  populates it. One storage for both builds, not a parallel web-only
  map. `dynamic_cast<ibWebWindow*>` is the single type-check point;
  virtual `HandleRequest(kind, value)` dispatches kind-specific
  behaviour to the right `ibWebXxx`. Adding a new control = one
  override + one JS renderer, no dispatcher / HTTP changes. Legacy
  kind-specific `DispatchControlAction / TextChange / Toggle` reduced
  to one-line shims over `Dispatch`. Per-subclass `m_webNode` +
  `GetWebNode` accessors removed. Verified on `fb_test251/CommonForm2`:
  buttons + textctrl still fire HTTP 200 with no SEH regression.
- Event dispatcher iteration 2a (textctrl) — `POST /change/<id>` with
  `value=…` commits textctrl edits. Full pipeline:
  `wfrontendFireTextChange` → `ibWebApplication::DispatchTextChange` →
  `ibWebTextCtrl::FireTextChange(value)` → `wxEVT_TEXT` + `FireEvent`
  → `ibValueTextCtrl::OnWebTextChanged` (mirrors desktop
  `TextProcessing` — type-coerce through metadata, `SetControlValue`,
  fire `OnChange` script). Validated on `fb_test251/CommonForm2`:
  ASCII, Cyrillic UTF-8, persistence across re-fetches, multiple
  textctrls independently. `ResolveControl` helper factored out of
  `DispatchControlAction` — each new Dispatch* reuses it. Choice is
  skipped (not used); checkbox is next.
- Event dispatcher iteration 1 — button click now routes through
  `wxEvtHandler::QueueEvent`/`ProcessPendingEvents` with the real
  `wxEVT_BUTTON` event, so `OnButtonPressed` fires identically to the
  desktop native-click path. `ibValueButton::Create` binds the web
  node on creation; `FireClick` is a 5-line shim that posts the
  event and drains on the HTTP handler thread. `buttonEvent.cpp` is
  now in `wfrontend.vcxproj`. Verified on `fb_test251/CommonForm2`
  (4 buttons fire HTTP 200 with no crashes/SEH). Full plan + what's
  still to do (textctrl/checkbox/choice/combobox, `AttachIdleHandler`,
  async drain) in `docs/web/event-dispatcher.md`.
- Sizer hierarchy split off `ibWebWindow` — `ibWebSizer : public wxObject`
  now lives in `webSizer.{h,cpp}`. The four concrete sizers
  (`ibWebBoxSizer`, `ibWebWrapSizer`, `ibWebStaticBoxSizer`,
  `ibWebGridSizer`) moved out of `webWindow.h`. `ibWebWindow` gained a
  single `ibWebSizer* m_sizer` slot (SetSizer/GetSizer, owned), and the
  walker in `visualHost.cpp` dispatches by runtime type: windows attach
  via `SetParent` to the nearest window ancestor; sizers attach via
  `SetSizer` on a parent window or `Add` on a parent sizer. `AddParams`
  (proportion/flag/border) are captured from `ibValueSizerItem` during
  the walk and passed into the corresponding `Add` call, so per-child
  layout hints reach the JSON as `"layout": {...}`. Verified: login,
  OnStart tab spawn, `/demo` tree all render; no `LoadChildForm nullptr`
  regressions. See `docs/web/conventions.md` → "Sizer hierarchy" for the
  rules that govern future sizer work.
- Tab switching on the web UI — `POST /tab/<i>` (activate) and
  `DELETE /tab/<i>` (close) endpoints added, backed by
  `wfrontendActivateTab` / `wfrontendCloseTab`. `ActivateTabInSession`
  re-walks the host (`CreateAndUpdateVisualHost`) before serialising
  because OnStart-script opened forms only populate the
  `ibValueFrame` tree *after* `CreateNewForm` returns; a one-shot walk
  at create time would have captured the tree empty. Client-side: tab
  label click activates, `×` closes.

## Deferred (not urgent)

- Stack-walking (`StackWalk64`) in the SEH handler — would give the
  full call chain when symbol+line isn't enough. Single-frame resolution
  has sufficed so far.
- Replace the `try/catch(...)` inside `moduleManagerEvent.cpp`
  BeforeStart/OnStart with SEH-aware wrappers in the web build. Right
  now the outer `StartMainModuleSafe` catches everything; no gain yet.
- `wfrontendInitServer` path (TCP Firebird) has never been exercised —
  only `wfrontendInitFile` has real coverage.
