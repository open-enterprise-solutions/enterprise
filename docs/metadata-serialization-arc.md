# Metadata serialization arc — self-serializing node + atomic load/save

Status: **partially implemented in the working copy — NOT yet built/verified.**

Landed (working copy, unbuilt):
- Node-owned walk on `ibValueMetaObject`: `SaveSubtree` / `LoadSubtree` / `DeleteSubtree`
  + lifecycle `RunSubtree` / `CloseSubtree`. The former `SaveMeta`/`LoadMeta` stay
  (reused for predefined-attribute inline serialization); the former
  `SaveMetaObject`/`LoadMetaObject`/`DeleteMetaObject` were folded into the `*Subtree`
  methods and removed.
- All three use the node's own `m_metaData` (no owner threading): the root is stamped by
  the container via `SetMetaData`, each child here at creation. `LoadSubtree` takes only
  the reader.
- Containers (config / DataProcessor / Report) reduced to thin forwarders: `SaveCommonTree`
  / `LoadCommonTree` / `DeleteCommonTree` (renamed from `*CommonMetadata`) → root `*Subtree`;
  `RunChildMetadata` / `CloseChildMetadata` → `Run`/`CloseSubtree`.
- These node methods live in their own TU `backend/metaCollection/metaObjectSerialize.cpp`.
- `Clear` (`ClearChildMetadata`) was **not** unified — config calls `OnDeleteMetaObject` on
  all children, DP/Report only on non-deleted ones, and the root-release differs
  (`wxDELETE` in config vs `DecrRef` in DP/Report). See "Clear / destruction model" below.
- **Config-metadata seam relocated to `ibMetaDataConfigurationFile`.** `SaveConfigToBuffer`
  + `SaveCommonTree` moved down from `ibMetaDataConfigurationStorage` to the File base (they
  only touch `m_commonObject`), mirroring the already-File-level `LoadConfigFromBuffer` /
  `LoadCommonTree`. `DeleteCommonTree` stays on Storage (apply-only). A standalone
  `ibMetaDataConfigurationFile` (compare doc / file inspection) now serializes its own tree
  instead of hitting the base no-op stub. Both the file path (`SaveConfigToFile` /
  `LoadConfigFromFile`) and the binary path (`appData::SaveDatabase` / `LoadDatabase`) funnel
  through this one File-level config-buffer seam.
- **Table-DATA round-trip renamed Dump/Restore** (disambiguates table data vs metadata
  serialization): node-level `SaveTableData`→`DumpTable`, `LoadTableData`→`RestoreTable`
  (`ibValueMetaObject` + RecordDataMutableRef / RegisterData / Constant); container-level
  `SaveDataToBuffer`→`DumpDataToBuffer`, `LoadDataFromBuffer`→`RestoreDataFromBuffer` (Storage
  + `appData`). These stay on Storage (DB + sequence bound). Net boundary: **File = config
  metadata, Storage = live table data.**

### Interim runtime apply-flow fixes (will move under Step 5's detached-root model)
- **Config load now RUNs the freshly-loaded tree.** The run was moved from the
  (base-non-virtual, never-dispatched-through-`ibMetaDataConfigurationBase*`)
  `LoadConfigFromFile` override onto the shared virtual `LoadConfigFromBuffer`. Without it an
  imported `.mcf` was replaced but never run → its per-type ctors stayed unregistered →
  restructure's `metaData->GetTypeCtor(clsid)` missed reference/enum types → a reference attr
  was read as string → `ALTER TABLE … DROP fldNNNN_S` on a non-existent column → **FB -607**.
  The redundant explicit `RunDatabase()` after `LoadConfigFromBuffer` in `appData::LoadDatabase`
  was removed (the override now runs it; otherwise a double-run hits `wxASSERT(!m_configOpened)`).
- **`m_configNew` cleared in `OnAfterSaveDatabase` after a successful apply.** It is a one-time
  "create new database" flag; it was never reset, so every Apply re-ran the DROP + recreate
  `sys_const` path and a constant read mid-cycle hit its just-dropped column (**FB -206**
  "column unknown FLDnnnn_TYPE"). Now the first Apply creates, the rest are incremental.

