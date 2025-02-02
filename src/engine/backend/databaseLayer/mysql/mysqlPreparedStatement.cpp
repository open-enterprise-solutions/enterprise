#include "mysqlPreparedStatement.h"
#include "mysqlDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

CMysqlPreparedStatement::CMysqlPreparedStatement(CMysqlInterface* pInterface)
	: IPreparedStatement()
{
	m_pInterface = pInterface;
	m_Statements.clear();
}

CMysqlPreparedStatement::CMysqlPreparedStatement(CMysqlInterface* pInterface, MYSQL_STMT* pStatement)
	: IPreparedStatement()
{
	m_pInterface = pInterface;
	AddPreparedStatement(pStatement);
}

CMysqlPreparedStatement::~CMysqlPreparedStatement()
{
	Close();
}


void CMysqlPreparedStatement::Close()
{
	CloseResultSets();

	// Free the statements
	MysqlStatementWrapperArray::iterator start = m_Statements.begin();
	MysqlStatementWrapperArray::iterator stop = m_Statements.end();

	while (start != stop)
	{
		if ((*start) != nullptr)
		{
			CMysqlPreparedStatementWrapper* pWrapper = (CMysqlPreparedStatementWrapper*)(*start);
			wxDELETE(pWrapper);
			(*start) = nullptr;
		}
		start++;
	}
}

void CMysqlPreparedStatement::AddPreparedStatement(MYSQL_STMT* pStatement)
{
	CMysqlPreparedStatementWrapper* pStatementWrapper = new CMysqlPreparedStatementWrapper(m_pInterface, pStatement);
	if (pStatementWrapper)
		pStatementWrapper->SetEncoding(GetEncoding());
	m_Statements.push_back(pStatementWrapper);
}

// get field
void CMysqlPreparedStatement::SetParamInt(int nPosition, int nValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition, nValue);
	}
}

void CMysqlPreparedStatement::SetParamDouble(int nPosition, double dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition, dblValue);
	}
}

void CMysqlPreparedStatement::SetParamNumber(int nPosition, const number_t &numValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition, numValue);
	}
}

void CMysqlPreparedStatement::SetParamString(int nPosition, const wxString& strValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition, strValue);
	}
}

void CMysqlPreparedStatement::SetParamNull(int nPosition)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition);
	}
}

void CMysqlPreparedStatement::SetParamBlob(int nPosition, const void* pData, long nDataLength)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition, pData, nDataLength);
	}
}

void CMysqlPreparedStatement::SetParamDate(int nPosition, const wxDateTime& dateValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition, dateValue);
	}
}

void CMysqlPreparedStatement::SetParamBool(int nPosition, bool bValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex]->SetParam(nPosition, bValue);
	}
}

int CMysqlPreparedStatement::GetParameterCount()
{
	MysqlStatementWrapperArray::iterator start = m_Statements.begin();
	MysqlStatementWrapperArray::iterator stop = m_Statements.end();

	int nParameters = 0;
	while (start != stop)
	{
		nParameters += ((CMysqlPreparedStatementWrapper*)(*start))->GetParameterCount();
		start++;
	}
	return nParameters;
}

int CMysqlPreparedStatement::RunQuery()
{
	MysqlStatementWrapperArray::iterator start = m_Statements.begin();
	MysqlStatementWrapperArray::iterator stop = m_Statements.end();

	int nRows = -1;
	while (start != stop)
	{
		nRows = ((CMysqlPreparedStatementWrapper*)(*start))->DoRunQuery();
		if (((CMysqlPreparedStatementWrapper*)(*start))->GetErrorCode() != DATABASE_LAYER_OK)
		{
			SetErrorCode(((CMysqlPreparedStatementWrapper*)(*start))->GetErrorCode());
			SetErrorMessage(((CMysqlPreparedStatementWrapper*)(*start))->GetErrorMessage());
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		start++;
	}
	return nRows;
}

IDatabaseResultSet* CMysqlPreparedStatement::RunQueryWithResults()
{
	if (m_Statements.size() > 0)
	{
		for (unsigned int i = 0; i < (m_Statements.size() - 1); i++)
		{
			CMysqlPreparedStatementWrapper* pStatement = m_Statements[i];
			pStatement->DoRunQuery();
			if (pStatement->GetErrorCode() != DATABASE_LAYER_OK)
			{
				SetErrorCode(pStatement->GetErrorCode());
				SetErrorMessage(pStatement->GetErrorMessage());
				ThrowDatabaseException();
				return nullptr;
			}
		}

		CMysqlPreparedStatementWrapper* pLastStatement = m_Statements[m_Statements.size() - 1];
		IDatabaseResultSet* pResults = pLastStatement->DoRunQueryWithResults();
		if (pLastStatement->GetErrorCode() != DATABASE_LAYER_OK)
		{
			SetErrorCode(pLastStatement->GetErrorCode());
			SetErrorMessage(pLastStatement->GetErrorMessage());
			ThrowDatabaseException();
		}
		LogResultSetForCleanup(pResults);
		return pResults;
	}
	else
		return nullptr;
}

int CMysqlPreparedStatement::FindStatementAndAdjustPositionIndex(int* pPosition)
{
	if (m_Statements.size() == 0)
		return 0;

	// Go through all the elements in the vector
	// Get the number of parameters in each statement
	// Adjust the nPosition for the the broken up statements
	for (unsigned int i = 0; i < m_Statements.size(); i++)
	{
		int nParametersInThisStatement = m_Statements[i]->GetParameterCount();

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

