# Command interface — the Section metaobject

> **Scope:** what the **Section** metaobject is, why it is also the platform's
> *subsystem* concept, and how a metaobject ends up as a command on a bar.
> This document describes code that **already exists**; it is a map, not a plan.
>
> *Naming:* the metatype is **`ibValueMetaObjectSection`** ("Section" / RU «Раздел»). 1C splits
> a *subsystem* (structural grouping) from the *section* it raises in the command interface; we
> collapse both into one metaobject (§1), so it is named for what the user actually sees — a
> **section** of the application — rather than carried over as the 1C "subsystem" term. The
> serialised clsid key stays `MD_SSYST` (an opaque wire id — renaming it would break configs).

---

## 1. One metaobject, two jobs

`backend/interfaceHelper.h` carries the banner:

```
//*                     Sybsystem - interface                     *
```

(the typo is in the source). It is the honest summary of the design: **there is no
separate Subsystem metatype.** The Section metaobject *is* the subsystem — it is both

1. the **grouping** a metaobject belongs to (a Catalog is "in" Sales), and
2. the **command surface** the user sees for that grouping.

Merging the two is deliberate: a subsystem that nothing shows is invisible, and a command
group with no membership has nothing to list. Because the two are one, the metatype is named
for the visible half — a **Section** — not the internal "subsystem".

---

## 2. Section tree — `ibValueMetaObjectSection`

`backend/metaCollection/metaInterfaceObject.h` (clsid `g_metaSectionCLSID`, key `MD_SSYST`)

```cpp
virtual ibClassID ResolveChild(const ibClassID& clsid) const {
    if (clsid == g_metaSectionCLSID)
        return clsid;
    return 0;
}
```

A Section accepts **a Section** as a child, and nothing else. That single override
is what makes sections a **recursive tree** — nested sections fall out of it for free,
with no extra container type. `GetInterfaceArrayObject()` returns the direct children.

Other members:

- `m_propertyPicture` + `GetPictureAsBitmap()` — the icon, falling back to
  `ibBackendPicture::CreatePicture(g_metaCommonMetadataCLSID)` when unset, so a bar item
  always has something to draw.
- `m_roleUse` + `AccessRight_Use()` — a Section is access-controlled in its own right
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
subsystems) without a join table, and the section tree stays a pure tree.

`SetInterface` updates the set and then calls the virtual `DoSetInterface` — the hook a
concrete metaobject uses to react (persist / mark modified).

---

## 4. Sections and command types

`backend/interfaceHelper.h`

```cpp
enum ibInterfaceCommandSection {              enum ibInterfaceCommandType {
    ibInterfaceCommandSection_Default   = 100,    ibInterfaceCommandType_Default = 100,
    ibInterfaceCommandSection_Important = 120,    ibInterfaceCommandType_Create  = 150,
    ibInterfaceCommandSection_Create    = 150,    ibInterfaceCommandType_List,
    ibInterfaceCommandSection_Combined,           ibInterfaceCommandType_Select,
    ibInterfaceCommandSection_Report,         };
    ibInterfaceCommandSection_Service,
};
```

The gather's section walk (§9) lays each section item out by **area**, iterating exactly this
list minus `_Combined`: `{ _Important, _Default, _Create, _Report, _Service }` (label
*Important / Normal / Create / Reports / Service*). The `_Create` area alone maps an object item
to `ibInterfaceCommandType_Create` (create a new item); every other area maps to `_Default` (open
the list).

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
    wxBitmap                bitmap;         // the COMMAND's own live icon (resolved from the command); wins over `picture`
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
Sort / View mode / Open / Generate) stay live. **"Modifies data" is a COMMAND attribute, not
the projection's** — the item / button merely obeys. AutoFill reads it off the action
collection (`m_modifiesData`, default true) / the object command; a manual item reads it off
its **bound command** through the command door (see §8). The `ibValueCommandBarItem` no longer
owns a `ModifiesData` property, and **`actionEvent` is retired** — a bar item binds solely
through the command-source property (§9). See [view-only.md](view-only.md).

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

---

## 7. The command metaobject — `ibValueMetaObjectCommand`

`backend/metaCollection/metaCommandObject.h` (clsid `g_metaCommandCLSID = metadata_to_clsid("MD_CMD")`)

A **command** is a first-class metaobject creatable at the config root (**general** / "Common
commands") AND under each business object (a "Commands" node, `GenericData::ResolveChild` +
`GetCommandArrayObject`). Like a Constant, it HOLDS an inner module and, as an
`ibBackendCommandItem`, OVERRIDES the global `Execute` to run its own handler

```
Procedure CommandProcessing(CommandParameter, ExecuteParameters)
```

through its **own** runtime descriptor (`ibValueCommandDataObject`, descriptor-direct — no
common-module registration), exactly the way a Constant runs its module.

