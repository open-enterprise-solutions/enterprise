# Record-write protection — DB row locks + DataVersion

> **Update 2026-06-16 — row-lock clause unified onto the dialect dictionary; liveness-probe legacy removed.**
> The per-driver `ibDatabaseLayer::RowLockHint()` / `NoWaitClause()` virtuals (and their 4 driver
> overrides) are **deleted**. The row-lock clause now lives solely on the dialect dictionary:
> `m_rowLockSuffix` (` FOR UPDATE` / FB ` WITH LOCK` / SQLite empty) plus the new
> `m_rowLockNoWaitSuffix` (` NOWAIT` on PG/MySQL; empty on FB/SQLite — their non-blocking acquire rides
> the TX `noWait`/TPB). A pessimistic SELECT is expressed at L2 as `ibQueryIR::m_lockForUpdate`
> (+ `m_lockNoWait`); the renderer appends the dialect clause to the top-level SELECT. `ibLockManager`
> and the Constant row-lock now go through this L2 path (where the old code referenced `RowLockHint()`
> below, read "the dialect `m_rowLockSuffix`"). Separately, the **pessimistic-row-lock-for-liveness**
> legacy — `TryProbeRowLock` and `HoldRowLocks` / `ReleaseRowLocks` (virtuals + firebird impl + the
> registry probe connection) — is **removed**; cluster liveness has long been snapshot +
> heartbeat-on-`lastActive`. (Part of the L1→L2 tier-hygiene arc — query-language-arc.md §24.)

> **Update 2026-06-09 — `LockAndCheckDataVersion` moved onto the L3 door.**
> The hand-rolled `ReadDataVersionForUpdate` (raw `SELECT <DataVersion> FROM <tbl>
> WHERE uuid = ? <RowLockHint>` + `GetResultString`) is **gone**. The version-lock
> read is now one L3 selection:
> `From(record).Where(ibRawDBColumn::String("uuid"), guid)` with
> `ibReadPageRequest::m_lockForUpdate = true` — the renderer appends the dialect
> `m_rowLockSuffix` (== the per-driver `RowLockHint()`), and the door runs on the
> session holder (the SAME connection as the open `BeginWriteScope` TX), so the lock
> is held to commit. The version reads through `sel.GetValue(dvAttr)`.
> **Why it had to move:** DataVersion's SQL field name is the *composite*
> `<fld>_TYPE,<fld>_S` (type tag + string), so the old raw `GetResultString(that)`
> looked up a column literally named with a comma → "field not found" at runtime.
> The provider's attribute assembly (`GetValueAttribute`, TYPE+S) is the only correct
> read. Validated on FB (`SELECT … WHERE uuid=? ORDER BY uuid WITH LOCK` is accepted —
> FB permits `ORDER BY` with `WITH LOCK`). Part of the reference-as-key arc.
>
> **Status:** **LANDED** 2026-05-24, smoke-validated on Debug|x86
> (two enterprise.exe sessions editing the same Catalog item —
> second Save throws ibBackendLockException::VersionChanged, dialog
> surfaces "data was changed by another user"). Build clean.
>
> What landed: phases 1-8 below — `ibDataVersion` stamp generator,
> `ibBackendLockException`, `RowLockHint()`/`NoWaitClause()` virtuals
> + 4 driver overrides (FB / PG / MySQL / ODBC), `LockAndCheckData
> Version` on mutable-ref base wired into Catalog/Document/ChartOf*
> Write+Delete, `LockByKeys` on register-set base wired into 3
> register types Write+Delete, inline single-row lock on Constant
> Write, web HTTP `ExceptionToJson` shape + `OES.handleBackendError`
> JS dispatcher, desktop `ibBackendLockException` Alert in toolbar +
> docview save paths, fix to subclass `FillArrayObjectByPredefined
> Attribute` lists (DataVersion was declared on base but missing
> from subclass lists — pre-existing bug closed in passing).
>
> What's deferred (not blocking — alpha audience is 2 users):
> auto-migration of `fld1042` columns on existing DBs (Designer
> Apply handles it), HTTP status codes (still 200 even on error —
> JSON `error` field carries the kind), gtests for parallel-write,
> localized ru/uk message catalogs. Revisit when audience grows or
> sys_lock upgrade comes (see "Planned upgrade path" below).
>
> Original proposal section follows for reference. The `DataVersion`
> column was already declared on `ibValueMetaObjectRecordDataMutableRef`
> (`commonObject.h:729`, `String(12)`) before this arc — this design
> lit it up.
>
> **TL;DR.** We lean on the database. Two cheap protections layered:
>
> 1. **DB-side `SELECT ... FOR UPDATE` / `WITH LOCK`** during the Write
>    transaction — the database itself serializes writers against the
>    same row. Auto-released on Commit/Rollback. No app-side table, no
>    sweep, no admin UI.
> 2. **`DataVersion` stamp check** within the same TX — catches the
>    lost-update window if anything bypasses the row-lock (direct
>    `db_query` SQL, plugins, future schema split). Backup defense.
>
> What this **doesn't** give: proactive "edited by user X" on form-open.
> Conflicts surface only at Save as *"Object was changed by another
> user, please reload"*. For 3-30-user shara workloads this is the right
> tradeoff — the proactive-lock infrastructure (`sys_lock` table,
> `ibLockManager`, cluster sweep, admin UI) is ~3000 lines of code that
> only pays off above 100 concurrent editors. Revisit when "lost N
> minutes of editing" complaints actually accumulate.

