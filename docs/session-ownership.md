# Session ownership — the window owns the session

> **Status:** LANDED (2026-07-30). Desktop, web and headless all run on
> it; Debug|x86 green; desktop and web close paths verified against a
> live infobase. See §"Verified end to end".
>
> **2026-07-31:** a session can now own one more session — the *reader*
> (`ibSessionKind::SessionReader`), created on first background read and
> released with the window or after 60 s idle. See §"The reader session".

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
| reader (`SessionReader`) | the session it reads for — `ibSession::m_reader` |
| job run (`BackgroundJob` / `ScheduledJob` / `SystemJob`) | the `ibJobEntry` / `ibBackgroundRun` |

The registry is **not** an owner. `m_own` is
`unordered_map<wxString, ibSessionWatch>` — an index plus the
`sys_session` row, nothing more.

## The reader session — the sleeping half of a window

> Landed 2026-07-31. `ibSessionKind::SessionReader = 104`;
> `ibSession::Reader()` / `DropIdleReader(int)` / private `OpenReader()`
> in `backend/session/session.{h,cpp}`.

A list or table control must not read on the thread that draws it. So a
read is not submitted to the window's own session — on the desktop that
session's worker **is** the wx main thread, the very thread the read has
to get off. It is addressed to somebody else:

```cpp
ibSession::Current()->Reader()->Submit(work);
```

The verb stays one (`Submit`). What differs is the **addressee**.

`Reader()` returns `this` when there can be no reader — teardown is under
way, there is no registry, or this session already *is* one. That is why
it returns a session rather than a pointer that can be empty: the call
above still compiles to something correct (submit to yourself, which the
GUI session runs inline), and the caller needs no branch, no null check
and no fallback of its own.

### Why a SESSION and not just a thread

Everything a read resolves — the connection, the RLS access policy, the
interpreter state — resolves through `ibSession::Current()`, and the
worker binds that with `ibSessionScope` before draining its queue. So
handing the read a *different session* is the whole mechanism: it is what
makes those lookups land somewhere else without threading an explicit
holder through every layer from L5 down to L2.

And a session owns exactly **one** connection (`ibSession::Holder()`;
`AcquireFreeConnection` is deliberately not mirrored on it) — which is
precisely the second connection a parallel read needs. A thread would
have given neither.

The reader is a plain `ibSession` (base factory), so its
`GetWorkerPool()` is the registry's pool — `ibWorkerPoolHeadless`, off
the UI thread. Its identity is the owner's: `m_userInfo` is read on the
owner's thread at creation and installed on the reader, so it sees
exactly what the person at that window sees — same rights, same
row-level policy, same result.

### Created on first read, woken by its own first task

`OpenReader()` runs under `m_readerMtx` (two controls on one window can
ask at the same moment and must not each build one). A window whose lists
never read in the background never pays for one.

The identity install and the runtime bring-up are submitted as the FIRST
task in the reader's own queue, not run inline — they must happen on the
reader's worker, where `ibSessionScope` has bound it:

```cpp
reader->Submit([registry, reader, owner]{
    if (owner.IsOk()) registry->InstallUser(reader, owner, wxEmptyString);
    registry->NotifyAuthenticated(reader);
    reader->SetActivity(_("waiting for a read"));
});
```

**The FIFO queue IS the initialisation barrier.** Because the queue is
drained in order under a lease, every read submitted after this one is
guaranteed to run after it finishes. No "ready" flag, nobody waiting, no
re-check per read. The same property gives **answer ordering for free**:
however many portions the scroll wheel asks for, the answers come back in
the order they were asked. Nothing enforces that — it is what a session
is.

A wake-up that throws is logged (`wxLogDebug`) and nothing else: the
reads then run on it and fail one by one, visibly, instead of this
failing once, silently, where nobody is looking.

### It is in Active Users, and it is not a person

The reader holds one connection out of the pool, and an invisible session
holding a connection is what makes pool exhaustion undiagnosable — the
administrator counts half the rows and sees none free. So it appears:
`GetApplication` → *"Background read"*, `GetSessionKindDescr` →
*"Reader"* (`sessionSnapshot.cpp`).

⚠ But it is **not a second user**. It carries the same user name and
computer as the window it reads for. Every predicate meaning "is a person
working here" skips it — `HasActiveUsers`, `IsUserActive`, and by the
same rule the three job kinds (`ibIsPersonSession` in
`sessionSnapshot.cpp`). Otherwise one person reads as two, an
exclusive-mode gate blocks on a session the blocking window created
itself, and a licence count doubles every seat.

Its administrative decision is its own, distinct from a job's: killing a
reader makes somebody's list stop loading and it comes back on the next
scroll, where killing a `BackgroundJob` loses a result somebody is
waiting for.

### It is released, not kept

A reader that outlives the reading is exactly what must not exist: an
idle form would keep a row in Active Users and a connection out of a pool
of 32 for as long as it stays open. So "sleeping" is not a state the
reader has — it stops existing, and the next scroll makes a new one. Two
exits:

- **Owner teardown.** The reader goes **first**, at the top of
  `ibSession::Teardown()`, before the owner quiesces itself. Nobody else
  knows about it, so nobody else would release it. The holder is moved
  out under `m_readerMtx` and reset **outside** the lock — its release
  runs the reader's own teardown (cancel, drain, hand the connection
  back), which must not wait behind the owner's.
- **Idle sweep.** `DropIdleReader(60)` from
  `ibSessionRegistry::JobDropIdleReaders()` on the 3 s tick.
  `m_readerLastUseMs` is stamped in `Reader()` — at SUBMIT, so a long read
  counts as use only at its start; the timeout is set well above any sane
  single-page read.

Sixty seconds is long next to a scroll and short next to a coffee break:
a burst of wheel turns is served by ONE reader, and only a genuine pause
pays for a fresh `Connect`. It is also what the worker pool uses to
shrink its own idle threads, so the two agree on what "idle" means rather
than each having a private opinion.

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
  ├ release m_reader          FIRST, and outside m_readerMtx — a session of
  │                           our own making that nobody else would release,
  │                           holding a pooled connection while it lives
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
