# Copy / paste — the copy-guid mechanism

**One idea, do not reinvent it.** Copy/paste in this engine is solved by ONE mechanism: a metaobject
carries a **copy-guid** while it is being copied; a reference to it serialises as that guid; on paste the
guid resolves to the copy's freshly-assigned id. Metaobjects, their references, forms, and form controls
all ride this SAME mechanism. There is no DB round-trip and no per-field remap table.

The key structural fact: **copy/paste is a SECOND serialization path, distinct from the normal
read/write path.** The stored copy-paste binary is understood only by the paste reader; a normal reader
sees a plain binary. What tells the two apart: the live **paste mark** on the synchronous path, and — for a
blob that outlives the mark (a lazily-built form) — a **self-describing tag stamped into the blob at copy
time** (see "Forms and controls" below).

---

## Paste is a MERGE BY NAME, not an exact-match restore

The usual shape of copy/paste — the payload names its class, the target is built FROM the payload,
kinds must agree — is deliberately **not** what this engine does, and the difference is structural
rather than a matter of taste.

**Why a name-keyed transfer is the only one available.** A metaobject's serialised BYTES are its own:
its layout, its order, its size, decided by what that metatype happens to store. Between two
different metaobjects there is nothing to match at that level — not "little in common", but no shared
frame of reference at all. The **property system is the one standardized surface** every metaobject
speaks in the same shape: a named value, asked and answered the same way regardless of metatype. So
the copy travels as an `ibDataNode` tree of name→value (`CopyProperty`: *no per-property byte writer
— each property yields its node value*), never as the `SaveMeta` blob.

And the target does not RECEIVE a set of properties — it **reuses its own**. It walks the properties
it already has and asks the payload for each by name; nothing foreign enters it, no layout, no extra
fields. That is what makes the whole thing work, and the merge semantics below are its consequence.

An exact-match paste makes the clipboard a **second constructor of metaobjects**, running beside the
one the navigator already has. Two things then decide what class an object is, and the pair has to be
kept in step: a table of which kinds may be pasted onto which, maintained by hand and falling behind
exactly the way any hand-kept list does.

The merge keeps a single constructor. **The target's class is decided by where the paste lands** — the
same decision an ordinary create makes — and the payload only supplies VALUES for properties the
target already has. `ibPropertyObject::PasteProperty` iterates over the TARGET's properties and pulls
each one out of the payload **by name**:

- present in both → carried over;
- only in the source → dropped;
- only in the target → **keeps its default**, which is why an absent value is SKIPPED rather than
  handed over as an empty `ibDataValue` (a property type that rightly refuses an empty value would
  otherwise fail the whole paste — this is what made "paste a Document onto a Constant" die).

The consequence worth naming: **a cross-kind paste is not a special case, it is the general case with
a smaller intersection.** Document→Document and Document→Constant take the same code path and differ
only in how much matched. No mode, no branch, no allow-list — and no root class id in the blob
header, which was tried and removed: nothing could read it without turning a legitimate request into
a refusal.

**The one honest cost.** What does not match is dropped SILENTLY, and property NAME is the only
identity there is. Rename a property and an older clipboard payload quietly brings less than it used
to. That is the price of not having a second currency for "what a metaobject is"; it is a real limit,
not a bug to be patched with a special case.

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

### Modules — RESET the guid, don't adopt it (the one exception to "child adopts")

The default `PasteNodeValue = raw = LoadNode` reads the guid straight out of the node (`SaveNode`/`LoadNode`
in `metaObjectSerialize.cpp` carry `Guid`), so a module held by `ibPropertyInnerModule` (a document's
**ObjectModule** / **ManagerModule**) would ADOPT the source guid like every other child. That adoption is
correct for anything RE-HOMED by guid (references, form source hops) — but WRONG for a module: nothing
references a module by guid (it has no incoming hops), while its compiled bytecode is **cached BY guid**
(`sys_bytecode_cache` + the in-memory `g_byteCodeRegistry`, both keyed on the descriptor guid). Adopting the
source guid makes a copied document's module share the ORIGINAL's cache row → the wrong owner's bytecode
loads → `Binding type mismatch for 'ThisObject': expected clsid X, got Y` at the module pre-flight
(`procUnit.cpp`). It bites the module because a module has bytecode; a predefined attribute adopts the same
way but has no cache row, so nothing goes wrong there.

So `ibPropertyInnerModule::PasteNodeValue` overrides the default — load the node, THEN reset to a fresh guid:

```cpp
virtual bool PasteNodeValue(const ibDataValue& value) override {
    const std::shared_ptr<ibDataNode>& child = value.AsChild();
    if (child) m_metaObject->LoadNode(*child);   // module code
    m_metaObject->ResetGuid();                    // but a FRESH guid — no bytecode-cache collision
    return true;
}
```

