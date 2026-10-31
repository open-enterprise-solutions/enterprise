#include "postgresPreparedStatement.h"
#include "postgresDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include <atomic>   // the statement-name counter

ibPreparedStatementPostgres::ibPreparedStatementPostgres(ibInterfacePostgres* pInterface)
	: ibPreparedStatement()
{
	m_pInterface = pInterface;
}

ibPreparedStatementPostgres::ibPreparedStatementPostgres(ibInterfacePostgres* pInterface, PGconn* pDatabase, const ibString& strSQL, const ibString& strStatementName)
	: ibPreparedStatement()
{
	m_pInterface = pInterface;
	AddStatement(pDatabase, strSQL, strStatementName);
}


ibPreparedStatementPostgres::~ibPreparedStatementPostgres()
{
	Close();
}


void ibPreparedStatementPostgres::Close()
{
	CloseResultSets();
	// ⭐ FREED ON THE SERVER TOO. A prepared statement lives on its connection until somebody deallocates it, and
	// nothing here ever did: a pooled connection kept every statement it had prepared for as long as it stayed
	// open — hours, on a server. The layer closes its statements before it closes the connection (Close), so
	// the connection is still there to ask.
	for (size_t i = 0; i < m_Statements.size(); i++)
		m_Statements[i].Deallocate();
	m_Statements.clear();
}

void ibPreparedStatementPostgres::AddStatement(PGconn* pDatabase, const ibString& strSQL, const ibString& strStatementName)
{
	ibPreparedStatementPostgresWrapper Statement(m_pInterface, pDatabase, strSQL, strStatementName);
	m_Statements.push_back(Statement);
}

ibPreparedStatementPostgres* ibPreparedStatementPostgres::CreateStatement(ibInterfacePostgres* pInterface, PGconn* pDatabase, const ibString& strSQL)
{
	std::vector<ibString> Queries = ParseQueries(strSQL);

	std::vector<ibString>::iterator start = Queries.begin();
	std::vector<ibString>::iterator stop = Queries.end();

	ibPreparedStatementPostgres* pStatement = new ibPreparedStatementPostgres(pInterface);
	while (start != stop)
	{
		ibString strName = ibPreparedStatementPostgres::GenerateStatementName();
		pStatement->AddStatement(pDatabase, (*start), strName);
		std::string nameBuffer = ibDatabaseStringConverter::ConvertToUnicodeStream(strName);
		std::string sqlBuffer = ibDatabaseStringConverter::ConvertToUnicodeStream(TranslateSQL((*start)));
		PGresult* pResult = pInterface->GetPQprepare()(pDatabase, nameBuffer.c_str(), sqlBuffer.c_str(), 0, nullptr);
		if (pResult == nullptr)
		{
			delete pStatement;
			return nullptr;
		}

		if (pInterface->GetPQresultStatus()(pResult) != PGRES_COMMAND_OK)
		{
			const char* state = pInterface->GetPQresultErrorField()(pResult, PG_DIAG_SQLSTATE);
			pStatement->SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(pInterface->GetPQresultStatus()(pResult), state),
				ibDatabaseLayerPostgres::ClassifySqlState(state != nullptr ? ibString::FromUTF8(state) : ibString()));
			pStatement->SetErrorMessage(ibDatabaseStringConverter::ConvertFromUnicodeStream(
				pInterface->GetPQresultErrorMessage()(pResult)));
			pInterface->GetPQclear()(pResult);
			pStatement->ThrowDatabaseException();
			return pStatement;
		}
		pInterface->GetPQclear()(pResult);

		start++;
	}

	// Success?  Return the statement
	return pStatement;
}

// set field
void ibPreparedStatementPostgres::SetParamInt(int nPosition, int nValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, nValue);
	}
}

void ibPreparedStatementPostgres::SetParamDouble(int nPosition, double dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, dblValue);
	}
}

void ibPreparedStatementPostgres::SetParamNumber(int nPosition, const ibNumber& dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, dblValue);
	}
}

void ibPreparedStatementPostgres::SetParamString(int nPosition, const ibString& strValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, strValue);
	}
}

void ibPreparedStatementPostgres::SetParamNull(int nPosition)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition);
	}
}

void ibPreparedStatementPostgres::SetParamBlob(int nPosition, const void* pData, long nDataLength)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, pData, nDataLength);
	}
}

void ibPreparedStatementPostgres::SetParamDate(int nPosition, const ibDateTime& dateValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		ibDateTimeParts parts;
		dateValue.ToParts(parts);
		m_Statements[nIndex].SetParam(nPosition, parts);
	}
}

