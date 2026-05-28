# Doc/View fork — one doc/view stack for desktop and web

> **Status:** landed in working tree, builds clean Debug|x86. Replaces
> the previous dependency on `wxDocManager` / `wxDocTemplate` /
> `wxDocument` / `wxView` with an in-tree fork generalised over
> `ibFrontendWindow`, so the same doc/view code drives both the
> desktop (`wxWindow`) and the web (`ibWebWindow`) frontends.
>
> Related: [`backend-frontend-split.md`](backend-frontend-split.md)
> (the `ibBackendDocFrame` interface this sits behind),
> `ARCHITECTURE.md` §Form System. Memory: [[project_doc_frame_any]],
> [[project_mdi_rename]], [[reference_template_creates_child_frame]].

---

## Why fork wx doc/view

The desktop UI was built on `wxDocManager` + `wxDocTemplate` +
`wxDocument` / `wxView`. The web frontend (`wfrontend.dll`,
`OES_USE_WEB`) renders into `ibWebWindow`, not `wxWindow`, so it could
not reuse any of that machinery — every doc/view concept had a
desktop-only `wxWindow` baked into its signature.

The fork copies wx's doc/view subsystem into
`frontend/docView/docView.{h,cpp}` with `wx*` → `ib*` renames and one
structural change: **every window-typed member and return value is
`ibFrontendWindow`** (`typedef wxWindow` on desktop, `ibWebWindow` on
web — see `frontend/frontendTypes.h`). The frame classes are
template-mixins parametrised on the concrete frame type, so they
instantiate against both transports from one source.

Origin of the copied code: wx doc/view by Julian Smart, wxWindows
licence — the attribution header stays at the top of `docView.h`.

---

## Layered shape

`docView.h` is one header in two halves:

**Forked base (transport-agnostic):**

| Type | Role |
|---|---|
| `ibDocument` | Document base (was `wxDocument`). Owns `m_childDocuments`; `IsChildDocument()` virtual; `OnCreate` / `DoOpenDocument` / `DoSaveDocument` / `IsModified` / `Modify`. |
| `ibView` | View base (was `wxView`). Owns `m_viewFrame : ibFrontendWindow*` + `GetFrame` / `SetFrame`; `OnCreate(ibDocument*, long)` / `OnUpdate` / `OnDraw` / `OnClose`. |
| `ibDocTemplate` | Plain file template (path/ext/filter keyed). `CreateDocument` / `CreateView` / `InitDocument`. Lives in `docManager.h` next to its meta subclass. |
| `ibDocManager` | Manager (was `wxDocManager`). Holds `m_templates`; the OES meta-template API (`AddDocTemplate(ibClassID, …)`, `OpenForm`, CLSID lookup) is part of this class. Declared in `docView.h` next to the base it forks. |
| `ibDocChildFrameAny<ChildFrame,ParentFrame>` / `ibDocChildFrameAnyBase` | Child-frame mixin over `ibFrontendWindow`. |
| `ibDocParentFrameAny<BaseFrame>` / `ibDocParentFrameAnyBase` | Parent/shell-frame mixin over `ibFrontendWindow`. |
| `ibDocPrintout` | Print adapter (`wxPrintout`). |

**OES adapter (metadata-aware):**

| Type | Role |
|---|---|
| `ibMetaDocument : ibBackendMetaDocument, ibDocument` | Metaobject-backed document. Adds `DoCreateView() → ibMetaView*`, `m_childDoc`, typed child-doc views. |
| `ibMetaDataDocument : ibMetaDocument` | Document carrying an `ibMetaData*` payload. |
| `ibValueModuleDocument : ibMetaDocument` | Module/form-bearing document base. |
| `ibMetaView : ibView` | View paired with `ibMetaDocument` (`OnCreate(ibMetaDocument*, long)`). |
| `ibMetaDocTemplate : ibDocTemplate` | Metadata template — adds CLSID + per-template GUID + icon for the "choose template" dialog. Registered via the `AddDocTemplate(ibClassID/ibPictureID, …)` overloads; lives in the same `m_templates` list as plain templates, found by iterating + `dynamic_cast`. |

Was previously split as `ibDocView.{h,cpp}` + `docView.{h,cpp}`;
collapsed into a single `docView.{h,cpp}` pair so both layers review
side by side. `docViewCmd.cpp` (the old command-processor TU) is
removed and dropped from `frontend.vcxproj` / `wfrontend.vcxproj`.

---

## Two document tiers