A top-level common module already gets this for free (a directly-pasted ROOT keeps its own fresh guid via
`PasteAndRunObject`; only a CHILD adopts). The rule: **a metaobject with re-homed bindings adopts the source
guid; a metaobject CACHED by guid must reset it.** (`metaCollection/metaModuleObject.h`) Note this only helps
copies made AFTER the fix — an already-pasted module has the adopted guid baked into the saved config, so an
old copy must be re-pasted (or its `sys_bytecode_cache` row cleared) once.

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

## Forms and controls — two paths, routed by the blob's self-describing tag

The control tree has the SAME dual structure as a metaobject:

- Normal load/save: `ibValueFrame::LoadNode` / `SaveNode` (`visualView/ctrl/frame.cpp`) → per-control
  `ReadData` / `WriteData` (raw).
- Copy/paste: `ibValueFrame::PasteNode` / `CopyNode` → every property + event through
  `Paste/CopyNodeValue` (a source property re-homes its hops; a plain control round-trips unchanged since
  the pair defaults to Read/Write).

`SaveControl` picks the writer on the form metaobject's **copy mark**; `LoadControl` picks the reader on the
blob's **own tag** — the blob is SELF-DESCRIBING:

```
SaveControl → metaForm->IsCopyMode() ? CopyNode + stamp root "PasteFormat"=true : SaveNode
LoadControl → root.GetValue<bool>("PasteFormat") ? PasteNode : LoadNode
```

The write side reads a live mark, but the read side must NOT — a copy blob can sit on disk long after the
mark cleared (a form pasted and saved but never opened; see the deferred case below). So the copy writer
**stamps `PasteFormat` into the blob root**, and `LoadControl` routes on that tag by CONTENT, independent of
any runtime mark. A raw blob — and every old config, where the key is absent — reads as raw: `GetValue<bool>`
returns `false` for a missing key, so it is back-compatible with no migration.

The in-designer control clipboard copy (`ibValueForm::CopyObject` → `ibValueFrame::CopyObject/PasteObject`)
already serialises through `CopyProperty` / `PasteProperty` → `Copy/PasteNodeValue` — consistent, by
analogy with the metaobject.

### `CopyData` / `PasteData` — the hook for what the walk skips

`CopyNode` / `PasteNode` are **forked from** `WriteData` / `ReadData`, not layered on them. That fork is
why the generic property walk alone is not enough: a datum that is not a property has no way into the
copy blob. Form **attributes** were exactly that datum — they did not survive a copy.

The seam is a pair of virtuals on the frame, defaulting to a no-op so no control has to care
(`visualView/ctrl/frame.h`):

```cpp
virtual bool CopyData(ibDataNode& node) const { return true; }
virtual bool PasteData(const ibDataNode& node) { return true; }
```

`ibValueFrame::CopyNode` / `PasteNode` call them as part of the walk (`frame.cpp`), and the form
overrides them (`visualView/ctrl/form.cpp`):

```cpp
bool ibValueForm::CopyData (ibDataNode& node) const { return WriteAttributes(node); }
bool ibValueForm::PasteData(const ibDataNode& node) { return ReadAttributes(node); }
```

**Why only attributes, and not `WriteData`'s whole payload:** the form's own properties (Title / Orient /
…) already ride the generic property walk inside `Copy/PasteNode`. Re-emitting them here — which is what
delegating to `WriteData` would do — would **double-write** them. Attributes are the one form-level datum
the walk skips, so they are the one thing the hook carries.

### The lazy object-form catch (the hard part)

An OBJECT form (catalog/document/…) materialises **lazily**: `OnAfterRunMetaObject` (the resolve pass)
registers an `ibDeferredForm` (`metaCollection/metaFormObject.h`) in the compile-value cache; the form is
built on first `FindCompileModule` lookup — long after `PasteObject` cleared the paste mark. So at build
time `IsPasteMode` is already false. Routing on the live mark would then read the copy-paste binary as raw
and drop every source hop → the pasted form comes up empty. This was the **reload-bomb**: a copy blob
persisted to disk, re-read in the wrong format.

The fix is the self-describing tag above — NOT a flag threaded through the deferred builder. The copy blob
already carries `PasteFormat` (stamped by `SaveControl` at copy time), so whenever the form is built —
immediately, or a session later off disk — `LoadControl` sees the tag and routes to `PasteNode`, which
re-homes each guid hop through `GetIdByGuid` onto the pasted object (the object adopted the source copy-guid
as its own guid, so the lookup lands on it). The live paste mark is irrelevant at build time.

