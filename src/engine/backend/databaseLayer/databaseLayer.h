#ifndef __DATABASE_LAYER_H__
#define __DATABASE_LAYER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/hashset.h>
#include <wx/arrstr.h>
#include <wx/variant.h>

#include <memory>
#include <vector>

#include "databaseLayerDef.h"
#include "databaseErrorReporter.h"
#include "databaseStringConverter.h"
#include "databaseResultSet.h"
#include "preparedStatement.h"

// ==========================================================================
// ibDialectDictionary — the per-DBMS "dictionary" that closes all dialect
// difference (docs/query-language-arc.md §6). The dialect is an L1 driver
// DESCRIPTOR — each driver self-describes its SQL via GetDialect() below — so it
// lives here, with the driver interface (moved from the former dialectDictionary.h).
// Level 2 stays DBMS-indifferent: ONE generic renderer walks the ibQueryIR and, at
// every divergence point, consults this dictionary. Adding a DBMS = a new dictionary,
// not an edit to L2.
// ==========================================================================

// Parameter placeholder style (bound positionally; 1-based, matching SetParam*).
enum class ibParamStyle
{
	QuestionMark,   // ?     — Firebird, SQLite, ODBC, MySQL
	DollarN,        // $1    — PostgreSQL
	Colon           // :p1   — Oracle-style named
};

// Row-limit / offset form.
enum class ibPagination
{
	FirstSkip,      // SELECT FIRST n SKIP m ...   — Firebird
	LimitOffset,    // ... LIMIT n OFFSET m        — PostgreSQL, SQLite, MySQL
	OffsetFetch,    // ... OFFSET m ROWS FETCH NEXT n ROWS ONLY  — MSSQL / ANSI
	Top             // SELECT TOP n ...            — MSSQL (legacy)
};

// Boolean literal spelling.
enum class ibBoolForm
{
	TrueFalse,      // TRUE / FALSE   — PostgreSQL
	OneZero,        // 1 / 0          — SQLite, MySQL
	Smallint        // 1 / 0 stored as SMALLINT — Firebird (no native boolean pre-3)
};

// Optional capabilities the renderer queries before choosing a form.
struct ibSqlFeatures
{
	bool m_window        = false;   // SUM() OVER (...)
	bool m_cte           = false;   // WITH ... AS (...)
	bool m_fullOuterJoin = false;
	bool m_iLike         = false;   // case-insensitive LIKE
	bool m_rollup        = false;   // GROUP BY ROLLUP(...) — hierarchical subtotals server-side (FB 2.1+ / PG / MySQL8; NOT SQLite)
};

struct ibDialectDictionary
{
	// --- declarative facts (close the large majority of divergence) -------

	ibParamStyle m_paramStyle = ibParamStyle::QuestionMark;
	ibPagination m_pagination = ibPagination::LimitOffset;
	ibBoolForm   m_boolForm   = ibBoolForm::TrueFalse;

	// DROP INDEX divergence: MySQL needs "DROP INDEX <name> ON <table>"; FB / PG /
	// SQLite take just "DROP INDEX <name>". Default = standalone (no table).
	bool m_dropIndexNeedsTable = false;

	// UPSERT (INSERT-or-update by PK) as a TEMPLATE the renderer fills. Placeholders:
	//   {table} {columns} {values} {keys} {update}
	// {update} = the conflict-update body: m_upsertUpdateItem per non-key column,
	// comma-joined. Firebird's MATCHING needs no update body (empty item). Default =
	// PG / SQLite ON CONFLICT.
	wxString m_upsertTemplate   = wxT("INSERT INTO {table} ({columns}) VALUES ({values}) ON CONFLICT ({keys}) DO UPDATE SET {update}");
	wxString m_upsertUpdateItem = wxT("{col} = excluded.{col}");

	// Identifier quoting — <open><name><close>. Default = NONE (bare identifiers, and
	// avoids Firebird turning quoted names case-sensitive). Opt-in: set to "\"" (ANSI).
	wxString m_identQuoteOpen;
	wxString m_identQuoteClose;

