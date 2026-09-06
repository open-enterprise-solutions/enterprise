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

#include <atomic>
#include <map>
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
// not an edit to L2-1.
// ==========================================================================

// Parameter placeholder style (bound positionally; 1-based, matching SetParam*).
enum class ibParamStyle
{
	QuestionMark,   // ?     — Firebird, SQLite, ODBC
	DollarN,        // $1    — PostgreSQL
	Colon           // :p1   — Oracle-style named
};

// Row-limit / offset form.
enum class ibPagination
{
	FirstSkip,      // SELECT FIRST n SKIP m ...   — Firebird
	LimitOffset,    // ... LIMIT n OFFSET m        — PostgreSQL, SQLite
	OffsetFetch,    // ... OFFSET m ROWS FETCH NEXT n ROWS ONLY  — MSSQL / ANSI
	Top             // SELECT TOP n ...            — MSSQL (legacy)
};

// Boolean literal spelling.
enum class ibBoolForm
{
	TrueFalse,      // TRUE / FALSE   — PostgreSQL
	OneZero,        // 1 / 0          — SQLite
	Smallint        // 1 / 0 stored as SMALLINT — Firebird (no native boolean pre-3)
};

// Optional capabilities the renderer queries before choosing a form.
//
// ⭐⭐ EVERY FLAG HERE IS A PROMISE, AND SETTING ONE IS TAKING RESPONSIBILITY FOR IT (Max). The rule
// is the same for all of them — window functions, CTE, ROLLUP, and whatever is added next:
//
//     TRUE  -> we commit. The statement goes out in that form. If the engine cannot digest it, the
//              ENGINE says so, the driver raises, and the report DIES — and it keeps dying until
//              somebody corrects the flag. That is the only way a wrong promise gets found.
//     FALSE -> the tier above takes the other road (fold in memory, render the long form, refuse
//              the construct out loud). A correct answer, arrived at differently.
//
// 🛑 WHAT MUST NEVER EXIST is the third behaviour: catching the engine's refusal and quietly taking
// the other road anyway. It looks like resilience and it is the opposite — a flag that lies then
// costs nothing to nobody, so it stays wrong for years and every report pays for it silently.
// (`m_rollup` was exactly that until 2026-08-22: it claimed Firebird could fold, nothing ever asked,
// and the claim went unexamined. It is measured now, and false.)
//
// ⚠ A refusal by a PUSH-DOWN GATE is a different thing and does not raise — it is asked on hot paths
// and answers "no" constantly, which is normal. See queryException.h for that line.
struct ibSqlFeatures
{
	bool m_window        = false;   // SUM() OVER (...)
	bool m_cte           = false;   // WITH ... AS (...)
	bool m_fullOuterJoin = false;
	bool m_iLike         = false;   // case-insensitive LIKE
	bool m_rollup        = false;   // GROUP BY ROLLUP(...) — hierarchical subtotals server-side
	// ⭐ AND `GROUPING(expr)` SEPARATELY, because the two do not arrive together: Firebird 5 PARSES
	// ROLLUP and has no GROUPING function (measured live 2026-08-22 — SQL error -804 "Function
	// unknown: GROUPING", which is a PREPARE error, raised after the parser had already accepted the
	// ROLLUP clause; an unknown keyword would have been -104 at parse time instead).
	//
	// The fold uses GROUPING only to learn which LEVEL a returned row belongs to. Where it is
	// missing, the same fact is read off the key columns being SQL NULL — exact here and not a
	// heuristic, because OES attributes hold typed empties and never SQL NULL, so a NULL in a result
	// can only have come from the rollup itself (queryRewrite.h leans on the same invariant).
	bool m_grouping      = false;

	// INSERT … VALUES (…), (…), (…) — several rows in ONE statement.
	//
	// Of the two PRODUCTION engines, PostgreSQL has it and FIREBIRD DOES NOT, at any version. That
	// is a difference in FORM, not in capability: the renderer writes the same rows as
	// `INSERT … SELECT … UNION ALL SELECT …` instead, which Firebird does have. So this flag never
	// reaches a caller — nobody asks "may I write many rows", they say m_extraRows and L2 spells it.
	//
	// SQLite is true as well, which matters for the TEST STAND rather than for deployment: the
	// suites run their SQL against it, so a form it could not parse would go unnoticed until a
	// live Firebird or PostgreSQL met it. Default false — an engine we know nothing about (ODBC,
	// and the MSSQL layer that will derive from it) gets the form that works everywhere.
	bool m_multiRowValues = false;
};

// A period truncation unit — "start of the minute / week / month / … containing this moment".
//
// It lives in the QUERY dictionary, not the materialization one, because truncating a period is
// an ordinary DECLARATIVE expression: it belongs in any SELECT that groups by month, whoever
// wrote it — a report, the composer, a user query — and only incidentally in a totals trigger.
// Locking it inside the dictionary that only the trigger generator can reach would have shut a
// generally useful fact into a private room.
//
// ORDERED COARSENING, and that ordering is a CONTRACT: every member is coarser than the one
// before it. Consumers rely on it to decide derivability — a value truncated to unit A can be
// re-truncated to any B >= A, and to nothing finer, because the finer information is gone.
// Insert new members in order.
//
// (Not to be confused with ibPeriodicity — that is the granularity of a record KEY, a different
// question that happens to share a word.)
enum class ibTotalsPeriod
{
	Second,
	Minute,
	Hour,
	Day,
	Week,       // starts Monday (ISO) on every engine — the truncations encode that per dialect
	TenDays,    // the 1st / 11th / 21st of the month; the last one runs 8-11 days
	Month,
	Quarter,
	HalfYear,
	Year
};

// ⭐ ONE PIECE OF A DATE, AS A NUMBER — what `YEAR(x)` / `WEEKDAY(x)` answer with. A different
// question from ibTotalsPeriod, which names a STRETCH of time and answers with a date: truncating to
// the month gives the 1st of it, taking the month gives 9. They share most of their words and none
// of their meaning, so they are separate enums rather than one with two readings.
//
// WeekDay is Monday = 1 … Sunday = 7 (ISO), pinned here rather than left to each engine's own
// numbering — a rule that differs per dialect is a report that reads differently per deployment.
enum class ibDatePart
{
	Year,
	Quarter,
	Month,
	DayOfYear,
	Day,
	Week,
	WeekDay,
	Hour,
	Minute,
	Second
};

