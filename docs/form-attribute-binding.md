# Form attribute binding — the form's typed source registry + path-through-gate

**Status:** in development (develop), experimental. First hardened slice landed —
exercised through the designer on the hard cases (copy/paste, delete-main, autocomplete,
custom objects) rather than a test harness. Uncommitted churn expected; the *model* below
is stable, the surface is not yet spec'd or tested.

## What it replaces

A form used to hold a single hard-wired data object (`m_sourceObject`): one form, one
source, controls reaching into it directly. That model didn't generalise to multiple
sources, custom objects, or designer-time type resolution.

Now a form owns a **registry of typed attributes**, and a control binds by a **PATH** that
is resolved through the form's attribute **gate**. The form no longer knows about
`ibSourceDataObject` for binding — it knows attribute ids and paths.

## The model

- The form owns `std::vector<std::unique_ptr<ibFormAttributeValue>> m_attributes`
  (`form.h`). `unique_ptr` keeps a returned entry pointer valid across vector growth.
- A binding is a `std::vector<ibSourceId>` (a metaId path). `path[0]` selects a
  **form-local attribute** (the gate); the remainder dot-walks that attribute's value.
- One resolver pair on the form — `GetValueByAttributePath` / `SetValueByAttributePath`
  (`formAttribute.cpp`) — does `FindAttributeById(path[0])` then delegates the tail to the
  wrapper's `GetValueByPath` / `SetValueByPath`. Controls (tablebox, column, textctrl,
  checkbox, …) all read/write through this one path API; nothing binds to a raw source.

## Classes

- **`ibValueFormAttribute`** (`formAttribute.h`) — pure DESCRIPTION: Name / Type / id /
  Main flag. `: ibValueDynamicMembers, ibPropertyObject, ibBackendFormAttribute`
  (ibValue first → offset 0, see `reference_ibvalue_first_base_pmf`). Registered via
  `SYSTEM_TYPE_REGISTER(ibValueFormAttribute, "FormAttribute")`. Accepts ANY type
  (`GetFilterDataType() == ibSelectorDataType_any`) — primitive, reference, list, object.
- **`ibFormAttributeValue`** (`formAttribute.h`) — the registry entry. Plain class (NOT an
  ibValue), non-copyable. Three-way split:
  - `ibValuePtr<ibValueFormAttribute> m_attribute` — owns the description.
  - `ibValue m_value` — the bind cell (the script variable's value; reassignable).
  - `ibSourceDataObject* m_sourceData` — the source, held via `SourceIncrRef` SEPARATELY
    from `m_value` so it survives a script reassignment of the bind cell.

  `GetBindValue()` returns `&m_value` (the live slot — `BindLocalVariable` stores the
  pointer). `SetSourceValue` does `SourceIncrRef` on the new source before releasing the
  old (a main-switch never drops the object to zero between), then seats it into `m_value`.

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

- `DoSetFromMetaId(id)` — resolves a form-attribute id to its type by walking the owner's
  `GetSourceList(attrs)`; the matching attribute's `GetTypeDesc()` is the answer.
- `DoRefreshTypeDesc()` — validates tabular sections against the GATE metaobject, obtained
  from the binding's start attribute via `owner->GetSourceDesc()` →
  `ResolveGateMeta(...)`, NOT the runtime `srcObject` (null at designer, blind to custom
  objects). This is what makes type resolution correct at design time, on custom objects,
  and during copy/delete cleanup of removed tabular sections.

All source controls override `ibBackendTypeSourceFactory::GetSourceDesc()` (returning
`m_propertySource->GetValueAsSourceDesc()`), so the gate descriptor comes straight from the
control with no temporaries.

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
- **table-dot** — dot-walk through references inside a tabular column (`List.Ref.Field`) is
  only partial; the path model allows it, the resolver lags.
- **Blocker B (compute server)** — binding is necessary but not sufficient; registers /
  reporting on top need the compute tier.
- **No test harness** — every form binds through this; the member-cell-reffer hazard above
  was statically detectable and should have been caught before runtime. A path-resolver
  harness is the cheapest insurance before building higher.

This is Blocker A of the ERP roadmap — the binding foundation under registers → reporting →
period-close → web / thin client.
