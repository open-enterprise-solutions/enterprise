# Form attribute binding — the form's typed source registry + path-through-gate

**Status:** in development (develop), experimental. First hardened slice landed —
exercised through the designer on the hard cases (copy/paste, delete-main, autocomplete,
custom objects) rather than a test harness. The **metadata-agnostic path resolver** landed on
top: the holder (`ibBackendFormAttributeValue`) materialises the value/source, the dot-walk
(`WalkSource`) returns a neutral `ibBackendSourceColumn`, and `ibSourceExplorer` is now
metadata-free (see the two sections below). On top of that: a bound path now SERIALIZES the way it
RESOLVES — as **raw ids the source explorer walks**, no metadata/guid (see *Path serialization*);
**drag-to-create** rides that same serializer (drop resolves the control class from the path); the
**value-table** rides the composite-attribute serialization as the dynamic list (and retypes cells
lazily on read); and `GetSourceDesc` was brought to the same mutable-ref-plus-setter shape as
`GetTypeDesc`. Uncommitted churn expected; the *model* below is stable, the surface is not yet spec'd or tested.

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
- A binding is a **hop vector** — `std::vector<ibSourceHop>`, each hop `{id, expected type}`
  (`sourceDescription.h`). `path[0]` selects a **form-local attribute** (the gate); the remainder
  dot-walks that attribute's value. The `id` is WHERE to step; the `expected type` rides ALONG so a
  composite-reference hop keeps the branch the picker pinned (undefined where none is imposed).
  `ibSourceDescription` wraps the vector and answers `GetHopCount()` / `GetFirst()` / `GetLeaf()` /
  `GetByIdx()` / `GetExpectedType(idx)` / `IsDotWalk()`.
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
  - **Façade methods** (the only outside surface — there is NO `GetAttribute()`): `GetName`
    / `GetId` / `IsMain` / `const ibTypeDescription& GetTypeDesc()` /
    `ibSourceDataObject* GetSourceValue()`, each forwarding to the private `m_attribute` or to the
    value. `GetBindValue()` returns `&m_value` (the live slot — `BindLocalVariable` stores the
    pointer); `SetSourceValue` seats the source into `m_value` (owned via the value's own refcount).
  - **Synonym is surfaced as "Caption".** The holder's inspector property for the human label is
    keyed/labelled `Caption` (`m_propertyCaption`) — but it is still read out through `GetSynonym()`,
    so the abstract-column trio (`GetName` / `GetSynonym` / `GetComment`) stays uniform with the
    metaclass and every neutral consumer keeps working. `GetComment()` is empty — the Comment
    property was removed from both the attribute and the value-table column (unused noise).
- **`ibBackendFormAttributeValue`** (`backend_type.h`) — the holder as a backend INTERFACE: exactly
  the façade above (`GetName` / `GetId` / `IsMain` / `GetTypeDesc` /
  `GetSourceValue`), no concrete description leaked. `GetSourceList` vends THESE (attribute + value
  together), so a backend resolver / the picker reads columns through `GetSourceValue()` —
  `GetSourceExplorer()` — without the concrete value type. (The former separate `ibBackendFormAttribute`
  description interface and `GetAttribute()` were removed: the holder answers for its hidden
  description directly.)

## The MAIN attribute

Exactly one attribute carries the Main flag — found by `IsMain()`, no separate
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
- **`SetMainAttribute` re-materialises, never just clears.** Flipping the sole-main flag
  (the designer toggle) drops the demoted holders' seated runtime source and then `Refresh()`es
  EVERY holder so its value rebuilds from its own Type. Clearing the value and stopping there
  (`SetSourceValue(nullptr)` alone) left the holder `TYPE_EMPTY` — the picker then asserted on a
  null source and column autofill produced nothing, and re-setting main never rebuilt the source
  (with no old main to inherit from, the new main cleared itself). `Refresh` keeps a
  correctly-typed seated source and rebuilds an emptied one, so the new main and the demoted ones
  are both correct; a source-typed holder is never left without its materialised source.

## Bind lifecycle

`BindAttributeVariable` binds the attribute name as a local (`<name>` / `ThisForm.<name>`)
and, for the main, the exported `DataSource`. `DropAttributeBinds` removes both before the
wrapper dies (left in place they dangle and the next compile reads freed memory — the
close-after-delete-main crash). `RemoveVariable` clears `m_listLocalValue` too.

Add/rename/delete go through `NextAttributeId` / `RegisterAttribute` / `WireAttribute`
(the last binds when the module is live and calls `InvalidateNames()` so the member surface
`ThisForm.<attr>` refreshes).

`IsWritableBinding`: only a DIRECT field is writable — `[attr]` or `[attr, field]`. A
reference dot-walk is read-only. It is ALSO the single **view-only** gate every editable
control asks: false when the form `IsViewOnly()` (explicit / rights) OR the binding's own
source metaobject denies `AccessRight_Modify` (per-source — a writable form can still carry a
read-only object). See [view-only.md](view-only.md).

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
control with no temporaries. **`GetSourceDesc` is the exact twin of `GetTypeDesc`**: a single
MUTABLE-ref getter that is both read AND write, plus the non-virtual setter family layered over
it — `SetDefaultSourceType(id)` / `SetDefaultSourceType(const ibSourceDescription&)` / `ClearSourceType()`
(mirroring `ibBackendTypeFactory::SetDefaultMetaType` / `ClearMetaType` over `GetTypeDesc`). One virtual
per control (the ref), no per-control setter; a caller binds a single hop or a whole DESCRIPTION through the
shared helpers. `SetBoundSource` (the drop) and `AutoBindNewSource` (auto-created control) both
seat the path this way.

**A binding path is passed as `ibSourceDescription` (the wrapper), not a bare `std::vector<ibSourceId>`.**
`ibSourceDescription` is to a source path what `ibTypeDescription` is to a composite type's clsid array — it
carries the whole address and the callers hand it straight through. Since resolution is source-explorer-driven
(below), the path-taking APIs take the wrapper: `WalkSource(const ibSourceDescription&)`,
`SetDefaultSourceType(const ibSourceDescription&)`, the designer drop chain (`DropBoundControl` /
`CreateBoundControl` / `ResolveDropControlClass` / `SetBoundSource`), and the form's binding API
(`GetValueByAttributePath` / `SetValueByAttributePath` / `IsWritableBinding`). The offset-walk primitives
(`ResolvePath` / `WalkColumns`, which walk a path SUFFIX from an index) and the RAM-hot raw getters
(`GetSourcePath`) take the bare `std::vector<ibSourceHop>` — the wrapper models a whole address, the
primitives step an offset of the same hops.

**One default-type map — `ibBackendTypeConfigFactory::GetDefaultTypeByFilter(filterKind)`.** The
default value type (clsid) for a filter kind — `_boolean` → Boolean, `_resource` → Number, `_table`
→ value-table, `_reference` / else → String — is a STATIC on the config factory, keyed on the kind.
It is the ONE mapping shared by `ibVariantDataAttribute::DoSetDefaultMetaType` (a fresh attribute's
Type) and `ibValueControl::AutoBindNewSource` (an auto-bound control's Type), so the two cannot drift.

## Path resolution — the metadata-agnostic dot-walk (`WalkSource`)

The dot-walk that turns a bound path into a leaf (its caption / type / validity) lives on the
factory: `ibBackendTypeSourceFactory::WalkSource(const ibSourceDescription&, valid, outText)` (`backend_type.cpp`). The
property layer just forwards to it — `ibVariantDataSource::GetSourceAttributeObject` /
`IsEmptySource` / `MakeString` are one-liners over `m_ownerProperty->WalkSource(...)`. **The
walker knows no metadata**: it steps ids and asks the source for columns; whether the data behind
a node is a metaobject attribute or a queryable column is hidden behind one neutral interface.

- **`ibBackendSourceColumn`** (`query/queryColumn.h`) — the neutral "column, like a DB column":
  `GetName` / `GetSynonym` / `GetTypeDesc` / `IsAllowed`. It is the **base of
  `ibBackendQueryColumn`** (which ADDS the DB-only `GetColumnId` / `GetPhysicalName` / `IsRawColumn`),
  so a metaobject attribute AND a dynamic list's queryable column are BOTH an `ibBackendSourceColumn`
  with no adapter. A value-table column (RAM, not a DB query) is a `ibBackendSourceColumn` **directly**
  — NOT a `ibBackendQueryColumn`: it pulls from memory, so the physical-name / raw-column DB semantics
  don't apply. `WalkSource` / `GetSourceAttributeObject` return THIS — the consumer (a column caption,
  a control's type) never sees the concrete class.
- **Gate 1** — `path[0]` is gated to a form attribute (`FindSourceHolder`, form-local, copy-safe).
- **Gate 2 — the live source.** The head holder's `GetSourceValue()` resolves each deeper hop by
  walking its `ibSourceExplorer` tree (`WalkColumns`, below) — metadata-blind: a dynamic list maps an
  id to a live queryable column, a record to its attribute, all behind one neutral node. A miss at any
  hop (`FindById` returns null) is a BROKEN binding. A reference field descends into the referenced
  type's columns IN PLACE — the node's own typed-empty value is a reference-as-source — so a dotted
  reference of ANY depth resolves through the same tree, with **no** config-wide `FindAnyObjectByFilter`
  re-scan (the former `GetSourceColumn` / `HasOwnColumns` open/closed split is gone).

`GetSelectMode` (hierarchical-catalog Items / Folders / FoldersAndItems) is a metaobject-attribute
concern, so the control resolves it by `dynamic_cast`-ing the walked leaf to
`ibValueMetaObjectAttributeBase`; a plain queryable column has no select mode → Items. Value
creation / type / metadata are the FACTORY's own (`CreateValueRef` / `GetDataType` /
`AdjustValue`), not the column's — the column carries only name / synonym / type.

## Source explorer — metadata-free column template

`ibSourceExplorer` (a public NESTED class of `ibSourceDataObject`, `srcDataObject.h` — the old
`srcExplorer.h` is gone) is the source's column/field TEMPLATE for form generation, the picker, and
the structure dot-walk. It is **metadata-free**: a node holds plain values + flags + the neutral
descriptor it was built from (`m_col`, an `ibBackendSourceColumn`); columns are appended via
`AppendColumn(col)` (deleted/disabled skipped through `col->IsAllowed()`; a queryable column IS-A
source column, so the node carries its real synonym), tabular sections via `AppendTable(...)`. No
`ibValueMetaObject*` member. Every `GetSourceExplorer()` builder (catalog / document / chart / record /
register / dynamic list) builds it from its own data. List-vs-object is answered by the source's OWN
explorer — `IsTableSource()` → `GetSourceExplorer()->IsTableSection()` (metadata-free). **Invariant:** every
table source (value-table, dynamic list, object list, register list) FLAGS its explorer ROOT as a table
section — `m_sourceExplorer.Reset(..., /*tableSection*/true)` — so it self-declares "I am a table"; a scalar
object / reference does not. A source that forgets this won't drag as a tablebox and its columns resolve as
standalone textboxes. A table can be EMPTY, so "has columns?" is not a reliable test — the flag is.

It is a **nullable-pointer API**, so a degenerate source signals "nothing to describe" instead of a
sentinel:

- `const ibSourceExplorer* GetSourceExplorer()` — the source's root template; **nullptr** when the
  source can't describe itself (an unresolved / empty reference with a null target metaobject).
- `const ibSourceExplorer* FindById(id)` / `const ibSourceExplorer* GetHelper(idx)` — per-node
  navigation; **nullptr** on miss / out-of-range (no shared empty-node sentinel any more).
- `void GetReferenceSources(std::vector<ibValue>&)` — the node's TYPED-EMPTY reference source(s): a
  reference column yields an empty **reference-as-source** PER target type (a composite reference => one
  each), built from the node's OWN type through the owner source's metaData (the node borrows it — it
  still stores no metaobject). Type-derived, not a live row read, so it resolves for a collection source
  (list / section) with no current row. The clsid→target resolution is the shared backend
  `ibSourceDataObject::ConvertToMetaIds` (the picker calls the same).

### `WalkColumns` — the structure-resolve hop

`WalkColumns(path, from, leaf&, outText)` is the design-time twin of the runtime `ResolvePath`
value-walk — one mechanism, differing only in typed-empty vs live value. It steps `path[from..]`
through the explorer tree:

- a **section** node (`GetHelperCount() > 0`) descends into its children in the SAME explorer
  (`explorer = node`) — a section is an `ibTabularObject`, not a source, so its value can't be hopped;
- a **reference** node descends into its target's columns: `node->GetReferenceSources()` materialises an
  empty reference-as-source per target type, and the walk picks the type whose explorer carries the next
  hop (`FindById(path[i+1])`) — so a COMPOSITE reference resolves to whichever branch owns the field, and
  a dotted reference of ANY depth resolves (`List.Ref.Sub…`); the chosen reference is parked so its
  explorer outlives the step;
- the leaf is `node->GetColumn()` — the neutral `ibBackendSourceColumn` the binding (caption / type)
  reads, pointing into the owning metaobject.

`WalkSource` (backend) gates `path[0]` to a holder, then delegates to `WalkColumns(path, 1, …)`.

### The runtime hop-walk — the gate, `ResolvePath`, and the table side

`WalkColumns` above is the DESIGN-TIME twin (typed-empty values over the explorer); at RUNTIME the same
hop vector fetches the LIVE value through ONE virtual gate, so the walk hops — it never steps via
`GetValueByMetaID`.

- **`GetValueBySourceHop(hop, out)`** — THE scalar gate on `ibSourceDataObject` (`srcDataObject.h`).
  Reference / record / list override it. The reference honours the PINNED type
  (`hop.m_type`): if the composite cell is undefined it hands back an empty typed TWIN of the pin
  (`ibValueReferenceDataObject::CoerceHopType`) — so a composite reference NEVER returns undefined and a
  hop can always step on. The twin / decode statics live ON the reference side, so `srcDataObject` itself
  stays metadata-free. `CoerceHopType(hop, out, filter, metaData)` takes the field's CURRENT type as a `filter`
  (`ibTypeDescription`) and validates the pin against it (`filter.ContainType(hop.m_type)`): a field RETYPED
  away from the pin (a value-table column changed in the designer) drops the stale pin instead of resolving a
  phantom twin; an EMPTY filter skips the check. The twin's target is decoded via `ConvertToMetaIds` (metadata,
  NOT a clsid body-mask). Every overriding gate is **out-of-line** and hands the field's live type: a
  record / manager via `GetMetaObject()->FindAnyAttributeObjectByFilter(id)->GetTypeDesc()`, a value-table via
  its column's `GetColumnType()`. A **value-table steps by TYPE, not value** — 0..N rows, no single cell, so its
  gate is JUST the twin (the record's `read-field || twin` degenerates to `twin` alone).
- **`ResolvePath(start, path, from, out)`** — the shared deep walk (static, `srcDataObject.cpp`). At each
  hop it converts the current value to `ibSourceDataObject` (a non-source value ends the walk — you cannot
  dot into a primitive) and calls its gate. `GetValueByPath` off a source feeds its own first hop, then
  delegates here.

**The table side — `ibTabularDataObject` (`tabularDataObject.h`).** A table value is PER-ROW, so its gate
carries the row: `GetValueBySourceHop(item, hop, out)`, a DIRECT translation `GetValueByMetaID(item,
hop.m_id, out)` on `ibValueModel` — a pure cell read, NO twin / reference checks. The walk lives in
`GetValueByPath(item, path, from, out)`: the table only STARTS the walk (the first hop off the row yields a
source cell) and TRANSFERS the deeper hops to the scalar `ibSourceDataObject::ResolvePath`. ONE hop vector,
the computation locus switches TABLE → SOURCE OBJECT. The tablebox renderer calls this single entry per
dotted cell (`ibValueModelTableBox::ResolveCellValue`), exactly as a control resolves an attribute path off
the form. `GetSourceMetaData` joins the tabular contract (`ibTabularObject`, by analogy with
`ibSourceObject`): a table WITH a meta object yields its config, one WITHOUT (a dynamic list) falls back to
the active config, so a reference cell still resolves its target. The gate being virtual is the extension
point — a table can override how a specific id resolves, and the walk still switches to the source object
unchanged.

### Path serialization is RAW — metadata-agnostic, mirroring the walk

Because a bound path RESOLVES purely by walking the source explorer (`FindById` per hop; each hop's
value yields the next explorer — see `ResolvePath` / `WalkColumns`), it also SERIALIZES that way:
`ibSourceDescriptionMemory` writes the path as **raw ids** and reads them verbatim — the explorer
resolves each on load. It consults **no metadata**: no `metaData` parameter, no metaId→guid mapping.
This is the point of the redesign — the walk never cares whether (or which) metadata backs a hop
(value-table columns are RAM-local ids, dynamic-list columns are query ids, catalog fields are config
metaIds — all just ids the explorer resolves), so the serializer must not reintroduce that coupling.
An earlier writer round-tripped the tail hops as copy-aware GUIDs (`GuidByMetaId` / `MetaIdByGuid`);
that dropped RAM-local ids (a value-table column has no config guid → the binding read back as
`<not selected>`) and is gone. A leading sentinel word marks the raw layout; a legacy guid-tail blob
recovers only its head (a whole-attribute binding) and re-save rewrites it raw. **Rule of thumb: a
binding path is a sequence of ids the source explorer walks — resolve AND serialize it through the
explorer, never through a metadata lookup.**

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
- **The picker's displayed type is PULL-ON-GET.** `CloneSourceAttribute()` self-refreshes through the
  source explorer (`RefreshTypeFromSource`) BEFORE cloning — the exact mirror of the Type side, where
  `ibVariantDataAttribute::GetTypeDesc` self-refreshes via `DoRefreshTypeDesc`. So `RefreshChildren`
  just clones (no separate `GetSourceTypeDesc()` priming step) and always shows the leaf's current type;
  a value-table column retyped in place keeps its leaf id, so the walk re-resolves it on the next get.
- **TableBox column refill is family-blind.** `ibValueModelTableBox::OnPropertyChanged` refills its
  columns from the bound source's `GetSourceExplorer()` (the head attribute's `GetSourceValue()`),
  NOT a clsid → metaobject gate — so a queryable dynamic list yields its query columns just as a
  catalog yields its attributes (mirrors the form builder in `formObject.cpp`). Columns are appended
  via `AppendColumn(col)`, so each node carries its descriptor (`m_col`) and the column's real SYNONYM;
  the header caption resolves through `GetControlTitle()` → `GetSourceAttributeObject()->GetSynonym()`,
  not the control name.