Members that a projection reads LIVE (never edited on the projection): `GetIcon()` (the tree
"run" glyph), `GetSynonym()`/`GetName()`, `GetToolTip()`, `GetModifiesData()` (view-only
greying), and **`GetParameterType()`** — a `ibPropertyType` making the command
*parameterizable* (§9). Because it owns a type property, the command **inherits
`ibBackendTypeConfigFactory`** (`GetTypeDesc` returns the parameter type's own storage,
`GetFilterDataType` = reference, `GetMetaData`) — an `ibPropertyType` whose owner is NOT such
a factory builds a null variant and crashes on first read. Default the type to `TYPE_STRING`,
never `TYPE_EMPTY`.

**Hub — a command HOLDS commands.** `ResolveChild` accepts `g_metaCommandCLSID`, so a command
takes child commands (a "Commands" node nests under a command in the designer tree, recursively);
`GetSubCommands()` / `HasSubCommands()` read them (a direct child scan — no array allocation on
the hot tree/menu path). A command WITH children is a **GROUP**: its projection is a dropdown
menu, not a terminal run — exactly as a subsystem holds subsystems (§9). The top-level gathers
use a non-recursive child filter, so sub-commands (grandchildren) never leak into them; the tree
and the door recurse explicitly.

---

## 8. The command interfaces — SENDER gives, RECEIVER takes

> **Status:** rewritten 2026-07-28. The command side was consolidated from **three tangled
> interfaces** into a clean **SENDER / RECEIVER** model that mirrors the SOURCE side. A command
> **SENDS** from a source and is **RECEIVED** by a projection — two concepts, not three: **give**
> and **take**. Both backend interfaces now live in **one header**, `backend/backend_command.h`
> (the old `backend/commandDataObject.h` was **deleted**); the frontend door **extends** the
> receiver. The old `ExecuteValueByCommandHop` (a)/(b)/(c) ladder and `FindControlBySource` remain
> GONE.

| # | interface | header / API | role | twin on the SOURCE side |
|---|---|---|---|---|
| 1 | **SENDER** — `ibBackendCommandSender` | `backend/backend_command.h` (BACKEND_API) | everything that GIVES commands: resolve ONE hop, self-describe the next (§8.1–§8.2) | `ibSourceDataObject` |
| 2 | **RECEIVER, backend slice** — `ibBackendCommandReceiver` | `backend/backend_command.h` (BACKEND_API) | the command-taker's backend contract: WALK a stored binding to prove it live (§8.3) | `ibBackendTypeSourceFactory` |
| 3 | **RECEIVER, full door** — `ibFrontendCommandReceiver : public ibBackendCommandReceiver` | `frontend/visualView/layers/commandReceiver.h` (FRONTEND_API) | RUN / READ / expand a leaf, render `wxBitmap` (§8.4) | (the frontend source-door) |

**Renames** (all landed): `ibBackendCommandDataObject` → **`ibBackendCommandSender`**;
`ibBackendSourceCommand` → **`ibBackendCommandReceiver`**; `ibCommandDataObject` /
`ibCommandReceiver` → **`ibFrontendCommandReceiver`**, and that door's file moved
`commandDataObject.{h,cpp}` → `commandReceiver.{h,cpp}`.

**Sender and Receiver are DISJOINT object sets** — a control is a data source but sends no command;
a button receives a command but holds no data. Coherence is reached along two **parallel**
interfaces (give / take), the same way the source side splits vend from bind — no shared base.

### 8.1 SENDER — the hop gate and the walk

`ibBackendCommandSender` (`backend/backend_command.h`) is the exact twin of `ibSourceDataObject` on
the command side. Where a data path hops through `GetValueBySourceHop` and each step's VALUE is again
a source (self-describing), a command path hops through `GetCommandByHop` and each step's value is
again an `ibBackendCommandSender`. The walk climbs the **same** way; there is no front-end ladder of
special cases.

```cpp
class BACKEND_API ibBackendCommandSender {
public:
    virtual ~ibBackendCommandSender() {}

    // THE hop gate — resolve ONE command id to the next command-capable VALUE (again a sender, so the
    // walk continues on it). false = not a command here. NOT const: a live frontend form answers "is
    // this hop a standard action?" via GetActionCollection (a mutable back-pointer) — the walk mutates nothing.
    virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) { return false; }

    // THE shared walk — the mirror of ibSourceDataObject::ResolvePath. Static: the start need not be THIS.
    static bool ResolveCommandPath(const ibValue& start, const std::vector<ibCommandHop>& path, size_t from, ibValue& out)
    {
        ibValue current = start;
        for (size_t i = from; i < path.size(); ++i) {
            ibBackendCommandSender* node = nullptr;
            current.ConvertToValue<ibBackendCommandSender>(node);
            if (node == nullptr) return false;                        // a non-command value ends the walk
            ibValue next;
            if (!node->GetCommandByHop(path[i], next)) return false;  // the id is not a command on this node
            current = next;
        }
        out = current;
        return true;
    }
};
```

