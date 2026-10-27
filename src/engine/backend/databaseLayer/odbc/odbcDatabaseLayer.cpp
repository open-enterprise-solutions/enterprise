#include "odbcDatabaseLayer.h"
#include "odbcInterface.h"
#include "odbcPreparedStatement.h"
#include "odbcResultSet.h"

#include "backend/databaseLayer/databaseErrorCodes.h"

// ctor()
ibDatabaseLayerODBC::ibDatabaseLayerODBC()
	: ibDatabaseLayer()
{
	m_bIsConnected = false;
	ResetErrorCodes();

	m_pInterface = new ibInterfaceODBC();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading ODBC library"));
		ThrowDatabaseException();
		return;
	}

	SQLHENV sqlEnvHandle = (SQLHENV)m_sqlEnvHandle;
	SQLRETURN nRet = m_pInterface->GetSQLAllocHandle()(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &sqlEnvHandle);
	m_sqlEnvHandle = sqlEnvHandle;
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	nRet = m_pInterface->GetSQLSetEnvAttr()((SQLHENV)m_sqlEnvHandle, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	SQLHDBC sqlHDBC = (SQLHDBC)m_sqlHDBC;
	nRet = m_pInterface->GetSQLAllocHandle()(SQL_HANDLE_DBC, (SQLHENV)m_sqlEnvHandle, &sqlHDBC);
	m_sqlHDBC = sqlHDBC;
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	m_strDSN = ibEmptyString;
	m_strUser = ibEmptyString;
	m_strPassword = ibEmptyString;
	m_strConnection = ibEmptyString;

}

ibDatabaseLayerODBC::ibDatabaseLayerODBC(const ibDatabaseLayerODBC& src)
{
	m_bIsConnected = false;
	ResetErrorCodes();

	m_pInterface = new ibInterfaceODBC();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(ibT("Error loading ODBC library"));
		ThrowDatabaseException();
		return;
	}

	SQLHENV sqlEnvHandle = (SQLHENV)m_sqlEnvHandle;
	SQLRETURN nRet = m_pInterface->GetSQLAllocHandle()(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &sqlEnvHandle);
	m_sqlEnvHandle = sqlEnvHandle;
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	nRet = m_pInterface->GetSQLSetEnvAttr()((SQLHENV)m_sqlEnvHandle, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	SQLHDBC sqlHDBC = (SQLHDBC)m_sqlHDBC;
	nRet = m_pInterface->GetSQLAllocHandle()(SQL_HANDLE_DBC, (SQLHENV)m_sqlEnvHandle, &sqlHDBC);
	m_sqlHDBC = sqlHDBC;
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	m_strDSN = ibEmptyString;
	m_strUser = ibEmptyString;
	m_strPassword = ibEmptyString;
	m_strConnection = ibEmptyString;

}

ibDatabaseLayerODBC::~ibDatabaseLayerODBC()
{
	Close();

	SQLRETURN nRet = m_pInterface->GetSQLFreeHandle()(SQL_HANDLE_DBC, (SQLHDBC)m_sqlHDBC);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	nRet = m_pInterface->GetSQLFreeHandle()(SQL_HANDLE_ENV, (SQLHENV)m_sqlEnvHandle);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	delete m_pInterface;
}

