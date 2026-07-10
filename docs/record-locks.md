# Record-write protection — optimistic DataVersion + DB row lock + sys_lock

> **Current state (verified against code 2026-06-19).** Three layers, all
> landed:
>
> 1. **Optimistic `DataVersion` check** — the primary lost-update guard.
>    Every mutable-ref row (Catalog / Document / ChartOf*) carries a
>    `DataVersion String(12)` stamp. Read captures it
>    (`m_loadedDataVersion`); Write re-reads under a row lock and throws
>    `ibBackendLockException::VersionChanged` on mismatch.
> 2. **DB-side row lock** during the Write TX — `SELECT … FOR UPDATE`
>    (PG/MySQL) / `WITH LOCK` (FB); the dialect appends the clause, the
>    database serializes concurrent writers against the same row.
>    Auto-released on Commit/Rollback.
> 3. **`sys_lock` table + `ibLockManager`** — proactive, cross-TX,
>    cluster-aware "edited by user X" locks (soft model: form opens on
>    conflict, throws at Save). Acquired on form-open and via script
>    `obj.Lock()`/`obj.Unlock()`.
>
> Liveness of sessions is a **separate** concern — `sys_session`
> heartbeat on `lastActive`, not a record lock. The earlier
> pessimistic-row-lock-for-liveness design (`TryProbeRowLock` /
> `HoldRowLocks`) was **rolled back**; only retirement comments remain
> (see `ARCHITECTURE.md` → ibSessionRegistry, `session-registry.md §4`).
>
> ### Where things live in code (authoritative pointers)
>
> | Concern | Symbol | File |
> |---|---|---|
> | Version capture (read + post-commit) | `CaptureLoadedDataVersion()` — sets `m_loadedDataVersion`; called from `ReadData` and `CommitWriteScope` (never from the bump) | `commonObjectRefQuery.cpp` |
> | Lock + version check | `ibValueRecordDataObjectRef::LockAndCheckDataVersion(bump)` | `commonObjectRefQuery.cpp:101` |
> | Register key lock | `ibValueRecordSetObject::LockByKeys()` | `commonObjectRecordSetQuery.cpp:26` |
> | Constant singleton lock | inline `ibQueryIR{ m_lockForUpdate }` | `constantObject.cpp:349` |
> | Stamp generator | `ibDataVersion::NewStamp()` | `utils/dataVersion.hpp` / `.cpp` |
> | DataVersion attr accessor | `GetDataVersion()` | `commonObject.h:749`; attr declared `commonObject.h:824` |
> | Exception | `ibBackendLockException` | `backend_exception.{h,cpp}` |
> | sys_lock coordinator | `ibLockManager` | `lock/lockManager.{h,cpp}` |
> | Lock RAII handle | `ibLockHandle` | `lock/lockHandle.{h,cpp}` |
> | Key hash (SHA-256) | `ibLockKeyHash`, `ibLockItem::ForRef`/`ForNamespace` | `lock/lockKeyHash.*`, `lock/lockTypes.h` |
> | Row-lock dialect clause | `m_rowLockSuffix` / `m_rowLockNoWaitSuffix` | `databaseLayer.h:148,155` |
> | L2 lock flag | `ibQueryIR::m_lockForUpdate` / `m_lockNoWait` | `databaseQueryBuilder.{h,cpp}` |
>
> ### How the row-lock clause is rendered
>
> There is **no** `RowLockHint()` / `NoWaitClause()` virtual — those
> were deleted. The clause lives on the dialect dictionary:
> `m_rowLockSuffix` (` FOR UPDATE` default / FB ` WITH LOCK` / SQLite
> empty) plus `m_rowLockNoWaitSuffix` (` NOWAIT` on PG/MySQL; empty on
> FB/SQLite — their non-blocking acquire rides the TX `noWait`/TPB). A
> pessimistic SELECT is expressed at L2 as `ibQueryIR::m_lockForUpdate`
> (+ `m_lockNoWait`); the renderer appends the dialect clause to the
> top-level SELECT (`databaseQueryBuilder.cpp:451`). Mutable-refs and
> registers reach it through the L3 door
> (`ibDataQueryBuilder … ibReadPageRequest::m_lockForUpdate`); the
> Constant write reaches it through a direct L2 `ibQueryIR`.
>
> **Why the version read goes through `GetValue(dvAttr)` and not raw
> SQL.** DataVersion's SQL field name is the *composite*
> `<fld>_TYPE,<fld>_S` (type tag + string data), so a raw
> `GetResultString` on a column literally named with a comma failed
> "field not found". The provider's attribute assembly is the only
> correct read.
>
> ---
>
> ### Arc history (superseded snapshots — kept for the diff trail)
>
> The sections below this line are the **original 2026-05-24 proposal
> and landing notes**. Concrete code shapes in them are **superseded**:
> they describe `RowLockHint()` / `NoWaitClause()` virtuals, a
> `ReadDataVersionForUpdate` raw-SQL wrapper, and a separate
> `m_currentDataVersion` member — **none of which exist in current
> code**. Read the table above for the live shape; read below only for
> the design rationale and the phased landing record.
>
> - **2026-06-16** — row-lock clause unified onto the dialect
>   dictionary; per-driver virtuals deleted. Liveness-probe legacy
>   (`TryProbeRowLock` / `HoldRowLocks` / `ReleaseRowLocks` + registry
>   probe connection) removed. (L1→L2 tier-hygiene arc —
>   query-language-arc.md §24.)
> - **2026-06-09** — `LockAndCheckDataVersion` moved onto the L3 door
>   (reference-as-key arc); the raw `ReadDataVersionForUpdate` wrapper
>   retired.
> - **2026-05-24** — **LANDED**, smoke-validated on Debug|x86 (two
>   enterprise.exe sessions editing the same Catalog item — second Save
>   throws `ibBackendLockException::VersionChanged`). Phases 1-8 below.
>
> **TL;DR.** Lost updates are caught by the optimistic `DataVersion`
> stamp; the in-TX `FOR UPDATE`/`WITH LOCK` row lock serializes racing
> writers so the version re-read is consistent; `sys_lock` adds the
> proactive "edited by user X" UX on top. Conflicts surface at Save as
> *"Object was changed by another user, please reload"* — right
> tradeoff for 3-30-user shara workloads.

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