The loop is uniform: `value -> ConvertToValue<ibBackendCommandSender> -> GetCommandByHop -> next
value`, exactly `srcDataObject.h`'s `ResolvePath` with `ibSourceDataObject` / `GetValueBySourceHop`
swapped in. An `ibCommandHop` is **just an id** (no kind tag), resolved AT WALK TIME by whichever
node it lands on. `ResolveCommandPath` is a static on the interface, the twin of `ResolvePath`. A
hop landing on a non-command value stops the walk (returns false), exactly as the source walk stops
on a non-source primitive — the two doors share a shape but never a value.

### 8.2 The FORM is the entry; every sender resolves its OWN hop

`ibBackendValueForm : public ibBackendValue, public ibBackendCommandSender`
(`backend/backend_form.h`) declares the hop **PURE VIRTUAL**:

```cpp
// the form IS the command SOURCE, declared server-side so a HEADLESS caller (web server /
// daemon / codeRunner holding a bare ibBackendValueForm*) can walk commands with NO front-end.
virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) override = 0;
```

This is the key move: the entry hop lives on the **backend** form contract, so a headless caller
can start the walk and run a command without any wxWidgets. `ibValueForm::GetCommandByHop`
(`frontend/visualView/ctrl/formVisualHost.cpp`) implements it. Each command-capable node — every
SENDER — resolves its OWN hop for what it holds; there is no central switch:

