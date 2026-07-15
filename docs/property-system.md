# Property system + object inspector

> **Scope:** the property mechanism — one declaration, **three surfaces** (designer editor,
> runtime script, serialization) — and the inspector that renders the first of them.
> Companions: [form-attribute-binding.md](form-attribute-binding.md) (what the *binding*
> properties mean), [report-engine.md](report-engine.md) (the spreadsheet a property
> carries), [ui-palette.md](ui-palette.md) (the inspector's tints).
> This document describes code that **already exists**; it is a map, not a plan.

---

## 1. One declaration, three surfaces

**This is not a Designer subsystem.** It lives in `backend.dll`, and the Designer is only
its most visible consumer. A property is declared **once** and every platform-wide
mechanism reads that one declaration:

| Surface | Entry | Lives |
|---|---|---|
| **Designer editor** | `GetPGProperty()` → a `wxPGProperty` | frontend only (null when unloaded) |
| **Runtime script** | `SetDataValue` / `GetDataValue` over `ibValue` (§7) | backend, headless-safe |
| **Serialization** | `ReadNodeValue` / `WriteNodeValue` over `ibDataValue` (§6) | backend, headless-safe |
| **Copy / paste** | `CopyNodeValue` / `PasteNodeValue` (§6) | backend, headless-safe |
| **Compare / merge** | `GetValue()` + `wxVariantData::Eq` (§6.1) | backend, headless-safe |

Four of the five work with no UI at all. That is why a daemon can load a configuration,
run a report and write it back without ever creating a `wxPGProperty` — and why
copy/paste and configuration-compare needed **no** mechanism of their own: declaring a
property is what enrolls a datum in all five.

That leverage is the reason to be strict about the rule in §3 (data lives *in* the
variant): a payload kept beside the variant is invisible to serialization, clipboard and
diff at once.

Everything with editable state is an `ibPropertyObject`
(`backend/propertyManager/propertyObject.h`, ~700 lines — the core). A metaobject is one;
a form control is one; a form attribute is one. They differ in *which* properties they
declare, never in *how* properties work.

An object declares its properties **as member initialisers**, so the property set is built
by the constructor and is stable for the object's life:

```cpp
ibPropertyCategory* m_categoryTemplate =
    ibPropertyObject::CreatePropertyCategory(wxT("Template"), _("Template"));
ibPropertySpreadsheet* m_propertyTemplate =
    ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(
        m_categoryTemplate, wxT("TemplateData"), _("Template data"));
```

`CreateProperty<T>` / `CreateEvent<T>` / `CreatePropertyCategory` are protected members of
`ibPropertyObject` — a type cannot create a property that is not filed under a category of
its own.

---

## 2. The three parts

| Class | Is | Owns |
|---|---|---|
| `ibPropertyObject` | the owner — "a thing with properties" | the root `ibPropertyCategory`, maps of properties/events |
| `ibPropertyCategory` | a **tree** node grouping properties by name | sub-categories (deletes them), **names** of its properties |
| `ibBackendProperty` | one property — name, label, help, value | its `wxVariant` value |

Note the split: the **category stores names** (`std::vector<wxString>`), the **object
stores the pointers** (`std::map<wxString, ibProperty*>`). The category is a layout, not
an owner. Rendering walks the category tree and resolves each name back through
`obj->GetProperty(name)`.

`ibProperty` and `ibEvent` are both `ibBackendProperty`; an event only adds `m_args`
(the handler's argument names).

---

## 3. The value lives in the variant

`ibBackendProperty::m_propValue` is a `wxVariant` — that is the property's storage. There
is no parallel typed member to keep in sync:

```cpp
template <typename cast_type = wxVariantData>
inline cast_type* get_cell_variant() const {
    cast_type* ret = dynamic_cast<cast_type*>(m_propValue.GetRefData());
    wxASSERT(ret != nullptr);
    return ret;
}
```

A composite property (source, dynamic list, spreadsheet) puts its whole payload into a
custom `wxVariantData` subclass and reaches it back through `get_cell_variant<T>()`. The
rule that follows: **generate derived state live from the variant, do not cache it in a
member** — a member cache and the variant drift the moment anything writes the variant
directly.

A concrete property then adds only typed sugar over that one slot:

```cpp
bool GetValueAsBoolean() const { return m_propValue; }
void SetValue(const bool boolean) { m_propValue = boolean; }
```

---

## 4. The backend/frontend seam — a function-pointer slot

`backend.dll` has **zero UI code** (decision #4 in [../CLAUDE.md](../CLAUDE.md)), yet each
property must produce a `wxPGProperty` for wxPropertyGrid. The seam is a static
function-pointer slot per property type, and a return type of `wxObject*` — the most
derived type backend is allowed to name:

```cpp
// backend/propertyManager/property/propertyBoolean.h
virtual wxObject* GetPGProperty() const {
    if (ms_propertyBoolean != nullptr)
        return ms_propertyBoolean(m_propLabel, m_propName, GetValueAsBoolean());
    return nullptr;   // frontend not loaded → no editor, no crash
}
static wxObject* (*ms_propertyBoolean)(const wxString&, const wxString&, const bool&);
```

```cpp
// backend/propertyManager/property/propertyBoolean.cpp — the slot starts empty
wxObject* (*ibPropertyBoolean::ms_propertyBoolean)(const wxString&, const wxString&, const bool&) = nullptr;
```

The frontend fills it by **static initialisation** — a loader object whose constructor
runs when `frontend.dll` loads:

```cpp
// frontend/propertyManager/property/advprop/advpropBoolean.cpp — the whole file
class ibPropertyBooleanLoader {
public:
    ibPropertyBooleanLoader() {
        ibPG_IMPLEMENT_PROPERTY_CALLBACK(wxBoolProperty, ibPropertyBoolean::ms_propertyBoolean);
    }
} g_boolLoader;
```

(`ibPG_IMPLEMENT_PROPERTY_CALLBACK` expands to `AssocProperty<name>(value)` —
`frontend/propertyManager/property/private/prop.h`.)

**There are 32 such slots.** The consequences are worth stating plainly:

- A headless run (daemon / codeRunner) never loads the frontend, every slot stays
  `nullptr`, `GetPGProperty()` returns null, and nothing breaks — properties still
  serialise and still carry data.
- Each property type is one backend file + one frontend `advprop*` file. The pairing is
  by convention, enforced only by the slot's signature.
- Registration order is static-initialisation order across a DLL — fine here because the
  slots are only *read* later, on first inspector render.

---

## 5. Two kinds of property object — flat and tree

Before the graphs: there are exactly **two kinds** of property object, and the difference
is whether it has children.

| Kind | Base | Who |
|---|---|---|
| **Flat** — properties only, no children | `ibPropertyObject` | grid selection (§8.4), form attribute, dynamic list, value table, command-bar layer |
| **Tree** — properties **plus** parent/children | `ibPropertyObjectHelper<T>` | **metadata** and **forms** — and nothing else |

The tree kind has exactly two users in the whole engine, each CRTP-parameterised by
*itself*:

```cpp
class BACKEND_API  ibValueMetaObject : … public ibPropertyObjectHelper<ibValueMetaObject>, …  // metaObject.h:118
class FRONTEND_API ibValueFrame      : … public ibPropertyObjectHelper<ibValueFrame>,      …  // frame.h:70
```

That is the whole story of the two big trees in the product:

- **the metadata tree** ([metadata-tree.md](metadata-tree.md)) is `ibValueMetaObject`'s
  child list, and
- **the form's control tree** ([form-editor.md](form-editor.md)) is `ibValueFrame`'s.

Neither tree is a separate data structure the navigator maintains — the navigator *renders*
the property objects' own parent/child links. Add a child to an `ibValueFrame` and the
form tree has a new node; the editor is a view of it.

**This is load-bearing, not incidental: the property object is the skeleton the metadata
hierarchy sits on.** Every "give me my children" accessor on a metaobject —
`GetInterfaceArrayObject`, `GetGenericAttributeArrayObject`, `GetFormArrayObject`,
`FindAnyObjectByFilter`, … — is a thin filter over `m_children`, the member that comes
from `ibPropertyObjectHelper`:

```cpp
template <typename _T1>
bool FillArrayObjectByFilter(std::vector<_T1*>& array,
                             std::initializer_list<ibClassID> filter,
                             const bool use_child_filter = false) const
{
    for (ibValueMetaObject* child : m_children) {        // ← the property object's children
        if (!child->IsAllowed()) continue;               // ← access rights, applied during the walk
        if (filter.size() > 0) {
            ibClassID child_clsid = child->GetClassType();
            for (const auto filter_clsid : filter)
                if (child_clsid == filter_clsid) { array.emplace_back(static_cast<_T1*>(child)); break; }
        }
        else if (_T1* ptr = dynamic_cast<_T1*>(child)) { array.emplace_back(ptr); }

        if (use_child_filter)
            child->FillArrayObjectByFilter<_T1>(array, filter, true);   // recurse
    }
    return array.size() > 0;
}
```

Read what that one function means:

- **Metadata composition IS property-object composition.** There is no second parent/child
  model for metaobjects — a Catalog's attributes, tabular sections and forms are literally
  its property children, distinguished only by their `ibClassID`.
- **Filtering is by kind, for free.** Because the clsid is kind-typed
  ([../CLAUDE.md](../CLAUDE.md) §6), the filter is an integer compare on children the
  object already holds — no `ibMetaData` lookup, no registry.
- **Access rights ride the same walk.** `IsAllowed()` is checked per child *inside* the
  traversal, so a caller cannot accidentally enumerate what the user may not see — the
  filter accessor and the permission check are the same pass.
- Typed accessors are sugar: `GetInterfaceArrayObject()` is
  `FillArrayObjectByFilter<ibValueMetaObjectInterface>(array, { g_metaInterfaceCLSID })`.

So "add a property" and "add a child" are the same system, and anything built on
`ibPropertyObject` inherits the hierarchy, the filtered lookups, the rights check, and all
five surfaces of §1 at once.

The flat kind is the simpler half and the more common one: anything that has settings but
no composition (a selection, an attribute, a layer) derives `ibPropertyObject` directly and
gets every surface of §1 with no tree machinery at all.

Everything in §5.1 below applies **only** to the tree kind; §5.2 applies to both.

### 5.1 The tree — `ibPropertyObjectHelper<T>`

The CRTP template that adds parent/children (metadata and forms only):

- `m_children` is `std::vector<ibValuePtr<propertyType>>` — **owning** handles (ref-counted).
- `m_parent` is a **raw, non-owning** back-pointer. The container is the structural owner.
- `RemoveAllChildren(keepPinned)` is the canonical reset. `keepPinned` preserves children
  that answer `IsPinnedToParent()` (predefined attributes, inner modules) — a reload reset
  passes `true` so the infrastructure built in the owner's constructor survives; teardown
  passes `false`.

Two **re-entrancy guards** are load-bearing and must not be "simplified" away:

```cpp
ibValuePtr<propertyType> dying(*it);   // hold a ref ACROSS erase()
m_children.erase(it);
```

Releasing the last ref *inside* `erase()` runs the child's destructor, which calls back
`RemovePropertyObject(this)` → a re-entrant erase on the vector being erased from (UB).
The local drops the ref only after `erase()` returns. `RemoveAllChildren` orphans children
(`SetParent(nullptr)`) *before* clearing for the same reason.

### 5.2 The attach graph — one object's properties shown inside another

```cpp
void AttachPropertyObject(ibPropertyObject* other);   // non-owning
ibPropertyObject* GetAttachOwner() const;             // the upward edge — "who attached me"
```

Attaching makes another object's properties appear as part of this one in the inspector,
**while `GetProperty` routes them back to the real owner** — so edits and
`OnPropertyChanged` fire on the attached object, not on the host. This is how a form
attribute shows its value's properties as one whole.

The upward edge (`m_attachOwner`) is **not** the tree parent. Changes bubble along it:

```cpp
virtual void OnChildChanged() { if (m_attachOwner != nullptr) m_attachOwner->OnChildChanged(); }
```

`OnChildChanged` deliberately **carries no payload** — it is a refresh signal, not a
property event. Reusing `OnPropertyChanged` for it would make a holder re-forward to the
property's owner and fire the child's handler a second time.

> **The lesson from the arc that built this:** do not add parallel notifiers. One standard
> upward path (`OnChildChanged` along the attach chain) replaced a set of ad-hoc
> `NotifyFormModified` / `RefreshAttributeTree` / `RefreshCompositionTree` calls.

---

## 6. Serialization — the node value is the only path

Per-property byte `SaveData`/`LoadData` is **gone**. One pair remains:

```cpp
virtual bool ReadNodeValue(const ibDataValue& value) = 0;
virtual bool WriteNodeValue(ibDataValue& value) const = 0;
ibDataValue GetNodeValue() const;   // non-virtual convenience over the writer
```

A property yields a **typed scalar** or a `Child` sub-node (a set of values) for a
composite; the sub-node is shared via `shared_ptr`, so an owner can place the same value
under several named areas cheaply. This is why a JSON view shows `"Name": "Price"` rather
than an opaque base64 blob.

Clipboard has its own overridable pair, defaulting to the node value:

```cpp
virtual bool CopyNodeValue(ibDataValue& value) const;   // default = GetNodeValue
virtual bool PasteNodeValue(const ibDataValue& value);  // default = ReadNodeValue
```

Byte transport lives **once**, at the owner's `CopyProperty` / `PasteProperty` boundary,
through the binary provider — not per property. See [copy-paste.md](copy-paste.md).

Object-level save/load is the virtual `ReadProperty` / `WriteProperty`; the base default
routes through attached objects (their data is part of us), so an override must call the
base to include them.

Runtime script access is a third, separate pair: `SetDataValue` / `GetDataValue` over
`ibValue`.

### 6.1 Compare — value equality straight through the variant

Configuration compare/merge ([configuration-compare.md](configuration-compare.md)) has
**no comparison machinery of its own**. Two metaobjects are equal when their identity
matches and every property compares equal — by name, through the variant
(`metaCollection/metaObject.cpp`):

```cpp
if (compareObject1->GetClassType() != compareObject2->GetClassType()) return false;
if (compareObject1->GetMetaID()    != compareObject2->GetMetaID())    return false;

for (unsigned int idx = 0; idx < compareObject1->GetPropertyCount(); idx++) {
    const ibProperty* propDst = compareObject1->GetProperty(idx);
    const ibProperty* propSrc = compareObject2->GetProperty(propDst->GetName());
    if (propSrc == nullptr)                    return false;   // property missing on the other side
    if (propDst->GetValue() != propSrc->GetValue()) return false;
}
return compareObject1->GetPropertyCount() == compareObject2->GetPropertyCount()
    && compareObject1->GetEventCount()    == compareObject2->GetEventCount()
    && compareObject1->GetChildCount()    == compareObject2->GetChildCount();
```

Properties are matched **by name, not by index** (the loop indexes the left side but looks
the right side up by `GetName()`), so declaration order is not part of identity. The final
count check catches the asymmetric case — a property present only on the right.

**`propDst->GetValue() != propSrc->GetValue()` is `wxVariant` comparison**, which for a
custom payload dispatches to `wxVariantData::Eq`. That is the whole reason a composite
implements it:

```cpp
virtual bool Eq(wxVariantData& data) const {                       // ibVariantDataSpreadsheet
    ibVariantDataSpreadsheet* srcData = dynamic_cast<ibVariantDataSpreadsheet*>(&data);
    if (srcData != nullptr) return srcData->m_spreadsheetDesc == m_spreadsheetDesc;
    return false;
}
```

Put the data in the variant and implement `Eq`, and the type is **diffable for free**.

The diff *presentation* adds two policies (`metaCollection/metaDiff.cpp`):

- **`StringifyPropertyValue`** — renders a value for the left/right columns. It branches on
  the variant type for the safe scalars (`string` / `long` / `longlong` / `double` / `bool`
  / `datetime`); for anything else it falls back to `MakeString()`, **strips control bytes**
  and caps at 200 chars, so a binary blob cannot garble the row or push the columns
  off-screen.
- **`IsStructuralProperty`** — module / form / picture / spreadsheet / source properties are
  **not** listed inline in the Properties group: their value *is* the object, so the
  containing object's own Same/Changed row already reports any difference. Listing them
  would duplicate it.

(`IsSkippedSubGroupClsid` mirrors what the metadata tree hides — predefined attributes,
object/manager modules — so the diff tree reads like the navigator.)

---

## 7. The runtime surface — properties are script members

A metaobject's properties **are** its script members. `ibValueMetaObject` exports them by
name and routes reads/writes straight into the property:

```cpp
// PrepareNames-side: every property becomes a named script member, keyed by its index
for (unsigned idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++) {
    ibProperty* property = ibPropertyObject::GetProperty(idx);
    if (property == nullptr) continue;
    helper.AppendProp(property->GetName(), true, false, idx);
}

bool ibValueMetaObject::SetPropVal(const long lPropNum, const ibValue& varPropVal) {
    ibProperty* property = GetPropertyByIndex(lPropNum);
    if (property != nullptr) return property->SetDataValue(varPropVal);
    return false;
}
bool ibValueMetaObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
    const ibProperty* property = GetPropertyByIndex(lPropNum);
    if (property != nullptr) return property->GetDataValue(pvarPropVal);
    return false;
}
```

So `SetDataValue` / `GetDataValue` are not a designer detail — they are the **script's**
view of the same declaration the inspector edits. No separate model, no sync step: the
Designer and a running script read one object.

### 7.1 The spreadsheet — the clearest case

A template (`ibValueMetaObjectSpreadsheet`, [report-engine.md § 4](report-engine.md))
stores its whole table in a property:

```cpp
ibPropertySpreadsheet* m_propertyTemplate =
    ibPropertyObject::CreateProperty<ibPropertySpreadsheet>(
        m_categoryTemplate, wxT("TemplateData"), _("Template data"));

virtual ibSpreadsheetDescription& GetSpreadsheetDesc() const {
    return m_propertyTemplate->GetValueAsSpreadsheetDesc();
}
```

and the payload lives **inside the variant** (§3), not beside it:

```cpp
class BACKEND_API ibVariantDataSpreadsheet : public wxVariantData {
    ibSpreadsheetDescription m_spreadsheetDesc;   // the whole table
    virtual bool Eq(wxVariantData& data) const;   // value equality → change detection
    virtual wxString GetType() const { return wxT("ibVariantDataSpreadsheet"); }
};
```

Read the chain as: **property → variant-data → `ibSpreadsheetDescription`**. At design
time the inspector edits it through `ms_propertySpreadsheet`; at runtime the report reads
the same descriptor out of the same property and builds an `ibBackendSpreadsheetObject`
from it ([report-engine.md § 3](report-engine.md)). One template, two readers, zero
copies of the storage.

`GetValueAsSpreadsheetDesc()` returning a **non-const reference** is what lets the runtime
work against the live descriptor — and is also why "generate live, do not cache" (§3)
matters here: a cached copy of the table would go stale the moment the variant is written.

Note the signature difference — a composite property's slot takes the **owner** and the
raw variant, because its editor needs the object for context:

```cpp
static wxObject* (*ms_propertySpreadsheet)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&);
```

---

## 8. The inspector — `ibObjectInspector`

`frontend/mainFrame/objinspect/objinspect.{h,cpp}`. A `final` `wxPanel` over
`wxPropertyGridManager`, reachable through the `objectInspector` macro singleton.

Render walks the category tree (`CreateCategory` → `AddItems`), calls `GetProperty(prop)`
→ `prop->GetPGProperty()` for each, appends the result, and records the mapping both ways:

```cpp
std::map<wxPGProperty*, ibProperty*> m_propMap;
std::map<wxPGProperty*, ibEvent*>    m_eventMap;
```

Per-class background tints (window / common / sizerItem) come from the interior palette —
see [ui-palette.md](ui-palette.md).

### 8.1 Deferred + coalesced rebuild (the crash guard)

`Create()` calls `m_pg->Clear()`, which **destroys every `wxPGProperty`**. A child edit
routes back here to rebuild (`RefreshEditor` → attribute tree → `SelectObject` →
`Create`). If a wxPG change event is still dispatching, that frees the very property wxPG
is editing — use-after-free the moment the handler returns. A burst of selects would also
Clear+refill repeatedly (flicker).

So while an event is in flight (`m_inGridEvent`) or a rebuild is queued
(`m_rebuildScheduled`), `Create` stashes the target and posts **one** `CallAfter`:

```cpp
if (m_inGridEvent || m_rebuildScheduled) {
    m_pendingObject = object;
    m_pendingForce  = m_pendingForce || force;
    if (!m_rebuildScheduled) { m_rebuildScheduled = true; CallAfter([this]{ /* rebuild once */ }); }
    return;
}
```

`OnPropertyGridChanged` pairs with it: if a rebuild is already scheduled it **skips**,
because walking the stale `m_propMap` would call `RefreshPGProperty` on an `ibProperty`
already freed by a re-materialised value (the attribute-Type-change crash).

### 8.2 RAII freeze — why `wxWindowUpdateLocker`

```cpp
wxWindowUpdateLocker updateLock(m_pg);   // Thaws in its dtor
```

A bare `Freeze()` / `Thaw()` pair leaks a freeze whenever a rebuild step throws or returns
early. That produced **two** bugs from one cause: a skipped `Thaw` leaves the grid frozen
forever (freeze), **and** unbalances the freeze count so later rebuilds stop suppressing
paint (flicker). Both Freeze/Thaw sites are RAII now — keep them that way.

### 8.3 `ModifyProperty` — the one write path

```cpp
const wxVariant oldValue = prop->GetValue();
if (m_currentSel->OnPropertyChanging(prop, newValue)) {
    prop->SetValue(newValue);
    m_currentSel->OnPropertyChanged(prop, oldValue, newValue);
    if (ibPropertyObject* owner = prop->GetPropertyObject())
        owner->OnChildChanged();     // raise from the property's REAL owner
    return true;
}
```

The `OnChildChanged` is raised from **`prop->GetPropertyObject()`**, not from
`m_currentSel`: the edited property may belong to a nested child the inspected holder only
*accumulates* (a dynamic list under a form-attribute holder — `m_currentSel` is the
holder, the Source belongs to the list). Calling the owner's own hook lets the child react
first and then bubble up the attach chain. A self-refreshing owner (a control) has no
attach owner, so the bubble stops — a no-op for it.

---

### 8.4 An inspected object need not own its data — `ibPropertyGridEditorSpreadsheet`

The most instructive consumer is **not** a metaobject. The spreadsheet editor
(`frontend/win/editor/gridEditor/gridEditor.h`) makes *itself* an `ibPropertyObject` so the
inspector can edit the **current cell selection**:

```cpp
class ibPropertyGridEditorSpreadsheet : public ibPropertyObject {
    ibPropertyGridEditorSpreadsheet(ibGridEditor* view) : m_view(view) {
        m_view->Bind(wxEVT_GRID_SELECT_CELL,    &ibPropertyGridEditorSpreadsheet::OnSelectCell,  this);
        m_view->Bind(wxEVT_GRID_RANGE_SELECTED, &ibPropertyGridEditorSpreadsheet::OnSelectCells, this);
    }
    virtual bool IsEditable() const { return m_view->IsEditable(); }
    wxVector<ibGridBlockCoords> m_selection;
};
```

The object it inspects is a **selection**, not a thing:

```cpp
void ibPropertyGridEditorSpreadsheet::ShowInspector() {
    m_selection.clear(); bool hasBlocks = false;
    for (const ibGridBlockCoords& coords : m_view->GetSelectedBlocks()) { m_selection.push_back(coords); hasBlocks = true; }
    if (!hasBlocks) {  // no block → the cursor cell IS the selection
        ibGridBlockCoords coords(m_view->GetGridCursorRow(), m_view->GetGridCursorCol(),
                                 m_view->GetGridCursorRow(), m_view->GetGridCursorCol());
        m_selection.push_back(coords);
    }
    if (m_view->IsPropertyEnabled()) {
        if (!objectInspector->IsShownInspector()) objectInspector->ShowInspector();
        objectInspector->SelectObject(this, true);
    }
}
```

And its property values are **generated live from the grid**, never stored on the object —
`OnPropertyCreated` is a per-property pull, keyed by the selection's coordinates:

```cpp
void ibPropertyGridEditorSpreadsheet::OnPropertyCreated(ibProperty* property, const ibGridBlockCoords& coords) {
    if (m_propertyName == property) {
        // the NAME of a selection is its address: "R1C1" for a cell, "R1C1:R2C3" for a block
        wxString nameField = (coords.GetTopLeft() != coords.GetBottomRight())
            ? wxString::Format(wxT("R%iC%i:R%iC%i"), coords.GetTopRow()+1, coords.GetLeftCol()+1,
                                                     coords.GetBottomRow()+1, coords.GetRightCol()+1)
            : wxString::Format(wxT("R%iC%i"), coords.GetTopRow()+1, coords.GetLeftCol()+1);
        m_propertyName->SetValue(nameField);
    }
    else if (m_propertyAlignHorz == property) {
        int horz, vert;
        m_view->GetCellAlignment(coords.GetTopRow(), coords.GetLeftCol(), &horz, &vert);
        m_propertyAlignHorz->SetValue(horz);
        m_propertyAlignVert->SetValue(vert);
    }
    else if (m_propertyOrient == property) {
        m_propertyOrient->SetValue(m_view->GetCellTextOrient(coords.GetTopRow(), coords.GetLeftCol()));
    }
    …
}
```

Three things this proves about the system:

- **The inspector is a general surface, not a metadata viewer.** Anything that answers
  `GetClassName` / `IsEditable` / `GetProperty` can be inspected — a selection, a tool, a
  transient.
- **"Generate live, do not cache" (§3) is the rule, and here it is structural.** The
  properties hold no truth; the grid does. A cached member would go stale on every cursor
  move.
- **`OnPropertyChanged(property, coords)` writes back to the cells** — the same pull, in
  reverse, applied across every block in `m_selection`. That is how one edit sets alignment
  on a whole selected range.

The destructor calls `m_view->DeletePendingEvents()` — the object binds to a view it does
not own, so queued grid events must not outlive it.

---

## 9. Traps and honest remainder

- **`GetMetaData()` — the non-const overload is a trap, guarded.** Types implement the
  *const* overload (a control resolves it through its owning form). A call through a
  non-const `ibPropertyObject*` hits the base, which `wxFAIL_MSG`es and returns null — a
  property serialising against a null metaData would write null GUIDs ("binding loses its
  source"). Reach the const overload through a const pointer unless you truly own a mutable
  metaData.
- **7 empty headers.** `advpropBoolean.h`, `advpropColour.h`, `advpropDate.h`,
  `advpropEnum.h`, `advpropForm.h`, `advpropModule.h`, `advpropSpreadsheet.h` are **0
  bytes** and still `#include`d by their `.cpp`. Dead includes — removal candidates for
  the restructuring plan.
- **A spreadsheet property renders as a hyperlink, by design.** `advpropSpreadsheet.cpp`
  registers `ibPGHyperLinkProperty` into `ibPropertySpreadsheet::ms_propertySpreadsheet` —
  a table is not editable in a grid row, so the inspector shows a link that opens the
  spreadsheet editor (§8.4). Worth knowing before "fixing" the apparent type mismatch.
- **Copy-pasted loader name.** That same file names its loader instance
  `s_dateLoaderSpreadsheet` (from `advpropDate.cpp`). Cosmetic, but it is the kind of
  drift the naming plan should sweep.
- The `.h`/`.cpp` split in `advprop/` is nominal: several of those `.cpp` files are only a
  loader object, which is why their headers are empty.
