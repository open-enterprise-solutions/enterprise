#ifndef __PROC_CONTEXT__H__
#define __PROC_CONTEXT__H__

#include <atomic>    // a capture frame counts its holders the way ibValue does
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
// `new ibValue[]` on every call anyway. docs/private/runtime-perf.md §10.
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

	// Can outlive the call that made it: the context embedded in an ibProcUnit, which lives
	// as long as its module. Owns its slots. A lambda reaches it by its own road
	// (ibValueFunction::m_moduleFrame) and takes nothing from it — which is how the capture walk
	// (OPER_LFUNC) tells the module's frame from a rung of the chain: by this kind.
	//
	// THE DEFAULT, because it is the safe answer: a frame declared as a member is
	// default-constructed and must never reserve on a stack it will outlive.
	Retained,

	// What a CLOSURE TOOK from one outer frame — ibRunCaptureContext, and only that class sets
	// it. It lives as long as the lambda holding it.
	Captured
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

	// WHICH KIND OF FRAME THIS IS, asked rather than inferred. The capture walk and the debugger
	// both used to ask `weak_from_this().lock()` and read a live control block as "this one was
	// promoted" — a mechanism standing in for an answer about kind.
	ibRunLifetime GetLifetime() const { return m_lifetime; }

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

// The ORDINARY frame: it is the call, and it ends where the call ends — on the C stack or as a
// member. What a closure takes from one is ibRunCaptureContext below, which is a kind of this one
// and says so through GetLifetime().
struct ibRunContext : ibRunContextSmall {

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

	// ⭐ THE FRAME ENTERS ITS FUNCTION, on every road into a body: the entry opcode of a named one (a
	// direct call, a call through a module, a handler fired from C++), and the call of a lambda, from a
	// script or from a pipeline. It takes the function it runs, and fills from the declaration each
	// argument the CALL marked as left out — a null slot: a tail the binder had nothing for, a hole a
	// lambda call was written with. A slot that names a value was passed, even when the value is
	// empty. A parameter with no default is refused. `bytecode` holds the defaults' constants: the
	// function's own module.
	void Enter(const ibByteCode::ibByteFunction* function, const ibByteCode* bytecode);

	// ⭐⭐ A VARIABLE A LAMBDA REFERENCES LIVES IN A CELL — an ibValue of its own on the heap, counted
	// like any other: the slot holds it and the slot's pointer points at it, so the body reads and
	// writes it exactly as before, and every lambda that took it holds the same cell. When the frame
	// ends the variable lives on for as long as some lambda holds it — and only that variable, never
	// the frame's others (ibRunCaptureContext).
	//
	// Leaves alone a slot that already is a cell, and a parameter passed by reference: that one is the
	// caller's variable, and a lambda reaches it through the pointer for as long as the caller's
	// variable lives.
	void Capture(const long slot);

	// The frame's own variables in `captured` — its depth-0 entries (ibByteFunction::m_listCaptured),
	// made cells when the frame is entered, before the body takes a pointer to any of them: a
	// variable moved out from under a pointer the body still holds would split in two.
	void Capture(const std::vector<ibParamRunUnit>& captured);

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
	// (either C-stack, or what a lambda took, held by the running
	// lambda). Walked at OPER_LFUNC materialise to find the outer
	// frames the new lambda takes from.
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
	// destructor named the container. See docs/private/runtime-perf.md §1h.
	//
	// It was never used as a map either: the only lookup is a linear `find_if`
	// with a case-insensitive compare, and the only write happens when that scan
	// found nothing. A vector is what the code was already doing.
	std::vector<std::pair<ibString, std::shared_ptr<ibProcUnitEvaluate>>> m_listEval;
};

