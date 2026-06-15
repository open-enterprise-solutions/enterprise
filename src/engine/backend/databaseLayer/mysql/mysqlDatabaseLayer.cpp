#include "mysqlDatabaseLayer.h"

#include "engine/mysql.h"

#include "mysqlInterface.h"
#include "mysqlPreparedStatement.h"
#include "mysqlPreparedStatementResultSet.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <wx/tokenzr.h>

// MySQL SQL dialect — owned by the driver (static definition + virtual access).
const ibDialectDictionary& ibDatabaseLayerMySQL::Dialect()
{
	static const ibDialectDictionary s_dialect = [] {
		ibDialectDictionary d;
		d.m_paramStyle  = ibParamStyle::QuestionMark;
		d.m_pagination  = ibPagination::LimitOffset;
		d.m_boolForm    = ibBoolForm::OneZero;
		d.m_dropIndexNeedsTable = true;               // DROP INDEX <name> ON <table>
		d.m_alterColumnTemplate = wxT("ALTER TABLE {table} MODIFY COLUMN {column} {type}");
		// ON DUPLICATE KEY UPDATE c = VALUES(c)
		d.m_upsertTemplate   = wxT("INSERT INTO {table} ({columns}) VALUES ({values}) ON DUPLICATE KEY UPDATE {update}");
		d.m_upsertUpdateItem = wxT("{col} = VALUES({col})");
		d.m_features.m_window = true;                 // MySQL 8+
		d.m_features.m_rollup = true;                 // GROUP BY <keys> WITH ROLLUP (MySQL spelling)
		d.m_rollupPrefix      = wxEmptyString;
		d.m_rollupSuffix      = wxT(" WITH ROLLUP");
		// type map
		d.m_typeBoolean = wxT("TINYINT");
		d.m_typeInteger = wxT("INT");
		d.m_typeDate    = wxT("DATETIME");
		d.m_typeBlob    = wxT("LONGBLOB");
		d.m_typeGuid    = wxT("CHAR(36)");
		return d;
	}();
	return s_dialect;
}

const ibDialectDictionary& ibDatabaseLayerMySQL::GetDialect() const
{
	return Dialect();
}

// ctor
ibDatabaseLayerMySQL::ibDatabaseLayerMySQL()
	: ibDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceMySQL();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	m_strServer = wxT("localhost");
	m_iPort = 3306; // default
	m_strDatabase = wxEmptyString;
	m_strUser = wxEmptyString;
	m_strPassword = wxEmptyString;
}

ibDatabaseLayerMySQL::ibDatabaseLayerMySQL(const wxString& strDatabase)
	: ibDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceMySQL();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	m_strServer = wxT("localhost");
	m_iPort = 3306; // default
	m_strUser = wxEmptyString;
	m_strPassword = wxEmptyString;
	Open(strDatabase);
}

ibDatabaseLayerMySQL::ibDatabaseLayerMySQL(const wxString& strServer, const wxString& strDatabase)
	: ibDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceMySQL();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	ParseServerAndPort(strServer);
	m_strUser = wxEmptyString;
	m_strPassword = wxEmptyString;
	Open(strDatabase);
}

ibDatabaseLayerMySQL::ibDatabaseLayerMySQL(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: ibDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceMySQL();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	m_strServer = wxT("localhost");
	m_iPort = 3306; // default
	m_strUser = strUser;
	m_strPassword = strPassword;
	Open(strDatabase);
}

ibDatabaseLayerMySQL::ibDatabaseLayerMySQL(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: ibDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceMySQL();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library5"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	ParseServerAndPort(strServer);
	m_strUser = strUser;
	m_strPassword = strPassword;
	Open(strDatabase);
}

ibDatabaseLayerMySQL::ibDatabaseLayerMySQL(const ibDatabaseLayerMySQL& src)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new ibInterfaceMySQL();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	m_strServer = src.m_strServer;
	m_iPort = src.m_iPort; // default
	m_strUser = src.m_strUser;
	m_strPassword = src.m_strPassword;
	Open(src.m_strDatabase);
}

