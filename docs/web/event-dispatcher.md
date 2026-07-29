# Event dispatcher — plan

Status: **iterations 1 / 2a / 2b / 2c / 2d landed.** Button, textctrl,
checkbox all flow through the same unified pipeline —
`HTTP /action | /change | /toggle` → thin shim →
`ibWebApplication::Dispatch(id, kind, value)` →
`form->FindControlByID(id)` → `host->GetWxObject(ctrl)` (using the
existing `m_baseObjects` map, shared with desktop — un-guarded from
`#ifndef OES_USE_WEB` and populated by the web walker the same way
desktop's `GenerateControl` populates it) → `dynamic_cast<ibWebWindow*>`
→ virtual `HandleRequest(kind, value)`. Each `ibWebXxx` owns its
kinds; no dispatcher type ladder, no web-only map — one storage for
both builds. Async / timer thread still to do — see "Iteration 2b"
below.

Centralise incoming event handling in the web runtime so that HTTP-borne
events (button click, textctrl change), timers (`AttachIdleHandler`
firings) and future server-push signals all land on a uniform path.
Keep the change small: **reuse real wx event types** (`wxEVT_BUTTON`,
`wxEVT_TIMER`, `wxEVT_TEXT`, ...) rather than introduce a parallel
`ibEVT_WEB_*` scheme. That way any `Bind(wxEVT_XXX, ...)` handler —
present or future — fires identically on desktop and web.

## Why now

Current path (works, but each event kind has its own shortcut):

```
HTTP POST /action/<id>
    → SessionManager::FireAction (wfrontend.cpp:667)
        → FireActionInSession (wfrontend.cpp:627)
            → btn->FireClick()                       ← direct call, no wxEvent
                → CallAsEvent(m_onButtonPressed, …)
```

`AttachIdleHandler` (formObject.cpp:656–689) is an explicit no-op on web
because there's no place for `wxEVT_TIMER` to land. Tab switch already
has its own entry point. Each new event kind ends up bespoke.

## Shape

### Transport

- **Posting events is always `wxEvtHandler::QueueEvent(wxEvent*)`** —
  thread-safe; wx takes ownership of the pointer; wx makes a copy so the
  caller's stack event doesn't dangle. Use this uniformly whether the
  source is the HTTP handler thread (today) or a timer / push thread
  (later).
- **Draining is `ProcessPendingEvents()`** — called on the session
  thread. Today the HTTP handler thread drains once per request, right
  before serialising the form tree into the response. When async lands,
  a per-session worker thread drains in a loop; HTTP requests post and
  block on completion.

### Dispatcher placement

Session-level events (timers, tabs, push) → `ibWebApplication` itself
acts as the handler. It already inherits `wxEvtHandler` and is per-
session. Bind once in `OnInit`; unbind in `OnExit`.

Control-level events (button click, text change, checkbox toggle) →
the control's own `wxEvtHandler` is the target. Mirrors desktop: on
desktop `wxButton` fires `wxEVT_BUTTON` on itself, scripts hook it via
`Bind`. On web the control is an `ibValueButton` (which inherits
`wxEvtHandler`) — same hookup works.

No new `ibWebDispatcher` class needed. `ibWebApplication` is the session
dispatcher; individual controls are their own per-control dispatchers,
exactly as on desktop. One class less to maintain.

### Event categories

The dispatcher transport (`wxEvtHandler::QueueEvent` + `ProcessPendingEvents`)
is event-class-agnostic — `wxEvent::Clone()` makes the queue hold any
wx event type. Practically, four families show up:

| Family | wx class | Examples | Web origin |
|---|---|---|---|
| Command | `wxCommandEvent` | `wxEVT_BUTTON`, `wxEVT_TEXT`, `wxEVT_CHECKBOX`, `wxEVT_CHOICE`, `wxEVT_COMBOBOX`, `wxEVT_MENU` | HTTP POST from browser (explicit user action) |
| Mouse | `wxMouseEvent` | `wxEVT_LEFT_DOWN/UP`, `wxEVT_MOTION`, `wxEVT_RIGHT_DOWN`, `wxEVT_MOUSEWHEEL` | HTTP POST carrying `{x, y, button, mod}` |
| Keyboard | `wxKeyEvent` | `wxEVT_KEY_DOWN/UP`, `wxEVT_CHAR` | HTTP POST carrying `{keyCode, mod, char}` |
| Paint / repaint | n/a on web | — | **Server-initiated**: after any dispatch, the visual host rebuilds the tree and the response body is the new render. No `wxEVT_PAINT` is posted — the browser re-renders from the JSON diff. |

The generic primitive on `ibWebWindow` accepts any of these:

```cpp
bool FireEvent(wxEvent& ev);       // primitive; stamps id + source, QueueEvent + drain
bool FireCommand(wxEventType type); // sugar for wxCommandEvent-typed events
```

Descendants add semantic wrappers per family:

```cpp
// Command-family (wxCommandEvent)
ibWebButton::FireClick()                         → FireCommand(wxEVT_BUTTON)
ibWebTextCtrl::FireTextChange(const wxString& v) → wxCommandEvent; ev.SetString(v); FireEvent(ev)
ibWebCheckBox::FireToggle(bool checked)          → wxCommandEvent; ev.SetInt(checked); FireEvent(ev)

// Mouse-family (wxMouseEvent)
ibWebXxx::FireMouseDown(int x, int y, int btn)   → wxMouseEvent(wxEVT_LEFT_DOWN); ev.m_x = x; ...; FireEvent(ev)

// Key-family (wxKeyEvent)
ibWebXxx::FireKeyDown(int keyCode, int mod)      → wxKeyEvent(wxEVT_KEY_DOWN); ev.m_keyCode = ...; FireEvent(ev)
```

**Repaint is implicit.** On desktop wx invalidates a region → paint event
→ native draw. On web there is no server-side drawing. The dispatcher's
post-fire step — `host->CreateAndUpdateVisualHost()` inside
`DispatchControlAction` — rebuilds the ibWebWindow tree from the
current ibValueFrame state, and the HTTP response body carries the new
JSON. The browser's per-type JS renderer updates the DOM from that.
Scripts that want to force a refresh call `ibValueForm::RefreshForm`
today; that can become a formal "render-needed" signal later if we add
server-push, but no new wx event type is needed.

### Event flow — button click (sync, current thread)

```
HTTP POST /action/<controlID>  ← wenterprise-server/main.cpp
    → SessionManager::FireAction
        → locate ibValueButton by controlID (existing)
        → wxCommandEvent ev(wxEVT_BUTTON, controlID);
          ev.SetEventObject(btn);
          btn->GetEventHandler()->QueueEvent(ev.Clone());
          btn->GetEventHandler()->ProcessPendingEvents();  ← sync drain
        → wx routes to every Bind'd handler:
             ibValueButton::OnClick(wxCommandEvent&)       ← new method
                 → CallAsEvent(m_onButtonPressed, …)       ← existing script call
        → visualHostClient->WalkToJSON → response body
```

The existing `ibValueButton::FireClick` becomes a two-line shim: build a
`wxCommandEvent` and `QueueEvent` on self, then `ProcessPendingEvents`.
The click-handling body moves into `OnClick(wxCommandEvent&)` bound in
the ctor via `Bind(wxEVT_BUTTON, &ibValueButton::OnClick, this)`. That's
the entire control-side change per event kind.

### Event flow — timer (async, cross-thread — future)

```
AttachIdleHandler("Refresh", 5, false)
    → create wxTimer bound to ibWebApplication (session handler)
    → timer starts; fires every 5s on wx's timer thread

tick:
    → dispatcher->QueueEvent(new wxTimerEvent(timer))   ← thread-safe
                                                           (wx copies)

next HTTP request arrives on handler thread:
    → dispatcher->ProcessPendingEvents()
        → ibWebApplication::OnTimer(wxTimerEvent&)
            → resolve procedureName from timer id
            → find form, invoke m_procUnit->CallAsFunc(name)
        → response includes updated tree
```

Today `AttachIdleHandler` does nothing on web. First cut: declare a
`wxTimer*` map keyed by (formId, procedureName), bound to the session's
`ibWebApplication`. `OnTimer` handler on the application resolves the
mapping and dispatches to the right form's `ibProcUnit`.

### Thread safety

- `ibProcUnit::Execute()` is not reentrant and holds per-session state.
  All script invocations must happen on a single serialised thread per
  session.
- `QueueEvent` is safe across threads; `ProcessPendingEvents` and any
  script invocation it triggers must stay on the session thread.
- Today = HTTP handler thread. When async lands, a session worker thread
  takes over; HTTP handlers become producers that wait on the worker.

### Migration steps — iteration 1 ✅

What actually shipped (button only). Key architectural choice:
**event-generation logic lives on `ibWebWindow` + descendants** — not
on the `ibValue*` controls, not on a separate dispatcher class.
`ibValueXxx` just wires the `Bind` in its web `Create()` and exposes a
`GetWebNode()` accessor so the HTTP layer can reach the emitter.

1. **Shared primitive on `ibWebWindow`** —
   `FireCommand(wxEventType type)` builds a `wxCommandEvent` with this
   node's control id, `QueueEvent`s it on self, and
   `ProcessPendingEvents` drains on the caller thread. This is where
   the Queue + Drain pair is localised for future async relocation.
2. **Per-control fire method on the descendant** — `ibWebButton::FireClick()`
   is a one-liner: `return FireCommand(wxEVT_BUTTON);`. Every new
   control adds its own semantic wrapper the same way
   (`ibWebTextCtrl::FireTextChange(...)`, `ibWebCheckBox::FireToggle(...)`).
3. **Bind in `ibValueButton::Create` (web branch)** —
   `webButton->Bind(wxEVT_BUTTON, &ibValueButton::OnButtonPressed, this)`.
   Mirrors the desktop line verbatim. `ibWebButton*` is stashed on
   `ibValueButton::m_webNode` (web-only field) + exposed via
   `GetWebNode()` for the dispatcher.
4. **HTTP dispatch in `FireActionInSession`** —
   `btn->GetWebNode()->FireClick()`. The old `ibValueButton::FireClick`
   shim is **removed** — the `ibValue*` layer is no longer a gatekeeper
   for event generation; it's just where the `Bind` target lives.
5. **`ibValueButton::OnButtonPressed(wxCommandEvent&)`** — already
   existed for desktop (`buttonEvent.cpp`); added one null-check on
   `m_formOwner`. `buttonEvent.cpp` added to `wfrontend.vcxproj` so
   the symbol exists in the web build.

**`m_webNode` lifetime**: set in `Create`, valid until the next
`ClearVisualHost` wipes the tree. On the current synchronous flow,
`FireClick` is only called during a request that either (a) builds the
tree first via `CreateAndUpdateVisualHost` or (b) was preceded by one.
Never dangling at use time. When async arrives, guard with a freshness
check or promote `ibValueButton` to `wxEvtHandler` and bind on itself.

Verified against `fb_test251` / `CommonForm2`: 4 buttons (ids 19, 21,
23, 25) all return `HTTP 200` with a consistent-size tree response
and no errors / no SEH traces in the server log.

### How to add a new event (growth template)

The architecture has three layers and adding an event touches each once.

**Layer 1 — emitter on the web node** (`webWindow.h`). Pick the wx event
class that matches the UI kind. Add a semantic wrapper that constructs
it and calls `FireEvent`:

```cpp
class ibWebTextCtrl : public ibWebWindow {
    // Payload is meaningful — put it in the event, not in a side channel.
    bool FireTextChange(const wxString& v) {
        m_value = v;                            // local state for JSON
        wxCommandEvent ev(wxEVT_TEXT);
        ev.SetString(v);
        return FireEvent(ev);                   // inherited, primitive
    }
    bool FireKeyDown(int keyCode, int mod) {
        wxKeyEvent ev(wxEVT_KEY_DOWN);
        ev.m_keyCode  = keyCode;
        ev.m_rawCode  = keyCode;
        // modifier flags live on dedicated ev fields (shift/ctrl/alt).
        return FireEvent(ev);
    }
};
```

**Layer 2 — HTTP → server bridge** (`ibWebApplication::Dispatch*`). Add
a dispatch method per event kind. It resolves the control by id and
calls the matching Layer-1 emitter:

```cpp
bool ibWebApplication::DispatchTextChange(int controlId, const wxString& v)
{
    ibValueFrame* ctrl = ResolveControl(controlId);  // same lookup as DispatchControlAction
    if (auto* tc = dynamic_cast<ibValueTextCtrl*>(ctrl)) {
        if (auto* web = tc->GetWebNode()) web->FireTextChange(v);
    }
    GetActiveHost()->CreateAndUpdateVisualHost();
    return true;
}
```

(Or generalise to `bool Dispatch(int controlId, wxEvent& event)` —
caller builds the event, dispatcher only resolves + fires. See "Open
questions" below on the tradeoff.)

**Layer 3 — HTTP endpoint + shim** (`wfrontend.cpp`, `main.cpp`). New
HTTP route carries the payload; shim calls Dispatch:

```cpp
// main.cpp
svr.Post(prefix + R"(/change/(\d+))",
    [](const httplib::Request& req, httplib::Response& res) {
        int id  = std::stoi(req.matches[1]);
        auto v  = req.get_param_value("value");   // or parse JSON body
        res.set_content(wfrontendFireTextChange(sid, id, v), "application/json");
});

// wfrontend.cpp shim
std::string wfrontendFireTextChange(sid, int id, const std::string& v) {
    // validate session, try/catch, call app->DispatchTextChange(id, v),
    // return active host JSON — same template as FireActionInSession.
}
```

**Browser side**: add a JS class for the new control type (existing
per-type renderer pattern in `wenterprise-server/main.cpp`) and wire
input events to the right endpoint. That's the only client-side change.

That's the whole template. Each new event kind is one new method at
each of the three layers; no base-class refactor, no new wx event type,
no cross-cutting change. The structure scales by pattern.

### Iteration 2

Two independent blocks. **2a first** (validates the growth template on
a second event family, still sync, no new threads). **2b next**
(introduces the first real cross-thread `QueueEvent` use; gated on the
separate "delivery" question below).

#### 2a — Textctrl / checkbox / choice events

Scope: three Command-family event kinds so the growth template shown
above gets exercised on a real second controller. Port the controls
(see `docs/web/open-issues.md` — they're already on the priority
list) and add their events in the same commit:

1. **Port the `*.cpp` to `wfrontend.vcxproj`** (textctrl and checkbox —
   all four `checkbox*.cpp` — are already ported; `choice.cpp` is the
   next one). Stub-first pattern from `conventions.md`.
2. **Layer 1** (`webWindow.h`):
   - `ibWebTextCtrl::FireTextChange(const wxString&)` — already has
     `m_value`; set it then `wxCommandEvent(wxEVT_TEXT); SetString(v); FireEvent(ev)`.
   - `ibWebCheckBox::FireToggle(bool)` — `wxCommandEvent(wxEVT_CHECKBOX); SetInt(checked ? 1 : 0); FireEvent(ev)`.
   - `ibWebChoice::FireSelection(int index)` — `wxCommandEvent(wxEVT_CHOICE); SetInt(index); FireEvent(ev)`.
3. **Layer 2** (`webApplication.{h,cpp}`):
   - `DispatchTextChange(int id, const wxString& v)`.
   - `DispatchToggle(int id, bool checked)`.
   - `DispatchSelection(int id, int index)`.
   - Each does the same `GetActiveHost → form → FindControlByID →
     dynamic_cast → GetWebNode → FireXxx → CreateAndUpdateVisualHost`
     shape as `DispatchControlAction` — factor a small `ResolveControl`
     helper on first repetition, not prematurely.
4. **Layer 3** (`wfrontend.{h,cpp}`, `wenterprise-server/main.cpp`):
   - HTTP endpoints:
     `POST /change/<id>` with form-param `value=…` (text),
     `POST /toggle/<id>` with form-param `checked=0|1`,
     `POST /select/<id>` with form-param `index=N`.
   - `wfrontend*` shim per endpoint mirrors `wfrontendFireAction`.
5. **Browser** (`main.cpp` inline JS): per-type `BaseControl` subclass
   listens to `input` / `change` events and `fetch`es the matching
   endpoint.
6. **`ibValueTextCtrl::OnTextEnter` / `OnChecked` / etc.** — if the
   desktop already has these as bound handlers, add their source files
   to `wfrontend.vcxproj`; otherwise add minimal web-only handlers.

Validation: one real textctrl-driven script on a test form
(`fb_test251/CommonForm2` has 5 textctrls) fires via HTTP POST and
actually mutates the form. Same verification discipline as iteration 1
(HTTP 200, no SEH, stable server log).

#### 2b — Server-side timer (`AttachIdleHandler`)

Timer is **fully server-side**. Browser is neither the tick source nor
the tick target. This is what today's NOP in `ibValueForm::AttachIdleHandler`
(formObject.cpp:656–689, web-guarded away) should become.

Mechanism:

1. **Per-session `wxTimer` map** on `ibWebApplication`:
   `std::map<int, std::pair<ibValueForm*, wxString>>` keyed by
   `wxTimer::GetId()`, valued by (form, procedure name). A parallel
   `std::vector<std::unique_ptr<wxTimer>>` owns the timers so they're
   destroyed with the app.
2. **`ibValueForm::AttachIdleHandler`, web branch**:
   ```cpp
   wxTimer* t = new wxTimer();
   // Bind to the session's ibWebApplication so fires route to its
   // wxEvtHandler queue, not to some unowned handler.
   ibWebApplication* app = sessionApp();   // session-scope accessor TBD
   t->SetOwner(app, t->GetId());
   app->RegisterTimer(t, this, procedureName);
   t->Start(interval * 1000, single);
   ```
3. **`ibWebApplication::OnTimer(wxTimerEvent&)`** — bound via
   `Bind(wxEVT_TIMER, &ibWebApplication::OnTimer, this)` in `OnInit`.
   Looks up `{form, name}` from the map via `event.GetId()`, runs
   `form->GetProcUnit()->CallAsFunc(name)`, then
   `host->CreateAndUpdateVisualHost()` (or lets the next drain pick
   it up; TBD based on the "delivery" decision below).
4. **Crucially the timer thread is wx's own internal thread** (when
   `wxTimer` is used without a GUI loop, wx drives it via
   `wxThread`-backed schedulers on MSW). The tick does
   `QueueEvent(new wxTimerEvent(...))` — thread-safe by contract.
   Our existing `ProcessPendingEvents()` on the HTTP handler thread
   drains it on the next request. That's a complete working loop for
   validation, **even without solving delivery**.
5. **Delivery of timer-driven changes** — three orthogonal options,
   pick one when a concrete use case appears (the timer works to mutate
   state regardless):
   - **Client polling**: browser `setInterval(fetch('/session'), N)`.
     Simplest; wasteful.
   - **Long-poll**: `GET /wait` hangs until a dirty flag flips, then
     returns. Server flips on timer-driven mutation. One thread per
     waiting session.
   - **SSE (`Server-Sent Events`)**: dedicated `GET /stream` keeps
     connection open, server `data:` pushes on dirty. Standard HTTP,
     no WebSocket upgrade, one-way.
   All three only consume the existing dirty signal; the timer
   implementation doesn't depend on which is chosen.

Validation (without a delivery mechanism): register an idle handler
in a script that increments a counter on the form. Wait 10 s. Issue
any HTTP request. Verify the JSON response shows the counter updated
N times (= fires drained). This proves the tick → queue → drain loop.

#### 2c — Polymorphic dispatch (remove the dynamic_cast cascade)

Current dispatcher does a type-dispatched `dynamic_cast` ladder
(`ibValueButton` / `ibValueTextCtrl` / `ibValueCheckbox` / …) and
calls the matching FireXxx on the web node. Adding a new control adds
a new branch in `webApplication.cpp`. Goal: fold the per-kind decision
into the web node itself. The dispatcher becomes generic; each
`ibWebXxx` knows its own event kinds.

Shape:

```
HTTP POST /event/<id>   body: kind=click|text|toggle  value=…
  → wfrontendFireEvent(sid, id, kind, value)
  → ibWebApplication::Dispatch(id, kind, value)
        ResolveControl(id) → ibValueFrame*
        ctrl->GetWebObject()           ← virtual accessor on ibValueFrame,
                                         returns base ibWebWindow*
        web->HandleRequest(kind, value) ← virtual on ibWebWindow,
                                         each descendant owns its kinds
  → CreateAndUpdateVisualHost
```

Touch list:

1. **`ibValueFrame` base**: web-only slot `ibWebWindow* m_webObject`
   + virtual `GetWebObject()/SetWebObject()`. Replaces the three
   copy-pasted `m_webNode` fields and per-subclass `GetWebNode`
   accessors in `ibValueButton` / `ibValueTextCtrl` / `ibValueCheckbox`.
2. **`ibWebWindow` base**: `virtual bool HandleRequest(const wxString&
   kind, const wxString& value)` — default returns false. Descendants
   override.
3. **`ibWebButton::HandleRequest`**: `kind == "click"` → `FireCommand(wxEVT_BUTTON)`.
4. **`ibWebTextCtrl::HandleRequest`**: `kind == "text"` → `FireTextChange(value)`.
5. **`ibWebCheckBox::HandleRequest`**: `kind == "toggle"` → `FireToggle(value == "1")`.
6. **`ibWebApplication::Dispatch(id, kind, value)`** — single method.
   `DispatchControlAction / DispatchTextChange / DispatchToggle` either
   disappear or become thin shims calling `Dispatch` with the kind
   baked in.
7. **HTTP endpoint**: add `POST /event/<id>` with `kind` + `value` form
   params. Existing `/action`, `/change`, `/toggle` stay as shims
   forwarding with a fixed kind, so no browser regression.
8. **JS**: existing renderers unchanged initially. When a new control
   needs an event, it POSTs `/event/<id>` with its kind; no new
   dispatcher branch needed server-side.

Adding the next control (radiobutton, combobox, …) touches exactly
one file on the server (the new `ibWebXxx::HandleRequest` override in
`webWindow.h`) plus its JS renderer. No dispatcher change. No new
HTTP route.

#### 2d — Per-session worker thread (timing accuracy)

The 2b timer is **infrastructure-complete but timing-imprecise**.
`wxTimer` ticks land in the app's event queue via `QueueEvent`, but
`ProcessPendingEvents` only runs on the HTTP handler thread when a
request arrives. Consequences:

- No HTTP activity for 5 s → 5 ticks queue up → next request fires
  the script **5 times back-to-back**. State converges, but script
  sees "5 seconds elapsed", not "5 separate second-boundary events".
- Browser polling at 500 ms → drain every ~500 ms → ticks fire
  within ±500 ms of their real time.
- Idle tab → script **never executes** until the user clicks again.

For "every second, exactly once, near its real boundary" we need a
**per-session worker thread** that holds the session's event loop
independently from HTTP:

```
wx timer thread  ──▶ QueueEvent(tick)  ──▶ ┐
                                           │
HTTP handler     ──▶ QueueEvent(req)   ──▶ ├── session worker thread
                                           │   (drains ProcessPendingEvents
future:          ──▶ QueueEvent(push)  ──▶ ┘    on its own schedule)
                                                     │
                                                     ▼
                                               ibProcUnit
                                               (one thread — no mutexes)
```

Key properties:

- **Script always on one thread.** Worker is the only thread that
  ever calls `ibProcUnit`; HTTP handlers and timer threads post via
  `QueueEvent`. No mutex around `m_procUnit`.
- **Ticks process at their actual time**, not when the next user
  request happens.
- **HTTP handler becomes producer + waiter**: posts an event,
  waits on a `std::condition_variable` or per-event future,
  worker drains and signals back. Latency: one context switch.
- **Shutdown**: `OnExit` signals the worker to drain + stop, joins
  the thread.

**Delivery is orthogonal.** The worker makes the tick *run* on time
server-side; a client-side mechanism is still needed for the
browser to *see* the update between user actions:

- **Client polling** — `setInterval(fetch('/session'), 1000)`.
  Simplest; extra traffic even when idle.
- **Long-poll** — `GET /wait` hangs until a dirty flag flips.
  One connection per session, server flips on any tick-driven
  mutation.
- **SSE** — dedicated `GET /stream` stays open; server `data:`
  pushes on dirty. One-way HTTP, no WebSocket handshake.

Pragmatic order if live updates become needed: **worker thread
first** (fixes timing), **client polling second** (covers most use
cases), **SSE/long-poll when polling load matters**.

Until 2d lands, server-state mutation from timers is correct but
eventually-consistent with the UI — a user who clicks the page sees
a merged view of all pending ticks. For diagnostic / counter /
background-refresh use cases this is often enough.

#### What's still not in iteration 2

- **Tab-switch scripting hooks** (OnActivate / OnClose). Deferred
  until a configuration needs them.
- **Event veto / priority / cancellation chains.**
- **Mouse / key events on regular controls.** Will land when a
  concrete widget needs them (chartBox pan, custom-drawn control).

### Out of scope for iteration 1

- Per-session worker thread. Everything drains on the HTTP handler
  thread.
- Timer implementation. Wire `AttachIdleHandler` in iteration 2 once
  iteration 1 proves the Bind-based path.
- Server-push (SSE / WebSocket).
- Event veto / priority / cancellation chains.

## Why not a custom `ibEVT_WEB_ACTION` event id

Two reasons:

1. **Uniformity with desktop.** A script or binding that listens to
   `wxEVT_BUTTON` on `ibValueButton` works regardless of where the
   event came from (native click in Designer, HTTP POST on the web).
   Custom ids would force every listener to know both.
2. **Zero wx cost.** `wxCommandEvent(wxEVT_BUTTON, id)` is the exact
   object wx itself emits on native clicks. Reusing it means no new
   header, no new macro, no new event table.

The only case a custom event id pays off is when no wx predefined type
fits — e.g. an OES-specific domain event ("document posted", "record
saved"). Those can live in a separate `ibEVT_DOC_*` namespace later
without coupling to this dispatcher design.

## Open questions (park until iteration 2)

- **Response waiting under async.** When the HTTP handler posts on a
  worker thread and waits, how does it know when the script finished
  mutating the form tree? Options: `wxCondVar` per event; per-event
  completion callback; short-circuit on the producer side if the worker
  happens to be idle. Decide when a concrete use case appears.
- **Reentrancy from script.** If a script handler opens another form
  that itself has an OnOpen script, we're recursively inside
  `ProcessPendingEvents`. wx handles this but the `m_procUnit` call
  stack must also tolerate it — already does for desktop, double-check.
- **Timer identity across requests.** `wxTimer` holds an integer id that
  must map back to (formId, procedureName). Simplest: `std::map<int,
  {form, name}>` on the application, timer id allocated on Attach.
  See iteration 2b above for the concrete shape.
- **wxString thread safety under MSW.** Today's sync path never hits
  it; 2b timer thread is the first real test. `wxEvent::Clone()` makes
  a copy that owns its own strings, so the producer's wxString goes
  out of scope safely. Validate once during 2b wiring that setting
  `wxCommandEvent::SetString` on one thread and reading on another
  survives a burst of ticks.
