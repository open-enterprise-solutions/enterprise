#include "postgresDatabaseLayer.h"
#include "postgresInterface.h"
#include "postgresResultSet.h"
#include "postgresPreparedStatement.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <cstdlib>   // std::strtol — the affected-row count off libpq's ASCII buffer

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
		d.m_features.m_window        = true;
		d.m_features.m_cte           = true;
		d.m_features.m_fullOuterJoin = true;
		d.m_features.m_iLike         = true;
		d.m_features.m_rollup        = true;   // GROUP BY ROLLUP(...) — standard spelling
		// type map
		d.m_typeBoolean       = wxT("BOOLEAN");
		d.m_typeDate          = wxT("TIMESTAMP");
		d.m_typeBlob          = wxT("BYTEA");
		d.m_typeBinaryPattern = wxT("BYTEA");     // PG has no fixed-width binary; BYTEA holds the _RRRef blob
		d.m_typeGuid          = wxT("UUID");
		d.m_typeNumberPattern = wxT("NUMERIC(%d,%d)");
		d.m_analyzePrefix     = wxT("ANALYZE");   // ANALYZE <t> — refresh planner stats (temps aren't autovacuumed)
		d.m_indexListQuery = wxT("SELECT indexname FROM pg_indexes WHERE tablename = LOWER(?)");   // differ introspects existing indexes
		d.m_rowIdColumn    = wxT("ctid");         // physical row id for the pre-UNIQUE dedup (keep one row per key)
		d.m_returningClause = wxT("RETURNING");   // PostgreSQL has had it since 8.2
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
			{ ibTotalsPeriod::Second,   wxT("date_trunc('second', {expr})")  },
			{ ibTotalsPeriod::Minute,   wxT("date_trunc('minute', {expr})")  },
			{ ibTotalsPeriod::Hour,     wxT("date_trunc('hour', {expr})")    },
			{ ibTotalsPeriod::Day,      wxT("date_trunc('day', {expr})")     },
			{ ibTotalsPeriod::Week,     wxT("date_trunc('week', {expr})")    },
			{ ibTotalsPeriod::TenDays,  wxT("(date_trunc('month', {expr}) + (LEAST(FLOOR((EXTRACT(DAY FROM {expr}) - 1) / 10), 2) * INTERVAL '10 days'))") },
			{ ibTotalsPeriod::Month,    wxT("date_trunc('month', {expr})")   },
			{ ibTotalsPeriod::Quarter,  wxT("date_trunc('quarter', {expr})") },
			{ ibTotalsPeriod::HalfYear, wxT("(date_trunc('year', {expr}) + (FLOOR((EXTRACT(MONTH FROM {expr}) - 1) / 6) * INTERVAL '6 months'))") },
			{ ibTotalsPeriod::Year,     wxT("date_trunc('year', {expr})")    },
		};
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
// COMMIT clause). Its mere PRESENCE flips PG off the RAM floor onto the temp path. (docs/temp-db.md)
const ibTempTableDialect& ibDatabaseLayerPostgres::TempDialect()
{
	static const ibTempTableDialect s_temp = [] {
		ibTempTableDialect t;
		t.m_strategy      = ibTempTableDialect::Strategy::AdHocCreate;
		t.m_createPrefix  = wxT("CREATE TEMPORARY TABLE");
		t.m_onCommitClause = wxEmptyString;     // session-scoped; the manager drops it explicitly
		t.m_autoDrops     = false;              // explicit DROP via the pinning scope (RAII, leak-free)
		t.m_dropPrefix    = wxT("DROP TABLE");
		return t;
	}();
	return s_temp;
}

