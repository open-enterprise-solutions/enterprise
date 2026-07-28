# Command metaobject — commands as a first-class metatype

> **Status:** CORE LANDED 2026-07-20 (green, Debug|x86); command WALK reworked into a
> **server-side hop interface** 2026-07-27 (uncommitted / just landed — see the block below).
> The command, the metatype, object commands, the runtime, and the form-button binding are in
> the tree and compile. The design below (§1–§13) is the original spec; the **As-built** blocks
> record where the landed code diverged from it. Sections still marked *plan* (navigator drag,
> general-command placement, desktop, roles / functional-option gates, subsystem rename) are not
> built.
>
> ### As-built (2026-07-27) — the command walk is a SERVER hop interface
>
> The front-end command resolver was replaced by a backend hop interface, the exact mirror of the
> source-hop (`command-interface.md` §8 has the full write-up). What changed since 2026-07-20:
>
> - **`ibBackendCommandDataObject`** (`backend/commandDataObject.h`) — the command-side twin of
>   `ibSourceDataObject`: `virtual bool GetCommandByHop(const ibCommandHop&, ibValue& out) const`
>   plus a static inline `ResolveCommandPath` (the mirror of `ibSourceDataObject::ResolvePath`,
>   now also a static inline). Each hop's result is a command-capable value that self-describes
>   the next hop; the loop is uniform, no per-case ladder.
> - **The FORM declares the hop server-side.** `ibBackendValueForm` now inherits
>   `ibBackendCommandDataObject` and declares `GetCommandByHop` **pure virtual** — so a headless
>   caller (web server / daemon / codeRunner) can walk commands with no front-end.
>   `ibValueForm::GetCommandByHop` (`formVisualHost.cpp`) implements the entry hop (form command /
>   config-object command via the form's own metadata / object item / descend into a bound
>   composite control / a standard action on the form's bus).
> - **Command sources each resolve their OWN hop:** `ibValueWindowComposite` (a tablebox — its own
>   actions; a plain control / sizer is NOT command-capable), `ibValueMetaObjectCommand` (a group
>   command hops into its sub-commands), `ibValueMetaObjectSection` (hops into sub-sections / its
>   own commands / interface items — a section has NO runtime, it only ROUTES; the leaf carries the
>   mini-runtime via `Execute`).
> - **The front-end door `ibCommandDataObject` is now THIN.** It starts `ResolveCommandPath` on the
>   form and switches on the resolved LEAF (form command → `CallAsEvent`; command / object item
>   `ibBackendCommandItem` → `Execute`; a frame → `CallAsAction`). The old 5-case `ResolveEndpoint`
>   / `ExecuteValueByCommandHop` ladder and `FindControlBySource` are GONE.
> - **Const walk / transient runtime / clean data-vs-command split** — walking never mutates; the
>   leaf command spawns a transient runtime for the call; a plain data value is not command-capable
>   (only command-bearing things implement `ibBackendCommandDataObject`).
>
> ### As-built (2026-07-20) — read this before the C++ shapes below
>
> - **Metatype.** `ibValueMetaObjectCommand : ibValueMetaObject, ibBackendCommandItem`
>   (`backend/metaCollection/metaCommandObject.{h,cpp}`). It does **not** inherit
>   `ibValueMetaObjectModuleBase` (the §2 shape); like a Constant it *holds* a **plain** inner
>   module (`ibPropertyInnerModule<ibValueMetaObjectModule>`) carrying the handler. A plain
>   module does **not** register with the module manager — a command handler is never called by
>   name from script, so it needs no external export (`feedback` on the manager path: the owner
>   chose descriptor-direct over common-module).
> - **Runtime = the command's OWN descriptor.** A private `ibValueCommandDataObject :
>   ibValueDynamicMembers, ibRuntimeModuleDataObject` compiles the command's module and runs
>   `CommandProcessing` on its own ProcUnit — the same sequence a record/constant object uses
>   (`EditModuleManagerFor → SetParent → InitializeRuntime → Compile → Run → ExecAsProc`). No
>   module-manager registration, no name-resolvable exports.
> - **`Execute` is overridden per metaobject, not branched in the base.** The base
>   `ibBackendCommandItem::Execute` opens a form (`GetFormByCommandType → ShowForm`) and nothing
>   else; a command **overrides** `Execute` to run its handler. `RunCode` (the §2/§6 branch) was
>   **removed** — there is no form-vs-code `if` in the base; each metaobject predefines its own
>   behavior.
> - **Object commands.** Every business object owns a **Commands** child node, exactly like
>   Attributes / Forms / Templates — one line in `GenericData::ResolveChild` (`clsid ==
>   g_metaCommandCLSID`) gave it to all of them at once; the designer tree grew a top-level
>   *Commands* branch plus a per-object *Commands* sub-branch. Ownership is **structural** (the
>   command is the object's child) — `GetCommandArrayObject()` returns just that object's commands.
> - **Form button binding.** `ibValueCommandBarItem` carries `m_propertyCommand` (an
>   `ibPropertyList` picker) storing the command by **metaId**; `ExecuteCommand` resolves it in
>   the running config (`activeMetaData->FindAnyObjectByFilter<…, ibMetaID>`) and runs it via the
>   unified `Execute`. NB: §9 argues "never `activeMetaData`" — the runtime execution point
>   **does** use it (the picker still lists from the form's own tree; execution runs in the live
>   image). This is a conscious deviation forced by the const-wall on the frame's `GetMetaData`,
>   matching how `wfrontend` / `mainFrame` already execute command items.
> - **AutoFill surfaces object commands.** With AutoFill on, a form's command bar auto-adds the
>   owner object's own commands (via `GetCommandArrayObject`) as transient buttons — "автогенерация
>   учитывает команды объекта". General-command placement is still *plan*.

---

## 1. The flaw was not absence — it was a JAMMED command

The command metaobject already exists: `ibBackendCommandItem`
(`backend/metaCollection/metaFormObject.h:16`). Its whole contract is
`GetFormByCommandType` → `ShowForm`. It is **degenerate on two axes**, the way the
connection pool was degenerate at `pool@1`:

- **behavior jammed at "open a form".** A command can only return a form and show it.
  "Post", "Export to bank", "Recalculate totals" are inexpressible — not because anything
  is broken, but because the parameter is stuck at one value.
- **tenant jammed at "metaobject".** Only metaobjects implement `ibBackendCommandItem`
  (`genericData.h:90`, `metaFormObject.h:207`, `constant.h:29`). There is no standalone
  general command.

A degenerate primitive **looks** functional (the form opens), which is exactly why the
flaw resisted naming: it is a jam, not a hole. The fix un-jams the parameter — it is
*extension of an existing seam*, never a green field.

---

## 2. Three axes of un-jamming

| Axis | Now | Target |
|---|---|---|
| **behavior** | `GetFormByCommandType` → `ShowForm` | `Execute` (form **or** module code) — reuses the existing `try/catch` envelope of `ShowFormByCommandType` |
| **tenant** | metaobjects only | one metatype `ibValueMetaObjectCommand : ibValueMetaObjectModuleBase, ibBackendCommandItem` for **both** object and general commands — same runtime module, only `m_scope` differs (`Object(owner)` vs `General`, "available to all") |
| **placement** | `m_interfaces` flat `set` + `GetCommandSection()` by kind | **value-table** — the same primitive as `form-attribute-binding.md` (landed) |

```cpp
// axis 1 — behavior stops being jammed at "form"
class BACKEND_API ibBackendCommandItem {
    virtual bool Execute(const ibCommandContext& ctx);          // form OR code
protected:
    virtual ibBackendValueForm* GetFormByCommandType(ibCommandType) { return nullptr; }
    virtual bool                RunCode(const ibCommandContext&)    { return false;   }
    virtual bool                ModifiesData() const                { return true;    } // view-only greying
};
```

---

## 3. Two doors of a form — the split is already in the code

A form has two providers, already separated (`reference_form_command_provider_split`).
The command door is the parallel of the data door, one level up:

| | data door (built) | command door (this arc) |
|---|---|---|
| provider | source-object (metadata-free) | **command-provider** |
| hop cargo | VALUE (schema-on-read) | **CALL TARGET** (what to run) |
| projection on the form | control (via dot-path) | **button** (via command-ref) |
| resolve | source-hop (dot-walk over references) | **command-hop (simpler)** |

`command-hop` is simpler because it carries a *terminal call target*, not a typed value.

---

## 4. command-hop — three topologies

1. **terminal** — `button → #metaId`. One command.
2. **hub** — `button → group#id → subcommand`. Recursion of the command (a command holds
   commands), rendered as a submenu — **not** a second mechanism, exactly as a subsystem
   holds subsystems.
3. **bound-to-source** — `command → form's source-object → output`. Where the two doors
   meet: a command that needs data takes the *same* source-object that feeds the controls.
   Do not introduce a second source.

⚠️ **bound-to-source** (command *pulls data* from source) ≠ **command source #4 below**
(source *supplies its own commands*, `ibStandardCommandTabular`). The source-object plays
two roles — data supplier and command supplier — keep them distinct.

---

## 5. Four command sources of a form → one set via hop

A form gathers commands from four sources; hop resolves all of them into one set. **Each
source knows its own runtime**, so "point at the right source" is enough — hop routes the
call into the correct runtime.

| source | supplies | execution runtime | example |
|---|---|---|---|
| **general** · global array | commands available to all by default | command module — same mechanism as an object command, **scope = all** | Print, Export |
| **object** · source's metaclass | object commands | command module — **scope = the object** | Post, Unpost |
| **manual** · designer-added | what was dragged onto the bar | **the form module** (form-runtime, arbitrary code) | your action |
| **data** · source-object | row operations | source-object | Add, Delete, Copy |

The union is de-duplicated and **ordered by the placement table**, not by C++ layout.

General and object commands are the **same command** (`ibValueMetaObjectCommand`, a module
command); only `m_scope` differs — global vs bound to an owner. That is unification which
serves both (§10), not a rule imposed on one runtime kind.

---

## 6. Navigator + command identity

**Navigator** — a section in the form-designer tree that collects *all available* commands
by group (general · from source · object · custom). It is the gather mechanism the designer
drags from.

**Command identity = a piece of runtime code.** A custom command is its own section that
references code in the appropriate runtime. Its projections — button · submenu item ·
desktop element — all hold one `metaId` and invoke **one and the same action**. Wherever it
is attached, the code is the same.

**The command body is one handler**, the same for object and general commands:

```
Procedure CommandProcessing(CommandParameter, ExecuteParameters)
    // CommandParameter   — what the command runs on (delivered by the hop)
    // ExecuteParameters  — execution context: source-object, owner form, window
EndProcedure
```

`RunCode` dispatches into this handler; the hop is what fills the two parameters. Object and
general commands differ only by `m_scope` — the handler shape is identical.

---

## 7. The command-picker property

A new property type on `ibValueCommandBarItem`: it stores the **path to the command**
(the command-hop path, keyed by `metaId`) — the parallel of the dot-path picker on the data
door. Two binding paths feed the *same* property:

- **drag** from the navigator onto the form → creates a button + sets the path.
- **picker** in the inspector of an existing button → rebind by hand.

On selection (either path) the command's **picture and behavior properties are pulled in
live** (tooltip, representation) — they are not edited on the button; the button is a
projection.

```cpp
// the button holds the command by metaId and resolves it LIVE, in its OWN tree
class FRONTEND_API ibValueCommandBarItem : public ibValueLayerObject {
    ibMetaID m_commandMetaId = wxNOT_FOUND;               // reference, not a copy
public:
    ibBackendCommandItem* ResolveCommand() const {
        const ibMetaData* meta = GetOwnerFrame()->GetMetaData();   // concrete tree, see §9
        return meta->FindCommandByMetaId(m_commandMetaId);
    }
};
```

---

## 8. Surfaces — one row structure, different owners

Three surfaces read the *same* command through the *same* placement structure; only the
owner of the rows differs:

| surface | placement owner |
|---|---|
| section menu (`ibSubSystemWindow`) | the subsystem |
| form bar | the form |
| **desktop** | the **root metaobject** (configuration) — right-click in the designer to attach reports / data processors / lists (funnel, plan); they render one under another |

The section menu keeps working; it is simply filled by commands through placement instead
of `GetCommandSection()` by kind.

The **config-root** owns more than the desktop: also the **section-panel order** and the
**home page**. The desktop is one facet of that root surface.

**Visibility gates — a separate arc.** A command shows only if all gates pass: **role**,
**auto-rights** (hidden for objects the user cannot access), and — config-wide —
**functional options** (declarative feature flags that enable/disable whole slices of
functionality). **Roles and functional options are wired in their own arc, not this one.**
Functional options are not command-specific — they gate data and controls too. The command
door **honors** the gates; it does **not** own them. Folding them into the command metaobject
would impose one устав on residents it does not fit (§10). Here they are just a gate the
placement query consults — leave the hook, wire the mechanism separately.

---

## 9. Metadata comes from the CONCRETE tree — never `activeMetaData`

Commands live in **different trees** (the active configuration, but also an external
report / data processor with its own metadata). Resolve every command / button / provider
through the metadata of the tree it belongs to — `GetMetaData()` / `m_metaData` — **not**
`activeMetaData`. The active image is needed only in the few places that literally mean
"the current main window".

The correct pattern is **already in the code**:
`ibValueMetaObjectInterface::GetInterfaceItemArrayObject` walks `m_metaData->GetAnyArrayObject()`,
not the active image. Sprinkling `activeMetaData->…` through command resolution is the
"culture of omission" — a mechanical global grab where an honest walk to the concrete tree
is required. Kin to `feedback_srcdataobject_metadata_free`.

---

## 10. Governing principle — do not impose one устав on all commands

**Unify only when the single system serves every tenant well.** The command door is worth
merging into one command + one hop + one placement **only because** it can be made good for
all four sources, all three topologies, and all three surfaces at once. If any tenant would
be served badly by the shared rule, do not force the rule on it — split, or leave it out,
rather than impose a uniform устав that half the residents suffer under. This is the
coherence test for the whole arc, not a nicety.

---

## 11. Reuse / yield (concrete nodes)

| node today | fate | into |
|---|---|---|
| `ibBackendCommandItem` | **reuse** | `GetFormByCommandType` → `Execute` (form ∨ module) |
| `ShowFormByCommandType` try/catch | **reuse** | ready execution envelope |
| `ibSubSystemWindow` + docking | **reuse** | the "section" surface frame |
| `AccessRight_Use` | **reuse** | visibility by role, extended to commands |
| `ibScrolledSubWindow` (270 lines of hand-built sections) | **yield** | data-driven loop over placement groups |
| `GetCommandSection()` by kind | **yield** | a "group" column in the table |
| `m_interfaces` flat `set` | **yield** | value-table rows (with order) |
| `OnMenuItemClicked` → `FindAnyObjectByFilter(metaID)` | **edit** | resolve to a command, not only a metaobject |
| desktop | **new** | second surface, same table |

---

## 12. Implementation order (owner's route)

1. **Form + navigation through a command.** *Detach* the command (do not rename the method);
   resolve through the concrete tree from the first line (§9).
2. **Object commands + typing.** Type via the clsid-kind system, not `metaID` equality.
3. **Auto-bind the source by type** (kind-match) — the `N×M → N+M` lever.
4. **General commands** (Execute branch: Print, subordination structure).
5. **Desktop** surface.

`Interface → Subsystem` is a **cosmetic rename** (the code already says "Sybsystem"), not
the fix. ⚠️ Migration tail: `"Interface"` sits in `METADATA_TYPE_REGISTER` / serialization —
check the cost before renaming. Attach the rename to step 2 or defer it; a mass rename is
churny (`naming-plan`).

---

## 13. Prototype

The executable spec lives as an artifact (5 tabs; the JS model reads as the architecture —
`Command` command, `Kind` clsid-typing, `placement[]` value-table, three surfaces reading
one bus). It is the tutor to collapse from, per `reference_concrete_before_primitive_not_cost`.
The next physical step is **step 1 in the tree**.

## 14. Related

`command-interface.md` (the Interface metaobject as-is) ·
`form-attribute-binding.md` (the value-table primitive to reuse) ·
`property-system.md` (where the command-picker property lands) ·
`view-only.md` (`m_modifiesData` greying, which the command must carry) ·
`source-object.md` (the data door the command door parallels).
