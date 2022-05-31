////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - methods
////////////////////////////////////////////////////////////////////////////

#include "constantManager.h"
#include "compiler/methods.h"
#include "databaseLayer/databaseLayer.h"
#include "appData.h"

CMethods CConstantManager::m_methods;

enum
{
	enSet = 0,
	enGet
};

void CConstantManager::PrepareNames() const
{
	SEng aMethods[] =
	{
		{"set", "set(value)"},
		{"get", "get()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);
}

CValue CConstantManager::Method(methodArg_t &aParams)
{
	CValue retValue;
	CConstantObject *constantValue =
		m_metaConst->CreateObjectValue();
	wxASSERT(constantValue);
	switch (aParams.GetIndex())
	{
	case enSet: constantValue->SetConstValue(aParams[0]); break;
	case enGet: retValue = constantValue->GetConstValue(); break;
	}
	wxDELETE(constantValue);
	return retValue;
}