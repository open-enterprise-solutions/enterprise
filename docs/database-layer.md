# Database layer — the driver abstraction

> **Scope:** `backend/databaseLayer/` (~191 files) — the abstraction every DB access goes
> through, its lineage, what is inherited and what is ours.
> Companions: [connection-pool.md](connection-pool.md) (the pool built on top),
> [database-modes.md](database-modes.md) (file vs server),
> [firebird-driver-hardening.md](firebird-driver-hardening.md),
> [firebird-mesh-driver.md](firebird-mesh-driver.md).
> This is foundation code.

---

## 1. Lineage — a fork of wxDatabaseLayer

`ibDatabaseLayer` descends from **wxDatabaseLayer** (originally hosted on **wxCode**) — a
cross-platform C++ library giving wxWidgets a database-independent API. Its last stable
release was **2009**; it was used in legacy projects such as CodeLite's DatabaseExplorer
plugin.

**Why it was taken, stated plainly:** there was no in-house reference for *how to write a
driver*. wxDatabaseLayer arrived with roughly a dozen working drivers behind one
architecture — it served as the teacher, not just the code. That is the same pattern as the
wx fork elsewhere in this engine ([uikit.md](uikit.md),
[form-editor.md § 1](form-editor.md)): the inherited code is a tutor you outgrow.

**How much is left:** the drivers have been reworked heavily, and more so recently — on the
order of **~40% resemblance to the original** today. The architecture stayed; the
implementations largely did not.

### 1.1 Reading the lineage from the tree

There is **no attribution header** in these files (unlike the form editor, where
`// Author : Maxim Kornienko, wxFormBuilder` is kept). What marks the inherited files is a
2009-era relic — `#include <wx/wxprec.h>` plus:

```cpp
#ifdef __BORLANDC__
#pragma hdrstop
#endif
```

Borland C++ support. It survives in exactly the upstream-derived files:

```
databaseLayer.h · databaseResultSet.h · databaseErrorReporter.h
databaseStringConverter.h · databaseQueryParser.h · firebird/firebirdInterface.h
```

Grep for `__BORLANDC__` and you have the inherited surface — the same trick as grepping
`wxFormBuilder` in the editor.

> **Worth doing:** the wxDatabaseLayer credit is not recorded anywhere in the tree. The
> project keeps upstream credits where they exist (ttmath, wxFormBuilder); this one is
> missing rather than deliberately removed.

---

## 2. The inherited architecture

The canonical wxDatabaseLayer shape, still intact:

| Class | Role |
|---|---|
| `ibDatabaseLayer` | the abstract connection — open, run, prepare |
| `ibPreparedStatement` | parameterised statement |
| `ibDatabaseResultSet` | forward cursor over rows |
| `ibResultSetMetaData` | column metadata of a result set |
| `ibDatabaseLayerException` | error type |
| `ibDatabaseQueryBuilder` / `ibDatabaseQueryParser` | SQL text handling |

Two mixins are inherited by both the statement and the result set, and they explain the
shape:

```cpp
class BACKEND_API ibPreparedStatement  : public ibDatabaseErrorReporter, public ibDatabaseStringConverter { … };
class BACKEND_API ibDatabaseResultSet  : public ibDatabaseErrorReporter, public ibDatabaseStringConverter { … };
```

- **`ibDatabaseErrorReporter`** — every DB object carries its own error state (code +
  message) rather than throwing from the driver. Drivers report; the layer above decides.
- **`ibDatabaseStringConverter`** — per-object encoding conversion. Each backend disagrees
  about charsets, so the conversion sits on the object that touches the bytes.

Usage is the same everywhere ([../CLAUDE.md](../CLAUDE.md) § Important Patterns):

```cpp
ibPreparedStatement* stmt = db_query->PrepareStatement(wxT("SELECT * FROM %s WHERE id = ?"), table);
stmt->SetParamInt(1, id);
ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
```

**Never concatenate user values into SQL** — the prepared statement is the only sanctioned
path ([../CLAUDE.md](../CLAUDE.md) § What Not To Do).

