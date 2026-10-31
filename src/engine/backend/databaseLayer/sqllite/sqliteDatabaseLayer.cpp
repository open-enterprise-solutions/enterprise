#include "sqliteDatabaseLayer.h"

#include "engine/sqlite3.h"

#include "sqliteResultSet.h"
#include "sqlitePreparedStatement.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "core/fileSystem.h"

// SQLite SQL dialect — owned by the driver (static definition + virtual access).
const ibDialectDictionary& ibDatabaseLayerSQLite::Dialect()
{
	static const ibDialectDictionary s_dialect = [] {
		ibDialectDictionary d;
		d.m_paramStyle = ibParamStyle::QuestionMark;
		d.m_pagination = ibPagination::LimitOffset;  // LIMIT n OFFSET m
		d.m_boolForm   = ibBoolForm::OneZero;
		d.m_groupByPosition = true;                   // GROUP BY 2 — a key that binds a value is named by its position
		d.m_features.m_window = true;                 // SQLite 3.25+
		d.m_features.m_cte    = true;                 // WITH … AS (…) — SQLite 3.8.3+
		d.m_features.m_recursiveCte      = true;      // WITH RECURSIVE — SQLite 3.8.3+
		d.m_features.m_recursiveCteUnion = true;      // UNION in the recursive part — a loop in a parent chain ends
		d.m_features.m_multiRowValues = true;         // INSERT … VALUES (…), (…) — SQLite 3.7.11+
		d.m_alterColumnTemplate = ibEmptyString;      // no in-place type change -> renderer throws
		d.m_alterTableMultiClause = false;            // one ADD/DROP per ALTER — the structure builder splits batches
		d.m_rowLockSuffix = ibEmptyString;            // SQLite locks the whole DB per TX — no row FOR UPDATE
		d.m_rowLockNoWaitSuffix = ibEmptyString;      // (no row lock at all -> nothing to make non-blocking)
		d.m_rowIdColumn    = ibT("rowid");            // physical row id for the pre-UNIQUE dedup (keep one row per key)
		d.m_maxParameters  = 32766;                   // SQLITE_MAX_VARIABLE_NUMBER, the 3.32+ default the vendored engine keeps
		d.m_returningClause = ibT("RETURNING");       // SQLite 3.35+; the embedded engine here is 3.48
		// type map (SQLite is dynamically typed; these set column affinity)
		d.m_typeBoolean       = ibT("INTEGER");
		d.m_typeBigInt        = ibT("INTEGER");   // SQLite INTEGER is 64-bit
		d.m_typeDate          = ibT("TEXT");
		d.m_typeDateOnly      = ibT("TEXT");
		d.m_typeTime          = ibT("TEXT");
		d.m_typeBlob          = ibT("BLOB");
		d.m_typeBinaryPattern = ibT("BLOB");      // SQLite has no fixed-width binary
		d.m_typeGuid          = ibT("TEXT");
		d.m_analyzePrefix     = ibT("ANALYZE");   // ANALYZE <t> — refresh planner stats (SQLite has no auto-analyze)
		// Period truncation. strftime handles the units that are a literal format mask; the four
		// that are not — Week, TenDays, Quarter, HalfYear — are date() arithmetic off the start of
		// the enclosing unit, expressed through SQLite's modifier strings:
		//   Week    — step back 6 days, then forward to the next Monday. Lands on this week's
		//             Monday for every day including Monday itself (ISO, as on the other engines).
		//   TenDays — 1st / 11th / 21st, offset CAPPED at 2 so a 31-day month cannot floor to 3
		//             and open a fourth one-day bucket; the last period runs 8-11 days.
		// Integer division on INTEGER operands is integer division here, and min() with two
		// arguments is the scalar function, not the aggregate.
		d.m_periodTrunc = {
			{ ibTotalsPeriod::Second,   ibT("strftime('%Y-%m-%d %H:%M:%S', {expr})")  },
			{ ibTotalsPeriod::Minute,   ibT("strftime('%Y-%m-%d %H:%M:00', {expr})")  },
			{ ibTotalsPeriod::Hour,     ibT("strftime('%Y-%m-%d %H:00:00', {expr})")  },
			{ ibTotalsPeriod::Day,      ibT("strftime('%Y-%m-%d 00:00:00', {expr})")  },
			{ ibTotalsPeriod::Week,     ibT("strftime('%Y-%m-%d 00:00:00', {expr}, '-6 days', 'weekday 1')") },
			{ ibTotalsPeriod::TenDays,  ibT("strftime('%Y-%m-%d 00:00:00', date(strftime('%Y-%m-01', {expr}), '+' || (min((CAST(strftime('%d', {expr}) AS INTEGER) - 1) / 10, 2) * 10) || ' days'))") },
			{ ibTotalsPeriod::Month,    ibT("strftime('%Y-%m-01 00:00:00', {expr})")  },
			{ ibTotalsPeriod::Quarter,  ibT("strftime('%Y-%m-%d 00:00:00', date(strftime('%Y-01-01', {expr}), '+' || (((CAST(strftime('%m', {expr}) AS INTEGER) - 1) / 3) * 3) || ' months'))") },
			{ ibTotalsPeriod::HalfYear, ibT("strftime('%Y-%m-%d 00:00:00', date(strftime('%Y-01-01', {expr}), '+' || (((CAST(strftime('%m', {expr}) AS INTEGER) - 1) / 6) * 6) || ' months'))") },
			{ ibTotalsPeriod::Year,     ibT("strftime('%Y-01-01 00:00:00', {expr})")  },
		};

		// The end of a period is the start of the next one less a second — said the same way on every
		// engine, so the boundary belongs to its period wherever the query runs. SQLite spells the
		// step as a datetime() modifier; the truncation above supplies the start.
		auto endOf = [&d](ibTotalsPeriod unit, const ibString& step) {
			return ibT("strftime('%Y-%m-%d %H:%M:%S', datetime(") + d.m_periodTrunc[unit]
			     + ibT(", '+1 ") + step + ibT("', '-1 second'))");
		};
		d.m_periodEnd = {
			{ ibTotalsPeriod::Second,   endOf(ibTotalsPeriod::Second,   ibT("second")) },
			{ ibTotalsPeriod::Minute,   endOf(ibTotalsPeriod::Minute,   ibT("minute")) },
			{ ibTotalsPeriod::Hour,     endOf(ibTotalsPeriod::Hour,     ibT("hour"))   },
			{ ibTotalsPeriod::Day,      endOf(ibTotalsPeriod::Day,      ibT("day"))    },
			{ ibTotalsPeriod::Week,     endOf(ibTotalsPeriod::Week,     ibT("day"))    },   // +7 days below
			{ ibTotalsPeriod::Month,    endOf(ibTotalsPeriod::Month,    ibT("month"))  },
			{ ibTotalsPeriod::Quarter,  endOf(ibTotalsPeriod::Quarter,  ibT("month"))  },   // +3 months below
			{ ibTotalsPeriod::HalfYear, endOf(ibTotalsPeriod::HalfYear, ibT("month"))  },   // +6 months below
			{ ibTotalsPeriod::Year,     endOf(ibTotalsPeriod::Year,     ibT("year"))   },
		};
		// The three whose step is more than one of its unit, spelled out rather than folded into the
		// helper: a lambda that took a count as well would be harder to read than the three lines it
		// saved.
		d.m_periodEnd[ibTotalsPeriod::Week]     = ibT("strftime('%Y-%m-%d %H:%M:%S', datetime(") + d.m_periodTrunc[ibTotalsPeriod::Week]     + ibT(", '+7 days', '-1 second'))");
		d.m_periodEnd[ibTotalsPeriod::Quarter]  = ibT("strftime('%Y-%m-%d %H:%M:%S', datetime(") + d.m_periodTrunc[ibTotalsPeriod::Quarter]  + ibT(", '+3 months', '-1 second'))");
		d.m_periodEnd[ibTotalsPeriod::HalfYear] = ibT("strftime('%Y-%m-%d %H:%M:%S', datetime(") + d.m_periodTrunc[ibTotalsPeriod::HalfYear] + ibT(", '+6 months', '-1 second'))");

		// datetime(x, '<n> months') takes the sign with the number, so one template serves both
		// directions — `|| ' months'` on a negative count reads as '-3 months', which is what it means.
		auto addOf = [](const ibString& step, const ibString& factor) {
			return ibT("strftime('%Y-%m-%d %H:%M:%S', datetime({expr}, (") + factor + ibT(") || ' ") + step + ibT("'))");
		};
		d.m_dateAdd = {
			{ ibTotalsPeriod::Second,   addOf(ibT("seconds"), ibT("{count}")) },
			{ ibTotalsPeriod::Minute,   addOf(ibT("minutes"), ibT("{count}")) },
			{ ibTotalsPeriod::Hour,     addOf(ibT("hours"),   ibT("{count}")) },
			{ ibTotalsPeriod::Day,      addOf(ibT("days"),    ibT("{count}")) },
			{ ibTotalsPeriod::Week,     addOf(ibT("days"),    ibT("({count}) * 7")) },
			{ ibTotalsPeriod::Month,    addOf(ibT("months"),  ibT("{count}")) },
			{ ibTotalsPeriod::Quarter,  addOf(ibT("months"),  ibT("({count}) * 3")) },
			{ ibTotalsPeriod::HalfYear, addOf(ibT("months"),  ibT("({count}) * 6")) },
			{ ibTotalsPeriod::Year,     addOf(ibT("years"),   ibT("{count}")) },
		};

		const ibString months = ibT("((CAST(strftime('%Y', {to}) AS INTEGER) * 12 + CAST(strftime('%m', {to}) AS INTEGER))"
		                            " - (CAST(strftime('%Y', {from}) AS INTEGER) * 12 + CAST(strftime('%m', {from}) AS INTEGER)))");
		auto secondsOver = [](const ibString& divisor) {
			return ibT("CAST((CAST(strftime('%s', {to}) AS INTEGER) - CAST(strftime('%s', {from}) AS INTEGER)) / ") + divisor + ibT(" AS INTEGER)");
		};
		d.m_dateDiff = {
			{ ibTotalsPeriod::Second,   secondsOver(ibT("1"))    },
			{ ibTotalsPeriod::Minute,   secondsOver(ibT("60"))   },
			{ ibTotalsPeriod::Hour,     secondsOver(ibT("3600")) },
			{ ibTotalsPeriod::Day,      ibT("CAST(julianday(date({to})) - julianday(date({from})) AS INTEGER)") },
			{ ibTotalsPeriod::Week,     ibT("CAST((julianday(date({to}, '-6 days', 'weekday 1')) - julianday(date({from}, '-6 days', 'weekday 1'))) / 7 AS INTEGER)") },
			{ ibTotalsPeriod::Month,    months },
			{ ibTotalsPeriod::Quarter,  ibT("(") + months + ibT(" / 3)") },
			{ ibTotalsPeriod::HalfYear, ibT("(") + months + ibT(" / 6)") },
			{ ibTotalsPeriod::Year,     ibT("(CAST(strftime('%Y', {to}) AS INTEGER) - CAST(strftime('%Y', {from}) AS INTEGER))") },
		};

		// strftime('%w') is Sunday = 0; this language pins Monday = 1 (ISO), so the shift is applied
		// here rather than left to whoever reads the number.
		d.m_datePart = {
			{ ibDatePart::Year,      ibT("CAST(strftime('%Y', {expr}) AS INTEGER)") },
			{ ibDatePart::Quarter,   ibT("((CAST(strftime('%m', {expr}) AS INTEGER) - 1) / 3 + 1)") },
			{ ibDatePart::Month,     ibT("CAST(strftime('%m', {expr}) AS INTEGER)") },
			{ ibDatePart::DayOfYear, ibT("(CAST(strftime('%j', {expr}) AS INTEGER))") },
			{ ibDatePart::Day,       ibT("CAST(strftime('%d', {expr}) AS INTEGER)") },
			// %V is the ISO week (SQLite 3.46+; the vendored one is 3.48) - what the script's GetWeekOfYear
			// and the other dialects' WEEK count. %W counted from the year's first Monday and had a week 0.
			{ ibDatePart::Week,      ibT("CAST(strftime('%V', {expr}) AS INTEGER)") },
			{ ibDatePart::WeekDay,   ibT("(((CAST(strftime('%w', {expr}) AS INTEGER) + 6) % 7) + 1)") },
			{ ibDatePart::Hour,      ibT("CAST(strftime('%H', {expr}) AS INTEGER)") },
			{ ibDatePart::Minute,    ibT("CAST(strftime('%M', {expr}) AS INTEGER)") },
			{ ibDatePart::Second,    ibT("CAST(strftime('%S', {expr}) AS INTEGER)") },
		};

		d.m_substring = ibT("substr({expr}, {from}, {len})");
		return d;
	}();
	return s_dialect;
}

