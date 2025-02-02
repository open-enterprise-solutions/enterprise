#include "sqlitePreparedStatement.h"
#include "sqliteResultSet.h"
#include "sqliteDatabaseLayer.h"

#include "backend/databaseLayer/databaseErrorCodes.h"

// ctor
CSqlitePreparedStatement::CSqlitePreparedStatement(sqlite3* pDatabase)
	: IPreparedStatement()
{
	m_pDatabase = pDatabase;
}

CSqlitePreparedStatement::CSqlitePreparedStatement(sqlite3* pDatabase, sqlite3_stmt* pStatement)
	: IPreparedStatement()
{
	m_pDatabase = pDatabase;
	m_Statements.push_back(pStatement);
}

CSqlitePreparedStatement::CSqlitePreparedStatement(sqlite3* pDatabase, StatementVector statements)
	: IPreparedStatement(), m_Statements(statements)
{
	m_pDatabase = pDatabase;
}

// dtor
CSqlitePreparedStatement::~CSqlitePreparedStatement()
{
	Close();
}

void CSqlitePreparedStatement::Close()
{
	CloseResultSets();

	StatementVector::iterator start = m_Statements.begin();
	StatementVector::iterator stop = m_Statements.end();
	while (start != stop)
	{
		if ((*start) != nullptr)
		{
			sqlite3_finalize((sqlite3_stmt*)(*start));
			(*start) = nullptr;
			//wxDELETE(*start);
		}
		start++;
	}
	m_Statements.Clear();
}
/*
void CSqlitePreparedStatement::AddPreparedStatement(CppSQLite3Statement* pStatement)
{
  m_Statements.push_back(pStatement);
}
*/
void CSqlitePreparedStatement::AddPreparedStatement(sqlite3_stmt* pStatement)
{
	m_Statements.push_back(pStatement);
}

// get field
void CSqlitePreparedStatement::SetParamInt(int nPosition, int nValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_int(m_Statements[nIndex], nPosition, nValue);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void CSqlitePreparedStatement::SetParamDouble(int nPosition, double dblValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_double(m_Statements[nIndex], nPosition, dblValue);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void CSqlitePreparedStatement::SetParamNumber(int nPosition, const number_t &dblValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_double(m_Statements[nIndex], nPosition, dblValue.ToDouble());
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void CSqlitePreparedStatement::SetParamString(int nPosition, const wxString& strValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		wxCharBuffer valueBuffer = ConvertToUnicodeStream(strValue);
		int nReturn = sqlite3_bind_text(m_Statements[nIndex], nPosition, valueBuffer, -1, SQLITE_TRANSIENT);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void CSqlitePreparedStatement::SetParamNull(int nPosition)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_null(m_Statements[nIndex], nPosition);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void CSqlitePreparedStatement::SetParamBlob(int nPosition, const void* pData, long nDataLength)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_blob(m_Statements[nIndex], nPosition, (const void*)pData, nDataLength, SQLITE_STATIC);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void CSqlitePreparedStatement::SetParamDate(int nPosition, const wxDateTime& dateValue)
{
	ResetErrorCodes();

	if (dateValue.IsValid())
	{
		int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
		if (nIndex > -1)
		{
			sqlite3_reset(m_Statements[nIndex]);
			wxCharBuffer valueBuffer = ConvertToUnicodeStream(dateValue.Format(_("%Y-%m-%d %H:%M:%S")));
			int nReturn = sqlite3_bind_text(m_Statements[nIndex], nPosition, valueBuffer, -1, SQLITE_TRANSIENT);
			if (nReturn != SQLITE_OK)
			{
				SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
				SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
				ThrowDatabaseException();
			}
		}
	}
	else
	{
		int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
		if (nIndex > -1)
		{
			sqlite3_reset(m_Statements[nIndex]);
			int nReturn = sqlite3_bind_null(m_Statements[nIndex], nPosition);
			if (nReturn != SQLITE_OK)
			{
				SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
				SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
				ThrowDatabaseException();
			}
		}
	}
}

void CSqlitePreparedStatement::SetParamBool(int nPosition, bool bValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_int(m_Statements[nIndex], nPosition, (bValue ? 1 : 0));
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

int CSqlitePreparedStatement::GetParameterCount()
{
	ResetErrorCodes();

	int nReturn = 0;
	StatementVector::iterator start = m_Statements.begin();
	StatementVector::iterator stop = m_Statements.end();
	while (start != stop)
	{
		nReturn += sqlite3_bind_parameter_count((sqlite3_stmt*)(*start));
		start++;
	}
	return nReturn;
}

int CSqlitePreparedStatement::RunQuery()
{
	ResetErrorCodes();

	StatementVector::iterator start = m_Statements.begin();
	StatementVector::iterator stop = m_Statements.end();
	while (start != stop)
	{
		int nReturn = sqlite3_step((sqlite3_stmt*)(*start));

		if (nReturn != SQLITE_ROW)
			sqlite3_reset((sqlite3_stmt*)(*start));

		if ((nReturn != SQLITE_ROW) && (nReturn != SQLITE_DONE))
		{
			SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		start++;
	}

	return sqlite3_changes(m_pDatabase);
}

IDatabaseResultSet* CSqlitePreparedStatement::RunQueryWithResults()
{
	ResetErrorCodes();

	if (m_Statements.size() > 1)
	{
		for (unsigned int i = 0; i < m_Statements.size() - 1; i++)
		{
			int nReturn = sqlite3_step(m_Statements[i]);

			if (nReturn != SQLITE_ROW)
				sqlite3_reset(m_Statements[i]);

			if ((nReturn != SQLITE_ROW) && (nReturn != SQLITE_DONE))
			{
				wxLogError(_("Error with RunQueryWithResults\n"));
				SetErrorCode(CSqliteDatabaseLayer::TranslateErrorCode(nReturn));
				SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
				ThrowDatabaseException();
				return nullptr;
			}
		}
	}
	// Work off the assumption that only the last statement will return result

	CSqliteResultSet* pResultSet = new CSqliteResultSet(this);
	if (pResultSet)
		pResultSet->SetEncoding(GetEncoding());

	LogResultSetForCleanup(pResultSet);
	return pResultSet;
}

int CSqlitePreparedStatement::FindStatementAndAdjustPositionIndex(int* pPosition)
{
	// Don't mess around if there's just one entry in the vector
	if (m_Statements.size() == 0)
		return 0;

	// Go through all the elements in the vector
	// Get the number of parameters in each statement
	// Adjust the nPosition for the the broken up statements
	for (unsigned int i = 0; i < m_Statements.size(); i++)
	{
		int nParametersInThisStatement = sqlite3_bind_parameter_count(m_Statements[i]);
		if (*pPosition > nParametersInThisStatement)
		{
			*pPosition -= nParametersInThisStatement;    // Decrement the position indicator by the number of parameters in this statement
		}
		else
		{
			// We're in the correct statement, return the index
			return i;
		}
	}
	return -1;
}