| node (a SENDER) | header | its hop resolves to |
|---|---|---|
| `ibValueForm` (via `ibBackendValueForm`) | `formVisualHost.cpp` | the form's **own** command (a form-local `ibFormCommandValue`) / a **config-or-object command metaobject** (`ibValueMetaObjectCommand`, through the form's own `GetMetaData()`) / an **object item** (a Catalog / Register checked into a section — an `ibBackendCommandItem`, opened by the desc's command type) / **descend** into a bound child **tablebox** / a standard **action** on the form's own bus |
| `ibValueWindowComposite` (a tablebox) | `frontend/visualView/ctrl/window.cpp` | a standard **action of its own bus** (the composite IS the action runtime → `CallAsAction`). A plain control / sizer does NOT inherit this and is not command-capable |
| `ibValueMetaObjectCommand` (a **group** command) | `backend/metaCollection/metaCommandObject.cpp` | one of its **sub-commands** (`GetSubCommands`), itself a sender → the walk keeps climbing; a plain leaf has none and terminates |
| `ibValueMetaObjectSection` | `backend/metaCollection/metaSectionObject.cpp` | a **sub-section** (descend), its **own** command, or an **interface item** (any area). A section has **NO runtime** — it only ROUTES; the leaf command carries the mini-runtime via `Execute` |

### 8.3 RECEIVER, backend slice — `ibBackendCommandReceiver`

`ibBackendCommandReceiver` (`backend/backend_command.h`) is the command-side twin of
`ibBackendTypeSourceFactory`. It is a **pure** contract plus one out-of-line destructor:

```cpp
class BACKEND_API ibBackendCommandReceiver {
public:
    virtual ~ibBackendCommandReceiver();   // vtable KEY FUNCTION — anchored out-of-line
    virtual bool WalkCommand(const ibCommandDescription& desc, wxString* outText = nullptr) const = 0;
};
```

The destructor is defined out-of-line in `propertyManager/property/propertyCommandSource.cpp`, so it
is the vtable key function: one exported vtable + typeinfo in `backend.dll`, and the backend
variant's cross-DLL `dynamic_cast<ibBackendCommandReceiver*>` resolves the **frontend-built** button
/ bar item. This mirrors `ibBackendTypeSourceFactory`'s out-of-line members.

**Why the receiver is declared in the BACKEND.** A stored command binding is just a hop path;
returning it blind cannot tell a live binding from a broken one — only WALKING the hops can (a hop no
longer resolves → `false`). The command-source VARIANT (`ibVariantDataCommandSource`, backend) casts
its owner to `ibBackendCommandReceiver*` and calls `WalkCommand` inside `MakeString()`, and that runs
at **RUNTIME**, not only in the designer inspector cell — a bound command that was deleted must read
`"<not found>"` everywhere. So the check cannot live in a frontend-only inspector adapter.

### 8.4 RECEIVER, full door — `ibFrontendCommandReceiver`

`ibFrontendCommandReceiver : public ibBackendCommandReceiver`
(`frontend/visualView/layers/commandReceiver.{h,cpp}`) is the full DOOR. It stays FRONTEND because it
renders `wxBitmap`. It EXTENDS the backend receiver and adds, over the pure gate
`GetCommandGateForm()`:

- **`ExecuteValueByPath`** (RUN): start `ibBackendCommandSender::ResolveCommandPath` **on the form**
  (the door casts the gate form to `ibValue` as the walk's start) and switch on the resolved LEAF —
  a **form command** (`ibFormCommandValue`) → `gate->CallAsEvent(procedure)`; a **command / object
  item** (`ibBackendCommandItem`) → `Execute(...)`, with `ResolveCommandParameter` filling
  `CommandParameter` from FORM CONTEXT first for a real `ibValueMetaObjectCommand` (§9 —
  parameterizable); a resolved **frame** (the form / a tablebox) → `CallAsAction(leafId, gate)`.
- **`ResolveValueByPath`** (READ): the SAME walk, then read the leaf's own default look — caption +
  icon + `modifiesData` — per leaf kind (form command / command metaobject / object item / standard
  action). This is where "the command owns its look AND its behaviour" is sourced.
- **`ResolveSubCommands`** (READ, hub): resolve the leaf; if it is a GROUP (§7), return each
  sub-command as a DIRECT 1-hop path + caption + icon — the menu a projection pops on click.

**The door IMPLEMENTS `WalkCommand` ONCE** (in `commandReceiver.cpp`) — every control inherits it,
none re-implements it. It is now a THIN wrapper over `ResolveCommand` (§8.7), the single resolve
every projection shares: walk on the door, and where the walk is fragile (a tablebox descend yields
an empty caption in the designer preview) fall back to the reliable `GatherFormCommands` set — the
SAME list the picker offered, so a valid pick is found there. `WalkCommand` returns `ResolveCommand`'s
`outPath`; only a truly gone command misses BOTH → `"<not found>"`.

### 8.5 The inheritance chain

The point of the refactor is a **single** receiver chain — SINGLE inheritance of the receiver side,
no separate factory base:

```
ibBackendCommandReceiver  <-  ibFrontendCommandReceiver  <-  ibValueButton
                                                          <-  ibValueCommandBar
                                                          <-  ibValueCommandBarItem
```

Each control inherits the **door** and supplies ONLY its own `GetCommandGateForm`; `WalkCommand` is
inherited from the door — no per-control implementation:

| control | header | `GetCommandGateForm()` returns |
|---|---|---|
| `ibValueButton` | `frontend/visualView/ctrl/widgets.h` | `GetOwnerForm()` |
| `ibValueCommandBar` | `frontend/visualView/layers/commandBar.h` | the owner frame's form |
| `ibValueCommandBarItem` | `frontend/visualView/layers/commandBar.h` | its **bar**'s gate form |

Verify the lines: `ibValueButton : public ibValueWindow, public ibFrontendCommandReceiver`
(`widgets.h`); `ibValueCommandBar` / `ibValueCommandBarItem : public ibValueLayerObject, public
ibFrontendCommandReceiver` (`commandBar.h`).

### 8.6 Data flow — the inspector "Command" cell

A command binds through the command-source property `ibPropertyCommandSource` (picker =
`ibPGCommandSourceProperty`). The variant's ONLY stored state is the `ibCommandDescription` (the hop
path) + a fallback display string; existence is never cached — it is re-walked on every read:

| step | code | result |
|---|---|---|
| the cell string | `ibVariantDataCommandSource::MakeString()` | no desc → `"<not selected>"`; owner (cast to `ibBackendCommandReceiver`) `WalkCommand` fails → `"<not found>"`; walks OK → the leaf name. No owner wired (a detached temp) → the picker-set display, else `"<not selected>"` |
| the property grid | `ibPGCommandSourceProperty::ValueToString` | returns the variant's own live string (`variant.Write` → `MakeString`) — no resolve logic in the picker |
| the cell image | `ibPGCommandSourceProperty::OnSetValue` | sets `m_valueBitmapBundle` to the bound command's OWN picture — the door's `ResolveCommand` icon (§8.7: walk, else the reliable gather); left empty when the command has no picture or was deleted |
| binding the owner | `ibPropertyCommandSource` (`propertyCommandSource.cpp`) | `dynamic_cast`s the owning property object to `ibBackendCommandReceiver*` and stores it in the variant, exactly as `ibVariantDataSource` keeps its source factory |

`GetCommandByHop` is NOT `const`, but the **walk mutates nothing** — `ResolveCommandPath` only reads;
command EXECUTION-const lives on `ibBackendCommandItem::Execute` (which IS `const`). Running the
resolved leaf spawns a **TRANSIENT** runtime (`ibValueCommandDataObject`, §7) that lives only for the
call; a **section** has no runtime — it only routes. Because resolution reads through the form's
**own** `GetMetaData()`, the read path no longer reaches for `activeMetaData` (§9 — `activeMetaData`
stays for config **mutation** only).

### 8.7 The single resolve — `ResolveCommand`

Every projection asks the same two-part question — *does this bound command still exist, and what
is its look?* — and used to answer it on its own: the button, the bar and the inspector cell each
re-implemented the walk + gather with slightly different logic. They drifted. A valid TABLEBOX
command's button was wrongly HIDDEN because the fragile designer-preview walk returned false where
one surface bailed and another did not. `ResolveCommand` is the ONE place that answers it, so the
projections agree by construction:

```cpp
bool ResolveCommand(const ibCommandDescription& desc, wxString& outCaption, wxBitmap& outIcon,
                    bool* outModifies = nullptr, wxString* outPath = nullptr,
                    bool* outPictureAndText = nullptr) const;
```

It runs on the door (`commandReceiver.cpp`) in two steps:

1. **Walk on THIS door** (`ResolveValueByPath`) — the authoritative caption / icon / `modifies`
   for a FORM or GLOBAL command.
2. **Fall back to the RELIABLE gather** (`GatherFormCommands`, the SAME set the picker offered) — a
   TABLEBOX command's walk resolves the hop but vends an EMPTY caption / no icon in the designer
   preview, so the gather fills the readable name + icon and PROVES the command still exists. Only a
   command that misses **both** is gone.

It returns `exists` (the walk resolved with a non-empty caption **OR** the gather has it) and fills:

| out param | value | who reads it |
|---|---|---|
| `outCaption` | the readable name | button / bar text |
| `outIcon` | the command's own icon | button / bar / cell image |
| `outModifies` | the walk's modifies-data flag | view-only greying |
| `outPath` | the gather's full path name (falls back to the caption for a form / global command) | the inspector cell text |
| `outPictureAndText` | the command's OWN default display (true = picture+text; a standard action like Close / Update is picture-only) | Auto-representation resolution on button / bar item |

**The four callers all route through it** — one resolve, no per-surface guess:

| caller | file | use |
|---|---|---|
| `ibValueButton::Update` | `button.cpp` | `cmdResolved = ResolveCommand(...)`; a **false** result HIDES the button (a dead binding), caption / icon feed the button's default look. This is the fix for the wrongly-hidden tablebox button — the fragile walk alone returned false |
| `ibValueCommandBar::BuildCommands` | `commandBar.cpp` | `if (!ResolveCommand(...)) continue;` — a gone command drops its bar projection; caption / icon / `modifies` feed the tool (`modifies` drives view-only greying) |
| `ibFrontendCommandReceiver::WalkCommand` | `commandReceiver.cpp` | now a THIN wrapper over `ResolveCommand`, returning `outPath` (else `"<not found>"`) — the variant's existence check (§8.3) |
| `ibPGCommandSourceProperty::OnSetValue` | `advpropCommandSource.cpp` | `door->ResolveCommand(...)` sets the cell's `m_valueBitmapBundle` to the command's own icon |

**The wart stays honest.** The tablebox-descend walk being fragile in the designer preview is a
real defect; the gather fallback PATCHES it, it does not fix it. `ResolveCommand` makes every
surface share the same patch — button, bar and cell now agree — but the underlying fragile walk is
still there to fix.

### 8.8 Future work — naming to reconcile

> **Status:** PROPOSAL, nothing applied. A COSMETIC naming pass, not a structural merge.

With the command side consolidated to **SENDER** (`ibBackendCommandSender`) / **RECEIVER**
(`ibBackendCommandReceiver` ← `ibFrontendCommandReceiver`), the old `actionInfo` layer (§6) is
no longer "the command system." It is now just ONE of the three command sources a sender vends:
the **standard commands** set (`GatherFormCommands` section 2, §9), alongside the form's own
custom commands (section 1) and the global config commands (section 3).

Historically `actionInfo` WAS the whole command system — command == action, a single source —
so it still carries the legacy "Action" vocabulary, an *okamenelost* (fossil) of that origin.
The migration is already HALF done: the collection ENTRY is named `ibCommandItem`, not
`ibActionItem` (§6), and `ibActionID` is really a command HOP id in a reserved range — a
gathered standard command's path is `ibCommandDescription(actionId)`, and
`ibBackendCommandSender::GetCommandByHop` matches that id against the collection (§8.1).

**This is naming coherence ONLY — not a merge.** `actionInfo` stays a distinct DATA layer: the
per-object-type catalog of built-in actions. It must NOT be folded into sender/receiver, because
it has independent consumers — the command bar's AutoFill builds standard toolbars straight from
`GetActionCollection` (§5), and view-only greying reads the `m_modifiesData` flag off it (§5–§6).
Generalizing it into the command interface would ADD coupling, not remove it.

Suggested mapping (a proposal, not applied):

| today | rename to |
|---|---|
| `ibActionDataObject` | a sender facet — "standard commands" |
| `GetActionCollection` | `GetStandardCommands` |
| `ibActionCollection` | `ibStandardCommandSet` |
| `ibActionID` | a standard-command id (or leave as-is — it is already a hop id) |

**Cost — do it as its own pass.** `GetActionCollection` is implemented in ~11 per-type
`*Action.cpp` files with ~43 consumers, so the blast radius is wide. Land it as one focused
change, not tacked onto other work.

---

## 9. Projections, the gather, and the command sections

A command **projects** onto a form control that binds it through the command-source property
`ibPropertyCommandSource` (the command-door twin of a control's `ibPropertySource`; picker =
`ibPGCommandSourceProperty`). Two targets:

- **`ibValueButton`** (`button.cpp`) — a command dropped on the form (or onto an existing button)
  binds here; a button added from the palette carries the picker. Its `Update` shows the button's
  OWN Title / Picture on top and the **command's caption + icon as the DEFAULT beneath** (resolved
  live via the door). A command button with no own text shows the command's look; a plain event
  button is untouched. A data-modifying command greys the button on a view-only form. A press
  runs the leaf command; a **GROUP** command instead pops a **dropdown** of its sub-commands
  (`ResolveSubCommands` → `wxMenu`, nested groups become submenus), and the chosen leaf runs.
  **A button bound to a DELETED command HIDES itself** (`button->Show(false)` when
  `ResolveCommand` returns false — §8.7) — the SAME rule as a button with no command bound; the
  orphan stays in the object tree to rebind or remove. Existence + look come from the single
  `ResolveCommand` resolve, so a valid tablebox command is no longer wrongly hidden by the fragile
  walk alone.
