#include "firebirdPreparedStatementWrapper.h"
#include "firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "firebirdResultSet.h"

ibPreparedStatementFirebirdWrapper::ibPreparedStatementFirebirdWrapper(ibInterfaceFirebird* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction, const wxString& strSQL)
	: ibDatabaseErrorReporter(), m_strSQL(strSQL)
{
	m_pInterface = pInterface;
	m_pDatabase = pDatabase;
	m_pTransaction = pTransaction;

	m_pStatement = 0;
	m_pParameters = NULL;
	m_pParameterCollection = NULL;
	m_bManageStatement = true;
	m_bManageTransaction = false;

	if (GetErrorCode() != DATABASE_LAYER_OK) ThrowDatabaseException();
}

ibPreparedStatementFirebirdWrapper::~ibPreparedStatementFirebirdWrapper()
{
	ResetErrorCodes();

	if (m_pParameterCollection) { wxDELETE(m_pParameterCollection); }

	if (m_pStatement && m_bManageStatement)
	{
		int nReturn = m_pInterface->GetIscDsqlFreeStatement()(m_Status, &m_pStatement, DSQL_drop);
		if (nReturn != 0)
		{
			// ⭐ DOES NOT RAISE, and it used to — the one site in this driver where the audit ran the
			// other way. This is a DESTRUCTOR: a statement is most often destroyed while an
			// exception is already travelling (a bind refused, a query failed), and a throw during
			// unwinding is std::terminate, not an error report. The whole point of raising a failed
			// bind is lost if the statement's own cleanup then kills the process instead of letting
			// the reason reach the caller. Freeing a server-side handle is cleanup and follows the
			// project's cleanup rule: report, never throw.
			InterpretErrorCodes();
			ibJournalError(wxT("db.firebird"),wxT("Firebird: isc_dsql_free_statement failed (%s)"), GetErrorMessage());
		}
	}
}

bool ibPreparedStatementFirebirdWrapper::Prepare(const wxString& strSQL)
{
	m_strSQL = strSQL;
	return Prepare();
}

bool ibPreparedStatementFirebirdWrapper::Prepare()
{
	ResetErrorCodes();

	int nReturn = m_pInterface->GetIscDsqlAllocateStatement()(m_Status, &m_pDatabase, &m_pStatement);
	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
		return false;
	}

	m_pParameters = (XSQLDA*)malloc(XSQLDA_LENGTH(1));

	if (m_pParameters == NULL)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
		return false;
	}

	m_pParameters->version = SQLDA_VERSION1;
	m_pParameters->sqln = 1;

	wxCharBuffer sqlBuffer = ConvertToUnicodeStream(m_strSQL);
	nReturn = m_pInterface->GetIscDsqlPrepare()(m_Status, &m_pTransaction, &m_pStatement, 0, (char*)(const char*)sqlBuffer, SQL_DIALECT_CURRENT, m_pParameters);
	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
		return false;
	}

	nReturn = m_pInterface->GetIscDsqlDescribeBind()(m_Status, &m_pStatement, SQL_DIALECT_CURRENT, m_pParameters);

	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
		return false;
	}

	if (m_pParameters->sqld > m_pParameters->sqln)
	{
		int nParameters = m_pParameters->sqld;
		free(m_pParameters);
		m_pParameters = (XSQLDA*)malloc(XSQLDA_LENGTH(nParameters));
		// Checked like the first allocation above: the old block is already freed, so a failure here
		// leaves the member NULL and the very next line would write through it.
		if (m_pParameters == NULL)
		{
			SetErrorCode(DATABASE_LAYER_QUERY_RESULT_ERROR);
			SetErrorMessage(wxT("Out of memory allocating the parameter descriptor"));
			ThrowDatabaseException();
			return false;
		}
		m_pParameters->version = SQLDA_VERSION1;
		m_pParameters->sqln = nParameters;
		nReturn = m_pInterface->GetIscDsqlDescribeBind()(m_Status, &m_pStatement, SQL_DIALECT_CURRENT, m_pParameters);
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
			return false;
		}
	}

	m_pParameterCollection = new ibDatabaseParameterFirebirdCollection(m_pInterface, m_pParameters);
	m_pParameterCollection->SetEncoding(GetEncoding());

	return true;
}