const ibDialectDictionary& ibDatabaseLayerSQLite::GetDialect() const
{
	return Dialect();
}

// SQLite temp tables: ad-hoc `CREATE TEMPORARY TABLE` of any shape, connection-scoped
// (auto-dropped on disconnect). Like PostgreSQL we DROP explicitly via the manager's
// pinning scope rather than lean on disconnect — a pooled connection is long-lived and
// reused, so explicit DROP keeps it tidy and deterministic (m_autoDrops=false). Its mere
// PRESENCE flips SQLite off the RAM floor onto the server-side temp path. (docs/private/temp-db.md)
const ibTempTableDialect& ibDatabaseLayerSQLite::TempDialect()
{
	static const ibTempTableDialect s_temp = [] {
		ibTempTableDialect t;
		t.m_strategy       = ibTempTableDialect::Strategy::AdHocCreate;
		t.m_createPrefix   = ibT("CREATE TEMPORARY TABLE");
		t.m_onCommitClause = ibEmptyString;     // session-scoped; the manager drops it explicitly
		t.m_autoDrops      = false;             // explicit DROP via the pinning scope (RAII, leak-free)
		t.m_dropPrefix     = ibT("DROP TABLE");
		return t;
	}();
	return s_temp;
}

const ibTempTableDialect* ibDatabaseLayerSQLite::GetTempTableDialect() const
{
	return &TempDialect();
}

