# Form attribute binding — the form's typed source registry + path-through-gate

**Status:** in development (develop), experimental. First hardened slice landed —
exercised through the designer on the hard cases (copy/paste, delete-main, autocomplete,
custom objects) rather than a test harness. The **metadata-agnostic path resolver** landed on
top: the holder (`ibBackendFormAttributeValue`) materialises the value/source, the dot-walk
(`WalkSource`) returns a neutral `ibBackendSourceColumn`, and `ibSourceExplorer` is now
metadata-free (see the two sections below). Uncommitted churn expected; the *model* below is
stable, the surface is not yet spec'd or tested.

## What it replaces

A form used to hold a single hard-wired data object (`m_sourceObject`): one form, one
source, controls reaching into it directly. That model didn't generalise to multiple
sources, custom objects, or designer-time type resolution.

Now a form owns a **registry of typed attributes**, and a control binds by a **PATH** that
is resolved through the form's attribute **gate**. The form no longer knows about
`ibSourceDataObject` for binding — it knows attribute ids and paths.

## The model

- The form owns `std::vector<ibValuePtr<ibFormAttributeValue>> m_attributes` (`form.h`).
  `ibValuePtr` (ref-counted — the holder IS a runtime value) keeps a returned entry valid across
  vector growth and lets the undo stack hold a detached entry alive.
- A binding is a `std::vector<ibSourceId>` (a metaId path). `path[0]` selects a
  **form-local attribute** (the gate); the remainder dot-walks that attribute's value.
- One resolver pair on the form — `GetValueByAttributePath` / `SetValueByAttributePath`
  (`formAttribute.cpp`) — does `FindAttributeById(path[0])` then delegates the tail to the
  wrapper's `GetValueByPath` / `SetValueByPath`. Controls (tablebox, column, textctrl,
  checkbox, …) all read/write through this one path API; nothing binds to a raw source.

## Classes

