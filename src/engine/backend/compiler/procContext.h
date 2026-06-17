#ifndef __PROC_CONTEXT__H__
#define __PROC_CONTEXT__H__

#include <memory>

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
		// Free a prior heap frame before re-sizing. SetLocalCount can run twice
		// for the same context (AttachRuntime's Run(false) prepare pass, then
		// Run(true) execute); a second new[] without this leaks the first frame.
		// Static-buffer frames (== m_c*) are never freed.
		if (m_pLocVars != nullptr && m_pLocVars != m_cLocVars)
			delete[] m_pLocVars;
		if (m_pRefLocVars != nullptr && m_pRefLocVars != m_cRefLocVars)
			delete[] m_pRefLocVars;
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

// Inherits enable_shared_from_this so heap-promoted instances (created
// via std::make_shared by OPER_CALL / OPER_CALL_LAMBDA when the called
// function has ibByteFunction::m_needsHeapFrame=true) can hand out
// shared_ptr<ibRunContext> copies at OPER_LFUNC capture time. Stack-
// allocated instances (the common case — function has no inner
// lambda) return an expired weak_ptr from weak_from_this() — used as
// the runtime discriminator: "heap-promoted iff weak_from_this().lock()
// is non-null". No separate kind flag needed.
struct ibRunContext : std::enable_shared_from_this<ibRunContext> {

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

		// Reset block-scope nesting depth — frame starts at depth 0
		// (fn-frame / module-body). OPER_CTX_BEGIN bumps, OPER_CTX_END
		// drops. SendLocalVariables filters by entry.m_scopeDepth <=
		// m_currentScopeDepth so block-locals are hidden before / after
		// their owning `{ }`.
		m_currentScopeDepth = 0;
	}

	long GetLocalCount() const { return m_lVarCount; }
	const ibByteCode* GetByteCode() const;

	void SetProcUnit(ibProcUnit* procUnit) { m_procUnit = procUnit; }
	ibProcUnit* GetProcUnit() const { return m_procUnit; }

	// Bytecode-driven derived getters. Each frame's metadata is
	// reconstructable from m_currentFunction (when inside a function)
	// or from GetByteCode()->m_bExpressionOnly (eval block) — runtime
	// carries no compile-context pointer at all.
	bool IsModuleBody() const { return m_currentFunction == nullptr; }
	bool IsReturningFunction() const {
		return m_currentFunction != nullptr && m_currentFunction->m_bCodeRet;
	}
	bool IsExpressionOnly() const {
		const ibByteCode* bc = GetByteCode();
		return bc != nullptr && bc->m_bExpressionOnly;
	}

	ibProcUnit* m_procUnit = nullptr;

	// Call-stack parent — set in OPER_CALL / OPER_CALL_METHOD / OPER_CALL_LAMBDA
	// handlers when constructing the callee's frame. Raw pointer: the
	// caller's frame is always alive for the duration of the call
	// (either C-stack or a shared_ptr held by some outer ibRunContext
	// or value). Walked at OPER_LFUNC materialise to identify
	// heap-promoted ancestors that the new lambda value captures.
	// nullptr for module-body entry (the Execute(bDelta=true) path).
	ibRunContext* m_parentRunContext = nullptr;

	// Bytecode-side function descriptor for the function this frame
	// is executing. nullptr → frame is module-body (top-level
	// descriptor body, not inside any function). Set at function
	// entry by Execute via FindFunctionByEntry; eval/debugger read
	// it instead of reaching to a compile-context. AOT-friendly:
	// bytecode-side pointer (const — never mutated through this slot).
	const ibByteCode::ibByteFunction* m_currentFunction = nullptr;

	long m_lStart, m_lCurLine; //current executing bytecode line

	long m_lVarCount, m_lParamCount;

	ibValue m_cLocVars[MAX_STATIC_VAR] = {};
	ibValue* m_pLocVars = nullptr;

	ibValue* m_cRefLocVars[MAX_STATIC_VAR] = {};
	ibValue** m_pRefLocVars = nullptr;

	// Current block-scope nesting depth. Push (++) on OPER_CTX_BEGIN,
	// pop (--) on OPER_CTX_END. SendLocalVariables filter:
	//   entry.m_scopeDepth <= m_currentScopeDepth → visible.
	// 0 at frame entry → only entries stamped 0 (fn-frame / module-body
	// level) show until execution enters a `{ }` block.
	int m_currentScopeDepth = 0;

	std::map<wxString, std::shared_ptr<ibProcUnitEvaluate>> m_listEval;
};

#endif // ! _PROC_CONTEXT__H__