// The RAM twin of the dialect's truncation expression: same answer, computed in C++ for the paths
// that cannot push down (a multi-source read materialises its leaves and folds them here).
//
// The two MUST agree exactly, or a query answers differently depending on whether it happened to
// co-locate — a difference that shows up as totals that reconcile in one deployment and not in
// another. So this walks the calendar (month lengths, leap years) rather than approximating with
// fixed-length arithmetic, exactly as the SQL expressions do.
BACKEND_API wxDateTime ibTruncateToPeriod(const wxDateTime& moment, ibTotalsPeriod unit);

// The start of the NEXT period after the one holding `moment` — the first instant a stored row of
// that grain no longer covers. A read whose lower boundary falls inside a grain cannot use that
// grain's stored row (it holds the part before the boundary too), so it starts at this instant and
// takes the head from the movements instead. Calendar-walking for the same reason as the truncation:
// months differ in length, and the ten-day bucket ending a month is not ten days long.
BACKEND_API wxDateTime ibNextPeriodStart(const wxDateTime& moment, ibTotalsPeriod unit);

// The LAST instant the period holding `moment` still covers — `ENDOFPERIOD(x, Month)`. Written as
// the start of the next period less one second, and said here ONCE so the RAM road and the SQL one
// cannot disagree about whether the boundary belongs to the period (it does).
BACKEND_API wxDateTime ibEndOfPeriod(const wxDateTime& moment, ibTotalsPeriod unit);

// Move a date by whole units, calendar-aware — `DATEADD(x, Month, 3)`. Adding a month to the 31st of
// a 31-day month lands on the last day of a shorter one, which is what a person means by "a month
// later" and what fixed-length arithmetic gets wrong.
BACKEND_API wxDateTime ibDateAddUnits(const wxDateTime& moment, ibTotalsPeriod unit, long count);

// How many WHOLE units lie between two moments — `DATEDIFF(a, b, Day)`. Negative when `to` is
// earlier, zero when they fall in the same unit.
BACKEND_API long ibDateDiffUnits(const wxDateTime& from, const wxDateTime& to, ibTotalsPeriod unit);

// One piece of a date as a number — `YEAR(x)`, `WEEKDAY(x)`. The RAM twin of the dialect's
// m_datePart expression, and it must agree with it to the digit.
BACKEND_API long ibReadDatePart(const wxDateTime& moment, ibDatePart part);

struct ibDialectDictionary
{
	// --- declarative facts (close the large majority of divergence) -------

	ibParamStyle m_paramStyle = ibParamStyle::QuestionMark;
	ibPagination m_pagination = ibPagination::LimitOffset;
	ibBoolForm   m_boolForm   = ibBoolForm::TrueFalse;

	// DROP INDEX divergence: MSSQL needs "DROP INDEX <name> ON <table>"; FB / PG /
	// SQLite take just "DROP INDEX <name>". Default = standalone (no table).
	bool m_dropIndexNeedsTable = false;

	// ⭐⭐ HOW MANY FIELDS AN INDEX MAY COVER. 0 (the default) = no limit worth declaring.
	//
	// A key is a list of LOGICAL columns; an index is built over their PHYSICAL fields, and a
	// reference is three of those. So a key of seven columns can be an index of twenty-one segments,
	// and Firebird stops at sixteen — "too many keys defined for index" at CREATE INDEX time, which
	// on Firebird takes the whole restructuring down with it and names a table that is not the
	// problem. PostgreSQL at thirty-two, SQLite has no limit anyone
	// reaches.
	//
	// It is declared rather than discovered because the DECLARATION side has to know it: a totals
	// table whose key cannot be indexed carries its identity in one hashed field instead
	// (databaseMaterializeBuilder.h — ibKeyHashColumnName), and that is a column, i.e. a schema
	// decision taken before any DDL is emitted.
	unsigned int m_maxIndexSegments = 0;

	// Physical row identifier, used to drop duplicate-key rows (keep one) BEFORE a UNIQUE index is created
	// over existing data: FB "RDB$DB_KEY", SQLite "rowid", PG "ctid". EMPTY (default) => no dedup, so the
	// UNIQUE create fails loudly on duplicates. Used by ibSchemaBuilder::Execute.
	wxString m_rowIdColumn;

	// UPSERT (INSERT-or-update by PK) as a TEMPLATE the renderer fills. Placeholders:
	//   {table} {columns} {values} {keys} {update}
	// {update} = the conflict-update body: m_upsertUpdateItem per non-key column,
	// comma-joined. Firebird's MATCHING needs no update body (empty item). Default =
	// PG / SQLite ON CONFLICT.
	wxString m_upsertTemplate   = wxT("INSERT INTO {table} ({columns}) VALUES ({values}) ON CONFLICT ({keys}) DO UPDATE SET {update}");
	wxString m_upsertUpdateItem = wxT("{col} = excluded.{col}");

	// RETURNING — a write that hands back column values from the rows it just wrote, so
	// "modify, then read what it became" is ONE statement instead of two. That matters
	// beyond convenience: a sequence bump (UPDATE ... SET n = n + 1 RETURNING n) is atomic
	// this way, with no window between the write and the read for another session to slip
	// into. Firebird, PostgreSQL and SQLite (3.35+) all spell it "RETURNING".
	//
	// EMPTY (default — ODBC) = the driver has no such form. The renderer THROWS
	// UnsupportedNode rather than emulate it, because the emulation (write, then SELECT)
	// silently drops exactly the atomicity the caller asked for.
	wxString m_returningClause;

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
	wxString m_typeBigInt        = wxT("BIGINT");        // 64-bit integer (reference _RTRef = clsid)
	wxString m_typeDate          = wxT("TIMESTAMP");     // DateTime fraction (date + time)
	wxString m_typeDateOnly      = wxT("DATE");          // Date fraction (no time)
	wxString m_typeTime          = wxT("TIME");          // Time fraction (no date)
	wxString m_typeBlob          = wxT("BLOB");
	wxString m_typeGuid          = wxT("VARCHAR(36)");   // VARCHAR (not CHAR): carries vary_length, so a guid reads back exact — no charset-padded CHAR tail (PG overrides to native UUID)
	wxString m_typeStringPattern = wxT("VARCHAR(%d)");   // variable-length string
	wxString m_typeCharPattern   = wxT("CHAR(%d)");      // fixed-length string
	wxString m_typeBinaryPattern = wxT("BINARY(%d)");    // fixed-length bytes (reference _RRRef = pure guid); indexable for = joins
	wxString m_typeNumberPattern = wxT("DECIMAL(%d,%d)");

