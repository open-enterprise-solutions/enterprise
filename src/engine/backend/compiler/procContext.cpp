#include "procContext.h"
#include "compileCode.h"
#include "procUnit.h"

//*************************************************************************************************
//*                                        RunContext                                             *
//*************************************************************************************************

CRunContext::~CRunContext() {
	if (m_lVarCount > MAX_STATIC_VAR) {
		wxDELETEA(m_pLocVars);
		wxDELETEA(m_pRefLocVars);
	}
	//удаление m_stringEvals
	for (auto& it : m_stringEvals) {
		CProcUnit*& procUnit(it.second);
		if (procUnit != nullptr) {
			wxDELETE(procUnit->m_pByteCode->m_compileModule);
			wxDELETE(procUnit);
		}
	}
}

CByteCode* CRunContext::GetByteCode() const {
	return m_procUnit ?
		m_procUnit->GetByteCode() : nullptr;
}