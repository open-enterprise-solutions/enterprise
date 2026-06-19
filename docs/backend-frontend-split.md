# Backend / frontend DLL split — architecture review

> **Status:** REVIEW / OBSERVATION (not a refactor arc). Describes the
> current backend / frontend interface (`ibBackendDocFrame` +
> siblings), lists known design smells, and points at the refactors
> that would pay off later. No active implementation; pick up when
> a third frontend (mobile / native) or the compute-server tiering
> plan creates concrete pressure.

This doc reviews the mechanism that keeps `backend.dll` UI-agnostic
while letting `frontend.dll` (desktop wx) or `wfrontend.dll` (web,
`OES_USE_WEB`) provide the concrete UI. The linchpin is
`ibBackendDocFrame` — an abstract interface in backend, implemented
once per frontend, reached through the owning `ibSession`. It works,
but the interface still carries some well-known design smells worth
documenting before the next scale step.

> **Naming note:** the historical interface was named
> `ibBackendDocMDIFrame` (and the singleton accessor `GetDocMDIFrame`),
> a relic of the old wxWidgets MDI API. The backend-side rename to
> `ibBackendDocFrame` has landed, and the frontend desktop class is now
> `ibFrontendMainFrame` (`frontend/mainFrame/mainFrame.h`) — the old
> `ibFrontendDocMDIFrame` name is gone from the tree. It still derives
> from `wxAuiMDIParentFrame`, so the wx-side "MDI" relic survives one
> level down; new code MUST NOT introduce the "MDI" suffix in new
> symbols — prefer `Shell`, `HostFrame`, `DocFrame`, `DocParentFrame`,
> or plain `MainFrame`.

## How it works today

**Interfaces in `backend/backend_mainFrame.h`:**

- `ibBackendDocFrame` — abstract. Methods cover four unrelated
  concerns:
  - **Form factory**: `CreateNewForm`, `CreateFormUniqueKey`,
    `FindFormByUniqueKey*`, `UpdateFormUniqueKey`.
  - **Frame/window service**: `RaiseFrame`, `ActiveWindow`,
    `RefreshFrame`, `SetTitle`, `SetStatusText`, `ShowModalMessage`.
  - **Spreadsheet output**: `ShowSpreadsheetDocument`,
    `PrintSpreadsheetDocument`.
  - **Misc metadata accessor**: `FindMetadataByPath`.

  (The earlier `AuthenticationUser` method moved to the session layer —
  `ibSession::OnShowAuthenticate`, overridden by `ibGUISession` for the
  desktop login dialog.)

- `ibBackendValueForm` — the minimum form API for backend (LoadForm /
  SaveForm / BuildForm / ShowForm / Notify* / access flags). Frontend
  provides a concrete `ibValueForm` that implements all this on top
  of wxWidgets (or the web equivalent on wfrontend).

- `ibBackendControlFrame`, `ibBackendMetaDocument`,
  `ibSourceDataObject` — minimal surface for controls, documents,
  data sources. Same pattern: backend talks to the abstract, frontend
  downcasts to concrete in implementations.

**Wiring:**

- The frame is no longer a process-level or thread-local singleton on
  the backend side. `ms_mainFrame` / `t_mainFrame` / `GetDocMDIFrame` /
  `InstallOnThread` and the `backend_mainFrame` macro are all gone.
  The frame belongs to the `ibSession` that created it; backend code
  reaches it through one of:
  - `ibSession::Current()->GetFrame()` (or the convenience
    `ibSession::CurrentFrame()`) — for code that already runs under a
    bound session, which is the common runtime path;
  - `appData->GetMainSession()->GetFrame()` — for process-lifecycle
    callers (metadata-config hooks, property dtors);
  - `ibWebSession::Session()->GetFrame()` — for per-tab web handlers;
  - explicit walking of an object descriptor's parent chain to its
    owning session (`object->GetSession()->GetFrame()`).
- On **desktop**, `ibSession::Current()` resolves through `AccessMode::Single`
  (one session per process for its lifetime) — every thread sees the
  same lone session and therefore the same frame.
- On **web** (wenterprise-server), `AccessMode::Shared` does per-thread
  lookup. Per-tab worker threads are bound to their `ibWebClientSession`
  via `ibSessionScope` / `ibSessionThreadBinding`; the wes process's
  technical `WebServer` session is registered as the fallback for
  unbound threads (registry consumer, signal handlers).
- On **headless** (daemon.exe, codeRunner.exe), the session has no GUI
  frame: `GetFrame()` returns null. The accessors in `backend_form.cpp`
  guard with `if (ibSession::CurrentFrame() != nullptr) ...` and
  otherwise raise
  `ibBackendCoreException::Error(_("Context functions are not available!"))`.

The frontend desktop side does keep its own GUI-local singleton:
`ibFrontendMainFrame::GetFrame()` (via the `mainFrame` macro in
`frontend/mainFrame/mainFrame.h`). That singleton is internal to the
GUI layer and is NOT what backend code reaches into — it's a wx-side
implementation detail of where the desktop frame lives. The web side
has no equivalent.

## What works