	// DDL/DML transaction barrier. Some engines cannot safely populate a table
	// (prepared INSERT/UPDATE with bind) in the SAME transaction that created or
	// altered it: Firebird's legacy isc_* API races the metadata cache — seed rows
	// are silently dropped or a prepare surfaces "table unknown". When TRUE, the
	// schema door (L3-2) commits the DDL batch first and runs the data/seed phase on
	// the commit event, in a fresh transaction. FALSE (PostgreSQL / SQLite /
	// ODBC) = DDL and DML coexist in one TX, no barrier needed. This is the
	// declarative replacement for the scattered `if (driver == FIREBIRD) Commit()`
	// forks in the metadata-save flow.
	bool m_ddlCommitBeforeData = false;

	// ALTER COLUMN (type change) as a TEMPLATE — {table} {column} {type}. Default =
	// PG / Firebird "ALTER COLUMN c TYPE t"; SQLite leaves
	// it EMPTY (no in-place type change) so the renderer THROWS rather than emulate.
	wxString m_alterColumnTemplate = wxT("ALTER TABLE {table} ALTER COLUMN {column} TYPE {type}");

	// Multi-clause ALTER TABLE: "ALTER TABLE t ADD c1, ADD c2, DROP COLUMN c3" in one statement.
	// FB / PG accept it; SQLite does NOT (one ADD/DROP per ALTER), so the structure builder
	// splits a batch into one statement per clause when this is false.
	bool m_alterTableMultiClause = true;

	// Column-drop spelling. Default = ANSI "DROP COLUMN c" (PG / SQLite 3.35+).
	// Firebird rejects the COLUMN keyword — it takes plain "ALTER TABLE t DROP c". Used by
	// DropColumn and the multi-clause ALTER fold.
	wxString m_dropColumnClause = wxT("DROP COLUMN ");

	// ANALYZE spelling — the renderer emits `<prefix> <table>` for an ibDdlKind::Analyze. Refreshes
	// the optimiser's statistics so it plans against real cardinality (after a temp materialise / bulk
	// load / restructure). PG / SQLite: "ANALYZE"; Firebird: EMPTY (no portable
	// ANALYZE — it tunes per-index stats differently), which renders empty → Execute no-ops.
	wxString m_analyzePrefix;   // empty = the driver has no ANALYZE statement

	// Row-lock clause APPENDED to a top-level SELECT for a pessimistic read-for-update (the
	// register set lock). Default = PG " FOR UPDATE"; Firebird overrides to " WITH
	// LOCK"; SQLite leaves it EMPTY (it locks the whole DB per transaction — the open TX IS
	// the lock, there is no row-level FOR UPDATE). Rendered after ORDER BY / LIMIT.
	// (docs/record-locks.md)
	wxString m_rowLockSuffix = wxT(" FOR UPDATE");

	// Appended AFTER m_rowLockSuffix when the IR asks for a non-blocking acquire (ibQueryIR::
	// m_lockNoWait). Default = PG " NOWAIT" (=> " FOR UPDATE NOWAIT"). Firebird leaves it
	// EMPTY — it has no FOR UPDATE NOWAIT; a non-blocking acquire is expressed by the transaction's
	// own lock-timeout (ibTxOptions::noWait -> isc_tpb_nowait). SQLite empty (whole-DB lock).
	// (docs/record-locks.md — unifies the old ibDatabaseLayer::NoWaitClause virtual onto the dictionary.)
	wxString m_rowLockNoWaitSuffix = wxT(" NOWAIT");

	// GROUP BY ROLLUP spelling (used only when m_features.m_rollup): the keys render between
	// prefix and suffix. Standard (PG / FB): "GROUP BY ROLLUP(<keys>)". MSSQL: prefix empty,
	// suffix " WITH ROLLUP" -> "GROUP BY <keys> WITH ROLLUP". (docs/query-language-arc.md §22.1b)
	wxString m_rollupPrefix = wxT("ROLLUP(");
	wxString m_rollupSuffix = wxT(")");

	// A source-less SELECT (SELECT <consts> with no FROM — the derived one-row VALUES relation the
	// write-time WITH CHECK builds). Most engines allow a bare FROM-less SELECT (empty here); Firebird
	// requires a one-row dummy table -> "RDB$DATABASE". Set per driver; empty = emit no FROM at all.
	wxString m_selectFromDual = wxEmptyString;

	// HOW A PLACEHOLDER STATES ITS TYPE inside the UNION-ALL spelling of a batched INSERT.
	// Placeholders: {value} — the rendered value (a bind marker); {table} / {column} — where it is
	// going. EMPTY = this engine needs nothing, emit {value} bare.
	//
	// In `INSERT INTO t (a) VALUES (?)` the parameter takes its type from the target column. In
	// `INSERT INTO t (a) SELECT ? FROM …` it does NOT — the SELECT is typed first, on its own, and a
	// bare `?` there has nothing to be typed FROM. Firebird says so and refuses to prepare:
	//
	//     Dynamic SQL Error / SQL error code = -804 / Data type unknown
	//
	// Three cheaper spellings were tried against a live engine and all three still fail: typing only
	// the FIRST union branch, seeding the union with a zero-row SELECT off the target table, and
	// leaving the extra branches bare. Firebird types EVERY branch independently. So every
	// placeholder in every branch carries its own cast, which is what this template renders.
	// (tests/test_firebirdBatchInsert.cpp keeps all four answers, against a real Firebird.)
	//
	// It casts to TYPE OF COLUMN rather than to a type word so the ENGINE looks the type up in its
	// own catalogue. The alternative — the renderer deciding what a column holds — would be a second
	// authority on the schema sitting next to the one that created it, free to disagree with it.
	wxString m_batchInsertCast = wxEmptyString;

