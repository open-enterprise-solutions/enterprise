# Configuration Compare + Merge

Side-by-side compare and partial merge of two OES configurations. Reachable from the designer's **Configuration** menu in three flavours: compare with a file, compare with the database baseline, or compare two arbitrary files. Mirrors the standard metadata tree shape (with Common umbrella, per-class groups, per-object property listing) so the diff reads the same way as the normal designer layout.

The feature lives in three places:
- a backend walker that produces a flat structured diff — `src/engine/backend/metaCollection/metaDiff.{h,cpp}` (`ibMetaDiffWalker`);
- the designer dataview model — `src/engine/designer/mainFrame/configCompare/configCompareModel.{h,cpp}` (`ibDataViewMetaDiffModel`);
- the designer doc/view that hosts it — `src/engine/designer/docManager/templates/docViewConfigCompare.{h,cpp}` (`ibConfigCompareDocument` / `ibConfigCompareView`). The compare surface is a doc/view tab, not a modal dialog.

Wiring lives in `src/engine/designer/mainFrame/mainFrameDesignerEvent.cpp` (the three "Compare with…" menu handlers).

---

## Entry points

Three items under **Configuration**, each producing the same compare view with different roots:

| Menu item | Left root | Right root | Push target |
|---|---|---|---|
| Compare with file... | `activeMetaData` | transient `ibMetaDataConfigurationFile` loaded from `.oap` | the same `.oap` (Push direction available) |
| Compare with database configuration | `activeMetaData` | `ibMetaDataConfigurationStorage::GetConfiguration()` (the in-memory DB baseline) | none (Push hidden — the DB-side mutation has no immediate persistence path) |
| Compare two files... | transient file A | transient file B | file B (left save callback is a V2 enhancement) |

All three skip any DB round-trip — both sides are already in memory by the time the view opens.

---

## Diff walker (`ibMetaDiffWalker`)

Walks two `ibValueMetaObject` subtrees in parallel and emits a flat `std::vector<ibMetaDiffRecord>` in pre-order. Pairing happens by `ibGuid` at each level; cross-level moves show as one `OnlyInLeft` + one `OnlyInRight` rather than a single "moved" record.

### Status enum (`ibMetaDiffStatus`)

| Status | Meaning |
|---|---|
| `Same` | Both sides present, `CompareObject()` returns true, child order matches |
| `Reordered` | Both sides present, identical composition (same set of GUIDs), but child order differs |
| `Changed` | Both sides present, `CompareObject()` returns false (some property differs) — or status was lifted up from a descendant |
| `OnlyInLeft` | Present in left only |
| `OnlyInRight` | Present in right only |

Status propagates upward: a Same group with any non-Same descendant gets lifted to `Changed`. This means the user can scan top-down to find diffs without expanding every branch.

### Record shape (`ibMetaDiffRecord`)

A single record is one of three logical kinds:

- **Object record** — both `m_left` and `m_right` are real `ibValueMetaObject*` pointers (or one is null for OnlyIn cases).
- **Group record** — `m_groupClsid != 0`; `m_left` / `m_right` are null. Synthetic; the walker inserts these between paired objects and their actual children to mirror the standard metadata-tree shape (Catalogs, Documents, Attributes, ...).
- **Property record** — `m_propertyName` non-empty; `m_left` / `m_right` are null. Synthetic; sits inside the "Properties" group of each paired object and surfaces per-property delta with stringified values.

Records carry `m_parentIndex` (into the same vector, -1 for the root) and `m_depth` so the dataview model can walk the tree without rebuilding pointer chains.

### Group shape (mirrors `treeConfiguration_impl.cpp`)

Under a paired metadata object, the walker emits:

1. A `Properties` group (`g_diffPropertiesGroupClsid`) — one child row per `ibProperty` on the object (skipping `ibPropertyModule` / `ibPropertyForm` / `ibPropertyPicture` / `ibPropertySpreadsheet` / `ibPropertySource` because their values are heavy/binary blobs).
2. Per-CLSID groups sorted by `GroupOrderRank` (Common modules → Common forms → ... → Catalogs → Documents → ... → Attributes → Forms → Modules).

