#include "mysqlDatabaseLayer.h"

#include "engine/mysql.h"

#include "mysqlInterface.h"
#include "mysqlPreparedStatement.h"
#include "mysqlPreparedStatementResultSet.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <wx/tokenzr.h>

// ctor
CMysqlDatabaseLayer::CMysqlDatabaseLayer()
	: IDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CMysqlInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	m_strServer = _("localhost");
	m_iPort = 3306; // default
	m_strDatabase = _("");
	m_strUser = _("");
	m_strPassword = _("");
}

CMysqlDatabaseLayer::CMysqlDatabaseLayer(const wxString& strDatabase)
	: IDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CMysqlInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	m_strServer = _("localhost");
	m_iPort = 3306; // default
	m_strUser = _("");
	m_strPassword = _("");
	Open(strDatabase);
}

CMysqlDatabaseLayer::CMysqlDatabaseLayer(const wxString& strServer, const wxString& strDatabase)
	: IDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CMysqlInterface();
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
	m_strUser = _("");
	m_strPassword = _("");
	Open(strDatabase);
}

CMysqlDatabaseLayer::CMysqlDatabaseLayer(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: IDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CMysqlInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading MySQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	InitDatabase();
	m_strServer = _("localhost");
	m_iPort = 3306; // default
	m_strUser = strUser;
	m_strPassword = strPassword;
	Open(strDatabase);
}

CMysqlDatabaseLayer::CMysqlDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: IDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CMysqlInterface();
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

// dtor
CMysqlDatabaseLayer::~CMysqlDatabaseLayer()
{
	Close();
	//m_pInterface->GetMysqlClose()(m_pDatabase);
	//delete m_pDatabase;
	//m_pInterface->GetMysqlLibraryEnd()();
	m_pInterface->GetMysqlServerEnd()();
	wxDELETE(m_pInterface);
}

// open database
void CMysqlDatabaseLayer::InitDatabase()
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
bool CMysqlDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase)
{
	ParseServerAndPort(strServer);
	return Open(strDatabase);
}

bool CMysqlDatabaseLayer::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strUser = strUser;
	m_strPassword = strPassword;
	return Open(strDatabase);
}

bool CMysqlDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	ParseServerAndPort(strServer);
	m_strUser = strUser;
	m_strPassword = strPassword;
	return Open(strDatabase);
}

bool CMysqlDatabaseLayer::Open(const wxString& strDatabase)
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
		wxCSConv conv(_("UTF-8"));
		SetEncoding(&conv);
#endif

		return true;
	}
	else
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}
}

void CMysqlDatabaseLayer::ParseServerAndPort(const wxString& strServer)
{
	int portIndicator = strServer.Find(_(":"));
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
bool CMysqlDatabaseLayer::Close()
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


bool CMysqlDatabaseLayer::IsOpen()
{
	return (m_pDatabase != nullptr);
}

// transaction support
void CMysqlDatabaseLayer::BeginTransaction()
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetMysqlAutoCommit()((MYSQL*)m_pDatabase, 0);
	if (nReturn != 0)
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
}

void CMysqlDatabaseLayer::Commit()
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetMysqlCommit()((MYSQL*)m_pDatabase);
	if (nReturn != 0)
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
	nReturn = m_pInterface->GetMysqlAutoCommit()((MYSQL*)m_pDatabase, 1);
	if (nReturn != 0)
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
}

void CMysqlDatabaseLayer::RollBack()
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetMysqlRollback()((MYSQL*)m_pDatabase);
	if (nReturn != 0)
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
	nReturn = m_pInterface->GetMysqlAutoCommit()((MYSQL*)m_pDatabase, 1);
	if (nReturn != 0)
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
		ThrowDatabaseException();
	}
}


// query database
int CMysqlDatabaseLayer::DoRunQuery(const wxString& strQuery, bool bParseQuery)
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
			SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		start++;
	}
	return m_pInterface->GetMysqlAffectedRows()((MYSQL*)m_pDatabase);
}

