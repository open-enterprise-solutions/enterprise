# Reference key = one guid (type + time + uniqueness)

**Status: guid primitive LANDED; the reference-key encoder AND the storage collapse (20→16) are in
code, uncommitted (build-verify pending on a stable tree).**

A reference to an object is a single **16-byte guid** that carries three things at once — no
side-channel, no separate metaID column: the same 16 bytes say *what type*, *roughly when*, and
*which one*. One primitive, consistency by construction.

```
 byte:  0  1  2  3 | 4  5  6  7 | 8  9 10 11 12 13 14 15
        └ Data1  ┘  └ Data2/3 ┘ └────────  Data4  ─────┘
        =  metaID     = seconds   =  64-bit random
           (type)      (order)       (uniqueness)
```

- **Data1 (4 bytes) = metaID** — the target's type. Leads the key → type grouping / SQL-slice, and
  it is the slot the reader extracts the type from.
- **Data2+Data3 (4 bytes) = a 32-bit second counter** — coarse time, for insert ORDER and index
  locality (~136-year range). It does NOT provide uniqueness (many rows share a second).
- **Data4 (8 bytes) = 64-bit random** — the uniqueness. Two concurrent same-second same-type inserts
  differ here. Uniqueness lives entirely in this tail; the timestamp only orders.

## Why this split (and not a plain v1/v4 uuid)

- The type must be embedded so a bare key is self-describing (drop the separate metaID storage).
- Concurrency needs a differentiator. **Uniqueness comes from the 64-bit random, NOT from timestamp
  precision** — two rows created in the same instant share the timestamp, so only the random tells
  them apart. Finer timestamps (µs/ns) don't help: the OS clock caps resolution (~µs), and the random
  is what actually distinguishes concurrent rows.
- **We take RANDOM bytes (v4 `UuidCreate`), never v1 `UuidCreateSequential`.** A v1 uuid keeps its
  fast-changing uniqueness bits in `time_low` = Data1 — exactly the slot we overwrite with metaID.
  Overwriting v1's time_low destroys uniqueness under concurrency (same-server rows within one ~7-min
  window collapse to identical tails). So the key is built from a v4 random guid; only Data1/Data2/Data3
  are stamped, Data4 stays the OS-random tail.

## Sizing (ERP-class)

- **metaID 4 bytes.** metaID is a *global* sequential id assigned to EVERY metaobject (via
  `GenerateNewID` in `OnCreateMetaObject`) — catalogs, documents, registers AND their attributes /
  tabular-section columns / forms / commands. A flagship ERP config reaches **millions** of metaobjects,
  so 3 bytes (16.7M) is too tight; 4 bytes (2.1B) is never a concern.
- **seconds 4 bytes** — 136-year range from a custom epoch (or Unix seconds, valid to 2106).
- **random 8 bytes** — 64 bits, bulletproof (below).

## metaID lifecycle & ceiling (AI-first)

`GenerateNewID` returns **max(live metaID) + 1** (`metadata.cpp`) — it never reuses a freed id. This is
deliberate: metaID is part of every stored reference, so it must **permanently** mean the same type;
reusing a deleted type's id would make old references point to the wrong type. The price of that
stability is that the ceiling **climbs cumulatively** with additions to a config (deletions leave gaps,
a fresh config resets from ~1000).

Ceiling now: `ibMetaID = int` (signed) → **~2.1 billion**. The guid's Data1 slot is already `uint32_t`,
so it holds the full unsigned range — **the type, not the key, is the limit**, and there is a clean 2×
growth lever available: switch metaID to `unsigned int` → **~4.29 billion**.

That growth is NOT free, though (an earlier note wrongly called it cheap): `wxNOT_FOUND` (−1) is the
entrenched "invalid / not-found" metaID sentinel in ~14 sites (`commandDescription`, `tabularSection`,
`metaData`, `modelDb` ×5, `modelRam`, `variantSource`, `srcDataObject`, …). Under `unsigned`, −1 becomes
`UINT_MAX` = the very top of the range, so the sentinel would **collide with the maximum valid metaID**.
Going unsigned therefore requires moving the sentinel to a non-colliding value (e.g. 0) across all those
sites plus a signed/unsigned sweep.

Reaching even 2.1 billion needs ~2 billion cumulative metaobject-additions to a SINGLE config; AI batch
generation could climb toward ~1 billion over a long-lived, heavily-iterated config — so headroom
matters — but 2.1 billion is sufficient. **Decision: keep signed `int` (2.1 billion) now; the room to
grow ~2× (unsigned, ~4.29 billion) is there for later, gated on the sentinel rework above.** Beyond that,
`int64` metaID + a guid rebalance remains the far-future path (≫ any conceivable need).

If some future extreme AI-churn scenario ever needed more, the path is a platform-wide `int64` metaID
plus a guid rebalance — e.g. `[metaID 6][sec 4][random 6]` (ceiling ~281 trillion, keeps time+random) or
`[metaID 8][random 8]` (drops the timestamp). Not needed now; recorded so the choice stays deliberate.