---

## What we're protecting against

Two failure modes on concurrent writes:

| Failure | Without protection | With protection |
|---|---|---|
| **Lost update** — A and B read the same row, both write, B silently overwrites A. | Silent data corruption. | DataVersion mismatch on B's write → exception. |
| **Mid-write race** — A's TX SaveData runs concurrently with B's TX SaveData on the same row. | Driver-specific; on some configurations both succeed and last-physical-write wins. | `FOR UPDATE` serializes: B waits for A's Commit, then re-reads, then DataVersion check fires. |

Reads (form-open read-only, list views, reports, eval expressions) are
**not protected and don't need to be** — they run at the driver's
default isolation, return latest committed data, never block, never
get blocked.

## Coverage scope

Three metaobject bases — confirmed 2026-05-24:

| Base | Subclasses | Protection |
|---|---|---|
| `ibValueMetaObjectRecordDataMutableRef` | Catalog, Document, ChartOfAccounts, ChartOfCharacteristicTypes | DataVersion (already declared) + `FOR UPDATE` on Write |
| `ibValueMetaObjectRegisterData` | InformationRegister, AccumulationRegister, AccountingRegister | `FOR UPDATE` on the recorder row (which is always the owning Document — see "Registers" below) |
| `ibValueMetaObjectConstant` | All Constants | `FOR UPDATE` on the single `_const` row (serializes all constant writes — acceptable, constants are rarely written) |

Enumeration is read-only at runtime — out of scope.

## Two-layer protection at Write

The same pattern in every mutating method
(`WriteObject` / `DeleteObject` / `WriteRecordSet` / constant write):

```cpp
bool ibValueRecordDataObjectXxx::WriteObject()
{
    if (appData->DesignerMode()) return true;

    ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
    if (!scope || !scope->IsOpen())
        ibBackendCoreException::Error(_("Database is not open!"));

    if (!ibBackendException::IsEvalMode()) {
        if (!m_metaObject->AccessRight_Write()) {
            ibBackendAccessException::Error();
            return false;
        }

        scope.SafeBeginTransaction();   // counter-based nested TX

        // -------- LAYER 1: DB row lock --------
        // Pin the row for the rest of this TX. Concurrent writers
        // block here (or fail-fast under NoWait). Released by Commit.
        wxString dbVer;
        if (!ReadDataVersionForUpdate(scope, dbVer)) {
            scope.SafeRollBackTransaction();
            ibBackendCoreException::Error(_("Failed to lock object row"));
            return false;
        }

        // -------- LAYER 2: DataVersion check --------
        if (!m_loadedDataVersion.IsEmpty() && dbVer != m_loadedDataVersion) {
            scope.SafeRollBackTransaction();
            ibBackendLockException::VersionChangedThrow(
                m_metaObject->GetSynonym(), m_loadedDataVersion, dbVer);
        }

        // -------- bump stamp before SaveData --------
        m_currentDataVersion = ibDataVersion::NewStamp();
        // SaveData picks up m_currentDataVersion when building UPDATE.

        // -------- existing flow unchanged --------
        ibValue cancel;
        m_procUnit->CallAsProc(wxT("BeforeWrite"), cancel);
        if (cancel.GetBoolean()) { scope.SafeRollBackTransaction(); return false; }

        if (!SaveData()) {
            scope.SafeRollBackTransaction();
            ibBackendCoreException::Error(_("Failed to write object in db!"));
            return false;
        }

        m_procUnit->CallAsProc(wxT("OnWrite"), cancel);
        if (cancel.GetBoolean()) { scope.SafeRollBackTransaction(); return false; }

        scope.SafeCommitTransaction();
        m_loadedDataVersion = m_currentDataVersion;   // next Write compares against this
    }
    return true;
}
```

`ReadDataVersionForUpdate` is the per-driver wrapper:

```cpp
bool ReadDataVersionForUpdate(ibConnectionScope& scope, wxString& outVer)
{
    const wxString hint = ibSqlDialect::ForCurrent().RowLockHint();
    // FB: "WITH LOCK", PG: "FOR UPDATE", MSSQL: "WITH (UPDLOCK, ROWLOCK)",
    // MySQL: "FOR UPDATE", SQLite: "" (single-writer naturally)

    auto stmt = scope->PrepareStatement(wxString::Format(
        wxT("SELECT %s FROM %s WHERE %s = ? %s"),
        kDataVersionColumn, m_metaObject->GetTableName(),
        kPkColumn, hint));
    stmt->SetParamBlob(1, m_objGuid.ToBytes(), 16);
    auto rs = stmt->RunQueryWithResults();
    if (!rs || !rs->Next()) return false;
    outVer = rs->GetResultString(1);
    return true;
}
```

NOWAIT vs wait mode: caller sets `scope.SafeBeginTransaction({.noWait =
true})` to fail-fast, default is wait (driver's normal lock wait
behavior — FB has `isc_tpb_lock_timeout = 30s` already plumbed). Most
business code uses the default.

## DataVersion lifecycle

Existing `String(12)` predefined attribute on
`ibValueMetaObjectRecordDataMutableRef`. Three additions:

**Capture on Read.** `LoadObject(ref)` already SELECTs all columns
including `DataVersion`. Add a member `wxString m_loadedDataVersion`
on `ibValueRecordDataObject`; populate it during LoadObject from the
result row.

