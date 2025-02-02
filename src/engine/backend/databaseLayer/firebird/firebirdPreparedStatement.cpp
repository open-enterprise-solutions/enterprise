#include "firebirdPreparedStatement.h"
#include "firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

// ctor()
CFirebirdPreparedStatement::CFirebirdPreparedStatement(CFirebirdInterface* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction)
	: IPreparedStatement()
{
	m_pInterface = pInterface;
	m_bManageTransaction = false;
	m_pTransaction = pTransaction;
	m_pDatabase = pDatabase;
}

CFirebirdPreparedStatement::~CFirebirdPreparedStatement()
{
	Close();
}

void CFirebirdPreparedStatement::Close()
{
	CloseResultSets();

	FirebirdStatementVector::iterator start = m_Statements.begin();
	FirebirdStatementVector::iterator stop = m_Statements.end();

	while (start != stop)
	{
		CFirebirdPreparedStatementWrapper* pWrapper = (CFirebirdPreparedStatementWrapper*)(*start);
		wxDELETE(pWrapper);
		(*start) = nullptr;
		start++;
	}

	// Close the transaction if we're in charge of it
	if (m_bManageTransaction && m_pTransaction)
	{
		int nReturn = m_pInterface->GetIscCommitTransaction()(m_Status, &m_pTransaction);
		m_pTransaction = nullptr;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
	}
}

bool CFirebirdPreparedStatement::AddPreparedStatement(const wxString& strSQL)
{
	CFirebirdPreparedStatementWrapper* pWrapper = new CFirebirdPreparedStatementWrapper(m_pInterface, m_pDatabase, m_pTransaction, strSQL);

	if (pWrapper->Prepare())
	{
		pWrapper->SetEncoding(GetEncoding());
		m_Statements.push_back(pWrapper);

		return true;
	}

	return false;
}