void ibPreparedStatementPostgres::SetParamBool(int nPosition, bool bValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, bValue);
	}
}

int ibPreparedStatementPostgres::GetParameterCount()
{
	int nParameters = 0;

	for (unsigned int i = 0; i < (m_Statements.size()); i++)
	{
		nParameters += m_Statements[i].GetParameterCount();
	}
	return nParameters;
}


int ibPreparedStatementPostgres::RunQuery()
{
	// Iterate through the statements and have them run their queries
	long rows = -1;
	for (unsigned int i = 0; i < (m_Statements.size()); i++) {
		rows = m_Statements[i].DoRunQuery();
		if (m_Statements[i].GetErrorCode() != DATABASE_LAYER_OK) {
			SetErrorCode(m_Statements[i].GetErrorCode(), m_Statements[i].GetErrorKind());
			SetErrorMessage(m_Statements[i].GetErrorMessage());
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
	}
	return rows;
}

ibDatabaseResultSet* ibPreparedStatementPostgres::RunQueryWithResults()
{
	for (unsigned int i = 0; i < (m_Statements.size() - 1); i++) {
		m_Statements[i].DoRunQuery();
		if (m_Statements[i].GetErrorCode() != DATABASE_LAYER_OK) {
			SetErrorCode(m_Statements[i].GetErrorCode(), m_Statements[i].GetErrorKind());
			SetErrorMessage(m_Statements[i].GetErrorMessage());
			ThrowDatabaseException();
			return nullptr;
		}
	}
	ibPreparedStatementPostgresWrapper* pLastStatement = &(m_Statements[m_Statements.size() - 1]);
	ibDatabaseResultSet* pResultSet = pLastStatement->DoRunQueryWithResults();
	if (pLastStatement->GetErrorCode() != DATABASE_LAYER_OK) {
		SetErrorCode(pLastStatement->GetErrorCode(), pLastStatement->GetErrorKind());
		SetErrorMessage(pLastStatement->GetErrorMessage());
		ThrowDatabaseException();
	}

	LogResultSetForCleanup(pResultSet);
	return pResultSet;
}

// ⭐ A NAME NO STATEMENT OF THIS PROCESS HAS HAD — the server keeps a prepared statement for the life of its
// connection (nothing here deallocates one), and the pool hands a connection from thread to thread, so a
// name must never come round again. It was ten digits of rand(), whose state the CRT keeps PER THREAD from
// the same seed: a registry thread drew exactly the names the main thread had already prepared on the same
// connection ("prepared statement … already exists", 2026-10-01, the first application-server run on
// PostgreSQL). A counter cannot repeat.
ibString ibPreparedStatementPostgres::GenerateStatementName()
{
	static std::atomic<unsigned long long> s_counter{ 0 };
	return ibString::Format(ibT("databaselayer_%llu"), ++s_counter);
}

int ibPreparedStatementPostgres::FindStatementAndAdjustPositionIndex(int* pPosition)
{
	if (m_Statements.size() == 0)
		return 0;

	// Go through all the elements in the vector
	// Get the number of parameters in each statement
	// Adjust the nPosition for the the broken up statements
	for (unsigned int i = 0; i < m_Statements.size(); i++)
	{
		int nParametersInThisStatement = m_Statements[i].GetParameterCount();

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

ibString ibPreparedStatementPostgres::TranslateSQL(const ibString& strOriginalSQL)
{
	int nParameterIndex = 1;
	ibString strReturn = ibEmptyString;//strOriginalSQL;
	/*
	int nFound = strReturn.Replace(_("?"), ibString::Format(_("$%d"), nParameterIndex), false);
	while (nFound != 0)
	{
	  nParameterIndex++;
	  nFound = strReturn.Replace(_("?"), ibString::Format(_("$%d"), nParameterIndex), false);
	}
	*/
	bool bInStringLiteral = false;
	unsigned int len = strOriginalSQL.length();
	for (unsigned int i = 0; i < len; i++)
	{
		ibChar character = strOriginalSQL[i];
		if (ibT('\'') == character)
		{
			// Signify that we are inside a string literal inside the SQL
			bInStringLiteral = !bInStringLiteral;
			// Pass the character on to the return string
			strReturn += character;
		}
		else
		{
			if ('?' == character)
			{
				if (bInStringLiteral)
				{
					// Pass the character on to the return string
					strReturn += character;
				}
				else
				{
					// Replace the question mark with a prepared statement placeholder
					strReturn += ibString::Format(ibT("$%d"), nParameterIndex);
					nParameterIndex++;
				}
			}
			else
			{
				// Pass the character on to the return string
				strReturn += character;
			}
		}
	}

	return strReturn;
}

