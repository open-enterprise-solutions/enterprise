# Byte I/O — `ibWriter` / `ibReader`, chunks and compression

> **Scope:** `backend/fileSystem/` — the byte-level reader/writer everything binary rides
> on: metadata blobs, copy/paste, the AOT cache, forms.
> Companions: [metadata-serialization-arc.md](metadata-serialization-arc.md) (the node tree
> above this), [copy-paste.md](copy-paste.md), [property-system.md § 6](property-system.md).
> This is foundation code.

> ⚠ **The directory name is misleading.** `fileSystem/` is **not** a file-system
> abstraction — it is a byte stream. Script-level file operations are the `File` value type
> and the file built-ins ([system-functions.md § 2.5](system-functions.md)). A rename
> candidate: this is `io/` or `stream/`.

---

## 1. Shape

Four classes (`fs.h`, 275 lines + `fs.cpp`, 396):

```cpp
class BACKEND_API ibWriter        { … };   // abstract sink
class BACKEND_API ibWriterMemory  : public ibWriter { … };
class BACKEND_API ibReader        { … };   // abstract source
class BACKEND_API ibReaderMemory  : public ibReader { … };
```

The abstract half defines *what a stream must do*; the memory half is the implementation
everything actually uses (`CopyProperty(ibWriterMemory&)`,
`PasteProperty(ibReaderMemory&)`, the paste blob, the AOT cache).

The kernel a backend must supply is three calls:

```cpp
virtual void seek(u32 pos) const = 0;
virtual u32  tell() const = 0;
virtual void w(const void* ptr, u32 count) = 0;
virtual void flush() = 0;
```

Everything else is generalised on top:

```cpp
inline void w_u64(u64 d);  w_u32; w_u16; w_u8;
inline void w_s64(s64 d);  w_s32; w_s16; w_s8;   inline void w_float(float d);
inline void w_stringZ(const char* p);            // NUL-terminated
inline void w_stringZ(const std::string& p);
inline void w_stringZ(const wxString& p);        // ← UTF-8 on the wire
void        w_printf(const char* format, ...);
```

**`wxString` is written as UTF-8** (`p.utf8_str()`), which is the "wchar in memory, UTF-8
on the wire" rule ([value-audit.md](value-audit.md)) enforced at the one place it matters.

Types are plain fixed-width aliases (`types.h`): `s8/u8 … s64/u64`, `f32/f64`.

---

## 2. Chunks — the format primitive

The stream is not flat. It is a **nesting of typed chunks**, and the writer tracks the
nesting itself:

```cpp
class BACKEND_API ibWriter {
private:
    std::stack<u64> m_chunk_pos;      // ← open chunks, innermost on top
public:
    u32  align();
    void open_chunk(u64 type);
    void close_chunk();
    u32  chunk_size() const;          // size of the currently open chunk, 0 otherwise
    void w_compressed(void* ptr, u32 count);
    void w_compressed(const wxMemoryBuffer& data);
    void w_chunk(u64 type, void* m_data, u32 size);
    void w_chunk(u64 type, const wxMemoryBuffer& data);
};
```

`open_chunk(type)` / `close_chunk()` bracket a region tagged with a `u64` type id;
`close_chunk` back-patches the size. Because the open positions live on a **stack**, chunks
nest freely — which is what lets a metaobject write its own chunk containing its children's
chunks, and lets a reader skip a chunk it does not understand by size alone.

This is why the reader side reads like this (`metaObject.cpp`):

```cpp
std::shared_ptr<ibReaderMemory> readerHeaderMemory(reader.open_chunk(headerBlock));
/*const ibVersionID& version =*/ readerHeaderMemory->r_s32();
```

— open the named chunk, read typed fields out of it. Chunk ids are what make the format
**forward-tolerant**: an unknown chunk is skippable, a missing chunk is detectable.

`ibNumber` uses the same discipline for its own payload (`kIbNumberChunk` —
[../CLAUDE.md](../CLAUDE.md) §2a).

### File kinds — one table, `backend/fileKind.h`

What a file is called is answered in one place, not at each dialog. A kind knows its
extension and how a file dialog should describe it; `ibFileMask` / `ibFileFilter` /
`ibFileExtension` are what the six call sites ask.

| Kind | Extension | What travels in it |
|---|---|---|
| `Application` | **`.oap`** | the whole application — metadata, module code, forms, rights |
| `Tool` | **`.otl`** | an external tool (formerly "data processor") |
| `Report` | **`.orp`** | an external report |
| `Table` | **`.oxl`** | a spreadsheet document |
| `Log` | **`.olg`** | the platform's own journal |
| `Save` | **`.osv`** | a data dump — the base written out, restorable elsewhere |