// SQLite materialisation: the per-row FLOOR. Its trigger body may contain only plain SQL
// statements — no variables, no IF, no procedural block — which is exactly why the family
// is designed here first: whatever expresses a totals delta in SQLite expresses it
// everywhere else, and the richer engines simply do not need their extra syntax.
// Period truncation goes through strftime, whose output is TEXT — consistent with this
// dialect storing dates as TEXT (m_typeDate above), so the totals key column and the
// movement's period column compare like for like.
const ibMaterializationDialect& ibDatabaseLayerSQLite::MaterializationDialect()
{
	static const ibMaterializationDialect s_mat = [] {
		ibMaterializationDialect m;
		m.m_family = ibTriggerFamily::PerRow;
		// SQLite names the trigger's timing before the table and takes no FOR EACH ROW
		// keyword requirement (it is the only mode it has), but accepts it — kept explicit
		// so the four per-row shells read alike.
		m.m_triggerShellTemplate = ibT("CREATE TRIGGER {name} {timing} ON {table} FOR EACH ROW BEGIN {body} END");
		m.m_functionShellTemplate = ibEmptyString;   // body inlines — no separate function object
		// The SELECT form (not VALUES) is also what SQLite itself prefers here: with
		// INSERT..SELECT the parser needs a WHERE before ON CONFLICT to disambiguate, so the
		// shape the conditional-delta case requires is the shape SQLite documents anyway.
		m.m_deltaUpsertTemplate =
			ibT("INSERT INTO {table} ({columns}) SELECT {values}{from}{where} ON CONFLICT ({keys}) DO UPDATE SET {update}");
		m.m_deltaTargetAlias  = ibT("{table}");     // ON CONFLICT names the target by table name
		m.m_deltaSourceAlias  = ibT("excluded");
		m.m_deltaUpdateItem   = ibT("{col} = COALESCE({target}.{col}, 0) + {source}.{col}");   // NULL-safe — see the default
		// NULL-safe like the default — SQLite spells it `IS`, which is the same operator under
		// another name. Unused by ON CONFLICT, kept true so it is not a trap. See databaseLayer.h.
		m.m_deltaKeyMatchItem = ibT("{target}.{col} IS {source}.{col}");   // unused by ON CONFLICT — rendered, not spent
		m.m_totalsTableSuffix = ibEmptyString;       // no fillfactor concept (single writer anyway)
		m.m_connectionIdExpr  = ibEmptyString;       // single writer => no contention to split; shards are meaningless, not missing
		// WHAT IS STANDING, asked of the catalogue — not only to guard a drop (SQLite's cannot hurt), but
		// because "is the bundle installed?" is answered by these (ibMaterializeSql::IsInstalled), and
		// with none it answered "yes" for triggers an apply had just dropped. Names compare without case,
		// as SQLite's identifiers do.
		m.m_viewExistsQuery    = ibT("SELECT 1 FROM sqlite_master WHERE type = 'view' AND name = '{name}' COLLATE NOCASE");
		m.m_triggerExistsQuery = ibT("SELECT 1 FROM sqlite_master WHERE type = 'trigger' AND name = '{name}' COLLATE NOCASE");
		return m;
	}();
	return s_mat;
}

