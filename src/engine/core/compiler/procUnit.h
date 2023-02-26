#ifndef _PROCUNIT_H__
#define _PROCUNIT_H__

#include "compileModule.h"

class CRunContext {
public:
	CProcUnit* m_procUnit;
	compileContext_t* m_compileContext;
	long m_lStart, m_lCurLine; //текуща€ исполн€ема€ строка байт-кода
	long m_lVarCount, m_lParamCount;
	CValue m_cLocVars[MAX_STATIC_VAR];
	CValue* m_pLocVars;
	CValue* m_cRefLocVars[MAX_STATIC_VAR];
	CValue** m_pRefLocVars;
	std::map<wxString, CProcUnit*> m_stringEvals;
public:

	CRunContext(int iLocal = wxNOT_FOUND) :
		m_lVarCount(0), m_lParamCount(0), m_lCurLine(0),
		m_procUnit(NULL), m_compileContext(NULL) {
		if (iLocal >= 0) {
			SetLocalCount(iLocal);
		}
	};

	~CRunContext();
	byteCode_t* CRunContext::GetByteCode() const;

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
			m_pRefLocVars = new CValue*[m_lVarCount];
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

class CProcUnit {
	//attributes:
	int m_nAutoDeleteParent;	//признак удалени€ родительского модул€
	byteCode_t* m_pByteCode;
	CValue*** m_pppArrayList;//указатели на массивы указателей переменных (0 - локальные переменные,1-переменные текущего модул€,2 и выше - переменные родительских модулей)
	CProcUnit** m_ppArrayCode;//указатели на массивы выполн€емых модулей (0-текущий модуль,1 и выше - родительские модули)
	std::vector <CProcUnit*> m_aParent;
	//static attributes
	static CProcUnit* m_currentRunModule;
private:
	CRunContext m_cCurContext;
	//static attributes
	static std::vector <CRunContext*> s_aRunContext; //список исполн€емых кодов модулей
public:

	// онструкторы/деструкторы
	CProcUnit() : m_pppArrayList(NULL),
		m_ppArrayCode(NULL),
		m_pByteCode(NULL),
		m_nAutoDeleteParent(0) {
	}

	virtual ~CProcUnit() {
		Clear();
	}

	//ћетоды
	void Clear() {

		if (m_pppArrayList != NULL)
			delete[] m_pppArrayList;

		if (m_ppArrayCode != NULL)
			delete m_ppArrayCode;

		if (m_currentRunModule == this)
			m_currentRunModule = NULL;

		m_nAutoDeleteParent = 0;

		m_pppArrayList = NULL;
		m_ppArrayCode = NULL;
		m_pByteCode = NULL;

		m_aParent.clear();
	}

	void SetParent(CProcUnit* procParent) {
		m_aParent.clear();
		if (procParent != NULL) {
			unsigned int count = procParent->m_aParent.size();
			m_aParent.push_back(procParent);
			for (unsigned int i = 1; i <= count; i++) {
				m_aParent.push_back(procParent->m_aParent[i - 1]);
			}
		}

	}

	CProcUnit* GetParent(unsigned int iLevel = 0) const {
		if (iLevel >= m_aParent.size()) {
			wxASSERT(iLevel == 0);
			return NULL;
		}
		else {
			wxASSERT(iLevel == m_aParent.size() - 1 || m_aParent[iLevel]);
			return m_aParent[iLevel];
		}
	}

	unsigned int GetParentCount() const {
		return m_aParent.size();
	}

	byteCode_t* GetByteCode() const {
		return m_pByteCode;
	}

	CValue Execute(byteCode_t& ByteCode, bool bRunModule = true);
	CValue Execute(CRunContext* pContext, bool bDelta); // bDelta=true - признак выполнени€ операторов модул€, которые идут в конце функций и процедур

	static CValue Evaluate(const wxString& strCode, CRunContext* pRunContext = NULL, bool bCompileBlock = false, bool* bError = NULL);
	bool CompileExpression(const wxString& strCode, CRunContext* pRunContext, CCompileModule& cModule, bool bCompileBlock);

	//вызов произвольной функции исполн€емого модул€
	long FindExportMethod(const wxString& methodName) const {
		return FindMethod(methodName, false, 2);
	}

	//ѕоиск экспортной функций
	long FindMethod(const wxString& methodName, bool bError = false, int bExportOnly = 0) const;

	template <typename ...Types>
	CValue CallFunction(const wxString& funcName, Types&... args) {
		CValue *ppParams[] = {&args..., NULL};
		return CallFunction(funcName, ppParams, (const long)sizeof ...(args));
	}

	CValue CallFunction(const wxString& funcName, CValue** ppParams, const long lSizeArray);
	CValue CallFunction(const long lCodeLine, CValue** ppParams, const long lSizeArray);

	long FindProp(const wxString& propName) const;

	bool SetPropVal(const wxString& propName, const CValue& varPropVal);
	bool SetPropVal(const long lPropNum, const CValue& varPropVal); //установка атрибута

	bool GetPropVal(const wxString& propName, CValue& pvarPropVal);
	bool GetPropVal(const long lPropNum, CValue& pvarPropVal);//значение атрибута

	//run module 
	static CProcUnit* GetCurrentRunModule() {
		return m_currentRunModule;
	}

	static void ClearCurrentRunModule() {
		m_currentRunModule = NULL;
	}

	//run context
	static void CProcUnit::AddRunContext(CRunContext* runContext) {
		s_aRunContext.push_back(runContext);
	}

	static unsigned int CProcUnit::GetCountRunContext() {
		return s_aRunContext.size();
	}

	static CRunContext* CProcUnit::GetPrevRunContext() {
		if (s_aRunContext.size() < 2)
			return NULL;
		return s_aRunContext[s_aRunContext.size() - 2];
	}

	static CRunContext* CProcUnit::GetCurrentRunContext() {
		if (!s_aRunContext.size())
			return NULL;
		return s_aRunContext.back();
	}

	static CRunContext* CProcUnit::GetRunContext(unsigned int idx) {
		if (s_aRunContext.size() < idx)
			return NULL;
		return s_aRunContext[idx];
	}

	static void CProcUnit::BackRunContext() {
		s_aRunContext.pop_back();
	}

	static byteCode_t* GetCurrentByteCode() {
		CRunContext* runContext = GetCurrentRunContext();
		if (runContext != NULL)
			return runContext->GetByteCode();
		return NULL;
	}

	static void Raise();

private:
	friend class CRunContext;
	friend class CRunContextSmall;
};

#endif 