	// Period truncation: unit -> a SQL expression template with ONE placeholder, {expr}. This is
	// what the L2-1 renderer spells an ibQueryExprKind::PeriodTrunc node through, so ANY query can
	// group by month without its author naming an engine. A unit absent from the map is NOT
	// SUPPORTED here and the renderer refuses it loudly — the alternative, silently falling back to
	// a neighbouring unit or leaving the value untruncated, is a wrong grouping key that reconciles
	// to nothing and surfaces months later. In practice every real driver covers the whole enum.
	//
	// One definition per engine, shared by every consumer: the totals trigger keys rows with it and
	// the read view projects columns with it, so a stored key and a read column CANNOT drift apart.
	std::map<ibTotalsPeriod, wxString> m_periodTrunc;

	// ⭐ THE REST OF THE CALENDAR, ON THE SAME TERMS. Each map is unit -> a SQL template with named
	// placeholders, so the IR keeps saying WHAT and the dictionary keeps saying HOW. A unit missing
	// from a map is refused loudly by the renderer, exactly as a missing truncation is: answering
	// with a neighbouring unit would be a wrong number that still runs.
	//
	//   m_periodEnd  {expr}          — the LAST instant of the period holding it
	//   m_dateAdd    {expr}, {count} — move by whole units, calendar-aware
	//   m_dateDiff   {from}, {to}    — how many whole units between two moments
	//   m_datePart   {expr}          — one piece of a date as a number
	//
	// They are four maps rather than one keyed by (operation, unit) because they are asked
	// separately and a missing entry has to be reportable as "this engine cannot END a quarter"
	// rather than as a lookup miss in a table nobody can see the shape of.
	std::map<ibTotalsPeriod, wxString> m_periodEnd;
	std::map<ibTotalsPeriod, wxString> m_dateAdd;
	std::map<ibTotalsPeriod, wxString> m_dateDiff;
	std::map<ibDatePart,     wxString> m_datePart;

	// SUBSTRING(<string>, <from>, <length>) — one template, {expr} / {from} / {len}. Not a map: the
	// call has no unit to vary by, and the engines differ only in the word (SUBSTRING … FROM … FOR …
	// against SUBSTR(…)). Empty = this engine is not asked to do it.
	wxString m_substring;

	// --- behaviour slots (the small tail data cannot express) -------------
	// Reserved for emulation rewrites (FULL OUTER -> LEFT UNION RIGHT, window ->
	// correlated subquery) as strategy callbacks, so L2-1 still reads only the dictionary.
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
	//                     (SQLite / PostgreSQL / MSSQL — any-shape, on the fly).
	//   PreDeclaredPool : grab a typed GLOBAL TEMPORARY TABLE from a schema-declared pool, INSERT,
	//                     ON COMMIT clears the private rows (Firebird — GTTs are fixed-shape schema
	//                     objects, NOT ad-hoc; arbitrary-shape intermediates need a generic pool or
	//                     fall back to RAM).
	enum class Strategy { AdHocCreate, PreDeclaredPool };
	Strategy m_strategy = Strategy::AdHocCreate;

	// CREATE prefix (ad-hoc) — the manager appends " <name> (<cols>)" + m_onCommitClause.
	wxString m_createPrefix   = wxT("CREATE TEMPORARY TABLE");
	// Clause appended after the column list: PG " ON COMMIT DROP"; FB GTT " ON COMMIT DELETE ROWS";
	// SQLite empty (connection-scoped, auto-dropped on disconnect).
	wxString m_onCommitClause;
	// Does the temp table drop itself at session / commit end? false => the manager emits DROP.
	bool     m_autoDrops      = true;
	// DROP statement prefix (used only when !m_autoDrops) — the manager appends the name.
	wxString m_dropPrefix     = wxT("DROP TABLE");
	// (ANALYZE is NOT here — it is a general L2-1 statement, ibDdlKind::Analyze, rendered from the
	//  main ibDialectDictionary::m_analyzePrefix, so any caller can refresh stats, not just temp.)
};

// ==========================================================================
// ibMaterializationDialect — the SECOND per-driver dictionary: the facts needed to
// MATERIALISE derived state (a register's totals table kept current by a trigger).
//
// Why it is not the L2-1 query dictionary. That one substitutes TOKENS over a SHARED
// declarative structure (LIMIT vs FIRST/SKIP, quoting, the type map) — every engine
// renders the same SELECT shape. A totals trigger is IMPERATIVE and diverges in
// STRUCTURE, not spelling: per-row engines fire FOR EACH ROW over NEW/OLD, T-SQL fires
// ONCE per statement over the `inserted`/`deleted` pseudo-tables and merges a set. That
// is a different ALGORITHM, so folding it into L2-1 would mean either imperative nodes in
// the query IR (RETURN / IF / NEW / OLD) or a raw-SQL escape hatch — and L2-1's "never raw
// SQL" invariant is exactly what keeps four drivers honest. A second dictionary keeps
// both invariants intact.
//
// Vended SEPARATELY and NULLABLE (ibDatabaseLayer::GetMaterializationDialect), same as
// ibTempTableDialect: its PRESENCE IS the capability. A driver returning nullptr gets no
// totals table and no trigger; its registers keep serving Balance / Turnover from the
// LIVE aggregation (ComputeBalance / ComputeTurnover — a signed SUM over the movement
// table). That live path is the always-works FLOOR: correct at any scale, merely slower,
// which is why ODBC — generic by construction, it cannot know the engine underneath —
// simply never opts in. Read paths above L3 do not change either way.
//
// PURE DECLARATIVE FACTS + a strategy DISCRIMINATOR, matching ibTempTableDialect: the
// family below is DATA; the control-flow fork it selects lives in the generator that
// reads it, never as `if (driver)` in this struct.
// (docs/register-totals-strategy.md § Engine integration)
// ==========================================================================