- **backend.dll is truly UI-free.** `daemon.exe` and `codeRunner.exe`
  link backend without dragging in wx. The
  pattern has proven itself over years of desktop operation.
- **Swapping frontend is cheap.** `wfrontend.dll` replacing
  `frontend.dll` was possible largely because this interface was
  already in place — web didn't need to touch backend to publish its
  own `ibWebFrame`.
- **Polymorphism, not ifdefs.** Backend call sites stay identical
  for desktop and web. No compile-time branching at the call site; the
  virtual call dispatches at runtime.
- **Frame ownership on the session.** Removing the
  `backend_mainFrame` singleton closed the previous "ambient state"
  hole — the session that created the frame is the explicit owner;
  every backend caller reaches the frame through a session pointer
  that's already in scope. Multiple concurrent web sessions in one
  process each see their own frame without collision.

## What smells

1. **`ibBackendDocFrame` is still a chunky interface.** Form factory,
   frame service, spreadsheet output, metadata lookup — all glued onto
   one vtable. Cohesion is moderate at best; a future component that
   needs only to raise a window ends up depending on form-creation
   too.

2. **Headless null-checks scattered.** Every `ibBackendValueForm::Find*`
   / `CreateNewForm` accessor in `backend/backend_form.cpp` hand-checks
   `if (ibSession::CurrentFrame() != nullptr) ...`. A new method that
   forgets the check would crash headless silently. A null-object
   pattern — an `ibNoUIFrame` that throws `ibBackendCoreException::Error`
   uniformly from every method — would centralise this.

3. **`AccessMode::Shared` per-thread binding is implicit.** Every
   web worker thread that calls into backend must be bound to its
   session via `ibSessionScope` (or `ibSessionThreadBinding` for
   long-lived bindings) before the first script-touching call. Miss
   the binding and `ibSession::Current()` falls back to the
   `WebServer` system session — meaning the call lands in the wrong
   session's runtime, with subtle data-bleed risk. The HTTP-handler
   discipline is documented in memory note
   `reference_sessionscope_http_handlers`, but it isn't enforced by
   the type system.

4. **MDI relic one level down.** Backend (`ibBackendDocFrame`) and the
   frontend desktop class (`ibFrontendMainFrame`) are both rename-clean,
   but `ibFrontendMainFrame` still derives from `wxAuiMDIParentFrame`,
   so the wx MDI machinery (z-order, active-child tracking) is still the
   substrate. Web (`ibWebFrame`) uses tabs with no nesting; some of
   those MDI semantics map awkwardly between the two. Replacing the
   `wxAuiMDIParentFrame` base with the uikit shell (see `docs/uikit.md`)
   would close the asymmetry.

5. **Backend knows frontend concepts via the interface.**
   `ibBackendValueForm::ShowForm(ibBackendMetaDocument* doc, ...)`
   (`backend/backend_form.h:94`) takes the document as its parent — that
   document is really a frontend `ibFormVisualDocument`
   (`: ibDocument`, the in-tree doc/view fork — see
   [`docview-fork.md`](docview-fork.md), not stock `wxDocument`). The
   abstract `ibBackendMetaDocument` surface has to carry enough types to
   let every frontend's concrete document fit through; if tomorrow a
   native-UI frontend needs a different document representation, the
   interface has to grow again.

## Suggested direction (not urgent)

The split is worth keeping — it's earning its cost. The specific
refactors that would pay off later:

- **Carve `ibBackendDocFrame` into cohesive services.**
  - `ibBackendFormFactory` (CreateNewForm / Find* / UpdateFormUniqueKey).
  - `ibBackendFrameService` (RaiseFrame / ActiveWindow /
    RefreshFrame / SetTitle / SetStatusText).
  - `ibBackendSpreadsheetOutput` (ShowSpreadsheetDocument /
    PrintSpreadsheetDocument).

  Each service is independently null-object'able, independently
  testable, and backend call sites only depend on the one they need.

- **Type-safe session binding.** Replace the implicit
  `ibSessionScope`-on-every-handler discipline with a
  `SessionBoundCallable<F>` wrapper that takes the session at
  construction and refuses to fire without one bound. The web
  request dispatcher's natural place to enforce this is the
  per-request entry point.

- **Null-object pattern for headless.** No more `if (frame != nullptr)`
  scattered through `backend_form.cpp` — either the service is
  bound to a real implementation or to a throw-only fallback. One
  place, one policy.

- **Drop the wx MDI base.** `ibFrontendMainFrame` is rename-clean but
  still `: public wxAuiMDIParentFrame`. Reseating it on the uikit shell
  (`docs/uikit.md`) removes the last MDI desktop-ism leak.

None of this blocks current work. It starts paying off when:

- A third frontend needs to ship (native mobile UI? terminal UI for
  `codeRunner` interactive mode?) — the chunky interface will make
  each new impl harder than necessary.
- The compute-server tiering plan progresses (see
  [`compute-server-tiering.md`](compute-server-tiering.md)) — a
  shared worker pool dispatching across many sessions means the
  per-handler binding discipline becomes a hot-path concern, not just
  a correctness one.

Until then the current architecture is adequate: it works, it's
understood, and the smells are documented so nobody trips over them
twice.
