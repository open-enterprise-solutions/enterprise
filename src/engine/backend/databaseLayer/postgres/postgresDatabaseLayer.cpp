#include "postgresDatabaseLayer.h"
#include "postgresInterface.h"
#include "postgresResultSet.h"
#include "postgresPreparedStatement.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <cstdlib>   // std::strtol — the affected-row count off libpq's ASCII buffer
#include <cstring>   // std::strcmp — the SQLSTATE of a cancelled statement (TranslateErrorCode)

// The PostgreSQL SQL dialect lives WITH its driver — no central factory, no
// type-switch. Static so a test (or anyone) can read it WITHOUT constructing
// the driver (no libpq); the virtual GetDialect() just exposes it to L2.
// Cached singleton (built once, immutable), returned by reference — zero cost.
const ibDialectDictionary& ibDatabaseLayerPostgres::Dialect()
{
	static const ibDialectDictionary s_dialect = [] {
		ibDialectDictionary d;
		d.m_paramStyle = ibParamStyle::DollarN;       // $1, $2, ...
		d.m_pagination = ibPagination::LimitOffset;
		d.m_boolForm   = ibBoolForm::TrueFalse;
		d.m_groupByPosition = true;                   // GROUP BY 2 — a key that binds a value is named by its position
		d.m_features.m_window        = true;
		d.m_features.m_cte           = true;
		d.m_features.m_recursiveCte      = true;
		d.m_features.m_recursiveCteUnion = true;   // UNION in the recursive part — a loop in a parent chain ends
		d.m_features.m_fullOuterJoin = true;
		d.m_features.m_iLike         = true;
		d.m_features.m_rollup        = true;   // GROUP BY ROLLUP(...) — standard spelling
		d.m_features.m_grouping      = true;   // …and GROUPING(expr) with it: PG has had both since 9.5
		d.m_features.m_multiRowValues = true;  // INSERT … VALUES (…), (…)
		// type map
		d.m_typeBoolean       = ibT("BOOLEAN");
		d.m_typeDate          = ibT("TIMESTAMP");
		d.m_typeBlob          = ibT("BYTEA");
		d.m_typeBinaryPattern = ibT("BYTEA");     // PG has no fixed-width binary; BYTEA holds the _RRRef blob
		d.m_typeGuid          = ibT("UUID");
		d.m_typeNumberPattern = ibT("NUMERIC(%d,%d)");
		d.m_analyzePrefix     = ibT("ANALYZE");   // ANALYZE <t> — refresh planner stats (temps aren't autovacuumed)
		d.m_rowIdColumn    = ibT("ctid");         // physical row id for the pre-UNIQUE dedup (keep one row per key)
		d.m_maxIndexSegments = 32;                // INDEX_MAX_KEYS — a build-time constant, 32 in every stock build
		d.m_maxParameters    = 65535;             // the protocol counts them in 16 bits (libpq-fe.h PQ_QUERY_PARAM_MAX_LIMIT)
		d.m_returningClause = ibT("RETURNING");   // PostgreSQL has had it since 8.2
		// Period truncation. date_trunc names seven of the ten units directly; the other three are
		// offsets from the start of the enclosing unit:
		//   TenDays  — the 1st / 11th / 21st. The offset is CAPPED at 2 because a 31-day month
		//              would otherwise floor to 3 and land on the 31st, opening a fourth one-day
		//              bucket. The last ten-day period runs 8-11 days by definition; the cap is
		//              what encodes that.
		//   HalfYear — January or July.
		//   Week     — date_trunc('week') is already ISO (Monday), the convention all engines pin to.
		// (PG's own 'decade' means ten YEARS and is unrelated to TenDays.)
		d.m_periodTrunc = {
			{ ibTotalsPeriod::Second,   ibT("date_trunc('second', {expr})")  },
			{ ibTotalsPeriod::Minute,   ibT("date_trunc('minute', {expr})")  },
			{ ibTotalsPeriod::Hour,     ibT("date_trunc('hour', {expr})")    },
			{ ibTotalsPeriod::Day,      ibT("date_trunc('day', {expr})")     },
			{ ibTotalsPeriod::Week,     ibT("date_trunc('week', {expr})")    },
			{ ibTotalsPeriod::TenDays,  ibT("(date_trunc('month', {expr}) + (LEAST(FLOOR((EXTRACT(DAY FROM {expr}) - 1) / 10), 2) * INTERVAL '10 days'))") },
			{ ibTotalsPeriod::Month,    ibT("date_trunc('month', {expr})")   },
			{ ibTotalsPeriod::Quarter,  ibT("date_trunc('quarter', {expr})") },
			{ ibTotalsPeriod::HalfYear, ibT("(date_trunc('year', {expr}) + (FLOOR((EXTRACT(MONTH FROM {expr}) - 1) / 6) * INTERVAL '6 months'))") },
			{ ibTotalsPeriod::Year,     ibT("date_trunc('year', {expr})")    },
		};

		// The end of a period is the start of the next one less a second — the same sentence the
		// Firebird dictionary and the RAM twin say, so a boundary belongs to its period on every
		// engine. TenDays is deliberately absent from the moving calls (its buckets are not all the
		// same length); the lowering refuses that unit before a dialect is asked.
		auto endOf = [&d](ibTotalsPeriod unit, const ibString& step) {
			return ibT("(") + d.m_periodTrunc[unit] + ibT(" + INTERVAL '1 ") + step + ibT("' - INTERVAL '1 second')");
		};
		d.m_periodEnd = {
			{ ibTotalsPeriod::Second,   endOf(ibTotalsPeriod::Second,   ibT("second")) },
			{ ibTotalsPeriod::Minute,   endOf(ibTotalsPeriod::Minute,   ibT("minute")) },
			{ ibTotalsPeriod::Hour,     endOf(ibTotalsPeriod::Hour,     ibT("hour"))   },
			{ ibTotalsPeriod::Day,      endOf(ibTotalsPeriod::Day,      ibT("day"))    },
			{ ibTotalsPeriod::Week,     endOf(ibTotalsPeriod::Week,     ibT("week"))   },
			{ ibTotalsPeriod::Month,    endOf(ibTotalsPeriod::Month,    ibT("month"))  },
			{ ibTotalsPeriod::Quarter,  endOf(ibTotalsPeriod::Quarter,  ibT("3 months")) },
			{ ibTotalsPeriod::HalfYear, endOf(ibTotalsPeriod::HalfYear, ibT("6 months")) },
			{ ibTotalsPeriod::Year,     endOf(ibTotalsPeriod::Year,     ibT("year"))   },
		};

		auto addOf = [](const ibString& step) {
			return ibT("({expr} + ({count}) * INTERVAL '1 ") + step + ibT("')");
		};
		d.m_dateAdd = {
			{ ibTotalsPeriod::Second,   addOf(ibT("second")) },
			{ ibTotalsPeriod::Minute,   addOf(ibT("minute")) },
			{ ibTotalsPeriod::Hour,     addOf(ibT("hour"))   },
			{ ibTotalsPeriod::Day,      addOf(ibT("day"))    },
			{ ibTotalsPeriod::Week,     addOf(ibT("week"))   },
			{ ibTotalsPeriod::Month,    addOf(ibT("month"))  },
			{ ibTotalsPeriod::Quarter,  ibT("({expr} + (({count}) * 3) * INTERVAL '1 month')") },
			{ ibTotalsPeriod::HalfYear, ibT("({expr} + (({count}) * 6) * INTERVAL '1 month')") },
			{ ibTotalsPeriod::Year,     addOf(ibT("year"))   },
		};

		// ⭐ WHOLE UNITS BETWEEN THE UNITS, not a span divided by a length. Postgres has no DATEDIFF,
		// so the calendar units are counted off the truncated pair (months since year zero, then
		// divided) and the fixed-length ones off the epoch difference. Both agree with the RAM twin,
		// which is the only requirement that matters here.
		const ibString months = ibT("((EXTRACT(YEAR FROM {to})::int * 12 + EXTRACT(MONTH FROM {to})::int)"
		                            " - (EXTRACT(YEAR FROM {from})::int * 12 + EXTRACT(MONTH FROM {from})::int))");
		auto secondsOver = [](const ibString& divisor) {
			return ibT("(FLOOR(EXTRACT(EPOCH FROM (date_trunc('second', {to}) - date_trunc('second', {from}))) / ") + divisor + ibT(")::bigint)");
		};
		d.m_dateDiff = {
			{ ibTotalsPeriod::Second,   secondsOver(ibT("1"))    },
			{ ibTotalsPeriod::Minute,   secondsOver(ibT("60"))   },
			{ ibTotalsPeriod::Hour,     secondsOver(ibT("3600")) },
			{ ibTotalsPeriod::Day,      ibT("(date_trunc('day', {to})::date - date_trunc('day', {from})::date)") },
			{ ibTotalsPeriod::Week,     ibT("((date_trunc('week', {to})::date - date_trunc('week', {from})::date) / 7)") },
			{ ibTotalsPeriod::Month,    months },
			{ ibTotalsPeriod::Quarter,  ibT("(") + months + ibT(" / 3)") },
			{ ibTotalsPeriod::HalfYear, ibT("(") + months + ibT(" / 6)") },
			{ ibTotalsPeriod::Year,     ibT("(EXTRACT(YEAR FROM {to})::int - EXTRACT(YEAR FROM {from})::int)") },
		};

		// ISODOW is Monday = 1 … Sunday = 7, which is exactly the numbering this language pins to —
		// DOW would be Sunday = 0 and would read differently from every other engine.
		d.m_datePart = {
			{ ibDatePart::Year,      ibT("EXTRACT(YEAR FROM {expr})::int")    },
			{ ibDatePart::Quarter,   ibT("EXTRACT(QUARTER FROM {expr})::int") },
			{ ibDatePart::Month,     ibT("EXTRACT(MONTH FROM {expr})::int")   },
			{ ibDatePart::DayOfYear, ibT("EXTRACT(DOY FROM {expr})::int")     },
			{ ibDatePart::Day,       ibT("EXTRACT(DAY FROM {expr})::int")     },
			{ ibDatePart::Week,      ibT("EXTRACT(WEEK FROM {expr})::int")    },
			{ ibDatePart::WeekDay,   ibT("EXTRACT(ISODOW FROM {expr})::int")  },
			{ ibDatePart::Hour,      ibT("EXTRACT(HOUR FROM {expr})::int")    },
			{ ibDatePart::Minute,    ibT("EXTRACT(MINUTE FROM {expr})::int")  },
			{ ibDatePart::Second,    ibT("FLOOR(EXTRACT(SECOND FROM {expr}))::int") },
		};

		d.m_substring = ibT("SUBSTRING({expr} FROM {from} FOR {len})");
		return d;
	}();
	return s_dialect;
}

