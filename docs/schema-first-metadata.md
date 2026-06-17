# Schema-first metadata

**Status: DIRECTION — design only, no code yet (2026-06-17).** This document
captures an architectural direction, not a landed feature. It reframes
metadata serialization in light of the now-landed L3 schema tier. The core
shift: **the schema becomes primary over metadata.**

It supersedes the "build it out as-is" plan of
[metadata-serialization-arc.md](metadata-serialization-arc.md) and is the
upper half of [metadata-storage-container-arc.md](metadata-storage-container-arc.md)
(single-blob → per-entry == file-per-object).

---

## Why

Today metadata is the owner: `ibMetaData` holds the tree, serializes itself
into a **single chunk-blob** in `sys_config`, and is the source of truth.
Search/load means parsing the monolith by chunks; editing one field rewrites
the whole blob and recomputes one MD5 over everything. This does not scale to
granularity, partial save, or an AI-driven editing seam.

The engine's predecessor lineage stored metadata **by files**, not by chunks —
a proven file-tree model. "Files-as-objects" is that model, and it converges
three existing threads into one:

- this direction (schema-first, file-tree, builder);
- the `metadata-storage-container` debt (replace single-blob with per-entry
  rows == file-per-object — its lower half);
- the predecessor's validated file storage.

## Model — three levels, two downward arrows

```
┌─ Level 1: GLOBAL SCHEMA (describes metadata) ──────────── NEW
│   • files = objects, subfolders = address → file-tree / ZIP,
│     search by path (e.g. Catalogs/Catalog1)
│   • builder facade over CreateMetaObject(CLSID)
│   • self-describing; PRIMARY
│
│        ↓ produces
│
│   METADATA (ibMetaData / ibValueMetaObject) = SKELETON / connective tissue
│   • runtime only: CLSID behaviour, links (Owner / Generation /
│     RegisterRecord), forms, modules. NOT the owner — derived from schema.
│
│        ↓ produces
│
└─ Level 2: ONE SUB-SCHEMA for DDL + DATA ──────────────── ALREADY LANDED
    • one sub-schema, two consumers:
        — DDL: create/update the database (restructuring)
        — data I/O: dump/restore (both operate on the same tables)
    • = ContributeTables + ibDataMover (docs/query-language-arc.md §23)
```

The restructuring sub-schema and the data sub-schema are **the same** —
DDL and data I/O both work over the same tables. That has already converged:
`ContributeTables` is the single source of DDL + data + seed, and `ibDataMover`
moves data over the same schema. **Level 2 is built.** What remains is Level 1.

## Ownership: decoupling, not full inversion

The precise framing is **not** "the schema generates metadata from nothing".
Metadata is **created as-is** and stays a first-class runtime core; what moves
to the schema is the **save/load mechanism**. This is exactly the L3 pattern,
one level up:

- **L3** took **data-access** out of metadata — the metaobject no longer issues
  SQL itself; access went to the L3 doors.
- **The schema** takes **structure-persistence** out of metadata — the
  metaobject no longer serializes/loads itself; save/load goes to the schema.

| Layer | Owns | Role |
|---|---|---|
| **metadata** | runtime: structure, behaviour, links (Owner/Generation/RegisterRecord), forms, modules | created **as-is**, first-class |
| **schema** | **persistence**: save / load / fill-with-data | fills metadata on load, extracts on save (via providers) |

So metadata does **not** vanish or become a pure projection — it stays the
core. Only **who does the I/O** changes: the metaobject used to serialize
itself; now the schema does, through pluggable providers. The metaobject slims
down (loses save/load — dovetails with the const-meta / footprint arcs). This
is **safer than a full inversion**: runtime metadata is untouched, only the
mechanism is extracted — just as L3 extracted data-access without breaking
metaobjects.

The two-way projection (the "living organism reshapes the schema") already
exists in part: designer runtime edits and Configuration Compare/Merge are the
reverse arrow. The compare walker becomes the canonical round-trip projector.

## Mechanism — generalize the `ContributeTables` walk

The walk already exists (`ContributeTables` over child metaobjects +
`ibSchemaBuilder` accumulator, landed for DDL). The global schema **reuses the
same pattern**, widened from "tables only" to "the whole object structure":

```
   walk children (GetChildren, recursive — like ContributeTables)
        │
        each metaobject → Contribute(ibSchemaBuilder&):
        │     • type (CLSID)
        │     • primitives: Name, Synonym, Comment, qualifiers, flags
        │     • complex (form / module) → binary
        │     • children → recurse
        ▼
   ibSchemaBuilder accumulates → SCHEMA tree
        │                        { CLSID, map<name,primitive>,
        │                          map<name,blob>, children[] }
        ├──→ SaveSchema:  schema → file-tree (binary alongside)
        └──→ GetSchema:   the tree itself
```

