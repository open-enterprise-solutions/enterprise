# Metadata lifecycle — load, run, save, close, and the events in order

> **Scope:** how a configuration's metadata is loaded, initialised, saved, and closed — the
> container (`ibMetaData`), the self-serializing node (`ibValueMetaObject`), the per-node
> **event sequence** that fires at each phase, and how **external Reports / DataProcessors**
> differ. A MAP of code that **already exists**; the arc docs below hold the design rationale.
>
> **Companions:** [metadata-serialization-arc.md](metadata-serialization-arc.md) (the detached-root
> + two-phase design rationale, superseded byte-walk history), [schema-first-metadata.md](schema-first-metadata.md)
> (the shipped node-serialization names), [metadata-storage-container-arc.md](metadata-storage-container-arc.md)
> (file vs storage split), [metadata-hot-reload.md](metadata-hot-reload.md) (live reload),
> [metadata-tree.md](metadata-tree.md) (designer navigator + external reports/processors),
> [metadata-containers.md](metadata-containers.md) (the `ibMetaData` family — the mechanism and its
> varieties: configuration vs external report / data-processor),
> [runtime-facade.md](runtime-facade.md) (why the runtime survives a reload).
>
> **Status:** landed. Node-owned `ibDataNode` serialization; detached-root atomic load/swap;
> buffer-then-single-write save. The one-line container summary is in [../CLAUDE.md](../CLAUDE.md) §7.

---

## 1. The pieces

| Thing | Class | Role |
|---|---|---|
| **Container** | `ibMetaData` (+ `ibMetaDataConfiguration*`, `ibMetaDataDataProcessor`, `ibMetaDataReport`) | owns one open configuration; orchestrates load/run/save/close |
| **Open state** | `std::shared_ptr<ibMetaImage> m_image` | **presence == open** (`IsConfigOpen()` ≡ `m_image != nullptr`); owns the type-ctor factory, module storage, compile cache. `LoadGuard` RAII builds it at run start, rolls it back on an incomplete run |
| **Tree root** | `m_commonObject` (`GetCommonMetaObject()`), an `ibValuePtr` | the whole metadata tree; the **unit of atomic replacement** |
| **Node** | `ibValueMetaObject : ibPropertyObjectHelper<ibValueMetaObject>` | a metaobject; children are its property-object children (`m_children` = owning `ibValuePtr`s). Destroying the root cascades the subtree |

There is no separate parent/child model for metadata — composition **is** property-object
composition ([property-system.md](property-system.md) §5).

---

## 2. The node serializes itself

The byte `SaveData`/`LoadData` chunk-walk is **gone**. A node writes itself into, and reads
itself from, the universal `ibDataNode` structure (`serialize/dataBuilder.h`), rendered to bytes
by a pluggable `ibFormatProvider` — `ibBinaryProvider` (round-trip) or `ibJsonProvider`
(write-only, for diff/inspection). Methods on `ibValueMetaObject` (`metaObject.h`,
`metaObjectSerialize.cpp`):

| Method | Does |
|---|---|
| `BuildDataNode(node, flags)` | `SaveNode(this)` + `OnSaveMetaObject` + recurse children |
| `ApplyDataNode(node, resetId)` | factory-create children by clsid + `LoadNode(this)` + recurse |
| `SaveNode` / `LoadNode` | common header (clsid / guid / id / name), then delegate per-type data |
| `WriteData` / `ReadData` | **per-type** payload (base has none; each metaobject type overrides) |
| `ContributeTables(out)` | declares this node's tables into an `ibSchemaSnapshot` — **the one source that drives DDL** |

The container is a thin forwarder: `LoadCommonTree` / `SaveCommonTree` / `DeleteCommonTree` →
the root's `ApplyDataNode` / `BuildDataNode`.

**The child walk is FILTERED.** `BuildDataNode` recurses only the children the type's
`ResolveChild` admits (`FilterChild`), so a child a metaobject holds without publishing it as a
tree node is invisible to the walk and has to be carried by its owner's `WriteData` / `ReadData`.
The accumulation register's two totals metaobjects are exactly that — nested, absent from
`ResolveChild`, and written as sub-nodes of the register's own node. That is what makes their
metaID survive a save; without it every register's totals would load carrying the same id, and
`ibSchemaSnapshot::Shared` matches a table by id alone.

