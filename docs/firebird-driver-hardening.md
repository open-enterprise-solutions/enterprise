# Firebird driver hardening

> **Status:** LANDED 2026-04-30, shipped in v1.3.0 (2026-05-12). No
> known regressions on FB 5.0 embedded; metadata-driven Apply paths
> exercise the changed code in desktop / wenterprise-server smoke
> runs. Three follow-ups parked (see end-of-doc).
>
> ⚠ **A SECOND PASS landed 2026-08-13/14** — the WRITE path this time, plus a blob read that could
> loop forever. It found a hundredfold understatement on ordinary money columns, a write past the end
> of a parameter buffer, and a reference losing its type; the account is
> [§ *Second pass*](#second-pass--2026-08-1314-the-write-side-and-a-blob-read-that-could-hang) at the
> end of this file. Read the sections below as the v1.3.0 audit, which is what they are — "no known
> regressions" was true of what that pass looked at, and the write side was not it.

Audit and fix pass on the FB driver — class `ibDatabaseLayerFirebird`
(`databaseLayer/firebird/firebirdDatabaseLayer.h`) and the supporting
`firebird*` files — for embedded FB 5.0 deployment. The driver is
written against legacy `ibase.h` (`FB_API_VER 40`) and works against
the FB 5.0 client. Headers and plugin layout (`engine13`, `icu63`) are
correct for FB 5.0 — legacy C-API and plugin engine version did not
bump between 4.x and 5.x.

> Class-name note: the driver classes use the **suffix** pattern —
> `ibDatabaseLayerFirebird`, `ibDatabaseResultSetFirebird`,
> `ibPreparedStatementFirebird`, `ibPreparedStatementFirebirdWrapper`,
> `ibDatatabaseParameterFirebird` /
> `ibDatatabaseParameterFirebirdCollection` (the `Datatabase` typo is
> in the actual type name), `ibInterfaceFirebird`. The `firebird*.cpp`
> filenames below are file references, not class names.

---

## Why this needed doing

Driver had several long-tail correctness landmines that didn't
detonate in normal use but would silently corrupt data or crash on
specific code paths:

- `XSQLDA` allocated with `malloc` was being released with `delete[]`
  via `wxDELETEA` — UB on every result-set close. Worked because the
  malloc allocator hadn't reused the block yet on most platforms.
- Result-set integer extraction silently truncated `SQL_INT64` /
  `SQL_INT128` to 32-bit `long` on Windows even though the function
  returns `long long`, and applied wrong scale arithmetic
  (`abs(scale) * 10` instead of `pow(10, |scale|)`) on any `numeric`
  with scale ≥ 2.
- String parameter binding used `wxStrncpy` over a UTF-8 byte buffer
  — copies wide-char-aligned half the bytes on Windows.
- Connection charset was never set on attach (`isc_dpb_set_db_charset`
  is honoured only at CREATE), so existing databases returned bytes
  in the engine's NONE charset → mojibake on Cyrillic text.
- Function-pointer table on `ibInterfaceFirebird` was uninitialised —
  partial `Init()` failure left dangling addresses for later
  invocations.
- `m_pageSize` typed `int16_t` — would sign-overflow if anyone bumped
  to 32768.
- Transaction handle stored on a singly-linked-list "stack" structure
  that base-class nesting counter made redundant. Every failed
  `BeginTransaction` leaked a node.
- `HoldRowLocks` / `TryProbeRowLock` would silently misbehave if
  called inside an outer transaction (inner `BeginTransaction` only
  bumps the counter; the SELECT WITH LOCK then runs under wait-mode
  TX and locks live until the outer commit).

---

## Changes by file

### `firebirdResultSet.cpp`

**Integer extraction rewritten in `GetResultLong/Double/Number`.**
Replaced `*(long*)(pVar->sqldata)` casts with `memcpy` into the
correctly-sized `int32_t` / `int64_t` / `short` / `float` / `double`.
On Linux LP64 `long` is 8 bytes but FB `SQL_LONG` is 4 — the old
cast over-read on Linux. Fixed scale math:

```cpp
// Was: nReturn /= abs(nScale) * 10;     // only correct for |scale|==1
// Now: for (short i = 0; i < -nScale; ++i) nReturn /= 10;
```

Removed silent `static_cast<long>(int64_t v)` truncation in
`SQL_INT64` / `SQL_INT128` paths — `long long` return type now
preserves full 64-bit value. `INT128` still drops the high 64 bits
(documented limitation; full precision goes through `GetResultNumber`
via `From128Bytes`).

**`FreeFieldSpace`:** removed `wxDELETEA(m_pFields)` — `XSQLDA` is
malloc'd in `firebirdDatabaseLayer::DoRunQueryWithResults` and
`firebirdPreparedStatementWrapper::Prepare`, so `delete[]` is UB.
Outer `Close()` already calls `free(m_pFields)`, so the inner free
is just nulled.

### `firebirdParameterCollection.cpp`

**`AllocateParameterSpace`:** swapped `new wxChar[N]` to `new char[N]`
for `SQL_TEXT` / `SQL_VARYING` / `SQL_INT64` / `SQL_INT128`. On
Windows `wxChar == wchar_t == 2 bytes` → over-allocated 2× and
freed via type-mismatched `delete[] (char*)` (UB).

**`FreeParameterSpace`:** replaced `wxDELETEA(m_FirebirdParameters)`
with `free()` (XSQLDA is malloc'd by the wrapper).

**`ResetBlobParameters`:** added null-slot guard plus null-bound
check (`sqlind != nullptr && *sqlind < 0`). Previously dereferenced
null pointer when caller bound NULL on a BLOB position.

### `firebirdParameter.cpp`

**`ibDatatabaseParameterFirebird(... wxString)` ctor:** replaced
`wxStrncpy((wxChar*)sqldata, (wxChar*)valueBuffer, length)` with
`memcpy(sqldata, valueBuffer, length)` and an explicit
`pVar->sqllen = (ISC_SHORT)length;` mutation. The buffer is UTF-8
bytes; treating it as wide chars copied half the data and wrote
past the allocated buffer on long strings.

**No clamp by `pVar->sqllen` on the memcpy** — it would look like
defensive programming but is actually a regression. After the first
bind into a parameter slot, `sqllen` no longer reflects the column's
max length; it carries the previous bound string's length. A clamp
`n = min(length, sqllen)` then truncates every rebind by the
*previous* value — first empty bind into a slot pins `sqllen = 0`
and every subsequent bind into the same slot writes zero bytes.
Surfaced 2026-05-08 as a TabularSection-save regression introduced
by `7494bae5` (5 rows saved with empty `_S` columns, ts-load
returned loaded=5 + isEmpty=1 for all). The buffer allocated by
`AllocateParameterSpace` reserves the column's declared maximum
(`+1` for SQL_TEXT, `+3` for SQL_VARYING) so an unclamped memcpy of
a string within column limits cannot overflow; FB itself rejects
oversized binds. See memory note `reference_fb_string_param_clamp`
for the full incident write-up — clamp must not be reintroduced on
any future driver refactor.

### `firebirdInterface.h`

All function-pointer members default-init to `nullptr`. Previously
uninit; partial `Init()` failure left random stack garbage.

### `firebirdDatabaseLayer.h` + `.cpp`

**Stack of transaction nodes (`fb_tr_list`) removed.** Base-class
nesting counter (`m_txDepth`) guarantees `DoBeginTransaction` /
`DoCommit` / `DoRollBack` fire only on 0↔1 transitions, so the
"stack" was always one node above a sentinel. Replaced with a
single `isc_tr_handle m_pTransaction = 0`. Constructors no longer
allocate a sentinel; dtor no longer needs to free it; all six
constructors collapsed in size; failure-path leak in
`DoBeginTransaction` is gone (no node to allocate).

**Page size widened to `int32_t`** and bumped from 8192 → 16384.
FB 5.0 OLTP recommendation; cache footprint at 16K × 2048 pages =
32 MB per attach is acceptable. Encoded via `uint32` shift to avoid
sign-overflow if anyone bumps to 32768.

**DPB block additions in `Open`:**

- `isc_dpb_lc_ctype = "UTF8"` — connection charset. Without it
  ATTACH falls back to NONE; existing databases returned raw bytes
  for non-ASCII columns (Cyrillic mojibake). `isc_dpb_set_db_charset`
  was already there but only honoured at CREATE.
- `isc_dpb_force_write = 0` — async writes for fresh-CREATE
  databases. ~5–10× faster than synchronous `FlushFileBuffers` per
  page on Windows NTFS. Existing DBs ignore (flag stored in header
  — flip with `gfix -w async` or `ALTER DATABASE SET WRITE = ASYNC`).
- `isc_dpb_session_time_zone = "UTC"` — pins TIMESTAMP WITH TIME
  ZONE handling to UTC regardless of OS time zone.

**Mkdir in `Open` recursive.** `wxMkDir` silently fails on
multi-level paths, leaving FB to report "I/O error during open".
Switched to `wxFileName::Mkdir(... wxPATH_MKDIR_FULL)`.

**`Open` is now safe to re-call.** If `m_pDatabase` is non-zero
(layer previously opened), `Close()` runs first to detach the old
handle. Avoids handle leak + invisible-second-attach surprise.

**`DoBeginTransaction` TPB rewritten:**

- Wait-mode: added `isc_tpb_lock_timeout = 30s`. Without it
  contention blocks indefinitely (UI hangs, daemons stall). 30 s
  gives transient contention room to resolve while flagging stuck
  peers.
- Read-only mode (new, opt-in via `ibTxOptions::readOnly`): uses
  `isc_tpb_read + read_committed + read_consistency`. FB 4+
  `read_consistency` gives statement-level snapshot inside
  read-committed without acquiring write-intent locks. SELECT-heavy
  paths benefit; default callers are unchanged.
- Failed `isc_start_transaction` now releases the new TX node before
  throwing (was leaking on every failed Begin).

**`DoCommit` / `DoRollBack` simplified.** No stack pop; clear
`m_pTransaction` whether the FB call succeeds or not (FB invalidates
the handle either way; preserving the old value risked double-free
on a stray follow-up call).

**`HoldRowLocks` / `TryProbeRowLock` / `ReleaseRowLocks` — REMOVED.** All three are gone
from the tree (only retirement comments remain in `session/sessionRegistry.*` and
`session/designerExclusivePolicy.*`); session liveness moved to a heartbeat on `lastActive`
instead of a held row lock. See [session-registry.md](session-registry.md) §4.

*Historical rationale, kept because the reasoning still applies to anything that takes a
lock inside a wrapper:* both refused when `IsActiveTransaction()` returned true — an inner
`BeginTransaction` would just bump the counter, leaving the actual SELECT WITH LOCK running
under the outer wait-mode TX, so the lock would outlive the release and the probe would block
instead of failing fast.

### `firebirdPreparedStatementWrapper.cpp`

**Skip `isc_info_sql_records` after Execute on SELECT.** That call
is a separate round-trip; on a SELECT it returns zero counts (no
DML modifications), so the cost is pure overhead. Now gated on
`!IsSelectQuery()`. Saves one round-trip per SELECT.

---

## Optimisations that landed but are opt-in

`ibTxOptions::readOnly` is wired through the base-class signature
and recognised by the FB driver. Other drivers silently ignore (no
behaviour change). To benefit, callers on read-heavy paths
(reports, list views, dashboards) need to start passing
`BeginTransaction({.readOnly = true})`. Nothing in OES does this
yet — feature is parked, ready when there's profile-driven motivation.

---

## What was NOT changed and why

- **SQL_BOOLEAN parameter handling:** `SetParamBool` stores `int` and
  FB reads only the low bytes. Endian-luck on x86/x64 makes it
  correct in practice. Proper 1-byte `FB_BOOLEAN` storage skipped
  until there's a concrete failure case.
- **Default `sysdba/masterkey` credentials in default constructor.**
  Hardcoded but works with FB 5.0 default `AuthClient =
  Srp256, Srp, Win_Sspi, Legacy_Auth` via Legacy_Auth fallback.
  Any caller passing real credentials overrides them.
- **MSSQL `INDEXED VIEW` / Oracle `ON COMMIT MV` driver-specific
  read-paths.** Trigger-maintained totals (see
  `register-totals-strategy.md`) is the cross-DB design; native
  materialisation only matters when one driver's profile is
  measurably constrained, not now.

---

## Cross-DB note: `ibTxOptions::readOnly`

Added to `databaseLayer.h` (base struct — the file is named after the header, not after the
class `ibDatabaseLayer` it declares). Drivers other than FB
ignore the flag — documented in the struct comment. PostgreSQL is
the next candidate to honour it (`SET TRANSACTION READ ONLY`),
straightforward to add when profile demands.

---

## Second pass — 2026-08-13/14 (the WRITE side, and a blob read that could hang)

> Kept as a section of its own rather than folded into the sections above, so the account of the
> v1.3.0 release stays an account of that release. Everything here was found AFTER it shipped, by
> running the accounting register's first live applies against FB 5 embedded.
>
> **The shape of this pass differs from the first.** The 2026-04-30 audit fixed reads that returned
> wrong values. This one is mostly the WRITE path, where the same class costs more: a read that is
> wrong is visible the moment somebody looks, while a write that is wrong is durable, and the value
> it destroyed is not recoverable from the row it produced.

### The rule this pass keeps re-teaching

⭐⭐ **A number that means "I could not establish this" must not share a variable with a number that
means a measured zero.** Three of the six defects below are that collapse, in three different places.

### `firebirdParameter.cpp`

| Defect | What it did |
|---|---|
| **A string was copied without regard for the buffer's size** | `sqldata` is allocated when the statement is DESCRIBED, sized from the parameter's *declared* width; the value's encoded length is an unrelated number. A string longer than the column ran straight past the allocation, and the heap corruption surfaced wherever the next allocation happened to live — never at the write. The clamp must also come BEFORE the assignment that replaces `sqllen` with the value's own length, or it clamps against what it just wrote |
| ⚠ **…and the first version of that clamp emptied every value** | Written as `capacity = sqllen > 0 ? sqllen : 0`, it read an *undescribed* parameter (`sqllen == 0`) as "no room at all" and clamped every value to nothing — a string went in, the write reported success, and the field came back EMPTY. Caught on a chart of accounts whose code survived (written through another path) while its description was blank after every save. Zero meant **unknown**, not **no room**; the clamp now applies only where the room is known |
| **Scale was applied in two of the four integer branches** | FB stores `NUMERIC(p,s)` as an integer with the scale remembered beside it. `SQL_INT64` / `SQL_INT128` multiplied by `10^-sqlscale`; `SQL_SHORT` / `SQL_LONG` did not — and FB maps `NUMERIC(1..4,s)` to SMALLINT and `NUMERIC(5..9,s)` to INTEGER, so an ordinary `Number(9,2)` money declaration took the unscaled branch. **1234.56 was written as 1234 and read back as 12.34** — a hundredfold understatement, silent, on the default driver's write path. The multiplication is now written once (`scaledForStorage`) |
| **`ToInt()` with no argument CLAMPS, and the clamped value then wraps** | At INT_MAX, then modularly into a 16-bit field: **40000 stored as −25536**, a sign flip on money. Replaced by a checked narrowing that refuses — a value that does not fit the column cannot be written, and writing a different one is worse than refusing |
| **The return code of `ToInt` was ignored on `SQL_INT64`** | On overflow it leaves its out-param UNTOUCHED, so the branch wrote a **zero** — and this branch carries more than money: `_RTRef` holds an `ibClassID`, an unsigned 64-bit value whose plugin range and raw-FNV ids have the top bit set. A reference whose type stored as 0 is a row whose identity cannot be read back. A value past `INT64_MAX` is not an error here — it is 64 bits that are not a signed number, and a BIGINT column holds 64 bits either way, so it is stored as the same bit pattern (exact round trip) and only a value fitting NEITHER reading is refused |
| **A null parameter buffer was checked in one place** | `RequireParameterBuffer()` is now called by every branch that writes into a DESCRIBE-allocated buffer (`SQL_INT64`, `SQL_INT128`, `SQL_TEXT`, `SQL_VARYING`). A null there is a bind addressing a parameter the statement does not have, and it used to arrive as an access violation inside the CRT with nothing on the stack naming a column |

The refusal messages carry the offending value, and it rides as an **argument**: `Error` is a
printf-style vararg, so a description handed in as the format string reads any `%` inside it as a
specifier ([exceptions.md](exceptions.md)).

#### ⏳ The clamp is not finished, and the reason is worth stating

`sqllen` carries **two opposite facts**: at DESCRIBE it is the column's declared width, which is what
`AllocateParameterSpace` sizes the buffer from; after the first bind it is the length of the last
value written, because the ctor must put it there — FB requires that for an input `SQL_TEXT`. The
capacity is then recorded **nowhere**, and the same `XSQLVAR` is reused for every bind at that
position (the allocation happens once, in the collection's ctor).

So the clamp reads a field that has stopped meaning capacity:

```
DESCRIBE:  sqllen = 50 (column width), buffer 51
bind #1 "abc"       -> no clamp (3 < 50), memcpy, sqllen = 3
bind #2 "abcdefgh"  -> 8 > 3  =>  TRUNCATED to "abc"
```

The `> 0` guard covers an *undescribed* parameter, not a *short previous* one. This is the third trip
round the same loop: the clamp was added 2026-04-30, reverted 2026-05-08 when re-binding truncated
(a batch insert wrote five rows with empty strings), and restored now because writing past the buffer
is worse. Both behaviours are wrong in the other's case, which is the signature of one number holding
two facts.

**The fix is not to choose** — it is to record the capacity at `AllocateParameterSpace` (a parallel
vector on the collection, or a field handed to the parameter's ctor) and clamp against that. Until
then, anyone touching this must check BOTH: a string longer than the column (must not write past the
allocation) and two different binds into one position (the second must not be cut down to the first).
The `SetParamBlob` `SQL_TEXT` / `SQL_VARYING` branches read `cap` from the same field and inherit the
same limitation.

### `firebirdResultSet.cpp` — reading a blob

| Defect | What it did |
|---|---|
| **The status of `isc_open_blob2` was dropped** | Everything after it read as though the blob were open. `isc_get_segment` on a null handle fails, but the loop's second condition consults `m_Status[1]`, which on that path still holds the code from whatever ran BEFORE the call — and when that leftover happened to be `isc_segment`, **the loop never ended**, appending the same uninitialised segment forever. A blob that cannot be opened has to say so, not hang |
| **`nSegmentLength` was uninitialised and used as a LENGTH** | A failed read leaves it untouched, and the buffer was sized from whatever was on the stack; the loop then appended that many bytes of it |
| **`isc_segstr_eof` was not told apart from a real failure** | The ordinary end of a blob and an error both ended the read, so a genuine failure could return a short buffer that reads exactly like a valid blob |
| **The status was interpreted after `isc_close_blob`** | The close writes its own outcome into the same status vector, so the report described how the CLOSE went and lost why the READ stopped. The failure is read first, then the blob is closed |

### What this pass says about the first one

The 2026-04-30 audit fixed the scale arithmetic on the READ side (§ *Changes by file*,
`firebirdResultSet.cpp`) and did not ask the same question of the write side, where the answer was
different in two of four branches. The lesson generalises past this driver and is the same one the
totals guard produced independently on the same days: **when a rule is found wrong in one direction,
the sweep is every place that expresses it, not every place that reads it.**

---

## Open follow-ups

1. **Self-heal for partial-Apply seed failures.** The atomic seed-TX
   that this session added closes the half-filled-table window
   (rollback restores empty table). The "table created but empty
   forever" edge case (total seed failure → metadata reload sees
   `foundedMeta != nullptr` → seed never reruns) still exists. Lift
   requires splitting `repairMetaTable` into `seedMetaTable`
   (data-only, idempotent) and keeping `repairMetaTable` (schema,
   gated to new objects). Constants currently overload the single
   flag for ALTER TABLE ADD COLUMN deferred-DDL, which is not
   idempotent — that's why the gate stays.
2. **SQL_BOOLEAN parameter:** revisit if FB 5 introduces strict-mode
   binding that catches the int-instead-of-byte case.
3. **Driver compile clean on Linux:** the new `int32_t` casts in
   `firebirdResultSet` are LP64-correct, but no CI catches a real
   Linux build at the moment. Verify on first cmake-on-clang run.