// Execution model of the maintenance trigger — the discriminator that picks a template
// FAMILY. Two, and only two, are needed for the five-plus engines:
enum class ibTriggerFamily
{
	//   PerRow   : fires once per affected row over NEW / OLD row aliases, and reduces to a
	//              handful of plain SQL statements — Firebird PSQL, PostgreSQL PL/pgSQL,
	//              SQLite. Designed to the SQLITE FLOOR (statements only, no
	//              procedural block), so one body fits all three; only the upsert idiom and
	//              the shell differ, and both are slots below.
	PerRow,
	//   SetBased : fires once per STATEMENT over the `inserted` / `deleted` pseudo-tables and
	//              merges an aggregated set — MSSQL / T-SQL. A different algorithm, not a
	//              different word; this is the case the L2-1 dictionary could not have held.
	//              Reserved for the MSSQL port — adding it is one more dictionary, not a reshape.
	SetBased
};

// (ibTotalsPeriod lives with the QUERY dictionary above — truncating a period is a declarative
// expression any SELECT may want, not a materialisation-only concern. The two roles it serves
// here are STORAGE granularity, the unit a movement's period is truncated to before it becomes
// part of the totals key, and READ projections, every coarser unit exposed as its own column on
// the view. They are independent by design: read granularity is a QUERY parameter, storage
// granularity a schema decision, and the second need only be fine enough to serve the first.
// Granularity finer than Second — per-recorder, per-record — is deliberately not in the enum:
// that is not a truncation, it is the movement row itself, and a totals row per movement is a
// movement table with extra steps. Those readings are served by reading the MOVEMENTS, which
// makes the enum exactly the boundary between what materialises and what does not.)

struct ibMaterializationDialect
{
	ibTriggerFamily m_family = ibTriggerFamily::PerRow;

	// --- row aliases the trigger body reads the movement through --------------
	// PerRow: the inserted / deleted ROW ("NEW" / "OLD").
	// SetBased: the inserted / deleted TABLE ("inserted" / "deleted") — same slots, and
	// that reuse is the point: the generator names a side, the dialect says how.
	wxString m_newRowAlias = wxT("NEW");
	wxString m_oldRowAlias = wxT("OLD");

	// --- the trigger shell ----------------------------------------------------
	// Placeholders: {name} {table} {timing} {body}. The generator supplies {body} already
	// rendered from the slots below, so the shell is pure syntax.
	//
	// {body} is N statements, not one. A single movement can feed SEVERAL totals tables:
	// an accounting register's line carries a debit side and a credit side, and each side
	// accumulates into its own turnover table (plus, later, per-dimension breakdowns). One
	// trigger, several deltas, all inside the one transaction that wrote the movement — so
	// the multi-table case keeps the same atomicity guarantee as the single-table one, and
	// needs no coordination anywhere above.
	wxString m_triggerShellTemplate =
		wxT("CREATE TRIGGER {name} {timing} ON {table} FOR EACH ROW BEGIN {body} END");

	// Separator the generator joins those statements with. A trailing separator after the
	// last statement is required by the PSQL / plpgsql / SQLite trigger bodies, so this is
	// a terminator rather than a true separator — the generator appends it after each.
	wxString m_bodyStatementTerminator = wxT("; ");

	// PostgreSQL cannot inline a trigger body — it needs a FUNCTION the trigger then calls.
	// When non-empty the generator emits THIS FIRST ({name} gets an "fn_" flavour, {body} the
	// same rendered body) and the shell above only references it. EMPTY (FB / SQLite)
	// = the body inlines, one statement instead of two.
	wxString m_functionShellTemplate;

	// Timing clauses substituted into {timing}, one per movement event.
	wxString m_timingInsert = wxT("AFTER INSERT");
	wxString m_timingUpdate = wxT("AFTER UPDATE");
	wxString m_timingDelete = wxT("AFTER DELETE");

	// DROP spelling — {name} {table}. PG drops by name alone; SQLite / FB likewise,
	// but MSSQL disagrees about the schema qualifier, so it stays a template.
	wxString m_dropTriggerTemplate = wxT("DROP TRIGGER {name}");

	// Drops the separate function object, for the engines that have one — {name}. EMPTY where
	// the body inlines (FB / SQLite), because there is nothing to drop. Without this
	// a dropped table would leave its trigger function orphaned in the schema: harmless, but
	// it accumulates across every restructure and nothing else would ever collect it.
	wxString m_dropFunctionTemplate;

	// --- "does this object exist?" — the probes that make a DROP safe ---------
	// A SELECT returning at least one row when the named object exists; {name} is substituted.
	//
	// Needed because a failed DDL is NOT harmless on every engine: Firebird ROLLS BACK the
	// transaction, so a DROP of something that was never created takes the whole restructuring
	// down with it — the first apply would destroy itself. Engines that spell DROP … IF EXISTS
	// leave these EMPTY: there the drop cannot fail, so nothing needs probing.
	//
	// The probe belongs to the dialect because the catalogue it reads is engine-specific
	// (RDB$RELATIONS / pg_class / sqlite_master), which is exactly the kind of fact a dictionary
	// exists to hold.
	wxString m_viewExistsQuery;
	wxString m_triggerExistsQuery;