## Lock + version check at Write

The Phase A scaffold (`record-object-refactor.md`) factored the
designer/eval/access/scope/lock bookkeeping into
`ibValueRecordDataObjectRef::BeginWriteScope` /
`CommitWriteScope` (mutable-refs) and
`ibValueRecordSetObject::BeginRecordSetWriteScope` /
`CommitRecordSetScope` (registers). `BeginWriteScope` is where the
lock layers fire (`commonObjectRefQuery.cpp:179`):

```cpp
bool ibValueRecordDataObjectRef::BeginWriteScope(ibConnectionScope& scope)
{
    if (appData->DesignerMode())          return false;   // designer skip
    if (!scope || !scope->IsOpen())
        ibBackendCoreException::Error(_("Database is not open!"));
    if (ibBackendException::IsEvalMode()) return false;   // eval skip
    if (!m_metaObject->AccessRight_Write()) { ibBackendAccessException::Error(); return false; }

    scope.SafeBeginTransaction();
    TryAcquireFormLock();                    // sys_lock soft-lock re-attempt
    LockAndCheckDataVersion(/*bump=*/true);  // row lock + version + bump
    return true;
}
```

`LockAndCheckDataVersion(bump)` does the row lock and the version
compare in one L3 selection (`commonObjectRefQuery.cpp:101`):

