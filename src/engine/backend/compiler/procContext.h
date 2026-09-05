#ifndef __PROC_CONTEXT__H__
#define __PROC_CONTEXT__H__

#include <memory>
#include <utility>
#include <vector>

#include "byteCode.h"
#include "compileContext.h"

//*******************************************************************************

class BACKEND_API ibProcUnit;
class BACKEND_API ibProcUnitEvaluate;

//*******************************************************************************

// A FRAME'S SLOTS ARE NOT IN THE FRAME. They are a run of slots on the session's
// run stack (ibRunStack, procUnitState.h) when the frame IS one call, and the
// frame's own memory when it can outlive one.
//
// Both frames below used to carry `ibValue m_cLocStorage[MAX_STATIC_VAR]` inline —
// ten slots on x86, twenty-five on x64 — and that cost twice over. In TIME first:
// the array was value-initialised, so entering ANY function built twenty-five
// ibValue and leaving it ran twenty-five destructors, whatever the function
// declared (~45% of a call, bench_runtime.cpp DISABLED_FrameCost), which was fixed
// by constructing only the declared slots. Then in SPACE: 1 200 bytes per frame on
// x64 for a reserve that fitted NEITHER kind of frame — argument frames never need
// more than five slots, local-variable frames need sixteen to ninety and reached
// `new ibValue[]` on every call anyway. docs/runtime-perf.md §10.
//
// ⚠ And it was hiding a defect, not merely costing: an index past the end of a
// frame read into that spare capacity — empty, plausible ibValue — instead of
// faulting. ResolveOuterFrame (procUnit.cpp) now asks the frame how wide it is.
struct ibRunStack;

// HOW LONG THIS FRAME LIVES — the one thing the caller knows and the frame cannot
// work out for itself, and everything about its storage follows from it. The
// callsite says what it means ("this frame is the call") instead of being handed a
// stack it would then have to know what to do with.
enum class ibRunLifetime {

	// Exactly one call, ending where it began: every frame built inside Execute for
	// the duration of a call, and inside CallAsFunc / CallAsProc. Its slots are a
	// run on the session's run stack, released in order.
	PerCall,

	// Can outlive the call that made it — heap-promoted for a lambda to capture
	// (ibByteFunction::m_needsHeapFrame), or the context embedded in an ibProcUnit,
	// which lives as long as its module. Owns its slots.
	//
	// THE DEFAULT, because it is the safe answer: a frame declared as a member is
	// default-constructed and must never reserve on a stack it will outlive.
	Retained
};

// WHEN THE METHOD'S ARITY IS UNKNOWN, this many slots. `GetNParams` answers
// wxNOT_FOUND for a handful of built-ins, and the frame still has to cover whatever
// the implementation may reach, because method implementations index paParams[] by
// their own declared arity without consulting the count they were handed.
//
// Eight, measured rather than assumed: a probe counted every frame the runtime built
// while the warehouse example ran, and argument frames never exceeded FIVE slots —
// three out of four were EMPTY (`max=5 | 0:150000 5:49999` over 200 000 of them).
// It used to be MAX_STATIC_VAR, which answered a different question, and answered it
// by the width of a pointer: 10 on x86, 25 on x64.
static const long kSlotsWhenArityUnknown = 8;

// The arguments of one C++ method call, and — since ibRunContext is this plus what
// makes a frame an interpreter frame — the slots themselves.
struct ibRunContextSmall {

	ibRunContextSmall(int varCount = wxNOT_FOUND, ibRunLifetime lifetime = ibRunLifetime::Retained)
		: m_lStart(0), m_lParamCount(0), m_lifetime(lifetime) {
		if (varCount >= 0) SetLocalCount(varCount);
	}

	~ibRunContextSmall() { DestroyLocals(); }

	// Out of line because a frame is built once per CALL, not per opcode — there is
	// nothing to gain from inlining the loops at every site that builds one, and the
	// header stays lighter.
	void SetLocalCount(const long varCount);

	// The one place that knows where the slots came from. Leaves the frame empty, so
	// calling it twice is harmless — which matters, because an exception unwinding
	// through a live frame (every Raise, every script `try`, every session cancel)
	// runs the destructor.
	void DestroyLocals();

	long GetLocalCount() const { return m_lVarCount; }

	long m_lStart, m_lParamCount;

