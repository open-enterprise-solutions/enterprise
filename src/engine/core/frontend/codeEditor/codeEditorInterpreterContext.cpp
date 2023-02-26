#include "codeEditorInterpreter.h"
#include "core/compiler/definition.h"
#include "core/compiler/systemObjects.h"
#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////
// CPrecompileContext CPrecompileContext CPrecompileContext CPrecompileContext  //
//////////////////////////////////////////////////////////////////////

/**
 * ѕоиск переменной в хэш массиве
 * ¬озвращает 1 - если переменна€ найдена
 */
bool CPrecompileContext::FindVariable(const wxString& sName, CValue& vContext, bool bContext)
{
	if (bContext)
	{
		auto itFounded = cVariables.find(StringUtils::MakeUpper(sName));
		if (itFounded != cVariables.end())
		{
			vContext = itFounded->second.m_valContext;
			return itFounded->second.bContext;
		}
		return false;
	}
	else
	{
		return cVariables.find(StringUtils::MakeUpper(sName)) != cVariables.end();
	}
}

/**
 * ѕоиск переменной в хэш массиве
 * ¬озвращает 1 - если переменна€ найдена
 */
bool CPrecompileContext::FindFunction(const wxString& sName, CValue& vContext, bool bContext)
{
	if (bContext)
	{
		auto itFounded = cFunctions.find(StringUtils::MakeUpper(sName));
		if (itFounded != cFunctions.end() && itFounded->second)
		{
			vContext = itFounded->second->m_valContext;
			return itFounded->second->bContext;
		}
		return false;
	}
	else
	{
		return cFunctions.find(StringUtils::MakeUpper(sName)) != cFunctions.end();
	}
}

void CPrecompileContext::RemoveVariable(const wxString& sName)
{
	auto itFounded = cVariables.find(StringUtils::MakeUpper(sName));
	if (itFounded != cVariables.end()) {
		cVariables.erase(itFounded);
	}
}

/**
 * ƒобавл€ет новую переменную в список
 * ¬озвращает добавленную переменную в виде SParamValue
 */
SParamValue CPrecompileContext::AddVariable(const wxString& paramName, const wxString& paramType, bool bExport, bool bTempVar, const CValue& valVar)
{
	if (FindVariable(paramName)) //было объ€вление + повторное объ€вление = ошибка
		return SParamValue();

	CPrecompileVariable currentVar;
	currentVar.bContext = false;
	currentVar.sName = StringUtils::MakeUpper(paramName);
	currentVar.sRealName = paramName;
	currentVar.bExport = bExport;
	currentVar.bTempVar = bTempVar;
	currentVar.sType = paramType;
	currentVar.m_valObject = valVar;
	currentVar.nNumber = cVariables.size();

	cVariables[StringUtils::MakeUpper(paramName)] = currentVar;

	SParamValue paramValue;
	paramValue.m_paramType = paramType;
	paramValue.m_paramObject = valVar;
	return paramValue;
}

void CPrecompileContext::SetVariable(const wxString& varName, const CValue& valVar)
{
	if (FindVariable(varName)) {
		cVariables[StringUtils::MakeUpper(varName)].m_valObject = valVar; 
	}
}

/**
 * ‘ункци€ возвращает номер переменной по строковому имени
 * ѕоиск определени€ переменной, начина€ с текущего контекста до всех родительских
 * ≈сли требуемой переменной нет, то создаетс€ новое определение переменной
 */
SParamValue CPrecompileContext::GetVariable(const wxString& sName, bool bFindInParent, bool bCheckError, const CValue& valVar)
{
	int nCanUseLocalInParent = nFindLocalInParent;
	SParamValue Variable;
	Variable.m_paramName = StringUtils::MakeUpper(sName);
	if (!FindVariable(sName)) {
		if (bFindInParent) {//ищем в родительских контекстах(модул€х)
			int nParentNumber = 0;
			CPrecompileContext* pCurContext = pParent;
			CPrecompileContext* pNotParent = pStopParent;
			while (pCurContext) {
				nParentNumber++;
				if (nParentNumber > MAX_OBJECTS_LEVEL) {
					CSystemObjects::Message(pCurContext->pModule->GetModuleName());
					if (nParentNumber > 2 * MAX_OBJECTS_LEVEL)
						break;
				}

				if (pCurContext == pNotParent) {//текущий модуль != запрещенный прародитель
					//провер€ем следующий родитель
					pNotParent = pCurContext->pParent;
					if (pNotParent == pContinueParent)//начина€ со следующего - нет запрещенных родителей
						pNotParent = NULL;
				}
				else {
					if (pCurContext->FindVariable(sName)) {//нашли
						CPrecompileVariable currentVar = pCurContext->cVariables[StringUtils::MakeUpper(sName)];
						//смотрим это экспортна€ переменна€ или нет (если nFindLocalInParent=true, то можно вз€ть локальные переменные родител€)
						if (nCanUseLocalInParent > 0 || currentVar.bExport) {
							//определ€ем номер переменной
							Variable.m_paramType = currentVar.sType;
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

		bool bTempVar = sName.Left(1) == "@";
		//не было еще объ€влени€ переменной - добавл€ем
		AddVariable(sName, wxEmptyString, false, bTempVar, valVar);
	}

	//определ€ем номер и тип переменной
	CPrecompileVariable currentVar = cVariables[StringUtils::MakeUpper(sName)];
	Variable.m_paramType = currentVar.sType;
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