// ⭐⭐ WHAT A CLOSURE TOOK FROM ONE OUTER FRAME — the variables it references there, and nothing
// else. Each is a cell (ibRunContext::Capture), held here under the slot number it has in that
// frame, so the depth and slot the compiler emitted read it unchanged; the slots it did not take
// stay empty. A lambda keeps one of these per frame it is nested in (ibValueFunction::
// m_capturedFrames), and none of them is the frame.
//
// 🛑 IT WAS THE FRAME, until 2026-11-03: a lambda held every frame it was nested in WHOLE, and so
// every variable of it. A lambda stored in a variable of the same frame was then a ring of
// references — the frame held the lambda, the lambda held the frame — and EVERY value of that frame
// lived until the thread ended. A COM object among them kept Excel running after `Quit` (leak6.ces:
// `var F = Function(x) { return x; };` beside `New COMObject("Excel.Application")`, nothing
// referenced). Now a ring is left only where the program makes one: a variable the lambda
// references holds the lambda.
//
// It counts its own holders — the mechanism ibValue already uses, not a second one beside it.
class ibRunCaptureContext : public ibRunContext {
public:

	// Takes from `outer` the entries of `captured` at `depth` — each made a cell there when it is
	// not one yet. As wide as the furthest slot it takes; with nothing to take it is an empty rung
	// of the chain, which still has to be there because the compiler counted it.
	ibRunCaptureContext(ibRunContext& outer, const std::vector<ibParamRunUnit>& captured, const long long depth);

	// ⚠ Destroyed by its own DecrRef and by nothing else — never `delete`d through a base
	// pointer, which is why neither destructor here is virtual and no frame pays for a vptr.
	~ibRunCaptureContext();

	void IncrRef() { m_refCount.fetch_add(1, std::memory_order_relaxed); }
	void DecrRef();

private:

	// Counted the way ibValue counts: a lambda can be handed to a background job, so the
	// holders are not always on one thread.
	std::atomic<unsigned int> m_refCount   { 0 };
	// Destruction releases the cells, and a lambda dying there lets go of US — the DecrRef
	// that arrives then must not delete a second time.
	bool                      m_destroying = false;
};

// The frame this one is, when it is what a closure took — asked of the kind it carries, so no cast
// is made to find out. Answers null for an ordinary frame.
inline ibRunCaptureContext* AsCaptureContext(ibRunContext* frame) {
	return (frame != nullptr && frame->GetLifetime() == ibRunLifetime::Captured)
		? static_cast<ibRunCaptureContext*>(frame) : nullptr;
}

// A hold on what a closure took — the ibValuePtr of those. Taking it says IncrRef, letting it go
// says DecrRef, and that is the whole of the lifetime.
class ibRunCapturePtr {
public:

	ibRunCapturePtr() = default;
	explicit ibRunCapturePtr(ibRunCaptureContext* frame) : m_frame(frame) { if (m_frame) m_frame->IncrRef(); }

	ibRunCapturePtr(const ibRunCapturePtr& other) : m_frame(other.m_frame) { if (m_frame) m_frame->IncrRef(); }
	ibRunCapturePtr(ibRunCapturePtr&& other) noexcept : m_frame(other.m_frame) { other.m_frame = nullptr; }

	~ibRunCapturePtr() { if (m_frame) m_frame->DecrRef(); }

	ibRunCapturePtr& operator=(const ibRunCapturePtr& other) {
		if (this != &other) { Reset(other.m_frame); }
		return *this;
	}
	ibRunCapturePtr& operator=(ibRunCapturePtr&& other) noexcept {
		if (this != &other) {
			if (m_frame) m_frame->DecrRef();
			m_frame = other.m_frame; other.m_frame = nullptr;
		}
		return *this;
	}

	void Reset(ibRunCaptureContext* frame = nullptr) {
		if (frame == m_frame) return;
		if (frame) frame->IncrRef();
		if (m_frame) m_frame->DecrRef();
		m_frame = frame;
	}

	ibRunCaptureContext* Get()        const { return m_frame; }
	ibRunCaptureContext* operator->() const { return m_frame; }
	ibRunCaptureContext& operator*()  const { return *m_frame; }
	explicit operator bool()          const { return m_frame != nullptr; }

private:

	ibRunCaptureContext* m_frame = nullptr;
};

#endif // ! _PROC_CONTEXT__H__
