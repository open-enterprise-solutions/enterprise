# Audit log — journal registration subsystem

> **Status:** LANDED 2026-05-25 in commit `37a9808b`. Backend `ibLogger`
> + own-SQLite sink + monthly file rotation + retention sweep + viewer
> in enterprise.exe / designer.exe (`Administration → Journal
> registration` menu item). Five gtests in `tests/test_logger.cpp`
> green. Five adjacent fixes shipped alongside (listed below).
>
> **Update:** the modal `ibDialogAuditLog` viewer was later rebased to a
> doc/view tab — `ibAuditLogDocument` / `ibAuditLogView` in
> `frontend/docView/templates/docViewAuditLog.{h,cpp}` (paged model over
> `ibLoggerReader`). The "Viewer" section below reflects the current shape.

---

## What it does

Local audit trail of business events and admin actions. Designed
for the activity-log admin surface: every Save / Delete of
a Catalog or Document, every authentication outcome, every metadata
Apply, every session open / close lands as a row in a per-month
SQLite file. The viewer dialog reads back through a filter strip
(date range, level, source, user, search) and drills into the
referenced object on double-click.

**Not** a security audit trail in the SOX / GDPR sense yet — there
is no tamper-evident chain, no separate audit DB, no archival
sign-off. The current shape is a developer / admin diagnostics
tool that happens to capture business-meaningful events. The data
model is ready to be promoted into a stronger compliance role
later (sign-each-batch, chain hash, off-site replication) without
breaking the existing call sites.

---

## Architecture

```
script / business code
       │
       │ ibLog->Audit(source, event, message, refGuid, refMetaId)
       │ ibLog->Info / Warn / Error (level-gated)
       ▼
┌─────────────────────────────────────┐
│  ibLogger  (backend/logger/)        │
│  - level gate (Info/Warn skipped    │
│    when below threshold; Audit      │
│    NEVER silenced)                  │
│  - never throws — errors swallowed  │
│    inside Emit                      │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│  ibLoggerQueue  (MPSC, bounded)     │
│  - std::mutex + std::cv + deque     │
│  - drop-new on overflow + counter   │
│  - notify_one only on empty→        │
│    non-empty (avoids syscall per    │
│    producer call under load)        │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│  ibLoggerWriter  (one drain thread) │
│  - batch up to 256 entries OR       │
│    100 ms timeout                   │
│  - final drain before thread exits  │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│  ibLoggerSink (abstract)            │
│  ├── ibLoggerSinkSqlite (concrete)  │
│  │   - own ibDatabaseLayerSQLite    │
│  │     (NOT in main pool, see       │
│  │     "decisions" below)           │
│  │   - WAL + synchronous=NORMAL     │
│  │   - oes_YYYY_MM.olg per month    │
│  │     (rotation inside WriteBatch  │
│  │     based on first entry's ts)   │
│  └── future: oes-server, syslog,    │
│      external SIEM                  │
└─────────────────────────────────────┘
```

Read path (admin viewer):

```
┌─────────────────────────────────────┐
│  ibLoggerReader → vector<ibLogRow>  │
│  - ibLogFilter: from_ms/to_ms,      │
│    min_level, user_name, source,    │
│    event_type, ref_guid, search,    │
│    limit/offset                     │
│  - CollectFiles() pre-filters by    │
│    oes_YYYY_MM name (skip months    │
│    outside the filter window)       │
│  - opens each file, prepared        │
│    statement with dynamic WHERE     │
│  - ORDER BY ts_ms DESC, id DESC     │
└──────────────┬──────────────────────┘
               ▼
   frontend/docView/templates/docViewAuditLog.{h,cpp}
   (ibAuditLogDocument owns the reader;
    ibAuditLogView = filter strip + AUI
    toolbar + ibDataViewCtrl; paged
    ibAuditLogModel : ibDataViewModel)
```

Retention sweep:

```
ibLoggerSweep::RunOnce(dir, retentionDays)
  parses month from filename
  deletes files (+ .olg-wal / .olg-shm sidecars)
  whose month bucket ends before cutoff

Started by ibApplicationData::CreateLogger via
ibLogger::StartDailySweep(retentionDays).  Background thread
runs sweep + waits 24h on CV.  Default 90 days hard-coded.
```

---

## Data model

`ibLogEntry` POD — what the writer thread persists:

| Field | Type | Purpose |
|---|---|---|
| `ts_ms` | `wxLongLong_t` | Milliseconds since Unix epoch, UTC |
| `level` | `ibLogLevel` enum | `Info` / `Warn` / `Error` / `Audit` (= 0..3) |
| `session_id` | `wxString` | Source session (dashed-UUID string form of ibGuid) |
| `user_name` | `wxString` | OES user (sys_user.userName); empty for system events |
| `host` | `wxString` | Computer name + address |
| `source` | `wxString` | Subsystem tag — `auth`, `session`, `record`, `document`, `metadata` |
| `event_type` | `wxString` | Verb — `login`, `login_failed`, `password_rehash`, `opened`, `closed`, `created`, `saved`, `deleted`, `posted`, `unposted`, `applied`, `apply_failed` |
| `message` | `wxString` | Human-readable text |
| `ref_guid` | `wxString` | String form of ibGuid; empty when no ref |
| `ref_meta_id` | `int` | ibMetaID; 0 when no ref. **0 = sys_user convention** (drills via `ibDialogUserItem`, not `ibValueReferenceDataObject`) |
| `details` | `wxMemoryBuffer` | Reserved for serialised `ibValue` details — populated by Audit overloads; column kept for forward compatibility |

SQLite schema (per-month file) — emitted by `ibLoggerSinkSqlite::EnsureSchema`:

```sql
CREATE TABLE IF NOT EXISTS log_entry (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_ms       INTEGER NOT NULL,
    level       INTEGER NOT NULL,
    session_id  TEXT,
    user_name   TEXT,
    host        TEXT,
    source      TEXT NOT NULL,
    event_type  TEXT NOT NULL,
    message     TEXT,
    ref_guid    TEXT,
    ref_meta_id INTEGER,
    details     BLOB
);
CREATE INDEX IF NOT EXISTS ix_log_ts       ON log_entry(ts_ms);
CREATE INDEX IF NOT EXISTS ix_log_level_ts ON log_entry(level, ts_ms);
CREATE INDEX IF NOT EXISTS ix_log_ref      ON log_entry(ref_guid);
PRAGMA journal_mode  = WAL;
PRAGMA synchronous   = NORMAL;
```

The composite `ix_log_level_ts (level, ts_ms)` covers the viewer's
default "audit only, recent first" query without a separate ts
index hit.

---

## Audit sites

Every site is wrapped in `try/catch (...)` so logger failures never
propagate into the surrounding business flow.

| Source | Event | Call site | Ref |
|---|---|---|---|
| `auth` | `login` | `Login` after `InstallUser` | sys_user guid, meta_id=0 |
| `auth` | `login_failed` | `AuthenticateUser` (user not found / bad password) | sys_user guid (if found) |
| `auth` | `password_rehash` | `AuthenticateUser` lazy MD5→PBKDF2 upgrade | sys_user guid |
| `session` | `opened` | `OnAuthenticated` listener | — |
| `session` | `closed` | `OnDisconnect` listener | — |
| `record` | `created` / `saved` | `ibValueRecordDataObjectRef::CommitWriteScope` | object guid + meta_id |
| `record` | `deleted` | `ibValueRecordDataObjectRef::CommitDeleteScope` | object guid + meta_id |
| `document` | `posted` / `unposted` | `ibValueRecordDataObjectRecorderRef::WriteObject(wm,pm)` after `OnWrite` | object guid + meta_id |
| `metadata` | `applied` / `saved` / `apply_failed` | `ibMetaDataConfigurationStorage::OnAfterSaveDatabase` | — |

`record` covers Catalog / Document / ChartOfAccounts /
ChartOfCharacteristicTypes through the single
`CommitWrite/DeleteScope` scaffold landed in `fc4efa55` (see
`record-object-refactor.md`).

---

## Storage layout

