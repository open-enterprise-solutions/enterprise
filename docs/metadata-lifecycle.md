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

**Row data is separate.** Table *contents* do not ride the config blob — they move through the
L3-3 `ibDataMover` (`DumpDataToBuffer` / `RestoreDataFromBuffer` on the storage container),
driven by the same `ContributeTables` snapshot that drives the DDL. Boundary: **File = config
metadata, Storage = live table data.**

---

## 3. The node event set

Every phase fires virtual hooks on `ibValueMetaObject` (`metaObject.h`):

```
OnCreateMetaObject(metaData, flags)   OnLoadMetaObject(metaData)
OnSaveMetaObject(flags)               OnDeleteMetaObject()
OnRenameMetaObject(name)              OnReloadMetaObject()          // designer
OnBeforeRunMetaObject(flags) / OnAfterRunMetaObject(flags)         // start (init) — "module manager started"
OnBeforeCloseMetaObject()    / OnAfterCloseMetaObject()            // teardown
```

Two orchestrators recurse the tree, firing the before/after phase on every node:
`RunSubtree(flags, before)` and `CloseSubtree(before)`.

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

`RunDatabase(flags)` → `RunSubtree(flags, before=true)` then `RunSubtree(flags, before=false)`:

```
RunDatabase
 ├─ RunSubtree(before=true)   → OnBeforeRunMetaObject on every node
 └─ RunSubtree(before=false)  → OnAfterRunMetaObject  on every node   ← ctors register, module manager starts
```

This is the **initialisation** phase: `OnAfterRunMetaObject` is where per-type constructors
register into the factory and module managers start. Skipping it (loading a config without
running the fresh tree) leaves types unregistered — the concrete bug it caused was a reference
attribute read as a string → a `DROP` on a non-existent column → FB-607. So load is always
followed by a run.

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
| Export → file (`.obk` / `.mcf`) | `ibWriterMemory` buffer | temp file + `std::filesystem::rename` | no (`saveToFileFlag` = export) |
| Delete (object removal) | — | DB TX (DDL rollback) → `DeleteSubtree` → `OnDeleteMetaObject` | — |

---

## 7. CLOSE

`CloseDatabase(flags)` → `CloseSubtree(before=true)` then `CloseSubtree(before=false)`:

```
CloseDatabase
 ├─ CloseSubtree(before=true)  → OnBeforeCloseMetaObject
 └─ CloseSubtree(before=false) → OnAfterCloseMetaObject   ← module manager exits
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
