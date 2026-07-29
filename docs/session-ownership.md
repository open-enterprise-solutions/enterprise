# Session ownership — the window owns the session

> **Status:** LANDED (2026-07-30). Desktop, web and headless all run on
> it; Debug|x86 green; desktop and web close paths verified against a
> live infobase. See §"Verified end to end".

A session lives exactly as long as the thing it exists for. Kill that
thing and the session is gone; while it lives, the session is alive.
There is no second way to end a session and no way to forget to end one.

This document is the *ownership* half of the session story. The registry
mechanics — priority queue, `sys_session` row I/O, heartbeat liveness,
policy chain — are in [session-registry.md](session-registry.md), which
predates this arc and still describes the queue accurately.

## The two primitives

```cpp
ibSessionHolder   // owns: move-only, its destructor ends the session
ibSessionWatch    // observes: weak, cannot extend anyone's life
```

`backend/session/sessionHolder.{h,cpp}`.

**Holder** is move-only on purpose. A copyable holder would mean "the
session dies when the LAST copy dies", and one copy forgotten in a
lambda capture or a worker would keep a session alive after its window
is long gone — the exact failure this arc removes. Exactly one owner,
always.

**Watch** is what everything else uses: the registry's index, a
session's own back-links, worker tasks, the debugger's parked queue. It
answers honestly after the owner is gone (`Expired()` true, `Share()`
empty), which is the difference between a question with an answer and a
dangling pointer.

`ibSessionWatch` has **no** `operator->`. It reads like a pointer but
takes a fresh hold on each use, so `if (w && w->Inserted())` is two
separate holds with a window between them where the owner can let go.
Take the hold once and work through it:

```cpp
auto s = watch.Share();
if (!s) return;            // owner already let go
s->SetActivity(...);
```

## Who owns what

| Session kind | Owner of the holder |
|---|---|
| desktop enterprise / designer | the main window (`ibFrontendMainFrame`) |
| web tab | the tab's window (`ibWebFrame`) |
| daemon / codeRunner | the enclosing scope (`main`) |
| wes technical row | `g_serverSession` in `wfrontend.cpp` |
| compute-server projection (future) | the server-side `ibBackendDocFrame` |
| scheduled-job runner (future) | the runner object |

The registry is **not** an owner. `m_own` is
`unordered_map<wxString, ibSessionWatch>` — an index plus the
`sys_session` row, nothing more.

## A frame cannot exist without a session

`ibBackendDocFrame` has no default constructor. The only way in is:

```cpp
explicit ibBackendDocFrame(ibSessionHolder&& holder);
```

so "someone forgot to attach the session" is not a bug that can be
written. Sessionless is still a legal case — it just has to be said out
loud, as `codeRunner` does:

```cpp
ibFrameCodeRunner::ibFrameCodeRunner(...) :
    ibBackendDocFrame(ibSessionHolder()),   // opens no infobase
    wxFrame(parent, id, title, pos, size, style), ...
```

## Startup: authenticate, then build the window around the holder

Opening a session is a front-end act. The backend cannot start one —
`CreateSession` hands the holder to its caller, and only the caller
builds a window around it.

```cpp
// enterprise/mainApp.cpp, designer/mainApp.cpp
ibSessionHolder holder = appData->CreateSession<ibGUISession>();
if (holder->Open(user, password) != ibSession::OpenResult::Authenticated)
    return 0;                       // holder dies here → row removed

auto* frame = new ibFrontendMainFrameEnterprise(std::move(holder));
if (!frame->Show()) { frame->Destroy(); return 1; }
return wxApp::OnRun();
```

Two things make this possible:

- **The login dialog needs no window.** `ibGUISession::OnShowAuthenticate`
  drives the standalone `ibPromptAuthenticationDialog`, so authentication
  happens before any frame exists. That is what lets the frame be built
  *around* an already-authenticated holder.
- **Every error path is just a dropped holder.** No cleanup call to
  forget: `return` removes the `sys_session` row.

The web mirror is one line inside `ibWebApplication::OnInit`:

```cpp
m_frame = new ibWebFrame(std::move(holder), this);
```

`Show()` does the whole opening in a fixed order — and the order is not
arbitrary:

```
CreateGUI()        panes exist first
EnsureRuntime()    root mm + AttachRuntime
AllowRun()         BeforeStart / OnStart (enterprise), metadata tree (designer)
wx Show + Raise
```

`AllowRun` reaches into panes (a startup script may open forms; the
Designer loads its tree into `m_metaWindow`), so asking before building
dereferences panes that do not exist yet. That was a real crash during
this arc, caught by the Designer's `m_metaWindow->Load()`.

## Shutdown: one road, two entrances

```
[X]                → wxEVT_CLOSE ─┐
session->Close(f)  → frame->Close(f) ─┴→ OnCloseWindow
                                          ├ !force && !AllowClose() → Veto, nothing happened
                                          └ Skip → wx destroys the window
                                              └ ~ibBackendDocFrame → holder released
                                                  └ ibSession::Teardown()
```

`ibSession::Close(bool force)` does exactly one thing: it asks whatever
owns the session to close. It never tears anything down itself, not even
under force — a session dismantled while its holder still lives would
leave the owner sitting on a corpse: a live window whose `GetSession()`
answers with a session that has no row, no runtime and no state.

Per-kind, "close" means:

| Kind | `OnClose(force)` |
|---|---|
| desktop | `frame->Close(force)` (marshalled to the main thread if needed) |
| web tab | `wfrontendRequestDestroySession(GetId())` — queued for the sweep |
| nothing to close | `Teardown()` — no owner will release a holder, so this *is* the end |

