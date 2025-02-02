#include "firebirdDatabaseLayer.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include "engine/ibase.h"

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
#include "firebirdInterface.h"
#endif

#include "firebirdPreparedStatement.h"
#include "firebirdResultSet.h"

#include <wx/file.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/regex.h>

// ctor()
CFirebirdDatabaseLayer::CFirebirdDatabaseLayer()
	: IDatabaseLayer()
{
	m_pDatabase = nullptr;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = nullptr;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CFirebirdInterface();
	
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}

#endif

	m_strServer = _("");  // assume embedded database in this case
	m_strUser = _("sysdba");
	m_strPassword = _("masterkey");
	m_strDatabase = _("");
	m_strRole = wxEmptyString;
}

CFirebirdDatabaseLayer::CFirebirdDatabaseLayer(const wxString& strDatabase)
	: IDatabaseLayer()
{
	m_pDatabase = nullptr;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = nullptr;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CFirebirdInterface();
	
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}

#endif

	m_strServer = _("");  // assume embedded database in this case
	m_strUser = _("sysdba");
	m_strPassword = _("masterkey");
	m_strRole = wxEmptyString;

	Open(strDatabase);
}

CFirebirdDatabaseLayer::CFirebirdDatabaseLayer(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: IDatabaseLayer()
{
	m_pDatabase = nullptr;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = nullptr;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CFirebirdInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}
#endif

	m_strServer = _("");  // assume embedded database in this case
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strRole = wxEmptyString;

	Open(strDatabase);
}

CFirebirdDatabaseLayer::CFirebirdDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: IDatabaseLayer()
{
	m_pDatabase = nullptr;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = nullptr;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CFirebirdInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}
#endif

	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strRole = wxEmptyString;

	Open(strDatabase);
}

CFirebirdDatabaseLayer::CFirebirdDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword, const wxString& strRole)
	: IDatabaseLayer()
{
	m_pDatabase = nullptr;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = nullptr;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	m_pInterface = new CFirebirdInterface();
	if (!m_pInterface->Init())
	{
		SetErrorCode(DATABASE_LAYER_ERROR_LOADING_LIBRARY);
		SetErrorMessage(wxT("Error loading Firebird library"));
		ThrowDatabaseException();
		return;
	}
#endif

	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_strRole = strRole;

	Open(strDatabase);
}

// dtor()
CFirebirdDatabaseLayer::~CFirebirdDatabaseLayer()
{
	Close();
	ISC_STATUS_ARRAY* pStatus = (ISC_STATUS_ARRAY*)m_pStatus;
	wxDELETEA(pStatus);
	m_pStatus = nullptr;
	wxDELETE(m_pInterface);
	m_pInterface = nullptr;
	wxDELETE(m_fbNode);
	m_fbNode = nullptr;
}

// open database
bool CFirebirdDatabaseLayer::Open(const wxString& strDatabase)
{
	m_strDatabase = strDatabase;
	return Open();
}

bool CFirebirdDatabaseLayer::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strUser = strUser;
	m_strPassword = strPassword;

	return Open(strDatabase);
}

bool CFirebirdDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;

	return Open(strDatabase);
}

