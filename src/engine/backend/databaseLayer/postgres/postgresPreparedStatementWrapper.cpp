#include "postgresPreparedStatementWrapper.h"
#include "postgresResultSet.h"
#include "postgresDatabaseLayer.h"

#include "backend/databaseLayer/databaseErrorCodes.h"

#include <cstdlib>   // std::strtol — the affected-row count off libpq's ASCII buffer

ibPreparedStatementPostgresWrapper::ibPreparedStatementPostgresWrapper(ibInterfacePostgres* pInterface, PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName)
	: ibDatabaseErrorReporter(), m_strSQL(strSQL), m_strStatementName(strStatementName)
{
	m_pInterface = pInterface;
	m_pDatabase = pDatabase;
}

ibPreparedStatementPostgresWrapper::~ibPreparedStatementPostgresWrapper()
{
}

// set field
void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, int nValue)
{
	m_Parameters.SetParam(nPosition, nValue);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, double dblValue)
{
	m_Parameters.SetParam(nPosition, dblValue);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, const ibNumber& dblValue)
{
	m_Parameters.SetParam(nPosition, dblValue);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, const wxString& strValue)
{
	m_Parameters.SetParam(nPosition, strValue);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition)
{
	m_Parameters.SetParam(nPosition);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, const void* pData, long nDataLength)
{
	m_Parameters.SetParam(nPosition, pData, nDataLength);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, const wxDateTime& dateValue)
{
	m_Parameters.SetParam(nPosition, dateValue);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, bool bValue)
{
	m_Parameters.SetParam(nPosition, bValue);
}

int ibPreparedStatementPostgresWrapper::GetParameterCount()
{
	int nParameterCount = 0;
	bool bInStringLiteral = false;

	unsigned int len = m_strSQL.length();

#ifndef _WXSTRING_COMPARE_STRING_
	const auto& stl_strSQL = m_strSQL.ToStdWstring();
#endif // !_WXSTRING_COMPARE_STRING_

	for (unsigned int i = 0; i < len; i++)
	{
#ifndef _WXSTRING_COMPARE_STRING_
		const auto& c = stl_strSQL.at(i);
#else
		const auto& c = m_strSQL.at(i);
#endif

		if (wxT('\'') == c) {
			// Signify that we are inside a string literal inside the SQL
			bInStringLiteral = !bInStringLiteral;
		}
		else if ((wxT('?') == c) && !bInStringLiteral) {
			nParameterCount++;
		}
	}

	return nParameterCount;
}

int ibPreparedStatementPostgresWrapper::DoRunQuery()
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
			SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(status));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQresultErrorMessage()(pResult)));
		}

		if (GetErrorCode() == DATABASE_LAYER_OK)
		{
			// Straight off libpq's ASCII buffer — this runs on every DML statement, and the
			// count is a decimal digit string, so neither the charset conversion nor the
			// wxString it fed was buying anything. Matches DoRunQuery in the layer.
			const char* const cmdTuples = m_pInterface->GetPQcmdTuples()(pResult);
			if (cmdTuples != nullptr && *cmdTuples != '\0')
				nRows = std::strtol(cmdTuples, nullptr, 10);
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

ibDatabaseResultSet* ibPreparedStatementPostgresWrapper::DoRunQueryWithResults()
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
			SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(status));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQresultErrorMessage()(pResult)));
		}
		else
		{
			delete[]paramValues;
			delete[]paramLengths;
			delete[]paramFormats;

			ibDatabaseResultSetPostgres* pResultSet = new ibDatabaseResultSetPostgres(m_pInterface, pResult);
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


