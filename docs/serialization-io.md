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

Four classes (`fs.h`, ~290 lines + `fs.cpp`, 396):

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

## 5. Honest remainder

- `fileSystem/` should be `io/` (§ header note).
- QuickLZ is vendored at **1.5.1 BETA 7** — a beta from 2011.
- LZHUF is 1988 C, kept for compatibility with data written by it; which codec writes
  *today* (and whether both are still reachable) is worth verifying before any swap.
- The `w_string` / `w_stringZ` pair differ subtly: `w_string` appends CRLF (13, 10),
  `w_stringZ` appends a NUL. Only `w_stringZ` is a data format; `w_string` is for text
  logs.