## Value-table as a form attribute

A form attribute whose Type is a value-table materialises an `ibValueModelTable` as its value — an
in-RAM (`ibValueModelStorage`) source, NOT a queryable/DB one. Like the dynamic list it works through
the family-blind seams above; the specific points:

- **Columns serialize IN the composite attribute, exactly like the dynamic list's Source/Settings —
  through the property mechanism, no bespoke format.** The holder's `WriteProperty` / `ReadProperty`
  (`ibFormAttributeValue`) already writes BOTH `m_attribute->WriteProperty(node)` (the amorphous Name /
  Type / id) AND `GetValueAsProperty()->WriteProperty(node)` (the materialised value's own props). The
  value-table IS an `ibPropertyObject`, so its column collection round-trips through THAT one call — the
  same unified path the dynamic list rides. Nothing hand-rolls a column blob.
- **Column-info is a variant-SSOT property object.** `ibValueModelTableColumnInfo` is
  `ibValueModelColumnInfo + ibPropertyObject + ibBackendTypeConfigFactory + ibBackendSourceColumn`. Its
  Name / Caption / Type are read THROUGH its properties (`m_propertyName` / `m_propertyCaption` /
  `m_propertyType`) — a single source of truth, no parallel member cache shadowing the properties (the
  "two federations" trap). Only the column id + width stay plain members. As an `ibBackendSourceColumn`
  it answers `GetName` (→ the Name property) / `GetSynonym` (→ Caption, falling back to Name) /
  `GetTypeDesc` (shared with the type factory) — so the value-table's columns walk and caption through
  the SAME neutral interface as a metaobject attribute or a query column.
- **The explorer carries the real descriptor.** `ibValueModelTable::GetSourceExplorer` appends each
  column via `AppendColumn(const ibBackendSourceColumn*, id)` (the source-column overload) — the node
  keeps the column-info as its `m_col`, so its synonym / type resolve live off the descriptor.
- **`GetGuid` is stable-by-construction** — generated once in the ctor (`wxNewUniqueGuid`, in-class
  initializer) and always returned, so a column's identity survives rebuilds and serialization.
- **A column Type change retypes cells LAZILY on read.** The column-info edits its Type in place (the
  property variant IS the storage), so existing row cells are NOT swept on the edit — its
  `OnPropertyChanged` is a no-op. Instead `GetValueByMetaID` / `GetValueByRow` coerce each cell to the
  column's CURRENT type on read (`ibValueTypeDescription::AdjustValue(GetColumnType(id), cell)`): a
  stale-typed cell reads back retyped, an absent one as the typed empty ("nothing there" reads cleanly).
  No eager row loop, no owner back-pointer — and there are no rows at design time anyway.
- **A retyped cell's variant must OWN the value.** `AdjustValue(...)` returns a TEMPORARY `ibValue` by
  value, so `GetValueByRow` wraps it with the OWNING `ibValueModel::ValueToVariant`
  (`ibVariantDataValueImpl<ibValue>`, a copy) — NOT the const-ref `ibVariantDataValueModel`. The ref
  variant is reserved for a value living IN the node (`GetTableValue()` returns `const ibValue&`);
  binding it to the retyped temporary dangled → access violation in `wxVariant::GetType()` on the next
  render (adding a row selects it and re-renders the fresh cell).
- **The current line syncs to the VISIBLE selection.** Programmatic selection — the top row
  auto-highlighted on form open, and the Select of a freshly-Added row — fires no
  `wxEVT_DATAVIEW_SELECTION_CHANGED`, so `m_tableCurrentLine` stayed null; a cell's "…" choice (which
  needs the current row to read the cell's real reference type) then fell back to a primitive type and
  silently no-op'd. Two seams close it: the `OnUpdated` seed-chain adopts the ctrl's actual active row
  (`GetSelections()`, else `GetTopItem()`) when no line is set, and `OnItemStartAdding` applies the
  just-added row as current (covering the first row added to an empty table).

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
icons; tree-item icon from the meta-attribute; name uniqueness enforced; `RefreshEditor` on
type-change / delete / set-main. Copy/paste are STATIC methods
(`ibFormAttributeValue::CopyToClipboard/PasteFromClipboard`) serialising the attribute
description through `ibBinaryProvider` (clipboard id `oes_clipboard_attribute`).

Selection is modelled exactly like the object tree — **by identity, not by name**:

- **`SelectInInspector(entry)`** is the single decode-and-select point (the mirror of the
  object tree's `SelectItemData`): raises the inspector and `SelectObject`s the HOLDER (façade).
  Every path funnels through it — the single-click reselect (a `wxEVT_LEFT_DOWN` that
  re-surfaces the ALREADY-selected item, since `OnSelChanged` only fires on a CHANGE),
  `OnSelChanged`, activate, and Properties. One click surfaces the attribute, like a control.
- **`SelectEntry(entry)`** lands the tree row on a holder by pointer identity
  (`GetEntryFromItem(id) == entry`) — the form owns the holders in `m_attributes`
  (ref-counted, stable across a rebuild), so the pointer is a valid key, the way the object
  tree keys `m_listItem` by `ibValueFrame*`. `RebuildTree` remembers the selection as the
  holder pointer and restores it after re-append; Add / Paste land the row on the NEW holder
  (its ref-counted pointer survives being handed to the undo command).
- **`RebuildTree`** wraps the tear-down in `Freeze`/`Thaw` (the tree is `SetDoubleBuffered`)
  and sets `m_rebuilding` to suppress `OnSelChanged` while items churn — a selection event
  fired mid-rebuild must not touch a stale/freed holder (the object tree guards the same way
  with `m_notifySelecting`).
- **Set-main is a toggle:** clicking the current main clears it
  (`SetMainAttribute(entry->IsMain() ? nullptr : entry)`) — no main → the control's
  own command bar comes back (below).

### Drag to create — a bound control from a tree node

Dragging a draggable tree node onto the form canvas creates a control already bound to that node.

- **The drag payload is the source PATH (raw ids), serialized through the engine's UNIFIED
  serializer — not bespoke byte-packing.** `OnBeginDrag` writes the node's `ibSourceDescription(path)`
  with `ibSourceDescriptionMemory::SaveData(writer, srcDesc)` into an `ibWriterMemory` buffer under the
  `oes_source_drag` data format; `ibFormEditorDropTarget::OnData` reads it back with
  `ibSourceDescriptionMemory::LoadData(reader, srcDesc)`. Metadata-agnostic (no `metaData` — the drop
  resolves each hop through the source explorer, see *Path serialization* above). **Canon: the drag
  carries the type/path; the DROP resolves the control class** — the payload holds no class name (the
  earlier hand-rolled `EncodeSourceDrag` / `DecodeSourceDrag` are gone).
- **Draggability gate = a non-zero control CLSID.** A node is draggable iff
  `DropControlClass(isTable, refTypes, typeDesc)` returns a non-zero `ibClassID` — the kind-typed
  control clsid the node maps to: table / section → `g_controlTableBoxCLSID`, boolean →
  `g_controlCheckboxCLSID`, scalar / reference → `g_controlTextCtrlCLSID`, a composite object
  container → `0` (drag its fields instead). The clsid is stored on the tree item purely as the
  draggable signal; it returns a **clsid, not a class-name string** (identity by clsid, per the
  project rule). The three control clsids are GLOBAL constants (`g_control*CLSID`, mirroring the
  existing `g_controlTableBoxCLSID` in `tableBox.h`; the widget pair lives in `widgets.h` and the
  control registrations key off the same constants).
- **The drop resolves the class from the path and records an undoable command.** `OnData` →
  `ibFormEditorDropTarget`'s host `DropBoundControl(x, y, desc)` → `CreateBoundControl(parentHint,
  desc)`. `ResolveDropControlClass(form, desc)` gates the head to the head attribute, walks the
  deeper hops through its source explorer, and picks Tablebox / Checkbox / Textctrl by the leaf's
  view + type — the SAME choice `BuildForm` makes per field. Creation runs through the command
  processor, so drag-to-create is undo/redo-able like any property edit. The whole chain passes the
  `ibSourceDescription` wrapper (the wrapper is born in `OnData` and seated verbatim by
  `SetDefaultSourceType(const ibSourceDescription&)`) — no decompose-to-vector and re-wrap round-trip.

## Command interface — a chrome LAYER over a control

A frame's toolbar/search/… are not children in the content sizer — they are **layers** stacked
ABOVE the content. The subsystem lives in `frontend/visualView/` (`layerObject.{h,cpp}`,
`canvasWindow.{h,cpp}`, `layers/commandBar.{h,cpp}`).

- **`ibValueLayerObject`** (`layerObject.h`, `FRONTEND_API`, abstract) — the common base a layer
  and a frame both present to the DESIGNER TREE, so one tree pointer drives both without casting
  per kind. `: ibValueDynamicMembers, ibPropertyObject`. Pure `GetOwnerFrame()` /
  `IsLayerContainer()`; virtual `IsTreeExpanded`/`SetTreeExpanded` (open-state kept on the object,
  restored across a rebuild); `PrepareDefaultMenu(wxMenu*)` / `ExecuteMenu(editor, id)` (the
  canonical menu hooks, named like `ibValueFrame`'s) — the object fills its own menu and runs the
  choice, the tree never branches per kind (see the object tree's `SelectItemData` / `OnRightClick`).
- **`ibValueCommandBar` / `ibValueCommandBarItem`** (`layers/commandBar.h`) — both
  `: ibValueLayerObject`. The bar carries an AutoFill flag + manual command items; an item mirrors
  the toolbar item (Name / Caption / Representation / Picture / Tooltip / Enabled / Visible +
  an `ibEventAction` Action). AutoFill entries use the real (small) action id; **manual items use a
  synthetic id `>= 32000`** — it must stay `< 32767` or the overflow dropdown's `wxMenuItem`
  asserts. `ExecuteCommand` is id-aware (synthetic → the item's own action). A tool click in the
  designer routes to the inspector (`FindItemByCommandId` → `SelectPropertyObject`), at runtime to
  `ExecuteCommand`. Copy/paste of commands goes through a static `ibDataNode` clipboard.
- **`ibCanvasWindow`** (`canvasWindow.h`, ex-`ibChromeWindow`) — `wxCompositeWindow<wxPanel>`; a
  vertical stack of `[layer parts][inner]`. Used by the COMPOSITE render path (`window.cpp`
  `CreateWithLayers` wraps the control's inner window). The FORM path builds the same layer parts
  straight into the host's main sizer (`visualHost.cpp` `CreateFormLayers`), tracked explicitly in
  `m_formLayerParts`. Both share the statics `ibValueWindowComposite::BuildLayerParts` /
  `UpdateLayerParts`, so the two renderers agree on one layer model.
- **Serialization** — the composite window writes a `"Layers"` block (first entry = `"CommandBar"`,
  extensible), read back symmetrically; the command bar round-trips through the layer object's
  `WriteData`/`ReadData`.
- **No duplicate bar on the main.** A control whose WHOLE source IS the form's MAIN attribute
  (`IsMainSourceBound()` — a single-hop path whose head holder `IsMain()`) suppresses its own command bar
  (`ibValueModelTableBox::HasCommandBar()` returns false when `IsMainSourceBound()`) — the form already
  carries the command interface for the main source, so the tablebox would otherwise render a second,
  redundant bar. A NESTED source (a tabular section — path `[mainAttr, section]`) keeps its own bar: the
  main attribute is only its HEAD, not its own source. Gating on `!path.empty()` (head-only) instead of
  the single-hop check was the bug that hid a tabular section's command panel.

### Command provider — the model stores, the view adapts, the form wraps

The toolbar's commands flow through ONE interface — `ibStandardCommandSource` (`GetStandardCommands` +
`CallAsAction`, `standardCommand.h`) — and `ibValueFrame` IS-A one, so every control and the form itself
already carry the pair. Three roles compose it:

- **The model is a dumb command STORE** — `ibStandardCommandTabular` (`GetCommandCollection(formType, out)` +
  `CallAsCommand(id, {selection, anchor}, form)`, `standardCommand.h`). It lists its OWN narrow set as `ibCommandItem`
  records (the SAME record the action collection lays out — a default item, `m_act_id == wxNOT_FOUND`, is
  a separator) and runs one by id against the FRONT-passed rows (delete / edit use the selection; a CREATE
  parents a new element under the anchor). No action composition, no widget
  pull. There is NO shared base command enum — each model class defines its OWN commands (a value-table /
  tabular section its Add / Copy / Edit / Delete inline; the list family a file-local enum in
  `commonObjectAction.cpp`, adding MarkAsDelete / AddFolder; an enum list nothing).
- **The tablebox is the ADAPTER** — it turns the dumb model into a full `ibStandardCommandSource`.
  `GetStandardCommands` composes Select (choice mode, first) + the model's `GetCommandCollection` + the
  view-state band (Filter / FilterByColumn / FilterClear / ViewMode — the TableBox's own high-base ids,
  `20000+`). `CallAsAction` reads the command rows off the live control — the selection (`m_tableCurrentLine`) plus the create anchor (per view mode: hierarchy = the drilled-into folder, tree = the folder the cursor stands in, list = none) — and
  routes: a band id → `Command_*` (driven straight on the widget); any other id →
  `model->CallAsCommand(id, {selection, anchor}, form)` (the object commands). The model never sees the widget; the view
  never composes what the model owns.
- **The form is a WRAPPER** — `GetStandardCommands` surfaces its command PROVIDER's set + the form chrome
  (Close / Update / Help / Change); `CallAsAction` handles the chrome ids, else forwards to the provider.
  `GetCommandProvider()` (`formAction.cpp`) resolves it: a **list form** — the control that reports
  `IsMainSourceBound()`, found by `FindMainCommandView` (a recursive walk asking the BASE
  `ibValueFrame::IsMainSourceBound` virtual — `false` by default, the tablebox overrides it, so no
  per-type cast); an **object form** — the source object itself
  (`dynamic_cast<ibStandardCommandSource*>(GetSourceObject())`: a catalog element / document IS both the data
  source and its own command interface). The form never touches the dumb `ibStandardCommandTabular`.

**Edit = a front-intercepted command (a flag bit baked into the id).** Inline editing is a pure FRONT
operation, so the Edit command is not round-tripped through the backend to call back. A model bakes a flag
bit into its Edit id in the enum — `constexpr ibActionID eStartEditingFlag = 0x1000` (`standardCommand.h`), so
`eEditValue = <n> | eStartEditingFlag`; for the MODEL it is just an ordinary command value (it emits
`eEditValue` and dispatches `case eEditValue` unaware of the flag). The tablebox's `CallAsAction` tests the bit
on EVERY id it forwards to `CallAsCommand` AS-IS (the model's Edit case carries the flag too — a list opens its
object form, a value-table does nothing there), and — if the bit is set — ALSO runs `EditCurrentRow(row)`, the
inline cell editor on the row's first editable column. So a value-table / tabular row edits inline (front), a
list opens its form (backend), nothing else changes. Tested per id (`id & FLAG`), no lookup table. The id (flag
included) MUST stay a valid **wxMenuItem** id (`< 32767`): the tablebox appends its command ids STRAIGHT as
context-menu item ids (`OnContextMenu`), so a high flag bit trips wx's `itemid < 32767` assert. `0x1000` is a
FREE bit no real band sets — model ids `1..27`, form chrome `10000..10003`, band ids `20000..20004` all leave
bit 12 clear — so `eEditValue = 3 | 0x1000 = 4099` stays valid. (Rejected en route: `0x40000000` — a "high bit
above every range" that broke the context menu; a reserved specific id `-100000` and a flag OR'd only at emit + stripped on the
front — both superseded by baking the flag into the enum value; a notifier `StartEditing` push; a front-only
`enTableEdit` command that broke the model's command order; and an `ibBackendTableFrame` callback that looped
front → back → front for a pure-front action.)

**Select value (a picker's return).** `ibValueModelReturnLine::GetSelectValue()` (Command_Choose →
`NotifyChoice`) redirects ONCE to the owner model — `GetOwnerModel()->GetItemSelectValue(m_lineItem)` —
and falls back to the row's own value when empty. `ibValueModel::GetItemSelectValue` defaults to empty (a
value-table / tabular / dynamic-list row selects itself — the current row); a concrete leaf overrides it —
a catalog / document / folder returns its REFERENCE (read off the row by the metaobject's data-reference
metaID), a register returns its record KEY (`CreateRecordKeyObjectValue`).

**Resolved LIVE, never cached.** `GetCommandProvider` re-derives every query. A stored back-pointer
(prototyped both ways — a push-attach from the main view, and an attribute-side subscriber list) goes
stale the moment the designer re-points the MAIN attribute (table-1 → table-2 fires no re-bind on the
demoted view) or the bound type is deleted externally (the link dangles). The walk keys on the
authoritative `IsMainSourceBound` — which re-reads `holder->IsMain()` live — so the next walk simply finds
the new main (or nothing); nothing to keep in sync. It is cheap (a few controls, only on toolbar
(re)build / command click), so the walk beats the state. An "invert ownership" variant (the main
composite owns the top bar and pulls the form chrome UP via `GetOwnerForm`) was prototyped and rejected —
it relocates WHERE the bar renders for no correctness gain.

**Commit the edited field before the command runs.** A text control pushes its typed value to the source
only on kill-focus / Enter (`ibValueTextCtrl::OnKillFocus`, `m_textModified`). A custom-drawn tool does not
steal focus on click, so the chrome command bar (`ibValueCommandBar`, an `ibAuiToolBar`) must `SetFocus`
itself before dispatching — mirroring the metaobject toolbar's `ibValueToolbar::OnTool` — so the field being
edited fires its kill-focus and commits FIRST. Without it a Write ran against a stale source (a register
record wrote an empty key and the manager false-positived "This entry already exists" — succeeded only on the
second click, once the error dialog had stolen focus). Fix: one `bar->SetFocus()` before `ExecuteCommand`,
reusing the existing commit path — no parallel "flush every control" mechanism.

## Designer: live column edits & drag-to-column

### Live-refresh a value-table column edit (property-object child-change notify)

A value-table's `ibValueModelTableColumnInfo` is a BACKEND property object edited through the generic
object inspector; it cannot reach the frontend designer to re-render when its Caption / Name / Type changes
(two-DLL). Rather than an ad-hoc inspector hook, the property-object graph gained an UPWARD edge:

- `ibPropertyObject::m_attachOwner` — "who attached me", DISTINCT from the tree `m_parent` on
  `ibPropertyObjectHelper` (that is the children hierarchy). Set by `AttachPropertyObject` (the accumulator
  owns the attached) and structurally by a value-table on its column-infos (`AddColumn`). Severed both ways in
  the destructor (`DetachAllPropertyObjects` + `RemoveAttachedObject`), so no back-link dangles.
- `virtual void OnChildChanged()` — default bubbles up the attach-owner chain; a frontend holder
  (`ibFormAttributeValue`) overrides it to `RefreshEditor`. It carries NO payload — a refresh signal, not the
  property event: reusing `OnPropertyChanged` would make the holder re-forward to the property's owner and fire
  the child's handler twice. Mirrors `wxPGProperty::ChildChanged`.
- The inspector (`ModifyProperty` AND `ModifyEvent`) calls `GetAttachOwner()->OnChildChanged()` after an edit —
  from the OWNER, not self, so the edited object's handler isn't re-run. Chain:
  column-info → value-table → holder → refresh. Controls / the holder itself have no attach-owner → no-op.

The attribute tree shows a column node by its NAME (its identity), not its Caption/synonym — the control's
header takes the synonym, the tree stays name-keyed so a column is findable by name.

### Drag a source column / attribute onto a tablebox → append a bound column

Dropping a node dragged from the attribute tree onto a **tablebox** appends a bound view column instead of
creating a standalone control (`CreateBoundControl`): climb `parentHint` to the nearest tablebox (by clsid). A
field UNDER the table's own bound source (its path a strict prefix of the field's) becomes a ROW column; a
single-value source from elsewhere (a scalar / reference attribute) becomes a FOREIGN / object column (Mode 2,
one value per row). Column source = the dragged absolute path — the same the renderer resolves. Enabling drag:
`AppendComposition` no longer zeroes a list/section field's drop-class, so its columns are draggable;
`ResolveDropControlClass` DECLINES a column whose container is a list, so an off-table drop makes nothing
rather than a broken `<not selected>` scalar.

**Drop over the grid.** A tablebox is a composite — `ibCanvasWindow` (a `wxCompositeWindow<wxPanel>`) stacks a
toolbar layer over the inner grid. The native grid swallows the OS drop, so the form-canvas drop target only
caught the toolbar strip. `ibCanvasWindow` OVERRIDES `SetDropTarget` to FORWARD it to its inner content window
— as `wxCompositeWindow` forwards colour / font / focus to its parts — so the designer sets ONE target on the
composite and it propagates (a `wxDropTarget` is single-owner, so it goes to the content child, the window a
body drop hits). The drop handler DEFERS the append via `CallAfter`: it runs on the tablebox's own grid, and
adding a column rebuilds / re-wires that grid — doing it inside the OS drop callback frees the very target
mid-call (a use-after-free crash). The append refreshes only the object tree — a column provisions no new form
attribute, so the attribute tree must not rebuild and lose the user's expansion / selection mid-drag.

## Open edges

- **Multi-source** — today it is one main + auxiliary attributes; true N-sources and the
  main-switch semantics ("old main goes empty") are pragmatic, not yet a principled model.
- **table-dot** — LANDED (including composite). A dotted reference of any depth (`List.Ref.Field`,
  `Object.Section.Ref.Field`) resolves through `WalkColumns`: each reference hop materialises the node's
  typed-empty reference-as-source per target type (`GetReferenceSources`) and descends into whichever
  branch owns the next field — so a COMPOSITE reference (a field of any target type) resolves too. The
  picker's nested-section column source resolves likewise — the bound table is walked down `parentPath`
  to the section node and its columns rooted directly (`ProcessTableColumn`). The clsid→target resolution
  is one shared backend helper (`ConvertToMetaIds`), used by both the walk and the picker. A dot-walk INTO a
  **value-table** column resolves too — its scalar gate steps by TYPE (the pinned twin), since a RAM table has
  no single row value at design time (see the value-table section). Composite enumeration is **REFERENCE-only**
  (`ConvertToMetaIds` filters `_Reference`): an OBJECT branch in a composite (e.g. `DocumentObject.X` alongside
  `CatalogRef.Y`) is intentionally NOT dot-walked — only the reference branches expand. This is by design, not
  a bug (no demand for object-branch dotting; references cover the case). Confirmed with a live composite column.
- **Value-table design-time dot-walk — LANDED (steps by TYPE).** A value-table has 0..N rows, so the
  DESIGN-TIME walk (`WalkColumns`, which has no row) cannot step by VALUE. Its scalar gate
  `GetValueBySourceHop` returns just the pinned branch's empty typed twin via
  `ibValueReferenceDataObject::CoerceHopType` — the reference's OWN static, so the table asks the reference to
  build itself, never fabricating reference logic on the table side. The record gate is `read-field || twin`;
  a value-table never has a readable single field, so it is JUST the twin. Row count is irrelevant — the twin
  is type-only. (Earlier this read `<not selected>` because `GetSourceMetaData` was null; it now yields the
  active config.) **Stale-pin — HANDLED:** the gate passes `CoerceHopType` the column's CURRENT type as a
  `filter` (`GetColumnByID(hop.m_id)->GetColumnType()`), and the reference validates the pin against it
  (`filter.ContainType(hop.m_type)`). A retyped column no longer lists the pin, so the dead path reads back
  `<not selected>` instead of a phantom twin. The check lives in `CoerceHopType` (the reference side), not the
  table — the table only supplies its column's type. A metadata-fixed field (record / manager / reference gate)
  hands ITS field's live type the same way (`FindAnyAttributeObjectByFilter(id)->GetTypeDesc()`); an EMPTY
  filter skips the check.
- **Blocker B (compute server)** — binding is necessary but not sufficient; registers /
  reporting on top need the compute tier.
- **Test harness — first slice landed.** `test_sourceDescription.cpp` (the hop-vector passport +
  metadata-free serialize round-trip), `test_tabularHop.cpp` (the table `GetValueByPath` walk via a mock
  table), and `test_sourceExplorer.cpp` (design-time `WalkColumns`) are gtest / CMake (`oes_tests`). Still
  uncovered: the full RUNTIME reference chain (table → reference → field), which needs a value-wrapped
  source, and the member-cell-reffer hazard above (statically detectable).

This is Blocker A of the ERP roadmap — the binding foundation under registers → reporting →
period-close → web / thin client.
