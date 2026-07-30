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

The Designer's side is `ibMetaDataTree` (`designer/mainFrame/metaTree/treeConfiguration.h`):

```cpp
class ibMetaDataTree : public wxPanel, public ibBackendMetadataTree { … };
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
| `ibMetadataTree` | `treeConfiguration.{h,cpp,_impl.cpp}` + `treeConfigurationEvent.cpp` | the whole configuration |
| `ibDataReportTree` | `treeDataReport.{h,cpp,_impl.cpp}` + `treeDataReportEvent.cpp` | one **external report** (`.erf`) |
| `ibDataProcessorTree` | `treeDataProcessor.{h,cpp,_impl.cpp}` + `treeDataProcessorEvent.cpp` | one **external data processor** |

All three derive from `ibMetaDataTree` — the panel + contract from §1. ⚠ Note the base and
the configuration tree differ **by one letter's case** (`ibMetaDataTree` vs
`ibMetadataTree`); see §7.

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

### 3.1 The layout is hard-coded — deliberately

The tree is a **presentation**, and its order is written out by hand. The root groups are
appended in a fixed sequence (`treeConfiguration_impl.cpp`):

```cpp
m_treeCONSTANTS      = AppendGroupItem(m_treeMETADATA, g_metaConstantCLSID,    constantsName);
m_treeCATALOGS       = AppendGroupItem(m_treeMETADATA, g_metaCatalogCLSID,     catalogsName);
m_treeDOCUMENTS      = AppendGroupItem(m_treeMETADATA, g_metaDocumentCLSID,    documentsName);
m_treeENUMERATIONS   = AppendGroupItem(m_treeMETADATA, g_metaEnumerationCLSID, enumerationsName);
m_treeDATAPROCESSORS = AppendGroupItem(m_treeMETADATA, g_metaDataProcessorCLSID, dataProcessorName);
m_treeREPORTS        = AppendGroupItem(m_treeMETADATA, g_metaReportCLSID,      reportsName);

m_treeINFORMATION_REGISTERS          = AppendGroupItem(m_treeMETADATA, g_metaInformationRegisterCLSID, …);
m_treeACCUMULATION_REGISTERS         = AppendGroupItem(m_treeMETADATA, g_metaAccumulationRegisterCLSID, …);
m_treeCHARTS_OF_CHARACTERISTIC_TYPES = AppendGroupItem(m_treeMETADATA, g_metaChartOfCharacteristicTypesCLSID, …);
m_treeCHARTS_OF_ACCOUNTS             = AppendGroupItem(m_treeMETADATA, g_metaChartOfAccountsCLSID, …);
m_treeACCOUNTING_REGISTERS           = AppendGroupItem(m_treeMETADATA, g_metaAccountingRegisterCLSID, …);
```

and each kind's children are built by its own hand-written adder, dispatched on clsid:

```cpp
if      (metaItem->GetClassType() == g_metaCatalogCLSID)                     AddCatalogItem(metaItem, createdItem);
else if (metaItem->GetClassType() == g_metaDocumentCLSID)                    AddDocumentItem(metaItem, createdItem);
else if (metaItem->GetClassType() == g_metaEnumerationCLSID)                 AddEnumerationItem(metaItem, createdItem);
else if (metaItem->GetClassType() == g_metaDataProcessorCLSID)               AddDataProcessorItem(metaItem, createdItem);
else if (metaItem->GetClassType() == g_metaReportCLSID)                      AddReportItem(metaItem, createdItem);
else if (metaItem->GetClassType() == g_metaInformationRegisterCLSID)         AddInformationRegisterItem(metaItem, createdItem);
else if (metaItem->GetClassType() == g_metaAccumulationRegisterCLSID)        AddAccumulationRegisterItem(metaItem, createdItem);
else if (metaItem->GetClassType() == g_metaChartOfCharacteristicTypesCLSID)  AddCatalogItem(metaItem, createdItem);          // reuses Catalog
else if (metaItem->GetClassType() == g_metaChartOfAccountsCLSID)             AddCatalogItem(metaItem, createdItem);          // reuses Catalog
else if (metaItem->GetClassType() == g_metaAccountingRegisterCLSID)          AddAccumulationRegisterItem(metaItem, createdItem); // reuses AccumulationRegister
```

**This is a deliberate trade, not neglect.** The display order of metadata is genuinely
irregular — Constants before Catalogs, Documents after Catalogs, registers grouped apart,
each kind showing a different set of child groups — and expressing that as data (a
declarative layout, a per-kind schema) costs more than writing it down. Hard-coding it was
cheaper and it settled the question.

Two things worth noticing before "improving" it:

- **The reuse is meaningful.** ChartOfCharacteristicTypes and ChartOfAccounts render *as a
  Catalog*; AccountingRegister renders *as an AccumulationRegister*. The adders encode
  "which existing kind does this look like", which is real information about the model.
- **Only display order is hard-coded.** *What* the children are still comes from the
  property skeleton ([property-system.md § 5](property-system.md)) — the adders choose how
  to lay out `FillArrayObjectByFilter` results, they do not own the composition.

The group members are `m_treeCONSTANTS`, `m_treeCATALOGS`, … — `UPPER_SNAKE` fields, which
is not the project's `m_camelCase` convention (§7).

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
- ⚠ **`ibMetaDataTree` vs `ibMetadataTree` — two classes, one letter's case apart**, in the
  same header:

  ```cpp
  class ibMetaDataTree : public wxPanel, public ibBackendMetadataTree { … };  // treeConfiguration.h:12  — the BASE
  class ibMetadataTree : public ibMetaDataTree                        { … };  // treeConfiguration.h:95  — the CONFIGURATION tree
  ```

  `MetaData` is the base, `Metadata` is the config tree. A typo in either direction compiles
  and resolves to the wrong class. This is the single highest-value rename in the
  navigator — the naming plan's first candidate.
- Group members are `m_treeCONSTANTS` / `m_treeCATALOGS` / `m_treeINFORMATION_REGISTERS` …
  — `UPPER_SNAKE` where the project convention is `m_camelCase`
  ([../CLAUDE.md](../CLAUDE.md) § Naming Conventions).
- `UpdateChoiceSelection()` is the one contract method with an empty default; which trees
  actually implement it is unverified here.