**The names are ours.** `o` marks the family; the two letters after it come from the word
*we* use for the thing, not from a neighbouring product's abbreviation. That is the whole
rule, and it is why the set reads as one set.

**Application, not schema.** The file carries module *code*, and no reading of the word
schema covers code — `schema` also already means the shape of the database tables in a few
hundred places in this tree. A file that contains an application is called an application.

**No legacy names.** `.mcf` `.edp` `.erp` `.obk` are gone rather than accepted-on-read: the
platform is pre-release, there is no installed base to carry, and carrying one would mean
every dialog offering two names for one thing forever. A file made before the rename opens
after being renamed.

**An extension is a hint, not proof.** The signature belongs INSIDE the file; this table is
where the pairing (kind → signature → extension → provider) will be stated once signatures
land.

---

### ⚠ A reader BORROWS its bytes

`ibReaderMemory` keeps the pointer it is given and never owns a copy. `ibWriterMemory::buffer()`,
by contrast, hands back a `wxMemoryBuffer` **by value**. Putting the two together directly —

```cpp
ibReaderMemory reader(writer.buffer());   // the temporary dies at the semicolon
```

— leaves the reader on freed memory, and the failure surfaces nowhere near the mistake: the chunk
lengths come back as garbage, and the first sized read walks off the heap as an access violation
inside `memcpy`, with nothing on the stack naming this line. It cost a full debugger session to
find once (`MetaDataSerialize.TheTreeIS_WhatTravels`, 2026-08-05).

The rvalue overload is therefore **deleted** in `fs.h`, so the mistake is now a compile error
rather than a rule to remember. Hold the buffer, then read it:

```cpp
const wxMemoryBuffer blob = writer.buffer();
ibReaderMemory reader(blob);
```

Every other call site in the tree already uses the `(data, size)` form over a live object.

---

## 3. Compression

`w_compressed` is the compressing write. Two vendored codecs sit under it:

| Codec | Directory | Origin |
|---|---|---|
| **LZHUF** | `lz/lzhuf.{h,cpp}` | Haruyasu Yoshizaki, 1988–89; comments translated by Haruhiko Okumura |
| **QuickLZ** | `quicklz/quicklz.{h,c}` | Lasse Mikkel Reinhold, 2006–2011; v1.5.1 BETA 7 |

QuickLZ requires that compression settings match on both sides —
`QLZ_COMPRESSION_LEVEL` and `QLZ_STREAMING_BUFFER` must be identical for compress and
decompress, or the data is garbage. They are compile-time defines, so **a build-flag change
is a format change**.

---

## 4. ⚠ Licensing — both codecs conflict with a commercial LGPL product

This is not a style note. OES is **LGPL 2.1** ([../CLAUDE.md](../CLAUDE.md) § Tech Stack)
and is intended to be sold. Both vendored codecs carry terms that do not fit that, quoted
verbatim from their headers:

**LZHUF** (`lz/lzhuf.cpp`):

```
LZHUF.C (c)1989 by Haruyasu Yoshizaki, Haruhiko Okumura, and Kenji Rikitake.
All rights reserved. Permission granted for non-commercial use.
```

> **non-commercial use** — a commercial distribution of OES is outside the grant.

**QuickLZ** (`quicklz/quicklz.h`):

```
QuickLZ can be used for free under the GPL 1, 2 or 3 license (where anything
released into public must be open source) or under a commercial license if such
has been acquired (see http://www.quicklz.com/order.html). The commercial license
does not cover derived or ported versions created by third parties under GPL.
```

> **GPL or a purchased commercial licence.** GPL is copyleft-stronger than LGPL 2.1:
> linking it in pulls the combined work toward GPL, which is incompatible with shipping OES
> under LGPL 2.1 — and with closed-source distribution.

**Options, in the order they are cheap:**

1. **Replace both** with a permissive codec — **LZ4** (BSD-2) or **Zstandard** (BSD/GPL2
   dual, BSD in practice). Same job, faster, actively maintained. The seam is already
   narrow: everything funnels through `w_compressed` / its reader twin, so a swap touches
   the codec files and that pair.
2. **Buy** a QuickLZ commercial licence — does not solve LZHUF.
3. Do nothing and accept the exposure — **not viable if the product is sold**.

