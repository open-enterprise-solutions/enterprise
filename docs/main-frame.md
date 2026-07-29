# Main frame — one base, two applications, and the startup phases

> **Scope:** the main window — the backend contract, the shared frontend base, the Designer
> and Enterprise subclasses — and **when metadata is initialised** relative to it.
> Companions: [ARCHITECTURE.md](ARCHITECTURE.md) (run modes, bootstrap),
> [docview-fork.md](docview-fork.md), [session-registry.md](session-registry.md),
> [compiler-pipeline.md § 6](compiler-pipeline.md) (runtime assembly).
> This is foundation code.

---

## 1. Three layers

```
ibBackendDocFrame                 backend/backend_mainFrame.h   — the CONTRACT (no wx GUI)
        ▲
ibFrontendMainFrame               frontend/mainFrame/mainFrame.h — the SHARED base
        ├── ibFrontendMainFrameDesigner     designer/mainFrame/    — the IDE window
        └── ibFrontendMainFrameEnterprise   enterprise/mainFrame/  — the runtime window
```

Same shape as everything else in this engine: **backend declares, frontend implements,
each application specialises** ([designer-editors.md § 1](designer-editors.md)).

```cpp
class FRONTEND_API ibFrontendMainFrame :
    public ibBackendDocFrame,        // the contract
    public wxAuiMDIParentFrame,      // the window
    public ibDocParentFrameAnyBase   // doc/view host
{
    virtual void CreateGUI() = 0;    // ← each application builds its own
};
```

`CreateGUI()` is pure — the base owns *behaviour* (sessions, forms, metadata lookup,
errors), the subclass owns *layout*. Designer gets the metadata tree + editors; Enterprise
gets the interfaces sidebar + user forms.

Both subclasses add the same two things and little else: a `static GetFrame()` accessor and
an output window (`Message` / `ClearMessage` / `BackendError` → `m_outputWindow`).

---

## 2. The frame belongs to the session, not the process

The contract's header opens with the rule and the four ways to reach a frame:

> **The frame is not a process-level singleton — it belongs to `ibSession`.** Every caller
> reaches its frame through a session pointer available in its own scope:
>
> - **Runtime callers** (script exec, error reporter): walk up the current ProcUnit to
>   `ibProcUnitRoot` → session → frame.
> - **Object-method callers** (`ibRuntimeModuleDataObject` subclasses):
>   `GetSession()->GetFrame()` (`GetSession` walks the parent chain).
> - **Process-lifecycle callers** (metadata config hooks, property dtor):
>   `appData->GetMainSession()->GetFrame()`.
> - **Web per-tab**: `ibWebSession::Session()->GetFrame()`.

That is what makes `wenterprise-server.exe` possible: N per-cookie sessions in one process,
each with its own frame ([ARCHITECTURE.md](ARCHITECTURE.md)). A process-wide `theMainFrame`
would have made the web host unbuildable.

The contract itself is now GUI-free, with no exception left:

```cpp
class BACKEND_API ibBackendDocFrame {
    virtual ibSession* GetSession() const { return nullptr; }   // default: headless / pre-session
    virtual ibMetaData* FindMetadataByPath(const wxString& strFileName) const { return nullptr; }
    virtual void BackendError(const wxString& file, const wxString& docPath, long line, const wxString& msg) const {}
    virtual ibBackendValueForm* ActiveWindow() const { return nullptr; }
    virtual ibBackendValueForm* CreateNewForm(const ibValueMetaObjectFormBase* creator,
                                              ibBackendControlFrame* ownerControl = nullptr,
                                              ibSourceDataObject* srcObject = nullptr,
                                              const ibUniqueKey& formGuid = wxNullUniqueKey) { return nullptr; }
    …
};
```

Every method has a **safe default** (`nullptr` / no-op) — a headless run has no frame, and
backend code calling these must simply get nothing back rather than crash.

> **`GetFrameHandler()` is gone.** It was the one method here that named a widget
> (`virtual wxFrame*`), and it turned out **nobody called it**: the desktop returned
> `s_instance`, `ibWebFrame` returned `nullptr` with a comment telling callers to guard —
> callers that never existed. Third dead hook of its kind, after `RefreshPGProperty` and
> `OnEventRefresh` ([property-system.md § 4](property-system.md)), and the pattern is worth
> naming: **where the core handed a UI type outward, the need had usually already gone.**
> The obligation outlived its purpose, not the other way round. If a native parent is ever
> needed again, it belongs on the frontend side of this interface, not in its signature.

One session link on the frame, and it is ownership — see
[session-ownership.md](session-ownership.md):

```cpp
// ibBackendDocFrame: no default ctor — a frame is always built around a session
explicit ibBackendDocFrame(ibSessionHolder&& holder);
ibSession* GetSession() const;          // answers out of the holder
```

The frame OWNS the session: releasing the holder in `~ibBackendDocFrame`
is what ends it. The old pair of links (`m_session` mirror plus
`SetGUISession` / `GetGUISession`, wired by `ibGUISession::AttachFrame`)
is gone — the desktop session answers `GetFrame()` from the window
singleton instead of storing a pointer.

