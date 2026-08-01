# Form engine — from metaobject to pixels

> **Scope:** how a form comes into being and stays alive at RUNTIME — who builds it, what it is
> bound to, what its identity is, how it reaches a window, and how it goes away. The Designer's
> visual editor is a different subject: [form-editor.md](form-editor.md).
>
> Companions: [source-object.md](source-object.md) (what a form binds to),
> [form-attribute-binding.md](form-attribute-binding.md) (how a control reaches a value),
> [command-interface.md](command-interface.md) (the command door),
> [docview-fork.md](docview-fork.md) (the doc/view stack under it),
> [home-page.md](home-page.md) (a composite of several forms in one tab),
> [user-form-editor.md](user-form-editor.md) (the USER re-arranging an open form),
> [ARCHITECTURE.md](ARCHITECTURE.md) § Form Open (the one-screen call chain).
> This is a map of code that exists, not a plan.

---

## 1. Three different things are called "form"

Most confusion about this layer comes from one word meaning three things. They live in
different DLLs and have different lifetimes:

| # | thing | class | lives | lifetime |
|---|---|---|---|---|
| 1 | the form **METAOBJECT** — what the developer drew | `ibValueMetaObjectFormBase` (`backend/metaCollection/metaFormObject.h`) | backend, inside the configuration | as long as the config is open |
| 2 | the form **VALUE** — a live form: controls, attributes, its running module | `ibValueForm` (`frontend/visualView/ctrl/form.h`) | frontend | from open to close |
| 3 | the form **SURFACE** — the window the value is painted into | `ibVisualHostClient` + `ibFormVisualDocument` / `ibFormVisualEditView` | frontend | with the tab / pane |

(1) is metadata — a serialised control tree (`GetFormData()`), a module text and a form TYPE
(`GetTypeForm()`: object / list / select / folder …). (2) is a runtime value, an `ibValue` like
any other, which script code can hold. (3) is doc/view + widgets.

Two form metatypes exist, and the difference between them is the whole reason the build has a
fork in it:

- **`ibValueMetaObjectForm`** — a form OF a business object. Its parent metaobject is an
  `ibValueMetaObjectGenericData` (a Catalog, Document, Register, Report …).
- **`ibValueMetaObjectCommonForm`** — a form that belongs to nobody, filed at the config root.

---

## 2. Building one — one verb, answered by the kind

```cpp
// ibValueMetaObjectFormBase — the single entry, virtual
virtual ibBackendValueForm* GetObjectForm(ibBackendControlFrame* ownerControl = nullptr,
                                          const ibUniqueKey& formGuid = wxNullGuid) const = 0;
```

A caller never asks "which kind of form are you" and then casts to say it. Each kind answers:

| kind | what it does | source bound |
|---|---|---|
| common form | access right (`Use`), then `CreateAndBuildForm(this, ownerControl, **nullptr**, formGuid)` | none — it stands alone |
| object form | asks its OWNER: `owner->CreateObjectForm(this, formGuid)` | whatever the owner's `CreateSourceObject(metaForm)` gives |

**The owner is the one that knows what to bind**, and it decides by the form's TYPE — this is
`CreateSourceObject`, implemented per metatype (`catalogMetadata.cpp` and friends):

| form type | source the owner builds |
|---|---|
| object form | a **NEW object** (`CreateObjectValue(OBJECT_ITEM)`) |
| folder form | a new folder object |
| list form | the universal **dynamic list** over the object's queryable |
| select / folder-select form | the same list in choice mode |

So "open the list of Catalog1" and "create a Catalog1 item" are the same call with a different
form type; nothing in the caller is form-kind aware.

### The build itself

```
GetObjectForm(ownerControl, formGuid)
  └─ ibValueMetaObjectGenericData::CreateAndBuildForm(name, formType, ownerControl, srcObject, formGuid)
       └─ ibValueMetaObjectFormBase::CreateAndBuildForm(creator, formType, …)
            ├─ compile cache hit?  → the cached value (DESIGNER ONLY, § 3)
            └─ else:
                 ibBackendValueForm::CreateNewForm(creator, ownerControl, srcObject, formGuid)
                     → the session's frame builds `new ibValueForm(...)`   (frontend factory)
                 creator has form data?  → LoadFormData(blob)   : BuildForm(formType)  (default tree)
                 InitializeFormModule()          ← the form module compiles and runs
```

`CreateNewForm` goes through `ibSession::CurrentFrame()` — that is what keeps the backend
GUI-free and lets the web host build the same form value with its own frame.

A build error (a module that will not compile, a missing binding, no access right) is
**surfaced, not swallowed**: the exception is reported and the form is not returned.

---

## 3. Designer vs runtime — one cached value vs one per open

