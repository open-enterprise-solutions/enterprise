////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value DatabaseLayer 
////////////////////////////////////////////////////////////////////////////

#include "valueDatabase.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"

//////////////////////////////////////////////////////////////////////

ibValue::ibValueMethodHelper ibValueDatabaseLayer::m_methodHelper;

enum
{
	ePrepareStatement,
	eRunQuery,
	eRunQueryWithResults,
};

ibValueDatabaseLayer::ibValueDatabaseLayer() :
	ibValue(ibValueTypes::TYPE_VALUE)
{
}

ibValueDatabaseLayer::~ibValueDatabaseLayer()
{
}

void ibValueDatabaseLayer::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendFunc(wxT("PrepareStatement"), 1, wxT("PrepareStatement(string: query, ...)"));
	m_methodHelper.AppendFunc(wxT("RunQuery"), 1, wxT("RunQuery(string: query, ...)"));
	m_methodHelper.AppendFunc(wxT("RunQueryWithResults"), 1, wxT("RunQueryWithResults(string: query, ...)"));
}

#include "backend/backend_exception.h"

bool ibValueDatabaseLayer::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) //function call
{
	if (lMethodNum == ePrepareStatement)
	{
		if (!appData->DesignerMode())
		{
			ibPreparedStatement* preparedStatement = ses_query->PrepareStatement(paParams[0]->GetString());
			if (preparedStatement == nullptr) {
				ibBackendCoreException::Error(ibBackendCoreException::GetLastError());
				return false;
			}
			pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValuePreparedStatement>(preparedStatement);
			return true;
		}

		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValuePreparedStatement>();
		return true;
	}
	else if (lMethodNum == eRunQuery)
	{
		if (!appData->DesignerMode())
			pvarRetValue = ses_query->RunQuery(paParams[0]->GetString());
		return true;
	}
	else if (lMethodNum == eRunQueryWithResults)
	{
		if (!appData->DesignerMode())
		{
			ibDatabaseResultSet* resultSet = ses_query->RunQueryWithResults(paParams[0]->GetString());
			if (resultSet == nullptr) {
				ibBackendCoreException::Error(ses_query->GetErrorMessage());
				return false;
			}
			pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueResultSet>(resultSet);
			return true;
		}

		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueResultSet>();
		return true;
	}

	return false;
}

bool ibValueDatabaseLayer::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) //procudre call
{
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueDatabaseLayer, "DatabaseLayer", string_to_clsid("VL_DBLY"));