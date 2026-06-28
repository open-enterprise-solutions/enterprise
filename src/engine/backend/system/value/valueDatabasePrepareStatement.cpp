#include "valueDatabase.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"


enum
{
	eSetParam,
	eRunQuery,
	eRunQueryWithResults,
};

ibValuePreparedStatement::ibValuePreparedStatement(ibPreparedStatement* preparedStatement) :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_preparedStatement(preparedStatement)
{
}

ibValuePreparedStatement::~ibValuePreparedStatement()
{
	if (m_preparedStatement != nullptr)
		ses_query->CloseStatement(m_preparedStatement);
}

void ibValuePreparedStatement_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProc(wxT("SetParam"), 2, wxT("SetParam(number: position, any: value)"));

	helper.AppendFunc(wxT("RunQuery"), 1, wxT("RunQuery()"));
	helper.AppendFunc(wxT("RunQueryWithResults"), 1, wxT("RunQueryWithResults()"));
}

#include "backend/backend_exception.h"

bool ibValuePreparedStatement::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) //function call
{
	if (lMethodNum == eRunQuery)
	{
		if (m_preparedStatement != nullptr)
			pvarRetValue = m_preparedStatement->RunQuery();
		return true;
	}
	else if (lMethodNum == eRunQueryWithResults)
	{
		if (m_preparedStatement != nullptr) {
			ibDatabaseResultSet* resultSet = m_preparedStatement->RunQueryWithResults();
			if (resultSet == nullptr) {
				ibBackendCoreException::Error(ibBackendCoreException::GetLastError());
				return false;
			}
			pvarRetValue = new ibValueResultSet(resultSet);
			return true;
		}

		pvarRetValue = new ibValueResultSet();
		return true;
	}

	return false;
}

bool ibValuePreparedStatement::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) //procudre call
{
	if (m_preparedStatement != nullptr && lMethodNum == eSetParam)
	{
		const int position = paParams[0]->GetInteger();
		if (position == 0 || position > m_preparedStatement->GetParameterCount()) {
			ibBackendCoreException::Error(_("Index goes beyond statement"));
			return false;
		}

		if (paParams[1]->GetType() == ibValueTypes::TYPE_BOOLEAN)
			m_preparedStatement->SetParamBool(position, paParams[1]->GetBoolean());
		else if (paParams[1]->GetType() == ibValueTypes::TYPE_NUMBER)
			m_preparedStatement->SetParamNumber(position, paParams[1]->GetNumber());
		else if (paParams[1]->GetType() == ibValueTypes::TYPE_DATE)
			m_preparedStatement->SetParamDate(position, paParams[1]->GetDateTime());
		else if (paParams[1]->GetType() == ibValueTypes::TYPE_STRING)
			m_preparedStatement->SetParamString(position, paParams[1]->GetString());
		else
			m_preparedStatement->SetParamNull(position);

		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValuePreparedStatement, "DatabasePreparedStatement", system_to_clsid("VL_DBPS"));
