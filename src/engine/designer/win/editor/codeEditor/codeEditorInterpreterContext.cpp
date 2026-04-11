#include "codeEditorInterpreter.h"
#include "backend/system/systemManager.h"

//////////////////////////////////////////////////////////////////////
// CPrecompileContext CPrecompileContext CPrecompileContext CPrecompileContext  //
//////////////////////////////////////////////////////////////////////

/**
 * ����� ���������� � ��� �������
 * ���������� 1 - ���� ���������� �������
 */
bool CPrecompileContext::FindVariable(const wxString& strName, ibValue vContext, bool bContext)
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
 * ����� ���������� � ��� �������
 * ���������� 1 - ���� ���������� �������
 */
bool CPrecompileContext::FindFunction(const wxString& strName, ibValue vContext, bool bContext)
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
 * ��������� ����� ���������� � ������
 * ���������� ����������� ���������� � ���� ibParamValue
 */
ibParamValue CPrecompileContext::AddVariable(const wxString& paramName, const wxString& paramType, bool bExport, bool bTempVar, const ibValue& valVar)
{
	if (FindVariable(paramName)) //���� ���������� + ��������� ���������� = ������
		return ibParamValue();

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

	ibParamValue paramValue;
	paramValue.m_paramType = paramType;
	paramValue.m_paramObject = valVar;
	return paramValue;
}

void CPrecompileContext::SetVariable(const wxString& strVarName, const ibValue& valVar)
{
	if (FindVariable(strVarName)) {
		cVariables[stringUtils::MakeUpper(strVarName)].m_valObject = valVar; 
	}
}

/**
 * ������� ���������� ����� ���������� �� ���������� �����
 * ����� ����������� ����������, ������� � �������� ��������� �� ���� ������������
 * ���� ��������� ���������� ���, �� ��������� ����� ����������� ����������
 */
ibParamValue CPrecompileContext::GetVariable(const wxString& strName, bool bFindInParent, bool bCheckError, const ibValue& valVar)
{
	int numCanUseLocalInParent = nFindLocalInParent;
	ibParamValue Variable;
	Variable.m_paramName = stringUtils::MakeUpper(strName);
	if (!FindVariable(strName)) {
		if (bFindInParent) {//���� � ������������ ����������(�������)
			int nParentNumber = 0;
			CPrecompileContext* pCurContext = pParent;
			CPrecompileContext* pNotParent = pStopParent;
			while (pCurContext) {
				nParentNumber++;
				if (nParentNumber > MAX_OBJECTS_LEVEL) {
					ibValueSystemFunction::Message(pCurContext->pModule->GetModuleName());
					if (nParentNumber > 2 * MAX_OBJECTS_LEVEL)
						break;
				}

				if (pCurContext == pNotParent) {//������� ������ != ����������� �����������
					//��������� ��������� ��������
					pNotParent = pCurContext->pParent;
					if (pNotParent == pContinueParent)//������� �� ���������� - ��� ����������� ���������
						pNotParent = nullptr;
				}
				else {
					if (pCurContext->FindVariable(strName)) {// found
						CPrecompileVariable currentVar = pCurContext->cVariables[stringUtils::MakeUpper(strName)];
						//������� ��� ���������� ���������� ��� ��� (���� nFindLocalInParent=true, �� ����� ����� ��������� ���������� ��������)
						if (numCanUseLocalInParent > 0 || currentVar.bExport) {
							//���������� ����� ����������
							Variable.m_paramType = currentVar.strType;
							Variable.m_paramObject = currentVar.m_valObject;
							return Variable;
						}
					}
				}
				numCanUseLocalInParent--;
				pCurContext = pCurContext->pParent;
			}
		}

		if (bCheckError)
			return Variable;

		bool bTempVar = strName.Left(1) == "@";
		// there was no variable declaration yet - add
		AddVariable(strName, wxEmptyString, false, bTempVar, valVar);
	}

	//determine the number and type of the variable
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