	// --- the delta upsert (the whole point of the dictionary) -----------------
	// "add this delta into the totals row for this key, creating the row if absent".
	//
	// It is NOT ibDialectDictionary::m_upsertTemplate. That one REPLACES a column
	// (`{col} = excluded.{col}`); a totals delta ACCUMULATES (`{col} = t.{col} + …`), and
	// the difference is structural rather than cosmetic. Firebird proves it: UPDATE OR
	// INSERT .. MATCHING can only replace, so an accumulating upsert there must be a MERGE
	// — a different STATEMENT for the same concept. That is why the template carries a
	// superset of placeholders and each engine spends only the ones its form needs:
	//
	//   {table}     the totals table
	//   {columns}   all totals columns (keys + resources), comma-joined
	//   {values}    the incoming values, positionally matching {columns}
	//   {keys}      the key columns alone (ON CONFLICT / MATCHING target)
	//   {update}    m_deltaUpdateItem per resource column, comma-joined
	//   {target}    m_deltaTargetAlias — how the totals row is named in {update} / {keyMatch}
	//   {source}    m_deltaSourceAlias — how the incoming row is named in the same
	//   {sourceRel} the incoming row as a one-row relation, per m_deltaSourceTemplate (MERGE USING)
	//   {keyMatch}  m_deltaKeyMatchItem per key column, AND-joined (MERGE ON)
	//   {from}      the source-less-SELECT FROM clause, from ibDialectDictionary::
	//               m_selectFromDual (empty on PG / SQLite, " FROM RDB$DATABASE" on Firebird)
	//   {where}     " WHERE <guard>", or EMPTY when the delta is unconditional
	//
	// The generator renders ALL of them and substitutes; an unused placeholder simply never
	// appears in a given dialect's template. That keeps the generator engine-agnostic — it
	// fills parameters and never asks which engine it is talking to.
	//
	// Why the SELECT form and not plain VALUES. A CONDITIONAL delta — apply this movement to
	// this totals table only if the side is populated — is what an accounting register needs
	// (a movement carries a debit side and a credit side; each feeds a DIFFERENT totals
	// table, and either may be absent). VALUES cannot carry a predicate, so the alternative
	// would be a procedural IF — which breaks the SQLite floor and splits the family in two.
	// INSERT..SELECT..WHERE expresses the same thing declaratively on all four engines. An
	// unconditional delta (an accumulation register) renders {where} empty and pays nothing.
	wxString m_deltaUpsertTemplate =
		wxT("INSERT INTO {table} ({columns}) SELECT {values}{from}{where} ON CONFLICT ({keys}) DO UPDATE SET {update}");

	// One accumulate item, comma-joined into {update}. Placeholders: {target} {source} {col}.
	wxString m_deltaUpdateItem   = wxT("{col} = {target}.{col} + {source}.{col}");
	// One key-equality item, AND-joined into {keyMatch}. Same placeholders.
	// ⭐⭐ NULL-SAFE, AND THE INDEX IS WHY. A key column may legitimately be empty — a register
	// dimension nobody filled in stores NULL in all three of its physical fields — and plain `=`
	// answers UNKNOWN when both sides are NULL, not TRUE. The MERGE then never matches the row that
	// IS there, falls into WHEN NOT MATCHED, and INSERTs a duplicate.
	//
	// 🛑 Which the unique index refuses, because IT treats those NULLs as equal:
	//   "attempt to store duplicate value in unique index ACCUMULATIONREGISTER…_BALANCETOTALS_PK …
	//    FLD1155_TYPE = NULL, FLD1155_RTREF = NULL, FLD1155_RRREF = NULL"
	// — seen live 2026-08-31 on a movement whose Characteristic was left empty. Two notions of
	// equality over one key: the index's and the ON clause's. The statement is written from the same
	// key list the index is built from, so the COMPARISON has to come from there too.
	wxString m_deltaKeyMatchItem = wxT("{target}.{col} IS NOT DISTINCT FROM {source}.{col}");

	// How the two sides are named inside the items above. PG / SQLite: the target is the
	// table itself and the incoming row is "excluded". Firebird's MERGE binds both to
	// explicit aliases. One slot, two spellings.
	wxString m_deltaTargetAlias = wxT("{table}");
	wxString m_deltaSourceAlias = wxT("excluded");

	// Renders the incoming row as a one-row relation for MERGE USING — {selectItems} is
	// "<value> AS <col>" per column, comma-joined. Empty (PG / SQLite) = the form
	// takes VALUES directly and needs no relation. Firebird additionally needs a FROM, and
	// takes it from ibDialectDictionary::m_selectFromDual (RDB$DATABASE) — the fact already
	// lives there, so it is not restated here.
	wxString m_deltaSourceTemplate;

	// (Period truncation is NOT here — it moved to ibDialectDictionary::m_periodTrunc, where the
	//  L2-1 renderer can reach it. A trigger keys its rows through the same map the view projects
	//  its columns through, so the two cannot disagree.)

	// --- the key HASH: an identity for a key an index cannot hold ---------------
	//
	// ⭐⭐ WHY A DERIVED TABLE NEEDS ONE. Its key must be UNIQUE — the delta upserts against it, and
	// two rows for one key would each accumulate half the movements, which is a pair of individually
	// plausible and jointly wrong totals. Uniqueness is held by an index, and an index has a segment
	// ceiling (ibDialectDictionary::m_maxIndexSegments). An accounting register's key — the period,
	// the account, a (kind, value) pair per analytic, the register's dimensions — passes it at four
	// analytics, and no rearrangement of that key makes it shorter.
	//
	// So the identity moves into ONE field: a digest of the very expressions the key columns are
	// filled from. The key columns stay ordinary columns and the upsert goes on MATCHING BY THEM, so
	// a digest collision cannot merge two different keys into one row — it can only refuse an insert,
	// loudly, which is the direction an accounting engine should fail in. (The engines whose upsert
	// names its conflict target by COLUMN LIST — ON CONFLICT — name the digest column instead there;
	// those engines are also the ones with no ceiling worth declaring, so it does not arise in
	// practice.)
	//
	// EMPTY (SQLite) = this engine cannot compute a digest in SQL. That is the honest empty and costs
	// nothing: its index has no ceiling to pass.
	//
	// {value} — one key expression, already rendered over the movement row. Per-field rather than one
	// digest of the concatenation, because the parts must be turned into text before they can be
	// joined at all, and a bare CAST of a binary reference key or an unbounded string is either a
	// character-set error or a silent truncation. A hashed part is fixed-width, ASCII, and cannot
	// truncate.
	wxString m_keyHashItem;
	// How the parts are joined. Firebird / PG / SQLite concatenate with ||; a dialect with	// CONCAT() instead joins on a comma and wraps the list in its digest template.
	wxString m_keyHashJoin = wxT(" || '.' || ");
	// {expr} — the joined parts, digested into what the column stores.
	wxString m_keyHashDigest;

	// --- the totals table itself ----------------------------------------------
	// Appended after the CREATE TABLE column list — PostgreSQL " WITH (fillfactor = 80)",
	// which keeps the hot totals UPDATE on the same page (HOT update, no index write) and is
	// worth a multiple on write throughput. Empty elsewhere.
	wxString m_totalsTableSuffix;