	ibSqlFeatures m_features;

	// --- type map (DDL): canonical column type -> dialect SQL type --------
	// Parameterised types are printf patterns the renderer fills. BLOB vs BYTEA,
	// DATE vs TIMESTAMP, boolean-as-SMALLINT live here.
	wxString m_typeBoolean       = wxT("BOOLEAN");
	wxString m_typeInteger       = wxT("INTEGER");
	wxString m_typeDate          = wxT("TIMESTAMP");
	wxString m_typeBlob          = wxT("BLOB");
	wxString m_typeGuid          = wxT("CHAR(36)");
	wxString m_typeStringPattern = wxT("VARCHAR(%d)");
	wxString m_typeNumberPattern = wxT("DECIMAL(%d,%d)");

	// ALTER COLUMN (type change) as a TEMPLATE — {table} {column} {type}. Default =
	// PG / Firebird "ALTER COLUMN c TYPE t"; MySQL "MODIFY COLUMN c t"; SQLite leaves
	// it EMPTY (no in-place type change) so the renderer THROWS rather than emulate.
	wxString m_alterColumnTemplate = wxT("ALTER TABLE {table} ALTER COLUMN {column} TYPE {type}");

	// Row-lock clause APPENDED to a top-level SELECT for a pessimistic read-for-update (the
	// register set lock). Default = PG / MySQL " FOR UPDATE"; Firebird overrides to " WITH
	// LOCK"; SQLite leaves it EMPTY (it locks the whole DB per transaction — the open TX IS
	// the lock, there is no row-level FOR UPDATE). Rendered after ORDER BY / LIMIT.
	// (docs/record-locks.md)
	wxString m_rowLockSuffix = wxT(" FOR UPDATE");

	// GROUP BY ROLLUP spelling (used only when m_features.m_rollup): the keys render between
	// prefix and suffix. Standard (PG / FB): "GROUP BY ROLLUP(<keys>)". MySQL: prefix empty,
	// suffix " WITH ROLLUP" -> "GROUP BY <keys> WITH ROLLUP". (docs/query-language-arc.md §22.1b)
	wxString m_rollupPrefix = wxT("ROLLUP(");
	wxString m_rollupSuffix = wxT(")");

	// --- behaviour slots (the small tail data cannot express) -------------
	// Reserved for emulation rewrites (FULL OUTER -> LEFT UNION RIGHT, window ->
	// correlated subquery) as strategy callbacks, so L2 still reads only the dictionary.
};

// ==========================================================================
// ibTempTableDialect — the per-driver facts for materialising an intermediate into a DB
// TEMPORARY table: the developer-driven optimisation lever — give the DBMS optimiser a real,
// cardinality-bearing materialised set instead of a mis-estimated subquery. Vended SEPARATELY and
// NULLABLE (ibDatabaseLayer::GetTempTableDialect):
// its **presence IS the capability**. A driver that returns nullptr has no DB temp tables, so L3
// materialises the intermediate in RAM (ibQueryComposer). That RAM path is the always-works
// FLOOR and also the runtime INSURANCE: if a present-dialect CREATE/INSERT fails at runtime (no
// DDL right, read-only replica, FB GTT pool not provisioned, temp-space out), the planner falls
// back to RAM rather than erroring — the fast path is opportunistic, correctness always lands.
//
// PURE DECLARATIVE FACTS + a strategy DISCRIMINATOR. The behavioural fork (ad-hoc create vs FB's
// pre-declared GTT pool) is captured here only as the m_strategy enum (data); the actual
// control-flow fork lives in the temp-table MANAGER that reads it — never as `if(driver)` in
// this struct. Keeping that line is what preserves "dictionary = facts, behaviour elsewhere".
// (docs/query-language-arc.md — temp-db foundation)
// ==========================================================================
struct ibTempTableDialect
{
	//   AdHocCreate     : CREATE a fresh temp table per query, INSERT, DROP / auto-drop
	//                     (SQLite / PostgreSQL / MySQL / MSSQL — any-shape, on the fly).
	//   PreDeclaredPool : grab a typed GLOBAL TEMPORARY TABLE from a schema-declared pool, INSERT,
	//                     ON COMMIT clears the private rows (Firebird — GTTs are fixed-shape schema
	//                     objects, NOT ad-hoc; arbitrary-shape intermediates need a generic pool or
	//                     fall back to RAM).
	enum class Strategy { AdHocCreate, PreDeclaredPool };
	Strategy m_strategy = Strategy::AdHocCreate;