const ibDialectDictionary& ibDatabaseLayerPostgres::GetDialect() const
{
	return Dialect();
}

// PostgreSQL is the FIRST (and primary) DB temp-table target — ad-hoc `CREATE TEMPORARY TABLE` of
// any shape per query. Strategy = AdHocCreate; lifetime is explicit (the manager DROPs via its
// pinning scope, deterministic, no dependency on commit timing — so m_autoDrops=false, no ON
// COMMIT clause). Its mere PRESENCE flips PG off the RAM floor onto the temp path. (docs/private/temp-db.md)
const ibTempTableDialect& ibDatabaseLayerPostgres::TempDialect()
{
	static const ibTempTableDialect s_temp = [] {
		ibTempTableDialect t;
		t.m_strategy      = ibTempTableDialect::Strategy::AdHocCreate;
		t.m_createPrefix  = ibT("CREATE TEMPORARY TABLE");
		t.m_onCommitClause = ibEmptyString;     // session-scoped; the manager drops it explicitly
		t.m_autoDrops     = false;              // explicit DROP via the pinning scope (RAII, leak-free)
		t.m_dropPrefix    = ibT("DROP TABLE");
		return t;
	}();
	return s_temp;
}

const ibTempTableDialect* ibDatabaseLayerPostgres::GetTempTableDialect() const
{
	return &TempDialect();
}