**Row data is separate.** Table *contents* do not ride the config blob — they move through the
L3-3 `ibDataMover` (`DumpDataToBuffer` / `RestoreDataFromBuffer` on the storage container),
driven by the same `ContributeTables` snapshot that drives the DDL. Boundary: **File = config
metadata, Storage = live table data.**

**Derived tables are a third case.** A table the snapshot marks `m_derived` (a register's totals)
holds rows that are a FUNCTION of another table's rows, so it belongs to neither category above: it
is not configuration, and it is not data worth moving. One bit routes it through all four floors —
L3-2 builds it *and* its trigger/view bundle, L3-3 **skips** it in both directions, and **L3-4**
(`ibDerivedState`, `query/derivedStateBuilder`) rebuilds it from the source when the trigger cannot
have kept up: a table created over an already-populated source, a restore, or a change to the
grouping shape.

The order matters on a restore — L3-3 loads the source rows first, then L3-4 regenerates from them.
Carrying the derived rows instead would ship a redundant copy at best, and at worst a stale one
that silently disagrees with the movements it claims to summarise.

`NeedsRegeneration` keeps the common case free: a column merely ADDED has no history, so its
correct value everywhere is the zero the `ALTER` default already wrote. Everything else rebuilds —
skipping a needed rebuild yields silently wrong totals, while a needless one only costs time. The
KEY SHAPE (a dimension, the stored grain, the shard split) and the NUMBER OF ACCUMULATIONS both
force a rebuild: re-keyed rows cannot be migrated in place, and gaining a second accumulation
changes what the first one means, so every stored figure is wrong even though its column still
exists.

**A derived table is REPLACED, never altered.** When `NeedsRegeneration` says its shape changed,
L3-2 drops the table and creates it fresh instead of emitting ALTER clauses. It holds no
information of its own — the regeneration recomputes every row regardless — so migrating it in
place buys nothing while costing the whole class of migration edges: re-keyed rows, an index over a
column being dropped, a field the physical table has and the baseline does not.

**A derived table that VANISHES must be un-maintained before it is dropped.** Its triggers live on
the SOURCE table and only mention it by name, so `DROP TABLE` leaves them behind, firing on the next
write into a table that is gone. The differ therefore calls `ibDropMaterialization` with the
BASELINE spec — the only thing that still knows the old names — before dropping. The register KIND
SWITCH is this path in practice: balances and turnovers keep separate tables under separate ids
(`GetTotalsObject()` — one totals METAOBJECT per kind, so the ids are ordinary metaIDs), so
switching is a drop of one plus a create-and-regenerate of the other, never an ALTER.

---

## 3. The node event set

Every phase fires virtual hooks on `ibValueMetaObject` (`metaObject.h`):

```
OnCreateMetaObject(metaData, flags)   OnLoadMetaObject(metaData)
OnSaveMetaObject(flags)               OnDeleteMetaObject()
OnRenameMetaObject(name)              OnReloadMetaObject()          // designer
OnBeforeRunMetaObject(flags) / OnAfterRunMetaObject(flags)         // run:   Before = register, After = resolve
OnBeforeCloseMetaObject()    / OnAfterCloseMetaObject()            // close: Before = un-resolve, After = un-register
```

Two orchestrators recurse the tree, firing one pass per phase on every node: `RunSubtree(flags, ibRunPhase)`
(top-down) and `CloseSubtree(ibRunPhase)` (bottom-up). The phase is an
`enum class ibRunPhase : unsigned char { Before, After }` — **two** passes (not a bool), mirror-named across
run and close so the sequence is predictable:

- **run** — `Before` = **register** (a node announces its identity / type ctor); `After` = **resolve**
  (cross-object references, `RegisterSource`, lazy form / object-module builders — every identity is present
  by now). Two passes because resolve needs every identity registered first.
- **close** — the LIFO mirror: `Before` = **un-resolve** (`UnregisterSource`); `After` = **un-register**.

There is no separate "build" pass: forms and object-module values are built **lazily** (§5), so the resolve
pass only registers a builder — it does not need a third synchronous phase.

---

## 4. LOAD — open a configuration

`LoadConfigFromFile` / `LoadDatabase` → `LoadCommonTree`, **detached-root, all-or-nothing**:

1. **Preflight** — header / sign / version / integrity compatible? On failure the config is
   simply "not loaded", never half-loaded.
2. `BuildFreshRoot()` — a **detached** fresh root (does NOT touch `m_commonObject`) → fires
   **`OnCreateMetaObject`** on the root (a "main" metaobject; OnCreate gates predefined
   sub-structure and fires only on the root + a manual add in the designer).
3. `ApplyDataNode` recurses: the factory creates each child by clsid, then **`OnLoadMetaObject`**
   fires per node. Blob-loaded children get OnLoad **only** — the factory `Init` does *not* call
   OnCreate; guid / id come from `LoadNode`.
4. **Success → atomic swap** `m_commonObject = fresh` (releases the old root, cascades). A throw
   discards the fresh root; the live tree is untouched.

The swapped-out old tree gets close events (via the pre-`CloseDatabase`) but **no**
`OnDeleteMetaObject` — this is replace semantics, not deletion. (The root sees a double
`OnLoadMetaObject` — `BuildFreshRoot`'s setup + the blob load — which matches the pre-detached
flow and is not a regression.)

---

## 5. RUN / INIT — bring the tree to life

`RunDatabase(flags)` drives the two passes with `CreateMainModule` seeded between them:

```
RunDatabase
 ├─ RunSubtree(Before)  → OnBeforeRunMetaObject  → REGISTER: each node's identity / type ctor
 ├─ CreateMainModule                             → seed the editor context, now that every identity exists
 └─ RunSubtree(After)   → OnAfterRunMetaObject   → RESOLVE: cross-refs, RegisterSource, lazy form/module builders
```

This is the **initialisation** phase. The split matters: the register pass makes every type ctor present,
`CreateMainModule` seeds the module manager on top of them, then the resolve pass wires cross-object
references and registers each node's L4 query source. Skipping the run entirely (loading a config without
running the fresh tree) leaves types unregistered — the concrete bug it caused was a reference attribute
read as a string → a `DROP` on a non-existent column → FB-607. So load is always followed by a run.

