#include "mysqlPreparedStatementWrapper.h"
#include "mysqlPreparedStatementResultSet.h"
#include "mysqlDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "engine/errmsg.h"

CMysqlPreparedStatementWrapper::CMysqlPreparedStatementWrapper(CMysqlInterface* pInterface, MYSQL_STMT* pStatement)
	: CDatabaseErrorReporter()
{
	m_pInterface = pInterface;
	m_pStatement = pStatement;
}

CMysqlPreparedStatementWrapper::~CMysqlPreparedStatementWrapper()
{
	Close();
}

void CMysqlPreparedStatementWrapper::Close()
{
	if (m_pStatement != nullptr)
	{
		m_pInterface->GetMysqlStmtClose()(m_pStatement);
		m_pStatement = nullptr;
	}
}

// set field
void CMysqlPreparedStatementWrapper::SetParam(int nPosition, int nValue)
{
	m_Parameters.SetParam(nPosition, nValue);
}

void CMysqlPreparedStatementWrapper::SetParam(int nPosition, double dblValue)
{
	m_Parameters.SetParam(nPosition, dblValue);
}

void CMysqlPreparedStatementWrapper::SetParam(int nPosition, const number_t &numValue)
{
	m_Parameters.SetParam(nPosition, numValue);
}

void CMysqlPreparedStatementWrapper::SetParam(int nPosition, const wxString& strValue)
{
	m_Parameters.SetParam(nPosition, strValue);
}

void CMysqlPreparedStatementWrapper::SetParam(int nPosition)
{
	m_Parameters.SetParam(nPosition);
}

void CMysqlPreparedStatementWrapper::SetParam(int nPosition, const void* pData, long nDataLength)
{
	m_Parameters.SetParam(nPosition, pData, nDataLength);
}

void CMysqlPreparedStatementWrapper::SetParam(int nPosition, const wxDateTime& dateValue)
{
	m_Parameters.SetParam(nPosition, dateValue);
}

void CMysqlPreparedStatementWrapper::SetParam(int nPosition, bool bValue)
{
	m_Parameters.SetParam(nPosition, bValue);
}

int CMysqlPreparedStatementWrapper::GetParameterCount()
{
	return m_pInterface->GetMysqlStmtParamCount()(m_pStatement);
}

int CMysqlPreparedStatementWrapper::DoRunQuery()
{
	MYSQL_BIND* pBoundParameters = m_Parameters.GetMysqlParameterBindings();

	int nBindReturn = m_pInterface->GetMysqlStmtBindParam()(m_pStatement, pBoundParameters);
	if (nBindReturn != 0)
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlStmtErrno()(m_pStatement)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlStmtError()(m_pStatement)));
		wxDELETEA(pBoundParameters);
		ThrowDatabaseException();
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}
	else
	{
		int nReturn = m_pInterface->GetMysqlStmtExecute()(m_pStatement);
		if (nReturn != 0)
		{
			SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlStmtErrno()(m_pStatement)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlStmtError()(m_pStatement)));
			wxDELETEA(pBoundParameters);
			ThrowDatabaseException();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
	}
	wxDELETEA(pBoundParameters);

	return (m_pStatement->affected_rows);
}

IDatabaseResultSet* CMysqlPreparedStatementWrapper::DoRunQueryWithResults()
{
	CMysqlPreparedStatementResultSet* pResultSet = nullptr;
	MYSQL_BIND* pBoundParameters = m_Parameters.GetMysqlParameterBindings();

	if (m_pInterface->GetMysqlStmtBindParam()(m_pStatement, pBoundParameters))
	{
		SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlStmtErrno()(m_pStatement)));
		SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlStmtError()(m_pStatement)));
		wxDELETEA(pBoundParameters);
		ThrowDatabaseException();
		return nullptr;
	}
	else
	{
		if (m_pInterface->GetMysqlStmtExecute()(m_pStatement) != 0)
		{
			SetErrorCode(CMysqlDatabaseLayer::TranslateErrorCode(m_pInterface->GetMysqlStmtErrno()(m_pStatement)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetMysqlStmtError()(m_pStatement)));
			wxDELETEA(pBoundParameters);
			ThrowDatabaseException();
			return nullptr;
		}
		else
		{
			pResultSet = new CMysqlPreparedStatementResultSet(m_pInterface, m_pStatement);
			if (pResultSet)
				pResultSet->SetEncoding(GetEncoding());
		}
	}
	wxDELETEA(pBoundParameters);;

	return pResultSet;
}

