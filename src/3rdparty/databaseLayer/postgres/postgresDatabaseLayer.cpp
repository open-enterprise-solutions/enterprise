#include "postgresDatabaseLayer.h"
#include "postgresInterface.h"
#include "postgresResultSet.h"
#include "postgresPreparedStatement.h"

#include <3rdparty/databaseLayer/databaseErrorCodes.h>
#include <3rdparty/databaseLayer/databaseLayerException.h>

// ctor
PostgresDatabaseLayer::PostgresDatabaseLayer()
	: DatabaseLayer()
{
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new PostgresInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = _("localhost");
	m_strUser = _("");
	m_strPassword = _("");
	m_strDatabase = _("");
	m_strPort = _("");
}

PostgresDatabaseLayer::PostgresDatabaseLayer(const wxString& strDatabase)
	: DatabaseLayer()
{
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new PostgresInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = _("localhost");
	m_strUser = _("");
	m_strPassword = _("");
	m_strPort = _("");

	Open(strDatabase);
}

PostgresDatabaseLayer::PostgresDatabaseLayer(const wxString& strServer, const wxString& strDatabase)
	: DatabaseLayer()
{
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new PostgresInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = strServer;
	m_strUser = _("");
	m_strPassword = _("");
	m_strPort = _("");

	Open(strDatabase);
}

PostgresDatabaseLayer::PostgresDatabaseLayer(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: DatabaseLayer()
{
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new PostgresInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = _("localhost");
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strPort = _("");

	Open(strDatabase);
}

PostgresDatabaseLayer::PostgresDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: DatabaseLayer()
{
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new PostgresInterface();
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
	m_strPort = _("");

	Open(strDatabase);
}

PostgresDatabaseLayer::PostgresDatabaseLayer(const wxString& strServer, int nPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: DatabaseLayer()
{
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new PostgresInterface();
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
	m_strPort = wxString::Format(_("%d"), nPort);

	Open(strDatabase);
}

// dtor
PostgresDatabaseLayer::~PostgresDatabaseLayer()
{
	Close();
	wxDELETE(m_pInterface);
}

// open database
bool PostgresDatabaseLayer::Open()
{
	ResetErrorCodes();

	wxCharBuffer serverCharBuffer;
	const char* pHost = NULL;
	wxCharBuffer databaseBuffer;
	const char* pDatabase = NULL;
	wxCharBuffer userCharBuffer;
	const char* pUser = NULL;
	wxCharBuffer passwordCharBuffer;
	const char* pPassword = NULL;
	const char* pTty = NULL;
	const char* pOptions = NULL;
	wxCharBuffer portCharBuffer;
	const char* pPort = NULL;

	if (m_strServer != _("localhost") && m_strServer != _(""))
	{
		serverCharBuffer = ConvertToUnicodeStream(m_strServer);
		pHost = serverCharBuffer;
	}

	if (m_strUser != _(""))
	{
		userCharBuffer = ConvertToUnicodeStream(m_strUser);
		pUser = userCharBuffer;
	}

	if (m_strPassword != _(""))
	{
		passwordCharBuffer = ConvertToUnicodeStream(m_strPassword);
		pPassword = passwordCharBuffer;
	}

	if (m_strPort != _(""))
	{
		portCharBuffer = ConvertToUnicodeStream(m_strPort);
		pPort = portCharBuffer;
	}

	m_pDatabase = m_pInterface->GetPQsetdbLogin()(pHost, pPort, pOptions, pTty, NULL, pUser, pPassword);
	if (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) == CONNECTION_BAD)
	{
		SetErrorCode(PostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}
	else
	{
		m_pInterface->GetPQsetClientEncoding()((PGconn*)m_pDatabase, "UTF-8");
		wxCSConv conv((const wxChar*)(m_pInterface->GetPQencodingToChar()(m_pInterface->GetPQclientEncoding()((PGconn*)m_pDatabase))));
		SetEncoding(&conv);
	}

	if (!DatabaseExists(m_strDatabase)) {
		bool result = RunQuery("CREATE DATABASE " + m_strDatabase, false) != DATABASE_LAYER_QUERY_RESULT_ERROR;
		if (!result)
			return false;
		RunQuery("GRANT ALL PRIVILEGES ON DATABASE " + m_strDatabase + " to " + m_strUser, false);
	}

	if (m_strDatabase != _(""))
	{
		databaseBuffer = ConvertToUnicodeStream(m_strDatabase);
		pDatabase = databaseBuffer;
	}

	if (m_pDatabase != NULL) {
		m_pInterface->GetPQfinish()((PGconn*)m_pDatabase);
		m_pDatabase = NULL;
	}

	m_pDatabase = m_pInterface->GetPQsetdbLogin()(pHost, pPort, pOptions, pTty, databaseBuffer, pUser, pPassword);
	if (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) == CONNECTION_BAD)
	{
		SetErrorCode(PostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}
	else 
	{
		m_pInterface->GetPQsetClientEncoding()((PGconn*)m_pDatabase, "UTF-8");
		wxCSConv conv((const wxChar*)(m_pInterface->GetPQencodingToChar()(m_pInterface->GetPQclientEncoding()((PGconn*)m_pDatabase))));
		SetEncoding(&conv);
	}

	return true;
}

bool PostgresDatabaseLayer::Open(const wxString& strDatabase)
{
	m_strDatabase = strDatabase;
	return Open();
}

bool PostgresDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase)
{
	m_strServer = strServer;
	m_strUser = _("");
	m_strPassword = _("");
	m_strDatabase = strDatabase;
	m_strPort = _("");
	return Open();
}

bool PostgresDatabaseLayer::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = _("localhost");
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = _("");
	return Open();
}

