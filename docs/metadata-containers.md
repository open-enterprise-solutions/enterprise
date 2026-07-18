# Metadata containers — the `ibMetaData` family, the mechanism and its varieties

> **Scope:** what a *metadata container* is in OES, the one shared mechanism every kind
> reuses, and the **varieties** (the `ibMetaData` class family) — configuration vs external
> report / data-processor — with the single axis of difference between them. This is the
> orientation map: read it first to know *which* container you are in, then the companions for
> the how.
>
> **Companions:** [metadata-lifecycle.md](metadata-lifecycle.md) (load / run / save / close +
> the per-node event order — the *mechanism in motion*), [metadata-tree.md](metadata-tree.md)
> (the Designer navigator over a container), [schema-first-metadata.md](schema-first-metadata.md)
> (node serialization names), [property-system.md](property-system.md) (a node *is* a
> property-object; composition = property composition), [runtime-facade.md](runtime-facade.md)
> (why the config runtime lives per-session, not on the container), [factories.md](factories.md)
> (the ctor registry the image owns), [../CLAUDE.md](../CLAUDE.md) §7 (the one-line container
> summary).
>
> **Status:** landed. This doc is a **MAP of code that already exists** — the metadata mechanism
> and the external-metadata containers are hand-written foundation, older than the query arc.
> Every claim below was verified against `metaData.h` / `metadataConfiguration.h` /
> `metadataDataProcessor.h` / `metadataReport.h` in the session that wrote this doc; where a
> claim is carried from a companion doc rather than re-verified, it is marked.

---

## 1. What a metadata container is

A **metadata container** owns one *open* body of metadata — a tree of metaobjects (Catalogs,
Documents, registers, modules, forms, …) plus the runtime scaffolding that "opening" fills and
"closing" discards. The abstraction is **`ibMetaData`** (`backend/metaData.h`) — an abstract base;
every concrete container is a subclass. There is no separate "metadata store" object: the
container *is* the store, the type-system, and the lifecycle driver in one.

The tree itself is **not** a separate model — a metaobject is an `ibValueMetaObject`, and its
children are its **property-object children** (composition **is** property composition, see
[property-system.md](property-system.md) §5). The container holds the *root* of that tree and
orchestrates it; it does not re-implement it.

---

## 2. The one shared mechanism (all varieties reuse it)

Everything in this section lives on the `ibMetaData` base, so **every** variety gets it for free.

### 2.1 Open state = the image

`ibMetaData` holds `std::shared_ptr<ibMetaImage> m_image`. **Its presence IS the open state** —
`IsConfigOpen()` is literally `m_image != nullptr` (`metaData.h:284`). There is no separate
`m_opened` bool anywhere in the family; a container that never opens just keeps the default null.

`ibMetaImage` (`metaData.h:156`) aggregates everything a run fills and a close drops:

| Slot | Holds | Notes |
|---|---|---|
| `m_factoryCtors` | `ibCtorRegistry<ibCtorMetaValueType>` — the metadata-defined **type-ctor factory** | owns its ctors via `shared_ptr`; dropping the image frees them |
| `m_moduleStorage` | common-module skeleton (init modules) | populated by descriptors' `OnBeforeRunMetaObject`; empty for kinds without init modules |
| `m_compileCache` | designer-only `ibCompileValueCache` | `nullptr` on runtime configs — callers gate with `if (auto* cc = GetCompileCache())`, not `DesignerMode()` |
| `m_sourceFactory` | this config's **own** L3/L4 queryable descriptors | resolve descends to the (currently empty, future-plugin) global factory on a miss |

### 2.2 `LoadGuard` — the load is a transaction

`LoadGuard` (`metaData.h:578`) is the RAII transaction: its ctor **creates** the image (asserts it
was closed — else "run without close"); its dtor **drops** it (rolling the load back) **unless
`Commit()` ran**. So a thrown `ibBackendException` (or any early return) unwinds through the dtor,
the image is dropped, and the state is exactly the closed state it started from — *exception ==
rollback, by construction*. Dropping the image frees the ctors, the compile cache, and the designer
module-manager (which lives in the cache) in one move.

### 2.3 What each container decides for itself

Two seams the base delegates so a kind describes its **own** setup in one place:

- `virtual CreateDesignerCache()` (`metaData.h:278`) — the image ctor calls it; base returns
  `nullptr` (runtime / non-designer kinds), designer kinds override to build the compile cache
  *with* its module-manager attached.
- `GetLangCode()`, `RunDatabase()`, `CloseDatabase()` — pure virtual; each variety supplies its own.

