# Home page — the composite doc/view

> **Scope:** the start page of the Enterprise window — one tab showing SEVERAL runtime forms
> at once, split in the proportions the configuration asks for. What it is made of, where the
> state lives, and what the composite adds that a normal form tab does not.
>
> Companions: [docview-fork.md](docview-fork.md) (the doc/view stack it is built on),
> [form-engine.md](form-engine.md) (how the forms it hosts are built, identified and shown),
> [main-frame.md](main-frame.md) (startup phases — where the tab is opened),
> [command-arc.md § 8](command-arc.md) (the config root as a surface owner),
> [metadata-tree.md](metadata-tree.md) (the designer tree the editor hangs off).

---

## 1. Why a composite

Every other document in the shell is one tab showing one thing: a form, an editor, a journal.
The start page is the exception the platform needs — a **showcase**: a sales funnel next to a
task list next to "create a document", all live, all at once, the first thing a user sees.

Nothing in the doc/view stack forbade it; the stack simply assumed *one document → one child
frame*. The composite is that assumption made **optional**, and nothing else:

```
ibHomePageDocument (ibDocument, tool tier — no metaobject)
  └── ibHomePageView (ibView) — its frame, divided by splitters into SECTIONS, a PANE per form
        ├── pane: ibFormVisualDocument (a CHILD doc) → ibFormVisualEditView → ibVisualHostClient
        ├── pane: …
        └── pane: …
```

An attached form is a **full runtime form**: its module runs, its events fire, its source
object is bound by its own metaobject. The composite contributes placement and nothing else —
which is why a form does not know, and does not need to know, that it is on the start page.

Files: `frontend/docView/templates/docViewHomePage.{h,cpp}` (runtime),
`backend/homePageDescription.{h,cpp}` (state), `designer/win/dlg/homePageEditor.{h,cpp}`
(designer).

---

## 2. The one primitive it needed — the parent houses the child

**The child lives on its parent, and the parent decides how to house it.** That sentence is
the whole mechanism, and `ibDocument` gained exactly one thing to say it:

```cpp
// A COMPOSITE document answers this for the children it lays out.
// Default null = "take a tab of your own", so nothing changes for an ordinary document.
virtual ibFrontendWindow* GetChildDocumentWindow(const ibDocument* child) const;
```

`ibDocument::OnCreate` then branches on **one question, asked through the doc parent**:

```cpp
ibFrontendWindow* const hostWindow = m_documentParent != nullptr
    ? m_documentParent->GetChildDocumentWindow(this) : nullptr;
hostWindow ? view->SetFrame(hostWindow) : CreateChildFrame(view, …);
```

Everything downstream — view creation, activation, modify tracking, the child-document
cascade — is untouched.

Note what is NOT there: the child carries **no state about where it lives**. It never receives
a window, never stores one, and cannot be "moved" — it asks its parent, which is the only one
that knows. The composition therefore lives entirely in the doc parent/child graph
([docview-fork.md § "Document parent/child lifecycle"](docview-fork.md)): the page's close
cascades into the forms, and the forms ask the page about everything below.

Two consequences worth naming:

- **The form's window IS a pane of the page's frame.** Nothing wraps it — what the splitter
  divides is the form host itself. `ibHomePageView` opens each form directly into the pane it
  is building (a placeholder panel appears only where a form is gone, since a splitter cannot
  hold a hole).
- **`ibFormVisualEditView::OnClose` does not destroy an embedded form's frame** — the pane
  belongs to the composite, and destroying it would tear a hole in the splitter tree.
- **One open verb.** `ibValueForm::ShowForm(docParent, createContext)` — the form states only
  whose child it is; where it lands follows. (`ibMetaDocument` derives from both
  `ibBackendMetaDocument` and `ibDocument`, so the two `ShowForm` overloads are equally good
  matches for it — the two call sites that pass one spell the cast out.)

---

## 2a. A composed form does not close itself — its parent closes it

The workspace is not a stack of tabs: a pane is a **slot the page owns**, and nothing in it may
leave a hole. The rule is one sentence — *a composed document cannot close itself, only its
parent may* — and it is asked through the same parent link the window came from:

```cpp
// ibDocument
virtual bool IsClosingChildren() const { return false; }   // the parent saying "it is me"
bool IsClosedByParent() const {
    return m_documentParent != nullptr && m_documentParent->IsClosingChildren();
}
```

**Where it is enforced matters.** `ibFormVisualEditView::OnClose` is the choke point every
teardown passes: the Close command, Save-and-close, a forced close from the object
(`NotifyDelete`), a sweep by the document manager. Refusing there covers all of them:

```cpp
if (composedDoc != nullptr && composedDoc->IsEmbedded() && !composedDoc->IsClosedByParent())
    return false;
```

Refusing only in `ibValueForm::CloseForm` (which is still done, as the near guard) covered the
BUTTON and nothing else — and a pane could still end up holding a header and no form, which is
exactly how the hole was found.

**Nothing else changes.** "Save and close" writes the object — the write happened before the
close verb was ever reached — and then simply does not close; `beforeClose` / `onClose` still
ran, so a script that cancels there stops the whole thing. Nothing is asked, nothing is
replaced: the same form stays where it was, now holding a saved object.

**The parent's own teardown** raises the flag in `ibHomePageDocument::Close()` — **before** the
base call, because `ibDocument::Close()` closes the children first and only reaches
`OnCloseDocument` at the end. A refused close lowers it again, so the panes are locked the
moment the page stays.