const ibMaterializationDialect* ibDatabaseLayerSQLite::GetMaterializationDialect() const
{
	return &MaterializationDialect();
}

// ctor()
ibDatabaseLayerSQLite::ibDatabaseLayerSQLite()
	: ibDatabaseLayer()
{
	m_pDatabase = nullptr; //&m_Database; //new sqlite3;
}

ibDatabaseLayerSQLite::ibDatabaseLayerSQLite(const ibString& strDatabase, bool mustExist /*= false*/)
	: ibDatabaseLayer()
{
	m_pDatabase = nullptr; //new sqlite3;
	Open(strDatabase, mustExist);
}

ibDatabaseLayerSQLite::ibDatabaseLayerSQLite(const ibDatabaseLayerSQLite& src)
	: ibDatabaseLayer()
{
	m_pDatabase = nullptr;
	if (!src.m_strDatabasePath.IsEmpty())
		Open(src.m_strDatabasePath);
	else
		Open(ibEmptyString, false);
}

// dtor()
ibDatabaseLayerSQLite::~ibDatabaseLayerSQLite()
{
	Close();
}

// How long a statement waits for the file lock — given at Open, and put back when a noWait
// transaction ends (DoBeginTransaction).
static const int kBusyTimeoutMs = 5000;

// open database
bool ibDatabaseLayerSQLite::Open(const ibString& strDatabase, bool mustExist)
{
	if (strDatabase != ibT(":memory:") && // :memory: is a special SQLite in-memory database
		mustExist && !(ibFileExists(strDatabase)))
	{
		SetErrorCode(DATABASE_LAYER_ERROR);
		SetErrorMessage(ibT("The specified database file '") + strDatabase + ibT("' does not exist."));
		ThrowDatabaseException();
		return false;
	}
	return Open(strDatabase);
}