// dtor
ibDatabaseLayerMySQL::~ibDatabaseLayerMySQL()
{
	Close();
	//m_pInterface->GetMysqlClose()(m_pDatabase);
	//delete m_pDatabase;
	//m_pInterface->GetMysqlLibraryEnd()();
	m_pInterface->GetMysqlServerEnd()();
	wxDELETE(m_pInterface);
}

// open database
void ibDatabaseLayerMySQL::InitDatabase()
{
	//char *server_options[] = { "mysql_test", "--defaults-file=my.cnf" };
	//int num_elements = sizeof(server_options)/ sizeof(char *);

	//char *server_groups[] = { "libmysqld_server", "libmysqld_client" };
	//m_pInterface->GetMysqlServerInit()(num_elements, server_options, server_groups);
	//m_pDatabase = new MYSQL();
	//m_pInterface->GetMysqlLibraryInit()();
	//m_pInterface->GetMysqlInit()(m_pDatabase);
  //  m_pInterface->GetMysqlServerInit()( 0, nullptr, nullptr );
	m_pDatabase = m_pInterface->GetMysqlInit()(nullptr);
}

// open database
bool ibDatabaseLayerMySQL::Open(const wxString& strServer, const wxString& strDatabase)
{
	ParseServerAndPort(strServer);
	return Open(strDatabase);
}

bool ibDatabaseLayerMySQL::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strUser = strUser;
	m_strPassword = strPassword;
	return Open(strDatabase);
}

bool ibDatabaseLayerMySQL::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	ParseServerAndPort(strServer);
	m_strUser = strUser;
	m_strPassword = strPassword;
	return Open(strDatabase);
}

bool ibDatabaseLayerMySQL::Open(const wxString& strDatabase)
{
	if (m_pInterface == nullptr)
		return false;

	m_strDatabase = strDatabase;

	wxCharBuffer serverCharBuffer = ConvertToUnicodeStream(m_strServer);
	wxCharBuffer userCharBuffer = ConvertToUnicodeStream(m_strUser);
	wxCharBuffer passwordCharBuffer = ConvertToUnicodeStream(m_strPassword);
	wxCharBuffer databaseNameCharBuffer = ConvertToUnicodeStream(m_strDatabase);
	long connectFlags = 0;
#if MYSQL_VERSION_ID >= 40100 
#if !defined ulong 
#define ulong unsigned long 
#endif 
	connectFlags |= CLIENT_MULTI_RESULTS;
	connectFlags |= CLIENT_MULTI_STATEMENTS;
#endif 
	if (m_pInterface->GetMysqlRealConnect()((MYSQL*)m_pDatabase, serverCharBuffer, userCharBuffer, passwordCharBuffer, databaseNameCharBuffer, m_iPort, nullptr/*socket*/, connectFlags) != nullptr)
	{
#if wxUSE_UNICODE
		const char* sqlStatement = "SET CHARACTER_SET_CLIENT=utf8, "
			"CHARACTER_SET_CONNECTION=utf8, "
			"CHARACTER_SET_RESULTS=utf8;";

		m_pInterface->GetMysqlRealQuery()((MYSQL*)m_pDatabase, sqlStatement, strlen(sqlStatement));
		wxCSConv conv(wxT("UTF-8"));
		SetEncoding(&conv);
#endif

		return true;
	}
	else
	{
		SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}
}

void ibDatabaseLayerMySQL::ParseServerAndPort(const wxString& strServer)
{
	int portIndicator = strServer.Find(wxT(":"));
	if (portIndicator > -1)
	{
		m_strServer = strServer.SubString(0, portIndicator - 1);
		m_iPort = wxAtoi(strServer.SubString(portIndicator + 1, strServer.Length() - 1));
	}
	else
	{
		m_strServer = strServer;
		m_iPort = 3306; // default
	}
}

// close database
bool ibDatabaseLayerMySQL::Close()
{
	CloseResultSets();
	CloseStatements();

	ResetErrorCodes();
	if (m_pDatabase)
	{
		m_pInterface->GetMysqlClose()((MYSQL*)m_pDatabase);
		m_pDatabase = nullptr;
	}
	//  m_pInterface->GetMysqlServerEnd()();
	  //delete m_pDatabase;
	  //m_pDatabase = nullptr;
	return true;
}