**The page itself is not closable by hand either** — `ibHomePageDocument::OnSaveModified()`,
the gate `CanClose` consults before any teardown, says no to everyone except the window closing
its own documents (§ 5).

---

## 3. The state — `ibHomePageDescription` on the config root

The workspace belongs to the **configuration**, not to a user setting: it is the one surface
every session opens before anything else, so it travels with the config.
`ibValueMetaObjectConfiguration` holds an `ibHomePageDescription`, serialised as one named
`HomePage` property (a Child node — add a field, old configs still load).

```cpp
enum ibHomePageTemplate { OneColumn, TwoEqualColumns, TwoColumnsWideLeft /*2:1*/, TwoColumnsWideRight /*1:2*/ };
struct ibHomePageItem { ibMetaID m_formId; unsigned int m_height; bool m_visible; };
// + two ordered columns of items
```

The description is deliberately **dumb** — a template plus, per column, an ordered list of
form ids. It holds no widgets and no runtime, so a headless or web host can read the same
description and lay the same forms out its own way.

- **`m_height` is a share, not pixels.** 0 means "split the column evenly with the other
  unset items" — that is what the designer's *same* height means.
- **A one-column template folds the right column into the left** on read
  (`GetShownItems`), so switching templates never loses attachments; it is reversible.
- **`m_visible == false` keeps the attachment and drops the cell** — the same
  greying-not-removing rule the command bar follows.

---

## 4. Layout — splitters, not a sizer

`GetShownItems` per column → a chain of `wxSplitterWindow`s:

| level | split | ratio |
|---|---|---|
| columns (two-column templates) | `SplitVertically` | `GetColumnGravity()` — 1:1 / 2:1 / 1:2 |
| within a column | `SplitHorizontally`, one splitter per pair | item weight ÷ remaining weight |

Splitters rather than a plain sizer for one reason: the sash is **draggable**, so a proportion
the configuration chose is a starting point, not a cage. `SetSashGravity` keeps the ratio
through window resizes; the initial sash position is applied **once**, on the first layout that
gives the splitter a real size (the tree is built before the frame is shown), and a user's drag
is never overwritten afterwards.

Each cell is a panel with a caption strip + the form's host. The caption exists because an
embedded form has no tab to carry its title.

---

## 5. Startup — created last, shown first

`ibFrontendMainFrame::Show()` gained one hook, at the end of the sequence:

```
CreateGUI()            → panes exist
EnsureRuntime()        → the session's runtime is up (forms can be built)
AllowRun()             → BeforeStart / OnStart (the script may open its own forms)
CreateStartupPage()     ← the window's OWN tabs — Enterprise opens the home page here
Show                   → the window comes up
```

**Created last, first on screen.** The start-up script runs on a bare window — a script that
vetoes `BeforeStart` should not have had a home page built for it — and the position is
settled separately: the page's tab is set to **`wxAuiTabKind::Locked`**
(`ibHomePageDocument::LockPageTab`). wx keeps locked tabs ahead of every normal one no matter
when they joined, refuses to drag them, and draws no close button on them. So "always first,
never movable, never closed by hand" is the notebook's own rule rather than a race against
`OnStart` plus a handful of vetoes. The `OnSaveModified` gate (§ 2a) stays behind it, for a
close arriving from anywhere but the window itself.

A configuration that attaches nothing gets **no tab at all** — an empty workspace should not
cost an empty tab.

`ibHomePageDocument::ShowHomePage()` is idempotent: a second caller (a script, a menu item)
activates the existing page instead of opening a second one.

---

## 6. Designer — the workspace editor

Right-click the configuration root → **Open home page workspace**. The seam is the one the
predefined-values editor uses: the metaobject asks
(`ibBackendMetadataTree::EditHomePage`), the designer owns the dialog — no GUI in the backend.

The dialog edits a **working copy** and commits on OK (so Cancel cancels), and marks the
metadata modified only when something actually changed. Per column: add (form picker tree),
height, delete, up / down, move to the other column; below: the template.

One rule it enforces:

- **A deleted form is not purged, it is shown.** The editor reads `<not found>`, the runtime
  cell reads *Form is not available*, and the other cells still open. Purging silently would
  lose the attachment on an undo.

---

## 7. Honest remainder

- **Desktop only today.** The description and the "which forms" decision are transport-neutral
  (backend), but the layout is `wxSplitterWindow` — `ibHomePageDocument` / `ibHomePageView` are
  built into `frontend.dll`, not `wfrontend.dll`. The web host can read the same description;
  it needs its own layout leaf, the same seam as the print renderer
  ([docview-fork.md § "Print seam"](docview-fork.md)).
- **The description is snapshotted at open.** The tab is a running layout of live forms;
  re-reading the description under it would invalidate the cells. Designer changes appear when
  the tab is reopened.
- **Every open builds its own form value — in the RUNTIME.** `CreateAndBuildForm` consults the
  compile cache first, but `CreateDesignerCache` returns null outside the designer
  (`metadataConfiguration.cpp`), so Enterprise never hits that branch: each cell, and each
  restart, gets a fresh form with a fresh source object. That is what makes "written → here is
  an empty one" true rather than "written → here it is again". In the DESIGNER the cache does
  hold one value per metaform — which is why nothing in this arc opens forms there.
- **Section-panel order and the rest of the root surface are NOT here.** The config root owns
  more than the start page ([command-arc.md § 8](command-arc.md)); this arc landed the start
  page only.