bool PostgresDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = _("");
	return Open();
}

bool PostgresDatabaseLayer::Open(const wxString& strServer, int nPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	SetPort(nPort);
	return Open();
}

// close database
bool PostgresDatabaseLayer::Close()
{
	CloseResultSets();
	CloseStatements();

	if (m_pDatabase)
	{
		m_pInterface->GetPQfinish()((PGconn*)m_pDatabase);
		m_pDatabase = NULL;
	}

	return true;
}

void PostgresDatabaseLayer::SetPort(int nPort)
{
	m_strPort = wxString::Format(_("%d"), nPort);
}

bool PostgresDatabaseLayer::IsOpen()
{
	if (m_pDatabase)
		return (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) != CONNECTION_BAD);
	else
		return false;
}

// transaction support
void PostgresDatabaseLayer::BeginTransaction()
{
	RunQuery(_("BEGIN"), false);
}

void PostgresDatabaseLayer::Commit()
{
	RunQuery(_("COMMIT"), false);
}

void PostgresDatabaseLayer::RollBack()
{
	RunQuery(_("ROLLBACK"), false);
}


// query database
int PostgresDatabaseLayer::RunQuery(const wxString& strQuery, bool WXUNUSED(bParseQuery))
{
	// PostgreSQL takes care of parsing the queries itself so bParseQuery is ignored

	ResetErrorCodes();

	wxCharBuffer sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer);
	if ((pResultCode == NULL) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_COMMAND_OK))
	{
		SetErrorCode(PostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQresultStatus()(pResultCode)));
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

DatabaseResultSet* PostgresDatabaseLayer::RunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();

	wxCharBuffer sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer);
	if ((pResultCode == NULL) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_TUPLES_OK))
	{
		SetErrorCode(PostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		m_pInterface->GetPQclear()(pResultCode);
		ThrowDatabaseException();
		return NULL;
	}
	else
	{
		PostgresResultSet* pResultSet = new PostgresResultSet(m_pInterface, pResultCode);
		pResultSet->SetEncoding(GetEncoding());
		LogResultSetForCleanup(pResultSet);
		return pResultSet;
	}
}

// PreparedStatement support
PreparedStatement* PostgresDatabaseLayer::PrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();

	PostgresPreparedStatement* pStatement = PostgresPreparedStatement::CreateStatement(m_pInterface, (PGconn*)m_pDatabase, strQuery);
	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool PostgresDatabaseLayer::DatabaseExists(const wxString& database)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	PreparedStatement* pStatement = NULL;
	DatabaseResultSet* pResult = NULL;