	// CREATE prefix (ad-hoc) — the manager appends " <name> (<cols>)" + m_onCommitClause.
	wxString m_createPrefix   = wxT("CREATE TEMPORARY TABLE");
	// Clause appended after the column list: PG " ON COMMIT DROP"; FB GTT " ON COMMIT DELETE ROWS";
	// SQLite / MySQL empty (connection-scoped, auto-dropped on disconnect).
	wxString m_onCommitClause;
	// Does the temp table drop itself at session / commit end? false => the manager emits DROP.
	bool     m_autoDrops      = true;
	// DROP statement prefix (used only when !m_autoDrops) — the manager appends the name.
	wxString m_dropPrefix     = wxT("DROP TABLE");
};

WX_DECLARE_HASH_SET(ibDatabaseResultSet*, wxPointerHash, wxPointerEqual, DatabaseResultSetHashSet);
WX_DECLARE_HASH_SET(ibPreparedStatement*, wxPointerHash, wxPointerEqual, DatabaseStatementHashSet);

class ibDatabaseConnectionHolder;
class ibConnectionPool;

class BACKEND_API ibDatabaseLayer
	: public ibDatabaseErrorReporter
	, public ibDatabaseStringConverter
	, public std::enable_shared_from_this<ibDatabaseLayer>
{
public:
	/// Constructor
	ibDatabaseLayer();

	/// Destructor
	virtual ~ibDatabaseLayer();

	// Open database
	virtual bool Open(const wxString& strDatabase) = 0;

	/// close database  
	virtual bool Close() = 0;

	/// Is the connection to the database open?
	virtual bool IsOpen() = 0;

	/// clone database  
	virtual ibDatabaseLayer *Clone() = 0;

	// transaction support

	// Optional transaction attributes. Struct so future knobs (lock
	// timeout, isolation level override) can land without changing the
	// BeginTransaction signature again. Default-constructed value
	// preserves historical behaviour — every existing caller works
	// unchanged.
	//
	// `noWait` — when true, row-lock contention raises a lock conflict
	// immediately instead of blocking. Used by row-lock probes
	// (TryProbeRowLock). On FB maps to `isc_tpb_nowait`; other drivers
	// approximate via session-level lock_timeout settings or ignore.
	//
	// `readOnly` — caller promises this TX won't issue DML. The driver
	// can then pick an isolation that doesn't acquire write-intent locks
	// (FB 4+: read_committed + read_consistency for snapshot-style
	// reads without long-running snapshot TX; PostgreSQL: SET TRANSACTION
	// READ ONLY). Drivers that don't have a cheap read-only mode silently
	// ignore the flag.
	struct ibTxOptions {
		bool noWait = false;
		bool readOnly = false;
	};

	// Transaction support — nested-safe counter layer.
	//
	// Public BeginTransaction / Commit / RollBack are non-virtual
	// wrappers that implement depth-counter + aborted-flag semantics
	// so that nested calls (Document.Write triggering register writes
	// triggering their own transactions; runtime BeginTransaction
	// wrapping several object writes) collapse onto a single real
	// driver transaction:
	//
	//   - First Begin drives DoBeginTransaction; subsequent nested
	//     Begins only bump the counter.
	//   - Commit on the outermost level drives DoCommit — unless any
	//     inner RollBack fired, in which case the aborted flag turns
	//     the outer Commit into a real DoRollBack. "Inner rollback
	//     poisons outer commit."
	//   - RollBack sets the aborted flag and decrements; when the
	//     counter reaches 0 the real DoRollBack fires.
	//
	// Drivers override DoBeginTransaction / DoCommit / DoRollBack with
	// their dialect-specific SQL or native API calls. Drivers must
	// NOT touch m_txDepth / m_txAborted directly — the base owns them.

	/// Begin a transaction. The default options preserve historical
	/// (wait-mode, read-committed, read-write) behaviour.
	void BeginTransaction(const ibTxOptions& opts = {});

	/// Commit the current transaction (or RollBack if any inner level
	/// called RollBack first — see the aborted-flag semantics above).
	void Commit();

	/// Rollback the current transaction.
	void RollBack();

	/// Is a transaction currently open on this layer? Derived from the
	/// depth counter so nested levels report active correctly.
	bool IsActiveTransaction() { return m_txDepth > 0; }

	/// Holder that currently has this layer reserved for an active TX,
	/// or nullptr if the layer is not pinned. Set by the connection
	/// pool's ReserveTx (depth 0→1 on Begin), cleared by ReleaseTx
	/// (1→0 on Commit/RollBack). Identity-only — the layer doesn't
	/// share-own the holder; the holder calls ReleaseTx before its
	/// dtor so this back-pointer is always valid while non-null.
	ibDatabaseConnectionHolder* GetHolder() const { return m_holder; }

	/// True while at least one ibPreparedStatement / ibDatabaseResultSet
	/// is alive on this layer. The pool consults this in Checkout so a
	/// conn whose result set is mid-iteration cannot be handed to
	/// another caller — the driver-side cursor would race. Goes back
	/// to false when every tracked stmt/rs has been closed or
	/// destructed (LogStatementForCleanup / LogResultSetForCleanup
	/// pair with the set-removal in Close()).
	bool IsBusy() const {
		return !m_ResultSets.empty() || !m_Statements.empty();
	}

	// Define formatted run query 

	/// Run an insert, update, or delete query on the database
	WX_DEFINE_VARARG_FUNC(int, RunQuery, 1, (const wxFormatString&),
		DoRunQueryWchar, DoRunQueryUtf8);

	///  Run a select query on the database
	WX_DEFINE_VARARG_FUNC(ibDatabaseResultSet*, RunQueryWithResults, 1, (const wxFormatString&),
		DoRunQueryWithResultsWchar, DoRunQueryWithResultsUtf8);

	/// ibPreparedStatement support
	WX_DEFINE_VARARG_FUNC(ibPreparedStatement*, PrepareStatement, 1, (const wxFormatString&),
		DoPrepareStatementWchar, DoPrepareStatementUtf8);

	// function names more consistent with JDBC and wxSQLite3
	// these just provide wrappers for existing functions

	/// See RunQuery
	WX_DEFINE_VARARG_FUNC(int, ExecuteUpdate, 1, (const wxFormatString&),
		DoRunQueryWchar, DoRunQueryUtf8);

	/// See RunQueryWithResults
	WX_DEFINE_VARARG_FUNC(ibDatabaseResultSet*, ExecuteQuery, 1, (const wxFormatString&),
		DoRunQueryWithResultsWchar, DoRunQueryWithResultsUtf8);

	/// Close a result set returned by the database or a prepared statement previously
	virtual bool CloseResultSet(ibDatabaseResultSet*& pResultSet);

	// ibPreparedStatement support

	/// Close a prepared statement previously prepared by the database
	virtual bool CloseStatement(ibPreparedStatement*& pStatement);

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	/// Check for the existence of a table by name
	virtual bool TableExists(const wxString& table) = 0;

	/// Check for the existence of a view by name
	virtual bool ViewExists(const wxString& view) = 0;
	
	/// Retrieve all table names
	virtual wxArrayString GetTables() = 0;
	
	/// Retrieve all view names
	virtual wxArrayString GetViews() = 0;

	/// Retrieve all column names for a table
	virtual wxArrayString GetColumns(const wxString& table) = 0;

	// Database single result retrieval API contributed by Guru Kathiresan
	/// With the GetSingleResultX API, two additional exception types are thrown:
	///   DATABASE_LAYER_NO_ROWS_FOUND - No database rows were returned
	///   DATABASE_LAYER_NON_UNIQUE_RESULTSET - More than one database row was returned

	/// Retrieve a single integer value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual int GetSingleResultInt(const wxString& strSQL, int nField, bool bRequireUniqueResult = true);
	virtual int GetSingleResultInt(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult = true);

	/// Retrieve a single string value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual wxString GetSingleResultString(const wxString& strSQL, int nField, bool bRequireUniqueResult = true);
	virtual wxString GetSingleResultString(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult = true);

	/// Retrieve a single long value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual long GetSingleResultLong(const wxString& strSQL, int nField, bool bRequireUniqueResult = true);
	virtual long GetSingleResultLong(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult = true);

	/// Retrieve a single bool value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual bool GetSingleResultBool(const wxString& strSQL, int nField, bool bRequireUniqueResult = true);
	virtual bool GetSingleResultBool(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult = true);

	/// Retrieve a single date/time value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual wxDateTime GetSingleResultDate(const wxString& strSQL, int nField, bool bRequireUniqueResult = true);
	virtual wxDateTime GetSingleResultDate(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult = true);

	/// Retrieve a single Blob value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual void* GetSingleResultBlob(const wxString& strSQL, int nField, wxMemoryBuffer& buffer, bool bRequireUniqueResult = true);
	virtual void* GetSingleResultBlob(const wxString& strSQL, const wxString& strField, wxMemoryBuffer& buffer, bool bRequireUniqueResult = true);

	/// Retrieve a single double value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual double GetSingleResultDouble(const wxString& strSQL, int nField, bool bRequireUniqueResult = true);
	virtual double GetSingleResultDouble(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult = true);

	/// Retrieve a single number value from a query
	/// If multiple records are returned from the query, a DATABASE_LAYER_NON_UNIQUE_RESULTSET exception
	///  is thrown unless bRequireUniqueResult is false
	virtual ibNumber GetSingleResultNumber(const wxString& strSQL, int nField, bool bRequireUniqueResult = true);
	virtual ibNumber GetSingleResultNumber(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult = true);

	/// Retrieve all the values of one field in a result set
	virtual wxArrayInt GetResultsArrayInt(const wxString& strSQL, int nField);
	virtual wxArrayInt GetResultsArrayInt(const wxString& strSQL, const wxString& Field);

	virtual wxArrayString GetResultsArrayString(const wxString& strSQL, int nField);
	virtual wxArrayString GetResultsArrayString(const wxString& strSQL, const wxString& Field);

	virtual wxArrayLong GetResultsArrayLong(const wxString& strSQL, int nField);
	virtual wxArrayLong GetResultsArrayLong(const wxString& strSQL, const wxString& Field);

#if wxCHECK_VERSION(2, 7, 0)
	virtual wxArrayDouble GetResultsArrayDouble(const wxString& strSQL, int nField);
	virtual wxArrayDouble GetResultsArrayDouble(const wxString& strSQL, const wxString& Field);
#endif

	virtual int GetDatabaseLayerType() const = 0;

	// The SQL dialect this driver speaks (placeholder style, pagination, type
	// map, feature flags). L2 (ibDatabaseQueryBuilder / ibQueryRenderer) reads
	// this instead of branching on GetDatabaseLayerType() — there is no
	// "is this PG or FB" check anywhere. PURE virtual: every concrete driver
	// owns its dialect (typically a static singleton it returns by reference);
	// ODBC returns a default-constructed ANSI dictionary.
	virtual const ibDialectDictionary& GetDialect() const = 0;

	// The driver's TEMP-TABLE facts, or nullptr if it has no DB temporary tables. PRESENCE = the
	// capability: nullptr => L3 materialises an intermediate in RAM (ibQueryComposer — the
	// always-works floor + runtime insurance). A driver lights up DB temp tables (and thus
	// server-side push-down of a materialised intermediate — the optimisation lever) by
	// overriding this to vend its ibTempTableDialect. NOT pure: drivers inherit nullptr (=> RAM)
	// until each implements it, so the feature lands additively, driver by driver. Paradox by
	// design: Firebird (fixed-shape GTTs, embedded/small deployments) will typically stay on RAM,
	// while PostgreSQL / SQLite (ad-hoc any-shape temp) carry the temp-table path — capability
	// lands where the data scale needs it. (docs/query-language-arc.md — temp-db foundation)
	virtual const ibTempTableDialect* GetTempTableDialect() const { return nullptr; }

	// Best-effort reconnect when an external cluster event has
	// invalidated this connection — currently triggered by the FB
	// driver's leader-mode handoff (followers' TCP to the old
	// leader's spawned firebird.exe is dead once a new leader takes
	// over). Default no-op (other drivers don't have cluster events
	// that move connections around). Long-lived background-thread
	// callers (session-registry heartbeat / snapshot jobs) call
	// this from their catch handlers so the next tick gets a fresh
	// handle instead of looping forever on the dead one. Returns
	// true if a reattach happened.
	virtual bool ReconnectIfStale() { return false; }

	// ---- Row-level pessimistic locks (cluster-level session coordination) ----
	//
	// Used by ibSessionRegistry to hold / probe pessimistic locks on rows of
	// sys_session. The lock is the source of truth for "owner process still
	// alive" — when the connection that holds the lock drops, the DB engine
	// rolls back its transaction and the lock is released, so peer processes
	// see the row as free and can DELETE it as a zombie.
	//
	// Default implementations here return false / no-op — registry treats
	// that as "not supported on this driver" and falls back to heartbeat-
	// timestamp based liveness (fine for single-process SQLite).
	//
	// HoldRowLocks opens a dedicated transaction on THIS connection and
	// locks each row identified by (tableName, pkColumn = pkValues[i])
	// via the driver's pessimistic row lock (FB: SELECT ... WITH LOCK;
	// PG/MySQL/MSSQL: SELECT ... FOR UPDATE). Transaction stays open
	// until ReleaseRowLocks() commits. Calling again with a new set
	// commits the prior TX before starting a fresh one. Returns true if
	// every requested row was locked.
	virtual bool HoldRowLocks(const wxString& tableName,
	                          const wxString& pkColumn,
	                          const std::vector<wxString>& pkValues) { (void)tableName; (void)pkColumn; (void)pkValues; return false; }

	// Release the hold from HoldRowLocks (commits the internal TX). No-op
	// if nothing is held. Always paired with HoldRowLocks.
	virtual void ReleaseRowLocks() {}

	// Non-blocking probe: try to take a short-term exclusive lock on
	// (tableName, pkColumn = pkValue). Returns true if acquired —
	// signalling that no other connection holds the row, so the caller
	// may treat it as a zombie and DELETE it. The probe transaction is
	// rolled back before return so no lock survives the call.
	virtual bool TryProbeRowLock(const wxString& tableName,
	                             const wxString& pkColumn,
	                             const wxString& pkValue) { (void)tableName; (void)pkColumn; (void)pkValue; return false; }

	// ---- Row-level pessimistic write lock for Write-time DataVersion check ----
	//
	// Used by the optimistic-concurrency Write protection: every
	// mutable-ref WriteObject does a `SELECT DataVersion FROM <tbl>
	// WHERE pk = ? <RowLockHint> <NoWaitClause>` inside its TX to pin
	// the row, compares against the version loaded at Read, then
	// updates. The two virtuals return the per-driver clause text.
	// Default empty (SQLite: single-writer naturally, no hint).
	//
	// See docs/record-locks.md for the full design.

	// SQL fragment appended to SELECT to take a row-level write lock
	// for the rest of the current TX. Per-driver:
	//   Firebird  : "WITH LOCK"
	//   PostgreSQL: "FOR UPDATE"
	//   MySQL     : "FOR UPDATE"
	//   ODBC/MSSQL: "WITH (UPDLOCK, ROWLOCK)" — placed AFTER FROM,
	//               not at end of statement (driver overrides whole
	//               clause shape via its own AppendRowLockHint helper
	//               where needed; default callers always append at end)
	//   SQLite    : "" (single-writer)
	virtual wxString RowLockHint() const { return wxEmptyString; }

	// SQL fragment appended after RowLockHint() to make acquisition
	// fail fast instead of waiting. Used for probe-style callers; the
	// regular Write path doesn't use it (wait at the driver's default
	// timeout — TPB-level on FB, session-level on MSSQL/MySQL).
	//   PostgreSQL: "NOWAIT"
	//   MySQL 8+  : "NOWAIT"
	//   Other     : "" (NOWAIT carried via TX options where supported)
	virtual wxString NoWaitClause() const { return wxEmptyString; }

	/// Close all result set objects that have been generated but not yet closed
	void CloseResultSets();

	/// Close all prepared statement objects that have been generated but not yet closed
	void CloseStatements();

protected:

	// query database

	/// Run an insert, update, or delete query on the database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQueries) = 0;

	/// Run a select query on the database
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery) = 0;

	// prepared statement support

	/// Prepare a SQL statement which can be reused with different parameters
	virtual ibPreparedStatement* DoPrepareStatement(const wxString& strQuery) = 0;

	// Driver-specific transaction operations. Called by the non-virtual
	// wrappers above only at depth transitions (0→1 for DoBegin, 1→0
	// for DoCommit / DoRollBack); intermediate nested levels never
	// reach the driver. Drivers implement these with their dialect's
	// transaction SQL or native API — and do NOT manipulate m_txDepth
	// or m_txAborted themselves.
	virtual void DoBeginTransaction(const ibTxOptions& opts) = 0;
	virtual void DoCommit() = 0;
	virtual void DoRollBack() = 0;