bool CFirebirdDatabaseLayer::Open()
{
	ResetErrorCodes();

	if (m_pInterface == nullptr)
		return false;

	wxCSConv conv(_("UTF-8"));
	SetEncoding(&conv);

	// Combine the server and databsae path strings to pass into the isc_attach_databse function
	wxString strDatabaseUrl;

	if (m_strServer.IsEmpty())
		strDatabaseUrl = m_strDatabase; // Embedded database, just supply the file name
	else
		strDatabaseUrl = m_strServer + _(":") + m_strDatabase;

	wxCharBuffer urlBuffer = ConvertToUnicodeStream(strDatabaseUrl);
	unsigned int urlLength = GetEncodedStreamLength(strDatabaseUrl);

	std::string dpbBuffer;
	{
		dpbBuffer.push_back(isc_dpb_version1);
		dpbBuffer.push_back(isc_dpb_sql_dialect);
		dpbBuffer.push_back(1); // 1 byte long
		dpbBuffer.push_back(SQL_DIALECT_CURRENT);

		//set page size equals '8192'
		dpbBuffer.push_back(isc_dpb_page_size);
		dpbBuffer.push_back(2); //2 byte long
		dpbBuffer.push_back(m_pageSize >> 8 & 0xFF); // 1 char byte 
		dpbBuffer.push_back(m_pageSize & 0xFF); // 1 char byte

		// set UTF8 as default character set
		const char sCharset[] = "UTF8";
		dpbBuffer.push_back(isc_dpb_set_db_charset);
		dpbBuffer.push_back(sizeof(sCharset) - 1);
		dpbBuffer.append(sCharset);

		dpbBuffer.push_back(isc_dpb_utf8_filename);
		dpbBuffer.push_back(urlLength);
		dpbBuffer.append(urlBuffer);

		if (m_strUser.length() > 0)
		{
			int nUsernameLength = m_strUser.length();
			dpbBuffer.push_back(isc_dpb_user_name);
			dpbBuffer.push_back(nUsernameLength);
			dpbBuffer.append(m_strUser);
		}

		if (m_strPassword.length() > 0)
		{
			int nPasswordLength = m_strPassword.length();
			dpbBuffer.push_back(isc_dpb_password);
			dpbBuffer.push_back(nPasswordLength);
			dpbBuffer.append(m_strPassword);
		}

		if (m_strRole.length() > 0)
		{
			int nRoleLength = m_strRole.length();
			dpbBuffer.push_back(isc_dpb_sql_role_name);
			dpbBuffer.push_back(nRoleLength);
			dpbBuffer.append(m_strRole);
		}
	}

	m_pDatabase = nullptr;

	isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;

	int nReturn = 0;

	if (m_strServer.IsEmpty())
	{
		if (!wxFile::Exists(strDatabaseUrl))
		{
			nReturn = m_pInterface->GetIscCreateDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus, urlLength, (char*)(const char*)urlBuffer,
				&pDatabase,
				dpbBuffer.length(), dpbBuffer.c_str(),
				NULL);
		}
		else
		{
			nReturn = m_pInterface->GetIscAttachDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus, urlLength, (char*)(const char*)urlBuffer,
				&pDatabase,
				dpbBuffer.length(), dpbBuffer.c_str());
		}
	}
	else
	{
		nReturn = m_pInterface->GetIscAttachDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus, urlLength, (char*)(const char*)urlBuffer,
			&pDatabase,
			dpbBuffer.length(), dpbBuffer.c_str());
	}

	m_pDatabase = pDatabase;

	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();

		return false;
	}

	return true;
}

// close database  
bool CFirebirdDatabaseLayer::Close()
{
	CloseResultSets();
	CloseStatements();

	if (m_pDatabase)
	{
		while (m_fbNode)
		{
			if (m_fbNode->m_pTransaction)
			{
				isc_tr_handle pTransaction = (isc_tr_handle)m_fbNode->m_pTransaction;
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
				m_fbNode->m_pTransaction = nullptr;
			}

			fb_tr_list *tr_link = m_fbNode->prev;

			if (m_fbNode->prev) {
				wxDELETE(m_fbNode);
			}

			m_fbNode = tr_link;
		}

		isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;
		int nReturn = m_pInterface->GetIscDetachDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase);
		m_pDatabase = nullptr;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
			return false;
		}
	}

	return true;
}

bool CFirebirdDatabaseLayer::IsOpen()
{
	return (m_pDatabase != nullptr);
}

// transaction support
void CFirebirdDatabaseLayer::BeginTransaction()
{
	ResetErrorCodes();

	//wxLogDebug(_("Beginning transaction"));
	if (m_pDatabase)
	{
		fb_tr_list *fbNextNode = new fb_tr_list;
		fbNextNode->prev = m_fbNode;
		fbNextNode->m_pTransaction = 0L;

		isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;
		isc_tr_handle pTransaction = (isc_tr_handle)fbNextNode->m_pTransaction;

		int nReturn = m_pInterface->GetIscStartTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction, 1, &pDatabase, 0, nullptr);

		m_pDatabase = pDatabase;
		fbNextNode->m_pTransaction = pTransaction;

		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
		}

		m_fbNode = fbNextNode;
	}
}

void CFirebirdDatabaseLayer::Commit()
{
	ResetErrorCodes();

	//wxLogDebug(_("Committing transaction"));
	if (m_pDatabase && m_fbNode->m_pTransaction)
	{
		isc_tr_handle pTransaction = (isc_tr_handle)m_fbNode->m_pTransaction;
		int nReturn = m_pInterface->GetIscCommitTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
		m_fbNode->m_pTransaction = pTransaction;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
		else
		{
			// We're done with the transaction, so set it to nullptr so that we know that a new transaction must be started if we run any queries
			m_fbNode->m_pTransaction = 0L;

			fb_tr_list *tr_link = m_fbNode->prev;
			wxDELETE(m_fbNode);
			m_fbNode = tr_link;
		}
	}
}

