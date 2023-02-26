#include "firebirdDatabaseLayer.h"
#include <3rdparty/databaseLayer/databaseErrorCodes.h>
#include <3rdparty/databaseLayer/databaseLayerException.h>

#include "engine/ibase.h"

#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
#include "firebirdInterface.h"
#endif

#include "firebirdPreparedStatement.h"
#include "firebirdResultSet.h"

#include <wx/file.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/regex.h>

// ctor()
FirebirdDatabaseLayer::FirebirdDatabaseLayer()
	: DatabaseLayer()
{
	m_pDatabase = NULL;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = NULL;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new FirebirdInterface();
	
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

FirebirdDatabaseLayer::FirebirdDatabaseLayer(const wxString& strDatabase)
	: DatabaseLayer()
{
	m_pDatabase = NULL;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = NULL;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new FirebirdInterface();
	
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

FirebirdDatabaseLayer::FirebirdDatabaseLayer(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: DatabaseLayer()
{
	m_pDatabase = NULL;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = NULL;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new FirebirdInterface();
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

FirebirdDatabaseLayer::FirebirdDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
	: DatabaseLayer()
{
	m_pDatabase = NULL;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = NULL;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new FirebirdInterface();
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

FirebirdDatabaseLayer::FirebirdDatabaseLayer(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword, const wxString& strRole)
	: DatabaseLayer()
{
	m_pDatabase = NULL;

	m_fbNode = new fb_tr_list_t;
	m_fbNode->prev = NULL;

	m_fbNode->m_pTransaction = 0L;

	m_pStatus = new ISC_STATUS_ARRAY();
#ifndef DONT_USE_DYNAMIC_DATABASE_LAYER_LINKING
	m_pInterface = new FirebirdInterface();
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
FirebirdDatabaseLayer::~FirebirdDatabaseLayer()
{
	Close();
	ISC_STATUS_ARRAY* pStatus = (ISC_STATUS_ARRAY*)m_pStatus;
	wxDELETEA(pStatus);
	m_pStatus = NULL;
	wxDELETE(m_pInterface);
	m_pInterface = NULL;
	wxDELETE(m_fbNode);
	m_fbNode = NULL;
}

// open database
bool FirebirdDatabaseLayer::Open(const wxString& strDatabase)
{
	m_strDatabase = strDatabase;
	return Open();
}

bool FirebirdDatabaseLayer::Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strUser = strUser;
	m_strPassword = strPassword;

	return Open(strDatabase);
}

bool FirebirdDatabaseLayer::Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_strServer = strServer;
	m_strUser = strUser;
	m_strPassword = strPassword;

	return Open(strDatabase);
}

bool FirebirdDatabaseLayer::Open()
{
	ResetErrorCodes();

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

	m_pDatabase = NULL;

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
bool FirebirdDatabaseLayer::Close()
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
				m_fbNode->m_pTransaction = NULL;
			}

			fb_tr_list *tr_link = m_fbNode->prev;

			if (m_fbNode->prev) {
				wxDELETE(m_fbNode);
			}

			m_fbNode = tr_link;
		}

		isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;
		int nReturn = m_pInterface->GetIscDetachDatabase()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase);
		m_pDatabase = NULL;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
			return false;
		}
	}

	return true;
}

bool FirebirdDatabaseLayer::IsOpen()
{
	return (m_pDatabase != NULL);
}

// transaction support
void FirebirdDatabaseLayer::BeginTransaction()
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

		int nReturn = m_pInterface->GetIscStartTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction, 1, &pDatabase, 0, NULL);

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

void FirebirdDatabaseLayer::Commit()
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
			// We're done with the transaction, so set it to NULL so that we know that a new transaction must be started if we run any queries
			m_fbNode->m_pTransaction = 0L;

			fb_tr_list *tr_link = m_fbNode->prev;
			wxDELETE(m_fbNode);
			m_fbNode = tr_link;
		}
	}
}

void FirebirdDatabaseLayer::RollBack()
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
			// We're done with the transaction, so set it to NULL so that we know that a new transaction must be started if we run any queries
			m_fbNode->m_pTransaction = NULL;

			fb_tr_list *tr_link = m_fbNode->prev;
			wxDELETE(m_fbNode);
			m_fbNode = tr_link;
		}
	}
}

