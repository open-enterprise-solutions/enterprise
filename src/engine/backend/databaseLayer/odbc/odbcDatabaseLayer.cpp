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
		SetErrorMessage(wxT("Error loading ODBC library"));
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

	m_strDSN = wxEmptyString;
	m_strUser = wxEmptyString;
	m_strPassword = wxEmptyString;
	m_strConnection = wxEmptyString;

#if wxUSE_GUI
	m_bPrompt = false;
	m_parentContext = nullptr;
#endif
}

ibDatabaseLayerODBC::ibDatabaseLayerODBC(const ibDatabaseLayerODBC& src)
{
	m_bIsConnected = false;
	ResetErrorCodes();

	m_pInterface = new ibInterfaceODBC();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading ODBC library"));
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

	m_strDSN = wxEmptyString;
	m_strUser = wxEmptyString;
	m_strPassword = wxEmptyString;
	m_strConnection = wxEmptyString;

#if wxUSE_GUI
	m_bPrompt = false;
	m_parentContext = nullptr;
#endif
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

	wxDELETE(m_pInterface);
}

bool ibDatabaseLayerODBC::Open()
{
	ResetErrorCodes();

	if (m_pInterface == nullptr)
		return false;

	if (!m_strDSN.IsEmpty())
	{
#ifdef wxUSE_UNICODE
		wxCharBuffer dsnCharBuffer = ConvertToUnicodeStream(m_strDSN);
		wxCharBuffer userCharBuffer = ConvertToUnicodeStream(m_strUser);
		wxCharBuffer passwordCharBuffer = ConvertToUnicodeStream(m_strPassword);
#else
		void* dsnCharBuffer = (void*)m_strDSN.c_str();
		void* userCharBuffer = (void*)m_strUser.c_str();
		void* passwordCharBuffer = (void*)m_strPassword.c_str();
#endif

		SQLRETURN nRet;
		nRet = m_pInterface->GetSQLConnect()((SQLHDBC)m_sqlHDBC, (SQLTCHAR FAR*)(const char*)dsnCharBuffer,
			SQL_NTS, (SQLTCHAR FAR*)(const char*)userCharBuffer, SQL_NTS,
			(SQLTCHAR FAR*)(const char*)passwordCharBuffer, SQL_NTS);
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

		//wxCharBuffer connectionCharBuffer = ConvertToUnicodeStream(m_strConnection);
		void* connectionCharBuffer = (void*)&m_strConnection.c_str();

#if wxUSE_GUI
		SQLRETURN nRet = m_pInterface->GetSQLDriverConnect()((SQLHDBC)m_sqlHDBC, m_parentContext ? (SQLHWND)m_parentContext->GetHandle() : nullptr, (SQLTCHAR*)(const char*)connectionCharBuffer,
			(SQLSMALLINT)m_strConnection.Length(), (SQLTCHAR*)buff, 8192, &iLen, m_bPrompt ? SQL_DRIVER_PROMPT : SQL_DRIVER_NOPROMPT);
#else
		SQLRETURN nRet = m_pInterface->GetSQLDriverConnect()((SQLHDBC)m_sqlHDBC, nullptr, (SQLTCHAR*)(const char*)connectionCharBuffer,
			(SQLSMALLINT)m_strConnection.Length(), (SQLTCHAR*)buff, 8192, &iLen, SQL_DRIVER_NOPROMPT);
#endif

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

bool ibDatabaseLayerODBC::Open(const wxString& strConnection)
{
	m_strDSN = wxEmptyString;
	m_strUser = wxEmptyString;
	m_strPassword = wxEmptyString;
	m_strConnection = strConnection;
#if wxUSE_GUI
	m_bPrompt = false;
	m_parentContext = nullptr;
#endif

	return Open();
}

#if wxUSE_GUI
bool ibDatabaseLayerODBC::Open(const wxString& strConnection, bool bPromptForInfo, wxWindow* parent)
{
	m_strConnection = strConnection;
	m_bPrompt = bPromptForInfo;
	m_parentContext = parent;
	m_strDSN = wxEmptyString;
	m_strUser = wxEmptyString;
	m_strPassword = wxEmptyString;

	return Open();
}
#endif

bool ibDatabaseLayerODBC::Open(const wxString& strDSN, const wxString& strUser, const wxString& strPassword)
{
	m_strDSN = strDSN;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strConnection = wxEmptyString;
#if wxUSE_GUI
	m_bPrompt = false;
	m_parentContext = nullptr;
#endif

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

	// MSSQL's session-level lock-wait is set via `SET LOCK_TIMEOUT 0`.
	// Works for any ODBC-linked MSSQL backend; other DBMSes behind ODBC
	// ignore the statement (some error-out). Wrapped so a failing
	// driver just falls back to default wait-mode.
	if (opts.noWait) {
		try { DoRunQuery(wxT("SET LOCK_TIMEOUT 0"), false); }
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


int ibDatabaseLayerODBC::DoRunQuery(const wxString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();

	//wxPrintf("Running: '%s'\n", strQuery.c_str());
	ibPreparedStatementODBC* pStatement = (ibPreparedStatementODBC*)DoPrepareStatement(strQuery, bParseQuery);

	if (pStatement)
	{
		try
		{
			int nRows = pStatement->RunQuery();
			wxDELETE(pStatement);
			return nRows;
		}
		catch (...)
		{
			SetErrorCode(pStatement->GetErrorCode());
			SetErrorMessage(pStatement->GetErrorMessage());
			wxDELETE(pStatement);
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
	}
	else
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
}

ibDatabaseResultSet* ibDatabaseLayerODBC::DoRunQueryWithResults(const wxString& strQuery)
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
			SetErrorCode(pStatement->GetErrorCode());
			SetErrorMessage(pStatement->GetErrorMessage());
			wxDELETE(pStatement);
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

ibPreparedStatement* ibDatabaseLayerODBC::DoPrepareStatement(const wxString& strQuery)
{
	ibPreparedStatement* pStatement = DoPrepareStatement(strQuery, true);
	LogStatementForCleanup(pStatement);
	return pStatement;
}

ibPreparedStatement* ibDatabaseLayerODBC::DoPrepareStatement(const wxString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();

	wxArrayString QueryArray;
	if (bParseQuery)
		QueryArray = ParseQueries(strQuery);
	else
		QueryArray.push_back(strQuery);

	ibPreparedStatementODBC* pReturnStatement = new ibPreparedStatementODBC(m_pInterface, (SQLHENV)m_sqlEnvHandle, (SQLHDBC)m_sqlHDBC);

	if (pReturnStatement)
		pReturnStatement->SetEncoding(GetEncoding());

	for (unsigned int i = 0; i < (QueryArray.size()); i++)
	{
		//#if wxUSE_UNICODE
		//        void* sqlBuffer = (void*)(QueryArray[i].c_str());
		//#else
		wxCharBuffer sqlBuffer = ConvertToUnicodeStream(QueryArray[i]);
		//#endif
				//wxPrintf(_("Preparing statement: '%s'\n"), sqlBuffer);

		SQLHSTMT pSqlStatement = allocStmth();
		SQLRETURN nRet = m_pInterface->GetSQLPrepare()(pSqlStatement, (SQLTCHAR*)(const char*)sqlBuffer, SQL_NTS);
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

bool ibDatabaseLayerODBC::TableExists(const wxString& table)
{
	bool bReturn = false;
	// Use SQLTables
	SQLHSTMT pStatement = allocStmth();
	wxCharBuffer tableBuffer = ConvertToUnicodeStream(table);
	wxString tableType = wxT("TABLE");
	wxCharBuffer tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)(const char*)tableBuffer, SQL_NTS,
		(SQLTCHAR*)(const char*)tableTypeBuffer, tableTypeBufferLength);

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

bool ibDatabaseLayerODBC::ViewExists(const wxString& view)
{
	bool bReturn = false;
	// Use SQLTables
	SQLHSTMT pStatement = allocStmth();
	wxCharBuffer viewBuffer = ConvertToUnicodeStream(view);
	wxString tableType = wxT("VIEW");
	wxCharBuffer tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)(const char*)viewBuffer, SQL_NTS,
		(SQLTCHAR*)(const char*)tableTypeBuffer, tableTypeBufferLength);

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

wxArrayString ibDatabaseLayerODBC::GetTables()
{
	wxArrayString returnArray;
	SQLHSTMT pStatement = allocStmth();
	wxString tableType = wxT("TABLE");
	wxCharBuffer tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)(const char*)tableTypeBuffer, tableTypeBufferLength);

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
		wxString strTable = ConvertFromUnicodeStream((const char*)buff);
		returnArray.Add(strTable);
		nRet = m_pInterface->GetSQLFetch()(pStatement);
	}

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return returnArray;
}

wxArrayString ibDatabaseLayerODBC::GetViews()
{
	wxArrayString returnArray;
	SQLHSTMT pStatement = allocStmth();
	wxString tableType = wxT("VIEW");
	wxCharBuffer tableTypeBuffer = ConvertToUnicodeStream(tableType);
	int tableTypeBufferLength = GetEncodedStreamLength(tableType);
	SQLRETURN nRet = m_pInterface->GetSQLTables()(pStatement,
		nullptr, 0,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)(const char*)tableTypeBuffer, tableTypeBufferLength);

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
		wxString strView = ConvertFromUnicodeStream((const char*)buff);
		returnArray.Add(strView);
		nRet = m_pInterface->GetSQLFetch()(pStatement);
	}

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return returnArray;
}