// ⭐⭐ PostgreSQL HAS NO MAX / MIN OVER `uuid`, and a reference key is exactly that here — so a fold
// over references cannot run until these exist. Four statements, created with the database.
//
// They live HERE, with the vendor whose gap they fill: this was the last engine-specific DDL written
// out anywhere in `metaCollection`, where nothing else knows one dialect from another.
//
// ⚠ RunStatement, not RunQuery. A function BODY is `BEGIN … ; … ; END`, and RunQuery is the
// printf-formatting, ';'-splitting door — right for text DESCRIBED by a format, wrong for text that
// is already final. This driver happens to ignore the splitting (PQexec parses a multi-statement
// string itself), so nothing was broken; a statement whose text is final still belongs at the door
// meant for it, and the next driver to run one may not be so forgiving.
//
// CREATE OR REPLACE throughout: running this against a database that already has them is a no-op
// rather than an error, which is what makes it safe to run again after a restore.
bool ibDatabaseLayerPostgres::CreateMissingRoutines()
{
	// DDL affects no rows, so the return value is 0 and says nothing about success. A driver reports
	// failure by THROWING — that is the only signal worth reading here. (This used to test the return
	// against DATABASE_LAYER_QUERY_RESULT_ERROR, which is 0, and passed only because this driver
	// reported a hardcoded 1 for every statement.)
	RunStatement(ibT("CREATE OR REPLACE FUNCTION max_uuid(uuid, uuid) RETURNS uuid AS $$ BEGIN IF $2 IS NULL OR $1 < $2 THEN RETURN $2; END IF; RETURN $1; END; $$ LANGUAGE plpgsql;"));
	RunStatement(ibT("CREATE OR REPLACE AGGREGATE max(uuid) (sfunc = max_uuid, stype = uuid, combinefunc = max_uuid, parallel = safe, sortop = operator ( > ));"));
	RunStatement(ibT("CREATE OR REPLACE FUNCTION min_uuid(uuid, uuid) RETURNS uuid AS $$ BEGIN IF $2 IS NULL OR $1 > $2 THEN RETURN $2; END IF; RETURN $1; END; $$ LANGUAGE plpgsql;"));
	RunStatement(ibT("CREATE OR REPLACE AGGREGATE min(uuid) (sfunc = min_uuid, stype = uuid, combinefunc = min_uuid, parallel = safe, sortop = operator ( < ));"));
	return true;
}

