#include "postgresDatabaseLayer.h"
#include "postgresInterface.h"
#include "postgresResultSet.h"
#include "postgresPreparedStatement.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

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

	if (m_strDatabase != wxT("") && !DatabaseExists(m_strDatabase)) {
		bool result = DoRunQuery("CREATE DATABASE " + m_strDatabase, false) != DATABASE_LAYER_QUERY_RESULT_ERROR;
		if (!result)
			return false;
		DoRunQuery("GRANT ALL PRIVILEGES ON DATABASE " + m_strDatabase + " to " + m_strUser, false);
	}

	if (m_strDatabase != wxT(""))
	{
		databaseBuffer = ConvertToUnicodeStream(m_strDatabase);
		pDatabase = databaseBuffer;

		if (!DatabaseExists(m_strDatabase))
		{
			bool result = DoRunQuery("CREATE DATABASE " + m_strDatabase, false) != DATABASE_LAYER_QUERY_RESULT_ERROR;
			if (!result)
				return false;
			DoRunQuery("GRANT ALL PRIVILEGES ON DATABASE " + m_strDatabase + " to " + m_strUser, false);
		}
	}

	if (m_pDatabase != nullptr) {
		m_pInterface->GetPQfinish()((PGconn*)m_pDatabase);
		m_pDatabase = nullptr;
	}

	m_pDatabase = m_pInterface->GetPQsetdbLogin()(pHost, pPort, pOptions, pTty, databaseBuffer, pUser, pPassword);
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
	// on commit/rollback. Used by ibSessionRegistry::TryProbeRowLock so
	// the probe SELECT fails immediately with a lock-timeout exception
	// rather than blocking.
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

bool ibDatabaseLayerPostgres::TryProbeRowLock(const wxString& tableName,
                                               const wxString& pkColumn,
                                               const wxString& pkValue)
{
	// FOR UPDATE NOWAIT on Postgres fails immediately when the row is
	// locked by another connection — exactly the signal the registry's
	// probe wants. Wrap the SELECT in its own TX so the lock, if taken,
	// dies with the ROLLBACK at the tail.
	try { BeginTransaction({ /*.noWait=*/true }); }
	catch (...) { return false; }

	const wxString sql = wxT("SELECT ") + pkColumn + wxT(" FROM ")
		+ tableName + wxT(" WHERE ") + pkColumn
		+ wxT(" = ? FOR UPDATE NOWAIT");
	ibPreparedStatement* stmt = DoPrepareStatement(sql);
	bool gotLock = false;
	if (stmt) {
		stmt->SetParamString(1, pkValue);
		try {
			ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
			if (rs) {
				if (rs->Next()) gotLock = true;
				rs->Close();
				CloseResultSet(rs);
			}
		}
		catch (...) { gotLock = false; }
		CloseStatement(stmt);
	}
	try { RollBack(); } catch (...) { /* swallowed: TryProbeRowLock always rolls back so no lock survives — failure here means lock is gone anyway */ }
	return gotLock;
}

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
		const wxString& rowsAffected = ConvertFromUnicodeStream(m_pInterface->GetPQcmdTuples()(pResultCode));
		long rows = 1;
		//rowsAffected.ToLong(&rows);
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