bool ibDatabaseLayerODBC::Open()
{
	ResetErrorCodes();

	if (m_pInterface == nullptr)
		return false;

	if (!m_strDSN.IsEmpty())
	{
		std::string dsnCharBuffer = ConvertToUnicodeStream(m_strDSN);
		std::string userCharBuffer = ConvertToUnicodeStream(m_strUser);
		std::string passwordCharBuffer = ConvertToUnicodeStream(m_strPassword);

		SQLRETURN nRet;
		nRet = m_pInterface->GetSQLConnect()((SQLHDBC)m_sqlHDBC, (SQLTCHAR FAR*)dsnCharBuffer.c_str(),
			SQL_NTS, (SQLTCHAR FAR*)userCharBuffer.c_str(), SQL_NTS,
			(SQLTCHAR FAR*)passwordCharBuffer.c_str(), SQL_NTS);
		if (nRet != SQL_SUCCESS && nRet != SQL_SUCCESS_WITH_INFO)
		{
			InterpretErrorCodes(nRet);
			ThrowDatabaseException();
		}
	}
	else if (!m_strConnection.IsEmpty())
	{
		SQLTCHAR buff[8192];
		SQLSMALLINT iLen;

		memset(buff, 0, 8192 * sizeof(SQLTCHAR));

		// The text in the same bytes as the DSN's above. This passed the ADDRESS of the temporary c_str() returns —
		// the driver was handed a pointer's bytes for a connection string; strict C++20 refused to take it.
		const std::string connectionCharBuffer = ConvertToUnicodeStream(m_strConnection);

		SQLRETURN nRet = m_pInterface->GetSQLDriverConnect()((SQLHDBC)m_sqlHDBC, nullptr, (SQLTCHAR*)connectionCharBuffer.c_str(),
			SQL_NTS, (SQLTCHAR*)buff, 8192, &iLen, SQL_DRIVER_NOPROMPT);

		if (nRet != SQL_SUCCESS && nRet != SQL_SUCCESS_WITH_INFO)
		{
			InterpretErrorCodes(nRet);
			ThrowDatabaseException();
		}
	}
	else
	{
		return false;
	}

	m_bIsConnected = true;

	return true;
}

bool ibDatabaseLayerODBC::Open(const ibString& strConnection)
{
	m_strDSN = ibEmptyString;
	m_strUser = ibEmptyString;
	m_strPassword = ibEmptyString;
	m_strConnection = strConnection;

	return Open();
}

bool ibDatabaseLayerODBC::Open(const ibString& strDSN, const ibString& strUser, const ibString& strPassword)
{
	m_strDSN = strDSN;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strConnection = ibEmptyString;

	return Open();
}

bool ibDatabaseLayerODBC::Close()
{
	ResetErrorCodes();

	CloseResultSets();
	CloseStatements();

	if (m_bIsConnected)
	{
		SQLRETURN nRet = m_pInterface->GetSQLDisconnect()((SQLHDBC)m_sqlHDBC);
		if (nRet != SQL_SUCCESS)
		{
			InterpretErrorCodes(nRet);
			ThrowDatabaseException();
		}

		m_bIsConnected = false;
	}

	return true;
}

bool ibDatabaseLayerODBC::IsOpen()
{
	return m_bIsConnected;
}

void ibDatabaseLayerODBC::DoBeginTransaction(const ibTxOptions& opts)
{
	ResetErrorCodes();

	SQLRETURN nRet = m_pInterface->GetSQLSetConnectAttr()((SQLHDBC)m_sqlHDBC, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	// ONE COMMITTED STATE FOR THE WHOLE TRANSACTION — see ibDbTxOptions::snapshot. Set through the
	// ODBC attribute rather than a dialect statement, so it holds for whatever backend is behind the
	// driver instead of only the ones that speak the MSSQL spelling. Best-effort like the lock knob
	// below: a driver that refuses the level leaves the transaction at its default, which is the
	// behaviour there was before the flag existed.
	if (opts.snapshot) {
		m_pInterface->GetSQLSetConnectAttr()((SQLHDBC)m_sqlHDBC, SQL_ATTR_TXN_ISOLATION,
			(SQLPOINTER)SQL_TXN_REPEATABLE_READ, 0);
	}

	// MSSQL's session-level lock-wait is set via `SET LOCK_TIMEOUT 0`.
	// Works for any ODBC-linked MSSQL backend; other DBMSes behind ODBC
	// ignore the statement (some error-out). Wrapped so a failing
	// driver just falls back to default wait-mode.
	if (opts.noWait) {
		try { DoRunQuery(ibT("SET LOCK_TIMEOUT 0"), false); }
		catch (...) { /* best-effort — MSSQL: ok; other DBMSes: no-op */ }
	}
}

void ibDatabaseLayerODBC::DoCommit()
{
	ResetErrorCodes();

	SQLRETURN nRet = m_pInterface->GetSQLEndTran()(SQL_HANDLE_DBC, (SQLHDBC)m_sqlHDBC, SQL_COMMIT);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	nRet = m_pInterface->GetSQLSetConnectAttr()((SQLHDBC)m_sqlHDBC, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_IS_INTEGER);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}
}