Shared services also on the base: metaobject CRUD (`CreateMetaObject` / `RenameMetaObject` /
`RemoveMetaObject`), the ctor facade (`RegisterCtor` / `FindCtor` / `CreateObjectRef` — mint a live
value from a clsid), `Serialize` / `Deserialize`, guid↔metaId identity resolve, the `IsFullAccess()`
RLS Tier-0 default (`true`), and `m_factoryCtorCountChanges` — a monotonic invalidation counter kept
**outside** the image on purpose (dropping the image must not reset it).

---

## 3. The varieties — the `ibMetaData` family

Two branches descend from `ibMetaData`. **One axis explains the split** (§4).

```
ibMetaData                                   (abstract — the mechanism, §2)
├── ibMetaDataConfigurationBase              access-rights + restructure ledger + config MD5/name/guid
│   └── ibMetaDataConfigurationFile          m_commonObject = ibValueMetaObjectConfiguration; RunDatabase/CloseDatabase
│       └── ibMetaDataConfiguration          DB load + RUN-on-load; owns the debugger server; the appData active metadata
│           └── ibMetaDataConfigurationStorage   designer edit config: inner "saved" baseline for compare
├── ibMetaDataDataProcessor                  external .epf — own m_commonObject + own m_moduleManager
└── ibMetaDataReport                         external .erf — same shape, Report types
```

### 3.1 Configuration branch (DB-backed, per-session runtime, restructures)

Four levels, each adding exactly what its role needs:

| Class | Adds | Why it exists |
|---|---|---|
| `ibMetaDataConfigurationBase` | `AccessRight_*` (Administration / DataAdministration / ExclusiveMode / …), the **restructure ledger** (`m_restructureInfo`, static `GetRestructureInfo()`), `GetConfigMD5/Name/Guid`, `LoadConfigFromFile/SaveConfigToFile`, `OnInitialize/OnDestroy` (appData lifecycle), config-type | only a *configuration* restructures; the ledger records real CREATE/ALTER/DROP + validation warnings |
| `ibMetaDataConfigurationFile` | `m_commonObject : ibValuePtr<ibValueMetaObjectConfiguration>`, `m_md5Hash`, `RunDatabase/CloseDatabase`, `LoadCommonTree/BuildFreshRoot`, access-rights **delegate to `m_commonObject`**, `IsFullAccess` | **public ctor** — instantiated directly by Designer document views to inspect a stand-alone `.obk` (per-document scratch, *not* the coordinator) |
| `ibMetaDataConfiguration` | `LoadConfigFromBuffer` that **additionally RUNs** the tree (register ctors before any DDL apply — else `GetTypeCtor` misses reference/enum types → bogus ALTER → FB-607), `LoadDatabase`, owns **`m_debugServer`** (one per process, the `debugServer` macro reads its slot), `m_metaGuid`; config-type = Load | the appData-owned **active runtime metadata**; private ctor gated on `ib::AppDataCtorToken` |
| `ibMetaDataConfigurationStorage` | composes an **inner `m_configMetadata`** (an `ibMetaDataConfiguration` = the *saved* baseline), `IsConfigSave() = CompareMetadata(baseline)`, RunDatabase/CloseDatabase **cascade to the inner baseline**, sequence data | the Designer's **active edit config** — edits are diffed against the saved baseline (see [configuration-compare.md](configuration-compare.md)) |

The config runtime does **not** live on the container — it lives **per session**
(`ibSession::m_root` + `m_lambdaRuntime`, see [runtime-facade.md](runtime-facade.md)). The container
commits to the DB; sessions run it.

### 3.2 External branch (file-backed, own runtime, no restructure)

`ibMetaDataDataProcessor` (`.epf`) and `ibMetaDataReport` (`.erf`) are **near-identical twins**
(`: ibMetaData`), kept separate on purpose (Report specialises later — they already share the node
methods; see §6). Each owns:

- `m_commonObject : ibValuePtr<ibValueMetaObject{DataProcessor|Report}>` — a **single** root object.
- `m_moduleManager : ibValueModuleRuntimeManagerExternal{…}` — its **own** runtime manager (owning
  handle; dtor runs `DestroyMainModule` — RAII). *The config has no manager member; this is the
  structural tell of "external".*
- `m_fullPath`, `m_version : ibVersionID`.

Surface: `LoadFromFile(path)` (detached-root swap, same atomicity as config load, then rebuild the
manager on the fresh root); `SaveToFile(path)` (temp + rename — **the file IS the commit**;
`SaveDatabase()` is a no-op); `CreateObjectRef(clsid|name)`; `StartMainModule/ExitMainModule`.
`LoadFromFile` also has a **load-into-config** branch (copy the object into an existing config tree,
no swap) — how an external object is imported.

