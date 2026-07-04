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
register / dynamic list) builds it from its own data. List-vs-object is a TYPE fact answered by the
class factory — `IsTableSource()` → `ibCtorAbstractType::IsTableValue()` by CLSID (no `dynamic_cast`,
works before a source is even picked).

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
  `ibSourceDataObject::GetReferenceTargets` (the picker calls the same).

### `WalkColumns` — the structure-resolve hop

`WalkColumns(path, from, leaf&, outText)` is the design-time twin of the runtime `ContinueHops`
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
  catalog yields its attributes (mirrors the form builder in `formObject.cpp`). Columns are appended
  via `AppendColumn(col)`, so each node carries its descriptor (`m_col`) and the column's real SYNONYM;
  the header caption resolves through `GetControlTitle()` → `GetSourceAttributeObject()->GetSynonym()`,
  not the control name.

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
  (`SetMainAttribute(entry->IsMainAttribute() ? nullptr : entry)`) — no main → the control's
  own command bar comes back (below).

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
- **No duplicate bar on the main.** A control whose WHOLE source IS the form's MAIN attribute (a
  single-hop path) suppresses its own command bar (`ibValueModelTableBox::HasCommandBar()` returns
  false when `path.size() == 1 && FindSourceHolder(path.front())->IsMainAttribute()`) — the form
  already carries the command interface for the main source, so the tablebox would otherwise render a
  second, redundant bar. A NESTED source (a tabular section — path `[mainAttr, section]`) keeps its own
  bar: the main attribute is only its HEAD, not its own source. Gating on `!path.empty()` (head-only)
  instead of `size() == 1` was the bug that hid a tabular section's command panel.

## Open edges

- **Multi-source** — today it is one main + auxiliary attributes; true N-sources and the
  main-switch semantics ("old main goes empty") are pragmatic, not yet a principled model.
- **table-dot** — LANDED (including composite). A dotted reference of any depth (`List.Ref.Field`,
  `Object.Section.Ref.Field`) resolves through `WalkColumns`: each reference hop materialises the node's
  typed-empty reference-as-source per target type (`GetReferenceSources`) and descends into whichever
  branch owns the next field — so a COMPOSITE reference (a field of any target type) resolves too. The
  picker's nested-section column source resolves likewise — the bound table is walked down `parentPath`
  to the section node and its columns rooted directly (`ProcessTableColumn`). The clsid→target resolution
  is one shared backend helper (`GetReferenceTargets`), used by both the walk and the picker.
- **Blocker B (compute server)** — binding is necessary but not sufficient; registers /
  reporting on top need the compute tier.
- **No test harness** — every form binds through this; the member-cell-reffer hazard above
  was statically detectable and should have been caught before runtime. A path-resolver
  harness is the cheapest insurance before building higher.

This is Blocker A of the ERP roadmap — the binding foundation under registers → reporting →
period-close → web / thin client.