void ibDatabaseLayerODBC::DoRollBack()
{
	ResetErrorCodes();

	SQLRETURN nRet = m_pInterface->GetSQLEndTran()(SQL_HANDLE_DBC, (SQLHDBC)m_sqlHDBC, SQL_ROLLBACK);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}

	nRet = m_pInterface->GetSQLSetConnectAttr()((SQLHDBC)m_sqlHDBC, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_IS_INTEGER);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}
}

// IsActiveTransaction inherits the base-class default (m_txDepth > 0).


int ibDatabaseLayerODBC::DoRunQuery(const ibString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();

	ibPreparedStatementODBC* pStatement = (ibPreparedStatementODBC*)DoPrepareStatement(strQuery, bParseQuery);

	if (pStatement)
	{
		try
		{
			int nRows = pStatement->RunQuery();
			delete pStatement;
			return nRows;
		}
		catch (...)
		{
			SetErrorCode(pStatement->GetErrorCode(), pStatement->GetErrorKind());
			SetErrorMessage(pStatement->GetErrorMessage());
			delete pStatement;
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
	}
	else
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
}

ibDatabaseResultSet* ibDatabaseLayerODBC::DoRunQueryWithResults(const ibString& strQuery)
{
	ResetErrorCodes();

	ibPreparedStatementODBC* pStatement = (ibPreparedStatementODBC*)DoPrepareStatement(strQuery, true);

	if (pStatement)
	{
		try
		{
			pStatement->SetOneTimer(true);
			ibDatabaseResultSet* pResults = pStatement->RunQueryWithResults(false /*false for "Don't log this result set for cleanup*/);
			LogResultSetForCleanup(pResults);
			return pResults;
		}
		catch (...)
		{
			SetErrorCode(pStatement->GetErrorCode(), pStatement->GetErrorKind());
			SetErrorMessage(pStatement->GetErrorMessage());
			delete pStatement;
			ThrowDatabaseException();
			return nullptr;
		}
	}
	else
		return nullptr;
}

void* ibDatabaseLayerODBC::allocStmth()
{
	ResetErrorCodes();

	SQLHANDLE handle = nullptr;

	SQLRETURN nRet = m_pInterface->GetSQLAllocHandle()(SQL_HANDLE_STMT, (SQLHDBC)m_sqlHDBC, &handle);
	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		ThrowDatabaseException();
	}
	return handle;
}

ibPreparedStatement* ibDatabaseLayerODBC::DoPrepareStatement(const ibString& strQuery)
{
	ibPreparedStatement* pStatement = DoPrepareStatement(strQuery, true);
	LogStatementForCleanup(pStatement);
	return pStatement;
}