const ibTempTableDialect* ibDatabaseLayerPostgres::GetTempTableDialect() const
{
	return &TempDialect();
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
			wxT("CREATE OR REPLACE FUNCTION {name}() RETURNS trigger AS $$ BEGIN {body} RETURN NULL; END; $$ LANGUAGE plpgsql");
		m.m_triggerShellTemplate =
			wxT("CREATE TRIGGER {name} {timing} ON {table} FOR EACH ROW EXECUTE FUNCTION {function}()");
		m.m_dropTriggerTemplate  = wxT("DROP TRIGGER IF EXISTS {name} ON {table}");
		m.m_dropFunctionTemplate = wxT("DROP FUNCTION IF EXISTS {name}()");
		m.m_deltaUpsertTemplate =
			wxT("INSERT INTO {table} ({columns}) SELECT {values}{from}{where} ON CONFLICT ({keys}) DO UPDATE SET {update}");
		m.m_deltaTargetAlias  = wxT("{table}");     // ON CONFLICT names the target by table name
		m.m_deltaSourceAlias  = wxT("excluded");
		m.m_deltaUpdateItem   = wxT("{col} = {target}.{col} + {source}.{col}");
		m.m_deltaKeyMatchItem = wxT("{target}.{col} = {source}.{col}");   // unused by ON CONFLICT — rendered, not spent
		m.m_totalsTableSuffix = wxT(" WITH (fillfactor = 80)");
		m.m_connectionIdExpr  = wxT("pg_backend_pid()");   // per-backend id — concurrent writers hash apart
		m.m_createViewTemplate = wxT("CREATE OR REPLACE VIEW {name} AS {body}");
		m.m_dropViewTemplate   = wxT("DROP VIEW IF EXISTS {name}");
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
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = wxT("localhost");
	m_strUser = wxT("postgres");
	m_strPassword = wxT("");
	m_strDatabase = wxT("postgres");
	m_strPort = wxT("5432");
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const wxString& strDatabase)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = wxT("localhost");
	m_strUser = wxT("postgres");
	m_strPassword = wxT("");
	m_strPort = wxT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const wxString& strServer, const wxString& strDatabase)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = strServer;
	m_strUser = wxT("postgres");
	m_strPassword = wxT("");
	m_strPort = wxT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = wxT("localhost");
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strPort = wxT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: ibDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strPort = wxT("5432");

	Open(strDatabase);
}

ibDatabaseLayerPostgres::ibDatabaseLayerPostgres(const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: ibDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfacePostgres();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
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
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
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
	wxDELETE(m_pInterface);
}

// open database
bool ibDatabaseLayerPostgres::Open()
{
	ResetErrorCodes();

	if (m_pInterface == nullptr)
		return false;

	wxCharBuffer serverCharBuffer;
	const char* pHost = nullptr;
	wxCharBuffer databaseBuffer;
	const char* pDatabase = nullptr;
	wxCharBuffer userCharBuffer;
	const char* pUser = nullptr;
	wxCharBuffer passwordCharBuffer;
	const char* pPassword = nullptr;
	const char* pTty = nullptr;
	const char* pOptions = nullptr;
	wxCharBuffer portCharBuffer;
	const char* pPort = nullptr;

	if (m_strServer != wxT("localhost") && m_strServer != wxT(""))
	{
		serverCharBuffer = ConvertToUnicodeStream(m_strServer);
		pHost = serverCharBuffer;
	}

	if (m_strUser != wxT(""))
	{
		userCharBuffer = ConvertToUnicodeStream(m_strUser);
		pUser = userCharBuffer;
	}

	if (m_strPassword != wxT(""))
	{
		passwordCharBuffer = ConvertToUnicodeStream(m_strPassword);
		pPassword = passwordCharBuffer;
	}

	if (m_strPort != wxT(""))
	{
		portCharBuffer = ConvertToUnicodeStream(m_strPort);
		pPort = portCharBuffer;
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
		wxCSConv conv((const char*)(m_pInterface->GetPQencodingToChar()(m_pInterface->GetPQclientEncoding()((PGconn*)m_pDatabase))));
		SetEncoding(&conv);
	}

	// The connection above deliberately carries no database name: it lands on the server's
	// default so that CREATE DATABASE can run. Once the database is there, the connection is
	// dropped and reopened against it below.
	if (m_strDatabase != wxT(""))
	{
		databaseBuffer = ConvertToUnicodeStream(m_strDatabase);
		pDatabase = databaseBuffer;

		if (!DatabaseExists(m_strDatabase))
		{
			// CREATE DATABASE affects no rows, so its return value is 0 and carries no verdict.
			// Failure arrives as an exception from DoRunQuery — that is what decides here.
			// (This used to compare the return against DATABASE_LAYER_QUERY_RESULT_ERROR, which
			// is 0; it only ever passed because the driver reported a hardcoded 1.)
			try {
				DoRunQuery("CREATE DATABASE " + m_strDatabase, false);
			}
			catch (const ibBackendException&) {
				return false;
			}
			DoRunQuery("GRANT ALL PRIVILEGES ON DATABASE " + m_strDatabase + " to " + m_strUser, false);
		}
	}

	if (m_pDatabase != nullptr) {
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
		wxCSConv conv((const char*)(m_pInterface->GetPQencodingToChar()(m_pInterface->GetPQclientEncoding()((PGconn*)m_pDatabase))));
		SetEncoding(&conv);
	}

	return true;
}

