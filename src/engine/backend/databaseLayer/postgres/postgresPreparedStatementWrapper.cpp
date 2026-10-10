#include "postgresPreparedStatementWrapper.h"
#include "postgresResultSet.h"
#include "postgresDatabaseLayer.h"

#include "backend/databaseLayer/databaseErrorCodes.h"

#include <algorithm> // std::max — the highest `$N` of a statement
#include <cstdlib>   // std::strtol — the affected-row count off libpq's ASCII buffer

namespace {

wxString SqlStateOf(ibInterfacePostgres* iface, PGresult* result)
{
	if (iface == nullptr || result == nullptr)
		return wxString();
	const char* state = iface->GetPQresultErrorField()(result, PG_DIAG_SQLSTATE);
	return state != nullptr ? wxString::FromAscii(state) : wxString();
}

}

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

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, const ibString& strValue)
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

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, const ibDateTimeParts& date)
{
	m_Parameters.SetParam(nPosition, date);
}

void ibPreparedStatementPostgresWrapper::SetParam(int nPosition, bool bValue)
{
	m_Parameters.SetParam(nPosition, bValue);
}

// ⭐ BOTH SPELLINGS OF A PARAMETER — `?`, which TranslateSQL numbers, and `$N`, which this driver's own
// dialect asks the query renderer for (ibParamStyle::DollarN) and which arrives already numbered. Counting
// only `?` gave every rendered statement no parameters at all: SetParam found no statement to hand a value
// to and dropped it, and the server was asked to bind 0 of 1 (2026-10-01, the first application-server run
// on PostgreSQL — "Bind message supplies 0 parameters, but prepared statement requires 1").
int ibPreparedStatementPostgresWrapper::GetParameterCount()
{
	int nParameterCount = 0;
	int nHighestDollar = 0;   // `$N` may repeat a number; the statement takes as many as the highest one
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
		else if ((wxT('$') == c) && !bInStringLiteral) {
			int nNumber = 0;
			unsigned int j = i + 1;
			for (; j < len; j++) {
#ifndef _WXSTRING_COMPARE_STRING_
				const auto& d = stl_strSQL.at(j);
#else
				const auto& d = m_strSQL.at(j);
#endif
				if (d < wxT('0') || d > wxT('9'))
					break;
				nNumber = nNumber * 10 + static_cast<int>(d - wxT('0'));
			}
			if (j > i + 1) {          // `$` followed by digits — a placeholder, not a `$` of some other use
				nHighestDollar = std::max(nHighestDollar, nNumber);
				i = j - 1;
			}
		}
	}

	return nParameterCount + nHighestDollar;
}

void ibPreparedStatementPostgresWrapper::Deallocate()
{
	if (m_pDatabase == nullptr || m_strStatementName.IsEmpty())
		return;
	const wxCharBuffer sql = ConvertToUnicodeStream(wxT("DEALLOCATE \"") + m_strStatementName + wxT("\""));
	PGresult* const pResult = m_pInterface->GetPQexec()(m_pDatabase, sql);
	// A refusal is not reported: the statement then lives until its connection closes, as every one did before.
	// (Inside an aborted transaction the server takes nothing but its end — a statement closed there stays.)
	if (pResult != nullptr)
		m_pInterface->GetPQclear()(pResult);
}

ibBackendDatabaseException::Kind ibPreparedStatementPostgresWrapper::ClassifyDatabaseError(int nativeCode) const
{
	(void)nativeCode;
	return ibDatabaseLayerPostgres::ClassifySqlState(m_sqlState);
}

int ibPreparedStatementPostgresWrapper::DoRunQuery()
{
	m_sqlState.clear();
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
			m_sqlState = SqlStateOf(m_pInterface, pResult);
			SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(status, m_pInterface->GetPQresultErrorField()(pResult, PG_DIAG_SQLSTATE)));
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
	m_sqlState.clear();
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
			m_sqlState = SqlStateOf(m_pInterface, pResult);
			SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(status, m_pInterface->GetPQresultErrorField()(pResult, PG_DIAG_SQLSTATE)));
			SetErrorMessage(ConvertFromUnicodeStream(m_pInterface->GetPQresultErrorMessage()(pResult)));
		}
		else
		{
			delete[]paramValues;
			delete[]paramLengths;
			delete[]paramFormats;

			ibDatabaseResultSetPostgres* pResultSet = new ibDatabaseResultSetPostgres(m_pInterface, pResult);
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