ibPreparedStatement* ibDatabaseLayerODBC::DoPrepareStatement(const ibString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();

	std::vector<ibString> QueryArray;
	if (bParseQuery)
		QueryArray = ParseQueries(strQuery);
	else
		QueryArray.push_back(strQuery);

	ibPreparedStatementODBC* pReturnStatement = new ibPreparedStatementODBC(m_pInterface, (SQLHENV)m_sqlEnvHandle, (SQLHDBC)m_sqlHDBC, &m_executing);

	for (unsigned int i = 0; i < (QueryArray.size()); i++)
	{
		std::string sqlBuffer = ConvertToUnicodeStream(QueryArray[i]);

		SQLHSTMT pSqlStatement = allocStmth();
		SQLRETURN nRet = m_pInterface->GetSQLPrepare()(pSqlStatement, (SQLTCHAR*)sqlBuffer.c_str(), SQL_NTS);
		if (nRet != SQL_SUCCESS && nRet != SQL_SUCCESS_WITH_INFO)
		{
			InterpretErrorCodes(nRet);
			m_pInterface->GetSQLFreeStmt()(pSqlStatement, SQL_CLOSE);
			ThrowDatabaseException();
			return nullptr;
		}

		if (pSqlStatement)
			pReturnStatement->AddPreparedStatement(pSqlStatement);
	}

	return pReturnStatement;
}

bool ibDatabaseLayerODBC::TableExists(const ibString& table)
{
	bool bReturn = false;
	// Use SQLTables
	SQLHSTMT pStatement = allocStmth();
	std::string tableBuffer = ConvertToUnicodeStream(table);
	ibString tableType = ibT("TABLE");
	std::string tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)tableBuffer.c_str(), SQL_NTS,
		(SQLTCHAR*)tableTypeBuffer.c_str(), tableTypeBufferLength);

	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
		ThrowDatabaseException();
		return false;
	}

	nRet = m_pInterface->GetSQLFetch()(pStatement);
	if (nRet == SQL_SUCCESS || nRet == SQL_SUCCESS_WITH_INFO)
		bReturn = true;

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return bReturn;
}

bool ibDatabaseLayerODBC::ViewExists(const ibString& view)
{
	bool bReturn = false;
	// Use SQLTables
	SQLHSTMT pStatement = allocStmth();
	std::string viewBuffer = ConvertToUnicodeStream(view);
	ibString tableType = ibT("VIEW");
	std::string tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)viewBuffer.c_str(), SQL_NTS,
		(SQLTCHAR*)tableTypeBuffer.c_str(), tableTypeBufferLength);

	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
		ThrowDatabaseException();
		return false;
	}

	nRet = m_pInterface->GetSQLFetch()(pStatement);
	if (nRet == SQL_SUCCESS || nRet == SQL_SUCCESS_WITH_INFO)
		bReturn = true;

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return bReturn;
}

std::vector<ibString> ibDatabaseLayerODBC::GetTables()
{
	std::vector<ibString> returnArray;
	SQLHSTMT pStatement = allocStmth();
	ibString tableType = ibT("TABLE");
	std::string tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)tableTypeBuffer.c_str(), tableTypeBufferLength);

	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
		ThrowDatabaseException();
		return returnArray;
	}

	nRet = m_pInterface->GetSQLFetch()(pStatement);
	while (nRet == SQL_SUCCESS || nRet == SQL_SUCCESS_WITH_INFO)
	{
		SQLTCHAR buff[8192];

		memset(buff, 0, 8192 * sizeof(SQLTCHAR));
#ifdef _WIN64
		SQLLEN  col_size = 8192;
		SQLLEN  real_size = 0;
#else
		SQLINTEGER  col_size = 8192;
		SQLINTEGER  real_size = 0;
#endif // _WIN64
		int nField = 3;

		SQLRETURN nGetDataReturn = m_pInterface->GetSQLGetData()(pStatement, nField, SQL_C_CHAR, buff,
			col_size, &real_size);
		if (nGetDataReturn != SQL_SUCCESS && nGetDataReturn != SQL_SUCCESS_WITH_INFO)
		{
			InterpretErrorCodes(nRet);
			m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
			ThrowDatabaseException();
			return returnArray;
		}
		ibString strTable = ConvertFromUnicodeStream((const char*)buff);
		returnArray.push_back(strTable);
		nRet = m_pInterface->GetSQLFetch()(pStatement);
	}

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return returnArray;
}

