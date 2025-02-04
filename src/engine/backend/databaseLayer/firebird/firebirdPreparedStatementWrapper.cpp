#include "firebirdPreparedStatementWrapper.h"
#include "firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "firebirdResultSet.h"

CFirebirdPreparedStatementWrapper::CFirebirdPreparedStatementWrapper(CFirebirdInterface* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction, const wxString& strSQL)
	: CDatabaseErrorReporter(), m_strSQL(strSQL)
{
	m_pInterface = pInterface;
	m_pDatabase = pDatabase;
	m_pTransaction = pTransaction;

	m_pStatement = NULL;
	m_pParameters = NULL;
	m_pParameterCollection = NULL;
	m_bManageStatement = true;
	m_bManageTransaction = false;

	if (GetErrorCode() != DATABASE_LAYER_OK) ThrowDatabaseException();
}

CFirebirdPreparedStatementWrapper::~CFirebirdPreparedStatementWrapper()
{
	ResetErrorCodes();

	if (m_pParameterCollection) { wxDELETE(m_pParameterCollection); }

	if (m_pStatement && m_bManageStatement)
	{
		int nReturn = m_pInterface->GetIscDsqlFreeStatement()(m_Status, &m_pStatement, DSQL_drop);
		if (nReturn != 0)
		{
			wxLogError(_("Error calling isc_dsql_free_statement"));
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
	}
}

bool CFirebirdPreparedStatementWrapper::Prepare(const wxString& strSQL)
{
	m_strSQL = strSQL;
	return Prepare();
}

bool CFirebirdPreparedStatementWrapper::Prepare()
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

	m_pParameterCollection = new CFirebirdParameterCollection(m_pInterface, m_pParameters);
	m_pParameterCollection->SetEncoding(GetEncoding());

	return true;
}

// set field
void CFirebirdPreparedStatementWrapper::SetParam(int nPosition, int nValue)
{
	m_pParameterCollection->SetParam(nPosition, nValue);
}

void CFirebirdPreparedStatementWrapper::SetParam(int nPosition, double dblValue)
{
	m_pParameterCollection->SetParam(nPosition, dblValue);
}

void CFirebirdPreparedStatementWrapper::SetParam(int nPosition, const number_t& dblValue)
{
	m_pParameterCollection->SetParam(nPosition, dblValue);
}

void CFirebirdPreparedStatementWrapper::SetParam(int nPosition, const wxString& strValue)
{
	m_pParameterCollection->SetParam(nPosition, strValue);
}

void CFirebirdPreparedStatementWrapper::SetParam(int nPosition)
{
	m_pParameterCollection->SetParam(nPosition);
}

void CFirebirdPreparedStatementWrapper::SetParam(int nPosition, const void* pData, long nDataLength)
{
	m_pParameterCollection->SetParam(nPosition, pData, nDataLength);
}

void CFirebirdPreparedStatementWrapper::SetParam(int nPosition, const wxDateTime& dateValue)
{
	m_pParameterCollection->SetParam(nPosition, dateValue);
}

void CFirebirdPreparedStatementWrapper::SetParam(int nPosition, bool bValue)
{
	m_pParameterCollection->SetParam(nPosition, bValue);
}

int CFirebirdPreparedStatementWrapper::GetParameterCount()
{
	if (m_pParameters == NULL)
		return 0;
	else
		return m_pParameters->sqld;
}

int CFirebirdPreparedStatementWrapper::DoRunQuery()
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

IDatabaseResultSet* CFirebirdPreparedStatementWrapper::DoRunQueryWithResults()
{
	ResetErrorCodes();

	XSQLDA* pOutputSqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(1));
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
	CFirebirdResultSet* pResultSet = new CFirebirdResultSet(m_pInterface, m_pDatabase, m_pTransaction, m_pStatement, pOutputSqlda);
	if (pResultSet)
		pResultSet->SetEncoding(GetEncoding());
	if (pResultSet->GetErrorCode() != DATABASE_LAYER_OK)
	{
		SetErrorCode(pResultSet->GetErrorCode());
		SetErrorMessage(pResultSet->GetErrorMessage());

		// Wrap the result set deletion in try/catch block if using exceptions.
		// We want to make sure the original error gets to the user
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		try
		{
#endif
			delete pResultSet;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		}
		catch (DatabaseLayerException& e)
		{
		}
#endif

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

		// Wrap the result set deletion in try/catch block if using exceptions.
		//We want to make sure the isc_dsql_execute2 error gets to the user
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		try
		{
#endif
			delete pResultSet;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		}
		catch (DatabaseLayerException& e)
		{
		}
#endif
		ThrowDatabaseException();
		return NULL;
	}

	m_bManageStatement = true;
	m_bManageTransaction = false;

	return pResultSet;
}

bool CFirebirdPreparedStatementWrapper::IsSelectQuery()
{
	wxString strLocalCopy = m_strSQL;
	strLocalCopy.Trim(false);
	strLocalCopy.MakeUpper();
	return strLocalCopy.StartsWith(_("SELECT "));
}

void CFirebirdPreparedStatementWrapper::InterpretErrorCodes()
{
	wxLogDebug(_("FirebirdPreparesStatementWrapper::InterpretErrorCodes()\n"));

	long nSqlCode = m_pInterface->GetIscSqlcode()(m_Status);
	SetErrorCode(CFirebirdDatabaseLayer::TranslateErrorCode(nSqlCode));
	SetErrorMessage(CFirebirdDatabaseLayer::TranslateErrorCodeToString(m_pInterface, nSqlCode, m_Status));
}