So `ibDeferredForm::Construct` is **pure** — no metadata side effect, no re-arm/disarm, no captured flag:

```cpp
ibValue* ibDeferredForm::Construct() const {
    if (m_form == nullptr || !m_build) return nullptr;
    return formWrapper::inl::cast_value(m_build());   // m_build() → LoadControl routes by the blob's own tag
}
```

There is **no copy→raw normalization anywhere**: the blob describes its own format, so nothing has to
rewrite it before save. Dissolving that normalization is what let the run drop its third "build" pass — the
lifecycle collapsed back to two phases (see [metadata-lifecycle.md](metadata-lifecycle.md) §5). `ibDeferredForm`
lives in `metaFormObject.h` (not `metaData.h`): the form type is complete there.

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

## The clipboard — one gesture, two currencies

The copy blob above is the engine's private language. But Ctrl+C is a **system-wide** gesture: the
same keystroke has to mean something to Notepad, to a chat window, to the next application the user
switches to. So a copied metaobject is put on the clipboard as **two representations of one thing**:

```cpp
wxDataObjectComposite* composite = new wxDataObjectComposite;

wxCustomDataObject* payload = new wxCustomDataObject(oes_clipboard_metadata);
payload->SetData(writer.size(), writer.pointer());   // the copy blob — only this engine reads it
composite->Add(payload);

composite->Add(new wxTextDataObject(metaObject->GetName()), /*preferred*/ true);
```

**Everyone else sees a string; this configuration sees an object.** Paste into a text editor and the
name arrives as text — which is what a person means by "copy" outside the designer. Paste into the
metadata tree and the custom format is found first, so the whole object arrives: attributes, tabular
sections, forms, their controls, their modules.

Nothing is converted between the two. They are written at the same moment from the same object, and
the receiver picks the representation it understands — which is exactly what the clipboard was
designed to do and what a single-format implementation gives up.

Four formats exist, one per copyable kind, so a paste never has to guess what it is holding:
`oes_clipboard_metadata`, `oes_clipboard_frame` (a form's control subtree),
`oes_clipboard_interface` (a section), `oes_clipboard_role` (`backend/backend_core.h`).

### The clipboard is a lock, and it is taken as one

`wxTheClipboard->Open()` that never reaches its `Close()` holds the clipboard for **every**
application in the session until the process exits — it is an OS-wide lock, not a local handle. All
three metadata trees used to close it inside the branch that succeeded, so two ordinary gestures
leaked it: Ctrl+C on a **group node** (a row that carries a class id and no metaobject, so the "copy
this" branch is skipped) and Ctrl+V with anything that is not an OES payload (`Open()` had already
succeeded when `IsSupported()` said no). It is now an RAII guard —
`designer/mainFrame/metaTree/clipboardLock.h` — because a guard cannot forget the paths nobody
wrote down.

### The blob names its own kind

The root of a copy carries its **clsid** in the header (children always did, as the chunk key).
Paste creates the target from the class id of the tree node it lands on and then compares the two;
a mismatch is refused with a message.

Without that comparison the kind came **only** from where you dropped it: copying an attribute onto
the *Constants* group produced a **constant filled with an attribute's properties** — an object
whose class and contents disagree, named after the thing it was copied from (`Attribute3` as a
constant). Such an object then fails half-way through its run, is never registered in the compile
cache, and its close raises — which surfaced days later as a rollback that could not close the
configuration. A payload that cannot say what it is will eventually be read as something else.

---

## Files

| Concern | Where |
|---|---|
| Clipboard composite (custom + text) + the RAII lock | `designer/mainFrame/metaTree/*Event.cpp`, `.../clipboardLock.h` |
| Clipboard format names | `backend/backend_core.h` (`oes_clipboard_*`) |
| Copy/paste guid slots + guards | `metaCollection/metaObject.{h,cpp}` |
| Copy/paste property hooks (default = raw) | `propertyManager/propertyObject.{h,cpp}` |
| Source binding — dumb raw serializer | `sourceDescription.{h,cpp}` |
| Source binding — guid copy/paste hooks | `propertyManager/property/propertySource.cpp`, `.../variant/variantSource.cpp` |
| Form/control raw vs copy/paste node paths | `visualView/ctrl/frame.{h,cpp}`, `formMem.cpp` |
| Lazy object-form deferred builder (pure `Construct`, self-describing blob) | `metaCollection/metaFormObject.{h,cpp}` (`ibDeferredForm`) |
| Dynamic-list read/select split | `system/value/valueDynamicList.{h,cpp}` |
| Per-config queryable factory | `metaData.{h,cpp}`, `query/queryableFactory.{h,cpp}` |
