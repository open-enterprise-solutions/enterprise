# Dynamic list — the universal list/tree

> **Status (2026-07-11): the migration is DONE.** `ibValueDynamicList` is now THE list
> for every standard metadata list/select (catalog / hierarchy / document / register /
> enumeration / charts) — the per-family model classes and their registrars are DELETED,
> not merely bypassed. The **source-command layer** (below) is the bridge that let a
> metadata-blind list act on a metadata row: commands, open, select, key, columns, and the
> source metaobject all forward from a per-metaobject descriptor. Folder / select / default
> sort became creation-time **settings** on the composer, not subtypes. Designer supports an
> arbitrary-query first tab (runtime rendering of the result is the remaining "later").
> Reference memory: `reference_source_command_layer`, `project_dynamic_list_unification`,
> `project_totals_by_without_aggregate`.

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
ibValueDynamicList : public ibValueModelCursor, public ibSourceDataObject, public ibPropertyObject
```

`ibValueModelCursor` (a cursor tree base) understands a flat list too (one root, the parent is the
root), so the single class carries both forms. It is **not** `ibValueModelTreeDataObject` — that one
is metaobject-bound (its column collection comes from the metaobject and its ctor needs one). The
dynamic list owns its own queryable-derived column collection.

It is also an `ibPropertyObject`: the list's own **Source** and **Settings** properties
surface onto the form attribute (like `ibValueSizerItem`) — the attribute just casts the
runtime value to `ibPropertyObject`, knowing nothing about "a dynamic list". `OnPropertyChanged`
is the hook (a virtual, not a backend function-pointer); `Read/WriteProperty` persist the Source
property, the settings, plus the default **view** (see *Choice mode* below). The class/type name reported to the runtime comes from the **factory**
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

### L5 composer + the fetch

The list carries the **L5 composer** (`m_composer`, the inherited `GetModelComposer()`).
`SetSource` wires it onto the queryable (`composer.FromSource(queryable)`); a custom query uses
`composer.FromText(text)`.

Fetch is **thin and NOT the list's own** — the dynamic list has no per-list fetch provider anymore
(the old `ibDynamicListProvider` + `ibDynamicListNode` are deleted). It fetches through the BASE
`ibValueModel::RunComposerPage`, exactly like every other model: ONE fetch in the parent, yielding
`ibComposerNode` rows read through the base `GetViewData<ibValueTreeNode>`. Row identity is the
**keyset** (primary-key column values), the cursor anchor is the sort-column values. The hierarchy
scope (flat-scan vs parent scope) rides on `ibReadPageRequest` (`m_flatScan` / `m_hierarchyCol` +
`m_hierarchyKey`), filled by the model directly (front-end List view = `s_constIgnoreParent` → flat).

**Fetch is `const`.** `RunComposerPage` only READS the source and RETURNS copied row nodes — it never
mutates the model, so it hands the provider a plain `this`, **no `const_cast`**. A returned node is
mutable later (write-back / change-notify) but holds the model link as a const pointer it only reads
(`IsAttached`) or pokes the notifier through.

## Settings (Filter / Order / Group)

`ibValueListSettings` (`backend/composition/listFilter.h`) is the runtime, script-
visible settings container (≈ a SettingsComposer):

- **Filter** — `ibValueFilterList` of `ibValueFilterItem {Use, Field, Comparison,
  Value}`; `Comparison` is the runtime enum `ibValueEnumComparisonKind`.
- **Order** — `ibValueSortList` of `ibValueSortItem {Field, Direction}`; `Direction`
  is `ibValueEnumSortDirection`.
- **Group** — `ibValueGroupList` (field paths).
`ibValueListSettings` itself holds only those three — `m_filter` / `m_order` / `m_group`
(`composition/listFilter.h`). The **source config** is not on it either: the main table is the
list's `Source` property and the arbitrary query over it is `m_propertyUseCustomQuery` /
`m_propertyCustomQuery`, both on the LIST — which is where the thing the user edits belongs.

⚠ All three lists are read and written through the **facade** (`Count` / `Get…` / `Add`), never off
the buffer fields: in facade mode the lines live in the COMPOSER and the buffer is empty, so a sort
or a grouping set on a live list used to have nothing to write and never reached the disk
(fixed 2026-08-07). A sort line and a grouping line travel as DATA — a path plus its direction, a
path plus its unfold kind — because unlike the filter TREE there is no line object in facade mode.

The settings object is a **transactional dialog buffer**, moved between it and the composer by
one pair (`composition/listFilter.h`):

- `ibLoadSettingsFromComposer(settings, composer)` — on dialog open, composer → buffer;
- `ibCommitSettingsToComposer(composer, settings)` — on OK, buffer → composer, **CLEAR then
  re-apply** (that clear-and-reapply *is* the commit); Cancel is simply a no-op.

Fields are paths: dot-walk (`"Ref.Owner"`) resolves to an auto-JOIN on the door.

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

(The holder's `UseCustomQuery` / `GetQueryText` / `GetKeyFields` virtuals are GONE, 2026-08-07 -
never overridden, never called. They came from a reading in which an arbitrary query REPLACED the
main table, so the holder had to say which of the two it was and, having no table, be told its key
by hand. The main table is always there now and the query lives over it, so the key is its PK by
construction.)

## Designer + form

- **Type** — `DynamicList` appears in the Choice type dialog
  (`advpropType.cpp`, `FillByClsid(selectorDataType, g_valueDynamicListCLSID)`), selectable as a
  form attribute's type alongside Table.
- **Settings entry point** — a form attribute (`ibValueFormAttribute`) of type
  DynamicList gets a **«List settings»** property in the inspector; picking
  «Open...» calls the frontend static `ibDialogListSettings::ShowListSettingsDialog(list)`
  (`frontend/win/dlgs/listSettings/listSettings.h`, invoked from `advpropDynamicList.cpp`).
  The settings buffer lives on the **base model** — `ibValueModel::m_listSettings`
  (`backend/model.h`) — not on the attribute, and no subclass holds its own.
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
  `GetSourceExplorer()` builds the metadata-free column template the same way
  (`AppendColumn(col)`).
- **One resolve path for every source.** The `HasOwnColumns()` open/closed distinction is gone,
  and so is the config-wide re-find fallback: every source now resolves through
  `ibSourceDataObject::WalkColumns(path, …)` over its own explorer, so a missing id is a
  genuinely broken binding (a column dropped by a Type/source change reads back broken in the
  caption) rather than something to hunt for elsewhere.
- **Metadata via the queryable.** `GetSourceMetaData()` returns `GetSourceQueryable()->GetMetaData()`
  (a metaobject source returns its metaobject's) — the ONE accessor the form's `GetMetaData()`
  chains to, so a source with no metaobject still resolves a metadata context.
- **Table fact without a source.** `IsTableSource()` answers list-vs-object by CLSID through the
  class factory (`IsTableValue()`), so the list reports a table before a source is even picked.

## Source-command layer

The list is metadata-blind, yet a standard list must **act on a metadata row** — list and run
commands (Add / Copy / Edit / Delete / Post / AddFolder …), open a row, resolve a picker's select
value, build the row's key, fill the columns, show the source's icon. All of that is metadata
behaviour. The bridge is a per-metaobject **descriptor** the list talks to WITHOUT knowing the
metaobject (`reference_source_command_layer`).

- **`ibQueryableSourceDescriptor`** (`query/queryableFactory.h`) — the base, held PARALLEL to the
  queryable (queryable = data; the descriptor = behaviour). Copy is `= delete` (it is `this`-bound
  to its metaobject; a copy would keep the ORIGINAL's ids — so a metaobject holding one is
  non-copy-constructible, which forces the paste-via-factory path with fresh ids). The surface is
  three neutral-default groups:
  - **ROW DATA / presentation** — `GetSelectValue(rowValues)` (picker value: a record → its
    reference cell, a register → its composite record key), `GetItemKey(rowValues)` (row identity:
    a record → its reference guid, a register → its COMPOSITE record key — registers have several
    key columns), `GetRowKeyByValue(value)` (the INVERSE of the fetch's row-key: an identity VALUE →
    its primary-key column values, for the FindRowValue selection-restore — see below),
    `FillSourceExplorer(explorer)` (the column set, system columns hidden).
  - **COMMAND INTERFACE** — `GetCommandCollection(formType, out)` (the command band),
    `CallAsCommand(id, anchor, key, srcForm)` (run one by id — `key` = the selected row for delete / edit / copy,
    `anchor` = the create context, the front-computed drill folder / top a NEW element parents under).
  - **ENTRY** — `ShowValueByKey(key, srcForm)` (open a row's value directly — the double-click /
    "enter" affordance, no command id).
- **Two templates, so a pure query source is not forced to carry the command surface:**
  - **`ibMetaQueryDescriptor<TQueryable, TMeta>`** — the query-identity HALF only
    (`GetNamespace` / `GetName` / `CreateQueryable` / `GetQueryable`, `m_meta` + `m_queryable`) PLUS the
    UNIVERSAL `GetRowKeyByValue` (it reads the PK columns off the value through its own `m_queryable`, so
    a record and a register share ONE implementation — no per-family override). A **constant** uses this:
    it is registered only so `From(constant)` resolves, never shown as a list, so it leaves the whole
    row+command surface at the base's neutral defaults and carries none of it.
  - **`ibMetaCommandDescriptor<TQueryable, TMeta> : ibMetaQueryDescriptor`** — adds the row +
    command surface, each call FORWARDED to `this->m_meta`. **Records and registers** use this. The
    metaobject carries the real behaviour, polymorphic down its OWN inheritance (`RecordDataRef` →
    `MutableRef` → `HierarchyMutableRef`; `Document : MutableRef` adds Post; `RegisterData` its
    record-manager set; enum → the neutral ref base, silent). No mixin, no separate command
    interface — `TMeta` must have the methods or the template won't compile (a compile-time contract).
- **The list is a mirror.** `GetSourceDescriptor()` re-resolves the descriptor LIVE by table id
  (parallel to `GetSourceQueryable()`, never cached). `ActivateItem` → `holder->ShowValueByKey`,
  the command band → `holder->GetCommandCollection` / `CallAsCommand`, choose → `holder->GetSelectValue`,
  `GetItemKey` → `holder->GetItemKey`, the explorer → `holder->FillSourceExplorer`. Bodies live in
  `commonObjectAction.cpp` (record/register) + `documentAction.cpp` (Post), with the metaobject they
  implement — NOT in a list TU.

### Selection restore — FindRowValue keys by the PK, through the descriptor

After a child-form save changes / creates a row, the TableBox re-finds the current row: `ResolveLineByValue`
→ `model->FindRowValue(changedValue)`. The dynamic list is metadata-blind, so it FORWARDS the key build to
the source descriptor — `GetSourceDescriptor()->GetRowKeyByValue(value)` — and wraps the result in a key-only
`ibComposerNode` stub; the freshly-fetched batch matches it by `m_rowKey` (`IsEqualTo`) and lands the focus.

`ibMetaQueryDescriptor::GetRowKeyByValue` reads the source's `GetPrimaryKeyColumns()` off the identity value
through the `ibSourceDataObject` hop gate — the SAME columns the fetch stamps into a node's `m_rowKey`. It is
UNIVERSAL: a record's reference yields its self-reference (one guid), a register's record-manager decomposes
into its COMPOSITE key. A custom-query source (no descriptor) falls back to the base `{value}` stub.

This restores the per-list "find by key" the deleted family models did. The generic single-value stub matched
a catalog / document row (one guid) but never a register row (multi-column key) — which is why a register list
dropped its selection on a value change until the key build moved onto the descriptor.

**Only an identity that APPEARED or MOVED positions the list (2026-08-03).** There used to be a second
channel beside `createdValue`: `NotifyChange` stamped `m_changedValue` and the list re-positioned onto the
saved element. It moved the cursor off whatever row the user was standing on whenever an object form was
saved while the list was browsed elsewhere — an object form stays open, so this was ordinary. The channel is
GONE, not gated: the knowledge it carried is no longer needed, because the current row is a refcounted node
that survives the wipe and re-locates itself in the new batch by its own row-key (`PagedRefresh` stamps it
into `m_pagedRestoreFocus`; `OnPagedFetchResetComplete` matches it via `IsEqualTo`). It dates from when a row
had to be SEARCHED for after a refresh. `NotifyChange` now means exactly "re-read".

What still earns an anchor is an identity the list cannot find: one that did not exist (a create), or one
that MOVED. The second is a register: its key floats over its dimensions, so editing a dimension does not
modify a record, it replaces it — the old key is gone from the table. `WriteRegister` therefore compares the
key composite across `SaveData` (which rewrites it in place via `SetKeyValues`) and reports `newObject ||
keyMoved` through `NotifyCreate`. For the list that is the truth: the row it held is gone and this one is
new. A plain re-write leaves every key where it was and sends no anchor at all.

**A stub answers for its identity too (2026-08-03).** That stub does not just travel to the bootstrap — it
BECOMES the current row (`ApplyCurrentLine`), and it stays current until the user clicks, because the
bootstrap's own `Select` is programmatic and fires no `SELECTION_CHANGED`. `GetItemKey` decoded identity from
the node's CELLS, which a stub has none of, so every by-key command — Copy / Edit / Delete / MarkAsDelete,
all of which open with `if (!key.IsOk()) return` — silently did nothing on a just-created element. Reported
as "add an element, then try to clone it: no reaction", clearing as soon as any row was clicked.

The stub is not a row without a key; it is a row that is NOTHING BUT its key. So `GetItemKey` resolves the
row by that key first (`ResolveAnchorByKey` — the SAME point lookup the keyset anchor and
`BuildAncestorBreadcrumb` already run on a stub) and decodes identity from what comes back. One question,
one existing answer, asked in a third place — and because it sits on the model rather than on the desktop
TableBox, the web front gets it for free.

### Source metaobject + icon / caption (via the queryable)

The list vends `GetSourceMetaObject()` (its `ibSourceDataObject` override) THROUGH the queryable —
`GetSourceQueryable()->GetSourceMetaObject()` returns the metaobject behind a metadata source
(`ibRecordQueryable` / `ibRegisterDataQueryable` return their `m_meta`; a custom-query source has
none → null). This is the ONE path the front reads the source's **icon** (`GetSourceMetaObject()->GetIcon()`)
and **caption** (`"Type: Synonym"` from `GetClassName()` + `GetSynonym()`) off, exactly as every
other source object does. (A blind list returning null here is what made a migrated catalog show no
row icon — the fix was to forward the metaobject, NOT to add a per-row icon method to the model.)

### Per-config source factory (copy-safe resolution)

Metadata-backed sources are **per-config**, not global. Each open configuration's snapshot
(`ibMetaImage`) owns its own `ibMetaQueryableFactory` (`ibMetaData::GetSourceFactory()`); a metaobject
registers its descriptor into **its own** config via the facade `m_metaData->RegisterSource(&m_queryable)`
(on run) / `UnregisterSource` (on close) — not a free hook against one global registry. So a **copied**
Document (or a second open config) keeps its OWN set of queryables; the old single global factory, keyed
by `(namespace, name)`, would collide or leak the original's descriptor into the copy — which is exactly
why a copied dynamic list resolved the original's columns.

Resolution always goes **through the metadata the query runs ON BEHALF OF**, never `appData` /
active-metadata directly:

- The list / its `Source` property / its variant resolve via the owner's config —
  `owner->GetMetaData()->GetSourceFactory()` (the list bridges `GetMetaData()` → `GetSourceMetaData()`,
  which reaches the owner form's config through the attach owner). The picker
  (`ibPGDynamicSourceProperty`) lists that same config's descriptors.
- The **query language** (`From Catalog.X` by name, `valueComposer` → `dataComposer` → `queryLowering`)
  threads the config down: `ibDataComposer` holds `m_metaData` (set from the list's `GetSourceMetaData()`,
  or from the running config for a script query) and installs an `ibSourceMetaDataScope` before
  `Execute` — **parallel to `ibTempSourceScope`** — which `ResolveSource` reads to pick the config's factory.

A per-config factory **descends to the global base factory on a resolve miss**, and every call site falls
back to it when there is no metadata in scope — the global factory is the future plugin / system seam
(empty today). The metaobject-coupled descriptor templates (`ibMetaQueryDescriptor` /
`ibMetaCommandDescriptor`) live in `commonObject.h`, keeping the L4 `queryableFactory.h` metadata-agnostic
(base descriptor + factory only).

### Folder container + creation-time settings

Folders are **settings**, not a subtype. A hierarchical list is built by a small factory that sets
the defaults at CREATION (serialised on the composer, so a user can remove them):

- `ibCreateList(q, presCol, view)` — a flat list + one default presentation sort.
- `ibCreateHierarchyList(q, folderCol, presCol, view)` — folder-first sort + presentation sort; the
  TREE comes from the queryable's hierarchy (parent) column.
- `ibCreateFolderList(q, folderCol, presCol, view)` — presentation sort + a fixed `IsFolder = true`
  filter (a folder-select form is just the list with that predicate, added by the backend at form
  generation; selection itself moved to the FRONT).

`view` (default `ibDynamicListView_Normal`) seeds the list's default view; a SELECT / FOLDER-SELECT
form passes `ibDynamicListView_Choice` — all three factories take it uniformly. See *Choice mode* below.

A **folder row renders as a drillable container even when empty** (the folder convention). The DB
level-fetch reports `hasChildren = false` for every row (dataComposer's flat path), so the folder
flag is the ONLY signal the tree has. The folder column is the LIST's display column
(`GetFolderDisplayColumn`, handed in by `ibCreateHierarchyList`, read by `RunComposerPage`) — a
DISPLAY concern of the hierarchical list, **not** a `queryable` accessor (there is no
`GetFolderColumn`). `GetPresentationSortColumn` / `GetFolderColumn` are gone; the hierarchical fetch (`GetHierarchyColumn`)
stays — it is the one structural tree mechanism. (`ibDynamicListView` came BACK — but in a new role: a
serialised default **view**, not a structural subtype/column; see *Choice mode* below.)

### Choice mode — the serialised default view

`ibDynamicListView { Normal, Choice }` is the list's **default view kind**, held on `m_view` and
serialised **implicitly** — a hidden intrinsic field (`"View"`) written/read by `WriteProperty` /
`ReadProperty`, NOT a user-facing property. It is the list's *default behaviour*, not a runtime mode:

- **Seeded at creation.** A metaobject's SELECT / FOLDER-SELECT form creates the list with
  `ibDynamicListView_Choice` (the `view` arg on the `ibCreate*` factories); a plain LIST form leaves it
  `Normal`. A folder-select is inherently a choice, so `ibCreateFolderList` is only ever called `Choice`.
- **One propagation seam — the explorer.** `GetSourceExplorer()` stamps the flag onto the explorer
  (`SetChoiceMode(m_view == Choice)`, after `FillSourceExplorer`). The form auto-build copies the
  explorer's flag onto the main **TableBox** (`mainTableBox->SetChoiceMode(...)`), where it serialises
  **per-form** (the TableBox's own `ChoiceMode` property). So BOTH carry it: the LIST value is the
  default a fresh form inherits; the TABLEBOX copy is the per-form override the user can clear.
- **Why both.** Dropping a dynamic list onto a form (designer drag) makes it stamp `select` by default —
  the list is the *source of the default*. If a form should not be a picker, the user clears choice on
  that form's TableBox; the list's default is untouched. Selection itself stays front-driven (a choice
  TableBox raises the pick affordance) — the flag only says *which behaviour the form defaults to*.

This is why the view is serialised on the list and not left transient: it is the default that flows to
every form built from the source, editable at each form.

## Migration

**Done (2026-07-11).** Every standard metadata list/select now creates an `ibValueDynamicList`
through the `ibCreate*` factories (`CreateSourceObject` / `GetListForm` / `GetSelectForm`). The
per-family model classes (`ibValueListDataObjectEnumRef` / `…Ref` / `…RefDocument` /
`ibValueListRegisterObject` / `ibValueModelTreeDataObjectFolderRef`) and their list-value-type
registrars are **DELETED**. `LIST_FOLDER` / `LIST_ITEM` modes are gone — folder vs item is a
creation-time **filter/sort** setting, not a class type; flat vs tree is the **view**. `valueDynamicList.{h,cpp}`
(renamed from `dynamicList.{h,cpp}` for the `value*` convention) lives in `backend/system/value/` (it is
metadata-independent, a sibling of `valueTable`).

A copied metaobject registers its queryable on paste: `PasteAndRunObject` runs the object with
`pasteObjectFlag` (not `onlyLoadFlag`), so `OnAfterRunMetaObject` registers the source descriptor —
a copy is a NEW object and its source must be resolvable.
