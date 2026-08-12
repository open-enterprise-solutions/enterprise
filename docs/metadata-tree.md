# Metadata tree — the Designer's navigator, and how objects open

> **Scope:** the tree on the left of the Designer, the backend contract behind it, and how
> opening a metaobject (or an **external** report / data processor) turns into a document.
> Companions: [docview-fork.md](docview-fork.md) (the doc/view stack),
> [property-system.md](property-system.md) (what the inspector shows for the selected node),
> [report-engine.md](report-engine.md).
> This document describes code that **already exists**; it is a map, not a plan.

---

## 1. The contract lives in the backend

`backend/backend_metatree.h` declares `ibBackendMetadataTree` — a pure-virtual contract
with **no UI in it**:

```cpp
class BACKEND_API ibBackendMetadataTree {
    virtual ibFormID SelectFormType(ibValueMetaObjectForm* metaObject) const = 0;
    virtual void Activate() = 0;
    virtual void SetReadOnly(bool readOnly = true) = 0;
    virtual bool IsEditable() const = 0;
    virtual void Modify(bool modify) = 0;
    virtual void EditModule(const ibGuid& moduleName, int lineNumber, bool setRunLine = true) = 0;
    virtual bool OpenObjectForm(ibValueMetaObject* obj) = 0;
    virtual bool OpenObjectForm(ibValueMetaObject* obj, ibBackendMetaDocument*& foundedDoc) = 0;
    virtual bool CloseObjectForm(ibValueMetaObject* obj) = 0;
    virtual void EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* obj) = 0;
    virtual ibBackendMetaDocument* GetDocument(ibValueMetaObject* obj) const = 0;
    virtual bool RenameMetaObject(ibValueMetaObject* obj, const wxString& strNewName) = 0;
    virtual void CloseMetaObject(ibValueMetaObject* obj) = 0;
    virtual void UpdateChoiceSelection() {}
};
```

**Why it is in the backend at all:** the metadata layer needs to *drive* the navigator —
a metaobject that is renamed, deleted, or reloaded must close/refresh its open document,
and a compile error must jump the editor to a line (`EditModule`). Backend cannot include
wx GUI headers, so it names the capability and the Designer supplies it. This is the same
shape as the property system's function-pointer slots
([property-system.md § 4](property-system.md)) and the spreadsheet's notifier
([report-engine.md § 3](report-engine.md)): **backend declares, frontend implements.**

The Designer's side is `ibMetaTreeBase` (`designer/mainFrame/metaTree/treeConfiguration.h`):

```cpp
class ibMetaTreeBase : public wxPanel, public ibBackendMetadataTree { … };
```

— a wxPanel *and* the contract. Note it is a **panel, not a tree control**: the actual
`wxTreeCtrl` is a nested member class (`ibMetaTreeCtrl`), so the panel can carry a search
box (`m_searchTree`) alongside it.

---

## 2. What the tree opens — three cases, one navigator

The metadata tree is **the backbone of working with metadata**, and it serves three
different things:

1. **A standalone file** — open an `.erf` / data-processor file and the tree simply renders
   *that* container's metadata.
2. **A part of a configuration** — the same rendering, nested inside the config tree.
3. **A database configuration** — the configuration stored in the base, which is the
   Designer's normal case.

The important consequence: **the tree is shared with external (common) data processors**.
It is not "the configuration tree, plus two special editors" — it is one representation of
*any* metadata container, and a config just happens to be the biggest one.

## 2.1 Three trees, one base

| Class | File | Edits |
|---|---|---|
| `ibConfigurationTree` | `treeConfiguration.{h,cpp,_impl.cpp}` + `treeConfigurationEvent.cpp` | the whole configuration |
| `ibDataReportTree` | `treeDataReport.{h,cpp,_impl.cpp}` + `treeDataReportEvent.cpp` | one **external report** (`.erf`) |
| `ibDataProcessorTree` | `treeDataProcessor.{h,cpp,_impl.cpp}` + `treeDataProcessorEvent.cpp` | one **external data processor** |

