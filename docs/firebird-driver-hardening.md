# Firebird driver hardening — 2026-04-30

Audit and fix pass on `ibDatabaseLayerFirebird` and the supporting
`firebird*` files for embedded FB 5.0 deployment. The driver was
written against legacy ibase.h (`FB_API_VER 40`) and works against
FB 5.0 client (`fbclient.dll 5.0.3.1683` confirmed via VersionInfo).
Headers and plugin layout (`engine13`, `icu63`) are correct for FB 5.0
— legacy C-API and plugin engine version did not bump between 4.x and
5.x.

> **Status:** landed. Shipped in v1.3.0 (2026-05-12). No known
> regressions on FB 5.0 embedded; metadata-driven Apply paths
> exercise the changed code in desktop / wenterprise-server smoke
> runs.

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

**`HoldRowLocks` / `TryProbeRowLock` outer-TX guards.** Both refuse
when `IsActiveTransaction()` returns true — inner `BeginTransaction`
would just bump the counter, leaving the actual SELECT WITH LOCK to
run under the outer wait-mode TX. The lock would then live past
`ReleaseRowLocks` and the probe would block instead of fail-fast.
`ReleaseRowLocks` falls back to `RollBack` if `Commit` throws
(otherwise a stuck commit would pin the cluster row).

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

Added to `ibDatabaseLayer.h` (base struct). Drivers other than FB
ignore the flag — documented in the struct comment. PostgreSQL is
the next candidate to honour it (`SET TRANSACTION READ ONLY`),
straightforward to add when profile demands.

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
