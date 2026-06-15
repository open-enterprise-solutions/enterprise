#include "sqliteDatabaseLayer.h"

#include "engine/sqlite3.h"

#include "sqliteResultSet.h"
#include "sqlitePreparedStatement.h"

#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <wx/tokenzr.h>
#include <wx/filename.h>

// SQLite SQL dialect — owned by the driver (static definition + virtual access).
const ibDialectDictionary& ibDatabaseLayerSQLite::Dialect()
{
	static const ibDialectDictionary s_dialect = [] {
		ibDialectDictionary d;
		d.m_paramStyle = ibParamStyle::QuestionMark;
		d.m_pagination = ibPagination::LimitOffset;  // LIMIT n OFFSET m
		d.m_boolForm   = ibBoolForm::OneZero;
		d.m_features.m_window = true;                 // SQLite 3.25+
		d.m_alterColumnTemplate = wxEmptyString;      // no in-place type change -> renderer throws
		d.m_alterTableMultiClause = false;            // one ADD/DROP per ALTER — the structure builder splits batches
		d.m_rowLockSuffix = wxEmptyString;            // SQLite locks the whole DB per TX — no row FOR UPDATE
		// type map (SQLite is dynamically typed; these set column affinity)
		d.m_typeBoolean       = wxT("INTEGER");
		d.m_typeBigInt        = wxT("INTEGER");   // SQLite INTEGER is 64-bit
		d.m_typeDate          = wxT("TEXT");
		d.m_typeDateOnly      = wxT("TEXT");
		d.m_typeTime          = wxT("TEXT");
		d.m_typeBlob          = wxT("BLOB");
		d.m_typeBinaryPattern = wxT("BLOB");      // SQLite has no fixed-width binary
		d.m_typeGuid          = wxT("TEXT");
		return d;
	}();
	return s_dialect;
}

const ibDialectDictionary& ibDatabaseLayerSQLite::GetDialect() const
{
	return Dialect();
}

// ctor()
ibDatabaseLayerSQLite::ibDatabaseLayerSQLite()
	: ibDatabaseLayer()
{
	m_pDatabase = nullptr; //&m_Database; //new sqlite3;
	wxCSConv conv(wxT("UTF-8"));
	SetEncoding(&conv);
}

ibDatabaseLayerSQLite::ibDatabaseLayerSQLite(const wxString& strDatabase, bool mustExist /*= false*/)
	: ibDatabaseLayer()
{
	m_pDatabase = nullptr; //new sqlite3;
	wxCSConv conv(wxT("UTF-8"));
	SetEncoding(&conv);
	Open(strDatabase, mustExist);
}

ibDatabaseLayerSQLite::ibDatabaseLayerSQLite(const ibDatabaseLayerSQLite& src)
	: ibDatabaseLayer()
{
	m_pDatabase = nullptr;
	wxCSConv conv(wxT("UTF-8"));
	SetEncoding(&conv);
	if (!src.m_strDatabasePath.IsEmpty())
		Open(src.m_strDatabasePath);
	else
		Open(wxEmptyString, false);
}

// dtor()
ibDatabaseLayerSQLite::~ibDatabaseLayerSQLite()
{
	//wxPrintf(_("~ibDatabaseLayerSQLite()\n"));
	Close();
	//wxDELETE(m_pDatabase);
}

// open database
bool ibDatabaseLayerSQLite::Open(const wxString& strDatabase, bool mustExist)
{
	if (strDatabase != wxT(":memory:") && // :memory: is a special SQLite in-memory database
		mustExist && !(wxFileName::FileExists(strDatabase)))
	{
		SetErrorCode(DATABASE_LAYER_ERROR);
		SetErrorMessage(wxT("The specified database file '") + strDatabase + wxT("' does not exist."));
		ThrowDatabaseException();
		return false;
	}
	return Open(strDatabase);
}

bool ibDatabaseLayerSQLite::Open(const wxString& strDatabase)
{
	ResetErrorCodes();

	m_strDatabasePath = strDatabase;

	wxCharBuffer databaseNameBuffer = ConvertToUnicodeStream(strDatabase);
	sqlite3* pDbPtr = (sqlite3*)m_pDatabase;
	int nReturn = sqlite3_open(databaseNameBuffer, &pDbPtr);
	m_pDatabase = pDbPtr;

	if (nReturn != SQLITE_OK)
	{
		SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
		SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
		ThrowDatabaseException();
		return false;
	}

	// Enable WAL mode for concurrent read/write access from multiple threads
	sqlite3_exec((sqlite3*)m_pDatabase, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
	sqlite3_exec((sqlite3*)m_pDatabase, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);

	return true;
}

// close database  
bool ibDatabaseLayerSQLite::Close()
{
	ResetErrorCodes();

	CloseResultSets();
	CloseStatements();

	if (m_pDatabase != nullptr)
	{
		int nReturn = sqlite3_close((sqlite3*)m_pDatabase);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg((sqlite3*)m_pDatabase)));
			ThrowDatabaseException();
			return false;
		}
		m_pDatabase = nullptr;
	}

	return true;
}