protected:

	/// Nested-transaction depth counter. Owned by the base-class
	/// Begin / Commit / RollBack wrappers; drivers must not touch it.
	int m_txDepth = 0;

	/// "Aborted" flag — set by any RollBack while a transaction is
	/// still open, cleared when the outermost level finally resolves.
	/// Makes the outermost Commit fall through to a real DoRollBack
	/// so an inner failure can poison an otherwise successful outer
	/// commit.
	bool m_txAborted = false;

	/// Back-pointer to the holder that has this layer reserved for an
	/// active TX. Maintained exclusively by ibConnectionPool — see
	/// ReserveTx / ReleaseTx. nullptr when not pinned.
	ibDatabaseConnectionHolder* m_holder = nullptr;
	friend class ibConnectionPool;

	/// Add result set object pointer to the list for "garbage collection"
	void LogResultSetForCleanup(ibDatabaseResultSet* pResultSet) { m_ResultSets.insert(pResultSet); }
	/// Add prepared statement object pointer to the list for "garbage collection"
	void LogStatementForCleanup(ibPreparedStatement* pStatement) { m_Statements.insert(pStatement); }

private:

	int GetSingleResultInt(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult = true);
	wxString GetSingleResultString(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult = true);
	long GetSingleResultLong(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult = true);
	bool GetSingleResultBool(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult = true);
	wxDateTime GetSingleResultDate(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult = true);
	void* GetSingleResultBlob(const wxString& strSQL, const wxVariant* field, wxMemoryBuffer& buffer, bool bRequireUniqueResult = true);
	double GetSingleResultDouble(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult = true);
	ibNumber GetSingleResultNumber(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult = true);
	wxArrayInt GetResultsArrayInt(const wxString& strSQL, const wxVariant* field);
	wxArrayString GetResultsArrayString(const wxString& strSQL, const wxVariant* field);
	wxArrayLong GetResultsArrayLong(const wxString& strSQL, const wxVariant* field);

