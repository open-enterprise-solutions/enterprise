#include "sqliteDatabaseLayer.h"

#include "engine/sqlite3.h"

#include "sqliteResultSet.h"
#include "sqlitePreparedStatement.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <wx/tokenzr.h>
#include <wx/filename.h>

// ctor()
CSqliteDatabaseLayer::CSqliteDatabaseLayer()
	: IDatabaseLayer()
{
	m_pDatabase = nullptr; //&m_Database; //new sqlite3;
	wxCSConv conv(_("UTF-8"));
	SetEncoding(&conv);
}

CSqliteDatabaseLayer::CSqliteDatabaseLayer(const wxString& strDatabase, bool mustExist /*= false*/)
	: IDatabaseLayer()
{
	m_pDatabase = nullptr; //new sqlite3;
	wxCSConv conv(_("UTF-8"));
	SetEncoding(&conv);
	Open(strDatabase, mustExist);
}

// dtor()
CSqliteDatabaseLayer::~CSqliteDatabaseLayer()
{
	//wxPrintf(_("~CSqliteDatabaseLayer()\n"));
	Close();
	//wxDELETE(m_pDatabase);
}

// open database
bool CSqliteDatabaseLayer::Open(const wxString& strDatabase, bool mustExist)
{
	if (strDatabase != wxT(":memory:") && // :memory: is a special SQLite in-memory database
		mustExist && !(wxFileName::FileExists(strDatabase)))
	{
		SetErrorCode(DATABASE_LAYER_ERROR);
		SetErrorMessage(_("The specified database file '") + strDatabase + _("' does not exist."));
		ThrowDatabaseException();
		return false;
	}
	return Open(strDatabase);
}

bool CSqliteDatabaseLayer::Open(const wxString& strDatabase)
{
	ResetErrorCodes();

	//if (m_pDatabase == nullptr)
	//  m_pDatabase = new sqlite3;

	wxCharBuffer databaseNameBuffer = ConvertToUnicodeStream(strDatabase);
	sqlite3* pDbPtr = (sqlite3*)m_pDatabase;
	int nReturn = sqlite3_open(databaseNameBuffer, &pDbPtr);
	m_pDatabase = pDbPtr;

	if (nReturn != SQLITE_OK)
	{
		SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}

	return true;
}

// close database  
bool CSqliteDatabaseLayer::Close()
{
	ResetErrorCodes();

	CloseResultSets();
	CloseStatements();

	if (m_pDatabase != nullptr)
	{
		int nReturn = sqlite3_close((sqlite3*)m_pDatabase);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
			ThrowDatabaseException();
			return false;
		}
		m_pDatabase = nullptr;
	}

	return true;
}

bool CSqliteDatabaseLayer::IsOpen()
{
	return (m_pDatabase != nullptr);
}

void CSqliteDatabaseLayer::BeginTransaction()
{
	wxLogDebug(_("Beginning transaction"));
	DoRunQuery(_("begin deferred transaction;"), false);
}

void CSqliteDatabaseLayer::Commit()
{
	wxLogDebug(_("Commiting transaction"));
	DoRunQuery(_("commit transaction;"), false);
}

void CSqliteDatabaseLayer::RollBack()
{
	wxLogDebug(_("Rolling back transaction"));
	DoRunQuery(_("rollback transaction;"), false);
}

// query database
int CSqliteDatabaseLayer::DoRunQuery(const wxString& strQuery, bool bParseQuery)
{
	ResetErrorCodes();

	if (m_pDatabase == nullptr)
		return false;

	wxArrayString QueryArray;
	if (bParseQuery)
		QueryArray = ParseQueries(strQuery);
	else
		QueryArray.push_back(strQuery);

	wxArrayString::iterator start = QueryArray.begin();
	wxArrayString::iterator stop = QueryArray.end();

	while (start != stop)
	{
		char* szErrorMessage = nullptr;
		wxString strErrorMessage = _("");
		wxCharBuffer sqlBuffer = ConvertToUnicodeStream(*start);
		int nReturn = sqlite3_exec((sqlite3*)m_pDatabase, sqlBuffer, 0, 0, &szErrorMessage);

		if (szErrorMessage != nullptr)
		{
			strErrorMessage = ConvertFromUnicodeStream(szErrorMessage);
			sqlite3_free(szErrorMessage);
		}

		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
			SetErrorMessage(strErrorMessage);
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}

		start++;
	}
	return (sqlite3_changes((sqlite3*)m_pDatabase));
}

