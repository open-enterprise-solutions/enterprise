#include "postgresPreparedStatementWrapper.h"
#include "postgresResultSet.h"
#include "postgresDatabaseLayer.h"

#include "backend/databaseLayer/databaseErrorCodes.h"

CPostgresPreparedStatementWrapper::CPostgresPreparedStatementWrapper(CPostgresInterface* pInterface, PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName)
	: CDatabaseErrorReporter(), m_strSQL(strSQL), m_strStatementName(strStatementName)
{
	m_pInterface = pInterface;
	m_pDatabase = pDatabase;
}

CPostgresPreparedStatementWrapper::~CPostgresPreparedStatementWrapper()
{
}

// set field
void CPostgresPreparedStatementWrapper::SetParam(int nPosition, int nValue)
{
	m_Parameters.SetParam(nPosition, nValue);
}

void CPostgresPreparedStatementWrapper::SetParam(int nPosition, double dblValue)
{
	m_Parameters.SetParam(nPosition, dblValue);
}

void CPostgresPreparedStatementWrapper::SetParam(int nPosition, const number_t& dblValue)
{
	m_Parameters.SetParam(nPosition, dblValue);
}

void CPostgresPreparedStatementWrapper::SetParam(int nPosition, const wxString& strValue)
{
	m_Parameters.SetParam(nPosition, strValue);
}

void CPostgresPreparedStatementWrapper::SetParam(int nPosition)
{
	m_Parameters.SetParam(nPosition);
}

void CPostgresPreparedStatementWrapper::SetParam(int nPosition, const void* pData, long nDataLength)
{
	m_Parameters.SetParam(nPosition, pData, nDataLength);
}

void CPostgresPreparedStatementWrapper::SetParam(int nPosition, const wxDateTime& dateValue)
{
	m_Parameters.SetParam(nPosition, dateValue);
}

void CPostgresPreparedStatementWrapper::SetParam(int nPosition, bool bValue)
{
	m_Parameters.SetParam(nPosition, bValue);
}

int CPostgresPreparedStatementWrapper::GetParameterCount()
{
	int nParameterCount = 0;
	bool bInStringLiteral = false;
	unsigned int len = m_strSQL.length();
	for (unsigned int i = 0; i < len; i++)
	{
		wxChar character = m_strSQL[i];
		if ('\'' == character)
		{
			// Signify that we are inside a string literal inside the SQL
			bInStringLiteral = !bInStringLiteral;
		}
		else if (('?' == character) && !bInStringLiteral)
		{
			nParameterCount++;
		}
	}
	return nParameterCount;
}

int CPostgresPreparedStatementWrapper::DoRunQuery()
{
	long nRows = -1;
	int nParameters = m_Parameters.GetSize();
	char** paramValues = m_Parameters.GetParamValues();
	int* paramLengths = m_Parameters.GetParamLengths();
	int* paramFormats = m_Parameters.GetParamFormats();
	int nResultFormat = 0; // 0 = text, 1 = binary (all or none on the result set, not column based)
	wxCharBuffer statementNameBuffer = ConvertToUnicodeStream(m_strStatementName);
	PGresult* pResult = m_pInterface->GetPQexecPrepared()(m_pDatabase, statementNameBuffer, nParameters, paramValues, paramLengths, paramFormats, nResultFormat);
	if (pResult != nullptr)
	{
		ExecStatusType status = m_pInterface->GetPQresultStatus()(pResult);
		if ((status != PGRES_COMMAND_OK) && (status != PGRES_TUPLES_OK))
		{
			SetErrorCode(CPostgresDatabaseLayer::TranslateErrorCode(status));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQresultErrorMessage()(pResult)));
		}

		if (GetErrorCode() == DATABASE_LAYER_OK)
		{
			wxString rowsAffected = ConvertFromUnicodeStream(m_pInterface->GetPQcmdTuples()(pResult));
			rowsAffected.ToLong(&nRows);
		}
		m_pInterface->GetPQclear()(pResult);
	}
	
	delete[]paramValues;
	delete[]paramLengths;
	delete[]paramFormats;

	if (GetErrorCode() != DATABASE_LAYER_OK) {
		ThrowDatabaseException();
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	return (int)nRows;
}

IDatabaseResultSet* CPostgresPreparedStatementWrapper::DoRunQueryWithResults()
{
	int nParameters = m_Parameters.GetSize();
	char** paramValues = m_Parameters.GetParamValues();
	int* paramLengths = m_Parameters.GetParamLengths();
	int* paramFormats = m_Parameters.GetParamFormats();
	int nResultFormat = 0; // 0 = text, 1 = binary (all or none on the result set, not column based)
	wxCharBuffer statementNameBuffer = ConvertToUnicodeStream(m_strStatementName);
	PGresult* pResult = m_pInterface->GetPQexecPrepared()(m_pDatabase, statementNameBuffer, nParameters, paramValues, paramLengths, paramFormats, nResultFormat);
	if (pResult != nullptr)
	{
		ExecStatusType status = m_pInterface->GetPQresultStatus()(pResult);
		if ((status != PGRES_COMMAND_OK) && (status != PGRES_TUPLES_OK))
		{
			SetErrorCode(CPostgresDatabaseLayer::TranslateErrorCode(status));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQresultErrorMessage()(pResult)));
		}
		else
		{
			delete[]paramValues;
			delete[]paramLengths;
			delete[]paramFormats;

			CPostgresResultSet* pResultSet = new CPostgresResultSet(m_pInterface, pResult);
			pResultSet->SetEncoding(GetEncoding());
			return pResultSet;
		}
		m_pInterface->GetPQclear()(pResult);
	}
	delete[]paramValues;
	delete[]paramLengths;
	delete[]paramFormats;

	ThrowDatabaseException();

	return nullptr;
}