bool ibDatabaseLayerSQLite::Open(const ibString& strDatabase)
{
	ResetErrorCodes();

	m_strDatabasePath = strDatabase;

	std::string databaseNameBuffer = ConvertToUnicodeStream(strDatabase);
	sqlite3* pDbPtr = (sqlite3*)m_pDatabase;
	int nReturn = sqlite3_open(databaseNameBuffer.c_str(), &pDbPtr);
	{
		std::lock_guard<std::mutex> guard(m_cancelGuard);   // what Cancel reads
		m_pDatabase = pDbPtr;
	}

	if (nReturn != SQLITE_OK)
	{
		SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)),
			ClassifyDatabaseError(sqlite3_errcode((sqlite3*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}

	// Enable WAL mode for concurrent read/write access from multiple threads
	sqlite3_exec((sqlite3*)m_pDatabase, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
	sqlite3_busy_timeout((sqlite3*)m_pDatabase, kBusyTimeoutMs);

	return true;
}

// close database  
bool ibDatabaseLayerSQLite::Close()
{
	ResetErrorCodes();

	CloseResultSets();
	CloseStatements();

	if (m_pDatabase != nullptr)
	{
		std::lock_guard<std::mutex> guard(m_cancelGuard);   // not under a Cancel still using it
		int nReturn = sqlite3_close((sqlite3*)m_pDatabase);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)),
			ClassifyDatabaseError(sqlite3_errcode((sqlite3*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
			ThrowDatabaseException();
			return false;
		}
		m_pDatabase = nullptr;
	}

	return true;
}

bool ibDatabaseLayerSQLite::IsOpen()
{
	return (m_pDatabase != nullptr);
}

// The one call SQLite takes from another thread on a busy connection: the statement running there returns
// SQLITE_INTERRUPT to its own caller. Nothing running, nothing to stop.
void ibDatabaseLayerSQLite::Cancel()
{
	// Held for the call: the owner closing meanwhile waits for it rather than closing under it (m_cancelGuard).
	std::lock_guard<std::mutex> guard(m_cancelGuard);
	if (m_pDatabase != nullptr)
		sqlite3_interrupt((sqlite3*)m_pDatabase);
}

void ibDatabaseLayerSQLite::DoBeginTransaction(const ibTxOptions& opts)
{
	// SQLite locks the file, and Open() asks it to wait five seconds (kBusyTimeoutMs).
	// A noWait transaction must not spend that wait: the registry thread's heartbeat, sweep
	// and snapshot run there, and a locked row has to come back at once so Add can be answered.
	// Zero disables the busy handler for this transaction; commit and rollback put the five
	// seconds back.
	//
	// `snapshot` needs nothing else, and that is a property rather than an omission: a deferred
	// transaction takes its read lock at the first read and holds it to the end, so every statement
	// in it already sees one committed state. SQLite gives for free what the others must be asked for.
	if (opts.noWait && m_pDatabase != nullptr)
		sqlite3_busy_timeout((sqlite3*)m_pDatabase, 0);
	ibJournalInfo(ibT("db.sqlite"),ibT("Beginning transaction"));
	try {
		DoRunQuery(ibT("begin deferred transaction;"), false);
	}
	catch (...) {
		if (opts.noWait && m_pDatabase != nullptr)
			sqlite3_busy_timeout((sqlite3*)m_pDatabase, kBusyTimeoutMs);
		throw;
	}
}

void ibDatabaseLayerSQLite::DoCommit()
{
	ibJournalInfo(ibT("db.sqlite"),ibT("Commiting transaction"));
	try {
		DoRunQuery(ibT("commit transaction;"), false);
	}
	catch (...) {
		if (m_pDatabase != nullptr)
			sqlite3_busy_timeout((sqlite3*)m_pDatabase, kBusyTimeoutMs);
		throw;
	}
	if (m_pDatabase != nullptr)
		sqlite3_busy_timeout((sqlite3*)m_pDatabase, kBusyTimeoutMs);
}

void ibDatabaseLayerSQLite::DoRollBack()
{
	ibJournalInfo(ibT("db.sqlite"),ibT("Rolling back transaction"));
	try {
		DoRunQuery(ibT("rollback transaction;"), false);
	}
	catch (...) {
		if (m_pDatabase != nullptr)
			sqlite3_busy_timeout((sqlite3*)m_pDatabase, kBusyTimeoutMs);
		throw;
	}
	if (m_pDatabase != nullptr)
		sqlite3_busy_timeout((sqlite3*)m_pDatabase, kBusyTimeoutMs);
}

// IsActiveTransaction inherits the base-class default (m_txDepth > 0).

// query database
int ibDatabaseLayerSQLite::DoRunQuery(const ibString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();

	if (m_pDatabase == nullptr)
		return false;

	std::vector<ibString> QueryArray;
	if (bParseQuery)
		QueryArray = ParseQueries(strQuery);
	else
		QueryArray.push_back(strQuery);

	std::vector<ibString>::iterator start = QueryArray.begin();
	std::vector<ibString>::iterator stop = QueryArray.end();

	while (start != stop)
	{
		char* szErrorMessage = nullptr;
		ibString strErrorMessage = ibEmptyString;
		std::string sqlBuffer = ConvertToUnicodeStream(*start);
		int nReturn = sqlite3_exec((sqlite3*)m_pDatabase, sqlBuffer.c_str(), 0, 0, &szErrorMessage);

		if (szErrorMessage != nullptr)
		{
			strErrorMessage = ConvertFromUnicodeStream(szErrorMessage);
			sqlite3_free(szErrorMessage);
		}

		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)),
			ClassifyDatabaseError(sqlite3_errcode((sqlite3*)m_pDatabase)));
			SetErrorMessage(strErrorMessage);
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}

		start++;
	}
	return (sqlite3_changes((sqlite3*)m_pDatabase));
}