---

## 3. What is ours

The upstream library had no notion of pooling, scoping or RAII — those are additions:

| Addition | File | Why |
|---|---|---|
| `ibConnectionPool` | `connectionPool.{h,cpp}` | single owner of every connection in the process |
| `ibConnectionScope` | `connectionScope.{h,cpp}` | scoped checkout/return |
| `ibDatabaseConnectionHolder` | `connectionHolder.h` | identity tag for "can reserve a connection across threads" |
| `ibResultSetGuard` / `ibStatementGuard` | `databaseLayer.h` | RAII over result sets / statements |

### ⚠ Result sets AND statements have TWO owners — use the guards

`RunQueryWithResults` and `PrepareStatement` hand the caller a pointer **and register it with
the layer**, which closes whatever is still registered when it goes away. So the caller does
not own it alone, and a plain `delete` — including `std::unique_ptr<ibDatabaseResultSet>` or
`std::unique_ptr<ibPreparedStatement>` — leaves that registration pointing at freed memory.

The damage lands nowhere near the mistake: every test passes, and the process dies at
teardown when the layer's destructor walks its list. The only hint in the log is one debug
line — `ResultSet NOT closed and cleaned up by the ibDatabaseLayer dtor`, or
`PreparedStatement NOT closed and cleaned up by the DatabaseLayer dtor` — printed just before
the crash. It reads as a leak warning and is in fact the crash announcing itself
(`oes_pg_dialect_test`, SIGSEGV in CI on 2026-08-05: once for a result set, then again the
same day for a statement the fix had left behind).

`ibResultSetGuard` / `ibStatementGuard` exist for exactly this: the first calls `Close()`
**and** `CloseResultSet()`, the second calls `CloseStatement()`, so both owners agree the
object is gone.

```cpp
ibResultSetGuard rs(db, db->RunQueryWithResults(wxT("SELECT …")));
if (rs && rs->Next())
    value = rs->GetResultString(1);

ibStatementGuard stmt(db, db->PrepareStatement(wxT("INSERT INTO t (v) VALUES (?)")));
stmt->SetParamInt(1, 42);
stmt->RunQuery();
```

> **Amended 2026-08-23 — the statement half of that is closed** (§3d). An `ibPreparedStatement`
> now strikes itself out of the layer's books as it dies, so a plain `delete` no longer leaves a
> dangling registration. **`ibDatabaseResultSet` has no such back-pointer**, so the paragraph above
> still holds for result sets exactly as written. Use both guards regardless — they are also what
> makes `Close()` happen.

`ibDatabaseConnectionHolder` is worth reading as a design note in its own right:

```
// identity tag for "something that can reserve a database connection across thread
// boundaries". The runtime holder today is ibSession; the pool keys its active-transaction
// reservation map on this base pointer so the pool / databaseLayer don't pull in session.h,
// and so future non-session holders (compute-server batch runner, daemon job scope) can
// plug in without growing ibSession.
```

An empty base used purely as an identity — it keeps `session.h` out of the database layer
and leaves room for non-session holders. See [connection-pool.md](connection-pool.md) and
[compute-server-tiering.md](compute-server-tiering.md).

### 3.1 Two return values a driver owes, and what they are NOT

**The affected-row count.** `RunQuery` / a prepared statement's `RunQuery` return the number of
rows the statement touched. `DATABASE_LAYER_QUERY_RESULT_ERROR` is `0` — which is also a
legitimate count (DDL, a `SET`, an `UPDATE` whose `WHERE` matched nothing), so **testing a return
value against that constant asks a question it cannot answer**. Failure is signalled by
`ThrowDatabaseException`, and that is the only reliable signal.

This was not theoretical: Firebird's `DoRunQuery` and PostgreSQL's both returned a hardcoded `1`
(PostgreSQL even read `PQcmdTuples` and threw the parse away), while SQLite and ODBC
reported the truth — so the same script call answered differently per driver, and the call sites
that compared against the sentinel only ever "worked" because of the fake `1`. Fixed 2026-08-03;
`databaseErrorCodes.h` carries the rule. Firebird's `execute_immediate` path has no statement
handle, so no `isc_info_sql_records` round trip is possible there — it reports `0` and says so;
every DML goes through the prepared path, whose wrapper reads the real counts.