⚠ **Changing the codec changes the on-disk format.** Existing bases / AOT caches / saved
blobs compressed with the old codec must be readable, so a swap needs either a version gate
on the chunk or a migration pass.

This belongs with the existing legal-review item in
[syntax-helper-design.md](syntax-helper-design.md) (§ *Legal-review TODO before Phase 2
starts pulling third-party*) — the same class of question, unresolved.

---

## 4a. Values pack themselves — one mechanism, any provider (2026-08-04)

A value used to serialize to a STRING and only to a string, which meant a composite could not
serialize at all: an array or a structure has no honest text form, so settings could hold a number
but not a list of them.

Now there is one mechanism and it writes an **ibDataNode** — the same tree metadata is written
through. What comes out is the provider's business: binary for storage and transport, JSON for
exchange and for a human reading a dump. Text is a RENDERING of the node, not a second
implementation every type had to maintain. Another format later is another provider, and nothing in
any value changes.

**The split is what keeps it honest:**

| Method | Who | What |
|---|---|---|
| `ibValue::Serialize` / `Deserialize` | the BASE, once | the header: the `IsTransferable` gate, then the type |
| `DoSerialize` / `DoDeserialize` | the CHILD | its own contents — and its elements are asked the same question, so the walk continues by itself |

The default knows the **primitives and nothing else**, which is all the base can honestly claim. A
mutable value — a form, an open object, a lambda — overrides nothing and is refused by the gate
before any of this runs.

### Packing is also COPYING — no packed form, no copy (2026-08-13)

`ibValue::CloneValue` is the default copy for everything that is not a primitive: pack the value, then create it
from the node through the registered ctor (`FromNode`). A primitive returns `*this` — the payload IS the
value. So the same override pair answers two questions, and a type that overrides neither is not merely
unsaveable, it is **unduplicatable**.

`TYPE_ENUM` was such a type until 2026-08-13. It fell through the base's switch to "a type with contents of
its own that did not override this" and answered `false`, so an enumeration could not be stored anywhere:

| Where | What the user saw |
|---|---|
| any saved setting holding an enum member | came back empty |
| the list-settings dialog over a filter on an enum column | an empty settings form over a plainly filtered list |

The second is the sharper case, and it is not about settings. `ibLoadSettingsFromComposer`
(`backend/composition/listFilter.cpp`) copies the LIVE filter tree into the dialog's edit buffer **by packing
it**, and it clears the buffer's root before filling it — so a refusal left the buffer cleared and the form
opened on nothing. Every step was honest, nothing raised, and the answer was wrong: the cost of a `false`
that no caller is obliged to report.

### A value is blind to metadata

The value layer does not know that metadata exists — no `activeMetaData`, no include of
`metadataConfiguration.h` under `backend/compiler`. A value packs and unpacks ITSELF; who holds the
registry of types is somebody else's question.

Creating a value FROM a node is **`ibValue::FromNode`** — one static mechanism, in one place: read
the type, create through the VALUE registry, hand the new value the whole node. Both doors end here.

What the header SAYS is a smaller question and lives apart, in the narrow
**`compiler/valueSerialization.h`** (`ibReadNodeType`), because it changes with the reading format
rather than with `ibValue`, and `value.h` is included by half the engine.

### The door is the metadata

```cpp
metaData->Serialize(value, node);          // a value in, a filled tree out
ibValue value = metaData->Deserialize(node);   // a tree in, a live value out
```

A caller takes the metadata it wants — the active one, or any other — and asks it. Instance methods,
not static: it is THAT configuration's ctor registry a catalog reference has to come from.

Bytes are not a second pair of methods. A caller that wants a blob writes the node through
`ibBinaryProvider`, one that wants text through `ibJsonProvider` — exactly how the metadata itself is
saved.

**The redirect is one line.** The door asks `GetTypeCtor` — its OWN registry, deliberately not
`IsRegisterCtor`, which already answers for `ibValue`'s registry too and would swallow the question.
Not mine → `return ibValue::FromNode(node)`, and everything past that point happens once, in the one
mechanism, for both doors.

### Failure is an exception, never a quiet empty

If neither the configuration nor the value registry has the type, the read RAISES
(`ibBackendCoreException`). Same for a value that cannot be created, cannot read its own contents, or
— on the writing side — has no packed form at all.

An empty value would be indistinguishable from one that legitimately IS empty, and would surface
three layers away as a blank field nobody can explain. The caller asked for a value; there is none;
saying so is the only honest answer.

### What is implemented

