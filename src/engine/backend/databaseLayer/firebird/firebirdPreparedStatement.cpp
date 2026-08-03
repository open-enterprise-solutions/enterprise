#include "firebirdPreparedStatement.h"
#include "firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

// ctor()
ibPreparedStatementFirebird::ibPreparedStatementFirebird(ibInterfaceFirebird* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction)
	: ibPreparedStatement()
{
	m_pInterface = pInterface;
	m_bManageTransaction = false;
	m_pTransaction = pTransaction;
	m_pDatabase = pDatabase;
}

ibPreparedStatementFirebird::~ibPreparedStatementFirebird()
{
	Close();
}

void ibPreparedStatementFirebird::Close()
{
	CloseResultSets();

	FirebirdStatementVector::iterator start = m_Statements.begin();
	FirebirdStatementVector::iterator stop = m_Statements.end();

	while (start != stop)
	{
		ibPreparedStatementFirebirdWrapper* pWrapper = (ibPreparedStatementFirebirdWrapper*)(*start);
		wxDELETE(pWrapper);
		(*start) = NULL;
		start++;
	}

	// Close the transaction if we're in charge of it
	if (m_bManageTransaction && m_pTransaction)
	{
		int nReturn = m_pInterface->GetIscCommitTransaction()(m_Status, &m_pTransaction);
		m_pTransaction = 0;
		if (nReturn != 0)
		{
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
	}
}

bool ibPreparedStatementFirebird::AddPreparedStatement(const wxString& strSQL)
{
	ibPreparedStatementFirebirdWrapper* pWrapper = new ibPreparedStatementFirebirdWrapper(m_pInterface, m_pDatabase, m_pTransaction, strSQL);

	if (pWrapper->Prepare())
	{
		pWrapper->SetEncoding(GetEncoding());
		m_Statements.push_back(pWrapper);

		return true;
	}

	return false;
}

ibPreparedStatementFirebird* ibPreparedStatementFirebird::CreateStatement(ibInterfaceFirebird* pInterface, isc_db_handle pDatabase, isc_tr_handle pTransaction, const wxString& strSQL, const wxCSConv* conv)
{
	wxArrayString Queries = ParseQueries(strSQL);

	wxArrayString::iterator start = Queries.begin();
	wxArrayString::iterator stop = Queries.end();

	ibPreparedStatementFirebird *pStatement = NULL;

	if (Queries.size() < 1)
	{
		pStatement = new ibPreparedStatementFirebird(pInterface, pDatabase, pTransaction);
		pStatement->SetEncoding(conv);

		pStatement->SetErrorCode(DATABASE_LAYER_ERROR);
		pStatement->SetErrorMessage(wxT("No SQL Statements found"));

		const int      nCode = pStatement->GetErrorCode();
		const wxString msg   = pStatement->GetErrorMessage();
		// Swallow a possible throw from the statement dtor — original
		// "no SQL statements" error is the user-visible one; a secondary
		// cleanup exception would mask it.
		try { delete pStatement; } catch (const ibBackendException&) {}
		ibDatabaseLayerException::Throw(
			ibBackendDatabaseException::Kind::Unknown,
			nCode, wxEmptyString, msg);
		return NULL;
	}

	// Start a new transaction if appropriate
	if (pTransaction == NULL)
	{
		ISC_STATUS_ARRAY status;

		pTransaction = 0L;

		int nReturn = pInterface->GetIscStartTransaction()(status, &pTransaction, 1, &pDatabase, 0, NULL);
		pStatement = new ibPreparedStatementFirebird(pInterface, pDatabase, pTransaction);
		pStatement->SetEncoding(conv);
		if (nReturn != 0)
		{
			long nSqlCode = pInterface->GetIscSqlcode()(status);
			pStatement->SetErrorCode(ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode));
			pStatement->SetErrorMessage(ibDatabaseLayerFirebird::TranslateErrorCodeToString(pInterface, nSqlCode, status));

			const int      nCode = pStatement->GetErrorCode();
			const wxString msg   = pStatement->GetErrorMessage();
			// Swallow a possible throw from the statement dtor — the
			// isc_start_transaction failure is what we want surfaced.
			try { delete pStatement; } catch (const ibBackendException&) {}
			ibDatabaseLayerException::Throw(
				ibBackendDatabaseException::Kind::Unknown,
				nCode, wxEmptyString, msg);
		}

		pStatement->SetManageTransaction(true);
	}
	else
	{
		pStatement = new ibPreparedStatementFirebird(pInterface, pDatabase, pTransaction);
		pStatement->SetEncoding(conv);
		pStatement->SetManageTransaction(false);
	}

	while (start != stop)
	{
		// AddPreparedStatement can throw ibBackendException directly
		// (driver classifies + throws in DatabaseErrorReporter); if it
		// does, the partly-built pStatement must die before we let the
		// exception out of this function — otherwise memory leak.
		// Re-throw preserves the original error type for the caller.
		try {
			bool succesStatement = pStatement->AddPreparedStatement((*start));
			if (!succesStatement)
			{
				wxDELETE(pStatement); //It's probably better to manually iterate over the list and close the statements, but for now just let close do it
				return pStatement;
			}
		}
		catch (const ibBackendException&) {
			try { delete pStatement; } catch (const ibBackendException&) {}
			throw;
		}

		if (pStatement->GetErrorCode() != DATABASE_LAYER_OK)
		{
			const int      nCode = pStatement->GetErrorCode();
			const wxString msg   = pStatement->GetErrorMessage();
			// Swallow a possible throw from the statement dtor — the
			// per-fragment AddPreparedStatement failure recorded above
			// is the original error and must reach the caller.
			try { delete pStatement; } catch (const ibBackendException&) {}
			ibDatabaseLayerException::Throw(
				ibBackendDatabaseException::Kind::Unknown,
				nCode, wxEmptyString, msg);
}
		start++;
}
	// Success?  Return the statement
	return pStatement;
}

