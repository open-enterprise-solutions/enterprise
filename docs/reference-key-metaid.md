# Reference key = guid (metaID in Data1)

**Status: Phase 1 in code (uncommitted, build-verify pending). Phases 2-6 planned.**

A reference to an object is just the object's **guid** — a single 16-byte value that
*also* carries the target's `metaID` in its first field (Data1). No side-channel type
tag, no separate metaID column: the same 16 bytes answer both "which object" and "what
type". One primitive, two guarantees, consistency by construction.

This replaces the older `_RRRef = [guid 16][metaID 4]` layout (20 bytes) with a
self-describing 16-byte key.

---

## The layout

```
 byte:  0   1   2   3 | 4 5 | 6 7 | 8 9 10 11 12 13 14 15
        └── Data1 ───┘  Data2 Data3 └──────  Data4 ──────┘
        =  metaID       └──── uniqueness within the type ────┘
```

- **Data1 = metaID** — the target metaobject's id (an `int`), stamped at mint.
- **Data2/3/4 (12 bytes) = the original random/time-based tail** — generated exactly as
  before by the OS uuid source, untouched.

Data1 is v1 `time_low`; branding overwrites it. That trades fine `time_low` ordering for
a **type prefix**; coarser creation-time order survives in Data2/3.

### Why Data1

- It is a **named field** on the storage dupe (`ibGuidImpl::m_data1`), so patch/read is
  one assignment — no byte-poking, no endian math.
- It **leads the persisted key**, so the metaID is SQL-sliceable (`substring(key,1,4)`)
  and an index prefix / sort groups references by type (see *Sorting* below).

---

## Uniqueness is per-metaID, not global

The metaID is *part* of the key, so two objects of different types can share the same
random tail and still be distinct keys. The tail therefore only has to be unique **within
a type**:

- **metaID** — exact, deterministic (the metadata generator is a counter, not a lottery).
- **tail** — 12 bytes = 96 bits from the OS uuid generator. Birthday bound ≈ 2⁴⁸; at 10⁹
  objects of one type the collision probability is ≈ 10⁻¹¹.

Two independent guarantees compose into one: *exact type × uniqueness-within-type*.

---

## Type roles (Phase 1, in code)

`ibGuid` stays a **dumb primitive** — it knows bytes, never metaIDs. All branding lives in
the metaclass.

| Element | Role |
|---|---|
| `ibGuid::newGuid(version)` | mints a fresh guid **in storage form** — returns `ibGuidImpl` |
| `ibGuid(const std::array<16>&)` | **explicit** — wrapping raw bytes is a deliberate act |
| `ibGuid(const ibGuidImpl&)` | **implicit** — the storage dupe flows into an ibGuid freely |
| `ibGuidImpl::m_data1` | pinned `uint32_t` (+ `static_assert(sizeof(ibGuidImpl)==16)`) so the dupe is a portable 16-byte POD on every platform (plain `unsigned long` is 64-bit on LP64) |

`newGuid` returns `ibGuidImpl` so the metaclass takes the dupe **directly** and patches
Data1 with no intermediate `ibGuid`; general callers still write `ibGuid g = ibGuid::newGuid();`
via the implicit ibGuidImpl→ibGuid ctor.

### The one door — `ibValueMetaObject` (metaObject.h)

```cpp
// mint a fresh reference key to an instance of THIS metaobject
ibGuid NewReferenceGuid() const {
    ibGuidImpl impl = ibGuid::newGuid(GUID_TIME_BASED);  // storage dupe, no intermediate ibGuid
    impl.m_data1 = (uint32_t)m_metaId;                    // Data1 = metaID
    return impl;                                          // implicit -> ibGuid
}

// inverse: the metaID stamped in a key's Data1 (pure function, no instance)
static ibMetaID ReferenceMetaID(const ibGuid& key) {
    return (ibMetaID)ibGuidImpl(key).m_data1;
}
```