All three derive from `ibMetaTreeBase` — the panel + contract from §1. **Renamed 2026-08-12**
(`ibMetaDataTree` → `ibMetaTreeBase`, `ibMetadataTree` → `ibConfigurationTree`): the base and the
configuration tree used to differ by one letter's case, in the same header, and a typo in either
direction compiled. This is [restructure-plan.md § A1](restructure-plan.md), done.

Each also owns its own nested `wxTreeCtrl` subclass (`ibMetaTreeCtrl` /
`ibDataReportTreeCtrl` / `ibDataProcessorTreeCtrl`).

The consistent 4-file split per tree is worth naming, since it repeats:

- `*.h` / `*.cpp` — the tree class, wiring, lifetime
- `*_impl.cpp` — **building the node structure** (which children a metaobject shows:
  attribute list, tabular sections, forms, templates, dimensions, resources)
- `*Event.cpp` — event handling (selection, menu, drag)

An external report / processor is a **self-contained metadata container**, so it gets a
full navigator of its own rather than a branch inside the configuration tree.

**A read-only tree refuses a drag at its START** (`OnBeginDrag`, `treeConfigurationEvent.cpp`).
`IsEditable()` (= `!m_bReadOnly`, the flag `SetReadOnly` sets — §1) is what the New / Delete /
Up / Down toolbar buttons and the Paste handler already ask. `OnEndDrag` asked it too, but
`OnBeginDrag` did not (fixed 2026-07-30): the node followed the cursor and targets lit up as
accepting, and only the landing was refused — silently. Same rule as the form editor's trees
([form-editor.md § 6.1](form-editor.md)).

---

## 3. Node payload — two node kinds

`backend/backend_metatree.h`, in the protected section of the contract:

```cpp
struct ibTreeData { bool m_expanded = false; };

struct ibTreeDataClassIdentifier : ibTreeData {   // a KIND folder — "Catalogs", "Documents"
    ibClassID m_clsid;                            // element type
};

struct ibTreeDataMetaItem : ibTreeData {          // a concrete metaobject — "Catalog.Goods"
    ibValueMetaObject* m_metaObject;              // element type
};
```

Every node is one of two things: a **folder standing for a class kind** (keyed by
`ibClassID`) or a **live metaobject**. Nothing else. Because the clsid is kind-typed (the
high byte is the `ibClassKind` — see [../CLAUDE.md](../CLAUDE.md) §6), a folder node
classifies itself with no `ibMetaData` lookup.

`m_expanded` lives on the base so expansion state survives a rebuild.

### 3.1 The layout is one table (2026-08-12)

The tree is a **presentation**, and its layout is written down — but in **one place**, as data:
`s_groups` in `treeConfiguration_impl.cpp`. One row per group node, and the row is the whole
truth about it:

```cpp
struct ibMetaTreeGroupDef {
    ibClassID   m_clsid;   // the metatype it stands for — icon, "New", context menu all follow from it
    const char* m_label;   // wxTRANSLATE-marked source string, translated when the node is made
    ibMetaBand  m_band;    // Common / Metadata — the two bands of the navigator
    ibClassID   m_owner;   // 0 = straight in the band; otherwise the group it nests under
    ibMetaRow   m_row;     // how a member is put in: Item / Group / Command
};
```

**The order of the table is the order on screen.** Before this there were three orders: the
sequence of `AppendGroupItem` calls in `InitTree`, the sequence of twenty fill loops in
`FillData`, and a `DeleteChildren` list in `ClearTree` that had silently fallen three entries
behind (session parameters, common attributes, languages) — invisible, because that block ran
immediately before `DeleteAllItems` and therefore did nothing at all.

`InitTree`, `FillData` and `ClearTree` are now one pass each over that table, and the group nodes
live in `std::map<ibClassID, wxTreeItemId> m_groups` — the storage and the order.

