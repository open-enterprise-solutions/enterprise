#ifndef _PROCUNIT_H__
#define _PROCUNIT_H__

#include "procContext.h"

class BACKEND_API CProcUnit {
	//attributes:
	int m_nAutoDeleteParent;	//признак удалени€ родительского модул€
	CByteCode* m_pByteCode;
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
	CProcUnit() : m_pppArrayList(nullptr),
		m_ppArrayCode(nullptr),
		m_pByteCode(nullptr),
		m_nAutoDeleteParent(0) {
	}

	virtual ~CProcUnit() {
		Clear();
	}

	//ћетоды
	void Reset() {

		if (m_pppArrayList != nullptr) {
			wxDELETEA(m_pppArrayList);
		}

		if (m_ppArrayCode != nullptr) {
			wxDELETE(m_ppArrayCode);
		}

		if (m_currentRunModule == this) {
			m_currentRunModule = nullptr;
		}

		m_nAutoDeleteParent = 0;

		m_pppArrayList = nullptr;
		m_ppArrayCode = nullptr;
		m_pByteCode = nullptr;
	}

	void Clear() {
		m_aParent.clear();
		Reset();
	}

	void SetParent(CProcUnit* procParent) {
		m_aParent.clear();
		if (procParent != nullptr) {
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
			return nullptr;
		}
		else {
			wxASSERT(iLevel == m_aParent.size() - 1 || m_aParent[iLevel]);
			return m_aParent[iLevel];
		}
	}

	unsigned int GetParentCount() const {
		return m_aParent.size();
	}

	CByteCode* GetByteCode() const {
		return m_pByteCode;
	}

	void Execute(CByteCode& ByteCode) {
		CValue pvarRetValue;
		Execute(ByteCode, pvarRetValue, true);
	}
	void Execute(CByteCode& ByteCode, bool bRunModule) {
		CValue pvarRetValue;
		Execute(ByteCode, pvarRetValue, bRunModule);
	}
	void Execute(CByteCode& ByteCode, CValue& pvarRetValue, bool bRunModule = true);
	void Execute(CRunContext* pContext, CValue& pvarRetValue, bool bDelta); // bDelta=true - признак выполнени€ операторов модул€, которые идут в конце функций и процедур

	static bool Evaluate(const wxString& strExpression, CRunContext* pRunContext, CValue& pvarRetValue, bool bCompileBlock);
	bool CompileExpression(CRunContext* pRunContext, CValue& pvarRetValue, CCompileCode& cModule, bool bCompileBlock);

	//вызов произвольной функции исполн€емого модул€
	long FindExportMethod(const wxString& strMethodName) const {
		return FindMethod(strMethodName, false, 2);
	}

	//ѕоиск экспортной функций
	long FindMethod(const wxString& strMethodName, bool bError = false, int bExportOnly = 0) const;

	long FindFunction(const wxString& strMethodName, bool bError = false, int bExportOnly = 0) const;
	long FindProcedure(const wxString& strMethodName, bool bError = false, int bExportOnly = 0) const;

	template <typename ...Types>
	inline void CallAsProc(const wxString& funcName, Types&... args) {
		CValue* ppParams[] = { &args..., nullptr };
		CallAsProc(funcName, ppParams, (const long)sizeof ...(args));
	}

	bool CallAsProc(const wxString& funcName, CValue** ppParams, const long lSizeArray) {
		CValue pvarRetValue;
		return CallAsFunc(funcName, pvarRetValue, ppParams, lSizeArray);
	}
	bool CallAsFunc(const wxString& funcName, CValue& pvarRetValue, CValue** ppParams, const long lSizeArray);
	void CallAsProc(const long lCodeLine, CValue** ppParams, const long lSizeArray) {
		CValue pvarRetValue;
		CallAsFunc(lCodeLine, pvarRetValue, ppParams, lSizeArray);
	}
	void CallAsFunc(const long lCodeLine, CValue& pvarRetValue, CValue** ppParams, const long lSizeArray);

	long FindProp(const wxString& strPropName) const;

	bool SetPropVal(const wxString& strPropName, const CValue& varPropVal);
	bool SetPropVal(const long lPropNum, const CValue& varPropVal); //установка атрибута

	bool GetPropVal(const wxString& strPropName, CValue& pvarPropVal);
	bool GetPropVal(const long lPropNum, CValue& pvarPropVal);//значение атрибута

	//run module 
	static CProcUnit* GetCurrentRunModule() {
		return m_currentRunModule;
	}

	static void ClearCurrentRunModule() {
		m_currentRunModule = nullptr;
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
			return nullptr;
		return s_aRunContext[s_aRunContext.size() - 2];
	}

	static CRunContext* CProcUnit::GetCurrentRunContext() {
		if (!s_aRunContext.size())
			return nullptr;
		return s_aRunContext.back();
	}

	static CRunContext* CProcUnit::GetRunContext(unsigned int idx) {
		if (s_aRunContext.size() < idx)
			return nullptr;
		return s_aRunContext[idx];
	}

	static void CProcUnit::BackRunContext() {
		s_aRunContext.pop_back();
	}

	static CByteCode* GetCurrentByteCode() {
		CRunContext* runContext = GetCurrentRunContext();
		if (runContext != nullptr)
			return runContext->GetByteCode();
		return nullptr;
	}

	static void Raise();

private:
	friend class CRunContext;
	friend class CRunContextSmall;
};

#endif 