void CFirebirdDatabaseLayer::RollBack()
{
	ResetErrorCodes();

	//wxLogDebug(_("Rolling back transaction"));
	if (m_pDatabase && m_fbNode->m_pTransaction)
	{
		isc_tr_handle pTransaction = (isc_tr_handle)m_fbNode->m_pTransaction;
		int nReturn = m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
		m_fbNode->m_pTransaction = pTransaction;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
		else
		{
			// We're done with the transaction, so set it to nullptr so that we know that a new transaction must be started if we run any queries
			m_fbNode->m_pTransaction = nullptr;

			fb_tr_list *tr_link = m_fbNode->prev;
			wxDELETE(m_fbNode);
			m_fbNode = tr_link;
		}
	}
}

// query database
int CFirebirdDatabaseLayer::DoRunQuery(const wxString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();
	if (m_pDatabase != nullptr)
	{
		wxCharBuffer sqlDebugBuffer = ConvertToUnicodeStream(strQuery);
#ifdef DEBUG
		wxLogDebug(_("Running query: \"%s\"\n"), (const char*)sqlDebugBuffer);
#endif // !DEBUG
		wxArrayString QueryArray;
		if (bParseQuery)
			QueryArray = ParseQueries(strQuery);
		else
			QueryArray.push_back(strQuery);

		wxArrayString::iterator start = QueryArray.begin();
		wxArrayString::iterator stop = QueryArray.end();

		long rows = 1;
		if (QueryArray.size() > 0)
		{
			bool bQuickieTransaction = false;

			if (m_fbNode->m_pTransaction == nullptr)
			{
				// If there's no transaction is progress, run this as a quick one-timer transaction
				bQuickieTransaction = true;
			}

			if (bQuickieTransaction)
			{
				BeginTransaction();
				if (GetErrorCode() != DATABASE_LAYER_OK)
				{
					wxLogError(_("Unable to start transaction"));
					ThrowDatabaseException();
					return DATABASE_LAYER_QUERY_RESULT_ERROR;
				}
			}

			while (start != stop)
			{
				wxCharBuffer sqlBuffer = ConvertToUnicodeStream(*start);
				isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;
				isc_tr_handle pTransaction = (isc_tr_handle)m_fbNode->m_pTransaction;
				//int nReturn = m_pInterface->GetIscDsqlExecuteImmediate()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase, &pTransaction, 0, (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, nullptr);
				int nReturn = m_pInterface->GetIscDsqlExecuteImmediate()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase, &pTransaction, GetEncodedStreamLength(*start), (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, nullptr);
				m_pDatabase = pDatabase;
				m_fbNode->m_pTransaction = pTransaction;
				if (nReturn != 0)
				{
					InterpretErrorCodes();
					// Manually try to rollback the transaction rather than calling the member RollBack function
					//  so that we can ignore the error messages
					isc_tr_handle pTransaction = (isc_tr_handle)m_fbNode->m_pTransaction;
					m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
					m_fbNode->m_pTransaction = nullptr;

					ThrowDatabaseException();
					return DATABASE_LAYER_QUERY_RESULT_ERROR;
				}
				start++;
			}

			if (bQuickieTransaction)
			{
				Commit();
				if (GetErrorCode() != DATABASE_LAYER_OK)
				{
					ThrowDatabaseException();
					return DATABASE_LAYER_QUERY_RESULT_ERROR;
				}
			}
		}

		return rows;
	}
	else
	{
		wxLogError(_("Database handle is nullptr"));
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}
}