CFirebirdPreparedStatement* CFirebirdPreparedStatement::CreateStatement(CFirebirdInterface* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction, const wxString& strSQL, const wxCSConv* conv)
{
	wxArrayString Queries = ParseQueries(strSQL);

	wxArrayString::iterator start = Queries.begin();
	wxArrayString::iterator stop = Queries.end();

	CFirebirdPreparedStatement *pStatement = nullptr;

	if (Queries.size() < 1)
	{
		pStatement = new CFirebirdPreparedStatement(pInterface, pDatabase, pTransaction);
		pStatement->SetEncoding(conv);

		pStatement->SetErrorCode(DATABASE_LAYER_ERROR);
		pStatement->SetErrorMessage(_("No SQL Statements found"));

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		// If we're using exceptions, then assume that the calling program won't
		//  won't get the pStatement pointer back.  So delete is now before
		//  throwing the exception
		DatabaseLayerException error(pStatement->GetErrorCode(), pStatement->GetErrorMessage());
		try
		{
			delete pStatement; //It's probably better to manually iterate over the list and close the statements, but for now just let close do it
		}
		catch (DatabaseLayerException& e)
		{
		}

		throw error;
#endif
		return nullptr;
	}

	// Start a new transaction if appropriate
	if (pTransaction == nullptr)
	{
		ISC_STATUS_ARRAY status;

		pTransaction = 0L;

		int nReturn = pInterface->GetIscStartTransaction()(status, &pTransaction, 1, &pDatabase, 0, nullptr);
		pStatement = new CFirebirdPreparedStatement(pInterface, pDatabase, pTransaction);
		pStatement->SetEncoding(conv);
		if (nReturn != 0)
		{
			long nSqlCode = pInterface->GetIscSqlcode()(status);
			pStatement->SetErrorCode(CFirebirdDatabaseLayer::TranslateErrorCode(nSqlCode));
			pStatement->SetErrorMessage(CFirebirdDatabaseLayer::TranslateErrorCodeToString(pInterface, nSqlCode, status));

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
			// If we're using exceptions, then assume that the calling program won't
			//  won't get the pStatement pointer back.  So delete it now before
			//  throwing the exception
			try
			{
				delete pStatement; //It's probably better to manually iterate over the list and close the statements, but for now just let close do it
			}
			catch (DatabaseLayerException& e)
			{
			}

			DatabaseLayerException error(pStatement->GetErrorCode(), pStatement->GetErrorMessage());
			throw error;
#endif
			return pStatement;
		}

		pStatement->SetManageTransaction(true);
	}
	else
	{
		pStatement = new CFirebirdPreparedStatement(pInterface, pDatabase, pTransaction);
		pStatement->SetEncoding(conv);
		pStatement->SetManageTransaction(false);
	}

	while (start != stop)
	{
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		try
		{
#endif
			bool succesStatement = pStatement->AddPreparedStatement((*start));
			if (!succesStatement)
			{
				wxDELETE(pStatement); //It's probably better to manually iterate over the list and close the statements, but for now just let close do it
				return pStatement;
			}

#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		}
		catch (DatabaseLayerException& e)
		{
			try
			{
				delete pStatement; //It's probably better to manually iterate over the list and close the statements, but for now just let close do it
			}
			catch (DatabaseLayerException& e)
			{
			}

			// Pass on the error
			throw e;
			}
#endif
		if (pStatement->GetErrorCode() != DATABASE_LAYER_OK)
		{
			// If we're using exceptions, then assume that the calling program won't
			//  won't get the pStatement pointer back.  So delete is now before
			//  throwing the exception
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	  // Set the error code and message
			DatabaseLayerException error(pStatement->GetErrorCode(), pStatement->GetErrorMessage());

			try
			{
				delete pStatement; //It's probably better to manually iterate over the list and close the statements, but for now just let close do it
			}
			catch (DatabaseLayerException& e)
			{
	}

			// Pass on the error
			throw error;
#endif

			return pStatement;
}
		start++;
}
	// Success?  Return the statement
	return pStatement;
}

// get field
void CFirebirdPreparedStatement::SetParamInt(int nPosition, int nValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, nValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void CFirebirdPreparedStatement::SetParamDouble(int nPosition, double dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, dblValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void CFirebirdPreparedStatement::SetParamNumber(int nPosition, const number_t& dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, dblValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void CFirebirdPreparedStatement::SetParamString(int nPosition, const wxString& strValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, strValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void CFirebirdPreparedStatement::SetParamNull(int nPosition)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition);
	else
		SetInvalidParameterPositionError(nPosition);
}

void CFirebirdPreparedStatement::SetParamBlob(int nPosition, const void* pData, long nDataLength)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, pData, nDataLength);
	else
		SetInvalidParameterPositionError(nPosition);
}

void CFirebirdPreparedStatement::SetParamDate(int nPosition, const wxDateTime& dateValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, dateValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void CFirebirdPreparedStatement::SetParamBool(int nPosition, bool bValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, bValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

int CFirebirdPreparedStatement::GetParameterCount()
{
	FirebirdStatementVector::iterator start = m_Statements.begin();
	FirebirdStatementVector::iterator stop = m_Statements.end();

	int nParameters = 0;
	while (start != stop)
	{
		nParameters += ((CFirebirdPreparedStatementWrapper*)(*start))->GetParameterCount();
		start++;
	}
	return nParameters;
}

int CFirebirdPreparedStatement::RunQuery()
{
	FirebirdStatementVector::iterator start = m_Statements.begin();
	FirebirdStatementVector::iterator stop = m_Statements.end();

	long rows = -1;
	while (start != stop)
	{
		rows = ((CFirebirdPreparedStatementWrapper*)(*start))->DoRunQuery();
		if (((CFirebirdPreparedStatementWrapper*)(*start))->GetErrorCode() != DATABASE_LAYER_OK)
		{
			SetErrorCode(((CFirebirdPreparedStatementWrapper*)(*start))->GetErrorCode());
			SetErrorMessage(((CFirebirdPreparedStatementWrapper*)(*start))->GetErrorMessage());
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}

		start++;
	}

	// If the statement is managing the transaction then commit it now
	if (m_bManageTransaction)
	{
		int nReturn = m_pInterface->GetIscCommitRetaining()(m_Status, &m_pTransaction);
		//int nReturn = isc_commit_transaction(m_Status, &m_pTransaction);
		// We're done with the transaction, so set it to nullptr so that we know that a new transaction must be started if we run any queries
		if (nReturn != 0) {
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
	}

	return rows;
}

IDatabaseResultSet* CFirebirdPreparedStatement::RunQueryWithResults()
{
	if (m_Statements.size() > 0)
	{
		// Assume that only the last statement in the array returns the result set
		for (unsigned int i = 0; i < m_Statements.size() - 1; i++)
		{
			m_Statements[i]->DoRunQuery();
			if (m_Statements[i]->GetErrorCode() != DATABASE_LAYER_OK)
			{
				SetErrorCode(m_Statements[i]->GetErrorCode());
				SetErrorMessage(m_Statements[i]->GetErrorMessage());
				return nullptr;
			}
		}

		CFirebirdPreparedStatementWrapper* pLastStatement = m_Statements[m_Statements.size() - 1];
		// If the statement is managing the transaction then commit it now
		if (m_bManageTransaction)
		{
			//int nReturn = isc_commit_retaining(m_Status, &m_pTransaction);
			int nReturn = m_pInterface->GetIscCommitTransaction()(m_Status, &m_pTransaction);
			// We're done with the transaction, so set it to nullptr so that we know that a new transaction must be started if we run any queries
			if (nReturn != 0)
			{
				InterpretErrorCodes();
				ThrowDatabaseException();
			}

			// Start a new transaction
			nReturn = m_pInterface->GetIscStartTransaction()(m_Status, &m_pTransaction, 1, &m_pDatabase, 0, nullptr);
			if (nReturn != 0)
			{
				InterpretErrorCodes();
				ThrowDatabaseException();
				return nullptr;
			}

			// Make sure to update the last statements pointer to the transaction
			pLastStatement->SetTransaction(m_pTransaction);
		}

		// The result set will be in charge of the result set now so change flag so that we don't try to close the transaction when the statement closes
		//m_bManageTransaction = false;

		IDatabaseResultSet* pResultSet = pLastStatement->DoRunQueryWithResults();
		if (pResultSet)
			pResultSet->SetEncoding(GetEncoding());
		if (pLastStatement->GetErrorCode() != DATABASE_LAYER_OK)
		{
			SetErrorCode(pLastStatement->GetErrorCode());
			SetErrorMessage(pLastStatement->GetErrorMessage());

			// Wrap the result set deletion in try/catch block if using exceptions.
			//We want to make sure the original error gets to the user
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
			try
			{
#endif
				if (pResultSet)
					delete pResultSet;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
		}
			catch (DatabaseLayerException& e)
			{
			}
#endif

			return nullptr;
	}

		LogResultSetForCleanup(pResultSet);
		return pResultSet;
}
	else
		return nullptr;
}

int CFirebirdPreparedStatement::FindStatementAndAdjustPositionIndex(int* pPosition)
{
	// Don't mess around if there's just one entry in the vector
	if (m_Statements.size() <= 1)
		return 0;

	// Go through all the elements in the vector
	// Get the number of parameters in each statement
	// Adjust the nPosition for the the broken up statements
	for (unsigned int i = 0; i < m_Statements.size(); i++)
	{
		int nParametersInThisStatement = 0;
		nParametersInThisStatement = m_Statements[i]->GetParameterCount();
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

void CFirebirdPreparedStatement::SetInvalidParameterPositionError(int nPosition)
{
	SetErrorCode(DATABASE_LAYER_ERROR);
	SetErrorMessage(_("Invalid Prepared Statement Parameter"));

	ThrowDatabaseException();
}

void CFirebirdPreparedStatement::InterpretErrorCodes()
{
	wxLogError(_("FirebirdPreparesStatement::InterpretErrorCodes()\n"));

	long nSqlCode = m_pInterface->GetIscSqlcode()(m_Status);
	SetErrorCode(CFirebirdDatabaseLayer::TranslateErrorCode(nSqlCode));
	SetErrorMessage(CFirebirdDatabaseLayer::TranslateErrorCodeToString(m_pInterface, nSqlCode, m_Status));
}