// PG materialisation: the production target, and the one per-row engine whose shell is
// two objects instead of one — a trigger here cannot carry a body, it can only EXECUTE a
// FUNCTION, so m_functionShellTemplate is non-empty and the generator emits the function
// first. The body itself is still the shared per-row shape (an accumulating ON CONFLICT
// upsert), which is the whole point of designing to the SQLite floor.
//
// fillfactor on the totals table is not a micro-tune: a totals row is UPDATEd on nearly
// every movement, and leaving free space on the page keeps that update HOT (same page, no
// index write). It is the difference between a totals table that keeps up with posting and
// one that becomes the write bottleneck.
const ibMaterializationDialect& ibDatabaseLayerPostgres::MaterializationDialect()
{
	static const ibMaterializationDialect s_mat = [] {
		ibMaterializationDialect m;
		m.m_family = ibTriggerFamily::PerRow;
		m.m_functionShellTemplate =
			ibT("CREATE OR REPLACE FUNCTION {name}() RETURNS trigger AS $$ BEGIN {body} RETURN NULL; END; $$ LANGUAGE plpgsql");
		m.m_triggerShellTemplate =
			ibT("CREATE TRIGGER {name} {timing} ON {table} FOR EACH ROW EXECUTE FUNCTION {function}()");
		m.m_dropTriggerTemplate  = ibT("DROP TRIGGER IF EXISTS {name} ON {table}");
		m.m_dropFunctionTemplate = ibT("DROP FUNCTION IF EXISTS {name}()");
		m.m_binaryLiteralTemplate = ibT("'\\x{hex}'::bytea");   // X'…' is a BIT string here, not a bytea
		m.m_deltaUpsertTemplate =
			ibT("INSERT INTO {table} ({columns}) SELECT {values}{from}{where} ON CONFLICT ({keys}) DO UPDATE SET {update}");
		m.m_deltaTargetAlias  = ibT("{table}");     // ON CONFLICT names the target by table name
		m.m_deltaSourceAlias  = ibT("excluded");
		m.m_deltaUpdateItem   = ibT("{col} = COALESCE({target}.{col}, 0) + {source}.{col}");   // NULL-safe — see the default
		// NULL-safe like the default, though ON CONFLICT never spends it: leaving a plain `=` here
		// would be a trap for whoever makes this template used (PostgreSQL 15 has MERGE), and the
		// defect it causes — an empty key column matching nothing and inserting a duplicate — costs
		// a write, not a warning. See databaseLayer.h.
		m.m_deltaKeyMatchItem = ibT("{target}.{col} IS NOT DISTINCT FROM {source}.{col}");   // unused by ON CONFLICT — rendered, not spent
		// THE KEY HASH. Thirty-two segments is a ceiling few keys reach, so this is the spare tyre
		// rather than the road — but ON CONFLICT names its target by COLUMN LIST, so if a key ever does
		// pass the ceiling the conflict target has to become this column and the engine must be able to
		// compute it. md5() takes text; ::text renders bytea as hex and a timestamp in ISO, both stable.
		m.m_keyHashItem   = ibT("COALESCE(md5({value}::text), '~')");
		m.m_keyHashJoin   = ibT(" || '.' || ");
		m.m_keyHashDigest = ibT("md5({expr})");
		m.m_totalsTableSuffix = ibT(" WITH (fillfactor = 80)");
		m.m_connectionIdExpr  = ibT("pg_backend_pid()");   // per-backend id — concurrent writers hash apart
		m.m_createViewTemplate = ibT("CREATE OR REPLACE VIEW {name} AS {body}");
		m.m_dropViewTemplate   = ibT("DROP VIEW IF EXISTS {name}");
		// WHAT IS STANDING, asked of the catalogue — the drops need no guard (IF EXISTS), but "is the bundle
		// installed?" is answered by these (ibMaterializeSql::IsInstalled), and with none it answered "yes"
		// for triggers an apply had just dropped. The names were created unquoted, so they are stored folded
		// to lower case.
		m.m_viewExistsQuery    = ibT("SELECT 1 FROM pg_class c WHERE c.relkind = 'v' AND c.relname = LOWER('{name}') AND pg_table_is_visible(c.oid)");
		m.m_triggerExistsQuery = ibT("SELECT 1 FROM pg_trigger t WHERE t.tgname = LOWER('{name}') AND NOT t.tgisinternal");
		return m;
	}();
	return s_mat;
}