| Tier | Base | Examples | View base |
|---|---|---|---|
| **Meta documents** — backed by a metaobject | `ibMetaDocument` (and subclasses) | Catalog / Document / Form / Module / Role / Interface / Metadata editors | `ibMetaView` |
| **Tool documents** — no metaobject, just a UI tab | plain `ibDocument` | Audit log, Text, Help, **Configuration Compare** | plain `ibView` |

Tool documents are the lightweight pattern: trivial
`IsModified()→false` / `Modify(){}` / `DoOpen`/`DoSave` no-ops, no
`m_metaData`, no `DoCreateView` override — the view is built from the
template's registered `viewClassInfo`. New tool tabs should mirror
`ibAuditLogDocument` / `ibAuditLogView`.

**`CreateChildFrame` lives on the template, not the document.** It was
lifted out of `ibMetaDocument::OnCreate` into `ibDocTemplate::CreateView`
so non-meta documents (AuditLog / Text / Help / ConfigCompare) get
their MDI child frame through the same path as meta documents. See
[[reference_template_creates_child_frame]].

---

## Naming: the `e→ib` de-corruption

A historical global replacement had corrupted the trailing `e` of
several document class stems to `ib` (`File→Filib`, `Module→Modulib`,
`Interface→Interfacib`, `Role→Rolib`), and the corrupted forms were
committed. They compiled because declaration and use were corrupted
consistently. This arc restored the canonical names across the
doc/view templates:

| Corrupted | Canonical |
|---|---|
| `ibReportFilibDocument`, `ibDataProcessorFilibDocument`, `ibMetadataFilibDocument`, `ibHelpFilibDocument`, `ibTextFilibDocument` | `…FileDocument` |
| `ibValueModulibDocument`, `ibModulibDocument` | `ibValueModuleDocument`, `ibModuleDocument` |
| `ibInterfacibDocument` | `ibInterfaceDocument` |
| `ibRolibDocument` | `ibRoleDocument` |

No `Filib` / `Modulib` / `Interfacib` / `Rolib` token remains in
`src/engine`.

---

## Configuration Compare → tool tab

`ibConfigCompareDocument` / `ibConfigCompareView` were rebased from the
meta tier (`ibMetaDocument` / `ibMetaView`) down to the tool tier
(`ibDocument` / `ibView`) — the compare tab is driven entirely by its
diff model + two non-owning root pointers and never touches metadata
through the document, so the meta base bought nothing. The view is now
constructed from the registered `CLASSINFO(ibConfigCompareView)`
template entry rather than a document-side `DoCreateView`. Behaviour is
unchanged; see [`configuration-compare.md`](configuration-compare.md).

---

## Document parent/child lifecycle (OES extension)

Stock wx `wxDocument` has **no** parent/child document hierarchy. OES
added one on the forked `ibDocument` and it is load-bearing — it is how
a dependent document dies with the one that opened it.

`ibDocument` owns two protected members (the old `wxDList`-based shadow
fields `m_documentParent` / `m_childDocs` are gone):

```cpp
ibDocument*            m_documentParent;   // who opened me (nullptr = top-level)
std::list<ibDocument*> m_childDocuments;   // docs I opened
```

**Binding.**
- `ibDocument(ibDocument* docParent)` ctor pushes `this` into
  `docParent->m_childDocuments` and sets `m_documentParent`.