## The encoder — lives on the reference

`ibValueReferenceDataObject::MakeNewGuid(ibMetaID)` (`metaCollection/partial/reference/reference.h`)
is THE place a new reference key is minted — it lives on the reference itself, so the mechanism is
visible where references are defined. It works through **ibGuidImpl fields** (not raw bytes) so the
key lines up with the guid comparator and the stored-blob field order:

```cpp
static ibGuid MakeNewGuid(ibMetaID metaID) {
    wxASSERT_MSG(metaID != 0, "MakeNewGuid: reference key minted with a zero (unset) metaID");
    ibGuidImpl impl = ibGuid::newGuid(GUID_RANDOM);   // v4 random — Data4 stays the tail
    impl.m_data1 = (uint32_t)metaID;                  // Data1  : metaID (type)
    const uint32_t sec = /* system_clock seconds since epoch */;
    impl.m_data2 = (unsigned short)(sec >> 16);       // Data2  : seconds high
    impl.m_data3 = (unsigned short)(sec & 0xFFFF);    // Data3  : seconds low
    return impl;
}
```

Minted only for a **NEW reference value** (the new-object branch of `ibValueRecordDataObjectRef`,
`commonObject.cpp`). Loaded / copied guids are kept as-is (already minted). A **zero metaID asserts** —
the type must be set before a key exists.

Scope: **reference objects only** — catalogs, documents, charts. **Registers** (accumulation /
information / accounting) use composite keys (dimensions), never mint through here.

The metaobject's OWN guid (`m_metaGuid`) is a separate, config-level identity — it is **not** branded;
it keeps its plain generation.

## Collision — bulletproof

A collision needs all three to match: **same type AND same second AND same 64-bit random**. It reduces
to a birthday on 64 bits **within one (type, second) bucket**: `P ≈ N² / 2⁶⁵`, N = same-type inserts in
that second.

**Key property:** collisions compete only inside a (type, second) bucket, so the risk depends on the
**peak inserts/sec into a single type — NOT on total DB size.** The database may hold trillions of rows.

| Rate into ONE type | collisions over 10 years |
|---|---|
| 1 000 /sec        | ~8.5×10⁻⁶ |
| 10 000 /sec       | ~8.5×10⁻⁴ |
| 100 000 /sec      | ~8.5×10⁻² |
| 1 000 000 /sec    | >1 |

A 50% chance within one second needs ≈ 2³² ≈ 4.3 billion inserts of one type in a single second —
physically impossible. For any realistic ERP load (stream spread across types, ≤1000/sec per type),
collision is ~10⁻⁵ over a decade, i.e. zero.

## Sorting

`ibGuid::operator<` (`guidValueCompare`) and the server's `ORDER BY` on the stored blob are aligned
(field-normalized to match). metaID in Data1 → references **group by type** automatically (same type =
identical leading bytes = adjacent). Within a type they cluster by second, and the random tie-breaks —
invisibly to the user, who never enters sub-second precision.

Caveat: the comparator matches the little-endian stored-blob order, so within a type the second is
*clustered* but not strictly numeric-ascending. A clean ascending time-sort would require storing the
blob big-endian — a codec change deferred to the storage phase; it is a query-locality nicety, not
correctness.

## Storage collapse 20 → 16 — DONE (in code)

The metaID tail is gone. `ibReference` now holds only the 16-byte guid; its ctor stamps Data1 from `id`
(real keys — already branded — are a no-op; metaID-only sentinels land right), and `GetMetaID()` reads
the type back from Data1. `reference_size_t` → 16 drives the DDL column, the blob write
(`SetParamBlob(…, sizeof(ibReference))`) and the read reconstruction (`CreateFromPtr` / `Create` via
`GetMetaID`) — all size-driven, no 20-byte / offset-16 assumptions. The reference value comparators use
the guid alone (metaID is in Data1; the old metaID tiebreak was removed).

Query-engine consistency holds: `guidValueCompare` matches the server's bytewise `ORDER BY` on the blob,
so reference `=` / `<` / `ORDER BY` / `JOIN` push down to SQL with identical results — a clean 16-byte
key is directly DB-comparable.

## Still open (deferred, not correctness)

- **Sort-order alignment (cosmetic).** In-memory order == server order and groups by type, but it is
  little-endian-scrambled (not strictly numeric-ascending time within a type). A clean ascending sort
  would need storing the blob big-endian + changing the comparator — not worth it; range scans (exact
  type prefix) and keyset paging already work on the current consistent total order.
- **SQL-side metaID slice (unused).** Type filtering already uses the `_RTRef` clsid column, not a blob
  slice; a `substring(key,1,4)` helper is only worth adding when a query actually needs it.

## Non-goals

- Branding the metaobject's own guid (`m_metaGuid`) — it is config identity, not a stored data reference.
- Register keys (composite dimensions) — no reference guid to brand.
- Global uniqueness / finer-than-second timestamps — neither is needed; uniqueness is the 64-bit random,
  and it is scoped per (type, second), which is sufficient and correct.