IDatabaseResultSet* CMysqlDatabaseLayer::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();

	wxArrayString QueryArray = ParseQueries(strQuery);

	int nArraySize = QueryArray.size();
	CMysqlPreparedStatementResultSet* pResultSet = nullptr;
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
					SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlStmtErrno()(pMysqlStatement)));
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
				SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
				SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
				ThrowDatabaseException();
			}
			if (i == nArraySize - 1)
			{
				pResultSet = new CMysqlPreparedStatementResultSet(m_pInterface, pMysqlStatement, true);
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
			SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
			ThrowDatabaseException();
			return nullptr;
		}
	}
	LogResultSetForCleanup(pResultSet);
	return pResultSet;
}

// IPreparedStatement support
IPreparedStatement* CMysqlDatabaseLayer::DoPrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();

	wxArrayString QueryArray = ParseQueries(strQuery);

	wxArrayString::iterator start = QueryArray.begin();
	wxArrayString::iterator stop = QueryArray.end();

	CMysqlPreparedStatement* pStatement = new CMysqlPreparedStatement(m_pInterface);
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
				SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
				SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
				ThrowDatabaseException();
			}
		}
		else
		{
			SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlErrno()((MYSQL*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlError()((MYSQL*)m_pDatabase)));
			ThrowDatabaseException();
			return nullptr;
		}
		start++;
	}
	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool CMysqlDatabaseLayer::TableExists(const wxString& table)
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
	IPreparedStatement* pStatement = nullptr;
	IDatabaseResultSet* pResult = nullptr;

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SHOW TABLE STATUS WHERE Comment != 'VIEW' AND Name=?;");
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
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
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

		throw e;
	}
#endif

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

bool CMysqlDatabaseLayer::ViewExists(const wxString& view)
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
	IPreparedStatement* pStatement = nullptr;
	IDatabaseResultSet* pResult = nullptr;

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SHOW TABLE STATUS WHERE Comment = 'VIEW' AND Name=?;");
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
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
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

		throw e;
	}
#endif

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

wxArrayString CMysqlDatabaseLayer::GetTables()
{
	wxArrayString returnArray;

	if (m_pInterface->GetMysqlGetServerVersion()((MYSQL*)m_pDatabase) >= 50010)
	{
		IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		try
		{
#endif
			wxString query = _("SHOW TABLE STATUS WHERE Comment != 'VIEW';");
			pResult = ExecuteQuery(query);

			while (pResult->Next())
			{
				wxString table = pResult->GetResultString(1).Trim();
				if (!table.IsEmpty())
					returnArray.Add(table);
			}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		}
		catch (DatabaseLayerException& e)
		{
			if (pResult != nullptr)
			{
				CloseResultSet(pResult);
				pResult = nullptr;
			}

			throw e;
		}
#endif

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
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

wxArrayString CMysqlDatabaseLayer::GetViews()
{
	wxArrayString returnArray;

	if (m_pInterface->GetMysqlGetServerVersion()((MYSQL*)m_pDatabase) >= 50010)
	{
		IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		try
		{
#endif
			wxString query = _("SHOW TABLE STATUS WHERE Comment = 'VIEW';");
			pResult = ExecuteQuery(query);

			while (pResult->Next())
			{
				returnArray.Add(pResult->GetResultString(1).Trim());
			}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		}
		catch (DatabaseLayerException& e)
		{
			if (pResult != nullptr)
			{
				CloseResultSet(pResult);
				pResult = nullptr;
			}

			throw e;
		}
#endif

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}
	}

	return returnArray;
}

wxArrayString CMysqlDatabaseLayer::GetColumns(const wxString& table)
{
	wxArrayString returnArray;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = wxString::Format(_("SHOW COLUMNS FROM %s;"), table.c_str());
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1).Trim());
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}


	return returnArray;
}

int CMysqlDatabaseLayer::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Mysql database error code values to IDatabaseLayer values
	// For now though, we'll just return error
	return nCode;
	//return DATABASE_LAYER_ERROR;
}

bool CMysqlDatabaseLayer::IsAvailable()
{
	bool bAvailable = false;
	CMysqlInterface* pInterface = new CMysqlInterface();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