**`RETURNING`.** A write can hand back columns from the rows it wrote:
`m_returningClause` in `ibDialectDictionary` (`"RETURNING"` on Firebird, PostgreSQL and
SQLite 3.35+; **empty** on ODBC). Empty means the renderer **throws** rather than
emulating it — the stand-in (write, then `SELECT`) drops exactly the atomicity that makes it worth
asking for. Built at L2 with `ibReturning(dml, {cols})` and run with `ExecuteReturning`, which
yields a cursor like any `SELECT`. See [query-engine-layers.md § L2](query-engine-layers.md).

---

## 3a. A failed statement reports; it does not decide (2026-08-06)

One rule, and every driver now keeps it: **a statement that fails throws
(`ThrowDatabaseException`) and leaves the transaction to whoever opened it.** Only a
transaction the driver started itself does it close, and then through the public
`RollBack()` so the base keeps its own books — `databaseLayer.h` says it outright,
*drivers must not touch `m_txDepth` / `m_txAborted`*.

Postgres and ODBC always did this. Firebird did not: in eight places it rolled
the CALLER's transaction back natively (`isc_rollback_transaction` on the raw handle),
reasoning that "the caller's own depth is theirs to resolve". It is not resolvable —
nothing tells the caller its transaction is gone. The depth counter stayed up,
`IsActiveTransaction()` kept answering true over a dead handle, so the rollback that
followed rolled back nothing and **everything issued in between ran with no transaction
at all, committing as it went**.

That is how a failed configuration apply left the schema ahead of the configuration with
no way back: by the time the DDL landed it was not inside a transaction. The two rollbacks
that remain in that driver are the legitimate ones — `DoRollBack` itself, and the
best-effort one before `isc_detach_database` (a connection cannot be detached with an open
transaction).

⚠ Not covered by a test. It is on the path of every configuration update, which makes it
the most valuable fixture missing from the suite ([ROADMAP.md](ROADMAP.md), metadata test
coverage).

---

## 3b. ⭐⭐ An empty virtual is the worst shape a binder can take (2026-08-14)

`ibPreparedStatement` (`databaseLayer/preparedStatement.h`) declares the blob binder twice — once for
raw bytes, once for a `wxMemoryBuffer`:

```cpp
virtual void SetParamBlob(int nPosition, const wxMemoryBuffer& buffer) {
    SetParamBlob(nPosition, buffer.GetData(), (long)buffer.GetDataLen());   // ← was: {}
}
virtual void SetParamBlob(int nPosition, const void* pData, long nDataLength) = 0;
```

The buffer overload used to have an **empty body**, and **no driver overrides it** — Firebird,
PostgreSQL, SQLite and ODBC each implement the `(const void*, long)` one and nothing else. So
any caller holding a `wxMemoryBuffer` picked the empty overload by ordinary overload resolution and
bound **nothing**: no error, no exception, no log line, a row written with a NULL where its payload
should have been. Found by audit, not by a failure — which is the point.

**Why this shape is the worst of the three available.** A pure virtual is *abstract*: forget it and
the driver does not link. A correct default is *correct*: forget it and nothing is lost. An empty
non-pure virtual is neither — nobody is forced to implement it, and it does not do what its name
promises, so the failure mode is a **silent wrong answer** in whichever caller happened to choose it.
The rule this leaves: a virtual with an empty body is either abstract (`= 0`) or has a real default;
"the derived class will surely override it" is not a third option, because nothing checks.

