#include "postgresDatabaseLayer.h"
#include "postgresInterface.h"
#include "postgresResultSet.h"
#include "postgresPreparedStatement.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

// ctor
CPostgresDatabaseLayer::CPostgresDatabaseLayer()
	: IDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CPostgresInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = _("localhost");
	m_strUser = _("postgres");
	m_strPassword = _("");
	m_strDatabase = _("postgres");
	m_strPort = _("5432");
}

CPostgresDatabaseLayer::CPostgresDatabaseLayer(const wxString& strDatabase)
	: IDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CPostgresInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = _("localhost");
	m_strUser = _("postgres");
	m_strPassword = _("");
	m_strPort = _("5432");

	Open(strDatabase);
}

CPostgresDatabaseLayer::CPostgresDatabaseLayer(const wxString& strServer, const wxString& strDatabase)
	: IDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CPostgresInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading PostgreSQL library"));
		ThrowDatabaseException();
		return;
	}
#endif
	m_strServer = strServer;
	m_strUser = _("postgres");
	m_strPassword = _("");
	m_strPort = _("5432");

	Open(strDatabase);
}

CPostgresDatabaseLayer::CPostgresDatabaseLayer(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: IDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CPostgresInterface();
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
	m_strPort = _("5432");

	Open(strDatabase);
}

CPostgresDatabaseLayer::CPostgresDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: IDatabaseLayer(), m_pDatabase(nullptr)
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CPostgresInterface();
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
	m_strPort = _("5432");

	Open(strDatabase);
}

CPostgresDatabaseLayer::CPostgresDatabaseLayer(const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: IDatabaseLayer()
{
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CPostgresInterface();
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

// dtor
CPostgresDatabaseLayer::~CPostgresDatabaseLayer()
{
	Close();
	wxDELETE(m_pInterface);
}

// open database
bool CPostgresDatabaseLayer::Open()
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

	m_pDatabase = m_pInterface->GetPQsetdbLogin()(pHost, pPort, pOptions, pTty, nullptr, pUser, pPassword);
	if (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) == CONNECTION_BAD)
	{
		SetErrorCode(CPostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
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

	if (m_strDatabase != _("") && !DatabaseExists(m_strDatabase)) {
		bool result = DoRunQuery("CREATE DATABASE " + m_strDatabase, false) != DATABASE_LAYER_QUERY_RESULT_ERROR;
		if (!result)
			return false;
		DoRunQuery("GRANT ALL PRIVILEGES ON DATABASE " + m_strDatabase + " to " + m_strUser, false);
	}

	if (m_strDatabase != _(""))
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
		SetErrorCode(CPostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
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

bool CPostgresDatabaseLayer::Open(const wxString& strDatabase)
{
	m_strDatabase = strDatabase;
	return Open();
}

bool CPostgresDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase)
{
	m_strServer = strServer;
	m_strUser = _("");
	m_strPassword = _("");
	m_strDatabase = strDatabase;
	m_strPort = _("");
	return Open();
}

bool CPostgresDatabaseLayer::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = _("localhost");
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = _("");
	return Open();
}

bool CPostgresDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;
	m_strPort = _("");
	return Open();
}

bool CPostgresDatabaseLayer::Open(const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strPort = strPort;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strDatabase = strDatabase;

	return Open();
}

// close database
bool CPostgresDatabaseLayer::Close()
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

bool CPostgresDatabaseLayer::IsOpen()
{
	if (m_pDatabase)
		return (m_pInterface->GetPQstatus()((PGconn*)m_pDatabase) != CONNECTION_BAD);
	else
		return false;
}

// transaction support
void CPostgresDatabaseLayer::BeginTransaction()
{
	DoRunQuery(_("BEGIN"), false);
}

void CPostgresDatabaseLayer::Commit()
{
	DoRunQuery(_("COMMIT"), false);
}

void CPostgresDatabaseLayer::RollBack()
{
	DoRunQuery(_("ROLLBACK"), false);
}

// query database
int CPostgresDatabaseLayer::DoRunQuery(const wxString& strQuery, bool WXUNUSED(bParseQuery))
{
	// PostgreSQL takes care of parsing the queries itself so bParseQuery is ignored

	ResetErrorCodes();

	wxCharBuffer sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer);
	if ((pResultCode == nullptr) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_COMMAND_OK))
	{
		SetErrorCode(CPostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQresultStatus()(pResultCode)));
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

IDatabaseResultSet* CPostgresDatabaseLayer::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();

	wxCharBuffer sqlBuffer = ConvertToUnicodeStream(strQuery);
	PGresult* pResultCode = m_pInterface->GetPQexec()((PGconn*)m_pDatabase, sqlBuffer);
	if ((pResultCode == nullptr) || (m_pInterface->GetPQresultStatus()(pResultCode) != PGRES_TUPLES_OK))
	{
		SetErrorCode(CPostgresDatabaseLayer::TranslateErrorCode(m_pInterface->GetPQstatus()((PGconn*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQerrorMessage()((PGconn*)m_pDatabase)));
		m_pInterface->GetPQclear()(pResultCode);
		ThrowDatabaseException();
		return nullptr;
	}
	else
	{
		CPostgresResultSet* pResultSet = new CPostgresResultSet(m_pInterface, pResultCode);
		pResultSet->SetEncoding(GetEncoding());
		LogResultSetForCleanup(pResultSet);
		return pResultSet;
	}
}

// IPreparedStatement support
IPreparedStatement* CPostgresDatabaseLayer::DoPrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();

	CPostgresPreparedStatement* pStatement = CPostgresPreparedStatement::CreateStatement(m_pInterface, (PGconn*)m_pDatabase, strQuery);
	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool CPostgresDatabaseLayer::DatabaseExists(const wxString& database)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	IPreparedStatement* pStatement = nullptr;
	IDatabaseResultSet* pResult = nullptr;

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT COUNT(*) FROM pg_catalog.pg_database WHERE datname=?;");
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

bool CPostgresDatabaseLayer::TableExists(const wxString& table)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	IPreparedStatement* pStatement = nullptr;
	IDatabaseResultSet* pResult = nullptr;

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_name=?;");
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

bool CPostgresDatabaseLayer::ViewExists(const wxString& view)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	IPreparedStatement* pStatement = nullptr;
	IDatabaseResultSet* pResult = nullptr;

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT COUNT(*) FROM information_schema.tables WHERE table_type='VIEW' AND table_name=?;");
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

wxArrayString CPostgresDatabaseLayer::GetTables()
{
	wxArrayString returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT table_name FROM information_schema.tables WHERE table_type='BASE TABLE' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
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

wxArrayString CPostgresDatabaseLayer::GetViews()
{
	wxArrayString returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT table_name FROM information_schema.tables WHERE table_type='VIEW' AND table_schema='public';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
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

wxArrayString CPostgresDatabaseLayer::GetColumns(const wxString& table)
{
	// Initialize variables
	wxArrayString returnArray;

	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	IPreparedStatement* pStatement = nullptr;
	IDatabaseResultSet* pResult = nullptr;

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT column_name FROM information_schema.columns WHERE table_name=? ORDER BY ordinal_position;");
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

	return returnArray;
}

int CPostgresDatabaseLayer::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Postgresql database error code values to IDatabaseLayer values
	// For now though, we'll just return error
	return nCode;
	//return DATABASE_LAYER_ERROR;
}

bool CPostgresDatabaseLayer::IsAvailable()
{
	bool bAvailable = false;
	CPostgresInterface* pInterface = new CPostgresInterface();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