**The named fields stayed**, as a *projection* of that map: `BindGroupFields()` points
`m_treeCATALOGS`, `m_treeDATAPROCESSORS`, … at the nodes it holds, after the build and again after
the search filter removes groups. Reason to keep them: while the order is still written out by
hand, keying the same twenty-three nodes by clsid buys nothing a name does not — and the two
external trees are written against names anyway. The map earns its keep the day the order comes
from the METATYPE and this table disappears; there is nothing to name the fields after then,
because the set becomes whatever registered.

The split does fix the search path either way: a group the filter drops is erased from the map
*and* unbound from its field, where a bare field kept pointing at a deleted node.

How a metaobject **unfolds** is one dispatcher, `ExpandMetaItem`, shared by the initial fill and
the create path. It used to be two chains of `else if`, and they had drifted — a section created
in the designer listed its sub-sections as flat rows and did not recurse, while a section loaded
from the configuration nested properly through `AddInterfaceItem`.

Two things worth knowing:

- **The reuse is meaningful.** ChartOfCharacteristicTypes and ChartOfAccounts render *as a
  Catalog*; AccountingRegister renders *as an AccumulationRegister*; a parameterized job renders
  *as a catalog entry with a second verb*. That is the only real knowledge in the dispatcher.
- **Only display order is written down.** *What* the children are still comes from the property
  skeleton ([property-system.md § 5](property-system.md)).

**Why a table and not yet a walk over the type registry.** What a metatype would have to answer
for the navigator to draw it with no table at all is exactly the columns above — a band, a rank,
a label, beside the `GetIconGroup()` it already answers. Writing them out in one place is what
makes that question askable; the registry walk itself already exists and is already used
(`backend_picture.cpp` walks `ibValue::GetListCtorsByType(object_metadata)` for icons). Moving
those columns onto the metatype is what would let a metatype added later appear in the tree —
and in configuration-compare, the role editor, "All functions" and the query constructor, which
each keep an order of their own today ([metaobject-naming.md § 4](metaobject-naming.md)).

---

## 4. Opening an object — the tree does not own the editor

`OpenObjectForm(obj)` resolves to an `ibMetaDocument` through the doc/view stack; the tree
only asks for it. The two-argument overload
(`OpenObjectForm(obj, ibBackendMetaDocument*& foundedDoc)`) hands back the document that
was found/created — that is how a caller reuses an already-open editor instead of opening
a second one.

Closing is symmetric and deliberately blunt:

```cpp
virtual void CloseMetaObject(ibValueMetaObject* metaObject) {
    ibMetaDocument* doc = GetDocument(metaObject);
    if (doc != nullptr) doc->DeleteAllViews();
}
```

A metaobject that goes away takes **all** its views with it — no per-view bookkeeping in
the tree. See [docview-fork.md](docview-fork.md) for the doc/view ownership rules.

`SelectFormType` is the hook for "this metaobject has several form kinds — which one?"
(it returns an `ibFormID`); `EditPredefinedValues` opens the predefined-values editor for
hierarchy-capable ref objects, and defaults to a no-op on trees where it makes no sense.

### 4.1 `EditModule` — the debugger's door, and a wx-RTTI trap (2026-08-12)

`ibMetaTreeBase::EditModule(guid, line, setRunLine)` is what the debug client calls to show the
**current execution line**: find the metaobject, reuse or open its editor, hand the line to
`ibValueModuleDocument::SetCurrentLine`.

The cast to that interface must be a **C++ `dynamic_cast`**, not `wxDynamicCast`:

> `wxDynamicCast` / `IsKindOf` walk the chain written BY HAND as the second argument of
> `wxIMPLEMENT_*CLASS`, not the C++ one. Eight document classes named a *grandparent* there —
> `ibModuleDocument` declared `ibMetaDocument` while really deriving from `ibValueModuleDocument` —
> so the skipped class answered "not a kind of" for every instance. The arrow silently stopped
> appearing while breakpoints, which travel another path, kept working.

The declared bases were corrected to the real ones; the rule to keep is that the second macro
argument is the hierarchy wx *believes in*, and it is not documentation.

---

## 5. External reports and data processors

Two different entry paths, one container concept.

### 5.1 In the Designer — a file becomes a document