const ibMaterializationDialect* ibDatabaseLayerPostgres::GetMaterializationDialect() const
{
	return &MaterializationDialect();
}

// ctor
ibDatabaseLayerPostgres::ibDatabaseLayerPostgres()
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = ibT("localhost");
	m_strUser = ibT("postgres");
	m_strPassword = ibT("");
	m_strDatabase = ibT("postgres");
	m_strPort = ibT("5432");
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const ibString& strDatabase)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = ibT("localhost");
	m_strUser = ibT("postgres");
	m_strPassword = ibT("");
	m_strPort = ibT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const ibString& strServer, const ibString& strDatabase)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = strServer;
	m_strUser = ibT("postgres");
	m_strPassword = ibT("");
	m_strPort = ibT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const ibString& strDatabase, const ibString& strUser, const ibString& strPassword)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = ibT("localhost");
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strPort = ibT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const ibString& strServer, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strPort = ibT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const ibString& strServer, const ibString& strPort, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword)
	: ibDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strPort = strPort;

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const ibDatabaseLayerPostgres& src)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = src.m_strServer;
	m_strUser = src.m_strUser;
	m_strPassword = src.m_strPassword;
	m_strDatabase = src.m_strDatabase;
	m_strPort = src.m_strPort;

	Open(src.m_strDatabase);
}

// dtor
ibDatabaseLayerPostgres::~ibDatabaseLayerPostgres()
{
	Close();
	delete m_pInterface;
}

// open database
bool ibDatabaseLayerPostgres::Open()
{
	ResetErrorCodes();

	if (m_pInterface == nullptr)
		return false;

	std::string serverCharBuffer;
	const char* pHost = nullptr;
	std::string databaseBuffer;
	const char* pDatabase = nullptr;
	std::string userCharBuffer;
	const char* pUser = nullptr;
	std::string passwordCharBuffer;
	const char* pPassword = nullptr;
	const char* pTty = nullptr;
	const char* pOptions = nullptr;
	std::string portCharBuffer;
	const char* pPort = nullptr;

	if (m_strServer != ibT("localhost") && m_strServer != ibT(""))
	{
		serverCharBuffer = ConvertToUnicodeStream(m_strServer);
		pHost = serverCharBuffer.c_str();
	}

	if (m_strUser != ibT(""))
	{
		userCharBuffer = ConvertToUnicodeStream(m_strUser);
		pUser = userCharBuffer.c_str();
	}

	if (m_strPassword != ibT(""))
	{
		passwordCharBuffer = ConvertToUnicodeStream(m_strPassword);
		pPassword = passwordCharBuffer.c_str();
	}

	if (m_strPort != ibT(""))
	{
		portCharBuffer = ConvertToUnicodeStream(m_strPort);
		pPort = portCharBuffer.c_str();
	}

	m_pDatabase = m_pInterface->GetPQsetdbLogin()(pHost, pPort, pOptions, pTty, nullptr, pUser, pPassword);
	if (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) == CONNECTION_BAD)
	{
		SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}
	else
	{
		m_pInterface->GetPQsetClientEncoding()((PGconn*)m_pDatabase, "UTF-8");
	}

	// The connection above deliberately carries no database name: it lands on the server's
	// default so that CREATE DATABASE can run. Once the database is there, the connection is
	// dropped and reopened against it below.
	if (m_strDatabase != ibT(""))
	{
		databaseBuffer = ConvertToUnicodeStream(m_strDatabase);
		pDatabase = databaseBuffer.c_str();

		if (!DatabaseExists(m_strDatabase))
		{
			// CREATE DATABASE affects no rows, so its return value is 0 and carries no verdict.
			// Failure arrives as an exception from DoRunQuery — that is what decides here.
			// (This used to compare the return against DATABASE_LAYER_QUERY_RESULT_ERROR, which
			// is 0; it only ever passed because the driver reported a hardcoded 1.)
			try {
				DoRunQuery(ibT("CREATE DATABASE ") + m_strDatabase, false);
			}
			catch (const ibCoreException&) {
				return false;
			}
			DoRunQuery(ibT("GRANT ALL PRIVILEGES ON DATABASE ") + m_strDatabase + ibT(" to ") + m_strUser, false);
		}
	}

	if (m_pDatabase != nullptr) {
		{
			std::lock_guard<std::mutex> guard(m_cancelGuard);   // not under a Cancel still using it
			if (m_pCancel != nullptr) {
				m_pInterface->GetPQfreeCancel()((PGcancel*)m_pCancel);
				m_pCancel = nullptr;
			}
		}
		m_pInterface->GetPQfinish()((PGconn*)m_pDatabase);
		m_pDatabase = nullptr;
	}

	m_pDatabase = m_pInterface->GetPQsetdbLogin()(pHost, pPort, pOptions, pTty, pDatabase, pUser, pPassword);
	if (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) == CONNECTION_BAD)
	{
		SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}
	else
	{
		m_pInterface->GetPQsetClientEncoding()((PGconn*)m_pDatabase, "UTF-8");

		// Made here, on the thread that owns the connection: PQcancel may use it from any other (Cancel),
		// PQgetCancel may not.
		PGcancel* const made = m_pInterface->GetPQgetCancel()((PGconn*)m_pDatabase);
		std::lock_guard<std::mutex> guard(m_cancelGuard);
		m_pCancel = made;
	}

	return true;
}