	// --- split totals (write-contention relief) --------------------------------
	// An expression yielding an id unique to the CURRENT connection / transaction. It is the
	// shard chooser: a register split N ways adds a shard column to its totals key, and the
	// trigger picks its shard by hashing THIS. Two properties make that safe with no
	// coordination — the choice is LOCAL (never a shared counter, which would contend exactly
	// where the split is meant to relieve contention), and the read SUMS all shards, so an
	// insert's +delta and a later delete's -delta may land in different shards and the total
	// is still exact. The trigger therefore remembers nothing.
	//
	// EMPTY = this engine cannot split totals. SQLite is the honest empty: a single-writer
	// embedded engine has no write contention to relieve, so the feature is meaningless
	// rather than missing. A register asking for shards on such an engine gets one shard.
	wxString m_connectionIdExpr;

	// Folds the connection id into a shard number — {conn} and {n}. Default is the infix
	// remainder every engine but Firebird takes; Firebird has no '%' operator and spells it
	// MOD(a, b). A one-token difference, but it is exactly the kind that would otherwise
	// become an `if (driver)` in the renderer.
	wxString m_shardExprTemplate = wxT("({conn} % {n})");

	// --- the read views -------------------------------------------------------
	// {name} {body}. The view is the register's public read API; the totals table under it is
	// an implementation detail, which is what lets the strategy change later without touching
	// a single caller above L3.
	wxString m_createViewTemplate = wxT("CREATE VIEW {name} AS {body}");
	wxString m_dropViewTemplate   = wxT("DROP VIEW {name}");
};

WX_DECLARE_HASH_SET(ibDatabaseResultSet*, wxPointerHash, wxPointerEqual, DatabaseResultSetHashSet);
WX_DECLARE_HASH_SET(ibPreparedStatement*, wxPointerHash, wxPointerEqual, DatabaseStatementHashSet);

class ibDatabaseConnectionHolder;
class ibConnectionPool;

// Transaction options — how a BeginTransaction should behave. Declared here at namespace
// scope rather than nested in ibDatabaseLayer so its default member initializers are complete
// where the class's own methods default an argument to it (see the alias inside the class).
//
// `noWait`   — fail immediately instead of blocking on a conflicting lock.
// `readOnly` — the caller promises this transaction issues no DML, letting a driver pick a
//              cheaper isolation. Drivers without one ignore the flag.
// `snapshot` — every statement in this transaction reads the SAME committed state.
//
// ⭐⭐ WHY THE THIRD FLAG EXISTS: A REPORT MUST NOT CONTRADICT ITSELF. It runs a dozen queries —
// balances, turnovers, the detail lines under them — and under read-committed each of those reads
// whatever is committed at the moment it starts. Post a batch of documents while one is running and
// the total in the header stops matching the rows beneath it: half the figures from before, half
// from after. Nobody can tell which half, and the report looks simply wrong.
//
// ⚠ NEITHER OF THE OTHER TWO GIVES THIS, which is the trap this flag exists to close. The default is
// read-committed, and `readOnly` is read-committed too — with read_consistency, which stabilises a
// SINGLE statement and says nothing about the next one. So a report wrapped in either still tears.
//
// It is not free: holding one state visible means the server keeps the record versions that state
// needs, so the older they get the more it holds. That is the right trade for a report — figures
// that agree with each other are the entire point of one — and the wrong trade for a long write.
struct ibDbTxOptions {
	bool noWait = false;
	bool readOnly = false;
	bool snapshot = false;
};

