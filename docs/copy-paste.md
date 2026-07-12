# Copy / paste — the copy-guid mechanism

**One idea, do not reinvent it.** Copy/paste in this engine is solved by ONE mechanism: a metaobject
carries a **copy-guid** while it is being copied; a reference to it serialises as that guid; on paste the
guid resolves to the copy's freshly-assigned id. Metaobjects, their references, forms, and form controls
all ride this SAME mechanism. There is no DB round-trip and no per-field remap table.

The key structural fact: **copy/paste is a SECOND serialization path, distinct from the normal
read/write path.** The stored copy-paste binary is understood only by the paste reader; a normal reader
sees a plain binary. What tells the two apart is the **mark** (see below).

---

## The mechanism

Every metaobject has two mutable guid slots (`metaCollection/metaObject.h`): `m_metaCopyGuid` /
`m_metaPasteGuid`.

- `GetCommonGuid()` = pasteGuid → copyGuid → metaGuid (the first that is set).
- `IsCopyMode()` = `m_metaCopyGuid.isValid()`, `IsPasteMode()` = `m_metaPasteGuid.isValid()`.
- `SetCopyGuid(g)` / `SetPasteGuid(g)` set the respective slot (both `const` — the slots are `mutable`).

The pasted object is assigned a NEW `m_metaGuid` **equal to the source's copy-guid** (`PasteObject` reads
the copy-guid from the blob header into `m_metaGuid`). So a reference written as the source's copy-guid,
on paste, resolves back to that object under its new id — and resolves ONLY to it, because the original's
copy-guid is erased once the copy is taken (no ambiguity: `GetIdByGuid(copy-guid)` lands on the paste).

### The two marks — copy-guard and paste-guard (by analogy)

Both marks are RAII, applied recursively across an object + its children (`metaObject.cpp`):

- **COPY** — `ibControlCopyGuard` stamps a fresh unique copy-guid on the tree at the start of `CopyObject`
  and erases it in the dtor. So while a copy is in flight the copied objects are `IsCopyMode()`.
- **PASTE** — `PasteObject` stamps `m_metaPasteGuid = m_metaGuid` per object as it is created, and the
  public entry erases the whole tree's paste marks on exit (mirror of the copy guard). So while a paste
  tree is being built + run, the pasted objects are `IsPasteMode()`.

The serializer routes on these marks; nothing else is threaded.

### The two property hooks (default = raw)

Serialisation of a property goes through `ReadNodeValue` / `WriteNodeValue` — the raw, on-disk form.
Copy/paste adds two parallel hooks on `ibBackendProperty` (`propertyManager/propertyObject.{h,cpp}`):

```cpp
virtual bool CopyNodeValue (ibDataValue& value) const { return WriteNodeValue(value); }  // DEFAULT = raw
virtual bool PasteNodeValue(const ibDataValue& value) { return ReadNodeValue(value); }   // DEFAULT = raw
```

**The default is the raw write** — a plain property copies as a plain save. Only a property that holds a
metaobject reference overrides them to ride the guid. The owner's copy/paste loop
(`ibPropertyObject::CopyProperty` / `PasteProperty`) walks every property + event calling
`CopyNodeValue` / `PasteNodeValue` — same shape as `Read/WriteData`, one level up.

---

## Source description — the form control binding

A control's binding is a hop path (`sourceDescription.{h,cpp}`): head = a form-local attribute id, deeper
hops = source-column (metaobject attribute) ids. Two serialisations, split cleanly:

- **`ibSourceDescriptionMemory`** is a DUMB serializer — writes/reads the id path verbatim, no metadata,
  no guids. This is the normal on-disk form, used by `ibPropertySource::Read/WriteNodeValue`.
- **`ibPropertySource::Copy/PasteNodeValue`** is the guid path (its OWN binary). Each hop that resolves to
  a metaobject rides its GUID (`ibVariantDataSource::GetGuidByID` == the metaobject's `GetCommonGuid`, which
  auto-picks the copy-guid while it is marked); the head (form-local, never a metaobject) stays a raw id.
  On paste, `GetIdByGuid` maps each guid to THIS config's live id — the pasted object's new id.