bool ibDatabaseLayerPostgres::Open(const ibString& strDatabase)
{
	m_strDatabase = strDatabase;
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const ibString& strServer, const ibString& strDatabase)
{
	m_strServer = strServer;
	m_strUser = ibT("");
	m_strPassword = ibT("");
	m_strDatabase = strDatabase;
	m_strPort = ibT("");
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const ibString& strDatabase, const ibString& strUser, const ibString& strPassword)
{
	m_strServer = ibT("localhost");
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = ibT("");
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const ibString& strServer, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = ibT("");
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const ibString& strServer, const ibString& strPort, const ibString& strDatabase, const ibString& strUser, const ibString& strPassword)
{
	m_strServer = strServer;
	m_strPort = strPort;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;

	return Open();
}

// close database
bool ibDatabaseLayerPostgres::Close()
{
	CloseResultSets();
	CloseStatements();

	{
		std::lock_guard<std::mutex> guard(m_cancelGuard);   // not under a Cancel still using it
		if (m_pCancel != nullptr)
		{
			m_pInterface->GetPQfreeCancel()((PGcancel*)m_pCancel);
			m_pCancel = nullptr;
		}
	}

	if (m_pDatabase)
	{
		m_pInterface->GetPQfinish()((PGconn*)m_pDatabase);
		m_pDatabase = nullptr;
	}

	return true;
}

bool ibDatabaseLayerPostgres::IsOpen()
{
	if (m_pDatabase)
		return (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) != CONNECTION_BAD);
	else
		return false;
}

// The server is asked, on a connection of its own, to stop what this one is running: the statement returns
// SQLSTATE 57014 to its own caller. Nothing running, nothing to stop.
void ibDatabaseLayerPostgres::Cancel()
{
	// Held for the whole call: the owner closing or reopening meanwhile waits for it rather than freeing the
	// PGcancel under it (m_cancelGuard).
	std::lock_guard<std::mutex> guard(m_cancelGuard);
	if (m_pCancel == nullptr)
		return;
	char errbuf[256];
	m_pInterface->GetPQcancel()((PGcancel*)m_pCancel, errbuf, sizeof(errbuf));
}

// transaction support
void ibDatabaseLayerPostgres::DoBeginTransaction(const ibTxOptions& opts)
{
	// ONE COMMITTED STATE FOR THE WHOLE TRANSACTION — see ibDbTxOptions::snapshot. PG's default is
	// READ COMMITTED, where each statement re-reads whatever has committed since, so a report built
	// under it can show a total from before a posting and the lines from after it. REPEATABLE READ is
	// PG's name for the same guarantee Firebird calls concurrency; it must be asked for on the BEGIN
	// itself, since the isolation level cannot be changed once a statement has run.
	DoRunQuery(opts.snapshot
		? (opts.readOnly ? ibT("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY")
		                 : ibT("BEGIN ISOLATION LEVEL REPEATABLE READ"))
		: ibT("BEGIN"), false);

	// lock_timeout of 0 means wait forever. A noWait transaction has to fail
	// the moment a lock is held, on every statement, not only one that spells
	// NOWAIT. If the SET itself fails, the transaction is already aborted:
	// roll it back and let that error out, or the connection stays unusable.
	if (opts.noWait) {
		try {
			DoRunQuery(ibT("SET LOCAL lock_timeout = '1ms'"), false);
		}
		catch (...) {
			try { DoRollBack(); }
			catch (...) {}   // the transaction is already dead; the SET's error is the one that leaves
			throw;
		}
	}
}