- **`ibValueCommandBarItem`** (`commandBar.cpp`) — a command dropped on a tablebox lands here (its
  own bar); a bar item with no command bound is **not rendered** (no empty tool). A bar item whose
  command was **deleted** is likewise **SKIPPED** in `ibValueCommandBar::BuildCommands` (when
  `ResolveCommand` returns false — §8.7, the SAME resolve the button uses) — nothing to click.
  (Hub dropdown is button-only — the bar is slated for removal.)

**Parameterizable — the parameter VALUE (`ResolveCommandParameter`).** When a command whose
`GetParameterType()` names a reference is run, `CommandParameter` is filled from form context:
(a) the edited object — the form's main source object (`IS-A ibValue`) when its reference type
matches; else (b) the current row of the **focused** table / list of that type (the control that
owns the keyboard focus wins over some other same-typed control; falls back to the first match).
An untyped command gets no parameter. The delivery mirrors how `ExecuteParameters` wraps the form
— both are ref-counted `ibValue`s.

`GatherFormCommands(ibValueForm*)` (`commandBar.cpp`) is the ONE list feeding BOTH the navigator
panel and the picker, so they can't drift. `GetCommandSections()` names the three FIXED sections
(`{ "Form commands", "Standard commands", "Global commands" }`) — the surfaces pre-create all
three so they show even EMPTY, and the gather labels its entries with exactly these strings. The
gather walks **four SOURCES** in order (the fourth files under section-named groups, not the three
fixed ones):