Branding at a single gate = the guarantee. There is deliberately **no** `ibMetaGuid`
subclass: a reference guid and a plain identity guid are the *same* type flowing through
the same fields (`ibUniqueKey::m_objGuid`, `ibValueDataObject::m_objGuid`), and a subclass
tag would be sliced back to `ibGuid` at every such boundary. The brand is a property of one
field, enforced at one door — not a type.

---

## Sorting — already type-first, matches the server

`ibGuid::operator<` routes through `guidValueCompare` (guid.cpp), which compares the bytes
in the field-normalized order `{3,2,1,0, 5,4, 7,6, 8..15}`. The first four steps are **all
of Data1**, so:

- different metaID → ordered by metaID (grouped by type),
- same metaID → falls through to the tail.

Exactly "metaID first, then the rest" — for free, because Data1 leads. The same normalized
order makes an in-memory sort equal the server's bytewise `ORDER BY` on the stored blob, so
the keyset cursor stays aligned with the DB.

**Trap:** reference sorting MUST go through `operator<` / `guidValueCompare`. A raw
`memcmp` on `ibGuid::_bytes` (Data1 big-endian) or on `ibGuidImpl` memory (Data1 native LE)
gives a *different* order and desyncs from the server.

Note: within a metaID the group order is LSB-first (to match the raw-byte server order), so
type groups are *grouped* but not in ascending-numeric metaID order. That is fine for
grouping / prefix range scans; numeric type ordering would require storing metaID big-endian
and is not done.

---

## Storage payoff (Phase 4, planned)

Once the guid is self-describing, the `_RRRef` metaID tail is redundant and drops: **20 → 16
bytes** per reference. Works for polymorphic columns too (the type is read from Data1).

Rough scale — 15 reference columns, 10⁹ rows: `15 × 4 B × 10⁹ = 60 GB` off the table, plus
~4 B per indexed reference in each index, plus denser pages (better cache / fewer page reads
on scans). It is a slice of a large table (20% of reference-column bytes), not a silver
bullet — but it compounds, and it is unlocked precisely by the branded, self-describing key.

The record's own PK is already a bare 16-byte guid (type implied by its table); branding it
adds self-describingness but saves no bytes.

---

## Remaining phases (planned)

- **Phase 2** — `using ibReference = ibGuid;` (drop the `{ibGuidImpl, metaID}` struct in
  `valueInfo.h`, `reference_size_t` → 16); readers of `.m_id` → `ReferenceMetaID`, `.m_guid`
  → the guid itself; drop the `m_reference_impl` heap allocation.
- **Phase 3** — mint gate: the new-object branch in `ibValueRecordDataObjectRef` (commonObject.cpp)
  goes through `m_metaObject->NewReferenceGuid()` instead of a bare `ibGuid::newGuid`
  (guard the null-metaObject path).
- **Phase 4** — `_RRRef` slot 20 → 16 in `columnLayout` + `ibColumnCodec` read/write; the
  type-prefix sentinels in `dbTableProvider`.
- **Phase 5** — reference reconstruction from the 16-byte key; a per-dialect helper to slice
  metaID out of a reference column in SQL (FB `SUBSTRING … OCTETS`, PG `substring`/`get_byte`,
  SQLite/MySQL `substr`). All type-extraction goes through `ReferenceMetaID`, not ad-hoc bytes.
- **Phase 6** — re-stamp on paste / type change (a new key is minted from the old tail + the
  new metaID; the copy-paste guid reset already exists as the hook).

---

## Out of scope / non-goals

- **Clustered indexes.** Firebird (the primary engine) has no clustered indexes at all; PG's
  `CLUSTER` is a non-maintained one-shot. A guid PK is the wrong shape for clustering (random
  → page splits), and on per-type tables the constant metaID prefix does not help. See the
  DDL (`databaseQueryBuilder.cpp`) — we emit plain `PRIMARY KEY` + `CREATE INDEX`, no
  `CLUSTERED`.
- **Global uniqueness.** Not a goal — uniqueness within a metaID is sufficient and correct.
