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
	// as long as its module. Owns its slots. A frame a CLOSURE outlives is the kind below —
	// this one is not capturable, and the module frame is reached by its own road
	// (ibValueFunction::m_moduleFrame).
	//
	// THE DEFAULT, because it is the safe answer: a frame declared as a member is
	// default-constructed and must never reserve on a stack it will outlive.
	Retained,

	// Outlives its call BECAUSE A CLOSURE TOOK IT — ibRunCaptureContext, and only that class
	// sets it. It is the kind the frame is asked for at run time (GetLifetime), so nothing has
	// to infer "was this one promoted?" from the storage it happens to sit in.
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
// member. A frame a closure takes is ibRunCaptureContext below, which is a kind of this one and
// says so through GetLifetime(). (Until 2026-09-24 there was one class for both, and
// "was it promoted?" was answered by asking whether weak_from_this() still locked — a control
// block standing in for a kind.)
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
	// (either C-stack, or a capture frame held by some outer frame or
	// value). Walked at OPER_LFUNC materialise to find the captured
	// ancestors the new lambda value links to.
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
	std::vector<std::pair<wxString, std::shared_ptr<ibProcUnitEvaluate>>> m_listEval;
};

// ⭐⭐ THE FRAME A CLOSURE TOOK, and it counts its own holders — the mechanism ibValue already
// uses, not a second one beside it. Whoever takes it says so (IncrRef), whoever lets go says so
// (DecrRef), and the last one out destroys it; the destructor releases the slots, so a lambda
// living in one of them dies with the frame.
//
// ⭐ AND IT HOLDS ITS OWN OUTER LINK, so a chain of nested closures comes apart in a CASCADE: a
// lambda holds the frame it was written in, that frame holds the one outside it, and so on to the
// root. Before this a lambda held a VECTOR of every frame in the chain — N strong references where
// one does the work, and the chain's shape was restated at every capture site.
//
// 🛑 AND NOTHING HERE WEIGHS WHO THE HOLDERS ARE. A first cut had DecrRef ask its slots how many
// of the remaining references were theirs and destroy the frame when that was all — and it crashed
// `MakeCounter`, because a returned lambda IS the lambda in the slot: one object, one reference to
// the frame, so "held only from my own slot" and "held by what escaped" are the same number
// (0xc0000005 in ClosureWritesBackIntoItsCapturedSlot / ScriptCorpus, 2026-09-24).
//
// The ring is cut where it is MADE instead: the slot OPER_LFUNC writes into is a compiler
// temporary, and a temporary does not outlive its call (ReleaseTemporarySlots, called on the way
// out of the function). Then nothing holds the frame from inside, and the counting below is the
// whole of the lifetime again.
class ibRunCaptureContext : public ibRunContext {
public:

	explicit ibRunCaptureContext(int varCount)
		: ibRunContext(varCount, ibRunLifetime::Captured) {}

	// ⚠ Destroyed by its own DecrRef and by nothing else — never `delete`d through a base
	// pointer, which is why neither destructor here is virtual and no frame pays for a vptr.
	~ibRunCaptureContext();

	void IncrRef() { m_refCount.fetch_add(1, std::memory_order_relaxed); }
	void DecrRef();

	// ⭐ THE COMPILER'S TEMPORARIES GO WHEN THE CALL DOES — the slots OPER_LFUNC and the expression
	// tape write through. Which ones they are is already in the bytecode: m_listLocals carries the
	// declared locals and leaves temporaries out (compileCode.cpp skips m_bTempVar), so a slot past
	// the parameters that no entry claims is one of them. No closure can have captured such a slot,
	// because it has no name to capture. Declared locals are untouched: they are exactly what an
	// escaped closure came for.
	void ReleaseTemporarySlots();

private:

	// Counted the way ibValue counts: a lambda can be handed to a background job, so the
	// holders of a frame are not always on one thread.
	std::atomic<unsigned int> m_refCount   { 0 };
	// Destruction releases the slots, and a lambda dying there lets go of US — the DecrRef
	// that arrives then must not delete a second time.
	bool                      m_destroying = false;
};

// The frame this one is, when it is a captured one — asked of the kind it carries, so no cast is
// made to find out. Answers null for an ordinary frame.
inline ibRunCaptureContext* AsCaptureContext(ibRunContext* frame) {
	return (frame != nullptr && frame->GetLifetime() == ibRunLifetime::Captured)
		? static_cast<ibRunCaptureContext*>(frame) : nullptr;
}

// A hold on a captured frame — the ibValuePtr of frames. Taking it says IncrRef, letting it go
// says DecrRef, and that is the whole of the lifetime: the call site keeps one for the length of
// the call and never has to remember anything on the way out, exceptions included.
class ibRunCapturePtr {
public:

	ibRunCapturePtr() = default;
	explicit ibRunCapturePtr(ibRunCaptureContext* frame) : m_frame(frame) { if (m_frame) m_frame->IncrRef(); }

	ibRunCapturePtr(const ibRunCapturePtr& other) : m_frame(other.m_frame) { if (m_frame) m_frame->IncrRef(); }
	ibRunCapturePtr(ibRunCapturePtr&& other) noexcept : m_frame(other.m_frame) { other.m_frame = nullptr; }

	// 🛑 A HOLD IS NOT THE CALL, and putting the temporaries' release here was a crash. The lambda
	// holds its captured frame through a hold of this very type (ibValueFunction::m_capturedFrames), and in
	// a pipeline a lambda is born and dies PER ROW — so the release fired on a frame that was still
	// executing and emptied slots under it (0xcdcdcdcd at procUnit.cpp:1308, x.Description inside a
	// Where lambda, live base, 2026-09-24). Harmless twice is not harmless EARLY.
	//
	// The call has one hold that stands for it, and only that one may release: ibRunCallFrame below.
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

// ⭐⭐ THE HOLD THAT IS THE CALL — one per call, built where the frame is built, and the only thing
// that may end the call's business. It holds the frame like any other hold; what it adds is that
// going out of scope means "the body is done": the compiler's temporaries go, and with them the
// lambda that sat in one, which is what keeps a closure from holding its own frame for ever.
//
// Why a type of its own rather than a flag: a hold says nothing about whose it is, and the lambda
// keeps one too (ibValueFunction::m_capturedFrames). Only the call site builds THIS, so only the call
// can end the call — the distinction the crash above was made of.
class ibRunCallFrame {
public:

	ibRunCallFrame() = default;
	explicit ibRunCallFrame(ibRunCaptureContext* frame) : m_hold(frame) {}

	ibRunCallFrame(const ibRunCallFrame&) = delete;
	ibRunCallFrame& operator=(const ibRunCallFrame&) = delete;

	~ibRunCallFrame() { if (m_hold) m_hold->ReleaseTemporarySlots(); }

	void Reset(ibRunCaptureContext* frame = nullptr) { m_hold.Reset(frame); }

	ibRunCaptureContext* Get()        const { return m_hold.Get(); }
	ibRunCaptureContext* operator->() const { return m_hold.Get(); }
	ibRunCaptureContext& operator*()  const { return *m_hold.Get(); }
	explicit operator bool()          const { return static_cast<bool>(m_hold); }

private:

	ibRunCapturePtr m_hold;
};

#endif // ! _PROC_CONTEXT__H__