bool ibDatabaseLayerPostgres::Open(const wxString& strDatabase)
{
	m_strDatabase = strDatabase;
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const wxString& strServer, const wxString& strDatabase)
{
	m_strServer = strServer;
	m_strUser = wxT("");
	m_strPassword = wxT("");
	m_strDatabase = strDatabase;
	m_strPort = wxT("");
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = wxT("localhost");
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = wxT("");
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = wxT("");
	return Open();
}

bool ibDatabaseLayerPostgres::Open(const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
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

// transaction support
void ibDatabaseLayerPostgres::DoBeginTransaction(const ibTxOptions& opts)
{
	DoRunQuery(wxT("BEGIN"), false);

	// PG's NOWAIT behaviour is per-statement (`SELECT ... FOR UPDATE NOWAIT`)
	// or session-level (`SET lock_timeout`). Inside a TX the cleanest knob
	// is `SET LOCAL lock_timeout = 0` — applies only to this TX, reverts
	// on commit/rollback. Lets a noWait transaction (ibTxOptions::noWait)
	// fail immediately with a lock-timeout exception rather than blocking.
	if (opts.noWait) {
		try { DoRunQuery(wxT("SET LOCAL lock_timeout = 0"), false); }
		catch (...) { /* best-effort — server without lock_timeout just waits */ }
	}
}

void ibDatabaseLayerPostgres::DoCommit()
{
	DoRunQuery(wxT("COMMIT"), false);
}

void ibDatabaseLayerPostgres::DoRollBack()
{
	DoRunQuery(wxT("ROLLBACK"), false);
}

// IsActiveTransaction inherits the base-class default (m_txDepth > 0).

// query database
int ibDatabaseLayerPostgres::DoRunQuery(const wxString& strQuery, bool WXUNUSED(bParseQuery))
{
	// PostgreSQL takes care of parsing the queries itself so bParseQuery is ignored

	ResetErrorCodes();

	wxCharBuffer sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer);
	if ((pResultCode == nullptr) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_COMMAND_OK))
	{
		SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(m_pInterface->GetPQresultStatus()(pResultCode)));
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
		// needs neither the charset conversion nor a wxString. The old code built that
		// wxString and then threw it away with the parse commented out — this does less
		// work than either version.
		const char* const cmdTuples = m_pInterface->GetPQcmdTuples()(pResultCode);
		long rows = 0;
		if (cmdTuples != nullptr && *cmdTuples != '\0')
			rows = std::strtol(cmdTuples, nullptr, 10);
		m_pInterface->GetPQclear()(pResultCode);
		return (int)rows;
	}
}

ibDatabaseResultSet* ibDatabaseLayerPostgres::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();

	wxCharBuffer sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer);
	if ((pResultCode == nullptr) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_TUPLES_OK))
	{
		SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		m_pInterface->GetPQclear()(pResultCode);
		ThrowDatabaseException();
		return nullptr;
	}
	else
	{
		ibDatabaseResultSetPostgres* pResultSet = new ibDatabaseResultSetPostgres(m_pInterface, pResultCode);
		pResultSet->SetEncoding(GetEncoding());
		LogResultSetForCleanup(pResultSet);
		return pResultSet;
	}
}

