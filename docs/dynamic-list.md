# Dynamic list — the universal list/tree

> **Status (2026-06-28): arc in progress.** Designer surface + runtime settings +
> form LANDED; the unified `ibValueDynamicList` is assembled and builds green.
> Settings application split into per-aspect helpers; grouping drill (incl. aggregate-free
> `TOTALS BY`) landed; the fetch path is const-correct (no `const_cast`). The List-settings
> field picker (dot-walk reference expansion, drag-to-add) landed 2026-07-07. Reference
> memory: `project_dynamic_list_unification`, `project_totals_by_without_aggregate`.

## What it is

`ibValueDynamicList` is **the single list/tree/register value** that replaces the
per-family models (`ibValueListDataObjectEnumRef` / `…Ref` / `…RefDocument` /
`ibValueListRegisterObject` / `ibValueModelTreeDataObjectFolderRef`). One entity:

- **Universal** — for lists, trees, and registers. The view (flat vs tree) is the
  front-end's call (`SetViewMode` → parent scope), **not** the class type.
- **Works through a QUERYABLE, never metadata.** Columns / identity / parent all
  come from `ibBackendQueryable` (`GetColumns` / `GetPrimaryKeyColumns` /
  `GetHierarchyColumn`), family-blind — catalog ≡ register ≡ document ≡ custom query.
  No `GetDataReference`, no `GetGenericAttributeArrayObject`, no metaobject in fetch.
- **Clean system — no raw guids.** Row identity is the **keyset** (primary-key
  column *values*); the cursor anchor is the sort-column values. `register ≡ ref`
  because identity is a column set, not a guid.

## The class

```
ibValueDynamicList : public ibValueModelTreeBase, public ibSourceDataObject, public ibPropertyObject
```

A tree base understands a flat list too (one root, the parent is the root), so the
single class carries both forms. It is **not** `ibValueModelTreeDataObject` — that
one is metaobject-bound (its column collection comes from the metaobject and its
ctor needs one). The dynamic list owns its own queryable-derived column collection.

It is also an `ibPropertyObject`: the list's own **Source** and **Settings** properties
surface onto the form attribute (like `ibValueSizerItem`) — the attribute just casts the
runtime value to `ibPropertyObject`, knowing nothing about "a dynamic list". `OnPropertyChanged`
is the hook (a virtual, not a backend function-pointer); `Read/WriteProperty` persist the Source
property plus the settings. The class/type name reported to the runtime comes from the **factory**
(`ibValue::GetClassName()` → the `VALUE_TYPE_REGISTER` name resolved by clsid), not a hardcoded
literal — the literal `"DynamicList"` lives only in the registration.

### Empty + SetSource

The list is created **empty** — it does not know its queryable up front. The source
is added afterwards:

```cpp
ibValueDynamicList* list = new ibValueDynamicList();   // empty — source set later
list->SetSource("Catalog", "Goods");                   // resolve via the factory
//   or list->SetSourceQueryable(q);  /  list->SetCustomQuery("SELECT …");

// The ctor may also take the queryable directly (null → set later):
ibValueDynamicList* list2 = new ibValueDynamicList(queryable);
```

`SetSource(ns, name)` resolves the queryable through
`ibQueryableFactory::Resolve(ns, name)` (`appData->GetQueryableFactory()`) — any
queryable registered there (register / reference / …) is selectable. The front-end
exposes a **source picker** built from the factory's registered `(ns, name)` set.

### L5 composer + the fetch provider

The list carries the **L5 composer** (`m_composer`, visible via `GetComposer()`).
`SetSource` wires it onto the queryable (`composer.FromSource(queryable)`); a custom
query uses `composer.FromText(text)`.

Fetch is **thin** — the composer does the work through a **special driver/provider**
(a `ibCompositionDriver` subclass). `composer.Run(provider)` calls `provider.OnRow`
per row; the provider builds the row node **straight into the table**:

```cpp
void OnRow(int level, bool hasChildren, const std::vector<ibValue>& values) override {
    // keyset identity = the primary-key column positions
    std::vector<ibValue> key;  for (size_t i : m_keyIdx) key.push_back(values[i]);
    auto* node = new ibValueDynamicList::ibDynamicListNode(m_tree, key, hasChildren);
    for (size_t c = 0; c < m_cols.size(); ++c)
        node->AppendTableValue(m_cols[c]->GetColumnId(), values[c]);
    m_rows.push_back(node);
}
```

`Get*Fetch` is then ~3 lines (build provider, `composer.Run`, wrap the driver rows).
**No hand-rolled row loop in the model.** The hierarchy scope (flat-scan vs parent
scope) rides on `ibReadPageRequest` (`m_flatScan` / `m_hierarchyCol` + `m_hierarchyKey`),
filled by the model directly (front-end List view = `s_constIgnoreParent` → flat).

**Fetch is `const`.** `Get*Fetch` / `RunPage` only READ the source and RETURN rows — they
never mutate the model. A returned row node *is* mutable later (write-back / change-notify),
so the node holds the model link as `const ibValueModelTreeBase*` (`ibValueTreeNode::m_valueTree`):
the node only reads it (`IsAttached`) or pokes the notifier (`RowValueChanged` / `m_modelProvider`,
both non-mutating). So the const fetch hands the provider a plain `this` — **no `const_cast`**.
The const link says "the row may notify the model", not "fetch mutates"; it also removed the
two pre-existing casts in `objectListQuery.cpp`.

## Settings (Filter / Order / Group)

`ibValueListSettings` (`backend/composition/listFilter.h`) is the runtime, script-
visible settings container (≈ a SettingsComposer):

- **Filter** — `ibValueFilterList` of `ibValueFilterItem {Use, Field, Comparison,
  Value}`; `Comparison` is the runtime enum `ibValueEnumComparisonKind`.
- **Order** — `ibValueSortList` of `ibValueSortItem {Field, Direction}`; `Direction`
  is `ibValueEnumSortDirection`.
- **Group** — `ibValueGroupList` (field paths).
- **Source config** — `MainTable` / `UseCustomQuery` / `QueryText` / `KeyFields`.

Applied to the composer through `ibApplyDynamicSettings(composer, settings)` — one source
of truth, shared with the legacy list path. It splits into per-aspect helpers
`ibApplyDynamicFilters` / `ibApplyDynamicSorts` / `ibApplyDynamicGroups` (the combined call
runs all three) so a caller can apply only part — the grouping drill applies Filters + Sorts
onto a scoped composer but supplies its OWN per-level grouping. **Applied on change, not cleared
and rebuilt every fetch.** Fields are paths: dot-walk (`"Ref.Owner"`) resolves to an auto-JOIN
on the door.

### Grouping & drill

A non-empty **Group** turns the list into a drillable group tree — the native parent hierarchy
is bypassed, and the grouping field is ANY query-result field, not the table's parent column:

- Each level is a `TOTALS BY <dim>` read, **including aggregate-free pure grouping**
  (`TOTALS BY <dim>` with no aggregate is a valid query — the parser and composer allow it;
  see `project_totals_by_without_aggregate`).
- Drill is lazy and **scoped per parent**: expanding a group RE-FETCHES with `dim == value`
  for every already-drilled dimension (the group becomes a filter), grouped by the NEXT
  dimension — or, past the last dimension, the plain detail rows. Generic over N levels.
- Detail rows page in portions (the shared `ibReadPageRequest`); a whole group level loads at
  once for now (group-level paging is a follow-up).

`ibBackendQueryableHolder` (`query/queryable.h`) carries the source config
(`UseCustomQuery` / `GetQueryText` / `GetKeyFields`) for the holder model.

## Designer + form

- **Type** — `DynamicList` appears in the Choice type dialog
  (`advpropType.cpp`, `FillByClsid(g_dynamicListCLSID)`), selectable as a form
  attribute's type alongside Table.
- **Settings entry point** — a form attribute (`ibValueFormAttribute`) of type
  DynamicList gets a **«List settings»** property in the inspector; picking
  «Open...» fires `ibValueListSettings::ms_showDialog` → the settings form. The
  settings live on the attribute (`m_listSettings`; harden: serialised).
