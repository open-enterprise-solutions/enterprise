////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : query value 
////////////////////////////////////////////////////////////////////////////

#include "valueQuery.h"	

#include "frontend/mainFrame.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueQuery, CValue);

CValue::CMethodHelper CValueQuery::m_methodHelper;

CValueQuery::CValueQuery() : CValue(eValueTypes::TYPE_VALUE) {}
CValueQuery::~CValueQuery() {}

enum
{
	enSetQueryText = 0,
	enExecute,
};

void CValueQuery::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendFunc(wxT("setQueryText"), 1, "setQueryText(string)");
	m_methodHelper.AppendFunc(wxT("execute"), "execute()");
}

bool CValueQuery::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enSetQueryText: 
		m_sQueryText = paParams[0]->GetString(); 
		return true;
	case enExecute: 
		Execute(); 
		return true;
	}
	return true;
}

///////////////////////////////////////////////////////////////////

void CValueQuery::Execute()
{
	if (m_sQueryText.IsEmpty()) 
		return;
	PreparedStatement *preparedStatement = databaseLayer->PrepareStatement(m_sQueryText);
	if (!preparedStatement) 
		return;
	DatabaseResultSet *databaseResulSet = preparedStatement->RunQueryWithResults();
	while (databaseResulSet->Next()) {}
	preparedStatement->Close();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueQuery, "query", TEXT2CLSID("VL_QUER"));