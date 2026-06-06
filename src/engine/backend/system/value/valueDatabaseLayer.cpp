////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value DatabaseLayer 
////////////////////////////////////////////////////////////////////////////

#include "valueDatabase.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"

//////////////////////////////////////////////////////////////////////

enum
{
	ePrepareStatement,
	eRunQuery,
	eRunQueryWithResults,
};

ibValueDatabaseLayer::ibValueDatabaseLayer() :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueDatabaseLayer::~ibValueDatabaseLayer()
{
}

void ibValueDatabaseLayer_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(wxT("PrepareStatement"), 1, wxT("PrepareStatement(string: query, ...)"));
	helper.AppendFunc(wxT("RunQuery"), 1, wxT("RunQuery(string: query, ...)"));
	helper.AppendFunc(wxT("RunQueryWithResults"), 1, wxT("RunQueryWithResults(string: query, ...)"));
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
			pvarRetValue = new ibValuePreparedStatement(preparedStatement);
			return true;
		}

		pvarRetValue = new ibValuePreparedStatement();
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
			pvarRetValue = new ibValueResultSet(resultSet);
			return true;
		}

		pvarRetValue = new ibValueResultSet();
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