- **`ibFormAttributeValue`** (`formAttribute.h`, frontend) — a RUNTIME VALUE (`ibValuePtr`-owned,
  `SYSTEM_TYPE_REGISTER(ibFormAttributeValue, "FormAttributeValue")`) that IS the holder. It creates
  its own description internally and exposes everything outside as a **pure FAÇADE** — the concrete
  description is never handed out. `: ibValueDynamicMembers, ibBackendFormAttributeValue,
  ibPropertyObject` (ibValue first → offset 0, see `reference_ibvalue_first_base_pmf`).
  - The description **`ibFormAttribute`** is a PRIVATE NESTED Impl class (the pattern from
    `ibValueModel`'s `ibValueModelColumnInfo` / `ibVariantDataValueImpl`): the amorphous Name / Type /
    id / Main-flag, NO value, `: ibValueDynamicMembers, ibPropertyObject, ibBackendTypeConfigFactory`,
    ctor `ibValueDynamicMembers(TYPE_VALUE, true)` (the `true` = internal, NOT `SYSTEM_TYPE_REGISTER`'d
    → invisible outside). The holder owns it via `ibValuePtr<ibFormAttribute> m_attribute` and forwards
    to it. It accepts ANY type (`GetFilterDataType() == ibSelectorDataType_any`).
  - The holder also holds `mutable ibValue m_value` — the value materialised from the attribute's
    Type. The SOURCE is NOT stored: it is **converted from the value on demand** —
    `GetValueAsSource()` (`m_value.ConvertToValue<ibSourceDataObject>`) / `GetValueAsProperty()`.
    `Refresh()` keeps `m_value` in sync with the Type (`AdjustValue`); a stale value becomes empty of
    the new type. As an `ibPropertyObject` accumulator the holder is what the inspector SELECTS — it
    surfaces the attribute's own props PLUS the materialised value's props (e.g. a dynamic list's
    Source / Settings).
  - **Façade methods** (the only outside surface — there is NO `GetAttribute()`): `GetAttributeName`
    / `GetAttributeId` / `IsMainAttribute` / `const ibTypeDescription& GetTypeDesc()` /
    `ibSourceDataObject* GetSourceValue()`, each forwarding to the private `m_attribute` or to the
    value. `GetBindValue()` returns `&m_value` (the live slot — `BindLocalVariable` stores the
    pointer); `SetSourceValue` seats the source into `m_value` (owned via the value's own refcount).
- **`ibBackendFormAttributeValue`** (`backend_type.h`) — the holder as a backend INTERFACE: exactly
  the façade above (`GetAttributeName` / `GetAttributeId` / `IsMainAttribute` / `GetTypeDesc` /
  `GetSourceValue`), no concrete description leaked. `GetSourceList` vends THESE (attribute + value
  together), so a backend resolver / the picker reads columns through `GetSourceValue()` —
  `GetSourceExplorer()` — without the concrete value type. (The former separate `ibBackendFormAttribute`
  description interface and `GetAttribute()` were removed: the holder answers for its hidden
  description directly.)

## The MAIN attribute

Exactly one attribute carries the Main flag — found by `IsMainAttribute()`, no separate
pointer (single source of truth = the flag). It is where the source object passed to the
form on open lands.

- **Always created in the ctor** (`InitializeForm` → `AddMainAttribute`), even for a
  creator-less auto-built list form.
- **Order matters:** id + name + Main flag + register + SEAT THE SOURCE, and only THEN set
  the Type. A form with no creator resolves its metadata THROUGH the source; the Type
  refresh needs it, so seating the source after `SetDefaultMetaType` is too late → null
  metadata crash in `DoRefreshTypeDesc`.
- **Never serialized** — reconstructed from the source in the ctor. `Read/WriteAttributes`
  skip the main, so copy/paste never produces a duplicate "Object".
- A column path is `{mainAttrId, field}` ("List.Field"), NOT a row-type hop
  ("List.Document1.Field").

## Bind lifecycle

`BindAttributeVariable` binds the attribute name as a local (`<name>` / `ThisForm.<name>`)
and, for the main, the exported `DataSource`. `DropAttributeBinds` removes both before the
wrapper dies (left in place they dangle and the next compile reads freed memory — the
close-after-delete-main crash). `RemoveVariable` clears `m_listLocalValue` too.

Add/rename/delete go through `NextAttributeId` / `RegisterAttribute` / `WireAttribute`
(the last binds when the module is live and calls `InvalidateNames()` so the member surface
`ThisForm.<attr>` refreshes).

`IsWritableBinding`: only a DIRECT field is writable — `[attr]` or `[attr, field]`. A
reference dot-walk is read-only.

## Type resolution — gate-aware

`ibVariantDataAttributeSource` (`variantSource.{h,cpp}`) overrides:

- `DoSetFromMetaId(id)` — resolves a form-attribute id to its type via
  `owner->FindSourceHolder(id)`; the matching holder's `GetTypeDesc()` (façade) is the answer.
- `DoRefreshTypeDesc()` — validates tabular sections against the GATE metaobject. The gate is
  resolved FAMILY-BLIND: `ResolveGateMeta` takes the start attribute's holder and asks its LIVE
  source value — `holder->GetSourceValue()->GetSourceMetaObject()` — instead of a
  clsid → ctor → ConvertToMetaValue gate (which assumed every source is a metaobject). A metaobject
  source (catalog / document list) yields its composite; a queryable dynamic list yields null (no
  metaobject → no tabular section to validate). NOT the runtime `srcObject` (null at designer, blind
  to custom objects) — so type resolution stays correct at design time, on custom objects, and during
  copy/delete cleanup of removed tabular sections.

`GetSourceList` vends **holders** (`ibBackendFormAttributeValue*`), not bare attributes — each
pairs the attribute with its materialised value/source. The "find the holder whose attribute id
matches" lookup is `ibBackendTypeSourceFactory::FindSourceHolder(id)` (`backend_type.cpp`), the
ONE shared site used by the dot-walk and the type/gate resolvers (no per-call re-scan of
`GetSourceList`).

All source controls override `ibBackendTypeSourceFactory::GetSourceDesc()` (returning
`m_propertySource->GetValueAsSourceDesc()`), so the gate descriptor comes straight from the
control with no temporaries.

## Path resolution — the metadata-agnostic dot-walk (`WalkSource`)

The dot-walk that turns a bound `path` into a leaf (its caption / type / validity) lives on the
factory: `ibBackendTypeSourceFactory::WalkSource(path, valid, outText)` (`backend_type.cpp`). The
property layer just forwards to it — `ibVariantDataSource::GetSourceAttributeObject` /
`IsEmptySource` / `MakeString` are one-liners over `m_ownerProperty->WalkSource(...)`. **The
walker knows no metadata**: it steps ids and asks the source for columns; whether the data behind
a node is a metaobject attribute or a queryable column is hidden behind one neutral interface.

- **`ibBackendSourceColumn`** (`query/queryColumn.h`) — the neutral "column, like a DB column":
  `GetName` / `GetSynonym` / `GetTypeDesc` / `IsAllowed`. It is the **base of
  `ibBackendQueryColumn`**, so a metaobject attribute AND a dynamic list's queryable column are
  BOTH an `ibBackendSourceColumn` with no adapter. `WalkSource` / `GetSourceAttributeObject` return
  THIS — the consumer (a column caption, a control's type) never sees the concrete class.
- **Gate 1** — `path[0]` is gated to a form attribute (`FindSourceHolder`, form-local, copy-safe).
- **Gate 2 — the live source.** The head holder's `GetSourceValue()` resolves each deeper hop via
  `ibSourceDataObject::GetSourceColumn(id)` (metadata-blind: a dynamic list maps it to a live
  queryable column). A **closed** source (`HasOwnColumns()` true — a queryable list) makes a MISS a
  BROKEN binding (a column dropped by a Type/source change reads back broken, not silently
  re-found); an **open** metaobject source falls through to the config-wide reference dot-walk
  (`FindAnyObjectByFilter`) — a dotted reference's deeper hop lives in ANOTHER type. After the first
  hit the walk leaves the source's own columns and continues config-wide (reference territory).

`GetSelectMode` (hierarchical-catalog Items / Folders / FoldersAndItems) is a metaobject-attribute
concern, so the control resolves it by `dynamic_cast`-ing the walked leaf to
`ibValueMetaObjectAttributeBase`; a plain queryable column has no select mode → Items. Value
creation / type / metadata are the FACTORY's own (`CreateValueRef` / `GetDataType` /
`AdjustValue`), not the column's — the column carries only name / synonym / type.

## Source explorer — metadata-free column template

`ibSourceExplorer` (`srcExplorer.h`) is the source's column/field TEMPLATE for one-time form
generation + the picker. It is **metadata-free**: a node holds plain values + flags, columns are
appended from the neutral `ibBackendQueryColumn` (`AppendColumn(col)` — deleted/disabled skipped
via `col->IsAllowed()`), tabular sections via `AppendTable(...)`. No `ibValueMetaObject*` member,
no metaobject constructors. Every `GetSourceExplorer()` builder (catalog / document / chart /
record / register / dynamic list) builds it from its own data; `srcExplorer.h` depends only on the
light `queryColumn.h`. List-vs-object is a TYPE fact answered by the class factory —
`ibSourceDataObject::IsTableSource()` → `ibCtorAbstractType::IsTableValue()` by CLSID (no
`dynamic_cast`, works before a source is even picked).

## Dynamic list as a form attribute

A form attribute whose Type is `DynamicList` materialises an `ibValueDynamicList` as its value — a
queryable-based source carrying NO metaobject. It works end-to-end (designer column refill → runtime
data) through the family-blind seams above; three points are specific to it:

- **Queryable resolved by id, never cached.** The list's source queryable lives in its `Source`
  property's variant (`ibVariantDataDynamicSource`). The queryable is OWNED by the metaobject's
  source descriptor (`ibMetaSourceDescriptor`; the factory vends `&m_queryable`), so caching the raw
  pointer dangles the moment the source is deleted or the metadata image is rebuilt → use-after-free
  (a freed descriptor's vtable is garbage → `call 0`). The variant therefore stores the source's
  STABLE table id and RE-RESOLVES through `appData->GetQueryableFactory()->ResolveById` on every
  `GetQueryable()` — a deleted source resolves to null (so `IsEmptySource()` cleanly reports empty),
  never a stale pointer. Mirrors the property's own serialization, which already round-trips the
  source as its table id. (See `reference_queryable_no_cache_reresolve`; `m_columns`'s `ColInfo`
  cache is the same class of risk, still open.)
- **Type locked once a source is assigned.** `ibVariantDataSource::IsPropAllowed()` asks the same
  `WalkSource` dot (`return IsEmptySource()`): a resolvable source locks the Type selector read-only
  (`ibPGDataSourceProperty::RefreshChildren` → `SetFlagRecursively(ReadOnly, !IsPropAllowed())`); a
  deleted/unbound source frees it again. The Type follows the source. Using the dot (not a raw
  `m_sourceDesc.GetLeaf()` peek) is what lets a DELETED source unlock the Type instead of crashing.
- **TableBox column refill is family-blind.** `ibValueModelTableBox::OnPropertyChanged` refills its
  columns from the bound source's `GetSourceExplorer()` (the head attribute's `GetSourceValue()`),
  NOT a clsid → metaobject gate — so a queryable dynamic list yields its query columns just as a
  catalog yields its attributes (mirrors the form builder in `formObject.cpp`). The dotted display
  uses each column's `GetName()` (consistent with the head attribute and the metadata-field hops),
  e.g. `List.DeletionMark`.

## Source ownership (NB)

`SourceIncrRef`/`SourceDecrRef` are aliases for `ibValue::IncrRef`/`DecrRef` — ONE counter.
The source is owned by:

- the RAII `ibSourceDataObjectGuard` in `CreateAndBuildForm` (during the build), and
- the MAIN attribute wrapper (`SetSourceValue` → IncrRef, dtor → DecrRef) for the form's
  life.

The form holds **no** separate ref — an `IncrRef` in `InitializeForm` with no matching
DecrRef is a pure leak (and a no-op in the designer, where `srcObject` is null).

### Trap — never reffer a MEMBER ibValue cell with ownership

`ibValue`'s ctor sets `refCount = 0`; a reffer's dtor `DecrRef`s its target, which at 1→0
does `delete this`. A bind cell (`&m_value`) is a MEMBER ibValue (refCount 0). Capturing it
as an **owning** reffer (`operator=(ibValue*)` → `TYPE_REFFER` + IncrRef) makes its refcount
0→1, and the next release 1→0 → `delete` of a member (interior pointer) → heap corruption.

This bit the designer-keydown autocomplete: the precompile (`codeEditorInterpreter.cpp`)
captured `DataSource` = `&m_value` (extern/context loops) as an owning reffer, then deleted
it on `Clear`. Fix: those captures are now NON-owning (`static_cast<const ibValue*>` →
`TYPE_CONST_REFFER`, no IncrRef/DecrRef-delete) — read-only is enough for autocomplete, and
the LOCAL capture already snapshotted via `GetValue()` for the same reason.

## Designer — attribute tree

`ibVisualEditorAttributeTree` (`designer/.../visualEditorAttributeTree.cpp`): no toolbar;
a context menu (Add / Edit-rename / Delete / Copy / Paste / SetMain / Properties) with
icons; tree-item icon from the meta-attribute; name uniqueness enforced; double-click
activates the inspector; `RefreshEditor` on type-change / delete / set-main. Copy/paste are
STATIC methods (`ibFormAttributeValue::CopyToClipboard/PasteFromClipboard`) serialising the
attribute description through `ibBinaryProvider` (clipboard id `oes_clipboard_attribute`).

## Open edges

- **Multi-source** — today it is one main + auxiliary attributes; true N-sources and the
  main-switch semantics ("old main goes empty") are pragmatic, not yet a principled model.
- **table-dot** — dot-walk through references inside a tabular/queryable column (`List.Ref.Field`)
  is still partial. `WalkSource` resolves the first hop against the live source and reference
  hops config-wide, but a reference dot-walk THROUGH a dynamic-list (closed) column is not modelled
  — after the first queryable hop the walk leaves the source's columns, so a length-3 list path
  reads broken. Fine today (no such binding); needs a per-column reference descent when it lands.
- **Blocker B (compute server)** — binding is necessary but not sufficient; registers /
  reporting on top need the compute tier.
- **No test harness** — every form binds through this; the member-cell-reffer hazard above
  was statically detectable and should have been caught before runtime. A path-resolver
  harness is the cheapest insurance before building higher.

This is Blocker A of the ERP roadmap — the binding foundation under registers → reporting →
period-close → web / thin client.