bool ibDatabaseLayerSQLite::IsOpen()
{
	return (m_pDatabase != nullptr);
}

void ibDatabaseLayerSQLite::DoBeginTransaction(const ibTxOptions& opts)
{
	// SQLite is single-writer, file-level locked — no per-TX wait/nowait
	// knob to honour. Options parameter accepted for interface conformance.
	(void)opts;
	wxLogDebug(wxT("Beginning transaction"));
	DoRunQuery(wxT("begin deferred transaction;"), false);
}

void ibDatabaseLayerSQLite::DoCommit()
{
	wxLogDebug(wxT("Commiting transaction"));
	DoRunQuery(wxT("commit transaction;"), false);
}

void ibDatabaseLayerSQLite::DoRollBack()
{
	wxLogDebug(wxT("Rolling back transaction"));
	DoRunQuery(wxT("rollback transaction;"), false);
}

// IsActiveTransaction inherits the base-class default (m_txDepth > 0).

// query database
int ibDatabaseLayerSQLite::DoRunQuery(const wxString& strQuery, bool bParseQuery)
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
		wxString strErrorMessage = wxEmptyString;
		wxCharBuffer sqlBuffer = ConvertToUnicodeStream(*start);
		int nReturn = sqlite3_exec((sqlite3*)m_pDatabase, sqlBuffer, 0, 0, &szErrorMessage);

		if (szErrorMessage != nullptr)
		{
			strErrorMessage = ConvertFromUnicodeStream(szErrorMessage);
			sqlite3_free(szErrorMessage);
		}

		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
			SetErrorMessage(strErrorMessage);
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}

		start++;
	}
	return (sqlite3_changes((sqlite3*)m_pDatabase));
}

