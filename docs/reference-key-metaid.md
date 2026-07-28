# Reference key = a pure guid; the type is a separate column

**Status: code complete, NOT yet committed. An earlier revision (pure-guid + `_RTRef`) built green; the
current revision adds the `(guid, type)` keyset tiebreak (below) and awaits a clean build — verification is
blocked only by an unrelated parallel change breaking the shared tree, not by this work.**

A stored reference is a PAIR — `_RTRef` (the target type, a clsid column) + `_RRRef` (a pure 16-byte object
guid). The guid carries NO type.

A reference to an object is **two physical columns**, exactly as it always was in the storage layer:

```
_RTRef : BIGINT   — the target type (clsid). Resolves to the reference's metaObject.
_RRRef : BINARY(16) — the object's identity: a plain guid. Nothing else.
```

The type lives in `_RTRef`; the identity lives in `_RRRef`. They are **orthogonal**: an empty reference is
an all-zero `_RRRef` with its type still in `_RTRef`; a retype touches only `_RTRef`; a join matches on the
globally-unique `_RRRef` guid.

## Why the guid is pure (the road here)

An earlier iteration tried to make the key **self-describing** — bake the `metaID` into the guid's `Data1`
so a bare 16-byte key knew its own type (collapsing 20 bytes → 16). It looked elegant and it was wrong:

- **It duplicated `_RTRef`.** The type was *already* stored beside the guid in the `_RTRef` clsid column
  (`query/columnLayout.cpp`) — the read path even reads it. Baking it into `Data1` stored the type twice.
- **It broke emptiness.** An unset reference became `{metaID, 0, 0, 0}` — non-zero because of the type
  slot — so `isValid()` read it as a real object and tried to resolve it (`Not found <metaID:…>`) instead
  of showing blank. Distinguishing "type slot" from "identity" then needed a special `ObjectIdentityOrEmpty`
  dance.
- **It shrank uniqueness.** `Data1` (4 of the 16 bytes) was spent on the type, leaving 96 identity bits
  instead of 128, and it forced a v4 random guid so the overwritten `time_low` didn't matter.

The clean model was **already half-built here** — we got it for free: a reference column already stored its
type in the `_RTRef` clsid column, and the read path already read it. The fix is simply to **stop storing the
type a second time in the key**. Single-type columns know their type from metadata (schema-on-read); every
reference column also carries the explicit `_RTRef` clsid for composite targets. So the guid is free to be
pure identity.

## How the type is resolved

- **Write** (`ibColumnCodec::WriteValue`): binds `_RTRef` = the value's clsid, `_RRRef` = its pure guid blob.
- **Read** (`ibColumnCodec::ReadField`): reads `_RTRef` into `refClsid`, then
  `ibValueReferenceDataObject::Create(metaData, refClsid, blob)` resolves the metaObject from the clsid
  (`GetTypeCtor(clsid)->GetMetaObject()`) and wraps the pure guid. The type comes from the column, never the
  key bytes.
- **Runtime**: a live reference holds its `metaObject` directly, so it always knows its type without touching
  the guid.

## Uniqueness

A new object mints a **plain unique guid** (`wxNewUniqueGuid` = full v4 random) — no encoding, no timestamp,
no type. 128 random bits: collision is a birthday on 2¹²⁸, i.e. never. Because the guid is globally unique,
a dot-walk JOIN (`source.fldX_RRRef = target.<selfref>_RRRef`) and a keyset anchor match on the guid alone —
the type is not needed to disambiguate identity (two objects, even of different types, do not share a guid).

## Sorting — (guid, type), consistently on both sides

A reference orders by its **guid first, then its type** — identity, then the target-type tiebreak. This holds
identically in RAM and in SQL:

- **Runtime**: `ibValueReferenceDataObject::CompareValueLS` compares `m_objGuid`, then `metaID`.
- **SQL**: the keyset ORDER BY (`BuildSortKeys`) and anchor predicate (`BuildAnchorPredicate`) emit **two**
  fields for a reference sort column — `_RRRef` (the guid, field-normalized to match `guidValueCompare`
  byte-for-byte) then `_RTRef` (the clsid). Because a reference's clsid is `(Reference_kind << 56) | metaID`,
  all reference clsids share the high byte and order by their metaID body — so `_RTRef` order **equals**
  metaID order, matching the runtime tiebreak exactly.

The type tiebreak matters only when guids tie, i.e. for **empty references** (all share the all-zero guid):
it keeps an empty ref of type A distinct from one of type B on both sides, so `CompareValueLS == 0` stays in
lockstep with `CompareValueEQ` (type + guid) and a keyset never mis-pages a run of mixed-type empties. For a
single-type / self-reference column `_RTRef` is constant, so the second field is a harmless no-op. Non-empty
references have unique guids, so the guid alone already decides — the tiebreak never fires there.

## The type id (`_RTRef`) and its ceiling

The type stored in `_RTRef` is the target's clsid, resolving to a metaObject whose `metaID` is the stable
type identity. `GenerateNewID` returns **max(live metaID) + 1** and never reuses a freed id — a stored
reference's type must permanently mean the same thing. `ibMetaID = int` (signed) → ~2.1 billion metaobjects,
which is never a concern (a flagship config reaches millions). `_RTRef` is a `BIGINT` column, so it holds the
full range with room to spare. AI batch-generating metaobjects could climb, but 2.1 billion is ample; the
far-future lever (unsigned / int64) is recorded in the code's sentinel notes, not needed now.

## Empty reference

An empty reference is simply an **all-zero `_RRRef` guid** with its type in `_RTRef`. `isValid()` reads the
zero guid as unset → `IsNewObject()` is true → it presents blank, and `GetObject()` mints a fresh empty
object of the `_RTRef` type. No special-casing, no identity/type gymnastics — zero is zero because the type
was never in the guid to begin with.

## Restructuring (why this shape suits an AI-first, schema-churning platform)

Because the type is a **separate column**, not fused into the key:

- **Retype** a reference column → change `_RTRef` (and the metadata), the `_RRRef` guids are untouched.
- **Clear** values → zero `_RRRef`, `_RTRef` stays; no blob rewrite dance.
- **Single-type columns** can go further (schema-on-read): the type is implied by the column's declaration,
  so even `_RTRef` is redundant there — a retype is a pure metadata operation. (Not yet specialized; `_RTRef`
  is written for every reference column today. A worthwhile follow-up if restructuring of populated tables
  proves hot.)

## Non-goals

- Baking the type into the guid — tried, reverted (this document is the record of why).
- Register keys (composite dimensions) — no reference guid to carry a type.
- A time-ordered guid for insert locality — dropped with the metaID experiment; a pure-random guid scatters
  inserts, which is a possible future refinement (a type-free time-ordered guid) if index locality bites.