IDatabaseResultSet* CFirebirdDatabaseLayer::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();
	if (m_pDatabase != nullptr)
	{
		wxCharBuffer sqlDebugBuffer = ConvertToUnicodeStream(strQuery);
#if DEBUG 
		wxLogDebug(_("Running query: \"%s\""), (const char*)sqlDebugBuffer);
#endif
		wxArrayString QueryArray = ParseQueries(strQuery);

		if (QueryArray.size() > 0)
		{
			bool bQuickieTransaction = false;

			if (m_fbNode->m_pTransaction == nullptr)
			{
				// If there's no transaction is progress, run this as a quick one-timer transaction
				bQuickieTransaction = true;
			}

			if (QueryArray.size() > 1)
			{
				if (bQuickieTransaction)
				{
					BeginTransaction();
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						wxLogError(_("Unable to start transaction"));
						ThrowDatabaseException();
						return nullptr;
					}
				}

				// Assume that only the last statement in the array returns the result set
				for (unsigned int i = 0; i < QueryArray.size() - 1; i++)
				{
					DoRunQuery(QueryArray[i], false);
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						ThrowDatabaseException();
						return nullptr;
					}
				}

				// Now commit all the previous queries before calling the query that returns a result set
				if (bQuickieTransaction)
				{
					Commit();
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						ThrowDatabaseException();
						return nullptr;
					}
				}
			} // End check if there are more than one query in the array

			isc_tr_handle pQueryTransaction = nullptr;
			bool bManageTransaction = false;
			if (bQuickieTransaction)
			{
				bManageTransaction = true;

				isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;
				int nReturn = m_pInterface->GetIscStartTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, 1, &pDatabase, 0, nullptr);
				m_pDatabase = pDatabase;
				if (nReturn != 0)
				{
					InterpretErrorCodes();
					ThrowDatabaseException();
				}
			}
			else
			{
				pQueryTransaction = m_fbNode->m_pTransaction;
			}

			isc_stmt_handle pStatement = nullptr;
			isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;
			int nReturn = m_pInterface->GetIscDsqlAllocateStatement()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase, &pStatement);
			m_pDatabase = pDatabase;
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				ThrowDatabaseException();
				return nullptr;
			}

			wxCharBuffer sqlBuffer = ConvertToUnicodeStream(QueryArray[QueryArray.size() - 1]);
			nReturn = m_pInterface->GetIscDsqlPrepare()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, &pStatement, 0, (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, nullptr);
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				ThrowDatabaseException();
				return nullptr;
			}

			//--------------------------------------------------------------

			XSQLDA* pOutputSqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(1));
			pOutputSqlda->sqln = 1;
			pOutputSqlda->version = SQLDA_VERSION1;

			// Make sure that we have enough space allocated for the result set
			nReturn = m_pInterface->GetIscDsqlDescribe()(*(ISC_STATUS_ARRAY*)m_pStatus, &pStatement, SQL_DIALECT_CURRENT, pOutputSqlda);
			if (nReturn != 0)
			{
				free(pOutputSqlda);
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				ThrowDatabaseException();
				return nullptr;
			}

			if (pOutputSqlda->sqld > pOutputSqlda->sqln)
			{
				int nColumns = pOutputSqlda->sqld;
				free(pOutputSqlda);
				pOutputSqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(nColumns));
				pOutputSqlda->sqln = nColumns;
				pOutputSqlda->version = SQLDA_VERSION1;
				nReturn = m_pInterface->GetIscDsqlDescribe()(*(ISC_STATUS_ARRAY*)m_pStatus, &pStatement, SQL_DIALECT_CURRENT, pOutputSqlda);
				if (nReturn != 0)
				{
					free(pOutputSqlda);
					InterpretErrorCodes();

					// Manually try to rollback the transaction rather than calling the member RollBack function
					//  so that we can ignore the error messages
					m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

					ThrowDatabaseException();
					return nullptr;
				}
			}

			// Create the result set object
			CFirebirdResultSet* pResultSet = new CFirebirdResultSet(m_pInterface, m_pDatabase, pQueryTransaction, pStatement, pOutputSqlda, true, bManageTransaction);
			pResultSet->SetEncoding(GetEncoding());
			if (pResultSet->GetErrorCode() != DATABASE_LAYER_OK)
			{
				SetErrorCode(pResultSet->GetErrorCode());
				SetErrorMessage(pResultSet->GetErrorMessage());

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				// Wrap the result set deletion in try/catch block if using exceptions.
				//We want to make sure the original error gets to the user
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
				try
				{
#endif
					delete pResultSet;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
				}
				catch (DatabaseLayerException& e)
				{
				}
#endif

				ThrowDatabaseException();
			}

			// Now execute the SQL
			nReturn = m_pInterface->GetIscDsqlExecute()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, &pStatement, SQL_DIALECT_CURRENT, nullptr);
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				// Wrap the result set deletion in try/catch block if using exceptions.
				//  We want to make sure the isc_dsql_execute error gets to the user
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
				try
				{
#endif
					delete pResultSet;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
				}
				catch (DatabaseLayerException& e)
				{
				}
#endif

				ThrowDatabaseException();
				return nullptr;
			}

			//--------------------------------------------------------------

			LogResultSetForCleanup(pResultSet);
			return pResultSet;
		}
		else
			return nullptr;
	}
	else
	{
		wxLogError(_("Database handle is nullptr"));
		return nullptr;
	}
}