`SaveData`/`LoadData` (already renamed Dump/Restore for data) gain a
structure-level sibling: **`SaveSchema` / `GetSchema`** over that same tree.

### Resolved: A — one schema, multiple backends

Each class declares its fields **once** (the rules live in metadata); the
**backend** picks the output format. There is no separate converter / parallel
`ContributeSchema` — that would re-state the structure (a second source of
truth that drifts). One `Visit(name, value)` per class, backend selects format:

```
   class declares fields once (rules in metadata)
                 │
     ┌───────────┼───────────┬──────────┐
     ▼           ▼           ▼          ▼
   binary       JSON        XML       (any)
   INTERNAL    EXTERNAL   EXTERNAL
   (platform   (export /  (export /
    data I/O,   AI)        exchange)
    fast,
    positional)
```

- **binary = internal** — read/write data inside the platform; fast, positional
  (the "binary is faster" argument holds — it stays here, where speed matters).
- **JSON / XML = external** — anything going out: export, AI, exchange; named,
  readable.

**This consolidates an existing triple-duplication.** XML/JSON config export
already exists (`SaveConfigToXML` / `SaveConfigToJSON`, see
`metadataConfigurationXML.cpp` / `metadataConfigurationJSON.cpp`) — today binary
blob + XML + JSON each re-list the same fields in three places, drifting on any
field change. Folding them into one field-visitor + N backends removes that
dup as a bonus — we are not adding a 4th serializer, we are collapsing the
three that already work.

## Node model — recursive, uniform

The schema is a tree of **uniform, self-similar nodes** — every node, at any
level, has the same shape:

```
   NODE:
     name        — this node's name (Catalog1)
     type        — CLSID (Catalog / Attribute / TabularSection / Form …)
     named area  — primitive fields (Synonym, Comment, qualifiers) via Visit(name,value)
     children    — sub-nodes: also NODES (recursion)
```

Each node is a small self-contained sub-schema. `TabularSection` holds
`Column` nodes; an `Attribute` may hold a composite-type sub-node; etc. The
walk does not branch on "is this a Catalog or an Attribute" — it recursively
visits **uniform nodes**, and the node's `type` says how to expand. One
`ibSchemaNode`, one recursive visitor — this uniformity is what makes the
schema "universal" (not N node classes, one self-similar node).

## Value layer — typed primitives + composite sub-nodes

The visitor passes **typed** values; each backend materializes each kind:

| Value kind | Internal (binary, fast) | External (JSON) |
|---|---|---|
| String | len + bytes | `"text"` |
| Number (big-decimal, 200+ digits) | binary decimal (exact) | **number-as-string** (text keeps precision; a JSON double would lose it) |
| Date | binary | ISO string |
| Bool | byte | `true` / `false` |
| GUID | **16 bytes** | **formatted string** `xxxx-…` |
| Composite type (`TypeDescription`) | sub-structure | **sub-section**: `typeID[]` + qualifiers (string-length / number-precision / date-parts) |

Primitives are scalars. A composite type (`TypeDescription` — an attribute's
type: Number+precision / String+length / Date+parts / Reference) is **not** a
scalar — it expands into a sub-node walked by the same recursion. The node/field
`type` tells the reader how to interpret each value.

## Format-agnostic visitor + pluggable provider

The visitor is **format-agnostic**. `Visit("name", value)` in a class declares
"I have field name with this value" — it does **not** know whether the output
is JSON, XML or binary. The format is a separate **provider** implementing the
writer interface:

```
   abstract ibSchemaWriter:
        Visit(name,value) / VisitNumber / VisitGuid / VisitChild …
                    ▲ implemented by
        ┌───────────┼────────────┬───────────┐
   ibBinaryWriter  ibJsonWriter  ibXmlWriter  (plugin) …
   (internal)      (rules:       (rules: …)
                    big-num→str,
                    guid→str)

   schema.Export(provider)   // passing the provider == passing the format rules
```

- `Export(jsonProvider)` — the provider encapsulates "how to save as JSON"
  (big-number→string, guid→string, composite→sub-section).
- `Export(binaryProvider)` — same walk, but 16-byte guid, positional decimal.
- Visitor/Strategy: the walk is abstract, the format rules live in the provider.

**Pluggable = extensibility.** A new format is a **new provider**, with zero
changes to the schema or to metadata — a format provider is one of the plugin
functional capabilities (ties into `plugin-extensible-metadata`). Reading is
symmetric: a **reader provider** (`ibSchemaReader`) parses the format into the
uniform node tree; the parser lives in the provider, the node tree stays
format-agnostic.

**Physical layout is the provider's concern, not the schema's.** Whether the
output is a flat indexed blob or a folder/file-tree is decided by the provider,
per its goal — the schema never mandates it:

- the **binary provider** stores for fastest lookup — a flat / indexed layout,
  **no mandatory folders**; speed is its job;
