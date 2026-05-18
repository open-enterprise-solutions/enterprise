#include "codeEditorInterpreter.h"
#include "backend/system/systemManager.h"

//////////////////////////////////////////////////////////////////////
// ibPrecompileContext ibPrecompileContext ibPrecompileContext ibPrecompileContext  //
//////////////////////////////////////////////////////////////////////

/**
 * FindVariable
 *   Look up a variable by name. When isContext=true, additionally
 *   reports whether the variable is bound to a context value via
 *   the out-parameter vContext.
 */
bool ibPrecompileContext::FindVariable(const wxString& name, ibValue* vContext, bool isContext)
{
	if (isContext)
	{
		auto it = m_variables.find(stringUtils::MakeUpper(name));
		if (it != m_variables.end())
		{
			if (vContext) *vContext = it->second.m_valContext;
			return it->second.m_isContext;
		}
		return false;
	}
	else
	{
		return m_variables.find(stringUtils::MakeUpper(name)) != m_variables.end();
	}
}

/**
 * FindFunction
 *   Look up a function by name. When isContext=true, reports whether
 *   the function is bound to a context value via the out-parameter
 *   vContext.
 */
bool ibPrecompileContext::FindFunction(const wxString& name, ibValue* vContext, bool isContext)
{
	if (isContext)
	{
		auto it = m_functions.find(stringUtils::MakeUpper(name));
		if (it != m_functions.end() && it->second)
		{
			if (vContext) *vContext = it->second->m_valContext;
			return it->second->m_isContext;
		}
		return false;
	}
	else
	{
		return m_functions.find(stringUtils::MakeUpper(name)) != m_functions.end();
	}
}

void ibPrecompileContext::RemoveVariable(const wxString& name)
{
	auto it = m_variables.find(stringUtils::MakeUpper(name));
	if (it != m_variables.end()) {
		m_variables.erase(it);
	}
}

/**
 * AddVariable
 *   Register a new variable in the context. Returns its descriptor
 *   wrapped as an ibParamValue. Re-declarations are rejected — when
 *   a variable with the same name already exists the function returns
 *   an empty ibParamValue.
 */
ibParamValue ibPrecompileContext::AddVariable(const wxString& paramName, const wxString& paramType, bool isExport, bool isTempVar, const ibValue& value, int declPos)
{
	if (FindVariable(paramName))   // already exists — re-declaration is an error
		return ibParamValue();

	ibPrecompileVariable currentVar;
	currentVar.m_isContext = false;
	currentVar.m_name = stringUtils::MakeUpper(paramName);
	currentVar.m_realName = paramName;
	currentVar.m_isExport = isExport;
	currentVar.m_isTempVar = isTempVar;
	currentVar.m_type = paramType;
	currentVar.m_valObject = value;
	currentVar.m_number = m_variables.size();
	currentVar.m_declPos = declPos;

	m_variables[stringUtils::MakeUpper(paramName)] = currentVar;

	ibParamValue paramValue;
	paramValue.m_paramType = paramType;
	paramValue.m_paramObject = value;
	return paramValue;
}

void ibPrecompileContext::SetVariable(const wxString& varName, const ibValue& value)
{
	if (FindVariable(varName)) {
		m_variables[stringUtils::MakeUpper(varName)].m_valObject = value;
	}
}

/**
 * GetVariable
 *   Resolve a variable by name. If not found in this context and
 *   findInParent is true, walk up the enclosing contexts (skipping
 *   contexts marked as "stop boundaries" — see m_stopParent /
 *   m_continueParent). When the name is not resolvable and checkError
 *   is false, the variable is auto-declared as a fresh local.
 */
ibParamValue ibPrecompileContext::GetVariable(const wxString& name, bool findInParent, bool checkError, const ibValue& value, int declPos)
{
	int numCanUseLocalInParent = m_findLocalInParent;
	ibParamValue Variable;
	Variable.m_paramName = stringUtils::MakeUpper(name);
	if (!FindVariable(name)) {
		if (findInParent) {                                       // walk up the parent chain
			int nParentNumber = 0;
			ibPrecompileContext* pCurContext = m_parent;
			ibPrecompileContext* pNotParent = m_stopParent;
			while (pCurContext) {
				nParentNumber++;
				if (nParentNumber > MAX_OBJECTS_LEVEL) {
					ibValueSystemFunction::Message(pCurContext->m_module->GetModuleName());
					if (nParentNumber > 2 * MAX_OBJECTS_LEVEL)
						break;
				}

				if (pCurContext == pNotParent) {                  // stop-boundary hit — skip lookup, advance the boundary
					pNotParent = pCurContext->m_parent;
					if (pNotParent == m_continueParent)             // continue-boundary reached — clear the marker
						pNotParent = nullptr;
				}
				else {
					if (pCurContext->FindVariable(name)) {        // found in an outer scope
						ibPrecompileVariable currentVar = pCurContext->m_variables[stringUtils::MakeUpper(name)];
						// Use locals from outer scope only while the
						// findLocalInParent budget allows it; export
						// variables are always visible.
						if (numCanUseLocalInParent > 0 || currentVar.m_isExport) {
							Variable.m_paramType = currentVar.m_type;
							Variable.m_paramObject = currentVar.m_valObject;
							return Variable;
						}
					}
				}
				numCanUseLocalInParent--;
				pCurContext = pCurContext->m_parent;
			}
		}

		if (checkError)
			return Variable;

		bool isTempVar = name.Left(1) == "@";
		// There was no declaration yet — auto-add it as a local. declPos
		// propagates so autocomplete only surfaces the var when the caret
		// is past the first reference (typical implicit-decl pattern is
		// `a = expr` — the LHS occurrence is the de-facto declaration).
		AddVariable(name, wxEmptyString, false, isTempVar, value, declPos);
	}

	//determine the m_number and type of the variable
	ibPrecompileVariable currentVar = m_variables[stringUtils::MakeUpper(name)];
	Variable.m_paramType = currentVar.m_type;
	Variable.m_paramObject = currentVar.m_valObject;
	return Variable;
}

ibPrecompileContext::~ibPrecompileContext()
{
	for (auto it = m_functions.begin(); it != m_functions.end(); it++) {
		ibPrecompileFunction* pFunction = (ibPrecompileFunction*)it->second;
		if (pFunction)
			delete pFunction;
	}
	m_functions.clear();
}