#if wxCHECK_VERSION(2, 7, 0)
	wxArrayDouble GetResultsArrayDouble(const wxString& strSQL, const wxVariant* field);
#endif

	DatabaseResultSetHashSet m_ResultSets;
	DatabaseStatementHashSet m_Statements;

#if !wxUSE_UTF8_LOCALE_ONLY
	int DoRunQueryWchar(const wxChar* format, ...);
	ibDatabaseResultSet* DoRunQueryWithResultsWchar(const wxChar* format, ...);
	ibPreparedStatement* DoPrepareStatementWchar(const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	int DoRunQueryUtf8(const wxChar* format, ...);
	ibDatabaseResultSet* DoRunQueryWithResultsUtf8(const wxChar* format, ...);
	ibPreparedStatement* DoPrepareStatementUtf8(const wxChar* format, ...);
#endif
};

#include <memory>

// RAII guard for an ibDatabaseResultSet* obtained from ibDatabaseLayer::RunQueryWithResults
// or ibPreparedStatement::RunQueryWithResults. Calls rs->Close() and db->CloseResultSet(rs)
// in the destructor so early returns and exceptions don't leak. The db parameter accepts
// either a raw ibDatabaseLayer* or a std::shared_ptr<ibDatabaseLayer> (the common form
// across OES — e.g. the db_query macro returns shared_ptr by value).
class BACKEND_API ibResultSetGuard {
public:
	ibResultSetGuard(ibDatabaseLayer* db, ibDatabaseResultSet* rs) : m_db(db), m_rs(rs) {}
	ibResultSetGuard(const std::shared_ptr<ibDatabaseLayer>& db, ibDatabaseResultSet* rs) : m_db(db.get()), m_rs(rs) {}
	~ibResultSetGuard() { reset(nullptr); }
	ibResultSetGuard(const ibResultSetGuard&) = delete;
	ibResultSetGuard& operator=(const ibResultSetGuard&) = delete;