IDatabaseResultSet* CSqliteDatabaseLayer::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();

	if (m_pDatabase != nullptr)
	{
		wxArrayString QueryArray = ParseQueries(strQuery);

		for (unsigned int i = 0; i < (QueryArray.size() - 1); i++)
		{
			char* szErrorMessage = nullptr;
			wxString strErrorMessage = _("");
			wxCharBuffer sqlBuffer = ConvertToUnicodeStream(QueryArray[i]);
			int nReturn = sqlite3_exec((sqlite3*)m_pDatabase, sqlBuffer, 0, 0, &szErrorMessage);

			if (szErrorMessage != nullptr)
			{
				SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
				strErrorMessage = ConvertFromUnicodeStream(szErrorMessage);
				sqlite3_free(szErrorMessage);
				return nullptr;
			}

			if (nReturn != SQLITE_OK)
			{
				SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
				SetErrorMessage(strErrorMessage);
				ThrowDatabaseException();
				return nullptr;
			}
		}

		// Create a Prepared statement for the last SQL statement and get a result set from it
		CSqlitePreparedStatement* pStatement = (CSqlitePreparedStatement*)DoPrepareStatement(QueryArray[QueryArray.size() - 1], false);
		CSqliteResultSet* pResultSet = new CSqliteResultSet(pStatement, true);
		if (pResultSet)
			pResultSet->SetEncoding(GetEncoding());

		LogResultSetForCleanup(pResultSet);
		return pResultSet;
	}
	else
	{
		return nullptr;
	}
}

IPreparedStatement* CSqliteDatabaseLayer::DoPrepareStatement(const wxString& strQuery)
{
	return DoPrepareStatement(strQuery, true);
}

IPreparedStatement* CSqliteDatabaseLayer::DoPrepareStatement(const wxString& strQuery, bool bLogForCleanup)
{
	ResetErrorCodes();

	if (m_pDatabase != nullptr)
	{
		CSqlitePreparedStatement* pReturnStatement = new CSqlitePreparedStatement((sqlite3*)m_pDatabase);
		if (pReturnStatement)
			pReturnStatement->SetEncoding(GetEncoding());

		wxArrayString QueryArray = ParseQueries(strQuery);

		wxArrayString::iterator start = QueryArray.begin();
		wxArrayString::iterator stop = QueryArray.end();

		while (start != stop)
		{
			const char* szTail = 0;
			wxCharBuffer sqlBuffer;
			do
			{
				sqlite3_stmt* pStatement;
				wxString strSQL;
				if (szTail != 0)
				{
					strSQL = (wxChar*)szTail;
				}
				else
				{
					strSQL = (*start);
				}
				sqlBuffer = ConvertToUnicodeStream(strSQL);
#if SQLITE_VERSION_NUMBER>=3047002
				int nReturn = sqlite3_prepare_v3((sqlite3*)m_pDatabase, sqlBuffer, -1, SQLITE_PREPARE_PERSISTENT, &pStatement, &szTail);
#elif SQLITE_VERSION_NUMBER>=3003009
				int nReturn = sqlite3_prepare_v2((sqlite3*)m_pDatabase, sqlBuffer, -1, &pStatement, &szTail);
#else
				int nReturn = sqlite3_prepare((sqlite3*)m_pDatabase, sqlBuffer, -1, &pStatement, &szTail);
#endif

				if (nReturn != SQLITE_OK)
				{
					SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
					SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
					wxDELETE(pReturnStatement);
					ThrowDatabaseException();
					return nullptr;
				}
				pReturnStatement->AddPreparedStatement(pStatement);

#if wxUSE_UNICODE
			} while (strlen(szTail) > 0);
#else
		} while (wxStrlen(szTail) > 0);
#endif    

			start++;
		}

		if (bLogForCleanup)
			LogStatementForCleanup(pReturnStatement);
		return pReturnStatement;
	}
	else
	{
		return nullptr;
	}
}