- `SetDocParent(ibDocument*)` re-parents at runtime (removes from the
  old parent's list, appends to the new).
- `ibMetaDocument(ibMetaDocument* docParent)` forwards to the base, so
  every metadata document can be opened owned-by-caller.

**Cascade close.** `~ibDocument`:
1. removes itself from `m_documentParent->m_childDocuments`;
2. closes its own children — first runs the `OnClose` veto pass over
   all of `m_childDocuments`, then pops and closes them (the child's
   own dtor removes it from the list, so the loop makes progress).

**`IsChildDocument()`** — base returns `m_documentParent != nullptr`;
`ibMetaDocument` overrides it to the `m_childDoc` flag so a metadata
doc can be flagged child (for the save/close-prompt policy) regardless
of whether it currently has a parent pointer.

**Where this shows up — selection mode.** Opening a form in selection
mode (a chooser invoked from another form) creates the chooser document
with the **calling document as `docParent`**. It is therefore registered
as a child; when the owner form closes, its dtor cascade-closes the
still-open chooser with it. No manual "close the picker if its owner
went away" bookkeeping at the call site — the doc graph enforces it.

This parent/child graph is one of the OES extensions that make the fork
worth owning: it is doc/view-level lifecycle the stock wx classes do
not provide.

---

## Print seam — desktop today, web next

**Desktop path (in tree).**

```
ibDocManager::OnPrint(wxCommandEvent)        // print command
   └─ ibView::OnPrint(wxDC*, wxObject*) / OnDraw(wxDC*)
        └─ ibDocPrintout : wxPrintout        // OnPrintPage(int)
             └─ wxPrinter / wxPrintPreview
```

A **report** prints through its output `ibSpreadsheetDocument`
(`ibMetaDocument` subclass) — the spreadsheet model is the printable
artifact, and it is **transport-neutral** (no `wxDC` in the model
itself). That model is the natural cut line for a second transport.

**Planned: print a report directly from the web.** The doc/view-level
print *request* is already transport-neutral (it lives on
`ibView`/`ibDocManager`); only the *renderer* is desktop-bound. The
constraint to respect:

> `ibDocPrintout` is `: public wxPrintout` — a desktop GUI type. The
> web build (`wfrontend`) must **not** derive from it.

Clean seam (same shape as `ibFrontendWindow`): introduce a thin,
transport-neutral interface — e.g. `ibPrintable` / `ibPrintJob`
("produce a printable representation of this document") — and provide
two implementations:

| Transport | Renderer | Output |
|---|---|---|
| Desktop | `wxPrintout` wrapper (current `ibDocPrintout`) | physical printer / `wxPrintPreview` |
| Web | web printout | server-rendered PDF or HTML → browser print dialog / download |

The report's `ibSpreadsheetDocument` feeds both renderers unchanged.
Result: adding web report printing = one new renderer class + a route
(download / preview); **the doc/view core is not touched**. This is the
payoff of generalising the fork over `ibFrontendWindow` rather than
keeping a desktop-only doc/view. (Design note — web renderer not yet
implemented.)

`ibView::OnCreatePrintout()` is the desktop hook today — it returns
`new ibDocPrintout(this)`. That factory is the single place a web build
would override to hand back its own printable instead.

---

## Active-view UI contribution: toolbar + submenu (OES extension)

Doc/view principle: the shell frame shows the menu and toolbar of the
**currently active view**, and swaps them when the active child frame
changes. Stock wx does only the thin part of this (enable/disable via
`wxUpdateUIEvent`, MRU into the File menu). OES built the rest on the
fork — per-view toolbar **and** per-view (sub)menu contribution.

**Per-view hooks** (`ibMetaView`):

```cpp
virtual wxMenuBar* CreateMenuBar() const         { return nullptr; }  // view's own menubar
virtual void       OnCreateToolbar(wxAuiToolBar*) {}                  // view's toolbar buttons
virtual void       OnActivateView(bool activate, ibView* active, ibView* deactive) override;
virtual void       Activate(bool activate) override;
```

A concrete editor view fills `CreateMenuBar()` / `OnCreateToolbar()`
with its own commands; views that need no chrome leave the defaults.

**Activation chain** — who triggers the swap:

```
child frame gets focus
  └─ ibDocChildFrameAny::OnActivate  (bound to wxEVT_ACTIVATE)
       └─ m_childView->Activate(active)
            └─ ibView::Activate:
                 OnActivateView(active, this, docManager->GetCurrentView())
                 docManager->ActivateView(this, active)
```

So switching MDI tabs deactivates the old view's chrome and activates
the new view's — the active document's menu/toolbar are what the shell
shows.

**Toolbar** is a `wxAuiToolBar` (desktop AUI); each view contributes
its buttons through `OnCreateToolbar`. Button enabled-state is driven
off the same machinery as everything else — the active document's
command processor (undo/redo) and dirty flag (Save) via UI-update — so
toolbar / menu / undo / dirty are one coupled system, not parallel
ones.

**Submenu merge — the MSW gotcha.** `ibAuiDocChildFrame::SetMenuBar`
clones the view's menu into the child-frame's merged menu bar
(`ibProcSubMenu::ConstructMenu`, recursive). Submenus must be attached
via `wxMenu::AppendSubMenu` — routing them through `Append()` +
`SetSubMenu()` on a `wxITEM_NORMAL` item leaves the submenu
**disconnected on MSW**, so its contents vanish from the merged bar
(symptom seen: "Start debugging ▸ Web client" missing after entering
the form editor). Item bitmap / enabled / `SetMarginWidth` (MSW) are
copied per item. See memory [[submenu-clone]].

**Transport relevance.** "Active view contributes its commands" is
model-level and transport-neutral; only the leaf differs — `wxAuiToolBar`
+ merged `wxMenuBar` on desktop, an HTML menu/toolbar on web when it
reaches parity. Same generalisation seam as `ibFrontendWindow`: the
contribution logic is shared, the rendering is per-transport.
