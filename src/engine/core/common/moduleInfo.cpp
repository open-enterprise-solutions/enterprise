////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : module information for CValue 
////////////////////////////////////////////////////////////////////////////

#include "moduleInfo.h"
#include "core/compiler/definition.h"
#include "appData.h"

IModuleInfo::IModuleInfo() :
	m_compileModule(NULL),
	m_procUnit(NULL)
{
}

IModuleInfo::IModuleInfo(CCompileModule* compileModule) :
	m_compileModule(compileModule), m_procUnit(NULL)
{
}

IModuleInfo::~IModuleInfo()
{
	wxDELETE(m_compileModule);
	wxDELETE(m_procUnit);
}

bool IModuleInfo::ExecuteProc(const wxString& methodName,
	CValue** paParams, const long lSizeArray) {
	if (m_procUnit != NULL && m_procUnit->FindMethod(methodName) != wxNOT_FOUND) {
		m_procUnit->CallFunction(methodName, paParams, lSizeArray);
		return true;
	}
	return false; 
}

bool IModuleInfo::ExecuteFunc(const wxString& methodName,
	CValue& pvarRetValue, CValue** paParams, const long lSizeArray) {
	if (m_procUnit != NULL && m_procUnit->FindMethod(methodName) != wxNOT_FOUND) {
		pvarRetValue = m_procUnit->CallFunction(methodName, paParams, lSizeArray);
		return true;
	}
	return false;
}