Under the root config specifically, common-tier groups (modules, forms, templates, pictures, interfaces, roles, languages) are wrapped in a synthetic `Common` umbrella (`g_diffCommonUmbrellaClsid`), matching the `m_treeCOMMON` node in the configuration tree.

### Skipped sub-groups

Per `treeConfiguration_impl.cpp` (`AddCatalogItem`), some sub-object types are intentionally hidden inside catalog/document context because their data lives in the parent's properties:

- `g_metaModuleCLSID` (object module)
- `g_metaManagerCLSID` (manager module)
- `g_metaPredefinedAttributeCLSID` (predefined items — folded into properties listing)

`WalkPair` filters them before group emission — since 2026-08-06 by **asking the owner**
(`child->IsAcceptedByParent()` → `FilterChild`) rather than by a list of clsids. The three
above are exactly the children no owner accepts: it does not create them, so it does not
host them, so they do not belong under it. The old `IsSkippedSubGroupClsid` was a second
copy of a rule the designer tree already had, and its own comment said so.

---

## UI model (`ibDataViewMetaDiffModel`)

`ibDataViewModel` subclass that wraps the walker output plus a parallel `m_selected` bitmap for merge selection. Columns:

| Idx | Name | Renderer | Notes |
|---|---|---|---|
| 0 | merge | `ibDataViewToggleRenderer` (activatable) | hides on Same / property rows that have nothing to act on |
| 1 | object name | `ibDataViewIconTextRenderer` | per-class icon via `ibBackendPicture::GetPicture(GetIconClsid())`; folder art for synthetic groups |
| 2 | status | text | localized label (Same / Changed / Reordered / Only in current / Only in other) |
| 3..4 | left / right | text | for property rows shows stringified values; for object rows shows the object name on the present side and "Missing" on the empty side |

### Refcounted row objects

`ibDataViewItem::IsContainer()` returns false unconditionally for RawId-mode items (the encoded-int trick), so the dataview's expander-visibility code can't see the tree shape. The model wraps each record in a refcounted `ibDataViewDiffRowObject` so `item.IsContainer()` dispatches to a real virtual that asks back into the model (`m_model->RecordHasChildren`).

The row object is also the only side that overrides `IsContainer` — the model's own `IsContainer` is inherited from the base default which routes through `item.IsContainer()`. Single source of truth: `RecordHasChildren`.

### Filter mode

`ibDataViewMetaDiffModel::FilterMode` (All / Differences / SameOnly) with bottom-up visibility recomputation: pass 1 marks records that match the filter directly; pass 2 walks reverse pre-order and promotes ancestor containers so the tree shape stays navigable. The view's filter combobox calls `SetFilterMode` which triggers a `BeforeReset / AfterReset` cycle so the dataview repopulates against the new visibility map.

### Selection cascade

Clicking a container's checkbox cascades down to every merge-candidate descendant (`CascadeSelection`). Ancestors of the clicked row get `ValueChanged` notifications so their aggregate checkbox state re-renders. Tri-state (mixed) is rendered as unchecked for now — full tri-state would need a custom toggle renderer.

---

## Apply merge

`OnApplyMerge` dispatches by (record status × direction), wrapping each per-record apply in `try { ... } catch (const ibBackendException& err)` so a DDL failure on one Catalog doesn't abort the whole merge.

| Status | Pull (other → current) | Push (current → other) |
|---|---|---|
| `OnlyInRight` | ADD to current | DELETE from other |
| `OnlyInLeft` | DELETE from current | ADD to other |
| `Changed` | REPLACE current with other's | REPLACE other with current's |
| `Reordered` | skipped (V2) | skipped (V2) |

### ADD

```cpp
ibWriterMemory writer;
source->CopyObject(writer);                                  // recursive — children follow
ibReaderMemory reader(writer.pointer(), writer.size());
ibValueMetaObject* newObj = meta->CreateMetaObject(
    source->GetClassType(), targetParent, /*runObject*/ false);
if (newObj->PasteObject(reader)) {
    // PasteAndRunObject reads the source GUID but doesn't assign it
    // at the top level — restore so future compares pair the new
    // object with its source-side counterpart.
    newObj->SetCommonGuid(source->GetGuid());
}
```

`PasteAndRunObject` (the top-level path) discards the GUID on read; the inner private `PasteObject` recursive variant preserves child GUIDs from the buffer. So `SetCommonGuid` is needed only at the top level.