void ibDatabaseLayerPostgres::DoCommit()
{
	DoRunQuery(ibT("COMMIT"), false);
}

void ibDatabaseLayerPostgres::DoRollBack()
{
	DoRunQuery(ibT("ROLLBACK"), false);
}

// IsActiveTransaction inherits the base-class default (m_txDepth > 0).

// query database
int ibDatabaseLayerPostgres::DoRunQuery(const ibString& strQuery, bool /*bParseQuery*/)
{
	// PostgreSQL takes care of parsing the queries itself so bParseQuery is ignored

	ResetErrorCodes();
	SetLastSqlState(ibString());

	std::string sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer.c_str());
	if ((pResultCode == nullptr) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_COMMAND_OK))
	{
		if (pResultCode != nullptr) {
			const char* state = m_pInterface->GetPQresultErrorField()(pResultCode, PG_DIAG_SQLSTATE);
			SetLastSqlState(state != nullptr ? ibString::FromUTF8(state) : ibString());
		}
		SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(m_pInterface->GetPQresultStatus()(pResultCode),
			m_pInterface->GetPQresultErrorField()(pResultCode, PG_DIAG_SQLSTATE)), ClassifySqlState(m_lastSqlState));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		m_pInterface->GetPQclear()(pResultCode);
		ThrowDatabaseException();
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}
	else
	{
		// Affected-row count, as the other drivers report it. PQcmdTuples is empty for any
		// statement that touches no rows (DDL, SET, ...) — that reads as 0, which is the
		// truth, not an error. Failure arrives as the exception thrown above.
		//
		// Read straight off the libpq buffer: it is an ASCII decimal digit string, so it
		// needs neither the charset conversion nor a string. The old code built that
		// string and then threw it away with the parse commented out — this does less
		// work than either version.
		const char* const cmdTuples = m_pInterface->GetPQcmdTuples()(pResultCode);
		long rows = 0;
		if (cmdTuples != nullptr && *cmdTuples != '\0')
			rows = std::strtol(cmdTuples, nullptr, 10);
		m_pInterface->GetPQclear()(pResultCode);
		return (int)rows;
	}
}

ibDatabaseResultSet* ibDatabaseLayerPostgres::DoRunQueryWithResults(const ibString& strQuery)
{
	ResetErrorCodes();
	SetLastSqlState(ibString());

	std::string sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer.c_str());
	if ((pResultCode == nullptr) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_TUPLES_OK))
	{
		if (pResultCode != nullptr) {
			const char* state = m_pInterface->GetPQresultErrorField()(pResultCode, PG_DIAG_SQLSTATE);
			SetLastSqlState(state != nullptr ? ibString::FromUTF8(state) : ibString());
		}
		SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase),
			m_pInterface->GetPQresultErrorField()(pResultCode, PG_DIAG_SQLSTATE)), ClassifySqlState(m_lastSqlState));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		m_pInterface->GetPQclear()(pResultCode);
		ThrowDatabaseException();
		return nullptr;
	}
	else
	{
		ibDatabaseResultSetPostgres* pResultSet = new ibDatabaseResultSetPostgres(m_pInterface, pResultCode);
		LogResultSetForCleanup(pResultSet);
		return pResultSet;
	}
}

// ibPreparedStatement support
ibPreparedStatement* ibDatabaseLayerPostgres::DoPrepareStatement(const ibString& strQuery)
{
	ResetErrorCodes();

	ibPreparedStatementPostgres* pStatement = ibPreparedStatementPostgres::CreateStatement(m_pInterface, (PGconn*)m_pDatabase, strQuery);
	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool ibDatabaseLayerPostgres::DatabaseExists(const ibString& database)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT COUNT(*) FROM pg_catalog.pg_database WHERE datname=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement != nullptr) {
			pStatement->SetParamString(1, database.Lower());
			pResult = pStatement->ExecuteQuery();
			if (pResult) {
				if (pResult->Next()) {
					if (pResult->GetResultInt(1) != 0) {
						bReturn = true;
					}
				}
			}
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
	}
	catch (const ibCoreException&) {
		// Close any still-open resources before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		if (pStatement != nullptr) {
			CloseStatement(pStatement);
			pStatement = nullptr;
		}
		throw;
	}

	return bReturn;
}