- the **JSON / export provider** lays out a file-tree / folders
  (`Catalogs/Catalog1`) for navigation and AI readability — folders are needed
  only on the *output*, when reading by file. The ZIP container (per-entry
  random access + compression) belongs here, to the read/export provider.

So "file-tree" / ZIP is **not a universal storage requirement** — it is how one
read-friendly provider materializes the tree. The binary provider is free to
optimize for speed; the in-memory node tree is agnostic to both. Providers are
pluggable components: JSON is the default ("our own" format), binary is the
provider you reach for when you need it fast and cheap.

## Named serialization (critical) + optimistic cursor

The binary format is **positional** (read in write order). The schema must be
**named**. The field-visitor carries the field name, always:

```
   Visit("name",    value)       // not just "a string" — name + what it is
   Visit("synonym", value)
   VisitBinary("form", blob)
   VisitChild(...)
```

The backend decides what to do with the name:

- **Binary** (current blob): may ignore it — writes positionally.
- **Schema / JSON / file-tree**: the name is the **key**. Written `"name": ...`,
  read **by key**, not by position.

Reading by key gives robustness + **forward-compatibility**: add / drop /
reorder a field and nothing breaks (a positional binary is fragile — reorder
`string / int / uint` and the rest cascades). **This closes forward-compat —
one of the three irreversibles in plugin-extensible metadata.** Minor cost: an
alias table for field renames (the name is now a contract).

### Optimistic-cursor reader

The reader expects `name` at the current cursor position:

- **Fast path** (order unchanged — typical): the key matches → read, cursor++ →
  positional speed plus one key check.
- **Slow path** (order shifted): key mismatch → search `name` from the cursor →
  `O(n)`, costlier but correct.
- **Not found at all** (new field / old schema): cursor does not move → default,
  not an error.

So named robustness at positional cost in the common case — pay for the search
only when the structure actually changed. For a sequential format
(stream / named-binary) the cursor matters; for a map format (JSON object) it
degenerates to an `O(1)` key lookup. Implement the reader as "try the current
position → else find" (covers both).

**Self-healing.** The slow path is temporary: the **first rewrite** of the
configuration writes fields in the current code order → the structure
normalizes → the next read is fast again. Degradation does **not** accumulate
(a `read-slow → write → read-fast` cycle); the slow path lives only in the
window between a metadata code change and the first config rewrite. Net:
amortized positional speed + always-correct + auto-normalization on write.

## First slice — deliberately narrowed, reversible

The cut: **make the AI able to read the configuration** — a JSON writer
provider over the existing per-class `Save`. Storage is untouched; "how it is
stored" (ZIP-in-DB, owner inversion) is a later concern.

1. **Field-visitor over the existing per-class `Save`** (Variant A): each class
   declares its fields once through the abstract `ibSchemaWriter`. The current
   binary path becomes the `ibBinaryWriter` provider — zero runtime risk, it
   keeps working as the internal/fast format.
2. **`ibJsonWriter` provider beside it**: walk the same declaration, emit
   AI-readable JSON per object (named keys; big-number→string; guid→string;
   composite type→sub-section). Primitives readable; truly-binary (compiled
   form, bytecode) referenced/flagged rather than inlined — modules ideally as
   source text.
3. **Read/export first.** The AI reads the JSON projection; metadata stays the
   owner; the projection lives **alongside** `sys_config`. Reverse
   (`reader provider`: JSON→nodes→metadata build), ZIP, owner-inversion — later.
4. **Eager round-trip golden-test** once the reader provider lands.

The whole slice: introduce the abstract field-visitor, route the existing
per-class `Save` through it (as `ibBinaryWriter`), add `ibJsonWriter`. No owner
inversion, no lazy, no special entities — and it removes the binary/XML/JSON
triple-duplication on the way.

## Deferred until the projection is proven

- **Owner inversion** (schema as source of truth) — the point of no return;
  it rewrites reload/persist across the system.
- **Lazy fault-in** from files — unlocks the file-tree fully but is heavier
  (designer lazy load, graph fault-in for reference integrity).
- **Provider / adapter special entities** — that is plugin extensibility,
  a separate track.
- **Replacing the `sys_config` blob** — schema lives alongside it first.

Do not build the mega-schema up front. Prove the mechanics on a reversible
slice; the owner inversion needs a working projector underneath it before it
can be trusted.

## Relationship to existing debts

- **metadata-serialization** — the node-owned chunk-walk is heading for
  **replacement** by the schema file-walk, not completion.
- **metadata-storage-container** — file-per-object is exactly its
  single-blob → per-entry goal; this is its lower half.
- **plugin-extensible metadata** — named serialization resolves the
  forward-compat irreversible naturally.
- **L3 query-architecture vision** — the open item "invert: the door consumes
  the schema" is precisely this direction.
