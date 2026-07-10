#ifndef _PROCUNIT_H__
#define _PROCUNIT_H__

#include "procContext.h"
#include "compileCode.h"  // ibProcUnitEvaluate owns std::unique_ptr<ibCompileCode>

struct ibProcUnitState;   // procUnitState.h — forward decl; full type via ibSession::GetPUState()

// Forward decl — ibValueFunction lives inline in procUnit.cpp; needs
// access to ibProcUnit::m_pByteCode for the swap-and-restore pattern
// in its Execute() implementation.
class ibValueFunction;
class ibSession;

// Invoke a lambda value with N positional arguments from host (C++) code.
// `callable` must wrap (directly or through TYPE_REFFER) an ibValueFunction;
// argPtrs[0..n) are the arguments. Returns true on success, false if the value
// is not a lambda. The one place the arity matters is the call site: aggregation
// selectors pass one (ibValueArray::Sum/Min/Max/Average), the L4-2 Queryable.Join
// push-down passes two (the outer,inner result-selector). One entry, not a family
// of arity-specific wrappers — fire a script lambda from C++ without going through
// OPER_CALL_LAMBDA bytecode.
BACKEND_API bool InvokeLambda(ibValue& callable, ibValue** argPtrs, long n, ibValue& retVal);

// Convenience over InvokeLambda for the common single-argument call (argPtrs = { &arg }).
BACKEND_API bool InvokeLambdaWithArg(ibValue& callable, ibValue& arg, ibValue& retVal);

class BACKEND_API ibProcUnit {
public:

	friend class ibValueFunction;
	// ibSession::CompileRoot wires the per-session lambda runtime
	// shim's frame array directly — needs access to m_pppArrayList /
	// m_ppArrayCode / m_cCurContext.
	friend class ibSession;

	//Constructors/destructors
	ibProcUnit() : m_numAutoDeleteParent(0),
		m_pByteCode(nullptr),
		m_pppArrayList(nullptr),
		m_ppArrayCode(nullptr) {
	}

	virtual ~ibProcUnit() { Clear(); }

	//Methods
	void Reset();

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
	const ibByteCode* GetByteCode() const { return m_pByteCode; }

	// Execute(bytecode, binder, retVal). Bytecode is a pure template
	// (m_listVar entries with kind ∈ {External, Context} declare the
	// binding contract); binder carries the live ibValue* slots filled
	// by manager via SetVar().
	void Execute(const ibByteCode& bc, ibByteBinder& br) { Execute(bc, br, nullptr); }
	void Execute(const ibByteCode& bc, ibByteBinder& br, ibValue& pvarRetValue) { Execute(bc, br, &pvarRetValue); }

	// Internal ibByteCode-only overloads — used by eval / nested call
	// paths that don't need a real binding session (extern frames
	// inherited via m_pppArrayList from a parent procunit). Construct
	// an empty binder internally bound to bc's m_listVar.
	void Execute(const ibByteCode& bc) { ibByteBinder br(bc.m_listVar, /*delta=*/true); Execute(bc, br, nullptr); }
	void Execute(const ibByteCode& bc, bool delta) { ibByteBinder br(bc.m_listVar, delta); Execute(bc, br, nullptr); }
	void Execute(const ibByteCode& bc, ibValue& pvarRetValue, bool delta = true) { ibByteBinder br(bc.m_listVar, delta); Execute(bc, br, &pvarRetValue); }

private:
	void Execute(const ibByteCode& bc, ibByteBinder& br, ibValue* pvarRetValue);
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

	// Comma-separated-args wrappers over the ppParams array forms below. Return TRUE if the named method
	// was found and run, FALSE if there is no such method (nothing ran) — same contract as the array forms.
	template <typename ...Types>
	inline bool CallAsProc(const wxString& funcName, Types&&... args) {
		ibValue* ppParams[] = { &args..., nullptr };
		return CallAsProc(funcName, ppParams, (const long)sizeof ...(args));
	}

	template <typename ...Types>
	inline bool CallAsFunc(const wxString& funcName, ibValue& pvarRetValue, Types&&... args) {
		ibValue* ppParams[] = { &args..., nullptr };
		return CallAsFunc(funcName, pvarRetValue, ppParams, (const long)sizeof ...(args));
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

	// Interpreter state (currentRunModule, runContext stack, errorPlace,
	// recCount) lives on ibProcUnitState — accessed through
	// ibSession::GetPUState()->X. The previous static forwarders on
	// ibProcUnit (GetCurrentRunModule / GetCurrentRunContext / Raise /
	// ...) were removed; callers go through GetPUState() directly,
	// which returns nullptr when no session is bound and lets the
	// caller decide on the null-handling policy explicitly.

protected:

	//attributes:
	int m_numAutoDeleteParent; //flag for deleting the parent module
	const ibByteCode* m_pByteCode = nullptr;
	ibValue*** m_pppArrayList = {}; //pointers to arrays of variable pointers (0 - local variables, 1 - variables of the current module, 2 and higher - variables of parent modules)
	ibProcUnit** m_ppArrayCode = {}; //pointers to arrays of executable modules (0 - current module, 1 and higher - parent modules)
	// "Body already executed" flag — set when Execute runs the module body
	// (bDelta), cleared in Reset(). Marks the frame DIRTY so a repeat Execute on
	// the same bytecode rebuilds instead of reusing locals left live by the prior
	// run. A Run(false) prepare pass never sets it, so the paired Run(true) reuses
	// the frame Run(false) built.
	bool m_bExecuted = false;
	std::vector <ibProcUnit*> m_procParent;

	// Per-thread state (m_currentRunModule, ms_runContext, s_nRecCount,
	// s_errorPlace) lives as thread_local in procUnit.cpp. The storage
	// cannot be declared `static thread_local` on an exported (BACKEND_API)
	// class — MSVC C2492 forbids the combination. Access goes through the
	// static inline/out-of-line methods below, each of which forwards to
	// the file-scope thread_local in procUnit.cpp.

	ibRunContext m_cCurContext;
};

class BACKEND_API ibProcUnitEvaluate : public ibProcUnit {
public:

	// Direct ownership of the eval's compile-code. Replaces the old
	// `delete m_pByteCode->m_compileModule` cleanup that reached
	// through the bytecode back-pointer. The compile-code is stamped
	// here BEFORE CompileExpression so that even if compile throws,
	// the unique_ptr cleans it up — no manual error-path handling
	// needed in the caller.
	void TakeCompileCode(std::unique_ptr<ibCompileCode> compileCode) {
		m_compileCode = std::move(compileCode);
	}
	ibCompileCode* GetCompileCode() const { return m_compileCode.get(); }

	//Constructors/destructors
	virtual ~ibProcUnitEvaluate() = default;

private:
	std::unique_ptr<ibCompileCode> m_compileCode;
};

#endif