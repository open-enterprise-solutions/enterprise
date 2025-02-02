////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : module information for CValue 
////////////////////////////////////////////////////////////////////////////

#include "moduleInfo.h"

IModuleInfo::IModuleInfo() :
	m_compileModule(nullptr),
	m_procUnit(nullptr)
{
}

IModuleInfo::IModuleInfo(CCompileModule* compileCode) :
	m_compileModule(compileCode), m_procUnit(nullptr)
{
}

IModuleInfo::~IModuleInfo()
{
	wxDELETE(m_compileModule);
	wxDELETE(m_procUnit);
}

bool IModuleInfo::ExecuteProc(const wxString& strMethodName, 
	CValue** paParams, const long lSizeArray) {
	if (m_procUnit != nullptr && m_procUnit->FindMethod(strMethodName) != wxNOT_FOUND) {
		return m_procUnit->CallAsProc(strMethodName, paParams, lSizeArray);
	}
	return false; 
}

bool IModuleInfo::ExecuteFunc(const wxString& strMethodName,
	CValue& pvarRetValue, CValue** paParams, const long lSizeArray) {
	if (m_procUnit != nullptr && m_procUnit->FindMethod(strMethodName) != wxNOT_FOUND) {
		return m_procUnit->CallAsFunc(strMethodName, pvarRetValue, paParams, lSizeArray);;
	}
	return false;
}