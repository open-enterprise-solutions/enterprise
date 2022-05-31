////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : query value 
////////////////////////////////////////////////////////////////////////////

#include "valuequery.h"
#include "methods.h"

#include "frontend/mainFrame.h"
#include "databaseLayer/databaseLayer.h"

#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueQuery, CValue);

CMethods CValueQuery::m_methods;

CValueQuery::CValueQuery() : CValue(eValueTypes::TYPE_VALUE) {}
CValueQuery::~CValueQuery() {}

enum
{
	enSetQueryText = 0,
	enExecute,
};

void CValueQuery::PrepareNames() const
{
	SEng aMethods[] =
	{
		{"setQueryText","setQueryText(string)"},
		{"execute","execute()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);
}

CValue CValueQuery::Method(methodArg_t &aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case enSetQueryText: m_sQueryText = aParams[0].ToString(); break;
	case enExecute: Execute(); break;
	}

	return ret;
}

///////////////////////////////////////////////////////////////////

void CValueQuery::Execute()
{
	if (m_sQueryText.IsEmpty()) return;
	PreparedStatement *preparedStatement = databaseLayer->PrepareStatement(m_sQueryText);
	if (!preparedStatement) return;
	DatabaseResultSet *databaseResulSet = preparedStatement->RunQueryWithResults();
	while (databaseResulSet->Next()) {}
	preparedStatement->Close();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueQuery, "query", TEXT2CLSID("VL_QUER"));