bool ibDatabaseLayerPostgres::TableExists(const ibString& table)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement != nullptr) {
			pStatement->SetParamString(1, table.Lower());
			pResult = pStatement->ExecuteQuery();
			if (pResult) {
				if (pResult->Next()) {
					if (pResult->GetResultInt(1) != 0) {
						bReturn = true;
					}
				}
			}
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
	}
	catch (const ibCoreException&) {
		// Close any still-open resources before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		if (pStatement != nullptr) {
			CloseStatement(pStatement);
			pStatement = nullptr;
		}
		throw;
	}

	return bReturn;
}

bool ibDatabaseLayerPostgres::ViewExists(const ibString& view)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='VIEW' AND table_name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement) {
			pStatement->SetParamString(1, view.Lower());
			pResult = pStatement->ExecuteQuery();
			if (pResult) {
				if (pResult->Next()) {
					if (pResult->GetResultInt(1) != 0) {
						bReturn = true;
					}
				}
			}
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
	}
	catch (const ibCoreException&) {
		// Close any still-open resources before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		if (pStatement != nullptr) {
			CloseStatement(pStatement);
			pStatement = nullptr;
		}
		throw;
	}

	return bReturn;
}

std::vector<ibString> ibDatabaseLayerPostgres::GetTables()
{
	std::vector<ibString> returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT table_name FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.push_back(pResult->GetResultString(1));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibCoreException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return returnArray;
}

std::vector<ibString> ibDatabaseLayerPostgres::GetViews()
{
	std::vector<ibString> returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT table_name FROM information_schema.tables WHERE table_type='VIEW' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.push_back(pResult->GetResultString(1));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibCoreException&) {
		// Close any still-open result set before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		throw;
	}

	return returnArray;
}

std::vector<ibString> ibDatabaseLayerPostgres::GetColumns(const ibString& table)
{
	// Initialize variables
	std::vector<ibString> returnArray;

	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		ibString query = ibT("SELECT column_name FROM information_schema.columns WHERE table_name=? ORDER BY ordinal_position;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, table);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				while (pResult->Next())
				{
					returnArray.push_back(pResult->GetResultString(1));
				}
			}
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
	}
	catch (const ibCoreException&) {
		// Close any still-open resources before propagating; preserves the
		// in-flight exception (sqlstate / native_code on derived types).
		if (pResult != nullptr) {
			CloseResultSet(pResult);
			pResult = nullptr;
		}
		if (pStatement != nullptr) {
			CloseStatement(pStatement);
			pStatement = nullptr;
		}
		throw;
	}

	return returnArray;
}

int ibDatabaseLayerPostgres::TranslateErrorCode(int nCode, const char* sqlState)
{
	// An interrupted statement (Cancel -> PQcancel) is the cancel, recorded as the platform's.
	if (sqlState != nullptr && std::strcmp(sqlState, "57014") == 0)   // query_canceled
		return DATABASE_LAYER_QUERY_CANCELLED;

	// Ultimately, this will probably be a map of Postgresql database error code values to ibDatabaseLayer values
	// For now though, we'll just return error
	return nCode;
	//return DATABASE_LAYER_ERROR;
}

ibBackendDatabaseException::Kind ibDatabaseLayerPostgres::ClassifySqlState(const ibString& sqlState)
{
	// SQLSTATE classes:
	//   08*** — connection exception           → ConnectionLost
	//   23*** — integrity constraint violation → Constraint
	//   40*** — transaction rollback           → Deadlock (40P01) /
	//                                            Timeout (40001 serialization fail)
	//   42*** — syntax / access rule violation → Syntax
	//   53*** — insufficient resources         → Timeout
	//   55P03 — lock_not_available             → Timeout (NOWAIT / lock_timeout)
	//   57*** — operator intervention          → ConnectionLost (admin shutdown)
	//
	// See https://www.postgresql.org/docs/current/errcodes-appendix.html
	using Kind = ibBackendDatabaseException::Kind;

	if (sqlState == ibT("55P03"))
		return Kind::Timeout;
	if (sqlState.length() >= 2) {
		const ibString cls = sqlState.Left(2);
		if (cls == ibT("08")) return Kind::ConnectionLost;
		if (cls == ibT("23")) return Kind::Constraint;
		if (cls == ibT("42")) return Kind::Syntax;
		if (cls == ibT("57")) return Kind::ConnectionLost;
		if (cls == ibT("53")) return Kind::Timeout;
		if (cls == ibT("40")) {
			// 40P01 = deadlock_detected, 40001 = serialization_failure
			if (sqlState == ibT("40P01")) return Kind::Deadlock;
			return Kind::Timeout;
		}
	}
	return Kind::Unknown;
}

bool ibDatabaseLayerPostgres::IsAvailable()
{
	ibInterfacePostgres library;
	return library.Init();
}