wxArrayString ibDatabaseLayerODBC::GetColumns(const wxString& table)
{
	wxArrayString returnArray;
	// Use SQLColumns
	SQLHSTMT pStatement = allocStmth();
	wxCharBuffer tableBuffer = ConvertToUnicodeStream(table);
	int tableBufferLength = GetEncodedStreamLength(table);
	SQLRETURN nRet = m_pInterface->GetSQLColumns()(pStatement,
		nullptr, 0,
		nullptr, 0,
		(SQLTCHAR*)(const char*)tableBuffer, tableBufferLength,
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
		wxString strColumn = ConvertFromUnicodeStream((const char*)buff);
		returnArray.Add(strColumn);
		nRet = m_pInterface->GetSQLFetch()(pStatement);
	}

	m_pInterface->GetSQLFreeStmt()(pStatement, SQL_CLOSE);

	return returnArray;
}

//void ibDatabaseLayerODBC::InterpretErrorCodes( long nCode, SQLHSTMT stmth_ptr )
void ibDatabaseLayerODBC::InterpretErrorCodes(long nCode, void* stmth_ptr)
{
	wxLogDebug(wxT("ibDatabaseLayerODBC::InterpretErrorCodes()\n"));

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

		SetErrorCode((int)iNativeCode);
		//SetErrorMessage(wxString((wxChar*)strBuffer));
		SetErrorMessage(ConvertFromUnicodeStream((char*)strBuffer));
		// Stash the 5-char SQLSTATE for ClassifyDatabaseError + the
		// exception's GetSqlState() — without this the override
		// returns empty and class-digit dispatch can't fire.
		SetLastSqlState(ConvertFromUnicodeStream((char*)strState));
	}
}