`ibMetaData::GetCompileCache()` is non-null **only in the Designer**
(`ibMetaDataConfiguration::CreateDesignerCache` returns null unless `appData->DesignerMode()`).
That single fact splits the behaviour of the whole layer:

| | Designer | Runtime (Enterprise / web) |
|---|---|---|
| form values per metaform | **one**, held by the compile cache | **one per open** — every call builds a fresh value with a fresh source |
| when it is built | lazily, via `ibDeferredForm` registered in `OnAfterRunMetaObject` (an eager build would need a root module manager that does not exist yet) | at the moment of the open |
| invalidation | `SaveFormData` (the editor commits) drops the cache entry so the next lookup rebuilds | — |
| the form key | the METAFORM's guid — the cache is keyed by metaform | see § 4 |

**This is why the same list can be opened twice at runtime and why the start page can hold two
copies of one form** — and why nothing in the runtime path may assume "the form of this
metaform" is a single object.

---

## 4. Identity — the form key

Every open form value carries a key, computed once:

```cpp
ibUniqueKey ibFormVisualDocument::CreateFormUniqueKey(ownerControl, sourceObject, formGuid)
{
    if (formGuid.isValid()) return formGuid;                 // 1. the caller said who I am
    if (ownerControl)       return ownerControl->GetControlGuid();  // 2. a form opened by a control
    if (sourceObject)       return sourceObject->GetGuid();   // 3. I am my source
    return wxNewUniqueGuid;                                   // 4. nothing to key on
}
```

Rule 3 is the ordinary case and the one that carries meaning: **a form IS its source**. It is
what makes "the object already has a window open" answerable, and it is why an object finds its
own form.

Who looks a form up, and by what:

| asks | function | keyed by |
|---|---|---|
| "is this thing already open? then just activate it" | `ibValueForm::CreateDocForm` gate → `FindDocByUniqueKey` | the FORM key |
| "where is MY form" — an object after a write / on Modify | `ibValueRecordDataObject::GetForm`, `Modify`, `Generate` → `FindFormBySourceUniqueKey` | the SOURCE's guid |
| "the form this control opened" | `FindFormByControlUniqueKey` | the owning control's guid |
| "re-stamp my key after a write" (register record set) | `UpdateFormUniqueKey` | the form key's guid |

**An object asks by SOURCE, not by form key.** The key is the form's own identity and a caller
may set it to anything; what never changes is that the form's source object is that object.
(Searching by key only worked while the key happened to fall back to the source — a
coincidence, and one that broke the moment a caller supplied a key of its own.)

**Supplying a key deliberately** is how an element gets an identity of its own — the start page
does it (`wxNewUniqueGuid` per attached form) so that two copies of one form can sit side by
side and so that opening the same list from the menu opens a real second one instead of
"activating" a pane the user cannot see.

---

## 5. Showing it — the open pipeline

```
ibValueForm::ShowForm(docParent, createContext = true)
  ├─ already have a visual document?      → just activate, done
  ├─ soft lock: srcData->TryAcquireFormLock()   (conflict = a caption badge, NOT a refusal —
  │                                             docs/record-locks.md)
  └─ CreateDocForm(docParent, createContext)
       ├─ "already open" gate (§ 4)  → activate that one instead
       ├─ script events: beforeOpen (may CANCEL), onOpen
       ├─ new ibFormVisualDocument(form)     ← registers in the open-form set; the key lives here
       ├─ docParent ? SetDocParent(docParent) : docManager->AddDocument(doc)
       └─ ibDocument::OnCreate
            ├─ DoCreateView()  → ibFormVisualEditView
            ├─ THE FRAME: composed by the parent (a home-page pane) or a child frame of its own
            │              — one question, asked through the doc parent (docview-fork.md)
            ├─ view->OnCreate  → new ibVisualHostClient(doc, form, frame)
            │                      └─ CreateAndUpdateVisualHost(): walks the ibValueFrame tree
            │                         and builds the real widgets under the host window
            └─ ShowFrame()
```

Two entries, one body: the backend contract `ShowForm(ibBackendMetaDocument*, bool)` is a type
step only (backend cannot name the doc/view types); the work is in
`ShowForm(ibDocument* docParent, bool createContext)`.

- **`createContext == false`** is the DESIGNER's preview: no script events, a demo document
  (`ibFormVisualDocumentDemo`), nothing bound.
- **`IsShown()`** is exactly "do I have a visual document" — not a window flag.
- The host is a `wxPanel` on desktop and `ibWebWindow` on web (`ibFrontendHostBase` typedef).
  **The form value and its control tree are the same code on both**; only the leaf that paints
  differs.

### 5a. The host: a facade and the window inside it

On desktop `ibVisualHost` is a FACADE, and the window that scrolls lives inside it:

```
ibVisualHost (wxPanel)          the facade — carries the form's CHROME (the command-bar
  │                             toolbar today, a search row later) and nothing else
  └ ibContentWindow             the inner window — HOLDS the controls (the ibValueFrame ->
      (wxScrolledCanvas)        wxObject index, their sizer) and scrolls them
```

The facade fills itself and hands everything about the controls down: `CreateVisualHost` builds
the chrome layers, then calls `CreateContent`; `UpdateVisualHost` refreshes the layers, then
`UpdateContent`; `ClearVisualHost` drops the layers, then `ClearContent`. Lookups
(`GetObjectBase` / `GetWxObject`) and the four control verbs are forwards. The per-control hooks
(`Create` / `OnCreated` / `Update` / `OnUpdated` / `Cleanup`) stay on the facade — that is where
the designer overrides them, so both hosts keep the behaviour they had.

**Why:** the toolbar is not in the scrolling window at all, so it no longer moves with the wheel.

Two windows per host, named by the same pair of methods every host answers:

| | `GetParentBackgroundWindow()` (chrome) | `GetBackgroundWindow()` (controls) |
|---|---|---|
| runtime (`ibVisualHostClient`) | the facade — toolbar stays put | the inner window |
| designer (`ibVisualEditorHost`) | the card's content panel — the toolbar scrolls WITH the card, because the card IS the form being drawn | the same panel |

`InitMainSizer()` decides where the chrome sizer lands from that pair and runs lazily on the
first build — a concrete host never calls it. `UpdateHostSize()` is the heavy pass, run once at
the end of an update (facade layout + repaint; wx recurses from there); `UpdateVirtualSize()` is
the cheap one (the inner window's scroll range, which follows what the controls actually need —
so scrollbars appear only when the form does not fit).

Traps worth keeping in mind when touching this: the form's colour has to reach BOTH windows (the
chrome reads its look off its parent); the chrome layers are deleted outright and BEFORE the
content (a deferred `Destroy()` would be freed twice by a host whose chrome shares the controls'
window); and `ClearContent` asks who owns the controls' sizer before dropping it.

---

## 6. Living and dying

| verb | what happens |
|---|---|
| `UpdateForm()` | re-reads values into the widgets (the view's `OnUpdate` path) |
| `Modify(bool)` | marks the document dirty (drives the tab's asterisk and the close prompt) |
| a WRITE | the object commits, then calls back `NotifyCreate` / `NotifyChange` on the form it was given — that is what refreshes the form and clears its dirty flag |
| `CloseForm(force)` | `beforeClose` (may CANCEL) → `onClose` → the document's views are deleted **deferred** (`CallAfter`) — deleting them inline would free the toolbar that is still dispatching the click |
| a COMPOSED form | cannot close at all: the window is its parent's, so the close is suppressed ([home-page.md § 2a](home-page.md)) |
| the document dies | releases the form value's refcount; child documents cascade with it |

---

## 7. Where the pieces meet

- **What a form is bound to** — one source object per form, reached by hops; a control binds a
  path into it. [source-object.md](source-object.md),
  [form-attribute-binding.md](form-attribute-binding.md).
- **What a form can DO** — standard commands from the source, its own form commands, config
  commands; all resolved through one hop walk. [command-interface.md](command-interface.md).
- **Where a form is drawn** — the doc/view fork, shared by desktop and web, including
  composition (one document laying out its children). [docview-fork.md](docview-fork.md).
- **Several forms in one tab** — the start page. [home-page.md](home-page.md).
- **How a form is EDITED** — the Designer's visual editor, a different engine over the same
  metaobject. [form-editor.md](form-editor.md).

---

## 8. Honest remainder

- ⚠ **A dynamic list's identity is its TABLE.** `ibValueDynamicList::GetGuid()` returns the
  queryable's table guid, so every list over the same object shares one source identity. Two
  list forms of the same catalog are therefore indistinguishable to `FindFormBySourceUniqueKey`
  — it answers with the first. The start page works around it with per-element form keys; the
  real fix is an instance identity on the list itself, and it is not done.
- ⚠ **`UpdateFormUniqueKey` searches by the FORM key**, so a form that was given a key of its
  own (a start-page element) is not re-stamped after a register write. Harmless today — the
  register finds its form by source — but it is the one lookup left on the old assumption.
- **The template path does not ask about composition.** `ibDocTemplate::CreateView` creates its
  child frame directly, so a TEMPLATED document (journal, text, help) opened as a child of a
  composite still takes a tab. Forms do not go that way, so nothing is broken today.
- **A form module error at build time** returns no form. The caller sees null, the user sees the
  message — but there is no "broken form" placeholder anywhere except the start page's own pane.