**Blast radius, stated honestly.** The only `wxMemoryBuffer` caller in the tree is the audit sink,
`logger/loggerSinkSqlite.cpp:139`, binding `ibLogEntry::details`. That payload is **empty today** —
`ibLogger::Emit` still drops `details` on the floor pending the metadata-serialisation wrapper
(`logger.cpp:174–178`, a phase-1 note) — so nothing has actually been lost yet, and the defect was
latent at its one site: it would have dropped every detail blob the day phase 3 filled them, on a
subsystem whose entire job is to be the record of what happened. Every other blob caller
(`query/columnLayout.cpp`, `query/dataMover.cpp`, `databaseQueryBuilder.cpp`) passes
`(GetData(), GetDataLen())` and was never affected. `ibQueryStatement` re-exposes the base overload
with `using ibPreparedStatement::SetParamBlob;` (`databaseQueryBuilder.h:1109`), so an L2 statement
inherited the same empty binder and the same latent hole.

---

## 3c. ⭐⭐ One committed state for a whole transaction — `snapshot` (2026-08-22)

`ibDbTxOptions` (`databaseLayer.h`, namespace scope; the nested spelling
`ibDatabaseLayer::ibTxOptions` is an alias) carried two flags and now carries three:

| Flag | Meaning |
|---|---|
| `noWait` | fail immediately instead of blocking on a conflicting lock |
| `readOnly` | the caller promises this transaction issues no DML, letting a driver pick a cheaper isolation |
| `snapshot` | **every statement in this transaction reads the SAME committed state** |

**Why the third flag exists: neither of the other two gives that.** The default is read-committed,
and `readOnly` is read-committed too — with `read_consistency`, which stabilises a **single**
statement and says nothing about the next one. So a multi-statement read wrapped in either can see
one state for the total and another for the lines under it, and contradict itself.

**It is not free.** Holding one state visible means the server keeps the record versions that state
needs, so the longer the transaction runs the more it holds. The right trade for a report; the wrong
one for a long write.

Per driver, all in `DoBeginTransaction`:

| Driver | How `snapshot` is asked for |
|---|---|
| Firebird | TPB `isc_tpb_snapshotMode` — version3 + write + wait + **concurrency** + 30 s lock timeout. With `readOnly` as well: `isc_tpb_snapshotReadMode` — version3 + read + wait + concurrency |
| PostgreSQL | `BEGIN ISOLATION LEVEL REPEATABLE READ`, and `BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY` when `readOnly` is also set. It has to be on the `BEGIN` itself — the level cannot be changed once a statement has run |
| SQLite | **nothing needed.** A deferred transaction takes its read lock at the first read and holds it to the end, so every statement in it already sees one committed state — a property, not an omission |
| ODBC | `SQL_ATTR_TXN_ISOLATION` set to `SQL_TXN_REPEATABLE_READ` through `SQLSetConnectAttr`. Best-effort: a driver that refuses the level stays at its default, which is what there was before the flag |

Firebird's write-mode snapshot is deliberate — a posting run reading what it is about to write needs
one state too, and a read TPB would refuse it. A caller says `readOnly` when it means it.

⛔ **Nothing asks for `snapshot` today** (2026-08-23). The flag and all four realisations above are
in the tree and correct; they are simply not requested. The caller they were built for —
`ibQueryReadState` (`query/queryReadState.h`), one committed state for the length of one answer — is
declared at the four read doors and opened at none of them. It was opened, and the application hung:
the read state holds a WRITE-mode snapshot for a whole answer and pins the connection to the session
for that time (`BeginTransaction` → `ibConnectionPool::SetActiveTxConnection`), so an incidental
write on the same session mid-answer — the bytecode cache, the session heartbeat — meets that
transaction and the Firebird TPB above waits (`isc_tpb_wait`). Available and unused until it is
settled which writes may happen inside a read; the account is in
[query-engine-layers.md § L3](query-engine-layers.md).

---

## 3d. "Closed" and "released" became one act (2026-08-23)

**Before:** the layer kept every statement it created in `m_Statements`, and only
`ibDatabaseLayer::CloseStatement` erased from that list **and** deleted. A statement freed by any
other route left a dangling pointer in the layer's list — so *closed* and *released* were different
acts, and exactly one door did both.