Remaining: the atomic detached-root swap (#10), and the Clear / destruction-model change.
**Not done and blocking verification: nothing in this working copy has been built —
Step 4 touches `ibValueMetaObject` (repo-wide blast radius), so until a clean CMake
`oes_tests` build runs, everything below is "written, unverified".** The two interim fixes
above sit on `OnAfterSaveDatabase` / `LoadConfigFromBuffer`, which Step 5 reworks — treat
them as transitional.

**Step 0 round-trip gtest — written (unbuilt):** `tests/test_metadataSerialize.cpp`
(registered in `tests/CMakeLists.txt` under `oes_tests`). DB-free by construction — it
drives a standalone `ibMetaDataConfigurationFile` (public ctor) and serializes with
`saveToFileFlag` so `SaveSubtree` skips the only DB-touching hook (`OnSaveMetaObject`); a
fresh File is never "run" so load skips `CloseDatabase`. Three cases: default-config
save→load→save bytes-equal (the mirror-drift guard); double-reload idempotence (load
consumes exactly what save wrote); file path (`SaveConfigToFile`/`LoadConfigFromFile`)
matches the in-memory buffer seam. Run via the CMake test target, not the MSBuild solution:
`cmake -B build -DBUILD_TESTING=ON && cmake --build build --target oes_tests`. Coverage is
currently the default tree (Configuration + Language) — extend with Catalog/Document/Constant
fixtures once it builds green (those need factory-created children; verify their
create/load hooks stay DB-free or wire a MockDatabaseLayer).

**Step 1 — reconsidered, NOT landed.** A first attempt threaded the per-node version word
into shared container state (`ibMetaData::SetLoadVersion`) on every node — reverted: the
word every `SaveMeta` writes is identical (`version_oes_last`), so per-node overwrite of one
shared field is irrational plumbing.

The version's real job is **compatibility control: compare a node's stored version against
the target's (header) version**, and the concrete consumer is **copy / paste of metaobjects
across configurations of different versions** (`copyObjectFlag` / `pasteObjectFlag`). The
per-node word is NOT redundant there — it travels with a copied object; on paste, a version
mismatch against the target config must be detected and handled specifically (migrate the
pasted node). So the rational shape is:
- a single **target/header version read once** (blob-global), exposed on the load context;
- each node compares **its own** version word against that target during load/paste;
- a header version field is itself a format change → version-gated (folds into Step 5 or a
  dedicated copy/paste-versioning task), designed together with the paste flow, not as blind
  per-node plumbing.
Until then `LoadMeta` keeps discarding the word (`(void)r_u32()`) — the honest status quo.

**Step 5 — export-to-file atomic rename — landed (unbuilt).** The isolated, low-risk slice
of Step 5: the save-to-file methods build the full blob in a buffer, write a sibling
`<file>.tmp`, then `std::filesystem::rename` over the target as the single commit point — a
partial/failed write never replaces a good file. Applied to all three save-to-file analogs:
`ibMetaDataConfigurationBase::SaveConfigToFile`, `ibMetaDataDataProcessor::SaveToFile`,
`ibMetaDataReport::SaveToFile`. Uses `std::filesystem` (C++17), not wx — part of the
backend wxBase→std I/O migration.

**Step 5 — exception signal in the load walk — landed (unbuilt).** `LoadSubtree` no longer
swallows via `catch(...) { return false; }`; a factory miss / malformed-or-missing chunk now
throws `ibBackendException` (with object-name context), and a null data block is checked
instead of dereferenced. `LoadMeta` stays `bool` — it is reused by ~50 per-type `LoadData`
sites for inline sub-properties, so only `LoadSubtree` (3 `LoadCommonTree` callers +
recursion) switched to throwing; its bool checks on `LoadMeta` / `OnLoadMetaObject` convert
false→throw at that level. The three `LoadCommonTree` (config / DP / Report) catch
`ibBackendException` at the container boundary and report `false`, keeping the bool contract
for callers that don't wrap the call (e.g. `OnConfiguration`). Behavior-preserving for now
(error → false → caller clears); the error chain is what improves.

**Step 5 — detached-root atomic in-memory swap (config load) — landed (unbuilt).**
`ibMetaDataConfigurationFile::LoadCommonTree` now builds a fresh root (`BuildFreshRoot` —
mirrors the ctor's root setup minus the serialized Language), `LoadSubtree`s into it, and on
success swaps `m_commonObject` and `DecrRef`s the old root; on a throw the fresh root is
`DecrRef`-discarded and the live tree is untouched (all-or-nothing). `LoadConfigFromBuffer`
drops its pre-`ClearDatabase` (which used to wipe the tree before the load) so a failed load
no longer loses data; it keeps the pre-`CloseDatabase`. The **close-before-swap / baseline**
subtlety is handled by keeping that pre-`CloseDatabase`: for the storage subclass it is
virtual and closes both the working tree and the saved baseline (`m_configMetadata`), so the
old root is already closed (ctors unregistered) before the swap's `DecrRef`, and the
post-load `RunDatabase` re-runs both — no dangling `m_factoryCtors`, no baseline re-run
assert. The DB-load path (`LoadDatabase`) still pre-clears, so it is not atomic-on-failure, but
a DB-load failure is a corrupt-config / startup case.

**Detached-root extended to external DP / Report + ibValuePtr ownership — landed (unbuilt).**
- **DP / Report start-from-file** (`ibMetaDataDataProcessor` / `ibMetaDataReport`,
  `IsExternalCreate` branch of `LoadFromFile`): same detached-root as config — `BuildFreshRoot`
  → `LoadCommonTree(fresh, …)` → swap on success. Beyond config, the external metadata owns its
  **module manager**, which caches the root via `m_objectValue`; the swap rebuilds it on the
  fresh root. The inner branch (read object + copy into a config) keeps load-into-existing — it
  is a child of the config tree, no swap. `LoadCommonTree` was parametrized to take the target
  root. (The manager is a *runtime* concern: `GetManagerModule` is used only at runtime via
  `CreateObjectExtValue`, not during `LoadSubtree`, so the fresh root loads manager-free.)
- **`m_commonObject` → `ibValuePtr`** in all three (config + DP + Report): the swap is now a
  plain assignment (`m_commonObject = fresh` releases the old root + adopts the fresh one), zero
  manual refcount; ctors drop their manual `IncrRef`, dtors their `DecrRef` / `wxDELETE`.
  `BuildFreshRoot` returns refcount 0 (the caller's `ibValuePtr` adopts). The inline accessors
  (`GetCommonMetaObject` / `GetDataProcessor` / `GetReport`) moved out-of-line where the header
  only forward-declared the root type.
- **`m_moduleManager` → `ibValuePtr`** in DP / Report; **`DestroyMainModule` moved into the
  manager's destructor** (RAII — idempotent via `m_initialized`, which init sets and
  DestroyMainModule clears). Releasing the manager (`m_moduleManager = nullptr`, or the member
  dtor) now always tears the runtime down; an explicit `DestroyMainModule` stays valid for an
  intentional mid-process rebuild. Order is UAF-safe: the manager is released **before** the
  root (it references the root via `m_objectValue`).
- **Consistency:** this aligns the external-file metadata with the **session** manager, which
  was already `ibValuePtr<ibValueModuleManagerConfiguration> m_root`. The config metadata has
  **no** manager member — the configuration runtime lives per-session (`ibSession::m_root` +
  `m_lambdaRuntime`), so config-detached-root just swaps the tree. `ibValuePtr` itself got a
  self-assign guard fix this pass (`this != ptr` → `m_pRef != ptr`, removing a `p = p.get()`
  use-after-free). Open: optional RAII `~ibValueModuleManagerConfiguration` for full symmetry.

**Save side — preflight then generate (no detached swap).** The save does NOT need the load's
detached-root machinery. It is the simple two-phase shape: (1) **preflight** — check "can we
save?" (no blocking obstacle — locks / permissions / target writable); (2) **execute** —
generate the binary via the node walk (`SaveMeta` / `SaveData` → `SaveSubtree` /
`SaveCommonTree`) into a buffer, then a single write. Save-to-blob is **atomic by construction**
— the full buffer is built first, then committed in one write (DB UPSERT inside the apply
transaction, or temp + rename for file export — both already landed). No in-memory swap, no
off-thread candidate tree: the heavier threaded / staged variant was considered and
deliberately **not** taken — it is unneeded while the buffer-then-single-write model holds.

**Caveat — `OnSaveMetaObject` side effects break full atomicity (not yet fixed).** "Atomic by
construction" covers only the *buffer*: the serialization walk is **not** side-effect-free.
`ibValueMetaObjectModuleBase::OnSaveMetaObject` (fired per module node when `saveConfigFlag &&
DesignerMode`, i.e. on a commit, not a `saveToFileFlag` export) does two things **during** the
walk, **before** the single write / commit:
- `debugClient->SaveModule(GetDocPath(), <line count>)` — updates the debugger's module offset;
- `ibByteCodeCache::Invalidate(GetGuid())` — drops the module's stale AOT-cache row.

Consequences:
- They run **per node, before** the commit — outside the "build buffer → one write" window.
- `ibByteCodeCache::Invalidate` is a `db_query` op → on a config DB-apply rollback it rolls back
  with the transaction. But `debugClient->SaveModule` is a **non-transactional** debugger call —
  a mid-apply failure leaves the `SaveModule` calls already made un-rolled-back (partial
  debugger state).
- **External DP / Report `SaveToFile` is worse:** no transaction at all. `SaveCommonTree` fires
  OnSave (debugger + AOT) and *then* writes the file (temp + rename); a failed write leaves the
  side effects done but the file unsaved → debugger / AOT say "saved", file doesn't.

**Severity: low — cosmetic desync, not corruption.** Both side effects self-correct, and there
are only two save paths (file, DB-apply), both controlled:
- `ibByteCodeCache::Invalidate` fired on a failed save → the next run just recompiles via the
  cache-miss path (against the *old* source, since the save didn't persist) — a wasted compile,
  not bad data.
- `debugClient->SaveModule` set an offset for a save that didn't land → breakpoints map slightly
  off until the next successful save; designer-only, transient.

So this is **not urgent**. Fix direction if ever wanted: move the OnSave side effects to **after**
a successful commit — collect `(descriptor, line-count)` during the walk and apply `SaveModule` /
`Invalidate` once the write / transaction succeeded; at minimum hoist the **non-transactional**
debugger part past the commit point (the save-side analogue of the load's "swap only after
success").

**Lifecycle events — verified across load + save (no regression from the detached-root work).**
- **Load.** `OnCreateMetaObject` fires only on the **root** (a "main" metaobject) and on a
  manual add in the designer — it gates the creation of predefined sub-structure. Objects
  **loaded from the blob** get `OnLoadMetaObject` only (the factory `Init` does NOT call
  OnCreate; guid / id come from `LoadMeta`). So `BuildFreshRoot` correctly calls OnCreate on the
  fresh root, and `LoadSubtree` fires OnLoad per node. The root therefore sees a double
  `OnLoadMetaObject` (BuildFreshRoot's setup + LoadSubtree's blob load) — but that exactly
  matches the pre-detached-root flow (ctor setup OnLoad + load OnLoad), so it is not a
  regression. The swapped-out old tree gets close events (via the pre-`CloseDatabase`) but no
  `OnDeleteMetaObject` (replace semantics, as before).
- **Save.** `SaveSubtree` skips `OnSaveMetaObject` only when `saveToFileFlag` is set. The leaf
  `ibValueMetaObjectModuleBase::OnSaveMetaObject` performs DB / debug side effects (debugger
  `SaveModule` offset + `ibByteCodeCache::Invalidate`) gated on `saveConfigFlag && DesignerMode`.
  The config-vs-external flag difference is **intentional, not a bug** — it tracks where each
  kind commits:
  - **config** — real commit is the DB apply (`OnSaveDatabase`, `saveConfigFlag` → OnSave
    fires); its file/`.obk` write (`SaveConfigToBuffer`, `saveToFileFlag`) is an **export** →
    OnSave skipped, no debug/AOT churn.
  - **external DP / Report** — `SaveDatabase()` is a no-op (no DB; the file IS the storage), so
    `SaveToFile` (`saveConfigFlag`) **is** the commit → OnSave fires correctly (persist debug
    module info, drop stale AOT). Leaving `saveConfigFlag` here is right.

## Goal

Move per-node serialization **onto `ibValueMetaObject`** so each metaobject node
owns load/save of itself + its subtree. The `ibMetaData` container only orchestrates
("build a fresh root → load → optionally DDL → atomically swap the root"). DDL
generation already lives on the node (`CreateMetaTable`/`UpdateMetaTable` →
`CreateAndUpdateTableDB`), so this **co-locates** "how a node persists its structure
(DDL) and its data (blob)". Every operation holds the invariant **all-or-nothing** —
no half-loaded tree, no half-written output.

## Why

- **Save↔Load mirror drift.** Every metaobject hand-maintains two positional, untagged
  field lists (`SaveData`/`LoadData`). Reorder one line in `SaveData` without the mirror
  edit and every object of that type silently corrupts. No test enforces the pairing.
- **Walk duplicated across 3 containers.** The tree-walk is copy-pasted in
  `metadataConfiguration`, `metadataDataProcessor`, `metadataReport`. Moving it onto the
  node collapses that intra-metadata duplication — all three call the same node methods.
- **Dead version word.** `version_oes_last` is written into every meta block but discarded
  on read (`metaObject.cpp` `(void)r_u32()`). No migration possible.
- **Half-load / leak risk.** `LoadChildMetadata` has `catch(...) { return false; }` that
  leaks the already-`IncrRef`'d node and leaves a partially-built tree on failure.

## Key design decisions

### Two-phase everywhere: preflight (can we?) → execute (reliably)

Every operation (load, save/export, delete, config replacement) splits into two phases,
uniformly:

1. **Preflight** — side-effect-free. Answers "can we do this?" by collecting **all**
   obstacles. If any obstacle exists → abort, nothing changed (e.g. on a failed load the
   DB is simply "not loaded", never half-loaded).
2. **Execute** — runs only after preflight passes, designed to be as failure-free as
   possible. Residual catastrophic failures are caught by the stage-then-commit model below.

Why this is stronger than stage→commit alone: for **irreversible targets** (MySQL
non-transactional DDL, file writes) there is no rollback, so check-before-act is the
**primary** safety; staging / DB-TX is only the secondary net.

Per-operation mapping:
- **Load** — preflight: header / sign / version / integrity compatible? → execute: build
  the detached root (which may still throw → discard). On failure the DB stays "not loaded".
- **Save / export** — preflight: no obstacles (locks, permissions, target writable) →
  execute: buffer + single write / rename.
- **Delete** — preflight: deletion allowed (no constraint / usage / lock) → execute: DROP
  in a DB TX (on MySQL preflight is the *only* guard).
- **Config replacement (Apply)** — **ordered** preflight: (1) any restriction on *closing*
  the current config (active sessions / locks)? → (2) any restriction on *deletion* /
  restructure? → (3) … → then execute in order: close → restructure → load new → swap.

Existing seed: `ibRestructureInfo::RequireExclusiveForDDL` already does the "can we close /
restructure?" preflight — it acquires exclusive mode and throws if other sessions are
connected. The arc generalizes this into the universal preflight pattern.

### The root metaobject is the atomic swap unit

`m_commonObject` (`GetCommonMetaObject()`) owns the whole tree, so it is the unit of
atomic replacement. No separate "staging tree" abstraction is needed — a freshly-built
root **is** the staging.

```
1. fresh = new root metaobject (ibValuePtr, detached — does NOT touch m_commonObject)
2. fresh->LoadSubtree(reader)         // recurse down; throws ibBackendException on error
3. error → fresh is RAII-discarded, m_commonObject untouched          (all-or-nothing)
4. success → diff(fresh vs m_commonObject) → DDL (CreateAndUpdateTableDB on nodes)
5. DDL + config blob written inside ONE DB transaction → DB commit
6. ONLY after DB commit → swap: m_commonObject = fresh; old DecrRef   (in-memory commit)
```

`ibMetaData` shrinks to a thin holder: owns the root pointer, orchestrates
load(fresh)→ddl→commit→swap. All per-node mechanics (save/load/ddl) live on
`ibValueMetaObject`, recursion from the root.

### Error signal = exception; atomicity = stage-then-commit

These are two separate concerns:

- **Signal**: node `LoadSubtree` throws `ibBackendException` on a malformed/missing chunk
  or factory failure. Fits the codebase (throw-by-value, catch-by-const-ref) and removes
  the bool-threading + the `catch(...)` leak.
- **Atomicity**: an exception alone does NOT give it — if N children were already attached
  to the live tree and the N+1th throws, the live tree is half-mutated. The detached-root
  model gives strong-exception-guarantee: nodes build into a buffer / a detached tree with
  no external side effects, and the container commits once.

### Two-layer commit ordering

The in-memory root swap happens **after** the DB transaction (DDL + blob) commits — never
before. Otherwise a DB rollback leaves the live in-memory metadata inconsistent with what's
actually in the database. On any failure (load / DDL / DB commit) the fresh root is dropped
and both the live tree and the DB stay consistent.

### Swap invalidation

After the root swap, anything holding **raw `ibValueMetaObject*`** into the old tree must
re-resolve. Runtime is already decoupled (const-meta + runtime-facade: holds bytecode /
descriptors, not metaobject pointers), so sessions/execution are unaffected. The consumer to
refresh on reload is the **designer tree / UI** (which already rebuilds on reload). Startup
load with no live tree just assigns the fresh root.

### Atomicity is uniform; only the commit mechanism differs per target

`SaveSubtree`/`LoadSubtree` on the node produce **no external side effects** — they only fill
an in-memory buffer (save) or build a detached tree (load). The irreversible commit is always
the container's, as a single point:

| Operation | Staging | Single commit point |
|---|---|---|
| Load | fresh root in-memory | swap `m_commonObject` |
| Save → blob | `ibWriterMemory` buffer | one prepared write in DB TX |
| Export → file | `ibWriterMemory` buffer | temp file + atomic rename |
| Delete | — | DB TX commit (DDL rollback) |

Save-to-blob is therefore **already atomic by construction** (build full buffer → single
write in a DB transaction); `SaveSubtree` just fills the buffer. Export-to-file needs
temp-file + rename so a partial file never becomes live (the "we already exported part and
then it failed" case). Delete relies on **transactional DDL** (FB/PG roll back partial drops);
**MySQL caveat** — DDL is not transactional there, so a partial drop can't roll back → either
pre-validate "everything is droppable" before the first drop, or accept the limitation.
Streaming/incremental export to an irreversible target can't buffer fully → would need a
scratch target + promote, or a transaction on the receiver.

### Relation to the existing Apply/restructure flow

This generalizes what `OnSaveDatabase` already does: it loads the **old** config into a temp
`commonObject`, diffs it against the new by GUID, and runs `CreateMetaTable`/`UpdateMetaTable`/
`DeleteMetaTable` inside a DB transaction. The detached-root model makes the fresh tree the
canonical "candidate"; DDL reconciles the DB to it; the in-memory swap is the last commit step.

### Not a target: the DataProcessor/Report parallel files

`metadataDataProcessor` ≡ `metadataReport` (and the other DP/Report pairs) are intentionally
kept as separate near-identical copies — Report will be specialized later. Do NOT unify them
into a template. They each call the same `ibValueMetaObject` node methods, so the *walk*
duplication still collapses without merging the container classes.

## Clear / destruction model — mostly landed

The original plan here was flagged "highest-risk (double-free)". It is **largely already
done** because the owning-children model (`ibValuePtr`) is in place:

- `ibValueMetaObject : ibPropertyObjectHelper<ibValueMetaObject>`, `m_children` =
  `vector<ibValuePtr<…>>` — owning handles (IncrRef on `AddChild`, DecrRef on drop).
  `~ibPropertyObjectHelper` → `RemoveAllChildren()` drops the handles, so destroying the root
  **cascades the whole subtree**. `~ibValueMetaObject` frees only `m_methodHelper`.
- The **"double-free class" is structurally closed** — it is the ownership model, not a
  two-changes-must-land-together hazard. `RemoveAllChildren` clears the owning vector; a later
  dtor finds it empty. `ClearDatabase(keepPinned)` just drops the non-pinned handles
  (idempotent), so the explicit clear and the dtor cascade coexist safely.
- **`OnDeleteMetaObject` is the delete *event*, separate from memory teardown** — already the
  target shape. config `ClearDatabase` intentionally does NOT fire it (wholesale replace,
  reconciled by the DDL diff); the event for real object removal fires in the apply path
  (`DeleteSubtree → DeleteData`). It is not in the dtor and does not silently vanish.

**Done:** root release reconciled to `DecrRef`. config `~ibMetaDataConfigurationFile` used
`wxDELETE(m_commonObject)` (bypasses the refcount — wrong once a detached-root swap shares the
root); now `DecrRef`, matching DP/Report and the owning model. For an unshared root (refcount
1) it destroys it just the same and cascades the subtree.

**Remaining (only under the detached-root swap, Step 5):** "reset before load" disappears —
load builds a fresh root and swaps, the old root's `DecrRef` cascades it, no explicit pre-load
`ClearDatabase`. Until that lands the explicit `ClearDatabase` before load stays and is
harmless.

## Steps

Each step builds and tests; format is byte-preserved until Step 5.

**Step 0 — Round-trip regression test (the linchpin).** gtest over a known config blob:
load → save → bytes identical (or load→save→load→deep-equal). Guards the format on every
later step. Do not start the arc without it.

**Step 1 — Honor the version word (no format change).** Thread the read version through the
load context so a node can query `ar.version()`. Unblocks safe evolution.

**Step 2 — `ibArchive` dual-mode wrapper (no format change).** Thin type over
`ibReaderMemory`/`ibWriterMemory`: `u32`, `stringZ`, `chunk(id, fn)`, `isLoading()`,
`version()`. Convert one leaf to a single `Serialize(ar)`; build + round-trip before scaling.

**Step 3 — Migrate leaf properties + simple metaobjects to `Serialize(ar)`.** Fold the
positional Save/Load mirror pairs into single bodies, one type per commit, round-trip each.
Kills the mirror-drift bug class. Format unchanged.

**Step 4 — Move the walk onto `ibValueMetaObject`.** Add `SaveSubtree(owner, chunkId, writer,
flags) const` (own data + recurse children, filtered by `FilterChild`) and `LoadSubtree` /
`LoadChildren` (children created via factory; root data-load stays at the container). The
three `ibMetaData` containers shrink to "build fresh root → delegate → swap". This is the
intra-metadata walk dedup. **Touches `ibValueMetaObject` (base used everywhere) → repo-wide
recompile; start from a clean build.**

**Step 5 — Atomic load + exception signal (format-touching → version-gated).** `LoadSubtree`
throws `ibBackendException`; container builds a detached fresh root, runs DDL diff, and swaps
only after DB commit (two-layer ordering). Removes the `catch(...)` leak. Add export-to-file
temp+rename. Round-trip + cross-version + failure-injection tests.

**Step 6 (optional, separate arc) — index-map for `Find*`.** `unordered_map<guid/name, node*>`
alongside `m_children` for O(1) `Find*`. Independent of serialization.

## Risks / notes

- Step 0's round-trip test is mandatory before anything else.
- Touches the config blob (`sys_config`), not the AOT bytecode cache. Confirm AOT keying
  unaffected.
- Step 4 changes `ibValueMetaObject` — highest blast radius; build the accumulated batch
  first so errors are attributable.
- MySQL non-transactional DDL is the one place delete atomicity is not free.