bool CSqliteDatabaseLayer::TableExists(const wxString& table)
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
		wxString attach = "sqlite_master", t = table;
		size_t pos_attach = table.find('.');
		if (pos_attach > 0) {
			attach = table.Left(pos_attach + 1) + attach;
			t = table.Right(table.length() - pos_attach - 1);
		}

		wxString query = wxT("SELECT COUNT(*) FROM " + attach + " WHERE type='table' AND name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, t);
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

bool CSqliteDatabaseLayer::ViewExists(const wxString& view)
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
		wxString attach = "sqlite_master", v = view;
		size_t pos_attach = view.find('.');
		if (pos_attach > 0) {
			attach = view.Left(pos_attach + 1) + attach;
			v = view.Right(view.length() - pos_attach - 1);
		}

		wxString query = wxT("SELECT COUNT(*) FROM " + attach + " WHERE type='view' AND name=?;");
		pStatement = DoPrepareStatement(query);
		if (pStatement)
		{
			pStatement->SetParamString(1, v);
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

wxArrayString CSqliteDatabaseLayer::GetTables()
{
	wxArrayString returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT name FROM sqlite_master WHERE type='table';");
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

wxArrayString CSqliteDatabaseLayer::GetViews()
{
	wxArrayString returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxString query = _("SELECT name FROM sqlite_master WHERE type='view';");
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

wxArrayString CSqliteDatabaseLayer::GetColumns(const wxString& table)
{
	wxArrayString returnArray;

	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	IDatabaseResultSet* pResult = nullptr;
	IResultSetMetaData* pMetaData = nullptr;

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		wxCharBuffer tableNameBuffer = ConvertToUnicodeStream(table);
		wxString query = wxString::Format(_("SELECT * FROM '%s' LIMIT 0;"), table.c_str());
		pResult = ExecuteQuery(query);
		pResult->Next();
		pMetaData = pResult->GetMetaData();

		// 1-based
		for (int i = 1; i <= pMetaData->GetColumnCount(); i++)
		{
			returnArray.Add(pMetaData->GetColumnName(i));
		}

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pMetaData != nullptr)
		{
			pResult->CloseMetaData(pMetaData);
			pMetaData = nullptr;
		}

		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pMetaData != nullptr)
	{
		pResult->CloseMetaData(pMetaData);
		pMetaData = nullptr;
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

int CSqliteDatabaseLayer::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of SQLite database error code values to IDatabaseLayer values
	// For now though, we'll just return error
	int nReturn = nCode;
	/*
	switch (nCode)
	{
	  case SQLITE_ERROR:
		nReturn = DATABASE_LAYER_SQL_SYNTAX_ERROR;
		break;
	  case SQLITE_INTERNAL:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_PERM:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_ABORT:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_BUSY:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_LOCKED:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_NOMEM:
		nReturn = DATABASE_LAYER_ALLOCATION_ERROR;
		break;
	  case SQLITE_READONLY:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_INTERRUPT:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_IOERR:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_CORRUPT:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_NOTFOUND:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_FULL:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_CANTOPEN:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_PROTOCOL:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_SCHEMA:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_TOOBIG:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_CONSTRAINT:
		nReturn = DATABASE_LAYER_CONSTRAINT_VIOLATION;
		break;
	  case SQLITE_MISMATCH:
		nReturn = DATABASE_LAYER_INCOMPATIBLE_FIELD_TYPE;
		break;
	  case SQLITE_MISUSE:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_NOLFS:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  case SQLITE_AUTH:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	  default:
		nReturn = DATABASE_LAYER_ERROR;
		break;
	}
	*/
	return nReturn;
}

