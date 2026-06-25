# Dynamic list — the universal list/tree

> **Status (2026-06-23): arc in progress.** Designer surface + runtime settings +
> form LANDED; the unified `ibValueDynamicList` class is being assembled (clean
> queryable rewrite). Reference memory: `project_dynamic_list_unification`.

## What it is

`ibValueDynamicList` is **the single list/tree/register value** that replaces the
per-family models (`ibValueListDataObjectEnumRef` / `…Ref` / `…RefDocument` /
`ibValueListRegisterObject` / `ibValueModelTreeDataObjectFolderRef`). One entity:

- **Universal** — for lists, trees, and registers. The view (flat vs tree) is the
  front-end's call (`SetViewMode` → parent scope), **not** the class type.
- **Works through a QUERYABLE, never metadata.** Columns / identity / parent all
  come from `ibBackendQueryable` (`GetColumns` / `GetPrimaryKeyColumns` /
  `GetParentColumn`), family-blind — catalog ≡ register ≡ document ≡ custom query.
  No `GetDataReference`, no `GetGenericAttributeArrayObject`, no metaobject in fetch.
- **Clean system — no raw guids.** Row identity is the **keyset** (primary-key
  column *values*); the cursor anchor is the sort-column values. `register ≡ ref`
  because identity is a column set, not a guid.

## The class

```
ibValueDynamicList : public ibValueModelTreeBase, public ibSourceDataObject
```

A tree base understands a flat list too (one root, the parent is the root), so the
single class carries both forms. It is **not** `ibValueModelTreeDataObject` — that
one is metaobject-bound (its column collection comes from the metaobject and its
ctor needs one). The dynamic list owns its own queryable-derived column collection.

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

`Get*Fetch` is then ~3 lines (build provider, `composer.Run`, `AdoptRowsToItems`).
**No hand-rolled row loop in the model.** `ibListFetchDriver::ibTreeScope` carries
flat-scan vs parent scope (front-end List view = `s_constIgnoreParent` → flat).

## Settings (Filter / Order / Group)

`ibValueListSettings` (`backend/composition/listFilter.h`) is the runtime, script-
visible settings container (≈ a SettingsComposer):

- **Filter** — `ibValueFilterList` of `ibValueFilterItem {Use, Field, Comparison,
  Value}`; `Comparison` is the runtime enum `ibValueEnumComparisonKind`.
- **Order** — `ibValueSortList` of `ibValueSortItem {Field, Direction}`; `Direction`
  is `ibValueEnumSortDirection`.
- **Group** — `ibValueGroupList` (field paths).
- **Source config** — `MainTable` / `UseCustomQuery` / `QueryText` / `KeyFields`.

Applied to the composer through `ibApplyDynamicSettings(composer, settings)` — one
source of truth, shared with the legacy list path. **Applied on change, not cleared
and rebuilt every fetch.** Fields are paths: dot-walk (`"Ref.Owner"`) resolves to an
auto-JOIN on the door.

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