The web close is queued rather than immediate because the caller is
usually the session's own worker (script `EndJob`) or the registry
thread, and the teardown drains that worker — doing it inline would
deadlock against itself. `SessionManager::RequestDestroy` puts the id in
a set and wakes the sweep thread, which owns nothing.

### force

`force` is never invented locally — it arrives either as `Close(true)`
or as `!event.CanVeto()` on the wx event, and travels down unchanged:

```
Close(true) → RequestForceExit()          interpreter unwinds at next opcode
            → OnClose(true) → frame->Close(true)
                → wxEVT_CLOSE with CanVeto()==false
                    → AllowClose() is NOT called — nobody is asked
                    → Skip → Destroy
```

`AllowClose()` therefore takes **no** force parameter: "don't ask" is
expressed by not asking. Under a soft close it runs in two steps, and a
refusal from either leaves everything untouched:

1. **documents** — `ibDocManager::CloseDocuments(false)`, each open
   document may refuse (unsaved data, an edit it will not abandon);
2. **runtime** — `ExitMainModule()`, which fires `BeforeExit` (may
   cancel) and `OnExit`. Both run while window, runtime and session are
   all still alive, so the script can save, message and query.

Note `ExitMainModule(true)` returns immediately without running
anything: under force the script's exit handlers do **not** fire. That
is pre-existing behaviour, not a consequence of this arc.

`Destroy()` closes whatever is still open unconditionally
(`CloseDocuments(true)`) — that is past the point of no return, where a
refusal could no longer be honoured anyway.

### Teardown

`ibSession::Teardown()` is private, and the only class that can reach it
is `ibSessionHolder` (`friend`). That is what makes "the owner died, so
the session died" a property of the types rather than a convention.

```
Teardown()
  ├ Transition(Stopping)      synchronously — see below
  ├ quiesce: RequestCancel + wait behind an empty task in the session's
  │          own FIFO (5s cap) — a script thread holds its session by RAW
  │          pointer on its stack, where no weak_ptr can help
  └ Remove@Urgent → registry thread:
        DELETE sys_session row, OnDisconnect (DetachRuntime + DestroyRoot),
        drop the index entry
            └ last session out → OnLastDisconnect → CloseDatabase
                  (CloseSubtree Before/After → m_image.reset(): the ctor
                   factory, module storage and compile cache all go)
```

`Stopping` is stamped **synchronously**, not left to the registry
thread: closing a window ends up calling `Teardown` again when its holder
is released, and without the synchronous mark that second pass would run
the whole teardown twice.

## Verified end to end

Desktop (`enterprise.exe --file=…`, `WM_CLOSE` sent to the window — the
[X] path):

```
[session INSERT] ok guid=2d07a729… mode=3
[session REFRESH] snapshot now has 2 row(s)
--- WM_CLOSE ---
* Unregister class 'DocumentObject.Document6' …     CloseDatabase, phase 3
[session REFRESH] snapshot now has 1 row(s)          row removed
process exits, no dump
```

Web (`wenterprise-server --file=… --port=8123`, curl):

```
GET /            200
POST /login      200  → [app] frame created, snapshot 2 rows
POST /logout     200  → [app] delete m_frame → snapshot 1 row
```

## Known edges

- **A close that never reaches an event loop.** `Destroy()` on a
  top-level window is delayed (`wxPendingDelete`, pruned on idle —
  `wxTopLevelWindowBase::Destroy`). On the failed-`Show()` path in
  `mainApp` we return without ever entering the loop, so the destructor
  does not run and the holder is not released; `registry->Stop()` in
  `OnExit` removes the row instead. This is the one path where the holder
  does not decide.
- **Recursion is prevented by base-class order.** `ibFrontendMainFrame`
  declares `ibBackendDocFrame` first, so it is destroyed last — by the
  time the holder is released, `~ibFrontendMainFrame` has already cleared
  `s_instance`, and `ibGUISession::OnClose` finds no window to close.
  Reordering the bases would reintroduce a loop.
- **Off-main-thread close answers before the fact.** `ibGUISession::OnClose`
  defers to `CallAfter` and returns `true`; the window closes a moment
  later. Harmless for kick / debug-kill, but the answer is optimistic.
- **Quiesce has a 5s cap.** A script blocked in I/O never sees the
  cancel flag; after the cap the teardown proceeds anyway.

## What this replaced

Removed, not moved: `OnCreateSession`, `OnDestroySession`, `ShowFrame`
(both the session hook and the static), `Initialize(session)`,
`AttachFrame` / `DetachFrame`, `m_session` mirror on the frame,
`SetGUISession` / `GetGUISession`, `GetFrontendFrame`,
`FinishCreateSession`, the `ibEnterpriseSession` / `ibDesignerSession`
wrappers (both had become empty), and the `mainFrameCreate` /
`mainFrameShow` / `mainFrameDestroy` macros.

Fixed along the way:

- an unclosable window when the runtime never came up (the old
  `AllowRun`/`AllowClose` pair returned false on **both** boundaries when
  the root was missing);
- `Show(false)` asking "may I close?" — minimising a window could run
  the `BeforeExit` script;
- documents polled from `Destroy()`, i.e. below the point of no return
  where their refusal could not be honoured;
- `OnClose` fired twice on every forced close (once from
  `RequestForceExit`, once from `Close`);
- a second teardown in the window between `Submit(Remove)` and the
  registry stamping `Stopping`.
