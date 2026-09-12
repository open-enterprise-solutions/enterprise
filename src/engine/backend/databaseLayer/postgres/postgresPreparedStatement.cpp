#include "postgresPreparedStatement.h"
#include "postgresDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include <wx/tokenzr.h>
#include <wx/arrimpl.cpp>

WX_DEFINE_OBJARRAY(ArrayOfPostgresPreparedStatementWrappers);

ibPreparedStatementPostgres::ibPreparedStatementPostgres(ibInterfacePostgres* pInterface)
	: ibPreparedStatement()
{
	m_pInterface = pInterface;
}

ibPreparedStatementPostgres::ibPreparedStatementPostgres(ibInterfacePostgres* pInterface, PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName)
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
	m_Statements.Clear();
}

void ibPreparedStatementPostgres::AddStatement(PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName)
{
	ibPreparedStatementPostgresWrapper Statement(m_pInterface, pDatabase, strSQL, strStatementName);
	m_Statements.push_back(Statement);
}

ibPreparedStatementPostgres* ibPreparedStatementPostgres::CreateStatement(ibInterfacePostgres* pInterface, PGconn* pDatabase, const wxString& strSQL)
{
	wxArrayString Queries = ParseQueries(strSQL);

	wxArrayString::iterator start = Queries.begin();
	wxArrayString::iterator stop = Queries.end();

	ibPreparedStatementPostgres* pStatement = new ibPreparedStatementPostgres(pInterface);
	while (start != stop)
	{
		wxString strName = ibPreparedStatementPostgres::GenerateRandomStatementName();
		pStatement->AddStatement(pDatabase, (*start), strName);
		wxCharBuffer nameBuffer = ibDatabaseStringConverter::ConvertToUnicodeStream(strName);
		wxCharBuffer sqlBuffer = ibDatabaseStringConverter::ConvertToUnicodeStream(TranslateSQL((*start)));
		PGresult* pResult = pInterface->GetPQprepare()(pDatabase, nameBuffer, sqlBuffer, 0, nullptr);
		if (pResult == nullptr)
		{
			delete pStatement;
			return nullptr;
		}

		if (pInterface->GetPQresultStatus()(pResult) != PGRES_COMMAND_OK)
		{
			pStatement->SetErrorCode(ibDatabaseLayerPostgres::TranslateErrorCode(pInterface->GetPQresultStatus()(pResult),
				pInterface->GetPQresultErrorField()(pResult, PG_DIAG_SQLSTATE)));
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

void ibPreparedStatementPostgres::SetParamString(int nPosition, const wxString& strValue)
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

void ibPreparedStatementPostgres::SetParamDate(int nPosition, const wxDateTime& dateValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, dateValue);
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
			SetErrorCode(m_Statements[i].GetErrorCode());
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
			SetErrorCode(m_Statements[i].GetErrorCode());
			SetErrorMessage(m_Statements[i].GetErrorMessage());
			ThrowDatabaseException();
			return nullptr;
		}
	}
	ibPreparedStatementPostgresWrapper* pLastStatement = &(m_Statements[m_Statements.size() - 1]);
	ibDatabaseResultSet* pResultSet = pLastStatement->DoRunQueryWithResults();
	if (pLastStatement->GetErrorCode() != DATABASE_LAYER_OK) {
		SetErrorCode(pLastStatement->GetErrorCode());
		SetErrorMessage(pLastStatement->GetErrorMessage());
		ThrowDatabaseException();
	}

	LogResultSetForCleanup(pResultSet);
	return pResultSet;
}

wxString ibPreparedStatementPostgres::GenerateRandomStatementName()
{
	// Just come up with a string prefixed with "databaselayer_" and 10 random characters
	wxString strReturn = wxT("databaselayer_");
	for (int i = 0; i < 10; i++)
	{
		strReturn << (int)(10.0*rand() / (RAND_MAX + 1.0));
	}
	return strReturn;
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

wxString ibPreparedStatementPostgres::TranslateSQL(const wxString& strOriginalSQL)
{
	int nParameterIndex = 1;
	wxString strReturn = wxEmptyString;//strOriginalSQL;
	/*
	int nFound = strReturn.Replace(_("?"), wxString::Format(_("$%d"), nParameterIndex), false);
	while (nFound != 0)
	{
	  nParameterIndex++;
	  nFound = strReturn.Replace(_("?"), wxString::Format(_("$%d"), nParameterIndex), false);
	}
	*/
	bool bInStringLiteral = false;
	unsigned int len = strOriginalSQL.length();
	for (unsigned int i = 0; i < len; i++)
	{
		wxChar character = strOriginalSQL[i];
		if (wxT('\'') == character)
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
					strReturn += wxString::Format(wxT("$%d"), nParameterIndex);
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