ibDatabaseResultSet* ibDatabaseLayerSQLite::DoRunQueryWithResults(const ibString& strQuery)
{
	ResetErrorCodes();

	if (m_pDatabase != nullptr)
	{
		std::vector<ibString> QueryArray = ParseQueries(strQuery);

		for (unsigned int i = 0; i < (QueryArray.size() - 1); i++)
		{
			char* szErrorMessage = nullptr;
			ibString strErrorMessage = ibEmptyString;
			std::string sqlBuffer = ConvertToUnicodeStream(QueryArray[i]);
			int nReturn = sqlite3_exec((sqlite3*)m_pDatabase, sqlBuffer.c_str(), 0, 0, &szErrorMessage);

			if (szErrorMessage != nullptr)
			{
				SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)),
			ClassifyDatabaseError(sqlite3_errcode((sqlite3*)m_pDatabase)));
				strErrorMessage = ConvertFromUnicodeStream(szErrorMessage);
				sqlite3_free(szErrorMessage);
				return nullptr;
			}

			if (nReturn != SQLITE_OK)
			{
				SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)),
			ClassifyDatabaseError(sqlite3_errcode((sqlite3*)m_pDatabase)));
				SetErrorMessage(strErrorMessage);
				ThrowDatabaseException();
				return nullptr;
			}
		}

		// Create a Prepared statement for the last SQL statement and get a result set from it
		ibPreparedStatementSQLite* pStatement = (ibPreparedStatementSQLite*)DoPrepareStatement(QueryArray[QueryArray.size() - 1], false);
		ibDatabaseResultSetSQLite* pResultSet = new ibDatabaseResultSetSQLite(pStatement, true);

		LogResultSetForCleanup(pResultSet);
		return pResultSet;
	}
	else
	{
		return nullptr;
	}
}