// get field
void ibPreparedStatementFirebird::SetParamInt(int nPosition, int nValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, nValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void ibPreparedStatementFirebird::SetParamDouble(int nPosition, double dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, dblValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void ibPreparedStatementFirebird::SetParamNumber(int nPosition, const ibNumber& dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, dblValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void ibPreparedStatementFirebird::SetParamString(int nPosition, const wxString& strValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, strValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void ibPreparedStatementFirebird::SetParamNull(int nPosition)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition);
	else
		SetInvalidParameterPositionError(nPosition);
}

void ibPreparedStatementFirebird::SetParamBlob(int nPosition, const void* pData, long nDataLength)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, pData, nDataLength);
	else
		SetInvalidParameterPositionError(nPosition);
}

void ibPreparedStatementFirebird::SetParamDate(int nPosition, const wxDateTime& dateValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, dateValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

void ibPreparedStatementFirebird::SetParamBool(int nPosition, bool bValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
		m_Statements[nIndex]->SetParam(nPosition, bValue);
	else
		SetInvalidParameterPositionError(nPosition);
}

int ibPreparedStatementFirebird::GetParameterCount()
{
	FirebirdStatementVector::iterator start = m_Statements.begin();
	FirebirdStatementVector::iterator stop = m_Statements.end();

	int nParameters = 0;
	while (start != stop)
	{
		nParameters += ((ibPreparedStatementFirebirdWrapper*)(*start))->GetParameterCount();
		start++;
	}
	return nParameters;
}

int ibPreparedStatementFirebird::RunQuery()
{
	FirebirdStatementVector::iterator start = m_Statements.begin();
	FirebirdStatementVector::iterator stop = m_Statements.end();

	long rows = -1;
	while (start != stop)
	{
		rows = ((ibPreparedStatementFirebirdWrapper*)(*start))->DoRunQuery();
		if (((ibPreparedStatementFirebirdWrapper*)(*start))->GetErrorCode() != DATABASE_LAYER_OK)
		{
			SetErrorCode(((ibPreparedStatementFirebirdWrapper*)(*start))->GetErrorCode());
			SetErrorMessage(((ibPreparedStatementFirebirdWrapper*)(*start))->GetErrorMessage());
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}

		start++;
	}

	// If the statement is managing the transaction then commit it now
	if (m_bManageTransaction)
	{
		int nReturn = m_pInterface->GetIscCommitRetaining()(m_Status, &m_pTransaction);
		//int nReturn = isc_commit_transaction(m_Status, &m_pTransaction);
		// We're done with the transaction, so set it to NULL so that we know that a new transaction must be started if we run any queries
		if (nReturn != 0) {
			InterpretErrorCodes();
			ThrowDatabaseException();
		}
	}

	return rows;
}

ibDatabaseResultSet* ibPreparedStatementFirebird::RunQueryWithResults()
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
				return NULL;
			}
		}

		ibPreparedStatementFirebirdWrapper* pLastStatement = m_Statements[m_Statements.size() - 1];
		// If the statement is managing the transaction then commit it now
		if (m_bManageTransaction)
		{
			//int nReturn = isc_commit_retaining(m_Status, &m_pTransaction);
			int nReturn = m_pInterface->GetIscCommitTransaction()(m_Status, &m_pTransaction);
			// We're done with the transaction, so set it to NULL so that we know that a new transaction must be started if we run any queries
			if (nReturn != 0)
			{
				InterpretErrorCodes();
				ThrowDatabaseException();
			}

			// Start a new transaction
			nReturn = m_pInterface->GetIscStartTransaction()(m_Status, &m_pTransaction, 1, &m_pDatabase, 0, NULL);
			if (nReturn != 0)
			{
				InterpretErrorCodes();
				ThrowDatabaseException();
				return NULL;
			}

			// Make sure to update the last statements pointer to the transaction
			pLastStatement->SetTransaction(m_pTransaction);
		}

		// The result set will be in charge of the result set now so change flag so that we don't try to close the transaction when the statement closes
		//m_bManageTransaction = false;

		ibDatabaseResultSet* pResultSet = pLastStatement->DoRunQueryWithResults();
		if (pResultSet)
			pResultSet->SetEncoding(GetEncoding());
		if (pLastStatement->GetErrorCode() != DATABASE_LAYER_OK)
		{
			SetErrorCode(pLastStatement->GetErrorCode());
			SetErrorMessage(pLastStatement->GetErrorMessage());

			// Swallow a possible throw from ~ibDatabaseResultSet — the
			// pLastStatement error recorded above is the one we want
			// surfaced via SetError(Code|Message); a secondary cleanup
			// exception would mask it.
			try {
				if (pResultSet)
					delete pResultSet;
			}
			catch (const ibBackendException&) {}

			return NULL;
	}

		LogResultSetForCleanup(pResultSet);
		return pResultSet;
}
	else
		return NULL;
}

int ibPreparedStatementFirebird::FindStatementAndAdjustPositionIndex(int* pPosition)
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

void ibPreparedStatementFirebird::SetInvalidParameterPositionError(int nPosition)
{
	SetErrorCode(DATABASE_LAYER_ERROR);
	SetErrorMessage(wxT("Invalid Prepared Statement Parameter"));

	ThrowDatabaseException();
}

void ibPreparedStatementFirebird::InterpretErrorCodes()
{
	// "I'm in this function" — see firebirdResultSet for the same
	// strip rationale.
	long nSqlCode = m_pInterface->GetIscSqlcode()(m_Status);
	SetErrorCode(ibDatabaseLayerFirebird::TranslateErrorCode(nSqlCode));
	SetErrorMessage(ibDatabaseLayerFirebird::TranslateErrorCodeToString(m_pInterface, nSqlCode, m_Status));
}

