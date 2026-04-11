#ifndef _PROCUNIT_H__
#define _PROCUNIT_H__

#include "procContext.h"

class BACKEND_API ibProcUnit {
public:

	//Constructors/destructors
	ibProcUnit() : m_numAutoDeleteParent(0),
		m_pByteCode(nullptr),
		m_pppArrayList(nullptr),
		m_ppArrayCode(nullptr) {
	}

	virtual ~ibProcUnit() { Clear(); }

	//Methods
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

		m_numAutoDeleteParent = 0;

		m_pppArrayList = nullptr;
		m_ppArrayCode = nullptr;
		m_pByteCode = nullptr;
	}

	void Clear() {
		m_procParent.clear();
		Reset();
	}

	void SetParent(ibProcUnit* procParent) {
		m_procParent.clear();
		if (procParent != nullptr) {
			unsigned int count = procParent->m_procParent.size();
			m_procParent.push_back(procParent);
			for (unsigned int i = 1; i <= count; i++) {
				m_procParent.push_back(procParent->m_procParent[i - 1]);
			}
		}
	}

	ibProcUnit* GetParent(unsigned int iLevel = 0) const {
		if (iLevel >= m_procParent.size()) {
			wxASSERT(iLevel == 0);
			return nullptr;
		}
		else {
			wxASSERT(iLevel == m_procParent.size() - 1 || m_procParent[iLevel]);
			return m_procParent[iLevel];
		}
	}

	unsigned int GetParentCount() const { return m_procParent.size(); }
	ibByteCode* GetByteCode() const { return m_pByteCode; }

	void Execute(ibByteCode& ByteCode) { Execute(ByteCode, nullptr, true); }
	void Execute(ibByteCode& ByteCode, bool bRunModule) { Execute(ByteCode, nullptr, bRunModule); }
	void Execute(ibByteCode& ByteCode, ibValue& pvarRetValue, bool bRunModule = true) { Execute(ByteCode, &pvarRetValue, bRunModule); }

private:
	void Execute(ibByteCode& ByteCode, ibValue* pvarRetValue, bool bRunModule = true);
	void Execute(ibRunContext* pContext, ibValue* pvarRetValue, bool bDelta); // bDelta=true - flag for executing module operators that come at the end of functions and procedures
public:

	static bool Evaluate(const wxString& strExpression, ibRunContext* pRunContext, ibValue& pvarRetValue, bool bCompileBlock);
	bool CompileExpression(ibRunContext* pRunContext, ibValue& pvarRetValue, ibCompileCode& cModule, bool bCompileBlock);

	//call an arbitrary function of the executable module
	long FindExportMethod(const wxString& strMethodName) const { return FindMethod(strMethodName, false, 2); }

	//Search for export functions
	long FindMethod(const wxString& strMethodName, bool bError = false, int bExportOnly = 0) const;

	long FindFunction(const wxString& strMethodName, bool bError = false, int bExportOnly = 0) const;
	long FindProcedure(const wxString& strMethodName, bool bError = false, int bExportOnly = 0) const;

	template <typename ...Types>
	inline void CallAsProc(const wxString& funcName, Types&&... args) {
		ibValue* ppParams[] = { &args..., nullptr };
		CallAsProc(funcName, ppParams, (const long)sizeof ...(args));
	}

	template <typename ...Types>
	inline void CallAsFunc(const wxString& funcName, ibValue& pvarRetValue, Types&&... args) {
		ibValue* ppParams[] = { &args..., nullptr };
		CallAsFunc(funcName, pvarRetValue, ppParams, (const long)sizeof ...(args));
	}

	bool CallAsProc(const wxString& funcName, ibValue** ppParams, const long lSizeArray);
	bool CallAsFunc(const wxString& funcName, ibValue& pvarRetValue, ibValue** ppParams, const long lSizeArray);

	void CallAsProc(const long lCodeLine, ibValue** ppParams, const long lSizeArray);
	void CallAsFunc(const long lCodeLine, ibValue& pvarRetValue, ibValue** ppParams, const long lSizeArray);

	long FindProp(const wxString& strPropName) const;

	bool SetPropVal(const wxString& strPropName, const ibValue& varPropVal);
	bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute

	bool GetPropVal(const wxString& strPropName, ibValue& pvarPropVal);
	bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

	//run module 
	static ibProcUnit* GetCurrentRunModule() { return m_currentRunModule; }
	static void ClearCurrentRunModule() { m_currentRunModule = nullptr; }

	//run context
	static void AddRunContext(ibRunContext* runContext) { ms_runContext.push_back(runContext); }
	static unsigned int GetCountRunContext() { return ms_runContext.size(); }

	static ibRunContext* GetPrevRunContext() {
		if (ms_runContext.size() < 2)
			return nullptr;
		return ms_runContext[ms_runContext.size() - 2];
	}

	static ibRunContext* GetCurrentRunContext() {
		if (!ms_runContext.size())
			return nullptr;
		return ms_runContext.back();
	}

	static ibRunContext* GetRunContext(unsigned int idx) {
		if (ms_runContext.size() < idx)
			return nullptr;
		return ms_runContext[idx];
	}

	static void BackRunContext() { ms_runContext.pop_back(); }

	static ibByteCode* GetCurrentByteCode() {
		const ibRunContext* runContext = GetCurrentRunContext();
		if (runContext != nullptr)
			return runContext->GetByteCode();
		return nullptr;
	}

	static void Raise();

protected:

	//attributes:
	int m_numAutoDeleteParent; //flag for deleting the parent module
	ibByteCode* m_pByteCode = nullptr;
	ibValue*** m_pppArrayList = {}; //pointers to arrays of variable pointers (0 - local variables, 1 - variables of the current module, 2 and higher - variables of parent modules)
	ibProcUnit** m_ppArrayCode = {}; //pointers to arrays of executable modules (0 - current module, 1 and higher - parent modules)
	std::vector <ibProcUnit*> m_procParent;

	//static attributes
	static ibProcUnit* m_currentRunModule;

	ibRunContext m_cCurContext;

	//static attributes
	static std::vector <ibRunContext*> ms_runContext; //list of executable module codes
};

class BACKEND_API ibProcUnitEvaluate : public ibProcUnit {
public:

	//Constructors/destructors
	virtual ~ibProcUnitEvaluate();
};

#endif 