```cpp
// Existing rows only — new ones have no row to lock, no prior version.
if (!m_newObject && !m_loadedDataVersion.IsEmpty()) {
    ibDataQueryBuilder q;
    q.From(m_metaObject->GetQueryable())
     .Where(ibRawDBColumn::String(wxT("uuid")), ibValue(wxString(m_objGuid)));
    ibReadPageRequest page;
    page.m_count = 1;
    page.m_lockForUpdate = true;            // dialect appends m_rowLockSuffix
    ibDataQueryResult sel = q.Execute(page);

    const bool rowFound = sel.Next();
    const wxString dbVer = rowFound ? sel.GetValue(dvAttr).GetString() : wxString();
    if (!rowFound)                          // row DELETEd under us — treat as conflict
        ibBackendLockException::VersionChangedThrow(m_metaObject->GetSynonym(),
            m_loadedDataVersion, wxT("<deleted>"));
    if (dbVer != m_loadedDataVersion)
        ibBackendLockException::VersionChangedThrow(m_metaObject->GetSynonym(),
            m_loadedDataVersion, dbVer);
}
if (bump) {                                  // stamp the row for SaveData's UPSERT
    const wxString newStamp = ibDataVersion::NewStamp();
    SetValueByMetaID(dvId, ibValue(newStamp));
    // NOTE: m_loadedDataVersion is NOT advanced here — see below.
}
```

The selection runs on the session holder — the SAME connection as the
open `BeginWriteScope` TX — so the row lock is held until commit.
Delete calls `LockAndCheckDataVersion(/*bump=*/false)` (the row is
going away). There is no separate `m_currentDataVersion` member — the
bumped value is written straight into the attribute slot.

**Marker advances only on commit (rollback-safe).** The bump writes the
new stamp into the DataVersion *attribute* so SaveData's UPSERT persists
it, but it deliberately does **not** touch `m_loadedDataVersion`. That
marker — the "version I loaded from the DB" — is advanced at exactly one
point: `CommitWriteScope`, after `SafeCommitTransaction()` makes the row
durable (via `CaptureLoadedDataVersion()`, which mirrors the committed
attribute value back into the marker). ReadData uses the same helper.

Why this matters: a Write can fail *after* the bump — RLS write-deny
(guarded UPDATE affects 0 rows → throw), a `SaveData` error, or a
`BeforeWrite`/`OnWrite` script cancel — and the surrounding
`ibConnectionScope` rolls the TX back. The DB row keeps its old version.
If the bump had advanced `m_loadedDataVersion` (as it once did), the next
Save on the same still-open form would re-read the unchanged DB version,
compare it against the *new* marker, and raise a false *"changed by
another user, reopen the form"* — even though nothing was ever committed.
Advancing the marker only on durable commit keeps it equal to the row's
last committed version, so a rolled-back Write leaves it retry-clean. The
back-to-back script case (`obj.Write(); obj.Write();`) still stays
consistent: the first Write's commit syncs the marker before the second
Write's check runs.