Forms and object-module values are **not built in this pass** — the resolve pass only registers a lazy
builder (`ibDeferredForm` / a compile-module thunk) into the compile cache; the real build runs on first
`FindCompileModule`, after the whole graph is resolved. That laziness is why the run needs only two phases,
not a third "build" pass (see [copy-paste.md](copy-paste.md) for the deferred-form case).

---

## 6. SAVE — apply a configuration

The save needs no detached-root machinery; it is **preflight → generate → commit**, atomic by
construction:

1. **Preflight** — `ibRestructureInfo::RequireExclusiveForDDL` (can we close / restructure? —
   acquires exclusive mode, throws if other sessions are connected).
2. **Generate** — `SaveCommonTree` → `BuildDataNode` → per-type `WriteData` into a **buffer**.
   `OnSaveMetaObject` fires per node (a leaf module hook does debugger `SaveModule` +
   `ibByteCodeCache::Invalidate`, gated `saveConfigFlag && DesignerMode`).
3. **DDL diff** — old tree vs new by guid → `CreateAndUpdateTableDB` (built from the
   `ContributeTables` snapshot) inside **one** DB transaction.
4. **Two-layer commit ordering** — DB commit **first**, then the in-memory root swap. Never the
   reverse: a DB rollback must not leave live memory ahead of the database.
5. `OnSaveDatabase` / `OnAfterSaveDatabase`.

Commit points differ by target but the staging is uniform (buffer / detached tree, no external
side effects until commit):

| Operation | Staging | Single commit point | OnSave fires? |
|---|---|---|---|
| Save → config blob (Apply) | `ibWriterMemory` buffer | DB UPSERT inside the apply TX | **yes** (`saveConfigFlag`) |
| Export → file (`.osv` / `.oap`) | `ibWriterMemory` buffer | temp file + `std::filesystem::rename` | no (`saveToFileFlag` = export) |
| Delete (object removal) | — | DB TX (DDL rollback) → `DeleteSubtree` → `OnDeleteMetaObject` | — |

---

## 7. CLOSE

`CloseDatabase(flags)` is the LIFO mirror of run — `DestroyMainModule` between the passes:

