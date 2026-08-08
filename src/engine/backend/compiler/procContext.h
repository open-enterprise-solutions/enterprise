#ifndef __PROC_CONTEXT__H__
#define __PROC_CONTEXT__H__

#include <memory>

#include "byteCode.h"
#include "compileContext.h"

//*******************************************************************************

class BACKEND_API ibProcUnit;
class BACKEND_API ibProcUnitEvaluate;

//*******************************************************************************

// The inline slot buffer is RAW storage, not an ibValue array, and only the
// slots a frame actually declares are constructed.
//
// It used to be `ibValue m_cLocVars[MAX_STATIC_VAR] = {}`, so entering ANY
// function value-initialised 25 ibValue objects and leaving it ran 25
// destructors — whatever the function declared. Measured
// (tests/bench_runtime.cpp, DISABLED_FrameCost): 25 ibValue cost 149.8 ns
// against 20.5 ns for the 3 a typical function uses, while a whole call is
// ~336 ns. That is ~45% of a call spent on slots nobody reads. See
// docs/runtime-perf.md §5.6.
//
// The price of the change is that lifetime is ours now: SetLocalCount and the
// destructor construct and destroy explicitly. Both funnel through
// DestroyLocals so there is ONE place that knows how the storage was obtained
// — which matters because an exception unwinding through a live frame (every
// Raise, every script `try`, every session cancel) runs that destructor.
struct ibRunContextSmall {

	ibRunContextSmall(int varCount = wxNOT_FOUND) :
		m_lParamCount(0), m_lVarCount(0) {
		if (varCount >= 0) SetLocalCount(varCount);
	}

	~ibRunContextSmall();

	// Out of line because a frame is built once per CALL, not per opcode — there
	// is nothing to gain from inlining three loops (destroy / placement-new /
	// pointer row) at every site that builds one, and the header stays lighter.
	//
	// Honest note on what this did NOT do: moving them out left
	// ibProcUnit::Execute at exactly 38 624 bytes, so the +9 KB that function
	// gained during the frame work came from somewhere else and is still
	// unaccounted for. Do not cite this as a size fix.
	void SetLocalCount(const long varCount);

	// The one place that knows whether the slots came from the heap or from the
	// inline buffer. Leaves the frame empty, so calling it twice is harmless.
	void DestroyLocals();

	long GetLocalCount() const { return m_lVarCount; }

	long m_lStart, m_lParamCount;

	alignas(ibValue) unsigned char m_cLocStorage[sizeof(ibValue) * MAX_STATIC_VAR];
	ibValue* m_pLocVars = nullptr;

	// Pointers are trivial — deliberately NOT value-initialised: only the first
	// m_lVarCount entries are ever written or read, and zeroing all 25 was pure
	// cost on a path taken by every call.
	ibValue* m_cRefLocVars[MAX_STATIC_VAR];
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

	// Raw inline storage, only the declared slots constructed — same reasoning
	// as ibRunContextSmall above, and this is the one that matters more: the
	// OPER_CALL path asks for the function's REAL local count (procUnit.cpp
	// `ibRunContext cRunContext(index3)`), so a function with three locals now
	// builds three ibValue instead of twenty-five.
	// Out of line for the same reason as ibRunContextSmall's — see the note
	// there. Built once per call; the header form was displacing the
	// interpreting loop from the instruction cache.
	void SetLocalCount(const long varCount);

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

	alignas(ibValue) unsigned char m_cLocStorage[sizeof(ibValue) * MAX_STATIC_VAR];
	ibValue* m_pLocVars = nullptr;

	// Trivial pointers, deliberately not value-initialised — only the first
	// m_lVarCount entries are written or read.
	ibValue* m_cRefLocVars[MAX_STATIC_VAR];
	ibValue** m_pRefLocVars = nullptr;

	// Single owner of the "how was this storage obtained" question, for the
	// same reason as in ibRunContextSmall: an exception unwinding through a
	// live frame runs the destructor, and placement-constructed slots must be
	// destroyed by hand there too.
	void DestroyLocals();

	// Current block-scope nesting depth. Push (++) on OPER_CTX_BEGIN,
	// pop (--) on OPER_CTX_END. SendLocalVariables filter:
	//   entry.m_scopeDepth <= m_currentScopeDepth → visible.
	// 0 at frame entry → only entries stamped 0 (fn-frame / module-body
	// level) show until execution enters a `{ }` block.
	int m_currentScopeDepth = 0;

	std::map<wxString, std::shared_ptr<ibProcUnitEvaluate>> m_listEval;
};

#endif // ! _PROC_CONTEXT__H__