### DELETE

Uses `ibMetaData::RemoveMetaObject` — marks the object deleted (`MarkAsDeleted`), doesn't physically remove from `m_children`. Matches the designer's own delete UX; the deleted-flag is honoured by the tree builders.

### REPLACE

`CopyObject` the source side first (before deleting target — target's child objects shouldn't be live when we deserialize, but the source buffer must be filled while source is intact), then `RemoveMetaObject` on target, then create-and-paste at the same parent with the source's GUID restored.

### Descendant skip

For a selected `OnlyIn*` record, descendants of that record are also selected (cascade) but get skipped in apply (`HasOnlyInAncestor`) — the ancestor's recursive `CopyObject` covers the whole subtree on a single Apply, so processing them individually would double-create.

### Save callback

The view accepts an optional `SetRightSaveCallback(std::function<bool()>)`. After Push, the view calls it so the right-side file (or whatever the backing is) gets persisted. When unset, the direction wxChoice hides the Push entry — there'd be nowhere to write the result.

After Apply, the designer refreshes the metadata tree against the mutated `activeMetaData`.

---

## Identifier preservation

Two identifiers live on every meta object:

- `m_metaGuid` — persistent UUID, the cross-config identity key
- `m_metaId` — numeric, internal, unique within one metadata tree

`ibValueMetaObject::CopyObject` serialises both into the buffer; `PasteObject`'s recursive private path assigns the source's GUID to every child it creates. The public `PasteAndRunObject` reads but discards the top-level GUID — we restore it via `SetCommonGuid(source->GetGuid())` so subsequent compares pair the merged object with its source-side counterpart by GUID.

---

## Dataview API refactor (bonus)

While building this feature, the `GetChildren` virtual on `ibDataViewModel` / `ibValueModel` was removed in favour of the unified fetch contract (`GetFirstFetch` / `GetNextFetch` / `GetPrevFetch`). Non-paged sources return the whole batch from `GetFirstFetch` and leave `GetNextFetch` / `GetPrevFetch` at the base default (returns 0, signalling "exhausted"). Paged sources stream via the full triple.

Touched files:

- `src/engine/backend/modelView.h` — removed pure-virtual `GetChildren` from `ibDataViewModel`
- `src/engine/backend/model.h` — removed pure-virtual `GetChildren` from `ibValueModel`; folded the four legacy overrides (`ibValueModelTableBase`, `ibValueModelRamTableBase`, `ibValueModelTreeBase`, `ibValueModelRamTreeBase`) into the existing `GetFirstFetch` paths
- `src/engine/frontend/win/ctrls/dataview/dataview.h` + `datavcmn.cpp` — migrated the three concrete-model overrides (`ibDataViewIndexListModel`, `ibDataViewVirtualListModel`, `ibDataViewTreeStore`)
- `src/engine/frontend/win/ctrls/dataview/datavgen.cpp` — the single internal call site (`GetModel()->GetChildren(parent, modelSiblings)`) routed through `GetFirstFetch`
- `src/engine/designer/win/dlg/predefinedEditor.{h,cpp}` — model override migrated

### BuildTreeHelper fix

`BuildTreeHelper` in `datavgen.cpp` previously bailed on `!item.IsContainer()` before fetching, which for the invisible root (empty `ibDataViewItem`, `IsContainer()` returns false unconditionally) skipped the top-level fetch entirely. Tree-mode controls stayed empty after `Cleared()` / `AssociateModel` re-fires. The guard is now `item.IsOk() && !item.IsContainer()` so the empty root passes through.

---

## Known V1 gaps

- **Reordered status** has no apply path yet — children appear in the wrong order on the target, but the structural composition is correct.
- **Tri-state (mixed) checkbox** on partially-selected containers renders as unchecked; clicking it selects all (going `unchecked → checked` first).
- **Two-file compare** has no left-side save callback yet, so Pull mutations to the left file aren't persisted automatically — the user has to save manually via Configuration → Save configuration if the left side is `activeMetaData`, or this scenario isn't supported when both sides are files.
- **Property-level merge** (cherry-pick individual property changes from a Changed object) — V2; today the whole object is the merge unit, even if only one property differs.