The whole **runtime** entry is one script function — `Create(fullPath)` on
`ibValueManagerDataObjectExternalReport` — which loads the file, produces a detached root, and hands
the script that root's manager module (verified in [metadata-tree.md](metadata-tree.md) §5.2). From
there an external object is indistinguishable from a config object — same forms, same object module,
same spreadsheet; the metaobject only needed `IsExternalCreate()` to differ.

---

## 4. The one axis of difference

Everything above collapses to a single distinction — **where it commits, and where its runtime
lives:**

| | Configuration | External DP / Report |
|---|---|---|
| **Storage / commit** | the DB (`SaveDatabase` real; DDL diff in one TX) | its **file** (`SaveToFile` = temp+rename; `SaveDatabase` no-op) |
| **Runtime** | **per-session** (`ibSession::m_root`) — container has no manager | its **own** `m_moduleManager` member (RAII) |
| **Restructure (DDL)** | yes — the restructure ledger | never |
| **Specialization depth** | 4 levels (rights / baseline-compare / debugger) | 1 level (a single root object) |
| **Tree scope** | the whole configuration | one root object (a `.epf` / `.erf`) |

Everything *else* — image/open-state, ctor factory, node serialization, the two-phase run/close
lifecycle, the source factory, metaobject CRUD — is the **shared base** (§2). That single axis is
why the external containers reuse ~95% of the config engine.

---

## 5. Which container am I in? (quick guide for a future session)

- Runtime, thick client / web / daemon executing a config → **`ibMetaDataConfiguration`** (the
  active metadata; reach it via `activeMetaData`). Its runtime is on the **session**, not here.
- Designer editing a config → **`ibMetaDataConfigurationStorage`** (has the compile cache + the
  saved baseline; `IsConfigSave()` compares against it).
- Designer inspecting a stand-alone `.obk` file → **`ibMetaDataConfigurationFile`** (public ctor,
  load-only).
- An external `.epf` / `.erf` opened at runtime (`Create(path)`) or in the Designer →
  **`ibMetaDataDataProcessor`** / **`ibMetaDataReport`** (file-backed, own runtime).

The presence of `m_image` tells you it is **open**; `GetCompileCache() != nullptr` tells you it is a
**designer-side** container; a `GetManagerModule()` member tells you it is **external**.

---

## 6. Honest remainder

- **`GetFactoryCountChanges` precedence bug in BOTH external containers.**
  `metadataDataProcessor.h:54` and `metadataReport.h:54`:
  ```cpp
  return m_factoryCtorCountChanges +
      activeMetaData != nullptr ? activeMetaData->GetFactoryCountChanges() : 0;
  ```
  `+` binds tighter than `?:`, so this parses as
  `(m_factoryCtorCountChanges + activeMetaData) != nullptr ? … : 0` — the local counter is consumed
  in **pointer arithmetic inside the condition** and dropped from the result; the function returns
  only the parent's count. The **base** version (`metaData.h:379`,
  `m_factoryCtorCountChanges + ibValue::GetFactoryCountChanges()`) is correct — the bug is only in
  the two external overrides, and it was **duplicated** across the twins. **Fixed** (this session):
  parenthesized the ternary in both files —
  `m_factoryCtorCountChanges + (activeMetaData != nullptr ? … : 0)`. Low impact (a stale
  invalidation-count edge). *Verified in code; fix applied.*
- **External twin duplication.** `ibMetaDataDataProcessor` and `ibMetaDataReport` headers are almost
  line-for-line identical (they differ only in `DataProcessor`↔`Report` type names — and they share
  the bug above). The rationale ("Report specialises later; do not merge into a template") holds only
  if Report actually diverges; how much it has is **not yet checked** (the `.cpp` files were not read
  for this doc). Worth a divergence audit before either extending or de-duplicating.
- **`Create` success-path ownership is doc-only, not re-verified.** [metadata-tree.md](metadata-tree.md)
  §7 flags that `Create` `wxDELETE`s the container only on failure; on success the raw pointer is
  handed to `ibExternalOwnerHelper` implicitly, and the path there is not visible in the function.
  That note is **carried from the companion doc**, not re-verified in this session — open an external
  report twice and watch the container before trusting it.
- **The version word round-trips but is discarded on read** (see
  [metadata-lifecycle.md](metadata-lifecycle.md) §10) — no migration path yet; its real consumer is
  cross-version copy/paste of metaobjects.