- **Form** — `ibDialogListSettings` (`frontend/win/dlgs/listSettings/`), a modal
  `wxDialog` + `wxNotebook` with **Source / Filter / Sort / Group** tabs. The
  Source tab is where the **main table / custom query** is chosen. Backend→frontend
  bridge: the frontend registers `ms_showDialog` at load; the backend never links
  the frontend.

### The field picker (Filter / Sort / Group)

Each of the Filter / Sort / Group tabs is a **two-pane picker**, not a bare list — the same
shape across all three, so a field reaches any composition list the same way:

- **Left — an available-fields tree.** Rooted on the source's explorer
  (`GetSourceExplorer()`), it lists the source's fields with attribute icons. A **reference**
  field carries a `[+]` and **lazily expands** into its target's fields on demand
  (`OnFieldTreeExpanding` → `ExpandSourceFieldNode`), so a path is dot-walked arbitrarily deep
  (`Ref.Owner.Code`). The metaData that resolves a reference's target is `SourceMetaData()` —
  the dynamic list's own (`GetSourceMetaData()`), else the ACTIVE config; without a valid
  metaData `ConvertToMetaIds` yields nothing and every field reads as a leaf (a flat tree, no
  `[+]`). A plain (non-source) model with no explorer falls back to its flat columns, and even
  those get a `[+]` when the column's declared type is a reference.
- **Right — the composition list**, an `ibDataViewCtrl` (the same control the Filter tab uses)
  editing the settings buffer in place. Sort's **Direction** is an inline choice column
  (`ibValueSortItem::SetDirection`), edited like the Filter's Comparison; Group is field-only.
- **Add a field** by double-click, by **dragging** a tree node onto the list
  (`OnFieldTreeBeginDrag` → a `wxTextDataObject` path dropped on `ibFieldDropTarget`), or via
  the **Add / Remove** context menu — which fires on the clicked ROW
  (`wxEVT_DATAVIEW_ITEM_CONTEXT_MENU`, not the empty-area `wxEVT_CONTEXT_MENU`).
- The two panes split on a **draggable sash** (`wxSplitterWindow`).

## Source binding — as a metadata-agnostic source

The dynamic list is an `ibSourceDataObject`, but it has **no metaobject** — its columns live only
in the queryable. It plugs into the form-binding dot-walk (see `form-attribute-binding.md`)
through the neutral source-column seam, not a metaobject:

- **Columns are source columns.** A queryable column (`ibBackendQueryColumn`) IS-A
  `ibBackendSourceColumn` (name / synonym / type), so the list vends them with no adapter:
  `GetSourceColumn(id)` finds a live queryable column by id; `GetSourceExplorer()` builds the
  metadata-free column template the same way (`AppendColumn(col)`).
- **Closed column set.** `HasOwnColumns()` is `true`: the queryable's columns are the COMPLETE
  set, so `WalkSource` treats a missing id as a BROKEN binding (a column dropped by a Type/source
  change reads back broken in the caption) instead of re-finding it config-wide. A metaobject
  source is open (`false`) and dot-walks references config-wide.
- **Metadata via the queryable.** `GetSourceMetaData()` returns `GetSourceQueryable()->GetMetaData()`
  (a metaobject source returns its metaobject's) — the ONE accessor the form's `GetMetaData()`
  chains to, so a source with no metaobject still resolves a metadata context.
- **Table fact without a source.** `IsTableSource()` answers list-vs-object by CLSID through the
  class factory (`IsTableValue()`), so the list reports a table before a source is even picked.

## Migration

Old per-family classes are **not deleted**. Sources are moved onto the dynamic list
incrementally — documents and catalog list/select first (`CreateSourceObject` →
`new ibValueDynamicList(); list->SetSource(ns,name)`), the rest after it proves out.
`LIST_FOLDER`/`LIST_ITEM` modes are gone — folder vs item is a **filter**, not a
class type; flat vs tree is the **view**.