- **array** — elements as child nodes; the declared count is a cross-check, the children are the loop bound
- **container / structure** — pairs; an odd number of children is refused
- **enumeration** — the member number, and nothing else: the header already carries the type, so the
  member IS the contents. The pair sits on `ibValueEnumerationVariantBase`
  (`compiler/enumUnit.h`), the template every registered enum member instantiates, so no enum
  declares it ([enumerations.md § 2](enumerations.md)).
- **reference** — identity only: the metaID **and** the guid. The object never travels; the far side
  re-reads it under its own rights.

**The two field names are shared.** `kValueFieldClsid` (`"t"`) and `kValueFieldData` (`"v"`) are declared
in **`compiler/value.h`**, not in the serialiser: more than one place writes into a packed node — the enum
template writes its member from where it is defined — and a value written under one spelling and read
under another is a value lost.

**Why a reference writes its metaID.** A reference's class id is DERIVED from the metaobject's
metaID, so within one base either identifies the type. Across bases they part company: a copy of the
base can number the same catalog differently, and a stored setting would then restore a reference
pointing at whatever type happens to hold that id — silently, and at the wrong table. The class id
stays in the header as the fast path; the metaID is what the reader consults when the fast path is
not enough. A mismatch is refused; an absent metaID (0) is an older record, not a disagreement.

### A restored reference recovers its IDENTITY, not just its guid (2026-08-13)

`ibValueReferenceDataObject::DoDeserialize` writes the guid into **both** the inner `ibReference` and the
object's own `m_objGuid`, decides `m_newObject` from that guid (an empty one restores as an empty reference
of that type, which IS new), clears `m_initializedRef` / `m_foundedRef`, and then calls `PrepareRef(true)` —
the same step `Create(metaObject, guid)` takes, and the step that decides whether the identity is FOUND.

Writing only the inner guid left the object still calling itself NEW: it had been constructed empty from
the class id in the header, and `m_newObject` was decided there. Everything that reads the guid — filtering,
comparison, saving — was correct, and `GetString()` answered `""`, because it returns an empty string for a
new object before it looks anything up. A stored list filter came back with its value invisible while
plainly still in force.

The init flags are cleared **before** `PrepareRef`, which returns early on `m_initializedRef`: the object may
already have prepared itself as the empty one it was a moment ago. Skipping the preparation swaps one wrong
presentation for another — the value reads `Not found` instead of its name.

⚠️ A user **enumeration** is a reference family (`ibValueMetaObjectRecordDataEnumRef`), so its members
travel this road, not the enum one above.

⚠️ **The node has a fixed set of codecs** (string, bool, s32, blob, guid, ibNumber, wxDateTime) and
none for a 64-bit integer: the type is stored as text, which also makes the JSON dump readable.

⚠️ **A reference inside a container** is read at the value level, where configuration types do not
exist — so it raises rather than degrading. Reading such a value goes through the metadata door.

Tests: `ValueSerialize.*` and `MetaDataSerialize.*` in `tests/test_compiler.cpp`. The latter stand in
for a configuration with a small `ibMetaData` subclass — enough to pin the contract of the door
without a database behind it.

---

## 4b. A node's areas mean different things — and a metaobject's children ARE metaobjects (2026-08-20)

`ibDataNode` (`serialize/dataBuilder.h`) is not one bag. It has three areas, and the difference
between them is a statement about the object, not a storage detail:

| Area | Written with | Means |
|---|---|---|
| **fields** | `SetValue` / `GetValue` | the node's plain intrinsic values (its id, its name) |
| **properties** | `SetProperty` / `GetProperty`, and `Child(name)` for a composite one | **what the object IS** — the property walk's own area, and where a named sub-node lives |
| **children** | `AddChild(clsid, metaId)`, walked by `Children()` | **the objects UNDER it** — the metaobject tree, recursed by `ApplyDataNode` |

⭐ **So a metatype keeps its content in a PROPERTY.** A composite value — a spreadsheet description,
a whole composition, a type descriptor — goes into a named sub-node of the properties area, where it
may write as many children of its own as it likes, because they are inside ITS node. The metatype's
own two lines then read the same in every type: `node.SetProperty(name, prop->GetNodeValue())` out,
`prop->SetNodeValue(node.GetProperty(name))` in.

🛑 **The rule is now enforced, and it raises.** `ibValueMetaObject::ApplyDataNode`
(`metaCollection/metaObjectSerialize.cpp`) refuses a child node whose clsid is not metadata-kinded:

```cpp
if (!IsMetadata(childNode.GetClsid()))
    ibBackendCoreException::Error(
        _("Node of '%s' holds a child that is not metadata (class id %lld) — a metaobject's "
          "children ARE metaobjects; its own data belongs in a named sub-node"), …);
```

The id is KIND-TYPED ([../CLAUDE.md](../CLAUDE.md) §6), so the question is answered by the id itself
with no registry lookup. **Raised, not skipped** (Max, 2026-08-20: *"give me an exception when it is
not a metaobject — not a quiet create"*): a skip would let a writer keep making the mistake behind a
warning nobody reads, while the refusal names the id — and the id names the writer.

**The defect it was written for.** The composer metatype lent its own node to
`ibValueDataComposition::WriteProperty`, which writes a child per VARIANT and a child per PARAMETER,
keyed by the loop INDEX under synthetic `CompositionVariant` / `CompositionParameter` ids of kind
`None`. Those sat in the area where a `(clsid, metaId)` pair means identity. The file saved cleanly
and then could never be opened again — *"Error creating object '67799176431653306'"*, that clsid as a
number. The composer now serialises through a property like the template metatype does
([report-engine.md §4f](report-engine.md)); this guard is what stops the next writer losing a file
the same way.

⚠ **A raise is only as loud as the boundary that catches it.** All three metadata containers wrap
`ApplyDataNode`, and until 2026-08-20 only the report's copy reported what it caught — so this
refusal, met by a configuration, closed the file with nothing said at all
([exceptions.md § 4](exceptions.md)).

---

## 4c. What LEVEL a thing is written at is a claim about it (2026-08-20)

The composition's node holds three child kinds — variants, parameters and resources — and where each
one hangs is not a filing convenience. **The level is the statement.** A variant is a snapshot, so
what hangs UNDER a variant is claimed to be per-snapshot; what hangs beside the variants is claimed
to be shared by all of them.

`ibValueDataComposition::WriteTotals` / `ReadTotals` write the **resources at composition level**,
beside the parameters, and that is a deliberate refusal to write them per variant. Today one store
(`ibDataComposer`'s totals) holds them for every variant — switching a variant does not switch them —
so writing a copy under each variant would put a snapshot on disk that the code never takes, and the
next reader would trust the file over the code. The parameters are written at the same level for the
same reason, and both are ⏳ open to move down the day the composer really does snapshot them.

⚠ **Order matters on the way back in, and only on the way back in.** `ReadProperty` reads the
resources **after** the variants: applying a variant rewrites the store's settings, and the store is
where a resource lives, so reading them first would hand them to a store that is about to be
overwritten. Nothing about the WRITE side cares about order, which is exactly why the read side's
dependence on it is worth writing down.

---

## 4d. 🛑 Writing through a live FACADE during a LOAD announces a change nobody made (2026-08-20)

A settings object comes in two modes — a **buffer** with its own storage, and a **facade** over the
composer's store ([list-settings.md § 6](list-settings.md)). A facade's mutators now also carry the
change signal, so a filter written through one announces itself
([property-system.md § 5.2](property-system.md)). That is right for an edit and wrong for a **load**:
reading a saved record is not somebody changing it.

The legacy read path is where it showed. `ibValueDataComposition::ReadProperty` has a branch for
records written before variants existed, and that branch read the old single settings set *through*
the live facade — so opening an old report announced it as modified before the person had touched
anything. It now reads into a **buffer-mode** `ibValueListSettings` and commits to the store directly
(`ibCommitSettingsToComposer`), which is the shape the variants path always had. The filter TREE is
the one thing that cannot travel that way — the composer takes a filter as one expression and cannot
hand it back — so it is installed with `SetFilterRoot`, which replaces the root without going through
a mutator and therefore stays silent too.

⭐ The general rule, and it applies to every value that grew a change signal: **anything that writes
through a live facade during a LOAD must go through the buffer instead.** A load has no author.

---
## 5. Honest remainder

- `fileSystem/` should be `io/` (§ header note).
- QuickLZ is vendored at **1.5.1 BETA 7** — a beta from 2011.
- LZHUF is 1988 C, kept for compatibility with data written by it; which codec writes
  *today* (and whether both are still reachable) is worth verifying before any swap.
- The `w_string` / `w_stringZ` pair differ subtly: `w_string` appends CRLF (13, 10),
  `w_stringZ` appends a NUL. Only `w_stringZ` is a data format; `w_string` is for text
  logs.