	ibDatabaseResultSet* get() const { return m_rs; }
	ibDatabaseResultSet* operator->() const { return m_rs; }
	explicit operator bool() const { return m_rs != nullptr; }

	void reset(ibDatabaseResultSet* rs) {
		if (m_rs != nullptr && m_db != nullptr) {
			m_rs->Close();
			m_db->CloseResultSet(m_rs);
		}
		m_rs = rs;
	}

private:
	ibDatabaseLayer* m_db;
	ibDatabaseResultSet* m_rs;
};

// RAII guard for an ibPreparedStatement* obtained from ibDatabaseLayer::PrepareStatement.
// Calls db->CloseStatement(stmt) in the destructor. Same shared_ptr accommodation as above.
class BACKEND_API ibStatementGuard {
public:
	ibStatementGuard(ibDatabaseLayer* db, ibPreparedStatement* stmt) : m_db(db), m_stmt(stmt) {}
	ibStatementGuard(const std::shared_ptr<ibDatabaseLayer>& db, ibPreparedStatement* stmt) : m_db(db.get()), m_stmt(stmt) {}
	~ibStatementGuard() { reset(nullptr); }
	ibStatementGuard(const ibStatementGuard&) = delete;
	ibStatementGuard& operator=(const ibStatementGuard&) = delete;

	ibPreparedStatement* get() const { return m_stmt; }
	ibPreparedStatement* operator->() const { return m_stmt; }
	explicit operator bool() const { return m_stmt != nullptr; }

	void reset(ibPreparedStatement* stmt) {
		if (m_stmt != nullptr && m_db != nullptr) {
			m_db->CloseStatement(m_stmt);
		}
		m_stmt = stmt;
	}

private:
	ibDatabaseLayer* m_db;
	ibPreparedStatement* m_stmt;
};

#endif // __DATABASE_LAYER_H__