**Bump before Write.** `ibDataVersion::NewStamp()` returns a fresh
12-char base-62 string. Format: `[ms_since_epoch][counter]` packed —
chronologically sortable, collision-safe across cluster (HLC-inspired
but no need for full HLC since DataVersion isn't read by anyone but
the version check, ordering doesn't matter — only equality):

```cpp
// backend/utils/dataVersion.{h,cpp}
namespace ibDataVersion {
    BACKEND_API wxString NewStamp();  // 12-char base-62
}
```

Implementation: `(wxGetUTCTimeMillis() << 16) | nextProcessCounter()`
encoded base-62. Process counter is `std::atomic<uint16_t>` rolling.

**Check on Write.** Compared against `m_loadedDataVersion`; mismatch →
`ibBackendLockException::VersionChangedThrow`. New objects start with
`m_loadedDataVersion = wxEmptyString` so the first Write skips the
check (no prior version to compare).

**No DataVersion on Registers or Constants.** Register records are
written atomically by recorder (DELETE-then-INSERT for the whole
recorder bucket); there's no read-edit-write diff cycle on individual
register records, so a per-record version stamp protects nothing.
Constants are atomic-write-per-call from script; the `FOR UPDATE` on
the singleton row covers the only race window.

## Constants

Stored in a single-row, column-per-constant table (`_const` or
similar). Write protection:

```cpp
bool ibValueConstantDataObject::WriteValue(const wxString& constName,
                                            const ibValue& val)
{
    ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
    scope.SafeBeginTransaction();

    // FOR UPDATE on the constants row — serializes all constant writes,
    // but constant writes are rare so this is fine.
    auto rs = scope->RunQueryWithResults(wxString::Format(
        wxT("SELECT 1 FROM %s %s"),
        kConstTableName, ibSqlDialect::ForCurrent().RowLockHint()));
    if (!rs) { scope.SafeRollBackTransaction(); return false; }

    auto stmt = scope->PrepareStatement(wxString::Format(
        wxT("UPDATE %s SET %s = ?"),
        kConstTableName, GetColumnFor(constName)));
    BindParam(stmt, 1, val);
    stmt->RunQuery();

    scope.SafeCommitTransaction();
    return true;
}
```

Concurrent writes to *different* constants serialize on the single
`_const` row. For shara 3-30 users with rare constant writes (settings
panel saves), this is invisible. If future profiling shows constant
writes are hot, schema can split to one-row-per-constant; the call site
above is unchanged.

## Registers

OES registers are owned by recorder Documents:
- AccumulationRegister / AccountingRegister: recorder = owning Document
- InformationRegister (periodic with recorder): same
- InformationRegister (non-periodic, dimension-keyed): no recorder

When a Document is posted (`WriteObject` → `OnWrite` cascades register
movements), the Document's own `FOR UPDATE` on its row protects the
whole posting:

```
Document.Write begins TX
  └─ SELECT DataVersion FROM _D_<id> WHERE _R_uuid = ? FOR UPDATE
       (Layer 1 + Layer 2 of the protection)
  └─ SaveData() writes the Document row + cascades:
       └─ AccumulationRegister.Inventory.RecordSet.Write()
            └─ DELETE FROM _Reg_X WHERE Recorder = ?
            └─ INSERT INTO _Reg_X (...) VALUES (...)
  └─ Commit — releases the FOR UPDATE
```

Register writes don't take their own DB lock in this hot path —
they're inside the Document's TX, and any concurrent writer trying to
re-post the same Document blocks on the Document row.

**Direct `RecordSet.Write()` from script** (recorder-keyed register
without a Document Write in scope — rare administrative path): take
`SELECT 1 FROM _D_<recorder> WHERE _R_uuid = ? FOR UPDATE` as a
pessimistic guard on the recorder Document before the DELETE+INSERT.
Same row, same lock semantics.

**Non-recorder InformationRegister write**: no owning Document.
Pessimistic guard via `SELECT 1 FROM _InfoReg_X WHERE dim1=? AND
dim2=? ... FOR UPDATE` over the matching key. Zero rows locked is fine
(equivalent to "nothing existed yet for this key" — INSERT below will
not race because any other writer with the same dimensions would hit
the same composite-unique constraint).

## Conflict UX

Single exception class, two surface messages:

```cpp
class BACKEND_API ibBackendLockException : public ibBackendException {
public:
    static void VersionChangedThrow(const wxString& objectSynonym,
                                    const wxString& expected,
                                    const wxString& actual);
    static void RowLockTimeoutThrow(const wxString& objectSynonym);
    // throw-by-value, catch-by-const-ref per project convention
};
```

User-visible (ru/uk/en):

| Trigger | Message |
|---|---|
| DataVersion mismatch | *"Объект «{0}» был изменён другим пользователем. Перечитайте и попробуйте снова."* |
| Row-lock NOWAIT failure / wait timeout | *"Объект «{0}» сейчас изменяется другим пользователем. Попробуйте позже."* |

UI surface: the standard `ibBackendException` error dialog/output panel
on desktop; HTTP 409 with body `{kind: "VersionChanged", message:
"..."}` on web. The web client's `OES.error()` shows it as a toast and
the form stays in its edit state so the user can copy unsaved changes
elsewhere before reloading.

No "locked by user Ivan from PC-23" identity — that requires `sys_lock`
infrastructure which we explicitly opted out of.

## Cross-driver dialect

Add one method to `ibSqlDialect` (already has `BuildUpsert`,
`NextIdSql`, `LimitClause`, `ConcatOp`, `BlobType`, `BoolType`,
`BuildCreateIndex` — see `firebird-mesh-driver.md` Phase 7):

```cpp
class BACKEND_API ibSqlDialect {
public:
    // Per-driver suffix appended to SELECT for row-level pessimistic lock.
    // Default empty (SQLite). Drivers override.
    virtual wxString RowLockHint() const { return wxEmptyString; }
    virtual wxString NoWaitClause() const { return wxEmptyString; }
};
```

Per-driver:

| Driver | `RowLockHint()` | `NoWaitClause()` (appended after RowLockHint) | Notes |
|---|---|---|---|
| Firebird | `WITH LOCK` | `` (NOWAIT carried by TPB `isc_tpb_nowait`) | TPB-level — already plumbed |
| PostgreSQL | `FOR UPDATE` | `NOWAIT` | PG raises `SQLSTATE 55P03` on NOWAIT fail |
| MySQL | `FOR UPDATE` | `NOWAIT` (MySQL 8+) | Pre-8 falls back to `innodb_lock_wait_timeout=1` (already plumbed) |
| MSSQL (ODBC) | `WITH (UPDLOCK, ROWLOCK)` | `` (NOWAIT carried by `SET LOCK_TIMEOUT 0`) | Already plumbed |
| SQLite | `` | `` | Single-writer naturally |

The acquire SQL string becomes:
`SELECT DataVersion FROM <tbl> WHERE <pk> = ? <RowLockHint> <NoWaitClause>`.

## What this design explicitly does NOT do

Conscious omissions, each with a revisit trigger:

| Omitted feature | What it would buy | Revisit when |
|---|---|---|
| `sys_lock` table | Proactive "edited by user X" on form-open; long-held cross-TX locks | Users report material data loss from "edited 20 min, lost on Save" — across enough sessions to justify the cost |
| Form-open auto-lock | Same | Same |
| `ibLockManager` singleton + handle RAII + sweep | Same | Same |
| Admin "currently held locks" UI | Operational visibility into who's editing what | Multi-tenant deployment where ops needs to force-release stuck sessions |
| Script-level `New DataLock()` API | 1С-script-compatibility for managed locks | Concrete migration project from 1С configs that uses managed locks |
| Range filters (`Period < #2023#`) | 1С-style range locks on RecordSet | Same as above |
| Shared (S) lock mode | "Block writers while I read consistently" | Reports complain about register snapshot shifting mid-computation (rare — usually `BeginTransaction({.readOnly = true})` is enough) |
| Wait-with-timeout `Lock(timeoutMs)` | User control over how long to wait | Specific UX request |
| Cross-TX session-scope locks | Long-held UX-driven locks | Same trigger as `sys_lock` |

All omissions are backward-compatible to add later — the Write-time
DataVersion + `FOR UPDATE` lives on every mutable-ref Write path
regardless; a future `sys_lock` layer plugs in *before* the existing
flow without changing the existing semantics.

### Planned upgrade path — Phase B sys_lock (LANDED 2026-05-24)

> **Status update 2026-05-24:** **all sub-phases B.1-B.5 + Polish
> landed** and smoke-validated. Soft-lock model (open OK on conflict,
> throw at Save) chosen over the original hard-block ("form refuses
> to open") design after first smoke pass; B.4 simplified from a
> `New DataLock` script value-class to direct `Lock()`/`Unlock()`
> methods on the four ref-object kinds per user feedback ("просто
> добавь лок в справочники, документы" — less script ceremony, same
> coverage of the practical use cases). Schema simplified — all
> locks are session-scoped, no TX-scoped variant (TX-row-locks
> already cover that from Phase 5-7).
>
> Net diff: ~1500 lines (less than the ~3000 initially estimated —
> dropping TX-scope + simpler conflict-check than originally
> sketched + dropping the `New DataLock` value-class scaffolding).

If the "edited N minutes, lost on Save" UX becomes a real problem in
production, the path is **app-table `sys_lock`** — not long-held TX
(see "Why NOT long-held transactions" below) and not per-row lock
columns (see "Why NOT per-row lock columns" further below). Shape of
`sys_lock` (as landed):

- ✅ New table `sys_lock(lockGuid, sessionGuid, namespace, keyHash,
  keyData, lockMode, acquiredAt, userName, computer)` inside the
  existing `sys.fdb` — a normal OES service table like `sys_session`
  already is, **not a separate file**. CREATE is idempotent and runs
  as additive migration on startup (`appDataQuery.cpp::CreateTable
  Lock`). Indices on `(namespace, keyHash)` for conflict-check and on
  `(sessionGuid)` for session-end cascade.
- ✅ `ibLockManager` singleton in `backend/lock/lockManager.{h,cpp}`
  with `Acquire(items, opts, customHolder=nullptr) → ibLockHandle`,
  `ReleaseRows`, `OnSessionEnd`, `OnZombieSweep`, `GetSnapshot`.
- ✅ `ibLockHandle` RAII (move-only) — `lock/lockHandle.{h,cpp}`.
- ✅ `ibLockHolder` interface + `ibSingleLockHolder` concrete
  (`lock/lockHolder.{h,cpp}`) for extensibility — custom owners that
  aren't tied to a user session (long-running jobs, system coordinator,
  tests).
- ✅ `ibLockKeyHash` deterministic SHA-256 over canonical key bytes
  (`lock/lockKeyHash.{h,cpp}`). Supports STRING / NUMBER / BOOLEAN /
  DATE / REFFER key value types. Wrapped by `ibLockItem::KeyHash()` /
  `KeyData()` (lazy-cached) so callers never reach the namespace
  directly — typed factories `ibLockItem::ForRef(namespacePath,
  refGuid, mode)` and `ibLockItem::ForNamespace(namespacePath, mode)`
  encapsulate per-domain key shape (the "Ref" field-name + the hash
  derivation). Adding a new resource kind = adding a new `For*`
  factory; lockManager just reads `item.KeyHash()` during Acquire.
- ✅ Hooks: `OnSessionEnd` from `ibSessionRegistry::ProcessRemove`
  (drop all session rows on logout), `OnZombieSweep` cascade in
  `JobSweepStale` (drop dead-session rows on heartbeat timeout).
- ✅ `ibSourceDataObject::TryAcquireFormLock` / `ReleaseFormLock`
  base API + storage; `ibValueRecordDataObjectRef` overrides with
  ref-keyed acquire. Default no-op for sources without lock identity
  (DataProcessor / Report).
- ✅ Soft-lock model: `ibValueForm::ShowForm` swallows LockConflict
  silently (form opens regardless); Write/Delete paths re-attempt
  acquire and throw on persistent conflict (caught by desktop's
  toolbar / docview Alert path).
- ✅ **B.4** — script API exposed as `obj.Lock()` / `obj.Unlock()` on
  catalog/document/chartOfAccounts/chartOfCharacteristicTypes ref-
  objects (4 files, ~6 lines each). Both methods route through the
  existing `TryAcquireFormLock` / `ReleaseFormLock` infrastructure
  on `ibSourceDataObject` — same code path the form-open soft-lock
  uses. Script usage:
  ```ves
  doc = Documents.Receipt.GetObject(ref);
  doc.Lock();
  // ... long-held edits across TX boundaries ...
  doc.Write();
  doc.Unlock();
  ```
  Replaces the originally-planned `New DataLock` value-class
  (`valueDataLock.{h,cpp}`) which was scaffolded then dropped per
  user feedback in favour of this simpler shape.
- ✅ **B.5** — `wfrontendLocksJSON()` / `wfrontendForceReleaseLockByGuid(guid)`
  in `frontend/wfrontend.{h,cpp}`. HTTP routes in
  `wenterprise-server/main.cpp`: `GET /admin/locks` returns the
  cluster-wide snapshot as JSON array; `DELETE /admin/locks/<guid>`
  is admin force-release. Designer side: `ibDialogActiveUser` now
  carries a `wxNotebook` with two pages — `Users` (existing) and
  `Locks` (new). The Locks page lists held rows with right-click
  → "Force release" context menu (designer-only, with confirmation).
- ✅ **Polish** — `ibBackendLockException` extended with
  `m_objectName` + `m_blockingUser` fields populated on
  `LockConflict` throws; `ibValueForm::SetLockBadge(holder)` /
  `GetLockBadgeHolder()` + `RefreshLockBadge()`. The initial
  title-suffix decoration in `ibVisualHostClient::SetCaption`
  was rolled back (OES titles are already long with code +
  description; the suffix overflowed MDI tabs). UI surface
  for the badge field is deferred to a follow-up — the field
  is populated and refresh-aware, ready for a lock-icon
  overlay or status-bar consumer.
- ✅ **Badge refresh** — `RefreshLockBadge()` re-attempts the
  acquire and flips the badge on outcome: success clears it
  (lock now ours, form editable), persistent conflict updates
  the holder if it changed. Wired into `UpdateForm` so the
  cross-user notifier tick naturally refreshes lock state
  alongside data state. Closes the original Polish wart where
  the badge stayed stale forever after the blocking session
  released.
- ✅ **Constant form-open lock** — `ibValueRecordDataObject
  Constant::TryAcquireFormLock` override keyed by the
  constant's namespace path only (single-row "global" — no
  per-key sub-identifier). Closes the silent miss where
  Constant forms (which inherit from `ibSourceDataObject`
  like ref-objects) used to fall back to the base no-op.
  Register-set sources don't inherit from `ibSourceData
  Object`, so the form-open path doesn't reach them — their
  write-time protection comes from `LockByKeys` (Phase A).

### Original design notes (kept for reference)

**Cost — much smaller than it looks.** Concrete numbers for the 3-30-
user shara workload, anchored to the already-running `sys_session`
heartbeat as the reference point:

| Metric | sys_session (in production) | sys_lock (Phase B projection) |
|---|---|---|
| Writes/sec at 30 users | ~30 (1Hz heartbeat per session) | ~5 (form-open INSERT + form-close DELETE; assumes 5 form-opens/min per user) |
| Where it lands | dedicated `sys_session` table | dedicated `sys_lock` table |
| MVCC bloat target | own table — vacuumed independently | own table — same |
| Read impact on business tables | none | none |
| Per-write cost | one PK index update | one `(namespace, key)` covering index update |
| Storage at steady state | ~30 rows × ~200 B = ~6 KB | ~50 rows × ~150 B = ~7.5 KB |

`sys_lock` load is **~6× lighter** than the heartbeat that's already
running fine in production. The cost worry that naturally arises when
adding "yet another table" doesn't survive the numbers — we already
pay 6× more for sys_session and nobody notices.

Real cost concerns to track (not blockers, just things to size):

- **Latency on form-open** — adds one DB round-trip. Local FB
  embedded: <1ms. Remote PG over LAN: ~5-30ms. Not user-visible.
- **Pool contention if acquire-TX blocks** — mitigated by NOWAIT
  mode (already plumbed across all 4 drivers via `ibTxOptions::noWait`).
- **Storage growth from DELETE'd rows** — vacuum / sweep already
  handle this for sys_session, same machinery reused.

### Why NOT long-held transactions (the obvious-wrong-answer trap)

"Just open a TX on form-open and hold it" looks natural but doesn't
work in any production OLTP system. The reasons are well-documented
industry-wide and apply to every database we support:

| Problem | Effect |
|---|---|
| Pins a pool connection | `maxSize=32` → 32 editing users saturate the pool; 33rd user can't Login. Web N tabs × N users = O(N²) connections. |
| Blocks GC/sweep | FB sweep / PG VACUUM can't reclaim old row versions while any TX is older than them. User on lunch with form open → database bloats for everyone. |
| `idle_in_transaction_session_timeout` | PG aborts the TX after default 5 min → lock vanishes silently → form still thinks it's editing → Save fails or corrupts. |
| Network blip = lock loss | Connection drops → TX rolls back → lock gone. User returns from lunch, someone else already edited and saved. |
| Breaks worker-pool model | `compute-server-tiering.md` Phase 5 assumes one worker serves many sessions. A held TX pins the connection to one session — worker can't switch. |
| Breaks `ibConnectionScope` invariant | Counter-based nested-TX expects bounded scope (`connection-pool.md`). Long TX across script calls doesn't compose. |

Industry response to all of the above is the same: **app-table for
long-held locks, short TX for actual writes**. That's the `sys_lock`
upgrade path above. Do not reach for long-held TX as a shortcut.

### Why NOT per-row lock columns (also evaluated, also rejected)

A natural-looking alternative to `sys_lock` is to add a few system
columns directly to every business table (`_C_*`, `_D_*`, `_Reg_*`):

```
_locked_by   VARCHAR(36)    -- session guid, NULL when free
_locked_at   TIMESTAMP      -- when acquired
_lock_kind   INTEGER        -- shared/exclusive/...
```

`OpenForm` editable would `UPDATE` the row to stamp `_locked_by`;
`CloseForm` would `UPDATE ... SET _locked_by = NULL`. Other sessions
see who holds it directly in the SELECT.

Appeal:
- Lock info lives **with** the data — natural locality.
- No new table.
- Survives backup/restore as part of the row.
- Familiar paradigm — same shape as `DataVersion` column extension.

Rejected because **both schemes require a write to acquire** (you can't
have "proactive locked-by-user-X" without persisting it somewhere),
but per-row columns put the write in the **worst possible place** —
the hot business tables that every other query also reads:

| Cost | per-row column | sys_lock |
|---|---|---|
| WAL/redo on acquire | on the **business table** | on a tiny dedicated table |
| New MVCC version | bloats the business row (FB sweep / PG VACUUM pressure) | bloats only sys_lock |
| Triggers | any audit/notify triggers on the business table fire on every form-open | only sys_lock's triggers (none by default) |
| Replication | FB replication / mesh ships every form-open as a data event | sys_lock can be excluded from replication scope |
| Row cache invalidation | invalidates the business row for every reader query | invalidates only sys_lock rows |
| Storage waste | 44 bytes × **every row in every table** (99% never locked) | ~100 bytes × **only currently-locked rows** (typically few KB total) |
| Schema migration | DDL on every metatable; cascades to tabular sections & register tables | one `CREATE TABLE` |
| Admin "show all locks" | `UNION ALL` across 30+ tables | one `SELECT * FROM sys_lock` |
| Constants (single-row table) | awkward — lock column on a row that holds N constants | clean — namespace `Constant.X` keyed by name |
| Force-release on session death | `UPDATE` across all tables with CTE/JOIN | one `DELETE FROM sys_lock WHERE sessionGuid = ?` |

Conclusion: the appeal of "lock lives with data" loses to the operational
cost of "every form-open writes to the hot business table". `sys_lock`
concentrates the write load in exactly the table that exists for it.
Per-row column is the wrong shape for this problem.

## Files touched

| Path | Change |
|---|---|
| `backend/utils/dataVersion.{h,cpp}` | **New** — `ibDataVersion::NewStamp()` 12-char base-62 generator |
| `backend/backend_exception.h` / `.cpp` | `ibBackendLockException` + 2 throw helpers |
| `backend/databaseLayer/sqlDialect.{h,cpp}` | `RowLockHint()` + `NoWaitClause()` virtuals + 5 per-driver overrides |
| `backend/metaCollection/partial/commonObject.h` | `wxString m_loadedDataVersion` + `wxString m_currentDataVersion` on `ibValueRecordDataObject` |
| `backend/metaCollection/partial/list/objectList.h` / `.cpp` | `LoadObject` captures `m_loadedDataVersion` from result row |
| `backend/metaCollection/partial/catalogObject.cpp` | `WriteObject` / `DeleteObject` adds Layer-1 + Layer-2 + bump |
| `backend/metaCollection/partial/documentObject.cpp` | Same |
| `backend/metaCollection/partial/chartOfAccountsObject.cpp` | Same |
| `backend/metaCollection/partial/chartOfCharacteristicTypesObject.cpp` | Same |
| `backend/metaCollection/partial/informationRegisterObject.cpp` | `WriteRecordSet` / `DeleteRecordSet` — recorder-row guard if direct-from-script; dimension-key guard for non-recorder |
| `backend/metaCollection/partial/accumulationRegisterObject.cpp` | `WriteRecordSet` / `DeleteRecordSet` — recorder-row guard |
| `backend/metaCollection/partial/accountingRegisterObject.cpp` | Same |
| `backend/metaCollection/partial/constantObject.cpp` | `SetConstValue` — single-row `FOR UPDATE` guard |
| `locale/*.po` + sources | 2 user-visible strings |
| `tests/test_dataVersion.cpp` | **New** — round-trip, mismatch detection, stamp monotonicity |
| `tests/test_writeProtection.cpp` | **New** — gtest: parallel Write on same ref → exactly one succeeds |

Estimated diff: ~400-500 lines added, ~30 lines edited per Write
method × ~9 methods.

## Phase plan

Single phase. Order matters: dialect + version helper land first so the
Write changes can call into them.

| Step | What | Status |
|---|---|---|
| 1 | `ibDataVersion::NewStamp()` + gtest | ✅ landed (no gtest yet — alpha tier) |
| 2 | `ibBackendLockException` + throw helpers | ✅ landed |
| 3 | `RowLockHint()` + `NoWaitClause()` virtuals on `ibDatabaseLayer` + 4 driver overrides (FB / PG / MySQL / ODBC; SQLite default) | ✅ landed (moved to base virtuals instead of separate `ibSqlDialect` per Phase 7 of mesh doc being reverted) |
| 4 | `m_loadedDataVersion` capture in `LoadObject` | ✅ landed |
| 5 | Mutable-refs (Catalog / Document / ChartOf*) Write/Delete protection | ✅ landed — `LockAndCheckDataVersion()` on `ibValueRecordDataObjectRef` + wired into 4 × 2 = 8 sites |
| 6 | Register Write/Delete protection (universal `LockByKeys()` — same path for recorder-keyed AR/AcR/IR-Subordinate and dimension-keyed IR; keys live in `m_keyValues`) | ✅ landed — wired into 3 × 2 = 6 sites |
| 7 | Constants Write protection (single-row guard) | ✅ landed (inline FOR UPDATE in `SetConstValue`) |
| 8 | Parallel-write gtest (two threads, same ref → one succeeds with VersionChanged on the other) | 🔲 deferred (alpha tier — manual smoke validated the path) |
| 9 | Web UX — `ExceptionToJson` shape (`error`+`kind`+`message`), `OES.handleBackendError` JS dispatcher, desktop Alert in toolbar + docview | ✅ landed (HTTP status codes still 200 — kind carried in JSON body; deferred) |
| 10 | Localized messages (ru/uk/en) | 🔲 deferred (English `_()`-wrapped, ready for xgettext sweep when locale workflow runs) |
| 11 | Bonus: closed pre-existing bug — DataVersion was declared on `ibValueMetaObjectRecordDataMutableRef` but **missing from each subclass `FillArrayObjectByPredefinedAttribute` list** so the column was never read/written by runtime despite being created in the DB schema. Fixed in Catalog / Document / ChartOfAccounts / ChartOfCharacteristicTypes. See [[reference-predefined-attr-subclass-lists]] memory note. | ✅ landed |
| 12 | DataVersion hidden from default list columns (added to `IsDataVersion` filter in `ibValueListDataObjectRef::GetSourceExplorer` + `ibValueModelTreeDataObjectFolderRef::GetSourceExplorer` alongside Reference/DeletionMark/PredefinedName) | ✅ landed |

Estimated: ~1 week of focused work.

## Open questions

1. **`DataVersion` stamp format.** Proposed: `(ms_since_epoch << 16) |
   counter` base-62 → 12 chars. Alternative: pure random base-62 (no
   ordering, no clock-skew). Pick when implementing — equality is the
   only operation we care about, so random works just as well.
2. **NoWait by default for `Write`?** Current proposal: wait mode
   (driver-default lock wait, ~30s on FB). Argument for NoWait:
   instant feedback to user. Argument against: legitimate brief
   contention (two posters racing for 100ms) escalates to a UX error.
   Default = wait; per-call NoWait if a future use case demands it.
3. **`SELECT FOR UPDATE` granularity on InformationRegister with no
   matching rows.** Per-driver behavior varies — some lock "the gap",
   some lock nothing. If two concurrent INSERTs target the same
   composite-unique-key, the second hits the unique constraint on
   INSERT regardless of FOR UPDATE; so the gap-lock difference is not
   observable from app code.
4. **`DataVersion` migration.** Existing databases have the column but
   it's empty or stale (never written). First Write on an existing row
   skips the check (`m_loadedDataVersion.IsEmpty()` short-circuits)
   and stamps the new version — silent migration on next use. No
   forced rewrite pass needed.

## Related docs & memory

- [`connection-pool.md`](connection-pool.md) — `ibConnectionScope`,
  nested-TX counter, NOWAIT plumbing.
- [`firebird-driver-hardening.md`](firebird-driver-hardening.md) — FB
  TPB with `isc_tpb_lock_timeout` and `isc_tpb_nowait`.
- [`session-registry.md`](session-registry.md) — per-driver NoWait
  status across FB/PG/MySQL/ODBC.

Memory notes that will be created on landing:
- `reference_data_version_format` — stamp generator, monotonicity
  guarantees.
- `reference_row_lock_hint` — per-driver SQL dialect for pessimistic
  row lock.
- `project_record_write_protection` — implementation status,
  open issues.