```
CloseDatabase
 ├─ CloseSubtree(Before)  → OnBeforeCloseMetaObject  → UN-RESOLVE: UnregisterSource
 ├─ DestroyMainModule                                → release the manager before its node is reset
 └─ CloseSubtree(After)   → OnAfterCloseMetaObject   → UN-REGISTER: drop the type ctors
```

---

## 8. External Reports / DataProcessors

An external `.epf` (DataProcessor) / `.erf` (Report) has its **own container**,
`ibMetaDataDataProcessor` / `ibMetaDataReport` (`: ibMetaData`) — near-identical twins of the
config container, kept **separate on purpose** (Report specialises later; do not merge into a
template — they already share the node methods). Differences from config:

- **It owns its own runtime.** `m_commonObject : ibValuePtr<ibValueMetaObjectDataProcessor>`
  **and** `m_moduleManager : ibValueModuleRuntimeManagerExternalDataProcessor` (owning handle;
  its dtor runs `DestroyMainModule` — RAII). The **config has no manager member** — the config
  runtime lives **per session** (`ibSession::m_root` + `m_lambdaRuntime`), see
  [runtime-facade.md](runtime-facade.md).
- **The file IS the storage.** `LoadFromFile(path)` uses the same detached-root as config
  (`BuildFreshRoot` → `LoadCommonTree(fresh)` → swap), and rebuilds the module manager on the
  fresh root (the manager is a *runtime* concern — the fresh root loads manager-free). `SaveDatabase()`
  is a no-op, so **`SaveToFile` (temp + rename) IS the commit** → `OnSaveMetaObject` **fires**
  here (unlike config's file export, which is only an export and skips OnSave).
- **Runtime entry.** `CreateObjectRef(clsid / className)` mints the external object value;
  `StartMainModule` / `ExitMainModule` raise / tear down the manager. `GetManagerModule` is
  runtime-only — never touched during `ApplyDataNode`.
- **Load-into-config branch.** `LoadFromFile` also has an inner branch that reads the object and
  copies it into an *existing* config tree (load-into-existing, no swap) — how an external object
  is imported into a configuration.

So: **config commits to the DB and runs per-session; an external DP/Report commits to its file
and owns its own runtime.** That single difference explains every branch above.

---

## 9. Where it lives

| File | Holds |
|---|---|
| `backend/metaData.h` | `ibMetaData`, `ibMetaImage`, `LoadGuard`, open-state |
| `backend/metadataConfiguration.{h,cpp}` | config container — `LoadConfigFromFile` / `SaveConfigToFile`, `LoadCommonTree` / `BuildFreshRoot`, the swap |
| `backend/metaCollection/metaObject.h` | `ibValueMetaObject` — the event set + `RunSubtree` / `CloseSubtree` + `ContributeTables` |
| `backend/metaCollection/metaObjectSerialize.cpp` | `BuildDataNode` / `ApplyDataNode` / `SaveNode` / `LoadNode` |
| `backend/metadataDataProcessor.{h,cpp}`, `backend/metadataReport.{h,cpp}` | external DP / Report containers |
| `backend/serialize/dataBuilder.{h,cpp}` | `ibDataNode`, `ibBinaryProvider` |
| `backend/query/schemaSnapshot.h`, `query/dataMover.h` | DDL snapshot + row-data mover |

---

## 10. Honest remainder

- **`OnSaveMetaObject` side effects break full atomicity (low severity).** The debugger
  `SaveModule` (non-transactional) and `ibByteCodeCache::Invalidate` run **per node, before** the
  commit. On a failed save they are already done — a cosmetic desync (a wasted recompile, a
  breakpoint offset off until the next save), **not** corruption. External `SaveToFile` is worse
  (no transaction at all). Fix direction: hoist the non-transactional part past the commit.
- **The version word is written but discarded on read** (`version_oes_last` → `(void)r_u32()`).
  No migration path yet; its real consumer is copy/paste of metaobjects across configs of
  different versions (see [metadata-serialization-arc.md](metadata-serialization-arc.md) Step 1).
- **MySQL DDL is non-transactional** — the one place delete atomicity is not free; preflight
  "everything droppable" is the only guard there.
- **The DB-load path is not atomic-on-failure** (it pre-clears), unlike the file-load path — but
  a DB-load failure is a corrupt-config / startup case, not a live edit.
