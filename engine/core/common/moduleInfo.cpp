////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : module information for CValue 
////////////////////////////////////////////////////////////////////////////

#include "moduleInfo.h"
#include "compiler/definition.h"
#include "appData.h"

IModuleInfo::IModuleInfo() : 
	m_compileModule(NULL), 
	m_procUnit(NULL) 
{
}

IModuleInfo::IModuleInfo(CCompileModule *compileModule) : 
	m_compileModule(compileModule), m_procUnit(NULL) 
{
}

IModuleInfo::~IModuleInfo()
{
	wxDELETE(m_compileModule);
	wxDELETE(m_procUnit);
}

CValue IModuleInfo::ExecuteMethod(methodArg_t &aParams)
{
	if (m_procUnit)
	{
		if (m_procUnit->FindFunction(aParams.GetName()) != wxNOT_FOUND) {
			return m_procUnit->CallFunction(aParams);
		}

		return new CValueNoRet(aParams.GetName());
	}

	return CValue();
}