1. **Form commands** (section "Form commands") — the form's own `ibFormCommandValue` events
   (`form->GetFormCommands()`), NOT metaobjects (§8.2). Each is a 1-hop `[id]` path; tree text = the
   command NAME, cell path = `Form.<name>`.
2. **Standard commands** (section "Standard commands", sub-grouped per source) — the standard
   actions of the form (`form->GetActionCollection`, sub-group "Form"), then of every tablebox
   (`CollectTableboxes`, sub-group = the table name). A form action carries `[actionId]`; a table
   action carries `[table-source, actionId]` so a click lands on THAT table. A tablebox bound to the
   form's MAIN source is SKIPPED (`IsMainSourceBound` — the form toolbar already serves it).
3. **Global commands** (section "Global commands") — every command metaobject in the config,
   `globalMeta->GetAnyArrayObject<ibValueMetaObjectCommand>({ g_metaCommonCommandCLSID,
   g_metaCommandCLSID }, /*child filter*/ true)`, deduped against section 1 and skipping deleted. A
   **parameterized** command (its `GetParameterType()` names ≥1 reference type) shows ONLY where the
   form carries data of a matching type (`CommandExcludedByType` against `CollectFormDataTypes` = the
   form's primary object type + every reference type in its source explorer); an untyped command
   shows everywhere.
4. **Section commands** (one group per interface Section, sub-grouped by AREA) — the form sees
   sections EXACTLY as the runtime menu builder: `walkSection` iterates each `ibValueMetaObjectSection`
   (`g_metaSectionCLSID`), pulls `GetInterfaceItemArrayObject(area, …)` for each area (§4), and recurses
   into sub-sections. EVERY item a section holds is pickable (a command, but also a Catalog / Constant /
   … checked into it): an OBJECT item carries the area's command TYPE (Create-area → create, else open
   the list), so the SAME object in Normal vs Create is a DISTINCT binding. Icon BY METATYPE
   (`item->GetIcon()`).