bool ibDatabaseLayerMySQL::IsOpen()
{
	return (m_pDatabase != nullptr);
}

// transaction support
void ibDatabaseLayerMySQL::DoBeginTransaction(const ibTxOptions& opts)
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetMysqlAutoCommit()((MYSQL*)m_pDatabase, 0);
	if (nReturn != 0)
	{
		SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}

	// InnoDB's NOWAIT is either per-statement (`SELECT ... FOR UPDATE NOWAIT`,
	// MySQL 8+) or session-level via `innodb_lock_wait_timeout`. Setting
	// session-level here scopes nowait to this connection; registry uses
	// a dedicated probe connection so this doesn't leak to other work.
	if (opts.noWait) {
		try { DoRunQuery(wxT("SET SESSION innodb_lock_wait_timeout = 1"), false); }
		catch (...) { /* best-effort */ }
	}
}

void ibDatabaseLayerMySQL::DoCommit()
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetMysqlCommit()((MYSQL*)m_pDatabase);
	if (nReturn != 0)
	{
		SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
	nReturn = m_pInterface->GetMysqlAutoCommit()((MYSQL*)m_pDatabase, 1);
	if (nReturn != 0)
	{
		SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
}

void ibDatabaseLayerMySQL::DoRollBack()
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetMysqlRollback()((MYSQL*)m_pDatabase);
	if (nReturn != 0)
	{
		SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
	nReturn = m_pInterface->GetMysqlAutoCommit()((MYSQL*)m_pDatabase, 1);
	if (nReturn != 0)
	{
		SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
}

// IsActiveTransaction inherits the base-class default (m_txDepth > 0).


// query database
int ibDatabaseLayerMySQL::DoRunQuery(const wxString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();

	wxArrayString QueryArray;
	if (bParseQuery)
		QueryArray = ParseQueries(strQuery);
	else
		QueryArray.push_back(strQuery);

	wxArrayString::iterator start = QueryArray.begin();
	wxArrayString::iterator stop = QueryArray.end();

	while (start != stop)
	{
		wxCharBuffer sqlBuffer = ConvertToUnicodeStream(*start);
		//puts(sqlBuffer);
		int nReturn = m_pInterface->GetMysqlQuery()((MYSQL*)m_pDatabase, sqlBuffer);
		if (nReturn != 0)
		{
			SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		start++;
	}
	return m_pInterface->GetMysqlAffectedRows()((MYSQL*)m_pDatabase);
}

ibDatabaseResultSet* ibDatabaseLayerMySQL::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();

	wxArrayString QueryArray = ParseQueries(strQuery);

	int nArraySize = QueryArray.size();
	ibDatabaseResultSetMySQL* pResultSet = nullptr;
	for (int i = 0; i < nArraySize; i++)
	{
		wxString strCurrentQuery = QueryArray[i];
		MYSQL_STMT* pMysqlStatement = m_pInterface->GetMysqlStmtInit()((MYSQL*)m_pDatabase);
		if (pMysqlStatement != nullptr)
		{
			wxCharBuffer sqlBuffer = ConvertToUnicodeStream(strCurrentQuery);
			//puts(sqlBuffer);
			wxString sqlUTF8((const char*)sqlBuffer, wxConvUTF8);
			if (m_pInterface->GetMysqlStmtPrepare()(pMysqlStatement, sqlBuffer, sqlUTF8.Length()) == 0)
			{
				int nReturn = m_pInterface->GetMysqlStmtExecute()(pMysqlStatement);
				if (nReturn != 0)
				{
					SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlStmtErrno()(pMysqlStatement)));
					SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlStmtError()(pMysqlStatement)));

					// Clean up after ourselves
					m_pInterface->GetMysqlStmtFreeResult()(pMysqlStatement);
					m_pInterface->GetMysqlStmtClose()(pMysqlStatement);

					ThrowDatabaseException();
					return nullptr;
				}
			}
			else
			{
				SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
				SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
				ThrowDatabaseException();
			}
			if (i == nArraySize - 1)
			{
				pResultSet = new ibDatabaseResultSetMySQL(m_pInterface, pMysqlStatement, true);
				if (pResultSet)
					pResultSet->SetEncoding(GetEncoding());
#if wxUSE_UNICODE
				//wxPrintf(_("Allocating statement at %d\n"), pMysqlStatement);
			   // m_ResultSets[pResultSet] = pMysqlStatement;
#endif
				LogResultSetForCleanup(pResultSet);
				return pResultSet;
			}

			m_pInterface->GetMysqlStmtFreeResult()(pMysqlStatement);
			m_pInterface->GetMysqlStmtClose()(pMysqlStatement);
		}
		else
		{
			SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
			ThrowDatabaseException();
			return nullptr;
		}
	}
	LogResultSetForCleanup(pResultSet);
	return pResultSet;
}