// set field
void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition, int nValue)
{
	m_pParameterCollection->SetParam(nPosition, nValue);
}

void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition, double dblValue)
{
	m_pParameterCollection->SetParam(nPosition, dblValue);
}

void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition, const ibNumber& dblValue)
{
	m_pParameterCollection->SetParam(nPosition, dblValue);
}

void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition, const wxString& strValue)
{
	m_pParameterCollection->SetParam(nPosition, strValue);
}

void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition)
{
	m_pParameterCollection->SetParam(nPosition);
}

void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition, const void* pData, long nDataLength)
{
	m_pParameterCollection->SetParam(nPosition, pData, nDataLength);
}

void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition, const wxDateTime& dateValue)
{
	m_pParameterCollection->SetParam(nPosition, dateValue);
}

void ibPreparedStatementFirebirdWrapper::SetParam(int nPosition, bool bValue)
{
	m_pParameterCollection->SetParam(nPosition, bValue);
}

int ibPreparedStatementFirebirdWrapper::GetParameterCount()
{
	if (m_pParameters == NULL)
		return 0;
	else
		return m_pParameters->sqld;
}

int ibPreparedStatementFirebirdWrapper::DoRunQuery()
{
	ResetErrorCodes();

	// Blob ID values are invalidated between execute calls, so re-create any BLOB parameters now
	if(!m_pParameterCollection->ResetBlobParameters(m_pDatabase, m_pTransaction))
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	int nReturn = m_pInterface->GetIscDsqlExecute()(m_Status, &m_pTransaction, &m_pStatement, SQL_DIALECT_CURRENT, m_pParameters);
	if (nReturn != 0)
	{
		InterpretErrorCodes();
		ThrowDatabaseException();
		return DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	// isc_info_sql_records is a separate round-trip after Execute; on a
	// SELECT it returns nothing useful (insert/update/delete counts are
	// all 0) — skip the call entirely. Saves a network/IPC hop on every
	// query for the most common path.
	if (IsSelectQuery())
		return 0;

	long nRows = 0;
	static char requestedInfoTypes[] = { isc_info_sql_records, isc_info_end };
	char resultBuffer[1024];
	memset(resultBuffer, 0, sizeof(resultBuffer));
	nReturn = m_pInterface->GetIscDsqlSqlInfo()(m_Status, &m_pStatement, sizeof(requestedInfoTypes), requestedInfoTypes, sizeof(resultBuffer), resultBuffer);
	if (nReturn == 0) {
		char* pBufferPosition = resultBuffer + 3;
		while (*pBufferPosition != isc_info_end) {
			char infoType = *pBufferPosition;
			pBufferPosition++;
			short nLength = m_pInterface->GetIscVaxInteger()(pBufferPosition, 2);
			pBufferPosition += 2;
			long infoData = m_pInterface->GetIscVaxInteger()(pBufferPosition, nLength);
			pBufferPosition += nLength;

			if (infoType == isc_info_req_insert_count ||
				infoType == isc_info_req_update_count ||
				infoType == isc_info_req_delete_count)
			{
				nRows += infoData;
			}
		}
	}
	return (int)nRows;
}

ibDatabaseResultSet* ibPreparedStatementFirebirdWrapper::DoRunQueryWithResults()
{
	ResetErrorCodes();

	XSQLDA* pOutputSqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(1));
	if (pOutputSqlda == NULL)
	{
		SetErrorCode(DATABASE_LAYER_QUERY_RESULT_ERROR);
		SetErrorMessage(wxT("Out of memory allocating the result descriptor"));
		ThrowDatabaseException();
		return NULL;
	}
	pOutputSqlda->sqln = 1;
	pOutputSqlda->version = SQLDA_VERSION1;

	// Make sure that we have enough space allocated for the result set
	int nReturn = m_pInterface->GetIscDsqlDescribe()(m_Status, &m_pStatement, SQL_DIALECT_CURRENT, pOutputSqlda);
	if (nReturn != 0)
	{
		free(pOutputSqlda);
		InterpretErrorCodes();
		ThrowDatabaseException();
		return NULL;
	}
	if (pOutputSqlda->sqld > pOutputSqlda->sqln)
	{
		int nColumns = pOutputSqlda->sqld;
		free(pOutputSqlda);
		pOutputSqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(nColumns));
		if (pOutputSqlda == NULL)
		{
			SetErrorCode(DATABASE_LAYER_QUERY_RESULT_ERROR);
			SetErrorMessage(wxT("Out of memory allocating the result descriptor"));
			ThrowDatabaseException();
			return NULL;
		}
		pOutputSqlda->sqln = nColumns;
		pOutputSqlda->version = SQLDA_VERSION1;
		nReturn = m_pInterface->GetIscDsqlDescribe()(m_Status, &m_pStatement, SQL_DIALECT_CURRENT, pOutputSqlda);
		if (nReturn != 0)
		{
			free(pOutputSqlda);
			InterpretErrorCodes();
			ThrowDatabaseException();
			return NULL;
		}
	}

	// Create the result set object
	ibDatabaseResultSetFirebird* pResultSet = new ibDatabaseResultSetFirebird(m_pInterface, m_pDatabase, m_pTransaction, m_pStatement, pOutputSqlda);
	if (pResultSet)
		pResultSet->SetEncoding(GetEncoding());
	if (pResultSet->GetErrorCode() != DATABASE_LAYER_OK)
	{
		SetErrorCode(pResultSet->GetErrorCode());
		SetErrorMessage(pResultSet->GetErrorMessage());

		// Swallow a possible throw from ~ibDatabaseResultSet — the
		// original isc_dsql_* error must reach the caller via
		// ThrowDatabaseException; secondary cleanup throw would mask it.
		try { delete pResultSet; } catch (const ibBackendException&) {}

		ThrowDatabaseException();
	}

	// Blob ID values are invalidated between execute calls, so re-create any BLOB parameters now
	if (!m_pParameterCollection->ResetBlobParameters(m_pDatabase, m_pTransaction)) {
		
		ThrowDatabaseException();
		
		return NULL;
	}

	// Now execute the SQL
	//nReturn = isc_dsql_execute2(m_Status, &m_pTransaction, &m_pStatement, 1, m_pParameters, pOutputSqlda);
	nReturn = m_pInterface->GetIscDsqlExecute()(m_Status, &m_pTransaction, &m_pStatement, SQL_DIALECT_CURRENT, m_pParameters);
	if (nReturn != 0)
	{
		InterpretErrorCodes();

		// Swallow on cleanup — isc_dsql_execute2 error above is the
		// user-visible one (ThrowDatabaseException below); a secondary
		// throw from the result-set dtor would mask it.
		try { delete pResultSet; } catch (const ibBackendException&) {}
		ThrowDatabaseException();
		return NULL;
	}

	m_bManageStatement = true;
	m_bManageTransaction = false;

	return pResultSet;
}

bool ibPreparedStatementFirebirdWrapper::IsSelectQuery()
{
	wxString strLocalCopy = m_strSQL;
	strLocalCopy.Trim(false);
	strLocalCopy.MakeUpper();
	return strLocalCopy.StartsWith(wxT("SELECT "));
}

void ibPreparedStatementFirebirdWrapper::InterpretErrorCodes()
{
	ibJournalInfo(wxT("db.firebird"),wxT("FirebirdPreparesStatementWrapper::InterpretErrorCodes()\n"));

	long nSqlCode = m_pInterface->GetIscSqlcode()(m_Status);
	SetErrorCode(ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode));
	SetErrorMessage(ibDatabaseLayerFirebird::TranslateErrorCodeToString(m_pInterface, nSqlCode, m_Status));
}