---

## 3. Startup phases — where metadata initialisation sits

This is the part that is easy to get wrong, and the code is explicit about the ordering:

```
1.  holder = appData->CreateSession<ibGUISession>()   ← session row, ownership in hand
2.  holder->Open(user, pwd)                           ← credentials (standalone dialog,
                                                        no window needed yet)
3.  LoadMetadata()                                    ← fires from the registry's
                                                        OnFirstConnect during Open
4.  new ibFrontendMainFrameEnterprise(std::move(holder))   ← the window takes the session
5.  frame->Show()                                     ← CreateGUI → EnsureRuntime → AllowRun
```

There is no separate bind step: the frame cannot exist without a session,
so `GetSession()` is valid from its first line. `Initialize(session)` and
the `m_session` mirror it filled are gone.

**Order inside `Show()` matters.** `CreateGUI()` runs *first*, before
`EnsureRuntime()` and before `AllowRun()`, because the gate reaches into
panes: enterprise fires `BeforeStart`/`OnStart` (a startup script may
open forms) and the Designer loads its metadata tree into `m_metaWindow`.
Asking before building dereferences panes that do not exist yet — that
was a live crash during the ownership arc.

**Why runtime start is deferred to `Show()`:** the runtime needs `activeMetaData` populated,
and the load happens during `Open` (registry's `OnFirstConnect`).

```cpp
bool ibFrontendMainFrame::EnsureRuntime()
{
    ibSession* session = GetSession();          // from the holder — no bind step
    if (session == nullptr || activeMetaData == nullptr)
        return false;

    // Re-entry guard — root module manager lives on the session; if it's
    // already installed the runtime was started on a previous Show().
    if (session->GetManagerModule() != nullptr)
        return true;

    const ibSessionKind kind = session->GetKind();
    const bool wantsRuntime =
        (kind == ibSessionKind::Enterprise) ||
        (kind == ibSessionKind::WebClient)  ||
        (kind == ibSessionKind::Service);
    if (!wantsRuntime)
        return true;                       // ← Designer takes this exit

    if (auto* mm = session->GetManagerModule())
        mm->AttachRuntime(session);
    return true;
}
```

- **`Show()` can happen many times** — hence the re-entry guard. Runtime start is once per
  session.
- **The Designer opts out by kind**, not by a flag: `ibSessionKind::Designer` is not in
  `wantsRuntime`, so the IDE compiles but never attaches a runtime
  ([ARCHITECTURE.md](ARCHITECTURE.md) § "Designer — compile only",
  [module-manager-split.md](module-manager-split.md)).

---

## 3a. Closing — one road, two entrances

The window's close handler is the whole sequence, and both roads run it:

```
[X]                → wxEVT_CLOSE ─┐
session->Close(f)  → frame->Close(f) ─┴→ OnCloseWindow
                                          force = !event.CanVeto()
                                          ├ !force && !AllowClose() → Veto, nothing happened
                                          └ Skip → wx destroys the window
                                              └ holder released → session ends
```

`AllowClose()` takes **no** force parameter — "don't ask" is expressed by
not asking, so a non-vetoable close skips it entirely. Soft close asks in
two steps: open documents first (each may refuse), then `ExitMainModule()`
on the session's runtime (`BeforeExit` may cancel, `OnExit` runs). The
enterprise window adds the second step; the Designer adds its
unsaved-configuration prompt instead — it has no runtime.

`Destroy()` closes whatever is still open unconditionally: it is past the
point of no return, where a refusal could not be honoured.

Full picture, including the web and headless owners:
[session-ownership.md](session-ownership.md).

---

## 4. Honest remainder

- ⚠ **`EnsureRuntime`'s `AttachRuntime` call looks unreachable.** Line ~143 returns early
  when `GetManagerModule() != nullptr`; line ~156 then does
  `if (auto* mm = m_session->GetManagerModule()) mm->AttachRuntime(m_session);` — which can
  only be entered when the manager is **non-null**, i.e. exactly the case that already
  returned. So either the guard or the attach is dead code. The adjacent comment
  (*"CreateRoot + CompileRoot already happened in OnRun after LoadMetadata — frame->Initialize
  is the runtime-start phase, only InitRuntime here"*) suggests the attach may indeed be
  redundant, and the sessions work — but the two statements contradict each other, and one
  of them is wrong. **Worth a deliberate read before touching session startup.**
- **`wxAuiMDIParentFrame`** is the base, i.e. an AUI *MDI* parent — while the product
  direction is explicitly non-MDI. Historical; the AUI MDI parent is used as a docking host
  rather than for MDI child semantics, but the type is what it is.
- The two subclasses duplicate `Message` / `ClearMessage` / `BackendError` over their own
  `m_outputWindow` (Designer's are non-virtual, Enterprise's are `virtual`). A small
  candidate for lifting into the base.