| Run mode | Log dir | Reason |
|---|---|---|
| File-mode (Firebird embedded) | `<basedir>/oeslog` | Co-located with the `.fdb` — backup tooling captures both in one sweep |
| Server-mode (Postgres / FB server) | `%LOCALAPPDATA%\OES\<tag>\logs` | Per-DB user-local path; tag = db name OR server name OR `default`, path separators (`\` `/` `:`) sanitised to `_` |
| Launcher mode | — | `CreateLogger` early-returns; launcher has no business events to audit |

Resolved by `ibApplicationData::ResolveLogDir()` at startup. Files
named `oes_YYYY_MM.olg` + `.olg-wal` + `.olg-shm` (SQLite WAL
sidecars).

---

## Why the logger uses its own SQLite outside the main pool

This is a one-line invariant that, if broken, breaks everything
else.

`ibDatabaseLayer::BeginTransaction` calls
`ibConnectionPool::SetActiveTxConnection(shared_from_this())` — the
pool registers the layer as the active TX holder for the current
session, so subsequent `ses_query` / `db_query` calls route to the
same connection (essential for nested-TX correctness, see
`connection-pool.md`).

If the logger's private SQLite layer went through `BeginTransaction`,
it would pin itself into the pool's `m_entries`. The next
`ses_query` from a business path would receive the SQLite layer
back — and the FB-flavoured caller would get "no such table:
Document1058" immediately.

The logger uses **raw `BEGIN` / `COMMIT` / `ROLLBACK`** through
`RunQuery` to skip the pool registration. This is the only place
in the codebase where bypassing `BeginTransaction` is correct.

See memory note [[logger-own-sqlite-pool]] for the regression-test
context.

---

## Viewer (doc/view tab)

`ibAuditLogDocument` / `ibAuditLogView`
(`frontend/docView/templates/docViewAuditLog.{h,cpp}`) — a standalone
doc/view tab (no metaobject; registered via a plain `ibDocTemplate` with
no CLSID key). Replaced the original modal `ibDialogAuditLog` so the
journal stays open alongside form editors. Mirrors `ibDialogUserList`'s
toolbar layout:

- **Document** owns the `ibLoggerReader` (built from
  `appData->GetLogger()->GetLogDir()`), is read-only (`IsModified()`
  always false).
- **View** builds the UI: filter strip + `wxAuiToolBar` + `ibDataViewCtrl`.
  Filter strip: `wxDatePickerCtrl` from (default −7 days) / to, level
  dropdown (default "Audit only"), source dropdown, user + search text,
  tail checkbox.
- **Model** `ibAuditLogModel : ibDataViewModel` — **paged** (`IsPagedModel()`),
  `kPageSize = 200`. `GetFirstFetch` loads page 0; scrolling drives
  `GetNextFetch`, appending pages to the accumulating `m_loadedRows`.
  Rows wrapped in refcounted `ibAuditLogRowObject` (pins an index past a
  re-fetch). Columns start at 1 (column 0 is reserved by the
  `ibDataViewCtrl` fork): Time / Level / User / Source / Event / Message /
  Ref.
- Tail checkbox → 3 s `wxTimer` (`OnTimer` → `Reload`).
- Double-click (`OnItemActivated`) → if `ref_meta_id != 0`,
  `ibValueReferenceDataObject::Create(activeMetaData, ref_meta_id,
  ibGuid(ref_guid))->ShowValue()`; if `ref_meta_id == 0`, open
  `ibDialogUserItem` keyed by guid (sys_user convention).
- Menu wiring: **Administration → Journal registration** in both
  enterprise.exe and designer.exe. Gated by
  `AccessRight_ActiveUsers()` — splitting into a dedicated
  `ViewAuditLog` right is metadata work, parked.

---

## Lifecycle

```
ibApplicationData::CreateAppDataEnv (file or server variant)
  ├── connection pool brought up
  ├── ibApplicationData::CreateLogger
  │     resolves log dir, constructs ibLogger, starts daily sweep
  └── ...

ibApplicationData::~ibApplicationData
  ├── m_logger.reset()              ← FIRST, before sessionRegistry->Stop()
  │     writer thread joins while sessions are still resolvable
  │     for the final session.closed audit row
  ├── m_sessionRegistry->Stop()
  └── m_pluginManager->UnloadAll()
```

The reset-logger-first order matters: the last audit row written
during teardown is `session.closed` for each open session. Once
`sessionRegistry` stops, session identity is gone; flushing the
logger after that would emit rows with empty session_id / user_name.

---

## Tests

`tests/test_logger.cpp` — 5 gtests, all green at landing:

| Test | What it validates |
|---|---|
| `Logger.SinglePush_LandsAsRow` | 1 `Info` → 1 row in SQLite |
| `Logger.AuditLevelPersists` | 2 `Audit` calls, count == 2; level gate doesn't drop Audit |
| `Logger.MultiThread_NoDrops_AtModerateLoad` | 8 threads × 1000 pushes, count == 8000, drop counter == 0 |
| `LoggerReader.AuditWithRef_RoundTrip` | Push with refGuid + meta_id, query by `ref_guid` and by `source`, both return the row |
| `LoggerSweep.RemovesOldFilesOnly` | Fresh + `oes_2020_01.olg`, sweep retention=30 → only fresh survives |

---

## Pending / open

1. **`ViewAuditLog` access right** — currently gated by
   `AccessRight_ActiveUsers()`; splitting into a dedicated right is
   metadata work (new role, designer UI, predefined entries).
2. ~~**`details` BLOB serialisation**~~ — **LANDED** (verified 2026-06-28):
   the `details` column is a live `wxMemoryBuffer` on `ibLoggerEntry`
   (`loggerEntry.h`) and the logger accepts an `ibValue&` overload
   (`logger.h`) — serialised `ibValue` details are written, not reserved.
3. **Retention via worker pool** — sweep runs daily in-process via a
   dedicated CV thread. Optional: drive via `ibWorkerPool` so
   headless modes share the primitive. Not blocking.
4. **Hot-path audit perf tuning** — current estimate <0.1% of write-TX
   cost; revisit if profiling shows otherwise.
5. **Cross-instance unified view** — orthogonal to journal but
   mentioned as the future home in `compute-server-tiering.md`. The
   data model (per-month SQLite files) is replication-friendly.

---

## Adjacent fixes that shipped alongside

Five unrelated issues surfaced while wiring the logger. Each fixed
in the same commit because they blocked the bring-up.

### 1. `ibDataViewIndexListModel::GetFirstFetch` bridge

After the paged-fetch refactor, `BuildListHelper` /
`BuildTreeHelper` inside `ibDataViewCtrl` started calling
`model->GetFirstFetch(...)` instead of `model->GetChildren(...)`.
The base class's default `GetFirstFetch` returns 0, so any control
backed by `ibDataViewIndexListModel` (userList, the new journal
viewer, others) rendered empty even when `Reset(N)` populated
`m_hash`.

Fix in `dataview.h` — a thin override that delegates to
`GetChildren`:

```cpp
virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
    const ibDataViewItem& /*anchor*/, int /*count*/,
    ibDataViewItemArray& out) const wxOVERRIDE
{
    return GetChildren(parent, out);
}
```

### 2. `ibDataViewCtrl::m_viewMode` default → `ibDataViewList`

Default was `ibDataViewTree`. `BuildTreeHelper` opens with
`if (!item.IsContainer()) return;` — the invalid root item from
`BuildTree`'s initial call is non-container for flat lists, so the
top-level fetch never ran. Switched the default to
`ibDataViewList` so legacy callers (which never explicitly call
`SetViewMode`) get the correct routing. Callers that need tree
semantics flip via `SetViewMode` after construction.

See `datavgen.cpp` near line 3953. Memory note
[[dataview-default-list-mode]] keeps the regression guard.

### 3. `ibFirebirdMaintenanceScheduler::Stop` — `std::async` → CV

`Stop`'s "join with timeout via `std::async`" pattern crashed on
DLL detach with `INVALID_PARAMETER c000000d` inside
`CreateThreadpoolWork`. Windows TPP is torn down before atexit
handlers run, so spawning new threads from `std::atexit([]{
ibFirebirdMaintenanceScheduler::Stop(); })` no longer works.

Replaced the futures-based timed join with a
`std::condition_variable` + `bool workerDone` set by the worker
right before returning. `Stop` `wait_for`s on the CV — no new
thread needed, safe at DllMain detach time. If timeout fires,
detach as before.

Memory note [[dll-atexit-no-threads]] documents the Windows TPP
teardown order.

### 4. `ibUserInfo::Read` chunk hardening

Each chunk read inside `FillFromRow` wrapped in `try { ... } catch
(...) {}` individually. A malformed chunk (e.g. role for a deleted
metadata object) no longer takes down the whole sys_user read;
identity stays populated from row columns, affected chunk fields
stay default.

`ReadPasswordChunk` / `ReadLanguageChunk` — read into locals first,
commit to `info` only after all reads succeed. Atomic-on-success
prevents the half-populated-then-throw state that previously left
`m_strUserPassword` set to a corrupted blob string — which made
Verify fail for users created with empty password.

`ReadRoleChunk` — per-role try/break; one bad role doesn't kill
subsequent ones.

### 5. `ibSession::Open` — pin `this` while showing auth dialog

```cpp
{
    ibSessionScope scope(this);
    dlgOk = OnShowAuthenticate(user, password);
}
```

The dialog's OK handler calls `appData->Login → InstallUser`,
which uses `ibSession::Current()` to choose the target. Without
the scope, `Current()` resolved to nullptr / wrong session on the
main thread → `m_userInfo` stayed empty → `submitAttach` went with
blank creds → second pass through `ProcessAttach` returned "invalid
user or password" even though the dialog showed success.

Scope ensures `InstallUser` writes to `this`, and the subsequent
`submitAttach` reads the freshly-set `m_userInfo`.

---

## Files touched (at landing)

Backend:

- `backend/logger/` (new) — 14 files: `logger.{h,cpp}`, `loggerEntry.h`,
  `loggerQueue.{h,cpp}`, `loggerReader.{h,cpp}`, `loggerSink.h`,
  `loggerSinkSqlite.{h,cpp}`, `loggerSweep.{h,cpp}`,
  `loggerWriter.{h,cpp}`.
- `backend/appData.{h,cpp}` — `m_logger` member, `GetLogger()`, `ibLog`
  macro, `ResolveLogDir` / `CreateLogger`, audit at
  `AuthenticateUser` / `Login` / `OnAuthenticated` / `OnDisconnect`,
  `DescribeSessionKind` helper.
- `backend/databaseLayer/firebird/firebirdMaintenanceScheduler.cpp` — CV
  pattern in `Stop`, `workerDone` signal in `WorkerLoop`.
- `backend/metaCollection/partial/commonObjectRefQuery.cpp` — audit at
  `CommitWriteScope` / `CommitDeleteScope` passing `ref_guid` +
  `ref_meta_id`.
- `backend/metaCollection/partial/commonObject.{h,cpp}` — audit at
  `WriteObject(wm, pm)` for `document.posted` / `unposted`.
- `backend/metaCollection/partial/{catalog,chartOfAccounts,chartOfCharacteristicTypes,document}Metadata.cpp`
  — restored 6 `SetDefaultProcedure` hooks in each leaf ctor (moved
  out of `ibValueMetaObjectRecordDataMutableRef` base ctor which
  cannot see the leaf field `m_propertyObjectModule`).
- `backend/metadataConfigurationQuery.cpp` — audit at
  `OnAfterSaveDatabase` for `metadata.applied` / `saved` /
  `apply_failed`.
- `backend/session/session.cpp` — `ibSessionScope` wrap around
  `OnShowAuthenticate`.
- `backend/userInfo.cpp` — per-chunk try/catch in `FillFromRow`,
  atomic-on-success in `ReadPasswordChunk` + `ReadLanguageChunk`,
  per-entry try/break in `ReadRoleChunk`.

Frontend:

- `frontend/win/ctrls/dataview/dataview.h` — `GetFirstFetch`
  override on `ibDataViewIndexListModel`.
- `frontend/win/ctrls/dataview/datavgen.cpp` — `m_viewMode` default
  flipped to `ibDataViewList`.
- `frontend/win/dlgs/auditLog.{h,cpp}` (new at landing) — modal viewer
  dialog. **Later removed** and replaced by the doc/view tab
  `frontend/docView/templates/docViewAuditLog.{h,cpp}` (see "Viewer" above).

App shells:

- `enterprise/mainFrame/mainFrameEnterprise.{h,cpp,Menu.cpp,Event.cpp}`
  — `wxID_ENTERPRISE_AUDIT_LOG`, menu item, `OnAuditLog` handler.
- `designer/mainFrame/mainFrameDesigner.{h,cpp,Menu.cpp,Event.cpp}` —
  `wxID_APPLICATION_AUDIT_LOG`, menu item, `OnAuditLog` handler.

Tests:

- `tests/test_logger.cpp` (new) — 5 tests.
- `tests/CMakeLists.txt` — `test_logger.cpp` added.

---

## Related docs

- [`record-object-refactor.md`](record-object-refactor.md) — the
  `CommitWriteScope` / `CommitDeleteScope` scaffold that the
  `record` audit sites hook into. Without that consolidation the
  audit would have needed 6 copy-paste call sites.
- [`session-registry.md`](session-registry.md) — `OnAuthenticated`
  / `OnDisconnect` listener phases; `ibSessionScope` discipline.
- [`connection-pool.md`](connection-pool.md) — why the logger's
  SQLite must NOT go through `BeginTransaction`.
- [`compute-server-tiering.md`](compute-server-tiering.md) — future
  home for unified cross-instance log view.