class BACKEND_API ibDatabaseLayer
	: public ibDatabaseErrorReporter
	, public ibDatabaseStringConverter
	, public ibDatabaseResultSetOwner
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
	// immediately instead of blocking. Used by non-blocking lock acquires
	// (e.g. ibLockManager's NOWAIT path via ir.m_lockNoWait). On FB maps to
	// `isc_tpb_nowait`; other drivers approximate via session-level lock_timeout or ignore.
	//
	// `readOnly` — caller promises this TX won't issue DML. The driver
	// can then pick an isolation that doesn't acquire write-intent locks
	// (FB 4+: read_committed + read_consistency for snapshot-style
	// reads without long-running snapshot TX; PostgreSQL: SET TRANSACTION
	// READ ONLY). Drivers that don't have a cheap read-only mode silently
	// ignore the flag.
	// The type itself is declared ABOVE the class (ibDbTxOptions); this alias keeps every
	// existing `ibDatabaseLayer::ibTxOptions` spelling working. It had to move out: a nested
	// class's default member initializers are not complete until the ENCLOSING class is, so
	// naming ibTxOptions{} / ibTxOptions() in a default argument of a method of that same
	// class is ill-formed — "default member initializer required before the end of its
	// enclosing class". MSVC accepts it, GCC does not, and no spelling of the default
	// argument fixes it while the struct stays nested.
	using ibTxOptions = ibDbTxOptions;

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
	// Defaulted to the namespace-scope name on purpose — see the alias above for why the
	// nested spelling cannot carry a default here.
	void BeginTransaction(const ibTxOptions& opts = ibDbTxOptions());

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

	// Run ONE statement exactly as given — no printf formatting, no splitting on ';'.
	//
	// RunQuery below does both, and both are wrong for a statement that CONTAINS those characters
	// rather than being described by them:
	//   * a trigger body is `BEGIN <stmt>; <stmt>; END` — splitting it on ';' hands the server a
	//     fragment ending mid-block, which it rejects as an unexpected end of command;
	//   * a period-truncation expression is `strftime('%Y-%m-%d', …)` — a '%' that printf would
	//     eat before the server ever saw it.
	// Anything whose text is already final belongs here, not there.
	int RunStatement(const wxString& sql) { return DoRunQuery(sql, /*bParseQueries*/ false); }

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
	// map, feature flags). L2-1 (ibDatabaseQueryBuilder / ibQueryRenderer) reads
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

	// ⭐⭐ THE ROUTINES THIS ENGINE DOES NOT HAVE AND THE PLATFORM'S OWN SQL ASSUMES.
	//
	// Some vendors are missing something the generated SQL takes for granted and the dialect cannot
	// spell around — PostgreSQL has no MAX / MIN over `uuid`, which the reference key IS, so those
	// aggregates have to exist before a single query that folds references can run. A driver missing
	// nothing overrides nothing.
	//
	// ⚠ THE VENDOR SQL LIVES WITH THE VENDOR. This used to be written out in `metaCollection`
	// (`ExecuteSystemSQLCommand`), which made a metadata file the only place outside this layer
	// spelling engine-specific DDL — the same leak the capability accessors closed on the read side,
	// one tier up. The CALLER still decides WHEN (a database is being created), because that is its
	// question; WHAT is this one's.
	//
	// ⚠ Called ONCE, when a database is made — deliberately NOT from Open(). Creating them on every
	// connection would put four DDL statements on every pool clone and would demand CREATE FUNCTION
	// rights of an ordinary working user, which a deployment is entitled to withhold.
	//
	// ⚠ Not "seed": in this tree that word already means posting the seed DATA a fresh schema needs
	// (the writes RunOrDefer queues past a DDL barrier). One word, one meaning.
	//
	// Returns false when the database cannot be considered usable without them. Drivers report failure
	// by THROWING, so a false is the belt beside the braces.
	virtual bool CreateMissingRoutines() { return true; }

	// The driver's MATERIALIZATION facts (trigger shell, delta upsert, period truncation,
	// view spelling), or nullptr if it cannot maintain derived state. PRESENCE = the
	// capability, exactly as above: nullptr => the register keeps no totals table and its
	// Balance / Turnover queryables stay on the LIVE aggregation, which is correct at any
	// scale and merely slower. ODBC is the permanent nullptr — it is engine-agnostic by
	// construction, so it cannot template engine-specific triggers, and that is a floor
	// rather than a gap. NOT pure: drivers inherit nullptr until each opts in, so totals
	// land additively, driver by driver. (docs/register-totals-strategy.md)
	virtual const ibMaterializationDialect* GetMaterializationDialect() const { return nullptr; }

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


	// ---- Row-level pessimistic write lock ----
	// The row-lock clause moved OFF per-driver virtuals onto the dialect dictionary
	// (m_rowLockSuffix / m_rowLockNoWaitSuffix); the L2-1 door renders it from
	// ibQueryIR::m_lockForUpdate / m_lockNoWait. The old RowLockHint() / NoWaitClause()
	// virtuals are gone — their last callers (ibLockManager, the constant row-lock) now
	// go through the door. See docs/record-locks.md.

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
	/// Atomic for cross-thread visibility: a TX-pinned connection
	/// resolves across the session's worker threads (see BeginTransaction),
	/// so the counter may be observed from a different thread than the one
	/// that last mutated it. Access is still logically serial per session;
	/// atomicity buys memory visibility, not contention handling.
	std::atomic<int> m_txDepth{0};

	/// "Aborted" flag — set by any RollBack while a transaction is
	/// still open, cleared when the outermost level finally resolves.
	/// Makes the outermost Commit fall through to a real DoRollBack
	/// so an inner failure can poison an otherwise successful outer
	/// commit. Atomic for the same cross-thread-visibility reason as
	/// m_txDepth.
	std::atomic<bool> m_txAborted{false};

	/// Back-pointer to the holder that has this layer reserved for an
	/// active TX. Maintained exclusively by ibConnectionPool — see
	/// ReserveTx / ReleaseTx. nullptr when not pinned.
	ibDatabaseConnectionHolder* m_holder = nullptr;
	friend class ibConnectionPool;

	/// Add result set object pointer to the list for "garbage collection"
	void LogResultSetForCleanup(ibDatabaseResultSet* pResultSet)
	{
		m_ResultSets.insert(pResultSet);
		pResultSet->SetOwner(this);   // ...and it strikes itself out again when it dies
	}

	/// A result set of ours is going away — drop it from the books, do NOT free it
	void ForgetResultSet(ibDatabaseResultSet* pResultSet) override { m_ResultSets.erase(pResultSet); }
	/// Add prepared statement object pointer to the list for "garbage collection"
	void LogStatementForCleanup(ibPreparedStatement* pStatement)
	{
		m_Statements.insert(pStatement);
		pStatement->SetOwner(this);   // ...and it strikes itself out again when it dies — see its dtor
	}

	/// Drop a statement from the books WITHOUT freeing it — called by ~ibPreparedStatement, nowhere else
	//
	// The friendship below is that sentence made enforceable: unregistering without freeing is exactly
	// half of a release, and half a release in anybody else's hands is the leak this was built to end.
	friend class ibPreparedStatement;
	void ForgetStatement(ibPreparedStatement* pStatement) { m_Statements.erase(pStatement); }

	// ⚠ EVERY READ BY KEY PREPARES ITS OWN STATEMENT, and it is not because nobody noticed. Measured
	// on 2026-08-22: 277 reference reads produced 276 prepares of two SQL texts, byte for byte
	// identical, differing only in the key bound to them — the server parsing, planning and allocating
	// descriptors each time, then throwing all of it away after fetching one row.
	//
	// Reuse was built and taken out again the same night, and what it broke says what it needs. A
	// statement may be handed out a second time only once its CURSOR is finished with, and the moment
	// a reader is finished is its DESTRUCTOR — which is exactly the signal reuse gives up, since
	// reusing means not destroying. Marking "in use" by hand instead produced both failures at once:
	// statements never marked free again (every ask prepared a new one, so reuse bought nothing) and
	// one marked free too early, handed out with its cursor still open — "-502, cursor already open".
	//
	// The shape that works: hand the caller a HOLDER that knows its layer, so destroying the holder
	// returns the statement instead of freeing it.
	//
	// ⚠ AND IT WAITS ON AN OLDER DEFECT, which is why this is a note rather than a smaller patch.
	// Closing is not releasing here: the layer keeps the pointer in m_Statements either way, so a
	// statement is only really let go by going through the layer — and the statement's own cursor
	// bookkeeping is inert (CloseResultSet is a stub returning false, CloseResultSets is empty, and
	// nothing erases from its list). So nothing today can answer "is this cursor finished with", and
	// reuse needs exactly that answer. Routing release through the layer fixes both at once, which is
	// the argument for doing it properly rather than patching around it.

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