// ibPreparedStatement support
ibPreparedStatement* ibDatabaseLayerMySQL::DoPrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();

	wxArrayString QueryArray = ParseQueries(strQuery);

	wxArrayString::iterator start = QueryArray.begin();
	wxArrayString::iterator stop = QueryArray.end();

	ibPreparedStatementMySQL* pStatement = new ibPreparedStatementMySQL(m_pInterface);
	if (pStatement)
		pStatement->SetEncoding(GetEncoding());
	while (start != stop)
	{
		MYSQL_STMT* pMysqlStatement = m_pInterface->GetMysqlStmtInit()((MYSQL*)m_pDatabase);
		if (pMysqlStatement != nullptr)
		{
			wxCharBuffer sqlBuffer = ConvertToUnicodeStream((*start));
			//puts(sqlBuffer);
			if (m_pInterface->GetMysqlStmtPrepare()(pMysqlStatement, sqlBuffer, GetEncodedStreamLength((*start))) == 0)
			{
				pStatement->AddPreparedStatement(pMysqlStatement);
			}
			else
			{
				SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
				SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
				ThrowDatabaseException();
			}
		}
		else
		{
			SetErrorCode(ibDatabaseLayerMySQL::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
			ThrowDatabaseException();
			return nullptr;
		}
		start++;
	}
	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool ibDatabaseLayerMySQL::TableExists(const wxString& table)
{
	bool bReturn = false;
	/*
	  // This is the way that I'd prefer to retrieve the list of tables
	  // Unfortunately MySQL returns both tables and view together
	  // So we have to try a SQL call (which may be MySQL version dependent)
	  wxCharBuffer tableBuffer = ConvertToUnicodeStream(table);
	  MYSQL_RES* pResults = m_pInterface->GetMysqlListTables()(m_pDatabase, tableBuffer);
	  if (pResults != nullptr)
	  {
		MYSQL_ROW currentRow = nullptr;
		while ((currentRow = m_Interfce.GetMysqlFetchRow()(pResults)) != nullptr)
		{
		  wxString strTable = ConvertFromUnicodeStream(currentRow[0]);
		  if (strTable == table)
			bReturn = true;
		}
		m_pInterface->GetMysqlFreeResult()(pResults);
	  }
	*/
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SHOW TABLE STATUS WHERE Comment != 'VIEW' AND Name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, table);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				if (pResult->Next())
				{
					wxString strTable = pResult->GetResultString(1);
					if (table == strTable)
						bReturn = true;
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

bool ibDatabaseLayerMySQL::ViewExists(const wxString& view)
{
	bool bReturn = false;
	/*
	  // This is the way that I'd prefer to retrieve the list of tables
	  // Unfortunately MySQL returns both tables and view together
	  // So we have to try a SQL call (which may be MySQL version dependent)
	  wxCharBuffer viewBuffer = ConvertToUnicodeStream(view);
	  MYSQL_RES* pResults = m_pInterface->GetMysqlListTables()(m_pDatabase, viewBuffer);
	  if (pResults != nullptr)
	  {
		MYSQL_ROW currentRow = nullptr;
		while ((currentRow = m_pInterface->GetMysqlFetchRow()(pResults)) != nullptr)
		{
		  wxString strView = ConvertFromUnicodeStream(currentRow[0]);
		  if (strView == view)
			bReturn = true;
		}
		m_pInterface->GetMysqlFreeResult()(pResults);
	  }
	*/
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SHOW TABLE STATUS WHERE Comment = 'VIEW' AND Name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, view);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				if (pResult->Next())
				{
					wxString strView = pResult->GetResultString(1);
					if (view == strView)
						bReturn = true;
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

wxArrayString ibDatabaseLayerMySQL::GetTables()
{
	wxArrayString returnArray;

	if (m_pInterface->GetMysqlGetServerVersion()((MYSQL*)m_pDatabase) >= 50010)
	{
		ibDatabaseResultSet* pResult = nullptr;
		try {
			wxString query = wxT("SHOW TABLE STATUS WHERE Comment != 'VIEW';");
			pResult = ExecuteQuery(query);

			while (pResult->Next())
			{
				wxString table = pResult->GetResultString(1).Trim();
				if (!table.IsEmpty())
					returnArray.Add(table);
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
	}

	// If no tables have been found, then try the MySQL API directly
	//  This may pick up VIEWS as well as tables unfortunately
	if (returnArray.Count() == 0)
	{
		MYSQL_RES* pResults = m_pInterface->GetMysqlListTables()((MYSQL*)m_pDatabase, nullptr);
		if (pResults != nullptr)
		{
			MYSQL_ROW currentRow = nullptr;
			while ((currentRow = m_pInterface->GetMysqlFetchRow()(pResults)) != nullptr)
			{
				wxString strTable = ConvertFromUnicodeStream(currentRow[0]);
				returnArray.Add(strTable);
			}
			m_pInterface->GetMysqlFreeResult()(pResults);
		}
	}

	return returnArray;
}

wxArrayString ibDatabaseLayerMySQL::GetViews()
{
	wxArrayString returnArray;

	if (m_pInterface->GetMysqlGetServerVersion()((MYSQL*)m_pDatabase) >= 50010)
	{
		ibDatabaseResultSet* pResult = nullptr;
		try {
			wxString query = wxT("SHOW TABLE STATUS WHERE Comment = 'VIEW';");
			pResult = ExecuteQuery(query);

			while (pResult->Next())
			{
				returnArray.Add(pResult->GetResultString(1).Trim());
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
	}

	return returnArray;
}

wxArrayString ibDatabaseLayerMySQL::GetColumns(const wxString& table)
{
	wxArrayString returnArray;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxString::Format(wxT("SHOW COLUMNS FROM %s;"), table.c_str());
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1).Trim());
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

int ibDatabaseLayerMySQL::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Mysql database error code values to ibDatabaseLayer values
	// For now though, we'll just return error
	return nCode;
	//return DATABASE_LAYER_ERROR;
}

ibBackendDatabaseException::Kind ibDatabaseLayerMySQL::ClassifyDatabaseError(int nativeCode) const
{
	using Kind = ibBackendDatabaseException::Kind;
	switch (nativeCode) {
		// Server-side error codes — server has accepted the connection
		// and returned a structured error.
		case 1213: return Kind::Deadlock;   // ER_LOCK_DEADLOCK
		case 1205: return Kind::Timeout;    // ER_LOCK_WAIT_TIMEOUT
		case 1062:                          // ER_DUP_ENTRY
		case 1452:                          // ER_NO_REFERENCED_ROW (FK)
		case 1451:                          // ER_ROW_IS_REFERENCED (FK)
		case 1048: return Kind::Constraint; // ER_BAD_NULL_ERROR (NOT NULL)
		case 1064:                          // ER_PARSE_ERROR
		case 1054:                          // ER_BAD_FIELD_ERROR
		case 1146: return Kind::Syntax;     // ER_NO_SUCH_TABLE

		// Client-side error codes (CR_* range, mysql/errmsg.h).
		case 2002:                          // CR_CONNECTION_ERROR (unix socket)
		case 2003:                          // CR_CONN_HOST_ERROR (TCP)
		case 2006:                          // CR_SERVER_GONE_ERROR
		case 2013: return Kind::ConnectionLost; // CR_SERVER_LOST

		default:   return Kind::Unknown;
	}
}

bool ibDatabaseLayerMySQL::IsAvailable()
{
	bool bAvailable = false;
	ibInterfaceMySQL* pInterface = new ibInterfaceMySQL();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