Each row is an `ibCommandSourceEntry` (`commandBar.h`) — the shared shape behind every surface:

| field | carries |
|---|---|
| `group` | the section it files under — one of the three fixed names, or a Section's own name |
| `label` | the TREE-LEAF text — the command's own NAME (the folders already carry the path) |
| `desc` | the `ibCommandDescription` hop path (+ command type) — what a drop / pick binds |
| `icon` | THE COMMAND's own picture — every surface reads it from HERE, never a second action lookup |
| `subgroup` | optional sub-group WITHIN the section (Standard nests actions per source: form vs each table) |
| `fullName` | the FLAT full path name — shown where there is NO tree to give the path (the bound cell) |

The form's data types = its primary object type (metadata) + every reference type in its source
explorer (recursive). The designer surfaces that consume this gather — the navigator tree, the
drop resolver, the inspector picker — are described in §10.

---

## 10. Designer surfaces — drag, drop, pick

Three surfaces let a designer wire a command; all read the SAME `GatherFormCommands` (§9), so a
command offered in one is bindable in the others.

**The command navigator tree — `ibVisualEditorCommandTree`**
(`designer/win/editor/visualEditor/visualEditorCommandTree.cpp`). The DRAG source and the
form-command editor. `RebuildTree` pre-creates the three fixed sections and fills them from the
gather; a leaf carries an `ibAttributeTreeItemData` holding the entry's `desc`. `OnBeginDrag`
serialises that desc through an `ibCommandDragItem` (engine memory writer, format
`oes_command_drag`) and starts the drag. The tree is READ-ONLY for standard / global / section
commands (drag-out only) — but it OWNS the **form commands** section: its context menu (gated to
that group) Adds via `form->AddFormCommand`, and Copy / Cut / Paste / Delete go through
`ibFormCommandValue::CopyToClipboard` / `PasteFromClipboard` / `form->DeleteFormCommand` (our own
clipboard format, exactly like a form attribute — NOT `activeMetaData` or the metaobject clipboard).
A form command IS a property object, so a click (`RevealSelectedCommand`) or double-click / Enter
(`wxEVT_TREE_ITEM_ACTIVATED`) reveals it in the inspector, mirroring the attribute tree.

**The drop — `CreateCommandButton`** (`visualEditorCmdProc.cpp`). The shared `ibSourceDragDropTarget`
decodes the dragged desc, seeds the display name by matching it against `GatherFormCommands`
(caption = `fullName`), then routes by WHERE it landed — three targets, in order:

1. a drop that landed on a command-bar TOOLBAR — the widget was tagged by `BuildCommandBarToolBar`
   with the `oes_command_bar_toolbar` name + its bar as client data, resolved by
   `ibCommandBarFromToolBar`; the drop resolver stashes it in `m_pendingDropBar`. Read + clear that
   channel → `bar->AddCommandItem()` bound to the desc. This covers the form toolbar AND a tablebox's
   own toolbar.
2. else climb from the target to the nearest `g_controlTableBoxCLSID` — if it `HasCommandBar()`, add
   the item to `box->GetCommandBar()`.
3. else (the form canvas or a plain control) — `CreateControlOfClass(target, "Button", …)`, a new
   `ibValueButton` bound via `SetCommandDesc`, Caption left unset so the command supplies the default
   look. A drop ALWAYS ADDS a button — even onto an existing one — never silently rebinds the target.

**The inspector picker — `ibPGCommandSourceProperty`** (`advpropCommandSource.cpp`). The registered
editor for `ibPropertyCommandSource`. `ResolveDoor` finds the command door from the property's owner
(a button / bar is-a door; a bar item delegates to its bar), asks it for `GetCommandGateForm`, and
builds a modal tree from `GatherFormCommands(form)` — the SAME three pre-made sections + per-source
sub-groups the navigator shows. OK on a real leaf commits a new `ibVariantDataCommandSource(owner,
desc, label)`, wiring the owner so `MakeString`/`WalkCommand` keep validating live (§8.3, §8.6). The
cell's own live string comes from `ValueToString` → the variant's `MakeString`; the cell IMAGE from
`OnSetValue` → `door->ResolveCommand` (§8.7).

---

## 11. Extending the command interface

The whole point of the sender / receiver split is that the two extension axes are independent: a new
**place commands come from** and a new **control that hosts a command** never touch each other.

### 11.1 Add a new command SOURCE (a new place a command is vended)

A source is anything the walk can hop INTO. To make a value vend commands:

1. **Make the vending value an `ibBackendCommandSender`** — inherit `ibBackendCommandSender`
   (`backend/backend_command.h`) and override `GetCommandByHop(const ibCommandHop&, ibValue& out)`:
   match the hop id against what you hold and set `out` to the next command-capable VALUE (a leaf
   command, or another sender to keep the walk climbing). Return `false` for an id you don't own — the
   walk stops there, exactly as a source path stops on a non-source value. This is the ONLY resolve
   logic; there is no central switch. Mirror the existing senders: `ibValueForm::GetCommandByHop`
   (`formVisualHost.cpp`), `ibValueWindowComposite` (a tablebox, `window.cpp`),
   `ibValueMetaObjectCommand` (a group, `metaCommandObject.cpp`), `ibValueMetaObjectSection`
   (`metaSectionObject.cpp`).
2. **Make the FORM reach it** — the walk always starts on the form (`ExecuteValueByPath` /
   `ResolveValueByPath` cast the gate form to `ibValue`), so `ibValueForm::GetCommandByHop` must
   resolve the FIRST hop to your value (or your value must be reachable through a hop it already
   vends, e.g. a control the form descends into).
3. **List it in `GatherFormCommands`** (`commandBar.cpp`) — add a block that pushes an
   `ibCommandSourceEntry` per command your source offers (`group`, `label`, `desc` = the full hop
   path, `icon`, optional `subgroup`, `fullName`). Without this the walk would still RUN the command,
   but no designer surface would OFFER it and `ResolveCommand`'s reliable fallback (§8.7) could not
   confirm it — the navigator, picker and the tablebox-fragility patch all key off this list.
4. **Teach the READ + RUN leaf kinds if the leaf is a NEW type** — `ResolveValueByPath` and
   `ExecuteValueByPath` (`commandReceiver.cpp`) switch on the resolved leaf (form command / command
   metaobject / object item / standard-action frame). A brand-new leaf kind needs a case in each; a
   leaf that is already one of those four needs nothing.

### 11.2 Add a new command PROJECTION (a new control that hosts a command)

A projection is any control that binds ONE command and renders it. To add one:

1. **Inherit the door** — `class ibValueMyControl : public <its control base>, public
   ibFrontendCommandReceiver` (`commandReceiver.h`). You get `WalkCommand`, `ResolveCommand`,
   `ExecuteValueByPath`, `ResolveValueByPath`, `ResolveSubCommands` for free — do NOT re-implement
   them (that drift is exactly what §8.7 removed).
2. **Supply only `GetCommandGateForm()`** — the one pure virtual: return the form the walk starts
   from (a button returns `GetOwnerForm()`; a bar item delegates to its bar). That is the entire
   per-control contract.
3. **Carry an `ibPropertyCommandSource`** — add the property (picker = `ibPGCommandSourceProperty`,
   auto-registered) and serialise its node value in Read/WriteData, like `ibValueButton::m_propertyCommand`.
4. **Resolve in Update** — call `ResolveCommand(desc, caption, icon, &modifies, nullptr,
   &picAndText)`; a `false` result means the binding is dead (hide / skip the projection, as the
   button and bar item do); otherwise feed caption / icon / representation and obey `modifies` for
   view-only greying. Run on press through `ExecuteValueByPath(desc)`; for a GROUP leaf, pop
   `ResolveSubCommands` as a menu. Follow `ibValueButton::Update` (`button.cpp`) and
   `ibValueCommandBar::BuildCommands` (`commandBar.cpp`) verbatim.
5. **Cast reaches it across the DLL** — because the door's backend base (`ibBackendCommandReceiver`)
   anchors its vtable out-of-line (§8.3), the backend variant's `dynamic_cast` resolves your
   frontend-built control automatically; nothing to wire.

The chain (§8.5) grows by one leaf:
`ibBackendCommandReceiver ← ibFrontendCommandReceiver ← { ibValueButton, ibValueCommandBar,
ibValueCommandBarItem, your control }`.

---

## 12. Type identity — no `dynamic_cast` for a command

Per the project convention (**type by clsid/kind**), command identity uses the **kind-typed
clsid**, never `dynamic_cast`: a typed find with a clsid filter (`FindAnyObjectByFilter<T>(id,
g_metaCommandCLSID, …)` / `GetAnyArrayObject<T>({g_metaCommandCLSID}, …)`) `static_casts` on a
clsid match and returns the exact type — the door, the gather's type filter, and the navigator
all use it. A control drop-target is likewise identified by `GetClassType() == g_controlButtonCLSID`
+ `static_cast`, not `dynamic_cast`. `dynamic_cast` survives only for genuine cross-type
**capability** mixins (`ibTypeControlFactory` — "does this control carry a source", used across
the form editor) and for `wxTreeItemData`.