std::vector<ibString> ibDatabaseLayerODBC::GetViews()
{
	std::vector<ibString> returnArray;
	SQLHSTMT pStatement = allocStmth();
	ibString tableType = ibT("VIEW");
	std::string tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)tableTypeBuffer.c_str(), tableTypeBufferLength);

	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
		ThrowDatabaseException();
		return returnArray;
	}

	nRet = m_pInterface->GetSQLFetch()(pStatement);
	while (nRet == SQL_SUCCESS || nRet == SQL_SUCCESS_WITH_INFO)
	{
		SQLTCHAR buff[8192];

		memset(buff, 0, 8192 * sizeof(SQLTCHAR));

#ifdef _WIN64
		SQLLEN  col_size = 8192;
		SQLLEN  real_size = 0;
#else
		SQLINTEGER  col_size = 8192;
		SQLINTEGER  real_size = 0;
#endif // _WIN64
		int nField = 3;

		SQLRETURN nGetDataReturn = m_pInterface->GetSQLGetData()(pStatement, nField, SQL_C_CHAR, buff,
			col_size, &real_size);
		if (nGetDataReturn != SQL_SUCCESS && nGetDataReturn != SQL_SUCCESS_WITH_INFO)
		{
			InterpretErrorCodes(nRet);
			m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
			ThrowDatabaseException();
			return returnArray;
		}
		ibString strView = ConvertFromUnicodeStream((const char*)buff);
		returnArray.push_back(strView);
		nRet = m_pInterface->GetSQLFetch()(pStatement);
	}

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return returnArray;
}

std::vector<ibString> ibDatabaseLayerODBC::GetColumns(const ibString& table)
{
	std::vector<ibString> returnArray;
	// Use SQLColumns
	SQLHSTMT pStatement = allocStmth();
	std::string tableBuffer = ConvertToUnicodeStream(table);
	int tableBufferLength = GetEncodedStreamLength(table);
	SQLRETURN nRet = m_pInterface->GetSQLColumns()(pStatement,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)tableBuffer.c_str(), tableBufferLength,
		nullptr, 0);

	if (nRet != SQL_SUCCESS)
	{
		InterpretErrorCodes(nRet);
		m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
		ThrowDatabaseException();
		return returnArray;
	}

	nRet = m_pInterface->GetSQLFetch()(pStatement);
	while (nRet == SQL_SUCCESS || nRet == SQL_SUCCESS_WITH_INFO)
	{
		SQLPOINTER buff[8192];

		memset(buff, 0, 8192 * sizeof(SQLTCHAR));

#ifdef _WIN64
		SQLLEN  col_size = 8192;
		SQLLEN  real_size = 0;
#else
		SQLINTEGER  col_size = 8192;
		SQLINTEGER  real_size = 0;
#endif // _WIN64
		int nField = 4;

		SQLRETURN nGetDataReturn = m_pInterface->GetSQLGetData()(pStatement, nField, SQL_C_CHAR, buff,
			col_size, &real_size);
		if (nGetDataReturn != SQL_SUCCESS && nGetDataReturn != SQL_SUCCESS_WITH_INFO)
		{
			InterpretErrorCodes(nRet);
			m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);
			ThrowDatabaseException();
			return returnArray;
		}
		ibString strColumn = ConvertFromUnicodeStream((const char*)buff);
		returnArray.push_back(strColumn);
		nRet = m_pInterface->GetSQLFetch()(pStatement);
	}

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return returnArray;
}