#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT COUNT(*) FROM pg_catalog.pg_database WHERE datname=?;");
		pStatement = PrepareStatement(query);
		if (pStatement != NULL) {
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
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		if (pStatement != NULL)
		{
			CloseStatement(pStatement);
			pStatement = NULL;
		}

		throw e;
	}
#endif

	if (pResult != NULL)
	{
		CloseResultSet(pResult);
		pResult = NULL;
	}

	if (pStatement != NULL)
	{
		CloseStatement(pStatement);
		pStatement = NULL;
	}

	return bReturn;
}

bool PostgresDatabaseLayer::TableExists(const wxString& table)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	PreparedStatement* pStatement = NULL;
	DatabaseResultSet* pResult = NULL;

#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_name=?;");
		pStatement = PrepareStatement(query);
		if (pStatement != NULL) {
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
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		if (pStatement != NULL)
		{
			CloseStatement(pStatement);
			pStatement = NULL;
		}

		throw e;
	}
#endif

	if (pResult != NULL)
	{
		CloseResultSet(pResult);
		pResult = NULL;
	}

	if (pStatement != NULL)
	{
		CloseStatement(pStatement);
		pStatement = NULL;
	}

	return bReturn;
}

bool PostgresDatabaseLayer::ViewExists(const wxString& view)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	PreparedStatement* pStatement = NULL;
	DatabaseResultSet* pResult = NULL;

#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='VIEW' AND table_name=?;");
		pStatement = PrepareStatement(query);
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
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		if (pStatement != NULL)
		{
			CloseStatement(pStatement);
			pStatement = NULL;
		}

		throw e;
	}
#endif

	if (pResult != NULL)
	{
		CloseResultSet(pResult);
		pResult = NULL;
	}

	if (pStatement != NULL)
	{
		CloseStatement(pStatement);
		pStatement = NULL;
}

	return bReturn;
}

wxArrayString PostgresDatabaseLayer::GetTables()
{
	wxArrayString returnArray;

	DatabaseResultSet* pResult = NULL;
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT table_name FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
}
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
}
	catch (DatabaseLayerException& e)
	{
		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		throw e;
	}
#endif

	if (pResult != NULL)
	{
		CloseResultSet(pResult);
		pResult = NULL;
}

	return returnArray;
	}

wxArrayString PostgresDatabaseLayer::GetViews()
{
	wxArrayString returnArray;

	DatabaseResultSet* pResult = NULL;
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT table_name FROM information_schema.tables WHERE table_type='VIEW' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
}
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		throw e;
	}
#endif

	if (pResult != NULL)
	{
		CloseResultSet(pResult);
		pResult = NULL;
	}

	return returnArray;
}

wxArrayString PostgresDatabaseLayer::GetColumns(const wxString& table)
{
	// Initialize variables
	wxArrayString returnArray;

	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	PreparedStatement* pStatement = NULL;
	DatabaseResultSet* pResult = NULL;

#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT column_name FROM information_schema.columns WHERE table_name=? ORDER BY ordinal_position;");
		pStatement = PrepareStatement(query);
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
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != NULL)
		{
			CloseResultSet(pResult);
			pResult = NULL;
		}

		if (pStatement != NULL)
		{
			CloseStatement(pStatement);
			pStatement = NULL;
		}

		throw e;
	}
#endif

	if (pResult != NULL)
	{
		CloseResultSet(pResult);
		pResult = NULL;
	}

	if (pStatement != NULL)
	{
		CloseStatement(pStatement);
		pStatement = NULL;
	}

	return returnArray;
}

int PostgresDatabaseLayer::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Postgresql database error code values to DatabaseLayer values
	// For now though, we'll just return error
	return nCode;
	//return DATABASE_LAYER_ERROR;
}

bool PostgresDatabaseLayer::IsAvailable()
{
	bool bAvailable = false;
	PostgresInterface* pInterface = new PostgresInterface();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