ibPreparedStatement* ibDatabaseLayerSQLite::DoPrepareStatement(const ibString& strQuery)
{
	return DoPrepareStatement(strQuery, true);
}

ibPreparedStatement* ibDatabaseLayerSQLite::DoPrepareStatement(const ibString& strQuery, bool bLogForCleanup)
{
	ResetErrorCodes();

	if (m_pDatabase != nullptr)
	{
		ibPreparedStatementSQLite* pReturnStatement = new ibPreparedStatementSQLite((sqlite3*)m_pDatabase);

		std::vector<ibString> QueryArray = ParseQueries(strQuery);

		std::vector<ibString>::iterator start = QueryArray.begin();
		std::vector<ibString>::iterator stop = QueryArray.end();

		while (start != stop)
		{
			const char* szTail = 0;
			std::string sqlBuffer;
			do
			{
				sqlite3_stmt* pStatement;
				ibString strSQL;
				if (szTail != 0)
				{
					strSQL = ConvertFromUnicodeStream(szTail);   // the rest of the text, UTF-8 bytes as sqlite left them
				}
				else
				{
					strSQL = (*start);
				}
				sqlBuffer = ConvertToUnicodeStream(strSQL);
#if SQLITE_VERSION_NUMBER>=3047002
				int nReturn = sqlite3_prepare_v3((sqlite3*)m_pDatabase, sqlBuffer.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &pStatement, &szTail);
#elif SQLITE_VERSION_NUMBER>=3003009
				int nReturn = sqlite3_prepare_v2((sqlite3*)m_pDatabase, sqlBuffer.c_str(), -1, &pStatement, &szTail);
#else
				int nReturn = sqlite3_prepare((sqlite3*)m_pDatabase, sqlBuffer.c_str(), -1, &pStatement, &szTail);
#endif

				if (nReturn != SQLITE_OK)
				{
					SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn), ClassifyDatabaseError(nReturn));
					SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
					delete pReturnStatement;
					ThrowDatabaseException();
					return nullptr;
				}
				pReturnStatement->AddPreparedStatement(pStatement);

			} while (strlen(szTail) > 0);

			start++;
		}

		if (bLogForCleanup)
			LogStatementForCleanup(pReturnStatement);
		return pReturnStatement;
	}
	else
	{
		return nullptr;
	}
}

bool ibDatabaseLayerSQLite::TableExists(const ibString& table)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;

	// Probe via sqlite_master — if the prepared statement or query
	// throws a structured DB exception (ThrowDatabaseException path),
	// treat the table as "not present" and let the cleanup below
	// release whatever we managed to allocate. Callers that need the
	// distinct error path use a raw ibPreparedStatement themselves;
	// TableExists is the convenience predicate.
	try {
		ibString attach = ibT("sqlite_master"), t = table;
		size_t pos_attach = table.find('.');
		if (pos_attach > 0) {
			attach = table.Left(pos_attach + 1) + attach;
			t = table.Right(table.length() - pos_attach - 1);
		}

		ibString query = ibT("SELECT COUNT(*) FROM ") + attach + ibT(" WHERE type='table' AND name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, t);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				if (pResult->Next())
				{
					if (pResult->GetResultInt(1) != 0)
					{
						bReturn = true;
					}
				}
			}
		}
	}
	catch (const ibBackendDatabaseException&) {
		bReturn = false;
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	if (pStatement != nullptr)
	{
		CloseStatement(pStatement);
		pStatement = nullptr;
	}

	return bReturn;
}