//void ibDatabaseLayerODBC::InterpretErrorCodes( long nCode, SQLHSTMT stmth_ptr )
void ibDatabaseLayerODBC::InterpretErrorCodes(long nCode, void* stmth_ptr)
{
	ibJournalInfo(ibT("db.odbc"),ibT("ibDatabaseLayerODBC::InterpretErrorCodes()\n"));

	//if ((nCode != SQL_SUCCESS) ) // && (nCode != SQL_SUCCESS_WITH_INFO))
	{
		SQLINTEGER iNativeCode;
		SQLTCHAR strState[ERR_STATE_LEN];
		SQLTCHAR strBuffer[ERR_BUFFER_LEN];
		SQLSMALLINT iMsgLen;

		memset(strState, 0, ERR_STATE_LEN * sizeof(SQLTCHAR));
		memset(strBuffer, 0, ERR_BUFFER_LEN * sizeof(SQLTCHAR));

		if (stmth_ptr)
			m_pInterface->GetSQLGetDiagRec()(SQL_HANDLE_STMT, (SQLHSTMT)stmth_ptr, 1, strState, &iNativeCode,
				strBuffer, ERR_BUFFER_LEN, &iMsgLen);
		else
			m_pInterface->GetSQLGetDiagRec()(SQL_HANDLE_DBC, (SQLHDBC)m_sqlHDBC, 1, strState, &iNativeCode,
				strBuffer, ERR_BUFFER_LEN, &iMsgLen);

		const ibString strSqlState = ConvertFromUnicodeStream((char*)strState);
		SetErrorCode(TranslateErrorCode((int)iNativeCode, strSqlState), ClassifySqlState(strSqlState, (int)iNativeCode));
		SetErrorMessage(ConvertFromUnicodeStream((char*)strBuffer));
		// Stash the 5-char SQLSTATE for the exception's GetSqlState().
		SetLastSqlState(strSqlState);
	}
}

int ibDatabaseLayerODBC::TranslateErrorCode(int nNativeCode, const ibString& strSqlState)
{
	// An interrupted statement (Cancel -> SQLCancel) is the cancel, recorded as the platform's.
	if (strSqlState == ibT("HY008"))   // operation canceled
		return DATABASE_LAYER_QUERY_CANCELLED;
	return nNativeCode;
}

// The one call ODBC takes from another thread on a busy connection — on the statement handle that is executing:
// SQLExecute returns HY008 to its own caller. Nothing executing, nothing to stop.
void ibDatabaseLayerODBC::Cancel()
{
	void* const hstmt = m_executing.load();
	if (hstmt != nullptr)
		m_pInterface->GetSQLCancel()((SQLHSTMT)hstmt);
}

ibBackendDatabaseException::Kind ibDatabaseLayerODBC::ClassifySqlState(const ibString& sqlState, int nativeCode)
{
	// Same SQLSTATE class-digit dispatch as PostgreSQL — ODBC standardises
	// on the SQL-spec SQLSTATE format, so the classification is portable.
	// See https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/appendix-a-odbc-error-codes
	using Kind = ibBackendDatabaseException::Kind;
	if (sqlState.length() >= 2) {
		const ibString cls = sqlState.Left(2);
		if (cls == ibT("08")) return Kind::ConnectionLost;
		if (cls == ibT("23")) return Kind::Constraint;
		if (cls == ibT("42")) return Kind::Syntax;
		if (cls == ibT("57")) return Kind::ConnectionLost;
		if (cls == ibT("HY")) {
			// HYT00 = ODBC timeout expired; HYT01 = connection timeout
			if (sqlState == ibT("HYT00") || sqlState == ibT("HYT01"))
				return Kind::Timeout;
		}
		if (cls == ibT("40")) {
			// MSSQL surfaces deadlock as native error 1205 with SQLSTATE
			// 40001 (serialization failure) — we treat both as Deadlock
			// since the engine already rolled the TX back.
			if (sqlState == ibT("40001") || sqlState == ibT("40P01"))
				return Kind::Deadlock;
			return Kind::Timeout;
		}
	}

	// MSSQL native code fallback — deadlock victim signal arrives even
	// when SQLSTATE 40001 isn't surfaced by the driver.
	if (nativeCode == 1205) return Kind::Deadlock;
	return Kind::Unknown;
}

bool ibDatabaseLayerODBC::IsAvailable()
{
	ibInterfaceODBC library;
	return library.Init();
}