	ibValue*  m_pLocVars    = nullptr;
	ibValue** m_pRefLocVars = nullptr;

protected:

	// WHERE the slots came from, remembered rather than looked up twice: the stack
	// that granted them and the position it granted at (the two numbers
	// ibRunStack::ibRun carries, kept as plain values so this header needs only the
	// forward declaration above). A frame must give its run back to the stack that
	// gave it, not to whichever one the session resolves to at destruction time.
	// m_runMark == kNoRun means the frame owns its slots; procContext.cpp is the
	// only place that reads any of this.
	long          m_lVarCount = 0;
	ibRunLifetime m_lifetime;
	ibRunStack*   m_pRunStack = nullptr;
	unsigned int  m_runBlock  = 0;
	unsigned int  m_runMark   = 0xFFFFFFFFu;
};

// Inherits enable_shared_from_this so heap-promoted instances (created
// via std::make_shared by OPER_CALL / OPER_CALL_LAMBDA when the called
// function has ibByteFunction::m_needsHeapFrame=true) can hand out
// shared_ptr<ibRunContext> copies at OPER_LFUNC capture time. Stack-
// allocated instances (the common case — function has no inner
// lambda) return an expired weak_ptr from weak_from_this() — used as
// the runtime discriminator: "heap-promoted iff weak_from_this().lock()
// is non-null". No separate kind flag needed.
struct ibRunContext : ibRunContextSmall, std::enable_shared_from_this<ibRunContext> {

	ibRunContext(int varCount = wxNOT_FOUND, ibRunLifetime lifetime = ibRunLifetime::Retained) :
		ibRunContextSmall(wxNOT_FOUND, lifetime), m_lCurLine(0) {
		if (varCount >= 0) SetLocalCount(varCount);
	}

	// A frame starts at block-depth 0 whatever its storage, and re-sizing a frame is
	// re-entering it (the prepare/execute pair reuses one). The scope depth is this
	// frame's business, not the slots', so it is reset here.
	void SetLocalCount(const long varCount) {
		ibRunContextSmall::SetLocalCount(varCount);
		m_currentScopeDepth = 0;
	}

	~ibRunContext();

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

	long m_lParamCount;

	// Current block-scope nesting depth. Push (++) on OPER_CTX_BEGIN,
	// pop (--) on OPER_CTX_END. SendLocalVariables filter:
	//   entry.m_scopeDepth <= m_currentScopeDepth → visible.
	// 0 at frame entry → only entries stamped 0 (fn-frame / module-body
	// level) show until execution enters a `{ }` block.
	int m_currentScopeDepth = 0;

	// `Cached` — the argument tuple this frame's result will be kept under, and
	// the function it belongs to. Filled at OPER_FUNC when the body is about to
	// run on a MISS, read at OPER_RET / OPER_ENDFUNC when it returns.
	//
	// IT LIVES ON THE FRAME because that is what makes recursion and re-entry
	// correct without a word of extra care: each call has its own, so a cached
	// function that calls itself cannot overwrite the key of the call above it.
	// An empty vector costs no allocation, so a frame that is not cached pays
	// for a pointer-sized triple and nothing else.
	long                 m_cachedEntry = wxNOT_FOUND;
	std::vector<ibValue> m_cachedKey;

	// A VECTOR, AND EMPTY IT COSTS NOTHING. This was a std::map, which on MSVC
	// allocates its sentinel node in the DEFAULT CONSTRUCTOR — so every frame paid
	// one heap allocation and one free for a container that is empty unless a
	// debugger is evaluating a watch. A frame is built per function call and per
	// pipeline lambda invocation, so a thin lambda paid it per ELEMENT.
	//
	// Measured, not assumed (xperf, DISABLED_LinqOneLambda): `~ibRunContext` was
	// 15.7% of the run, `operator new` 96% called from CallLambdaWithArgs,
	// `_free_base` 93% from ~ibRunContext, and a `_Tree_val::_Erase_tree` in the
	// destructor named the container. See docs/runtime-perf.md §1h.
	//
	// It was never used as a map either: the only lookup is a linear `find_if`
	// with a case-insensitive compare, and the only write happens when that scan
	// found nothing. A vector is what the code was already doing.
	std::vector<std::pair<wxString, std::shared_ptr<ibProcUnitEvaluate>>> m_listEval;
};

#endif // ! _PROC_CONTEXT__H__