// query database
int FirebirdDatabaseLayer::RunQuery(const wxString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();
	if (m_pDatabase != NULL)
	{
		wxCharBuffer sqlDebugBuffer = ConvertToUnicodeStream(strQuery);
#ifdef _DEBUG
		wxLogDebug(_("Running query: \"%s\"\n"), (const char*)sqlDebugBuffer);
#endif // !_DEBUG
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

			if (m_fbNode->m_pTransaction == NULL)
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
				//int nReturn = m_pInterface->GetIscDsqlExecuteImmediate()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase, &pTransaction, 0, (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, NULL);
				int nReturn = m_pInterface->GetIscDsqlExecuteImmediate()(*(ISC_STATUS_ARRAY*)m_pStatus, &pDatabase, &pTransaction, GetEncodedStreamLength(*start), (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, NULL);
				m_pDatabase = pDatabase;
				m_fbNode->m_pTransaction = pTransaction;
				if (nReturn != 0)
				{
					InterpretErrorCodes();
					// Manually try to rollback the transaction rather than calling the member RollBack function
					//  so that we can ignore the error messages
					isc_tr_handle pTransaction = (isc_tr_handle)m_fbNode->m_pTransaction;
					m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pTransaction);
					m_fbNode->m_pTransaction = NULL;

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
		wxLogError(_("Database handle is NULL"));
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}
}

DatabaseResultSet* FirebirdDatabaseLayer::RunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();
	if (m_pDatabase != NULL)
	{
		wxCharBuffer sqlDebugBuffer = ConvertToUnicodeStream(strQuery);
#if _DEBUG 
		wxLogDebug(_("Running query: \"%s\""), (const char*)sqlDebugBuffer);
#endif
		wxArrayString QueryArray = ParseQueries(strQuery);

		if (QueryArray.size() > 0)
		{
			bool bQuickieTransaction = false;

			if (m_fbNode->m_pTransaction == NULL)
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
						return NULL;
					}
				}

				// Assume that only the last statement in the array returns the result set
				for (unsigned int i = 0; i < QueryArray.size() - 1; i++)
				{
					RunQuery(QueryArray[i], false);
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						ThrowDatabaseException();
						return NULL;
					}
				}

				// Now commit all the previous queries before calling the query that returns a result set
				if (bQuickieTransaction)
				{
					Commit();
					if (GetErrorCode() != DATABASE_LAYER_OK)
					{
						ThrowDatabaseException();
						return NULL;
					}
				}
			} // End check if there are more than one query in the array

			isc_tr_handle pQueryTransaction = NULL;
			bool bManageTransaction = false;
			if (bQuickieTransaction)
			{
				bManageTransaction = true;

				isc_db_handle pDatabase = (isc_db_handle)m_pDatabase;
				int nReturn = m_pInterface->GetIscStartTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, 1, &pDatabase, 0, NULL);
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

			isc_stmt_handle pStatement = NULL;
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
				return NULL;
			}

			wxCharBuffer sqlBuffer = ConvertToUnicodeStream(QueryArray[QueryArray.size() - 1]);
			nReturn = m_pInterface->GetIscDsqlPrepare()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, &pStatement, 0, (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, NULL);
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				ThrowDatabaseException();
				return NULL;
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
				return NULL;
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
					return NULL;
				}
			}

			// Create the result set object
			FirebirdResultSet* pResultSet = new FirebirdResultSet(m_pInterface, m_pDatabase, pQueryTransaction, pStatement, pOutputSqlda, true, bManageTransaction);
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
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
				try
				{
#endif
					delete pResultSet;
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
				}
				catch (DatabaseLayerException& e)
				{
				}
#endif

				ThrowDatabaseException();
			}

			// Now execute the SQL
			nReturn = m_pInterface->GetIscDsqlExecute()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction, &pStatement, SQL_DIALECT_CURRENT, NULL);
			if (nReturn != 0)
			{
				InterpretErrorCodes();

				// Manually try to rollback the transaction rather than calling the member RollBack function
				//  so that we can ignore the error messages
				m_pInterface->GetIscRollbackTransaction()(*(ISC_STATUS_ARRAY*)m_pStatus, &pQueryTransaction);

				// Wrap the result set deletion in try/catch block if using exceptions.
				//  We want to make sure the isc_dsql_execute error gets to the user
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
				try
				{
#endif
					delete pResultSet;
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
				}
				catch (DatabaseLayerException& e)
				{
				}
#endif

				ThrowDatabaseException();
				return NULL;
			}

			//--------------------------------------------------------------

			LogResultSetForCleanup(pResultSet);
			return pResultSet;
		}
		else
			return NULL;
	}
	else
	{
		wxLogError(_("Database handle is NULL"));
		return NULL;
	}
}

