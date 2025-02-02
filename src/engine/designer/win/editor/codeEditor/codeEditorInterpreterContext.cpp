#include "codeEditorInterpreter.h"
#include "backend/systemManager/systemManager.h"

//////////////////////////////////////////////////////////////////////
// CPrecompileContext CPrecompileContext CPrecompileContext CPrecompileContext  //
//////////////////////////////////////////////////////////////////////

/**
 * ѕоиск переменной в хэш массиве
 * ¬озвращает 1 - если переменна€ найдена
 */
bool CPrecompileContext::FindVariable(const wxString& strName, CValue& vContext, bool bContext)
{
	if (bContext)
	{
		auto it = cVariables.find(stringUtils::MakeUpper(strName));
		if (it != cVariables.end())
		{
			vContext = it->second.m_valContext;
			return it->second.bContext;
		}
		return false;
	}
	else
	{
		return cVariables.find(stringUtils::MakeUpper(strName)) != cVariables.end();
	}
}

/**
 * ѕоиск переменной в хэш массиве
 * ¬озвращает 1 - если переменна€ найдена
 */
bool CPrecompileContext::FindFunction(const wxString& strName, CValue& vContext, bool bContext)
{
	if (bContext)
	{
		auto it = cFunctions.find(stringUtils::MakeUpper(strName));
		if (it != cFunctions.end() && it->second)
		{
			vContext = it->second->m_valContext;
			return it->second->bContext;
		}
		return false;
	}
	else
	{
		return cFunctions.find(stringUtils::MakeUpper(strName)) != cFunctions.end();
	}
}

void CPrecompileContext::RemoveVariable(const wxString& strName)
{
	auto it = cVariables.find(stringUtils::MakeUpper(strName));
	if (it != cVariables.end()) {
		cVariables.erase(it);
	}
}

/**
 * ƒобавл€ет новую переменную в список
 * ¬озвращает добавленную переменную в виде CParamValue
 */
CParamValue CPrecompileContext::AddVariable(const wxString& paramName, const wxString& paramType, bool bExport, bool bTempVar, const CValue& valVar)
{
	if (FindVariable(paramName)) //было объ€вление + повторное объ€вление = ошибка
		return CParamValue();

	CPrecompileVariable currentVar;
	currentVar.bContext = false;
	currentVar.strName = stringUtils::MakeUpper(paramName);
	currentVar.strRealName = paramName;
	currentVar.bExport = bExport;
	currentVar.bTempVar = bTempVar;
	currentVar.strType = paramType;
	currentVar.m_valObject = valVar;
	currentVar.nNumber = cVariables.size();

	cVariables[stringUtils::MakeUpper(paramName)] = currentVar;

	CParamValue paramValue;
	paramValue.m_paramType = paramType;
	paramValue.m_paramObject = valVar;
	return paramValue;
}

void CPrecompileContext::SetVariable(const wxString& strVarName, const CValue& valVar)
{
	if (FindVariable(strVarName)) {
		cVariables[stringUtils::MakeUpper(strVarName)].m_valObject = valVar; 
	}
}

/**
 * ‘ункци€ возвращает номер переменной по строковому имени
 * ѕоиск определени€ переменной, начина€ с текущего контекста до всех родительских
 * ≈сли требуемой переменной нет, то создаетс€ новое определение переменной
 */
CParamValue CPrecompileContext::GetVariable(const wxString& strName, bool bFindInParent, bool bCheckError, const CValue& valVar)
{
	int nCanUseLocalInParent = nFindLocalInParent;
	CParamValue Variable;
	Variable.m_paramName = stringUtils::MakeUpper(strName);
	if (!FindVariable(strName)) {
		if (bFindInParent) {//ищем в родительских контекстах(модул€х)
			int nParentNumber = 0;
			CPrecompileContext* pCurContext = pParent;
			CPrecompileContext* pNotParent = pStopParent;
			while (pCurContext) {
				nParentNumber++;
				if (nParentNumber > MAX_OBJECTS_LEVEL) {
					CSystemFunction::Message(pCurContext->pModule->GetModuleName());
					if (nParentNumber > 2 * MAX_OBJECTS_LEVEL)
						break;
				}

				if (pCurContext == pNotParent) {//текущий модуль != запрещенный прародитель
					//провер€ем следующий родитель
					pNotParent = pCurContext->pParent;
					if (pNotParent == pContinueParent)//начина€ со следующего - нет запрещенных родителей
						pNotParent = nullptr;
				}
				else {
					if (pCurContext->FindVariable(strName)) {//нашли
						CPrecompileVariable currentVar = pCurContext->cVariables[stringUtils::MakeUpper(strName)];
						//смотрим это экспортна€ переменна€ или нет (если nFindLocalInParent=true, то можно вз€ть локальные переменные родител€)
						if (nCanUseLocalInParent > 0 || currentVar.bExport) {
							//определ€ем номер переменной
							Variable.m_paramType = currentVar.strType;
							Variable.m_paramObject = currentVar.m_valObject;
							return Variable;
						}
					}
				}
				nCanUseLocalInParent--;
				pCurContext = pCurContext->pParent;
			}
		}

		if (bCheckError)
			return Variable;

		bool bTempVar = strName.Left(1) == "@";
		//не было еще объ€влени€ переменной - добавл€ем
		AddVariable(strName, wxEmptyString, false, bTempVar, valVar);
	}

	//определ€ем номер и тип переменной
	CPrecompileVariable currentVar = cVariables[stringUtils::MakeUpper(strName)];
	Variable.m_paramType = currentVar.strType;
	Variable.m_paramObject = currentVar.m_valObject;
	return Variable;
}

CPrecompileContext::~CPrecompileContext()
{
	for (auto it = cFunctions.begin(); it != cFunctions.end(); it++) {
		CPrecompileFunction* pFunction = (CPrecompileFunction*)it->second;
		if (pFunction)
			delete pFunction;
	}
	cFunctions.clear();
}