// ibPreparedStatement support
ibPreparedStatement* ibDatabaseLayerPostgres::DoPrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();

	ibPreparedStatementPostgres* pStatement = ibPreparedStatementPostgres::CreateStatement(m_pInterface, (PGconn*)m_pDatabase, strQuery);
	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool ibDatabaseLayerPostgres::DatabaseExists(const wxString& database)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT COUNT(*) FROM pg_catalog.pg_database WHERE datname=?;");
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
	catch (const ibBackendException&) {
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

bool ibDatabaseLayerPostgres::TableExists(const wxString& table)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_name=?;");
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
	catch (const ibBackendException&) {
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

bool ibDatabaseLayerPostgres::ViewExists(const wxString& view)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='VIEW' AND table_name=?;");
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
	catch (const ibBackendException&) {
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

wxArrayString ibDatabaseLayerPostgres::GetTables()
{
	wxArrayString returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT table_name FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibBackendException&) {
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

wxArrayString ibDatabaseLayerPostgres::GetViews()
{
	wxArrayString returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT table_name FROM information_schema.tables WHERE table_type='VIEW' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}
	catch (const ibBackendException&) {
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

wxArrayString ibDatabaseLayerPostgres::GetColumns(const wxString& table)
{
	// Initialize variables
	wxArrayString returnArray;

	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT column_name FROM information_schema.columns WHERE table_name=? ORDER BY ordinal_position;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, table);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				while (pResult->Next())
				{
					returnArray.Add(pResult->GetResultString(1));
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
	catch (const ibBackendException&) {
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

int ibDatabaseLayerPostgres::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Postgresql database error code values to ibDatabaseLayer values
	// For now though, we'll just return error
	return nCode;
	//return DATABASE_LAYER_ERROR;
}

ibBackendDatabaseException::Kind ibDatabaseLayerPostgres::ClassifyDatabaseError(int nativeCode) const
{
	// PostgreSQL surfaces both a CONNECTION_* status enum (small int,
	// connection-level only) and SQLSTATE (5-char alphanumeric — the
	// real classifier). We prefer SQLSTATE when present; the int code
	// is only used for connection-level failures where libpq doesn't
	// produce a SQLSTATE at all.
	//
	// SQLSTATE classes:
	//   08*** — connection exception           → ConnectionLost
	//   23*** — integrity constraint violation → Constraint
	//   40*** — transaction rollback           → Deadlock (40P01) /
	//                                            Timeout (40001 serialization fail)
	//   42*** — syntax / access rule violation → Syntax
	//   53*** — insufficient resources         → Timeout
	//   57*** — operator intervention          → ConnectionLost (admin shutdown)
	//
	// See https://www.postgresql.org/docs/current/errcodes-appendix.html
	using Kind = ibBackendDatabaseException::Kind;

	if (m_lastSqlState.length() >= 2) {
		const wxString cls = m_lastSqlState.Left(2);
		if (cls == wxT("08")) return Kind::ConnectionLost;
		if (cls == wxT("23")) return Kind::Constraint;
		if (cls == wxT("42")) return Kind::Syntax;
		if (cls == wxT("57")) return Kind::ConnectionLost;
		if (cls == wxT("53")) return Kind::Timeout;
		if (cls == wxT("40")) {
			// 40P01 = deadlock_detected, 40001 = serialization_failure
			if (m_lastSqlState == wxT("40P01")) return Kind::Deadlock;
			return Kind::Timeout;
		}
	}

	// No SQLSTATE — fall back to the libpq connection-status enum.
	// CONNECTION_BAD is the only one we surface in error paths today.
	(void)nativeCode;
	return Kind::Unknown;
}

bool ibDatabaseLayerPostgres::IsAvailable()
{
	bool bAvailable = false;
	ibInterfacePostgres* pInterface = new ibInterfacePostgres();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

