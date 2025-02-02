#include "byteCode.h"

////////////////////////////////////////////////////////////////////////
// CByteCode CByteCode CByteCode CByteCode						      //
////////////////////////////////////////////////////////////////////////

long CByteCode::FindMethod(const wxString& strMethodName) const
{
	auto foundedMethod = std::find_if(m_aFuncList.begin(), m_aFuncList.end(),
		[strMethodName](const std::pair<const wxString, CByteMethod>& pair) {
			return stringUtils::CompareString(strMethodName, pair.first);
		}
	);
	if (foundedMethod != m_aFuncList.end())
		return (long)foundedMethod->second;
	return wxNOT_FOUND;
}

long CByteCode::FindExportMethod(const wxString& strMethodName) const
{
	auto foundedMethod = std::find_if(m_aExportFuncList.begin(), m_aExportFuncList.end(),
		[strMethodName](const std::pair<const wxString, CByteMethod>& pair) {
			return stringUtils::CompareString(strMethodName, pair.first);
		}
	);
	if (foundedMethod != m_aExportFuncList.end())
		return (long)foundedMethod->second;
	return wxNOT_FOUND;
}

long CByteCode::FindFunction(const wxString& funcName) const
{
	auto foundedFunction = std::find_if(m_aFuncList.begin(), m_aFuncList.end(),
		[funcName](const std::pair<const wxString, CByteMethod>& pair) {
			return stringUtils::CompareString(funcName, pair.first) && pair.second.m_bCodeRet;
		}
	);
	if (foundedFunction != m_aFuncList.end())
		return (long)foundedFunction->second;
	return wxNOT_FOUND;
}

long CByteCode::FindExportFunction(const wxString& funcName) const
{
	auto foundedFunction = std::find_if(m_aExportFuncList.begin(), m_aExportFuncList.end(),
		[funcName](const std::pair<const wxString, CByteMethod>& pair) {
			return stringUtils::CompareString(funcName, pair.first) && pair.second.m_bCodeRet;
		}
	);
	if (foundedFunction != m_aExportFuncList.end())
		return (long)foundedFunction->second;
	return wxNOT_FOUND;
}

long CByteCode::FindProcedure(const wxString& procName) const
{
	auto foundedProcedure = std::find_if(m_aFuncList.begin(), m_aFuncList.end(),
		[procName](const std::pair<const wxString, CByteMethod>& pair) {
			return stringUtils::CompareString(procName, pair.first) && !pair.second.m_bCodeRet;
		}
	);
	if (foundedProcedure != m_aFuncList.end())
		return (long)foundedProcedure->second;
	return wxNOT_FOUND;
}

long CByteCode::FindExportProcedure(const wxString& procName) const
{
	auto foundedProcedure = std::find_if(m_aExportFuncList.begin(), m_aExportFuncList.end(),
		[procName](const std::pair<const wxString, CByteMethod>& pair) {
			return stringUtils::CompareString(procName, pair.first) && !pair.second.m_bCodeRet;
		}
	);
	if (foundedProcedure != m_aExportFuncList.end())
		return (long)foundedProcedure->second;
	return wxNOT_FOUND;
}

long CByteCode::FindVariable(const wxString& strVarName) const
{
	auto foundedFunction = std::find_if(m_aVarList.begin(), m_aVarList.end(),
		[strVarName](const std::pair<const wxString, long>& pair) {
			return stringUtils::CompareString(strVarName, pair.first);
		}
	);
	if (foundedFunction != m_aVarList.end())
		return (long)foundedFunction->second;
	return wxNOT_FOUND;
}

long CByteCode::FindExportVariable(const wxString& strVarName) const
{
	auto foundedFunction = std::find_if(m_aExportVarList.begin(), m_aExportVarList.end(),
		[strVarName](const std::pair<const wxString, long>& pair) {
			return stringUtils::CompareString(strVarName, pair.first);
		}
	);
	if (foundedFunction != m_aExportVarList.end())
		return (long)foundedFunction->second;
	return wxNOT_FOUND;
}