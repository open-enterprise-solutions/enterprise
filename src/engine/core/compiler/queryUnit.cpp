////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : query unit 
////////////////////////////////////////////////////////////////////////////

#include "queryunit.h"
#include "utils/stringUtils.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "appData.h"

CQueryUnit::CQueryUnit() : queryText(wxEmptyString) {}

CQueryUnit::CQueryUnit(const wxString &query) : queryText(query) {}

void CQueryUnit::SetQueryText(const wxString &query)
{
	queryText = query;
}

wxString CQueryUnit::GetQueryText()
{
	return queryText;
}

void CQueryUnit::SetQueryParam(const wxString &sParamName, CValue cParam)
{
	paParams[StringUtils::MakeUpper(sParamName)] = cParam;
}

CValue CQueryUnit::GetQueryParam(const wxString &sParamName)
{
	return paParams[StringUtils::MakeUpper(sParamName)];
}

void CQueryUnit::Execute()
{
}

void CQueryUnit::Reset()
{
	queryText = wxEmptyString;
	paParams.clear();
}