`designer/docManager/templates/docViewDataReportFile.cpp`. Opening an `.erf` creates a
view that builds **its own tree** over the loaded container:

```cpp
bool ibReportEditView::OnCreate(ibDocument* docBase, long flags) {
    ibMetaDocument* doc = GetDocument();
    m_metaTree = new ibDataReportTree(doc, m_viewFrame);
    m_metaTree->SetReadOnly(false);
    return ibView::OnCreate(docBase, flags);
}
```

`OnActivateView` forwards to `m_metaTree->ActivateTree()`; `OnClose` freezes and destroys
the tree. `docViewDataProcessorFile.cpp` is the same shape for processors.

### 5.2 At runtime — one script function

The manager is a registered system value:

```cpp
SYSTEM_TYPE_REGISTER(ibValueManagerDataObjectExternalReport, "ExternalManagerReport",
                     system_to_clsid("MG_EXTR"));

void ibValueManagerDataObjectExternalReport::FillManagerMethods(ibMemberTable& helper) const {
    helper.AppendFunc(wxT("Create"), 1, wxT("Create(fullPath : string)"));
}
```

`Create` is the whole external-report runtime surface:

```cpp
ibMetaDataReport* metaReport = new ibMetaDataReport();
if (metaReport->LoadFromFile(paParams[0]->GetString())) {
    ibValueModuleRuntimeManagerExternalReport* moduleManager = metaReport->GetManagerModule();
    pvarRetValue = moduleManager->GetObjectValue();
    return true;
}
wxDELETE(metaReport);
ibBackendCoreException::Error(_("Failed to load report '%s'"), paParams[0]->GetString());
```

Read it as: **a file load produces a detached metadata root, and the value handed to the
script is that root's manager module.** From there the report behaves like a configuration
report — same object, same forms, same spreadsheet
([report-engine.md](report-engine.md)) — which is why the metaobject only needed
`IsExternalCreate()` to differ.

The transient container's lifetime is the RAII job of `ibExternalOwnerHelper`, mixed into
`ibValueRecordDataObjectExternalReport`, which drops the owned metadata in its destructor.

---

## 6. Copy / paste rides the property system

The tree does not implement copying. Its handlers (`treeConfigurationEvent.cpp`) call the
metaobject and nothing more:

```cpp
if (metaSrcObject->CopyObject(dataWritter)) { …
    if (createdMetaObject->PasteObject(reader)) { … }
}
```

And `ibValueMetaObject::CopyObject` (`metaCollection/metaObject.cpp`) works by walking the
**property children** — the same `GetChildCount()` / `GetChild()` that `FillArrayObjectByFilter`
uses ([property-system.md § 5](property-system.md)):

```cpp
class ibControlCopyGuard {
    static void Generate(const ibValueMetaObject* copyObject) {
        for (unsigned int idx = 0; idx < copyObject->GetChildCount(); idx++)
            Generate(copyObject->GetChild(idx));          // recurse the property tree
        copyObject->m_metaCopyGuid = wxNewUniqueGuid;     // mark every node
    }
    static void Erase(const ibValueMetaObject* copyObject);
};
```

So copying a Catalog copies its attributes, tabular sections and forms **because they are
its property children** — not because the tree enumerated them. The guid mechanism and the
second serialization path are documented in [copy-paste.md](copy-paste.md); the point here
is only that the navigator contributes nothing to it.

---

## 7. Honest remainder

- **Verify the ownership of `metaReport` on the SUCCESS path.** `wxDELETE` runs only on
  the failure branch; on success the raw `ibMetaDataReport*` is handed off implicitly and
  the intended owner is `ibExternalOwnerHelper`. The path from `Create` to that helper is
  not visible in this function — worth a runtime pass (open an external report twice, watch
  the container) before trusting it.