PreparedStatement* FirebirdDatabaseLayer::PrepareStatement(const wxString& strQuery)
{
	ResetErrorCodes();

	FirebirdPreparedStatement* pStatement = FirebirdPreparedStatement::CreateStatement(m_pInterface, m_pDatabase, m_fbNode->m_pTransaction, strQuery, GetEncoding());
	if (pStatement && (pStatement->GetErrorCode() != DATABASE_LAYER_OK))
	{
		SetErrorCode(pStatement->GetErrorCode());
		SetErrorMessage(pStatement->GetErrorMessage());
		wxDELETE(pStatement); // This sets the pointer to NULL after deleting it

		ThrowDatabaseException();
		return NULL;
	}

	LogStatementForCleanup(pStatement);
	return pStatement;
}

bool FirebirdDatabaseLayer::TableExists(const wxString& table)
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
		wxString tableUpperCase = table.Upper();
		wxString query = _("SELECT COUNT(*) FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NULL AND RDB$RELATION_NAME=?;");
		pStatement = PrepareStatement(query);
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

bool FirebirdDatabaseLayer::ViewExists(const wxString& view)
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
		wxString viewUpperCase = view.Upper();
		wxString query = _("SELECT COUNT(*) FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NOT NULL AND RDB$RELATION_NAME=?;");
		pStatement = PrepareStatement(query);
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

wxArrayString FirebirdDatabaseLayer::GetTables()
{
	wxArrayString returnArray;

	DatabaseResultSet* pResult = NULL;
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NULL");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1).Trim());
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

wxArrayString FirebirdDatabaseLayer::GetViews()
{
	wxArrayString returnArray;

	DatabaseResultSet* pResult = NULL;
#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS
	try
	{
#endif
		wxString query = _("SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG=0 AND RDB$VIEW_BLR IS NOT NULL");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1).Trim());
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

wxArrayString FirebirdDatabaseLayer::GetColumns(const wxString& table)
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
		wxString tableUpperCase = table.Upper();
		wxString query = _("SELECT RDB$FIELD_NAME FROM RDB$RELATION_FIELDS WHERE RDB$RELATION_NAME=?;");
		pStatement = PrepareStatement(query);
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

int FirebirdDatabaseLayer::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of Firebird database error code values to DatabaseLayer values
	// For now though, we'll just return the original error code
	return nCode;
}

//wxString FirebirdDatabaseLayer::TranslateErrorCodeToString(FirebirdInterface* pInterface, int nCode, ISC_STATUS_ARRAY status)
wxString FirebirdDatabaseLayer::TranslateErrorCodeToString(FirebirdInterface* pInterface, int nCode, void* status)
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
		strReturn = DatabaseStringConverter::ConvertFromUnicodeStream(szError, (const char*)systemEncoding);
	}

	return strReturn;
}

void FirebirdDatabaseLayer::InterpretErrorCodes()
{
	//wxLogDebug(_("FirebirdDatabaseLayer::InterpretErrorCodes()"));

	long nSqlCode = m_pInterface->GetIscSqlcode()(*(ISC_STATUS_ARRAY*)m_pStatus);
	SetErrorMessage(FirebirdDatabaseLayer::TranslateErrorCodeToString(m_pInterface, nSqlCode, *(ISC_STATUS_ARRAY*)m_pStatus));
	if (nSqlCode < -900)  // Error codes less than -900 indicate that it wasn't a SQL error but an ibase system error
	{
		SetErrorCode(FirebirdDatabaseLayer::TranslateErrorCode(*((ISC_STATUS_ARRAY*)m_pStatus)[1]));
	}
	else
	{
		SetErrorCode(FirebirdDatabaseLayer::TranslateErrorCode(nSqlCode));
	}
}

bool FirebirdDatabaseLayer::IsAvailable()
{
	bool bAvailable = false;
	FirebirdInterface* pInterface = new FirebirdInterface();
	bAvailable = pInterface && pInterface->Init();
	wxDELETE(pInterface);
	return bAvailable;
}