**Now:** `ibPreparedStatement` holds a back-pointer to its layer (`SetOwner`, set in
`LogStatementForCleanup` — the single place a statement is registered), and its destructor calls
`ibDatabaseLayer::ForgetStatement(this)`. Whatever destroys a statement, the books agree afterwards.

**Forget and delete are deliberately two verbs.** `ForgetStatement` **only** unregisters; deleting
stays with whoever owns the pointer. Had the destructor called the deleting door instead, each
delete would re-enter it and free the statement twice.

Consequence recorded in the code: `ibDatabaseLayer::CloseStatements()` swaps `m_Statements` into a
local before deleting, because each delete now erases from the member set — deleting while walking
the member would be erasing from the container being iterated.

---

## 3e. Prepared-statement reuse — built, measured, taken back out (2026-08-22)

Recorded here as a **known cost with a prerequisite**, not as a feature. Nothing in the tree reuses
a prepared statement today.

**The measurement** ([technology-journal.md](technology-journal.md)): 277 reference reads produced
**276 prepares** of two SQL texts that were byte for byte identical and differed only in the key
bound to them.

**What the attempt broke.** Reuse keyed on a hand-maintained "in use" mark failed both ways in one
run: statements that were never marked free again — so every ask prepared a new one and reuse bought
nothing — and one marked free too early and handed out with its cursor still open, which Firebird
answered with `-502`, *cursor already open*.

**The prerequisite.** Reuse has to know when a **cursor** is finished with, and nothing can answer
that today: `ibPreparedStatement::CloseResultSet` is a stub returning `false`, `CloseResultSets` is
empty, and nothing erases from its list. The shape that would work is handing callers a **holder that
knows its layer**, so destroying the holder returns the statement instead of freeing it.

## 4. Drivers

Present in the tree:

| Driver | Directory | Notes |
|---|---|---|
| Firebird | `firebird/` | **production**; the default, embedded in file mode ([database-modes.md](database-modes.md)) |
| PostgreSQL | `postgres/` | **production**; `OES_USE_POSTGRESQL` |
| SQLite | `sqllite/` | always embedded, no build flag; vendored `engine/sqlite3.c`. **Tests and logging only** — single-process, no materialized views, `ALTER COLUMN` throws. Never a production target |
| ODBC | `odbc/` | `OES_USE_ODBC`. The base an **MSSQL** layer derives from, with a dialect dictionary of its own |

A MySQL driver used to sit here and was **removed** (2026-08-15): the production surface is
deliberately two engines, and every extra dialect is another dictionary, another trigger shape
and another set of hazards to meet on a live base. `DATABASELAYER_MYSQL` (4) is left vacant in
`databaseLayerDef.h` rather than reused — see the note there.

**Oracle and Microsoft SQL Server are not in this tree — but they exist upstream.**
wxDatabaseLayer shipped drivers for SQLite3, Firebird, PostgreSQL, **Oracle**,
**MSSQL** and ODBC. So "no Oracle driver" is a *porting* task against a known-good
reference, not a from-scratch one — a materially different estimate, and the reason
[ROADMAP.md § 5](ROADMAP.md) should not read as "impossible".

Adding a driver means implementing `ibDatabaseLayer` + `ibPreparedStatement` +
`ibDatabaseResultSet` for the backend and wiring an `OES_USE_*` option
([BUILD.md](BUILD.md)).

---

## 5. Honest remainder

- **`sqllite/` is a typo** (two `l`s) that is now a directory name, an include path and a
  vcxproj filter. A rename candidate — cheap in isolation, noisy across project files.
- **`#ifdef __BORLANDC__` / `wxprec.h` are dead weight.** Borland C++ is not a supported
  compiler ([../CLAUDE.md](../CLAUDE.md): MSVC / Clang / GCC). Removing them is the
  cleanest possible first sweep of the inherited surface — and doing so erases the only
  marker of what came from upstream, so record the lineage (§1) before erasing it.
- The upstream credit is missing from the tree entirely (§1.1).