bool ibDatabaseLayerSQLite::ViewExists(const ibString& view)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;

	try {
		ibString attach = ibT("sqlite_master"), v = view;
		size_t pos_attach = view.find('.');
		if (pos_attach > 0) {
			attach = view.Left(pos_attach + 1) + attach;
			v = view.Right(view.length() - pos_attach - 1);
		}

		ibString query = ibT("SELECT COUNT(*) FROM ") + attach + ibT(" WHERE type='view' AND name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, v);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				if (pResult->Next())
				{
					if (pResult->GetResultInt(1) != 0)
					{
						bReturn = true;
					}
				}
			}
		}
	}
	catch (const ibBackendDatabaseException&) {
		bReturn = false;
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	if (pStatement != nullptr)
	{
		CloseStatement(pStatement);
		pStatement = nullptr;
	}

	return bReturn;
}

std::vector<ibString> ibDatabaseLayerSQLite::GetTables()
{
	std::vector<ibString> returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT name FROM sqlite_master WHERE type='table';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.push_back(pResult->GetResultString(1));
		}
	}
	catch (const ibBackendDatabaseException&) {
		// Best-effort enumeration — partial results stay in the array.
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

std::vector<ibString> ibDatabaseLayerSQLite::GetViews()
{
	std::vector<ibString> returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT name FROM sqlite_master WHERE type='view';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.push_back(pResult->GetResultString(1));
		}
	}
	catch (const ibBackendDatabaseException&) {
		// Best-effort enumeration — partial results stay in the array.
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

std::vector<ibString> ibDatabaseLayerSQLite::GetColumns(const ibString& table)
{
	std::vector<ibString> returnArray;

	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibDatabaseResultSet* pResult = nullptr;
	ibResultSetMetaData* pMetaData = nullptr;

	try {
		std::string tableNameBuffer = ConvertToUnicodeStream(table);
		ibString query = ibString::Format(ibT("SELECT * FROM '%s' LIMIT 0;"), table.c_str());
		pResult = ExecuteQuery(query);
		pResult->Next();
		pMetaData = pResult->GetMetaData();

		// 1-based
		for (int i = 1; i <= pMetaData->GetColumnCount(); i++)
		{
			returnArray.push_back(pMetaData->GetColumnName(i));
		}
	}
	catch (const ibBackendDatabaseException&) {
		// Missing table / bad name → empty column list, cleanup below
		// still runs.
	}

	if (pMetaData != nullptr)
	{
		pResult->CloseMetaData(pMetaData);
		pMetaData = nullptr;
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

ibBackendDatabaseException::Kind ibDatabaseLayerSQLite::ClassifyDatabaseError(int code)
{
	// Values are SQLite's stable ABI (sqlite3.h SQLITE_*), so the integer literals are safe.
	using Kind = ibBackendDatabaseException::Kind;
	switch (code & 0xFF) {
		case 5:  // SQLITE_BUSY  — database file locked by another process / writer
		case 6:  // SQLITE_LOCKED — table locked by a concurrent connection
			return Kind::Timeout;

		case 19: // SQLITE_CONSTRAINT
			return Kind::Constraint;

		case 1:  // SQLITE_ERROR  — catch-all for SQL errors / missing table
		case 21: // SQLITE_MISUSE — library used incorrectly (usually a bad prepared statement)
			return Kind::Syntax;

		default:
			return Kind::Unknown;
	}
}

int ibDatabaseLayerSQLite::TranslateErrorCode(int nCode)
{
	// An interrupted statement (Cancel -> sqlite3_interrupt) is the cancel, recorded as the platform's.
	if ((nCode & 0xFF) == SQLITE_INTERRUPT)
		return DATABASE_LAYER_QUERY_CANCELLED;

	// Ultimately, this will probably be a map of SQLite database error code values to ibDatabaseLayer values
	// For now though, we'll just return error
	int nReturn = nCode;
	/*
	switch (nCode)
	{
	  case SQLITE_ERROR:
		nReturn = DATABASE_LAYER_SQL_SYNTAX_ERROR;
		break;
	  case SQLITE_INTERNAL:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_PERM:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_ABORT:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_BUSY:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_LOCKED:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_NOMEM:
		nReturn = DATABASE_LAYER_ALLOCATION_ERROR;
		break;
	  case SQLITE_READONLY:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_INTERRUPT:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_IOERR:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_CORRUPT:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_NOTFOUND:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_FULL:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_CANTOPEN:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_PROTOCOL:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_SCHEMA:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_TOOBIG:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_CONSTRAINT:
		nReturn = DATABASE_LAYER_CONSTRAINT_VIOLATION;
		break;
	  case SQLITE_MISMATCH:
		nReturn = DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE;
		break;
	  case SQLITE_MISUSE:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_NOLFS:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_AUTH:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  default:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	}
	*/
	return nReturn;
}