ibBackendDatabaseException::Kind ibDatabaseLayerODBC::ClassifyDatabaseError(int nativeCode) const
{
	// Same SQLSTATE class-digit dispatch as PostgreSQL — ODBC standardises
	// on the SQL-spec SQLSTATE format, so the classification is portable.
	// See https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/appendix-a-odbc-error-codes
	using Kind = ibBackendDatabaseException::Kind;
	if (m_lastSqlState.length() >= 2) {
		const wxString cls = m_lastSqlState.Left(2);
		if (cls == wxT("08")) return Kind::ConnectionLost;
		if (cls == wxT("23")) return Kind::Constraint;
		if (cls == wxT("42")) return Kind::Syntax;
		if (cls == wxT("57")) return Kind::ConnectionLost;
		if (cls == wxT("HY")) {
			// HYT00 = ODBC timeout expired; HYT01 = connection timeout
			if (m_lastSqlState == wxT("HYT00") || m_lastSqlState == wxT("HYT01"))
				return Kind::Timeout;
		}
		if (cls == wxT("40")) {
			// MSSQL surfaces deadlock as native error 1205 with SQLSTATE
			// 40001 (serialization failure) — we treat both as Deadlock
			// since the engine already rolled the TX back.
			if (m_lastSqlState == wxT("40001") || m_lastSqlState == wxT("40P01"))
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
	bool bAvailable = false;
	ibInterfaceODBC* pInterface = new ibInterfaceODBC();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

