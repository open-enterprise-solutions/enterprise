#ifndef __PROC_CONTEXT__H__
#define __PROC_CONTEXT__H__

#include "byteCode.h"
#include "compileContext.h"

class CProcUnit;

class CRunContextSmall {
public:

	CRunContextSmall(int nLocal = wxNOT_FOUND) :
		m_lVarCount(0), m_lParamCount(0) {
		if (nLocal >= 0) {
			SetLocalCount(nLocal);
		}
	};

	~CRunContextSmall() {
		if (m_lVarCount > MAX_STATIC_VAR) {
			delete[]m_pLocVars;
			delete[]m_pRefLocVars;
		}
	}

	long m_lStart, m_lParamCount;

	CValue m_cLocVars[MAX_STATIC_VAR];
	CValue* m_pLocVars;

	CValue* m_cRefLocVars[MAX_STATIC_VAR];
	CValue** m_pRefLocVars;

private:
	long m_lVarCount;
public:

	void SetLocalCount(long varCount) {
		m_lVarCount = varCount;
		if (m_lVarCount > MAX_STATIC_VAR) {
			m_pLocVars = new CValue[m_lVarCount];
			m_pRefLocVars = new CValue * [m_lVarCount];
		}
		else {
			m_pLocVars = m_cLocVars;
			m_pRefLocVars = m_cRefLocVars;
		}
		for (long i = 0; i < m_lVarCount; i++) {
			m_pRefLocVars[i] = &m_pLocVars[i];
		}
	};

	long GetLocalCount() const {
		return m_lVarCount;
	}
};

class CRunContext {
public:
	CProcUnit* m_procUnit;
	CCompileContext* m_compileContext;
	long m_lStart, m_lCurLine; //текущая исполняемая строка байт-кода
	long m_lVarCount, m_lParamCount;
	CValue m_cLocVars[MAX_STATIC_VAR];
	CValue* m_pLocVars;
	CValue* m_cRefLocVars[MAX_STATIC_VAR];
	CValue** m_pRefLocVars;
	std::map<wxString, CProcUnit*> m_stringEvals;
public:

	CRunContext(int iLocal = wxNOT_FOUND) :
		m_lVarCount(0), m_lParamCount(0), m_lCurLine(0),
		m_procUnit(nullptr), m_compileContext(nullptr) {
		if (iLocal >= 0) {
			SetLocalCount(iLocal);
		}
	};

	~CRunContext();
	CByteCode* CRunContext::GetByteCode() const;

	void SetLocalCount(long varCount) {
		m_lVarCount = varCount;
		if (m_lVarCount > MAX_STATIC_VAR) {
			m_pLocVars = new CValue[m_lVarCount];
			m_pRefLocVars = new CValue * [m_lVarCount];
		}
		else {
			m_pLocVars = m_cLocVars;
			m_pRefLocVars = m_cRefLocVars;
		}
		for (long i = 0; i < m_lVarCount; i++)
			m_pRefLocVars[i] = &m_pLocVars[i];
	}

	long GetLocalCount() const {
		return m_lVarCount;
	}

	void SetProcUnit(CProcUnit* procUnit) {
		m_procUnit = procUnit;
	}

	CProcUnit* GetProcUnit() const {
		return m_procUnit;
	}
};

#endif // ! _PROC_CONTEXT__H__