- ~~⚠ `ibMetaDataTree` vs `ibMetadataTree` — one letter's case apart~~ — **renamed 2026-08-12**
  to `ibMetaTreeBase` / `ibConfigurationTree` (§ 2.1). Three RTTI slips came out with it, all
  from the same copy-paste: each nested tree control declared `wxDECLARE_DYNAMIC_CLASS` naming
  **another class** (the macro ignores the argument, so it compiled and simply read as a lie),
  and both external trees declared their wx base as `wxPanel` rather than the metadata-tree base
  — so wx's own class chain did not know they are metadata trees. Nothing casts through it today;
  the configuration tree always declared its base correctly.
- ~~Group members are `m_treeCONSTANTS` / `m_treeCATALOGS` / …~~ — **gone 2026-08-12**, replaced
  by `m_groups` + `Group(clsid)` (§ 3.1). The two external trees still carry four `UPPER_SNAKE`
  fields each (`m_treeATTRIBUTES`, `m_treeTABLES`, `m_treeFORM`, `m_treeTEMPLATES`), which is the
  next thing to fold if those trees are unified with the configuration one.
- **Fixed the same day, in both external trees** (they are copies of each other, so every defect
  came in pairs): the Templates group was labelled `objectTablesName` — "Tables" — and the
  attribute filter still spelled out `GetClassType() == g_metaPredefinedAttributeCLSID`, the form
  `IsAcceptedByParent()` replaced everywhere else. The sweep that introduced `IsAcceptedByParent`
  covered the configuration tree's ten sites and stopped there; these six were left behind.
- **Fixed across all three trees, 2026-08-12** (an audit pass; each defect existed in every copy
  unless noted):
  - **The clipboard was left open.** `Close()` sat inside the success branch, so Ctrl+C on a group
    node (no metaobject) or Ctrl+V with a non-OES payload held it — on MSW that is a session-wide
    lock for every application until the process exits. Now an RAII guard, `clipboardLock.h`.
  - **A failed paste left an orphan.** `NewItem` had already created the metaobject; when
    `PasteObject` failed nothing removed it, so it was saved into the configuration unseen. The
    three trees also disagreed on what to do — the data-processor one put the half-built object
    into the navigator, the other two pointed the inspector at an object with no node.
  - **Deleting an object did not close its children's editors.** The sweep walked the *direct*
    children, which are group nodes carrying no metaobject, so it closed nothing; `EraseItem` now
    recurses.
  - **Every search closed every editor** opened from the navigator: `ClearTree` began by
    `DeleteAllViews()`, and a search is a rebuild. Split out as `CloseOwnedDocuments`, called from
    `Load` only.
  - **The image list only grew** — `Append*` adds a bitmap, nothing ever removed one, so each
    rebuild (i.e. each keystroke in the search box) re-added the whole configuration for the life
    of the process.
  - **`GetNextChild(child, cookie)`** where the API wants the *parent* — 15 sites. wxMSW ignores
    the argument, the generic implementation does not, and this repo ships a wxUniversal fork.
  - `EraseItem` had lost its null guard in both copies; `EditModule` used `static_cast` where the
    null check afterwards could never fire; `GetMetaTree()` guarded the view instead of the
    `wxDynamicCast` result; the two external trees never cleared the metadata's back-pointer to the
    tree in their destructors, and their default constructors left every member indeterminate.
  - In `metaDiff`, **commands had neither a label nor a rank** — the compare tree showed them last,
    captioned with a raw clsid number.
- **Still open in the external trees**: they show no **Commands** group, while the same metaobject
  under a configuration does (`AddDataProcessorItem` / `AddReportItem` create one). The metaobject
  is the same class and answers `GetCommandArrayObject()` either way, so this is a missing node
  rather than a missing capability — but turning it on is opening a path, not fixing a typo, and
  wants a run before it is trusted.
- **Still open, needs a decision rather than a fix**: `OnCompareItems` casts to
  `wxTreeItemMetaData` in the configuration tree and to the mixin base `ibTreeDataMetaItem` in both
  copies. The copies therefore also match group-bearing rows, so sorting the *Tables* group in an
  external data processor reorders the tabular sections through `ChangeChildPosition`, which the
  configuration tree refuses to do. One of the two is right; they should not differ.
- `UpdateChoiceSelection()` is the one contract method with an empty default; which trees
  actually implement it is unverified here.
