# Command interface — the Interface metaobject

> **Scope:** what the **Interface** metaobject is, why it is also the platform's
> *subsystem* concept, and how a metaobject ends up as a command on a bar.
> This document describes code that **already exists**; it is a map, not a plan.

---

## 1. One metaobject, two jobs

`backend/interfaceHelper.h` carries the banner:

```
//*                     Sybsystem - interface                     *
```

(the typo is in the source). It is the honest summary of the design: **there is no
separate Subsystem metatype.** The Interface metaobject *is* the subsystem — it is both

1. the **grouping** a metaobject belongs to (a Catalog is "in" Sales), and
2. the **command surface** the user sees for that grouping.

Merging the two is deliberate: a subsystem that nothing shows is invisible, and a command
group with no membership has nothing to list.

---

## 2. Interface tree — `ibValueMetaObjectInterface`

`backend/metaCollection/metaInterfaceObject.h`

```cpp
virtual ibClassID ResolveChild(const ibClassID& clsid) const {
    if (clsid == g_metaInterfaceCLSID)
        return clsid;
    return 0;
}
```

An Interface accepts **an Interface** as a child, and nothing else. That single override
is what makes interfaces a **recursive tree** — nested subsystems fall out of it for free,
with no extra container type. `GetInterfaceArrayObject()` returns the direct children.

Other members:

- `m_propertyPicture` + `GetPictureAsBitmap()` — the icon, falling back to
  `ibBackendPicture::CreatePicture(g_metaCommonMetadataCLSID)` when unset, so a bar item
  always has something to draw.
- `m_roleUse` + `AccessRight_Use()` — an Interface is access-controlled in its own right
  (`IsFullAccess() || AccessRight(m_roleUse)`). A user without **Use** does not see the
  subsystem. See [access-policy-rls.md](access-policy-rls.md).

---

## 3. Membership — the `ibInterfaceObject` mixin

`backend/interfaceHelper.h`

```cpp
class BACKEND_API ibInterfaceObject {
    void SetInterface(const ibMetaID& id, const bool& set = true);
    bool IsSetInterface(const ibMetaID& id) const;
    // std::set<ibMetaID> m_interfaces;  + virtual DoSetInterface hook
};
```

Membership lives on the **member**, not on the interface: a metaobject holds the set of
interface ids it belongs to. So membership is many-to-many (one Catalog in several
subsystems) without a join table, and the interface tree stays a pure tree.

`SetInterface` updates the set and then calls the virtual `DoSetInterface` — the hook a
concrete metaobject uses to react (persist / mark modified).

---

## 4. Sections and command types

`backend/interfaceHelper.h`

```cpp
enum ibInterfaceCommandSection {         enum ibInterfaceCommandType {
    ibInterfaceCommandSection_Default  = 100,   ibInterfaceCommandType_Default = 100,
    ibInterfaceCommandSection_Create   = 150,   ibInterfaceCommandType_Create  = 150,
    ibInterfaceCommandSection_Combined,         ibInterfaceCommandType_List,
    ibInterfaceCommandSection_Report,           ibInterfaceCommandType_Select,
    ibInterfaceCommandSection_Service,      };
};
```

- **Section** = *where on the surface* a metaobject's commands are filed. A metaobject
  declares its own section by overriding `GetCommandSection()` — e.g.
  `ibValueMetaObjectReport` returns `ibInterfaceCommandSection_Report`
  ([report-engine.md](report-engine.md)). The section is a property of the **kind**, not
  a user setting.
- **Type** = *which command* — create a new item, open the list, open a selection list.
  This mirrors the source-command layer: the five virtual commands live on the metaobject
  in its own inheritance, not in a mixin or a proxy.

`GetInterfaceItemArrayObject(ibInterfaceCommandSection page, std::vector<ibValueMetaObject*>& array)`
is the query that drives a surface: *give me every metaobject filed under this section of
this interface.*

The `Default = 100` / `Create = 150` gaps are deliberate id spacing for later insertions
without renumbering — these values are serialised.

---

## 5. The bar — a form layer

`frontend/visualView/layers/commandBar.h`

The command bar is a **form layer** (`ibValueLayerObject`), not a control. Two classes:

| Class | Role |
|---|---|
| `ibValueCommandBar` | the bar — a layer node in the designer tree |
| `ibValueCommandBarItem` | one child command — a leaf layer node |

Commands are rendered from a flat entry struct:

```cpp
struct ibCommandEntry {
    ibActionID              id;             // wxNOT_FOUND marks a SEPARATOR
    wxString                caption;
    ibPictureDescription    picture;
    ibRepresentation        representation; // picture / text / both, resolved per command
    bool                    enabled;        // greys out WITHOUT dropping from the bar
    ibValueCommandBarItem*  item;           // source child; nullptr for an AutoFill command
};
```

Two details worth keeping:

- **`item == nullptr` means AutoFill.** The bar can be filled automatically from the
  form's own actions, or manually from `ibValueCommandBarItem` children. The back-pointer
  exists so a designer click on a rendered tool resolves back to the child that produced
  it — manual commands are editable, AutoFill ones are not.
- **`enabled` greys, it does not remove.** A disabled command keeps its slot, so the bar
  does not reflow as state changes.

**View-only greys DATA-MODIFYING commands.** `BuildCommands` computes `enabled` from the
owner form's `IsViewOnly()`: a data-modifying command (Save / Post / Create / Delete / …)
goes `enabled = false` when the form is view-only; read-only commands (Refresh / Filter /
Sort / View mode / Open / Generate) stay live. What counts as "modifies data" is a flag on
each action (`ibCommandItem::m_modifiesData`, default true) for AutoFill and a
`ModifiesData` property on a manual `ibValueCommandBarItem`. The **same** flag is honoured
by the tablebox context menu (`OnContextMenu`), not just the toolbar. See
[view-only.md](view-only.md).

The shared runtime / property / metadata / routing machinery lives in the
`ibValueLayerObject` base; `ibValueCommandBarItem` adds only its own fields and its
designer menu.

---

## 6. Action collection — `ibActionDataObject` (`backend/actionInfo.h`)

An `ibValueFrame` IS-A `ibActionDataObject`, so every control and the form itself hands out
an `ibActionCollection` (`GetActionCollection`) and executes one by id (`CallAsAction`). The
collection is a vector of `ibCommandItem` records built through the **funnel** `EmplaceAction`
(one definition of field order — no swapped-argument bugs). Field names read plainly:
`m_actionId` (wxNOT_FOUND = separator), `m_name`, `m_caption`, `m_pictureDescription`,
`m_pictureAndText`, `m_createInForm`, `m_srcData`, `m_modifiesData`.

`AddAction` / `InsertAction` **return the placed item by ref**, so a caller chains fluent
setters right after:

```cpp
actions.AddAction(wxT("Generate"), _("Generate"), g_picGenerateCLSID, true, eGenerate).SetModify(false);
```

`SetModify(bool)` / `SetPictureAndText(bool)` / `SetCreateInForm(bool)` each return `*this`.
`ibActionDataObject` has a `virtual` destructor (it is a polymorphic base). There is no
id-keyed `SetModifiesData` setter — the flag is set at add time through the chain.

- The `Sybsystem` typo in the `interfaceHelper.h` banner is real and is a rename candidate
  (see the naming plan), not a hidden concept.
- `ibInterfaceCommandType_Select` exists as an enumerator; the selection-list command path
  is the one to verify at runtime before relying on this table.
