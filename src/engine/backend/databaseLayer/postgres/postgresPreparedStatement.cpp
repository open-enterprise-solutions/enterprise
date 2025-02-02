#include "postgresPreparedStatement.h"
#include "postgresDatabaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include <wx/tokenzr.h>
#include <wx/arrimpl.cpp>

WX_DEFINE_OBJARRAY(ArrayOfPostgresPreparedStatementWrappers);

CPostgresPreparedStatement::CPostgresPreparedStatement(CPostgresInterface* pInterface)
	: IPreparedStatement()
{
	m_pInterface = pInterface;
}

CPostgresPreparedStatement::CPostgresPreparedStatement(CPostgresInterface* pInterface, PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName)
	: IPreparedStatement()
{
	m_pInterface = pInterface;
	AddStatement(pDatabase, strSQL, strStatementName);
}


CPostgresPreparedStatement::~CPostgresPreparedStatement()
{
	Close();
}


void CPostgresPreparedStatement::Close()
{
	CloseResultSets();
	m_Statements.Clear();
}

void CPostgresPreparedStatement::AddStatement(PGconn* pDatabase, const wxString& strSQL, const wxString& strStatementName)
{
	CPostgresPreparedStatementWrapper Statement(m_pInterface, pDatabase, strSQL, strStatementName);
	Statement.SetEncoding(GetEncoding());
	m_Statements.push_back(Statement);
}

CPostgresPreparedStatement* CPostgresPreparedStatement::CreateStatement(CPostgresInterface* pInterface, PGconn* pDatabase, const wxString& strSQL)
{
	wxArrayString Queries = ParseQueries(strSQL);

	wxArrayString::iterator start = Queries.begin();
	wxArrayString::iterator stop = Queries.end();

	CPostgresPreparedStatement* pStatement = new CPostgresPreparedStatement(pInterface);
	const char* strEncoding = pInterface->GetPQencodingToChar()(pInterface->GetPQclientEncoding()(pDatabase));
	wxCSConv conv((const wxChar*)strEncoding);
	pStatement->SetEncoding(&conv);
	while (start != stop)
	{
		wxString strName = CPostgresPreparedStatement::GenerateRandomStatementName();
		pStatement->AddStatement(pDatabase, (*start), strName);
		wxCharBuffer nameBuffer = CDatabaseStringConverter::ConvertToUnicodeStream(strName, strEncoding);
		wxCharBuffer sqlBuffer = CDatabaseStringConverter::ConvertToUnicodeStream(TranslateSQL((*start)), strEncoding);
		PGresult* pResult = pInterface->GetPQprepare()(pDatabase, nameBuffer, sqlBuffer, 0, nullptr);
		if (pResult == nullptr)
		{
			delete pStatement;
			return nullptr;
		}

		if (pInterface->GetPQresultStatus()(pResult) != PGRES_COMMAND_OK)
		{
			pStatement->SetErrorCode(CPostgresDatabaseLayer::TranslateErrorCode(pInterface->GetPQresultStatus()(pResult)));
			pStatement->SetErrorMessage(CDatabaseStringConverter::ConvertFromUnicodeStream(
				pInterface->GetPQresultErrorMessage()(pResult), strEncoding));
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
void CPostgresPreparedStatement::SetParamInt(int nPosition, int nValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, nValue);
	}
}

void CPostgresPreparedStatement::SetParamDouble(int nPosition, double dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, dblValue);
	}
}

void CPostgresPreparedStatement::SetParamNumber(int nPosition, const number_t& dblValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, dblValue);
	}
}

void CPostgresPreparedStatement::SetParamString(int nPosition, const wxString& strValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, strValue);
	}
}

void CPostgresPreparedStatement::SetParamNull(int nPosition)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition);
	}
}

void CPostgresPreparedStatement::SetParamBlob(int nPosition, const void* pData, long nDataLength)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, pData, nDataLength);
	}
}

void CPostgresPreparedStatement::SetParamDate(int nPosition, const wxDateTime& dateValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, dateValue);
	}
}

void CPostgresPreparedStatement::SetParamBool(int nPosition, bool bValue)
{
	int nIndex = FindStatementAndAdjustPositionIndex(&nPosition);
	if (nIndex > -1)
	{
		m_Statements[nIndex].SetParam(nPosition, bValue);
	}
}

int CPostgresPreparedStatement::GetParameterCount()
{
	int nParameters = 0;

	for (unsigned int i = 0; i < (m_Statements.size()); i++)
	{
		nParameters += m_Statements[i].GetParameterCount();
	}
	return nParameters;
}


int CPostgresPreparedStatement::RunQuery()
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

IDatabaseResultSet* CPostgresPreparedStatement::RunQueryWithResults()
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
	CPostgresPreparedStatementWrapper* pLastStatement = &(m_Statements[m_Statements.size() - 1]);
	IDatabaseResultSet* pResultSet = pLastStatement->DoRunQueryWithResults();
	if (pLastStatement->GetErrorCode() != DATABASE_LAYER_OK) {
		SetErrorCode(pLastStatement->GetErrorCode());
		SetErrorMessage(pLastStatement->GetErrorMessage());
		ThrowDatabaseException();
	}

	LogResultSetForCleanup(pResultSet);
	return pResultSet;
}

wxString CPostgresPreparedStatement::GenerateRandomStatementName()
{
	// Just come up with a string prefixed with "databaselayer_" and 10 random characters
	wxString strReturn = _("databaselayer_");
	for (int i = 0; i < 10; i++)
	{
		strReturn << (int)(10.0*rand() / (RAND_MAX + 1.0));
	}
	return strReturn;
}

int CPostgresPreparedStatement::FindStatementAndAdjustPositionIndex(int* pPosition)
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

wxString CPostgresPreparedStatement::TranslateSQL(const wxString& strOriginalSQL)
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
		if ('\'' == character)
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
					strReturn += wxString::Format(_("$%d"), nParameterIndex);
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

