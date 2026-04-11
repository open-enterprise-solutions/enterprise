#ifndef __PROC_CONTEXT__H__
#define __PROC_CONTEXT__H__

#include "byteCode.h"
#include "compileContext.h"

//*******************************************************************************

class BACKEND_API ibProcUnit;
class BACKEND_API ibProcUnitEvaluate;

//*******************************************************************************

struct ibRunContextSmall {

	ibRunContextSmall(int varCount = wxNOT_FOUND) :
		m_lParamCount(0), m_lVarCount(0) {
		if (varCount >= 0) SetLocalCount(varCount);
	}

	~ibRunContextSmall();

	void SetLocalCount(const long varCount) {
		m_lVarCount = varCount;
		if (m_lVarCount > MAX_STATIC_VAR) {
			m_pLocVars = new ibValue[m_lVarCount];
			m_pRefLocVars = new ibValue * [m_lVarCount];
		}
		else {
			m_pLocVars = m_cLocVars;
			m_pRefLocVars = m_cRefLocVars;
		}
		for (long i = 0; i < m_lVarCount; i++) {
			m_pRefLocVars[i] = &m_pLocVars[i];
		}
	};

	long GetLocalCount() const { return m_lVarCount; }

	long m_lStart, m_lParamCount;

	ibValue m_cLocVars[MAX_STATIC_VAR] = {};
	ibValue* m_pLocVars = nullptr;

	ibValue* m_cRefLocVars[MAX_STATIC_VAR] = {};
	ibValue** m_pRefLocVars = nullptr;

	long m_lVarCount;
};

struct ibRunContext {

	ibRunContext(int varCount = wxNOT_FOUND) :
		m_lStart(0), m_lCurLine(0), m_lVarCount(0), m_lParamCount(0) {
		if (varCount >= 0) SetLocalCount(varCount);
	}

	~ibRunContext();

	void SetLocalCount(const long varCount) {

		m_lVarCount = varCount;

		if (m_lVarCount > MAX_STATIC_VAR) {
			m_pLocVars = new ibValue[m_lVarCount];
			m_pRefLocVars = new ibValue * [m_lVarCount];
		}
		else {
			m_pLocVars = m_cLocVars;
			m_pRefLocVars = m_cRefLocVars;
		}

		for (long i = 0; i < m_lVarCount; i++) m_pRefLocVars[i] = &m_pLocVars[i];
	}

	long GetLocalCount() const { return m_lVarCount; }
	ibByteCode* GetByteCode() const;

	void SetProcUnit(ibProcUnit* procUnit) { m_procUnit = procUnit; }
	ibProcUnit* GetProcUnit() const { return m_procUnit; }

	ibProcUnit* m_procUnit = nullptr;
	ibCompileContext* m_compileContext = nullptr;

	long m_lStart, m_lCurLine; //current executing bytecode line
	long m_lVarCount, m_lParamCount;

	ibValue m_cLocVars[MAX_STATIC_VAR] = {};
	ibValue* m_pLocVars = nullptr;

	ibValue* m_cRefLocVars[MAX_STATIC_VAR] = {};
	ibValue** m_pRefLocVars = nullptr;

	std::map<wxString, std::shared_ptr<ibProcUnitEvaluate>> m_listEval;
};

#endif // ! _PROC_CONTEXT__H__