NOWAIT vs wait mode: a non-blocking acquire sets `m_lockNoWait`
(→ ` NOWAIT` on PG/MySQL; FB rides `isc_tpb_nowait`). Default is wait
(driver's normal lock-timeout — FB `isc_tpb_lock_timeout` plumbed).

## DataVersion lifecycle

`DataVersion String(12)` predefined attribute declared on
`ibValueMetaObjectRecordDataMutableRef` (`commonObject.h:824`),
accessor `GetDataVersion()` (`commonObject.h:749`).

**Capture on Read.** `ReadData` (`commonObjectRefQuery.cpp:68`) reads
the loaded row's DataVersion into `wxString m_loadedDataVersion`
(`commonObject.h:1903`). New objects leave it empty so the first Save
skips the check.

**Bump before Write.** `ibDataVersion::NewStamp()` returns a fresh
12-char base-62 string. Payload: `(unix_ms_since_epoch << 16) |
per_process_counter`, base-62 encoded — wall-clock prefix keeps stamps
roughly chronological for debugging, the 16-bit atomic counter rolls
per process so two stamps within one millisecond don't collide. Only
equality matters for the lost-update check, so cluster clock skew is
harmless. `IsValidStamp()` validates shape.

```cpp
// backend/utils/dataVersion.hpp
namespace ibDataVersion {
    BACKEND_API wxString NewStamp();              // 12-char base-62
    BACKEND_API bool     IsValidStamp(const wxString&);
}
```

**Check on Write.** Compared against `m_loadedDataVersion`; mismatch →
`ibBackendLockException::VersionChangedThrow`. A row that vanished
between Read and Write (concurrent DELETE) is also treated as a
version conflict (same reload-and-retry recovery).

**No DataVersion on Registers or Constants.** Register records are
written atomically by recorder (DELETE-then-INSERT for the whole
recorder bucket); there's no read-edit-write diff cycle on individual
register records, so a per-record version stamp protects nothing.
Constants are atomic-write-per-call from script; the `FOR UPDATE` on
the singleton row covers the only race window.

## Constants

Each constant has its own single-row table; `RECORD_KEY = '6'` is the
singleton row. `ibValueRecordDataObjectConstant::SetConstValue`
(`constantObject.cpp:342`) takes a row lock via a direct L2 `ibQueryIR`
(constants don't go through the L3 builder for the lock) before the
UPSERT:

```cpp
scope.SafeBeginTransaction();
{
    ibDatabaseQueryBuilder q(ibSession::Current()->Holder());
    ibQueryIR ir(ibProject(
        ibFilter(ibScan(tableName),
            ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("RECORD_KEY")),
                    ibConst(ibValue(wxString(wxT("6")))))),
        { { ibConst(ibValue(1)), wxEmptyString } }));   // SELECT 1 — lock only
    ir.m_lockForUpdate = true;
    ibQueryResult lockRs = q.ExecuteIR(ir);
    while (lockRs.Next()) {}                              // drain — side effect is the lock
}
```

It runs on the session holder, so it joins the TX that the subsequent
UPSERT commits. Concurrent writes to *different* constants don't
conflict (separate tables); concurrent writes to the *same* constant
serialize on its `RECORD_KEY = '6'` row. No DataVersion on constants —
a single-row UPSERT has no read-edit-write diff cycle to protect; the
row lock covers the only race window.

The form-open soft-lock is also wired:
`ibValueRecordDataObjectConstant::TryAcquireFormLock` keys on the
constant's namespace path via `ibLockItem::ForNamespace`
(`constantObject.cpp:310`). The `SetConstValue` script path itself
does not re-take the sys_lock — the DB-side row lock still protects
against concurrent writes, but a script `Const.X = v` skips the
proactive notify (flagged in `record-object-audit.md`).

## Registers

OES registers are owned by recorder Documents:
- AccumulationRegister / AccountingRegister: recorder = owning Document
- InformationRegister (periodic with recorder): same
- InformationRegister (non-periodic, dimension-keyed): no recorder

Registers have **no DataVersion** — a record set is written atomically
by DELETE-then-INSERT for the whole bucket, so there is no
read-edit-write diff cycle on individual lines. Their protection is
`ibValueRecordSetObject::LockByKeys()` (`commonObjectRecordSetQuery.cpp:26`),
called from `BeginRecordSetWriteScope` / `BeginRecordSetDeleteScope`.

`LockByKeys()` locks the register's **own** existing lines for the
bound composite key — it does **not** reach back to a recorder
Document row. It builds an L3 selection over the queryable, constrains
by the populated dimensions (`GetGenericDimensionArrayObject` filtered
by `FindKeyValue` — `{recorder}` for AR/AcR, `{period, dim…}` for
non-recorder IR), sets `page.m_lockForUpdate = true`, and drains the
result to hold the lock:

```cpp
ibDataQueryBuilder q;
q.From(m_metaObject->GetQueryable());
for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
    if (!FindKeyValue(object->GetMetaID())) continue;
    q.Where(object, m_keyValues.at(object->GetMetaID()));
}
ibReadPageRequest page;
page.m_count = 0;                 // every matching line
page.m_lockForUpdate = true;      // dialect appends FOR UPDATE / WITH LOCK
ibDataQueryResult sel = q.Execute(page);
while (sel.Next()) {}             // drain — side effect is the lock
```

When a Document posts, the register `WriteRecordSet` runs inside the
Document's TX, so its `LockByKeys` lock and the Document's
`LockAndCheckDataVersion` row lock are held to the same commit. Zero
matching rows is fine (nothing existed yet for this key) — a racing
INSERT with the same dimensions hits the composite-unique constraint
regardless of the lock. If no key fields are populated, `LockByKeys`
skips the lock entirely and relies on the UPSERT's unique constraint.

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

The row-lock clause is **data on the dialect dictionary** (two
`wxString` fields on `ibDatabaseLayer`), not a virtual method. There is
no `RowLockHint()` / `NoWaitClause()` — those virtuals were deleted in
the 2026-06-16 L1→L2 cleanup. Each driver sets the fields in its
dialect init; the L2 renderer
(`databaseQueryBuilder.cpp:451`) appends them when
`ir.m_lockForUpdate` (and, for no-wait, `ir.m_lockNoWait`) are set:

```cpp
// ibDatabaseLayer (databaseLayer.h:148, 155)
wxString m_rowLockSuffix       = wxT(" FOR UPDATE");   // default
wxString m_rowLockNoWaitSuffix = wxT(" NOWAIT");       // default
```

Per-driver values:

| Driver | `m_rowLockSuffix` | `m_rowLockNoWaitSuffix` | Notes |
|---|---|---|---|
| Firebird | ` WITH LOCK` | `` (empty) | NOWAIT rides the TPB `isc_tpb_nowait` (`ibTxOptions::noWait`) |
| PostgreSQL | ` FOR UPDATE` (default) | ` NOWAIT` (default) | PG raises `SQLSTATE 55P03` on NOWAIT fail |
| MySQL | ` FOR UPDATE` (default) | ` NOWAIT` (default) | NOWAIT honoured on MySQL 8+; older rely on `innodb_lock_wait_timeout` |
| ODBC/MSSQL | ` FOR UPDATE` (default) | ` NOWAIT` (default) | dialect default; not separately tuned |
| SQLite | `` (empty) | `` (empty) | whole-DB TX lock — no row `FOR UPDATE` |

The rendered SELECT becomes
`SELECT … WHERE uuid = ?<m_rowLockSuffix><m_rowLockNoWaitSuffix>`. FB
permits `ORDER BY` together with `WITH LOCK` (validated).

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
| `backend/utils/dataVersion.hpp` / `.cpp` | **New** — `ibDataVersion::NewStamp()` 12-char base-62 generator + `IsValidStamp` |
| `backend/backend_exception.h` / `.cpp` | `ibBackendLockException` + throw helpers |
| `backend/databaseLayer/databaseLayer.h` + driver dialect init | `m_rowLockSuffix` / `m_rowLockNoWaitSuffix` dialect fields (replaced the originally-planned `RowLockHint()`/`NoWaitClause()` virtuals) |
| `backend/metaCollection/partial/commonObject.h` | `wxString m_loadedDataVersion` on `ibValueRecordDataObjectRef` (no separate `m_currentDataVersion` — bump writes the attr slot directly) |
| `backend/metaCollection/partial/commonObjectRefQuery.cpp` | `ReadData` captures `m_loadedDataVersion`; `LockAndCheckDataVersion` impl |
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
| 3 | Per-driver row-lock clause | ✅ landed — **later superseded**: started as `RowLockHint()`/`NoWaitClause()` virtuals on `ibDatabaseLayer`; 2026-06-16 these were deleted and the clause moved to the `m_rowLockSuffix`/`m_rowLockNoWaitSuffix` dialect fields (see "Cross-driver dialect" above) |
| 4 | `m_loadedDataVersion` capture on read | ✅ landed (in `ReadData`, not the originally-named `LoadObject`) |
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

1. **`DataVersion` stamp format.** ✅ Resolved: `(unix_ms_since_epoch
   << 16) | per_process_counter`, base-62 → 12 chars
   (`dataVersion.cpp`). Equality is the only operation that matters, so
   the wall-clock prefix is purely a debugging convenience.
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

Related memory:
- `project_record_write_protection` — landed status (optimistic +
  sys_lock), implementation pointers, open issues.
- `reference_session_pool_web_reality` — confirms the row-lock-for-
  liveness rollback (verified 2026-06-09).
