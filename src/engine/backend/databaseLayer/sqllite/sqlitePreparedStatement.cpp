#include "sqlitePreparedStatement.h"
#include "sqliteResultSet.h"
#include "sqliteDatabaseLayer.h"

#include "backend/databaseLayer/databaseErrorCodes.h"

// ctor
ibPreparedStatementSQLite::ibPreparedStatementSQLite(sqlite3* pDatabase)
	: ibPreparedStatement()
{
	m_pDatabase = pDatabase;
}

ibPreparedStatementSQLite::ibPreparedStatementSQLite(sqlite3* pDatabase, sqlite3_stmt* pStatement)
	: ibPreparedStatement()
{
	m_pDatabase = pDatabase;
	m_Statements.push_back(pStatement);
}

ibPreparedStatementSQLite::ibPreparedStatementSQLite(sqlite3* pDatabase, StatementVector statements)
	: ibPreparedStatement(), m_Statements(statements)
{
	m_pDatabase = pDatabase;
}

// dtor
ibPreparedStatementSQLite::~ibPreparedStatementSQLite()
{
	Close();
}

void ibPreparedStatementSQLite::Close()
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
void ibPreparedStatementSQLite::AddPreparedStatement(CppSQLite3Statement* pStatement)
{
  m_Statements.push_back(pStatement);
}
*/
void ibPreparedStatementSQLite::AddPreparedStatement(sqlite3_stmt* pStatement)
{
	m_Statements.push_back(pStatement);
}

// get field
void ibPreparedStatementSQLite::SetParamInt(int nPosition, int nValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_int(m_Statements[nIndex], nPosition, nValue);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void ibPreparedStatementSQLite::SetParamDouble(int nPosition, double dblValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_double(m_Statements[nIndex], nPosition, dblValue);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void ibPreparedStatementSQLite::SetParamNumber(int nPosition, const ibNumber &dblValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		// 🛑 A WHOLE NUMBER A DOUBLE CANNOT CARRY IS BOUND AS AN INTEGER. Everything went in as a double, and a
		// double keeps 53 bits: a reference's table id is a kind-typed clsid - sixty bits of it - so on this
		// driver a reference written through the codec read back naming a type nobody registered, and came out
		// EMPTY (which is why no test here ever read a reference out of a table: 2026-09-20, a sequence's border
		// lost its recorder).
		//
		// ONLY such a number. Everything a double does carry goes the way it always did, for two reasons: an
		// INTEGER parameter would turn `Amount / &Count` into SQLite's integer division where it was a real one,
		// and the exactness test below walks the bignum tier - not something to pay on every bind of a journal
		// row.
		//
		// ⚠ Past int64 is NOT a reference left out. `_RTRef` holds a reference kind (0x10..0x1D, clsid.h), which
		// fits; the 0x80.. range is a reserved seam nobody registers; and Firebird REFUSES such a value for a
		// BIGINT outright (firebirdParameter.cpp). Spelling it here as an unsigned int64 would make this driver
		// accept what the default one refuses - a second road - so a number past int64 goes in as a double.
		const double approximate = dblValue.ToDouble();
		long long whole = 0;
		const bool exact = (approximate >= 9007199254740992.0 || approximate <= -9007199254740992.0)   // 2^53
			&& dblValue.ToInt(whole) == 0 && ibNumber(whole) == dblValue;
		int nReturn = exact
			? sqlite3_bind_int64(m_Statements[nIndex], nPosition, static_cast<sqlite3_int64>(whole))
			: sqlite3_bind_double(m_Statements[nIndex], nPosition, approximate);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void ibPreparedStatementSQLite::SetParamString(int nPosition, const wxString& strValue)
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
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void ibPreparedStatementSQLite::SetParamNull(int nPosition)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_null(m_Statements[nIndex], nPosition);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void ibPreparedStatementSQLite::SetParamBlob(int nPosition, const void* pData, long nDataLength)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_blob(m_Statements[nIndex], nPosition, (const void*)pData, nDataLength, SQLITE_STATIC);
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

void ibPreparedStatementSQLite::SetParamDate(int nPosition, const wxDateTime& dateValue)
{
	ResetErrorCodes();

	if (dateValue.IsValid())
	{
		int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
		if (nIndex > -1)
		{
			sqlite3_reset(m_Statements[nIndex]);
			wxCharBuffer valueBuffer = ConvertToUnicodeStream(dateValue.Format(wxT("%Y-%m-%d %H:%M:%S")));
			int nReturn = sqlite3_bind_text(m_Statements[nIndex], nPosition, valueBuffer, -1, SQLITE_TRANSIENT);
			if (nReturn != SQLITE_OK)
			{
				SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
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
				SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
				SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
				ThrowDatabaseException();
			}
		}
	}
}

void ibPreparedStatementSQLite::SetParamBool(int nPosition, bool bValue)
{
	ResetErrorCodes();

	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		sqlite3_reset(m_Statements[nIndex]);
		int nReturn = sqlite3_bind_int(m_Statements[nIndex], nPosition, (bValue ? 1 : 0));
		if (nReturn != SQLITE_OK)
		{
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
		}
	}
}

int ibPreparedStatementSQLite::GetParameterCount()
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

int ibPreparedStatementSQLite::RunQuery()
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
			SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
			SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		start++;
	}

	return sqlite3_changes(m_pDatabase);
}

ibDatabaseResultSet* ibPreparedStatementSQLite::RunQueryWithResults()
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
				ibJournalError(wxT("db.sqlite"),wxT("Error with RunQueryWithResults\n"));
				SetErrorCode(ibDatabaseLayerSQLite::TranslateErrorCode(nReturn));
				SetErrorMessage(ConvertFromUnicodeStream(sqlite3_errmsg(m_pDatabase)));
				ThrowDatabaseException();
				return nullptr;
			}
		}
	}
	// Work off the assumption that only the last statement will return result

	ibDatabaseResultSetSQLite* pResultSet = new ibDatabaseResultSetSQLite(this);

	LogResultSetForCleanup(pResultSet);
	return pResultSet;
}

int ibPreparedStatementSQLite::FindStatementAndAdjustPositionIndex(int* pPosition)
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

