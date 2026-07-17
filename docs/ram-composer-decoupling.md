# Composer / model split — decoupling RAM tables from the queryable (arc design)

Status: **STEPS 1–4 LANDED + RUNTIME-VERIFIED** (2026-07-01, backend + solution build GREEN Debug|x86, 0 errors;
Max confirmed the RAM live-node display / edit works at runtime — "works surprisingly well"). Step 5 (the literal `ibValueModelDb`/`ibValueModelRam` class split) is deliberately NOT done — the
polymorphic composer + a universal node that serves both a live RAM row and a DB copy already realise the
DB/RAM behaviour split, so the class split is pure reorganisation against the recent one-model collapse. This
doc is the source of truth for the arc.

> **Current state (2026-07-17):** this arc LANDED in full — the model IS split (`ibValueModelCursor` /
> `ibValueModelStorage`), RAM display GROUPING and dot-walk-over-reference RAM filters both work, and a DB
> Cursor can now ALSO serve the whole list from a RAM snapshot (the `DynamicRead=off` fallback — the RAM
> paging primitive `RunStoragePage` is shared by the native RAM model and the Cursor's snapshot). The clean,
> current map is **[table-model.md](table-model.md)** (§1, §4a, §5); the per-phase notes below are the design
> churn-log that produced it — later phases supersede earlier ones, so read the "Honest remainder" /
> "Deferred" lists below as HISTORY (RAM grouping + dot-walk RAM filters are no longer open).

## Why

The in-memory-source-unification arc routed EVERY table — including RAM tables (value table,
tabular section, record set) — through the L5 composer over an `ibBackendQueryable`. For a RAM table
that meant: wrap the live rows in `ibRamTableQueryable`, register it in the per-query `ibTempSourceScope`,
`ComputeRows`-copy the rows into an `ibQueryRamTable`, then "query" the copy. That is a query over data
already in memory — heavy, and it forced upsert / backing-index / `m_writeSource` machinery onto the node
just so the discardable copy could write back to the storage. The temp-registry is a patch over a leaky
abstraction: a RAM table is **not** a queryable source.

DB lists (catalog / document / register / dynamic list) ARE queryable (data is in the DB, filter/sort/join
happen server-side). For them the composer→SQL path is correct. RAM tables are different: data is already
materialised; filter/sort are trivial in-place ops; the composer's full power (joins, dot-walk over refs)
is rarely needed for DISPLAY. So: split the two, keep the composer as the unified SETTINGS engine.

## Phase 2 — the MODEL SPLIT BY DATA SOURCE (agreed 2026-07-01, IN PROGRESS)

Max refined the endgame: split the ONE `ibValueModel` into two kinds **by where the data comes from** (NOT by
display shape — a list/tree is just a fetch that returns children or not, on either kind):

```
ibValueModel  (ABSTRACT — the universal node + display + columns + list-settings facade + Get*Fetch;
               NO composer, NO node storage; `virtual GetModelComposer()=0`, `virtual RunComposerPage()=0`)
   ├─ ibValueModelDb   holds `ibDataComposer m_composer` (L5.1, SQL over a queryable).  NO node storage —
   │                   the DB reads DYNAMICALLY from the cursor; its fetch = SQL → keyset page → COPY nodes
   │                   (transient, not stored). DB work = LISTS: group / filter / open by reference or KEY.
   └─ ibValueModelRam  holds `ibRamComposer m_composer` (L5.2, in place) + `ibRamValueStorage m_storage`.
```

- **GetModelComposer() is COVARIANT**: `ibDataComposer&` on Db, `ibRamComposer&` on Ram (via the base it is
  `ibDataComposerBase&`). A DB caller gets FromSource/FromText with NO cast (kills the dynamic-list casts).
- **`ibRamValueStorage`** — the RAM analog of a queryable (does NOT inherit `ibBackendQueryable`; a sibling under
  a future common minimal base). It OWNS the live nodes (`std::vector<ibValueTableRow*>` — flat OR tree) + the
  column collection. Mutations `AddValue / InsertValue / RemoveValue / Clear` take the MODEL as an argument (for
  the view notify) — the storage does not hold the model. `ibValueModelRam` owns it and binds the composer to it
  in its ctor: `m_composer.FromStorage(&m_storage)`. The composer NEVER references the model — only the DATA.
- **`ibRamComposer` sources from the storage** (`FromStorage`), reads its nodes DIRECTLY, and materialises the
  view: iterate → deliver the requested fields → filter / **group** / sort, ALL IN MEMORY (element counts are
  modest — value-tables 10–100, big lists 60–200k max). No query text, no lowering, no ComputeRows copy. `RunComposerPage`
  is split: `ibValueModelDb::RunComposerPage` = the SQL/copy-node path; `ibValueModelRam::RunComposerPage` =
  `ComputeOrder` over the storage → window by anchor → return the LIVE nodes.
- **`BindComposerSource` is GONE** — the queryable-based "decide RAM-or-DB" was a crutch; now the KIND decides
  (Db binds a queryable via FromSource, Ram binds the storage via FromStorage in its own ctor).
- **Aliases reparent for free**: `ibValueModelRamTableBase = ibValueModelRam`; `ibValueModelTableBase /
  ibValueModelTreeBase = ibValueModelDb` — every `: public ibValueModel*Base` lands on the right kind.

### Surgery status (build DEFERRED to the very end, per Max — long rebuild, key nodes change)
1. ✅ Base: duplicate m_nodeValues-based GetRowCount/GetRow/GetItem removed (DB-default virtuals in); the RAM ops
   moved OUT of the base.
2. ✅ `ibRamValueStorage` created (owns the nodes + column-collection ptr + mutation/read API; friend of the node).
3. ✅ `ibValueModelRam` complete: owns `ibRamValueStorage m_storage` + `ibRamComposer m_composer`; ctor
   `FromStorage(&m_storage)`; RAM ops + GetRowCount/GetRow/GetItem + StorageIndexOf/RowOf delegate to the storage;
   GetSourceQueryable (ramTableQueryable) for explicit querying only. `ibValueModelDb` holds `ibDataComposer`.
4. ✅ model.cpp: RunComposerPage SPLIT (ibValueModelDb / ibValueModelRam); base GetModelComposer /
   BindComposerSource / StorageIndexOf / StorageRowOf impls removed; `ibRamComposer::ComputeOrder + Run` read the
   storage; the model-vector m_nodeValues fully gone (the remaining m_nodeValues are the NODE's cell map).
5. ✅ Subclass access: DB ctors bind `m_composer.FromSource(q)` DIRECTLY (m_composer is the subclass's own
   protected member, already ibDataComposer — GetModelComposer() is the accessor for EXTERNAL callers only, e.g.
   the settings facade / frontend); RAM ctors drop the manual bind (auto in ibValueModelRam); dynamicList drops
   the dynamic_casts (covariant). No dangling refs to the removed symbols.
6b. ✅ FLUENT RAM builder: node `AddRow()` / `Set(col,v)` return a node& (chain), model `AddRow()` for a top-level
   row. `GetParent()` navigates up. (`model.AddRow().Set(a,1).Set(b,2); row.AddRow()…` for a value-tree.)

### STILL OPEN (agreed, not yet done)
6. **RAM GROUPING** (ibRamComposer.ComputeOrder → a group tree; the node already carries children) + **DOT-WALK
   over references** (resolve `Ref.Field` per row for filter/sort — load the ref, read its field).
7. **Build** ONCE at the very end + fix whatever the compiler surfaces (the structural change is unverified —
   build deferred per Max).

## The design (closed)

```
ibDataComposerBase  (L5 — settings: filter/sort/group as DOT-WALK paths + virtual Run/Execute/Add/Notify)
   ├─ ibDataComposer (DB):  source = QUERYABLE  → render SQL (dot-walk → JOIN) → READ-ONLY portions
   │                                                                            (does NOT signal the GUI)
   └─ ibRamComposer  (RAM): source = its OWN ROWS (owns the storage, not a queryable)
                            → in-place filter+sort (dot-walk → resolve the ref per row) → portions
                            + Add (a new row goes THROUGH the composer)
                            + NOTIFY the GUI on mutation / settings-change (the role the model had before)

Model: ABSTRACT base — does NOT store a composer, only `virtual GetComposer() -> ibDataComposerBase&`.
       Base carries: the universal NODE, all DISPLAY (GetValueByRow / IsContainer / HasValue / Compare /
       GetParent), the Get*Fetch DISPATCH, columns / commands, and RELAYS the composer's notify to the control.
   ├─ ibValueModelDb  (owns ibDataComposer; queryable / keyset cursor / parent-ref hierarchy — DB machinery)
   └─ ibValueModelRam (owns ibRamComposer; almost everything lives in the composer — thin)

Node (ibValueTableRow, ONE universal class, tree-by-default; a flat list is a tree without children):
   RAM = the LIVE storage row inside ibRamComposer (writable — edit a cell writes straight through)
   DB  = a TRANSIENT per-page adapter over the raw fetch values (READ-ONLY; carries rowKey ONLY for
         selection-survival across re-fetch). The DB model stores NO rows at all.

Settings form: ONE dialog, talks to `ibDataComposerBase` polymorphically (DB/RAM-agnostic). Fields come
   from the MODEL'S COLUMNS (not the source), dot-walk offered via the source explorer.
```

### Key consequences

- **Write split.** RAM: edit a cell → the node IS the storage row → direct write; ADD a row → through
  `ibRamComposer.Add`. DB: the grid is READ-ONLY (edit via the object form). This DELETES `UpsertCell`,
  the node's `m_writeSource`, and the backing-index — the whole "node carries the write via the queryable".
- **Selection survival is a DB-only problem.** RAM nodes are stable storage rows → selection survives
  trivially (same pointer). DB nodes are transient per-page snapshots → survive by rowKey / `IsEqualTo`.
  (The view-mode-switch selection-restore pain we hit is legitimately DB-side and stays there.)
- **Hierarchy (parent-ref tree) is DB-only.** `GetHierarchyColumn` / `GetFolderColumn` / `page.m_hierarchyCol`
  / the drill / the `s_constIgnoreParent` flat-vs-tree sentinel all live in `ibValueModelDb`. RAM is flat
  (only a user grouping could make it a tree — see deferral below).
- **`ibRamTableQueryable` + `ibTempSourceScope` stay — but ONLY for EXPLICit querying** (a value-table deliberately
  put into a query — the "put it into a temp table, then query it" case). They leave the DISPLAY path.
- **Composer becomes the real L5 data ENGINE**: source + settings + portions + (RAM) mutation/notify. The
  model is a thin showcase / notify-relay. This is the "composer fully replaces data management" endgame.

### Deferred

- **RAM display grouping** (group flat rows by a field → group-header + detail tree). Rare for a value-table;
  its own collapse/aggregate method is a separate DATA method, not display grouping. Slice-1 of
  `ibRamComposer` is **filter + sort only (flat)**. Grouping is additive later (the node is already
  tree-capable). Heavy dot-walk over RAM is the "explicit querying" case, not the common display path.

## Implementation order (behaviour AFTER structure; each step its own build+test, no accumulation of mines)

1. ✅ **`ibDataComposerBase` extracted** (composition/dataComposer.h/.cpp): the settings vocabulary
   (Select/Filter/Sort/Total/TotalBy/Parameter/ClearSettings + facade + scope + SetDriver) + `virtual bool
   Run(driver)`. `ibDataComposer` is now the DB descendant (`FromSource(ns,name)`/`FromText`/`RenderText`/
   `Execute`/the SQL `Run`). Additive — DB behaviour unchanged.
2. ✅ **`ibRamComposer` built** (composition/ramComposer.h; `Run`/`ComputeOrder`/`OrderRows` in model.cpp):
   `ComputeOrder()` = filter + stable multi-key sort over the queryable's `ComputeRows` snapshot → storage
   indices; `Run(driver)` = the generic value-copy walk (a validation row-sink). No text / parse / lowering.
3. ✅ **RAM routed through it + node = LIVE storage row**: the model holds a polymorphic
   `unique_ptr<ibDataComposerBase>`; `BindComposerSource(q)` picks `ibRamComposer` when `q` is an
   `ibRamTableQueryable` (decided off the EXPLICIT queryable, not the virtual GetSourceQueryable — avoids the
   base-ctor trap), else `ibDataComposer`. `RunComposerPage` SHORT-CIRCUITS a RAM model: `ComputeOrder` →
   window by the browsed anchor's storage index → return the LIVE `m_nodeValues` rows (no copy, no driver). The
   list-settings facade resolves `GetModelComposer()` lazily through the model (the composer is created after
   full construction). ⚠ runtime-test value-table / tabular / record-set.
4. ✅ **backing-index / write-back "patch" DELETED**: `m_backingIndex`, `m_writeSource`, `UpsertCell` (node +
   ibRamTableQueryable + the base virtual), the hidden `kComposerBackingIdMetaID` column + constant — all gone.
   A RAM edit writes its live storage row directly (`SetValue` → notify); a DB grid is read-only (edit via the
   object form). The DETAIL copy ctor lost its `source` arg. ⚠ runtime-test list edit + selection survival.
5. ⛔ **Structural model split** `ibValueModelDb`/`ibValueModelRam` — NOT done, deliberately (see Status). The
   polymorphic composer + universal node subsume it; revisit only if a real need appears.

**Honest remainder** (deferred, all runtime-test-gated): RAM display GROUPING (slice-1 is flat); RAM keyset-vs-
index paging edge cases on LARGE RAM tables (the window is exact for the whole in-memory list, but untested at
scale); the tabular-section line-number under a SORTED RAM view (StorageIndexOf is the storage position, not the
display position); dot-walk-over-reference RAM filters (skipped, so they currently do not filter). Everything
here builds GREEN; the ⚠ items need a runtime pass on the live catalog / value-table.

---

## Phase 2 — LANDED (structural split + full RAM decoupling)

Step 5 above was reversed by decision: the structural split is now DONE, and the RAM path is fully self-
contained (no L5-1 / L4-1 / queryable tie). What changed:

- **Model split by DATA SOURCE** (not display shape). Abstract `ibValueModel` base (pure-virtual
  `GetModelComposer()` + `RunComposerPage()`); `ibValueModelDb` (holds `ibDataDBComposer`, NO node storage —
  dynamic cursor reads) / `ibValueModelRam` (holds `ibDataRamComposer` + `ibRamValueStorage`). Covariant
  `GetModelComposer()` returns the concrete composer to a typed caller. Historical base aliases redirect
  (`ibValueModelTableBase=ibValueModelDb`, `…RamTableBase=ibValueModelRam`, `…TreeBase=ibValueModelDb`) — every
  subclass reparents with ZERO per-subclass edits. `BindComposerSource` (the queryable-picks-kind crutch) is gone;
  the KIND decides.

- **Final composer names**: base `ibDataComposer`, DB `ibDataDBComposer`, RAM `ibDataRamComposer`. The base `Run`
  is a non-pure no-op default; RAM does NOT override it (L5-2 is display = ComputeOrder + live nodes, no driver walk).

- **`ibRamValueStorage` = the RAM analog of a queryable, and it IS a ROOT node** (Max: "the list lives inside the
  root"). It holds a single `ibComposerNode m_root`; the rows ARE `m_root.m_children`. A flat list = a root of leaf
  children; a value-TREE = children with their own children. No parallel node vector — the node's own
  `m_children`/`Append`/`Remove`/`GetChild` ARE the storage. The root is synthetic (never displayed, never a top-
  level row's parent-item); its dtor cascades `DecRef` into the whole tree. Mutation ops (`AddValue`/`InsertValue`/
  `RemoveValue`/`Clear`/`ClearRange`) take the model for the view notify.

- **`ibRamTableQueryable` DELETED** (query/ramTableQueryable.h removed) + `GetSourceQueryable` RAM override + the
  `m_sourceQueryable` member. A RAM model inherits the base null `GetSourceQueryable` → `HasKeyedRows` false →
  restore-by-index. The RAM composer sources the storage via `FromStorage(&m_storage)` in `ibValueModelRam`'s ctor.

- **Node renamed** `ibValueTableRow` → **`ibComposerNode`** (canonical composer name; it is the universal fetch/
  display row for DB copies, RAM live rows, group nodes, tree nodes). Nested `ibValueModel::ibComposerNode` + a
  top-level `using ibComposerNode = …` alias. 137 refs across 20 files. Fluent RAM builder on it:
  `node.AddRow().Set(col,v).Set(col2,v2)…`, navigate via `GetParent()`.

- **RAM dot-walk over references** (`RamResolveField`): a field path splits into a HEAD storage column + a dotted
  TAIL; each tail segment hops into the previous value AS A SOURCE (a reference cell IS an `ibSourceDataObject` →
  `GetSourceExplorer` name→id → `GetValueByMetaID`, the same hop `ContinueHops` runs). Drives filter + sort +
  group dims. A primitive mid-path / unknown segment → empty value (filter passes, sort key empty — never a crash).

- **RAM GROUPING** in `ibValueModelRam::RunComposerPage`: the browsed parent node carries a group PATH; scope the
  ordered rows to it; at a group LEVEL emit one synthetic `ibComposerNode` group node per distinct `dims[depth]`
  value (container, drillable); at the DETAIL level (drilled through every dim) return the LIVE scoped rows. Same
  node semantics as the DB path (GetGroupPath / container), so the paged control treats both identically. `slice`:
  Elements grouping; a dot-tail dim groups by the walked value (stamped under the head column).

- **File split**: `model.cpp` → shared base + node (`model.cpp`), DB fetch (`modelDb.cpp`:
  `ibValueModelDb::RunComposerPage` + `ResolveAnchorByKey`), RAM fetch + composer (`modelRam.cpp`:
  `RamResolveField`/`RamSplitField`/`RamWindowPositions` + `ibRamValueStorage::ColumnIdByName` +
  `ibDataRamComposer::ComputeOrder` + `ibValueModelRam::RunComposerPage`). Both new units added to backend.vcxproj.

**Open**: the header `model.h` name is now misleading (it holds `ibValueModel`/`ibComposerNode`/
`ibRamValueStorage`, nothing "table"-specific) — a header rename is a ~30-file include churn, DEFERRED pending a
call. RAM keyset-vs-index at scale, tabular line-number under a sorted RAM view, and dot-walk-in-a-composite-mid-
segment remain runtime-test-gated. Everything here is a single final build away.

### Phase 2 — FINAL (build green, 2026-07-01)

Built clean (Debug|x86, 0 errors) after two post-split fixes + a naming pass:
- **The model kinds were renamed** `ibValueModelDb → ibValueModelCursor` (DB — pages a query cursor) /
  `ibValueModelRam → ibValueModelStorage` (RAM — owns ibRamValueStorage). Base stays `ibValueModel`.
- **The 3 historical aliases (`ibValueModelTableBase` / `RamTableBase` / `TreeBase`) were DELETED** — every use
  now names the real class. `ibValueModelTableBase` had meant TWO things (the common base for generic references
  AND the parent DB-list classes derive); the split resolved it by making generic uses `ibValueModel` and the one
  DB-list derivation explicit `ibValueModelCursor`. No RAM behaviour can leak into DB lists: dyn-list + every list
  derive `ibValueModelCursor` → the DB `RunComposerPage`; the RAM path lives only on `ibValueModelStorage`.
- **`ibDataRamComposer::ComputeOrder` moved to `composition/ramComposer.cpp`** (beside its declaration). The shared
  dot-walk helpers became storage methods `ibRamValueStorage::SplitField` / `ResolveField`, reused by the composer's
  ComputeOrder and the model's grouping. Files now: `model.cpp` (base + node), `modelDb.cpp` (Cursor fetch),
  `modelRam.cpp` (Storage fetch + storage methods), `composition/ramComposer.cpp` (the RAM engine).

---

## Phase 3 — FLAT node (row-with-values) / composer-owns-hierarchy / model-interprets (design 2026-07-01)

The node KEEPS its values but LOSES its hierarchy; the COMPOSER owns the tree; the MODEL interprets. Converged
with Max across the design thread (this REVISES the initial "node = pure key, drop m_nodeValues" framing).

**Contract**
- The node (`ibComposerNode` behind an `ibDataViewItem`) IS the storage of ONE row — a grouping row or an
  ordinary row alike. It KEEPS `m_nodeValues` + `GetValue`/`SetValue` (+ maybe ONE technical flag, e.g. "is a
  group"). It exists in the FETCH moment: the backend hands out the portion, and the node lives while the frontend
  holds a reference (refcount); the frontend reads its value list to display. That value list is enough for output.
- The node has **NO HIERARCHY** — no `m_children` / `m_parent` / tree ops. It operates only with ROWS (flat).
- The **HIERARCHY is the COMPOSER's job** (how the groups / tree are laid out). Both composers produce the portion
  in the **SAME format** (RAM portion == DB portion, Max): one flat row set, tree-shaped when grouped (level +
  hasChildren), byte-for-byte alike. RAM sources `ibRamValueStorage`, DB sources SQL; identical portion out.
- The **MODEL decodes** the node: given an item it answers value / the guid-key to open a form / "is this a
  group/container?" — reading the node's values + the composer's structure. External code and the frontend NEVER
  reach into the node; they ask the model.

**Consequences**
- Strip the tree from `ibComposerNode` (`m_children`/`m_parent`/`Append`/`Insert`/`Remove`/`GetChild`/`GetParent`/
  `GetParentItem`). The node is a flat row-with-values.
- `ibRamValueStorage` stops being a "root node with children" (supersedes that Phase-2 shape) — the composer builds
  the display tree (the portion) at fetch, both kinds identically.
- Group/container is a composer-produced structure + a node flag the model reads — NOT node tree-state. So
  `GetRowByItem`/parent resolve via the composer's portion through the model, fixing grouped-detail editing at the
  root (no Walker patch).
- `m_nodeValues` STAYS (it is the node's row-value store); the fetch just doesn't lean on node hierarchy.

**Revises Phase 2**: (a) m_nodeValues is NOT removed; (b) the node is NOT tree-capable — the storage-as-root-node
design is superseded, the composer owns the hierarchy.

**Execution order** (one arc, buildable stages): (1) this note; (2) make the composer OWN the tree/portion (RAM
composer emits the same row-portion as DB, tree via level/hasChildren); (3) strip hierarchy from the node → flat
row; (4) `ibRamValueStorage` becomes a flat row collection, not a node-tree; (5) model decodes container/parent
from the portion → grouped-detail editing falls out.

### Phase 3 — execution progress (2026-07-01, build DEFERRED to the final)

- **Stage 1 (node loses display-parent) — DONE.** `ibComposerNode` lost `m_parent` / `GetParentItem` / `GetParent`
  / `SetParent`. `IsContainer()` is now the COMPOSER's flag (`m_container`), NOT "has storage children". `m_children`
  STAYS — it is the storage value-tree (downward links; the fluent `AddRow` no longer sets a parent). The model's
  `GetParent(item)` returns invalid (the composer's slice defines parentage). Neutral for current cases (a display
  node had no children and `m_parent` was already null for a tabular row) — no regression. No dangling callers of
  the removed API (verified by grep).
- **Stage 3 (frontend locates via the materialised slice, not node parent) — DONE.** A composer node has no
  display-parent, so the frontend's `GetRowByItem` and `FindNode` (which built a parent-chain via
  `item.GetParentItem()`) fail for a GROUP's detail row. FIX: keep the fast parent-chain walk, and on a MISS fall
  back to an INSTANCE-IDENTITY scan of the realised tree (the composer's slice) — `RowByIdentityJob` (pre-order row
  count) for `GetRowByItem`, `FindTreeNodeByIdentity` (recursive) for `FindNode`. So a grouped detail is located →
  its cell has a real host window → the inline editor opens. Flat / top-level rows keep the fast path (no fallback).
- **Remaining (cleanup, NOT needed for editing):** unify the fetch so the RAM composer EMITS the same driver-row
  portion as DB (level/hasChildren), collapsing the `RunComposerPage` shapes into one — the composer already lays
  out the grouping tree per-parent-fetch, so this is a consolidation, not a behaviour change.

### Phase 3 — CORRECTION: the node DOES carry a composer-set display-parent (2026-07-01)

Reversed the "node loses display-parent" step above (Max: "the row carries a reference to the parent, and the composer
is what determines it"). The clean shape:
- `ibComposerNode` carries `m_parent` — its DISPLAY-parent — set by the COMPOSER per slice via `SetDisplayParent`,
  a MANUALLY refcounted strong ref (IncRef on set / DecRef on re-set + in the dtor). It does NOT dangle (a child
  keeps its parent alive) and does NOT leak past the slice (re-set to null on a flat re-fetch); no cycle — a
  backend group holds no children (the slice tree lives on the control). `GetParentItem` surfaces it.
- The COMPOSER writes the hierarchy: `ibValueModel::SetSliceParents(parent, out)` runs at the tail of BOTH
  `RunComposerPage` kinds — every fetched node's display-parent = the node it was fetched a level UNDER (null for a
  top-level / flat fetch, which also releases a stale grouped parent). So the node-parent is the composer's grouping
  SETTINGS materialised into the current slice — the rule lives in the settings, its result on the node, one owner.
- `ibValueModel::GetParent(item)` = `item.GetParentItem()` (surfaces the node's parent).
- The FRONTEND is UNCHANGED — the original `GetRowByItem` / `FindNode` parent-chain walk works again because
  `GetParentItem` returns the display-parent. The temporary identity-walk fallback (RowByIdentityJob /
  FindTreeNodeByIdentity) was REMOVED — redundant once the node carries the parent. The whole fix lives in the
  backend node; grouped-detail editing works because the frontend can walk detail→group→root.

---

## Phase 3 — FINAL STATE (2026-07-01, supersedes the Phase-3 progress notes above)

The earlier Phase-3 entries recorded a churn (node-loses-display-parent → identity-walk fallback → SetSliceParents)
that was REVERSED in the design thread. **This is the source of truth.** Compiles clean (Debug|x86, 0 errors — not
a runtime pass yet; the review is pending).

**The concept (Max, verbatim intent):** you set a parent ON the node, and that's all. The composer reconstructs the
SLICE every fetch, so the visible element count changes on the fly — a parent's children count is DYNAMIC (rows
scrolling out are released, new ones hung). The DB/RAM difference is PURELY refcount: a RAM row is owned by its
storage (+1), so it SURVIVES scroll-out (the frontend drops its ref, storage keeps it); a DB copy is held ONLY by
the frontend, so it DIES at 0 on scroll-out. No "current row" tracking — refcount decides who lives.

**The mechanism:**
- `ibComposerNode::m_parent` — the DISPLAY-parent, a MANUALLY-refcounted strong ref (SetDisplayParent: IncRef on
  set / DecRef on re-set + dtor). The child holds the parent → it never dangles; released past the slice → no leak;
  no cycle (a backend group holds no children — the display tree lives on the control). `GetParentItem` surfaces it.
- `ibValueModel::SetItemParent(item, parent)` — **VIRTUAL**, the ONE parent-setter. Takes the current node + the
  parent to set, nothing more. DEFAULT: cast BOTH sides to our one node type (ibComposerNode); if the item IS a
  node, assign (SetDisplayParent). A model may OVERRIDE it for kind-specific handling. `RunComposerPage` calls it
  per fetched node at its tail (RAM + DB): each node's parent = the one it was fetched a level under (null for flat).
- The FRONTEND is UNCHANGED — its native `GetRowByItem` / `FindNode` parent-chain works because `GetParentItem`
  returns the display-parent. No frontend fallback, no identity walk. The whole fix is the backend node's parent.

**The global pattern this instances:** every model virtual takes the generic `item` and CASTS it to the model's own
node type, interpreting it per-kind — `GetValueByMetaID(item,…)`, `IsContainer(item)`, `GetParent(item)`,
`SetItemParent(item,parent)`. An item can be handled WITHOUT a fetch; each derived model overrides its handling.

**Future bonus (NOT implemented — design only, DB-only):** a virtual `GetItemKey(item) → ibUniqueKey` the DB models
override to generate the right key from an item WITHOUT fetching — a REGISTER returns its dimensions (composite
key), a CATALOG returns the reference guid — fed into the form on open (the action events). RAM has no such key
(its rows are live/local). Seeds already exist ad-hoc: `RegisterSelectionKey`, `RowGuidOfItem` + the node row-key.

**Grouping arc (functional, this session):** Features::Grouping capability on RAM models; the Sort/Group field
picker sources the model's columns (was empty via the dead RAM queryable); List view = flat (ignores grouping),
Tree/Hierarchical = grouped; group header renders its dimension value (HasContainerColumns for a group node);
a CONTAINER (group) is not inline-editable (EditableLine), a DETAIL is; editing a detail INSIDE a group works via
the node display-parent → the frontend parent-chain locates it. NOT runtime-verified — pending the careful review.