ibDatabaseResultSet* ibDatabaseLayerSQLite::DoRunQueryWithResults(const wxString& strQuery)
{
	ResetErrorCodes();

	if (m_pDatabase != nullptr)
	{
		wxArrayString QueryArray = ParseQueries(strQuery);

		for (unsigned int i = 0; i < (QueryArray.size() - 1); i++)
		{
			char* szErrorMessage = nullptr;
			wxString strErrorMessage = wxEmptyString;
			wxCharBuffer sqlBuffer = ConvertToUnicodeStream(QueryArray[i]);
			int nReturn = sqlite3_exec((sqlite3*)m_pDatabase, sqlBuffer, 0, 0, &szErrorMessage);

			if (szErrorMessage != nullptr)
			{
				SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
				strErrorMessage = ConvertFromUnicodeStream(szErrorMessage);
				sqlite3_free(szErrorMessage);
				return nullptr;
			}

			if (nReturn != SQLITE_OK)
			{
				SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(sqlite3_errcode((sqlite3*)m_pDatabase)));
				SetErrorMessage(strErrorMessage);
				ThrowDatabaseException();
				return nullptr;
			}
		}

		// Create a Prepared statement for the last SQL statement and get a result set from it
		ibPreparedStatementSQLite* pStatement = (ibPreparedStatementSQLite*)DoPrepareStatement(QueryArray[QueryArray.size() - 1], false);
		ibDatabaseResultSetSQLite* pResultSet = new ibDatabaseResultSetSQLite(pStatement, true);
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

ibPreparedStatement* ibDatabaseLayerSQLite::DoPrepareStatement(const wxString& strQuery)
{
	return DoPrepareStatement(strQuery, true);
}

ibPreparedStatement* ibDatabaseLayerSQLite::DoPrepareStatement(const wxString& strQuery, bool bLogForCleanup)
{
	ResetErrorCodes();

	if (m_pDatabase != nullptr)
	{
		ibPreparedStatementSQLite* pReturnStatement = new ibPreparedStatementSQLite((sqlite3*)m_pDatabase);
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
					SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
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

bool ibDatabaseLayerSQLite::TableExists(const wxString& table)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;

	// Probe via sqlite_master — if the prepared statement or query
	// throws a structured DB exception (ThrowDatabaseException path),
	// treat the table as "not present" and let the cleanup below
	// release whatever we managed to allocate. Callers that need the
	// distinct error path use a raw ibPreparedStatement themselves;
	// TableExists is the convenience predicate.
	try {
		wxString attach = wxT("sqlite_master"), t = table;
		size_t pos_attach = table.find('.');
		if (pos_attach > 0) {
			attach = table.Left(pos_attach + 1) + attach;
			t = table.Right(table.length() - pos_attach - 1);
		}

		wxString query = wxT("SELECT COUNT(*) FROM " + attach + wxT(" WHERE type='table' AND name=?;"));
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
	}
	catch (const ibBackendDatabaseException&) {
		bReturn = false;
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

	return bReturn;
}

bool ibDatabaseLayerSQLite::ViewExists(const wxString& view)
{
	// Initialize variables
	bool bReturn = false;
	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibPreparedStatement* pStatement = nullptr;
	ibDatabaseResultSet* pResult = nullptr;

	try {
		wxString attach = wxT("sqlite_master"), v = view;
		size_t pos_attach = view.find('.');
		if (pos_attach > 0) {
			attach = view.Left(pos_attach + 1) + attach;
			v = view.Right(view.length() - pos_attach - 1);
		}

		wxString query = wxT("SELECT COUNT(*) FROM " + attach + wxT(" WHERE type='view' AND name=?;"));
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
	}
	catch (const ibBackendDatabaseException&) {
		bReturn = false;
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

	return bReturn;
}

wxArrayString ibDatabaseLayerSQLite::GetTables()
{
	wxArrayString returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT name FROM sqlite_master WHERE type='table';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
		}
	}
	catch (const ibBackendDatabaseException&) {
		// Best-effort enumeration — partial results stay in the array.
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

wxArrayString ibDatabaseLayerSQLite::GetViews()
{
	wxArrayString returnArray;

	ibDatabaseResultSet* pResult = nullptr;
	try {
		wxString query = wxT("SELECT name FROM sqlite_master WHERE type='view';");
		pResult = ExecuteQuery(query);

		while (pResult->Next())
		{
			returnArray.Add(pResult->GetResultString(1));
		}
	}
	catch (const ibBackendDatabaseException&) {
		// Best-effort enumeration — partial results stay in the array.
	}

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

wxArrayString ibDatabaseLayerSQLite::GetColumns(const wxString& table)
{
	wxArrayString returnArray;

	// Keep these variables outside of scope so that we can clean them up
	//  in case of an error
	ibDatabaseResultSet* pResult = nullptr;
	ibResultSetMetaData* pMetaData = nullptr;

	try {
		wxCharBuffer tableNameBuffer = ConvertToUnicodeStream(table);
		wxString query = wxString::Format(wxT("SELECT * FROM '%s' LIMIT 0;"), table.c_str());
		pResult = ExecuteQuery(query);
		pResult->Next();
		pMetaData = pResult->GetMetaData();

		// 1-based
		for (int i = 1; i <= pMetaData->GetColumnCount(); i++)
		{
			returnArray.Add(pMetaData->GetColumnName(i));
		}
	}
	catch (const ibBackendDatabaseException&) {
		// Missing table / bad name → empty column list, cleanup below
		// still runs.
	}

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

int ibDatabaseLayerSQLite::TranslateErrorCode(int nCode)
{
	// Ultimately, this will probably be a map of SQLite database error code values to ibDatabaseLayer values
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

ibBackendDatabaseException::Kind ibDatabaseLayerSQLite::ClassifyDatabaseError(int nativeCode) const
{
	// SQLite returns a single-int result code via sqlite3_errcode().
	// The values are part of SQLite's stable ABI (sqlite3.h SQLITE_*
	// macros) so the integer literals are safe.
	using Kind = ibBackendDatabaseException::Kind;
	switch (nativeCode) {
		case 5:  // SQLITE_BUSY  — database file locked by another process / writer
		case 6:  // SQLITE_LOCKED — table locked by a concurrent connection
			return Kind::Timeout;

		case 19: // SQLITE_CONSTRAINT
			return Kind::Constraint;

		case 1:  // SQLITE_ERROR  — catch-all for SQL errors / missing table
		case 21: // SQLITE_MISUSE — library used incorrectly (usually a bad prepared statement)
			return Kind::Syntax;

		default:
			return Kind::Unknown;
	}
}