IPreparedStatement* CFirebirdDatabaseLayer::DoPrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();

	CFirebirdPreparedStatement* pStatement = CFirebirdPreparedStatement::CreateStatement(m_pInterface, m_pDatabase, m_fbNode->m_pTransaction, strQuery, GetEncoding());
	if (pStatement && (pStatement->GetErrorCode() != DATABASE_LAYER_OK))
	{
		SetErrorCode(pStatement->GetErrorCode());
		SetErrorMessage(pStatement->GetErrorMessage());
		wxDELETE(pStatement); // This sets the pointer to nullptr after deleting it

		ThrowDatabaseException();
		return nullptr;
	}

	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool CFirebirdDatabaseLayer::TableExists(const wxString& table)
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
		wxString tableUpperCase = table.Upper();
		wxString query = _("SELECT COUNT(*) FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS nullptr AND RDB$RELATION_NAME=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, tableUpperCase);
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

bool CFirebirdDatabaseLayer::ViewExists(const wxString& view)
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
		wxString viewUpperCase = view.Upper();
		wxString query = _("SELECT COUNT(*) FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NOT NULL AND RDB$RELATION_NAME=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, viewUpperCase);
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

wxArrayString CFirebirdDatabaseLayer::GetTables()
{
	wxArrayString returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS nullptr");
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

wxArrayString CFirebirdDatabaseLayer::GetViews()
{
	wxArrayString returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NOT NULL");
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

wxArrayString CFirebirdDatabaseLayer::GetColumns(const wxString& table)
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
		wxString tableUpperCase = table.Upper();
		wxString query = _("SELECT RDB$FIELD_NAME FROM RDB$RELATION_FIELDS WHERE RDB$RELATION_NAME=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, tableUpperCase);
			pResult = pStatement->ExecuteQuery();
			if (pResult)
			{
				while (pResult->Next())
				{
					returnArray.Add(pResult->GetResultString(1).Trim());
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

int CFirebirdDatabaseLayer::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Firebird database error code values to IDatabaseLayer values
	// For now though, we'll just return the original error code
	return nCode;
}

//wxString CFirebirdDatabaseLayer::TranslateErrorCodeToString(CFirebirdInterface* pInterface, int nCode, ISC_STATUS_ARRAY status)
wxString CFirebirdDatabaseLayer::TranslateErrorCodeToString(CFirebirdInterface* pInterface, int nCode, void* status)
{
	char szError[512];
	wxString strReturn;

	if (nCode > -901) // Error codes less than -900 indicate that it wasn't a SQL error but an ibase system error
	{
		long* pVector = (long*)status;
		pInterface->GetFbInterpret()(szError, 512, (const ISC_STATUS**)&pVector);

		strReturn = wxString::Format(_("%s\n"), szError);
		while (pInterface->GetFbInterpret()(szError, 512, (const ISC_STATUS**)&pVector))
		{
			strReturn += wxString::Format(_("%s\n"), szError);
		}

		pInterface->GetIscSqlInterprete()(nCode, szError, sizeof(szError));
		strReturn += wxString::Format(_("%s\n"), szError);
	}
	else
	{
		pInterface->GetIscSqlInterprete()(nCode, szError, sizeof(szError));
		wxCharBuffer systemEncoding = wxLocale::GetSystemEncodingName().mb_str(*wxConvCurrent);
		strReturn = CDatabaseStringConverter::ConvertFromUnicodeStream(szError, (const char*)systemEncoding);
	}

	return strReturn;
}

void CFirebirdDatabaseLayer::InterpretErrorCodes()
{
	//wxLogDebug(_("CFirebirdDatabaseLayer::InterpretErrorCodes()"));

	long nSqlCode = m_pInterface->GetIscSqlcode()(*(ISC_STATUS_ARRAY*)m_pStatus);
	SetErrorMessage(CFirebirdDatabaseLayer::TranslateErrorCodeToString(m_pInterface, nSqlCode, *(ISC_STATUS_ARRAY*)m_pStatus));
	if (nSqlCode < -900)  // Error codes less than -900 indicate that it wasn't a SQL error but an ibase system error
	{
		SetErrorCode(CFirebirdDatabaseLayer::TranslateErrorCode(*((ISC_STATUS_ARRAY*)m_pStatus)[1]));
	}
	else
	{
		SetErrorCode(CFirebirdDatabaseLayer::TranslateErrorCode(nSqlCode));
	}
}

bool CFirebirdDatabaseLayer::IsAvailable()
{
	bool bAvailable = false;
	CFirebirdInterface* pInterface = new CFirebirdInterface();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