So the deep dot-walk hop (`Object.Reference.Field`) that used to break on copy now re-homes: its metaobject
copy-guid is written on copy, resolved to the copy on paste. This is the residual-risk fix — done in the
right layer (the source property's copy hook), NOT a parallel format.

---

## Forms and controls — two paths, routed by the mark

The control tree has the SAME dual structure as a metaobject:

- Normal load/save: `ibValueFrame::LoadNode` / `SaveNode` (`visualView/ctrl/frame.cpp`) → per-control
  `ReadData` / `WriteData` (raw).
- Copy/paste: `ibValueFrame::PasteNode` / `CopyNode` → every property + event through
  `Paste/CopyNodeValue` (a source property re-homes its hops; a plain control round-trips unchanged since
  the pair defaults to Read/Write).

`SaveControl` / `LoadControl` route once, on the form metaobject's mark:

```
SaveControl → metaForm->IsCopyMode()  ? CopyNode  : SaveNode
LoadControl → metaForm->IsPasteMode() ? PasteNode : LoadNode
```

The in-designer control clipboard copy (`ibValueForm::CopyObject` → `ibValueFrame::CopyObject/PasteObject`)
already serialises through `CopyProperty` / `PasteProperty` → `Copy/PasteNodeValue` — consistent, by
analogy with the metaobject.

### The lazy object-form catch (the hard part)

An OBJECT form (catalog/document/…) materialises **lazily**: `OnAfterRunMetaObject` registers an
`ibDeferredForm` (`metaCollection/metaFormObject.h`) in the compile-value cache; the form is built on first
`FindCompileModule` lookup — long after `PasteObject` cleared the paste mark. So at build time
`IsPasteMode` is already false, and a naive load would read the copy-paste binary as raw.

`ibDeferredForm` bridges the gap:

- Its **constructor RECORDS** the paste flag (`form->IsPasteMode()`) at registration, while the mark is
  still live.
- `Construct` (first access) **re-arms** the same guid (`SetPasteGuid(GetGuid())`) so the build's
  `LoadControl` routes to `PasteNode`; the re-homing runtime form reads the paste binary from the stream;
  then it **disarms** (`SetPasteGuid(wxNullGuid)`) once created. One-shot, universal — the deferred builder
  owns it, not any single form kind.

```cpp
ibValue* ibDeferredForm::Construct() const {
    if (m_paste) m_form->SetPasteGuid(m_form->GetGuid());          // re-arm → deferred read goes to PasteNode
    ibValue* result = ...->CreateObjectForm(m_form);              // create + read (PasteNode) + run (module init)
    if (m_paste) m_form->SetPasteGuid(wxNullGuid);               // disarm after created — consumed
    return result;
}
```

The paste flag is INSURANCE: it says "the binary on input is a paste blob, not a normal one" — so
`PasteNode` processes it (re-home); once consumed, the form works with a standard binary.

`ibDeferredForm` lives in `metaFormObject.h` (not `metaData.h`): the form type is complete there, so the
constructor reads `IsPasteMode` inline.

---

## Dynamic list — copy resolves through the PER-CONFIG factory

A dynamic list's source is a queryable resolved through a **queryable factory** that is **per config** — it
lives in the metadata (`ibMetaData::GetSourceFactory`, on the `ibMetaImage` snapshot), NOT a global
singleton. So a copied list resolves its source through the COPY's config and gets the copy's descriptor,
not the original's (the old global singleton keyed by `(namespace, name)` handed back the original — that
was the `<not selected>` copy bug). The list's source now simply follows the main attribute's TYPE
(`list_to_clsid(metaID)`), which the standard metaobject copy already remaps.

Register sources ALWAYS: a metaobject registers its source into its config via
`m_metaData->RegisterSource(&m_queryable)` in `OnAfterRunMetaObject`, **not** gated by `onlyLoadFlag`. The
gate was a leftover from the global-singleton era; with a per-config factory EVERY config must register its
OWN sources (including a read-only DB load), or its forms can't resolve their sources and object forms
crash on open.

---

## Files

| Concern | Where |
|---|---|
| Copy/paste guid slots + guards | `metaCollection/metaObject.{h,cpp}` |
| Copy/paste property hooks (default = raw) | `propertyManager/propertyObject.{h,cpp}` |
| Source binding — dumb raw serializer | `sourceDescription.{h,cpp}` |
| Source binding — guid copy/paste hooks | `propertyManager/property/propertySource.cpp`, `.../variant/variantSource.cpp` |
| Form/control raw vs copy/paste node paths | `visualView/ctrl/frame.{h,cpp}`, `formMem.cpp` |
| Lazy object-form deferred paste flag | `metaCollection/metaFormObject.{h,cpp}` (`ibDeferredForm`) |
| Dynamic-list read/select split | `system/value/valueDynamicList.{h,cpp}` |
| Per-config queryable factory | `metaData.{h,cpp}`, `query/queryableFactory.{h,cpp}` |
