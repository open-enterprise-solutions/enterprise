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
#include "backend/metadataConfiguration.h"   // activeMetaData — whose rights the running session folds

bool ibValueDatabaseLayer::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) //function call
{
	// ⭐⭐ THE ONE HATCH, CLOSED BY THE RIGHT THAT NAMES IT. Every other road to the data passes the
	// engine — the access policy, the row-level restrictions, the register rules; this one hands a
	// script the session's raw connection, so what it runs is answered by nobody. That is a tool for
	// the person who administers the data, and only for them: DataAdministration, the same right the
	// designer's administration menu asks. (Nothing runs in the designer anyway — it answers stubs.)
	if (!appData->DesignerMode() && activeMetaData != nullptr && !activeMetaData->AccessRight_DataAdministration())
		ibBackendAccessException::Error(_("DatabaseLayer runs raw SQL past the access policy - it needs the Data administration right"));

	// A statement is DATA, not a format: the three doors below are printf-style, and a script's SQL
	// handed to them as the format turned any per cent sign in it (a LIKE pattern) into a conversion
	// specifier — the same fault fixed at eight engine call sites (2026-09-15), reachable here from
	// a script.
	if (lMethodNum == ePrepareStatement)
	{
		if (!appData->DesignerMode())
		{
			ibPreparedStatement* preparedStatement = ses_query->PrepareStatement(wxT("%s"), paParams[0]->GetString());
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
			pvarRetValue = ses_query->RunQuery(wxT("%s"), paParams[0]->GetString());
		return true;
	}
	else if (lMethodNum == eRunQueryWithResults)
	{
		if (!appData->DesignerMode())
		{
			ibDatabaseResultSet* resultSet = ses_query->RunQueryWithResults(wxT("%s"), paParams[0]->GetString());
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

VALUE_TYPE_REGISTER(ibValueDatabaseLayer, "DatabaseLayer", value_to_clsid("VL_DBLY"));
