#include "sqliteResultSet.h"
#include "sqlitePreparedStatement.h"
#include "sqliteDatabaseLayer.h"
#include "sqliteResultSetMetaData.h"

#include "backend/databaseLayer/databaseLayerException.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

// ctor
ibDatabaseResultSetSQLite::ibDatabaseResultSetSQLite()
	: ibDatabaseResultSet()
{
	m_pStatement = nullptr;
	m_pSqliteStatement = nullptr;
	m_bManageStatement = false;
}

ibDatabaseResultSetSQLite::ibDatabaseResultSetSQLite(ibPreparedStatementSQLite* pStatement, bool bManageStatement /*= false*/)
	: ibDatabaseResultSet()
{
	m_pStatement = pStatement;
	m_pSqliteStatement = m_pStatement->GetLastStatement();
	m_bManageStatement = bManageStatement;

	// Populate field lookup map
	int nFieldCount = sqlite3_column_count(m_pSqliteStatement);
	for (int i = 0; i < nFieldCount; i++)
	{
		wxString strField = ConvertFromUnicodeStream(sqlite3_column_name(m_pSqliteStatement, i));
		m_FieldLookupMap[strField] = i;
	}
}

// dtor
ibDatabaseResultSetSQLite::~ibDatabaseResultSetSQLite()
{
	Close();
}


void ibDatabaseResultSetSQLite::Close()
{
	CloseMetaData();

	if (m_bManageStatement)
	{
		if (m_pStatement != nullptr)
		{
			m_pStatement->Close();
			wxDELETE(m_pStatement);
		}
	}
}


bool ibDatabaseResultSetSQLite::Next()
{
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	int nReturn = sqlite3_step(m_pSqliteStatement);

	if (nReturn != SQLITE_ROW)
		sqlite3_reset(m_pSqliteStatement);

	if ((nReturn != SQLITE_ROW) && (nReturn != SQLITE_DONE))
	{
		wxLogError(wxT("Error with RunQueryWithResults\n"));
		SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
#if SQLITE_VERSION_NUMBER>=3002002
		// sqlite3_db_handle wasn't added to the SQLite3 API until version 3.2.2
		SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(sqlite3_db_handle(m_pSqliteStatement))));
#else
		SetErrorMessage(wxT("Unknown error advancing result set"));
#endif
		ThrowDatabaseException();
		return false;
	}

	return (nReturn == SQLITE_ROW);
}


// get field
int ibDatabaseResultSetSQLite::GetResultInt(int nField)
{
	int nValue = -1;
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	nValue = sqlite3_column_int(m_pSqliteStatement, nField - 1);

	return nValue;
}

wxString ibDatabaseResultSetSQLite::GetResultString(int nField)
{
	wxString strValue = wxEmptyString;
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	strValue = ConvertFromUnicodeStream((const char*)(sqlite3_column_text(m_pSqliteStatement, nField - 1)));

	return strValue;
}

long long ibDatabaseResultSetSQLite::GetResultLong(int nField)
{
	long long nValue = -1;
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	nValue = sqlite3_column_int(m_pSqliteStatement, nField - 1);

	return nValue;
}

bool ibDatabaseResultSetSQLite::GetResultBool(int nField)
{
	int nValue = 0;
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	nValue = sqlite3_column_int(m_pSqliteStatement, nField - 1);

	return (nValue != 0);
}

wxDateTime ibDatabaseResultSetSQLite::GetResultDate(int nField)
{
	// Don't use nField-1 here since GetResultString will take care of that
	wxString strDate = GetResultString(nField);
	wxDateTime date;
	// First check for the 2-digit year format
	if (date.ParseFormat(strDate, wxT("%m/%d/%y %H:%M:%S")))
	{
		return date;
	}
	else if (date.ParseDateTime(strDate))
	{
		return date;
	}
	else if (date.ParseDate(strDate))
	{
		return date;
	}
	else
	{
		return wxDefaultDateTime;
	}
}

double ibDatabaseResultSetSQLite::GetResultDouble(int nField)
{
	double dblValue = -1;
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	dblValue = sqlite3_column_double(m_pSqliteStatement, nField - 1);

	return dblValue;
}

ibNumber ibDatabaseResultSetSQLite::GetResultNumber(int nField)
{
	ibNumber dblValue = -1;
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	dblValue = sqlite3_column_double(m_pSqliteStatement, nField - 1);

	return dblValue;
}

void* ibDatabaseResultSetSQLite::GetResultBlob(int nField, wxMemoryBuffer& buffer)
{
	int nLength = 0;
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	nLength = sqlite3_column_bytes(m_pSqliteStatement, nField - 1);
	if (nLength < 1)
	{
		wxMemoryBuffer tempBuffer(0);
		tempBuffer.SetDataLen(0);
		tempBuffer.SetBufSize(0);
		buffer = tempBuffer;

		return nullptr;
	}

	const void* pBlob = sqlite3_column_blob(m_pSqliteStatement, nField - 1);

	wxMemoryBuffer tempBuffer(nLength);
	void* pBuffer = tempBuffer.GetWriteBuf(nLength);
	memcpy(pBuffer, pBlob, nLength);
	tempBuffer.UngetWriteBuf(nLength);
	tempBuffer.SetDataLen(nLength);
	tempBuffer.SetBufSize(nLength);

	buffer = tempBuffer;
	return buffer.GetData();
}

bool ibDatabaseResultSetSQLite::IsFieldNull(int nField)
{
	if (m_pSqliteStatement == nullptr)
		m_pSqliteStatement = m_pStatement->GetLastStatement();
	return (nullptr == sqlite3_column_text(m_pSqliteStatement, nField - 1));
}

int ibDatabaseResultSetSQLite::LookupField(const wxString& strField)
{
	StringToIntMap::iterator SearchIterator = std::find_if(m_FieldLookupMap.begin(), m_FieldLookupMap.end(),
		[strField](const auto pair) { return stringUtils::CompareString(pair.first, strField); });

	if (SearchIterator == m_FieldLookupMap.end())
	{
		// Throw rather than return -1: caller code paths in OES routinely
		// pass LookupField's result straight into GetResultXxx with no
		// sentinel check, so a missed field used to surface as a silent
		// wrong-column read. With ibDatabaseLayerException unified into
		// ibBackendException, the throw lands in the same handler chain
		// every other DB error uses.
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			DATABASE_LAYER_FIELD_NOT_IN_RESULTSET,
			/*sqlState*/ wxEmptyString,
			wxT("Field '") + strField + wxT("' not found in the resultset"));
	}
	else
	{
		return ((*SearchIterator).second + 1);  // Add +1 to make the result set 1-based rather than 0-based
	}
}

ibResultSetMetaData* ibDatabaseResultSetSQLite::GetMetaData()
{
	ibResultSetMetaData* pMetaData = new ibResultSetMetaDataSQLite(m_pSqliteStatement);
	LogMetaDataForCleanup(pMetaData);
	return pMetaData;
}

