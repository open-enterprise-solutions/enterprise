#ifndef _IB_PROC_UNIT_LAMBDA_H__
#define _IB_PROC_UNIT_LAMBDA_H__

// THE LAMBDA AS A VALUE, and the cursor that comes with it — the two runtime-internal ibValue
// subclasses the interpreter makes for itself.
//
// ⭐ NAMED FOR THE LAMBDA rather than for "values", 2026-09-08: what a reader comes here for is
// `ibValueFunction` — a function held in a slot, with its captured frames — and the iterator is
// beside it because a lambda is what a pipeline calls per row. A file named after the category it
// belongs to says nothing a reader could not have guessed from the folder.
//
//   - ibValueIterator   (TYPE_ITERATOR) — holds an ibValueIteratorState
//                       cursor + the "hot-from-NEXT_ITER" flag used by
//                       OPER_FOREACH to detect a fresh entry vs Break
//                       re-entry.
//   - ibValueFunction   (TYPE_FUNCTION) — lambda value. Stores
//                       (parentBc, funcIndex) + captured-frame vector +
//                       m_needsHeapFrame mirror. Dispatch goes through
//                       the session's lambda runtime; access to
//                       ibProcUnit's protected/private members is
//                       granted via friend declaration in procUnit.h.
//
// Declared here (not inline in procUnit.cpp anymore) so other TUs in
// the same DLL — currently procUnitLINQ.cpp — can hold these by value
// (state classes embed an ibValueFunction predicate / projection).
// The g_value{Iterator,Function} CLSIDs are header-defined inline constexpr
// below (constexpr + ODR-safe across DLLs); wxIMPLEMENT_DYNAMIC_CLASS still
// lives in procUnit.cpp.

#include "procUnit.h"          // class ibProcUnit (friend grant for ibValueFunction)
#include "session/session.h"   // ibSession::GetPUState() — used by ibValueFunction::Execute
#include "backend/eventDispatcher.h"   // ibValueFunction IS-A dispatcher — a lambda dispatches by running its own body

#include <memory>   // std::unique_ptr — the string buffer and the object a destination lets go of
// ibBackendCoreException reaches us transitively via compileCode.h's chain.

#pragma region iterator_support

inline constexpr ibClassID g_valueIterator = system_to_clsid("SO_ITER");   // header-defined: constexpr + ODR-safe across DLLs

// Runtime holder for the @it_ slot. Wraps a shared_ptr to the
// state and tracks whether the most recent transition into
// OPER_FOREACH came via OPER_NEXT_ITER (the "hot" flag): NEXT_ITER
// sets it, FOREACH consumes it. A re-entry with the flag clear means
// we returned via Break from a previous outer-loop iteration; the
// cursor is stale and must be reset.
class ibValueIterator : public ibValue {
	public:
	ibValueIterator()
		: ibValue(ibValueTypes::TYPE_ITERATOR),
		  m_hotFromNextIter(false) {}

	explicit ibValueIterator(std::shared_ptr<ibValueIteratorState> state)
		: ibValue(ibValueTypes::TYPE_ITERATOR),
		  m_state(std::move(state)),
		  m_hotFromNextIter(false) {}

	virtual ~ibValueIterator() = default;

	// NOT transferable: a cursor is a position inside somebody else's collection,
	// and advancing it from a second session would move it under the first.
	virtual bool IsTransferable() const override { return false; }

	bool MoveNext(ibValue& current) {
		return m_state ? m_state->MoveNext(current) : false;
	}
	void ResetState() { if (m_state) m_state->Reset(); }

	bool ConsumeHot() {
		const bool h = m_hotFromNextIter;
		m_hotFromNextIter = false;
		return h;
	}
	void MarkHot() { m_hotFromNextIter = true; }

private:
	std::shared_ptr<ibValueIteratorState> m_state;
	bool m_hotFromNextIter;
};

#pragma endregion

#pragma region function_value_support

inline constexpr ibClassID g_valueFunction = system_to_clsid("VL_FUNC");   // header-defined: constexpr + ODR-safe across DLLs

// First-class callable value. Backs OES anonymous functions (lambdas).
// Lives heap-allocated; regular ibValue holders carry it via TYPE_REFFER
// + m_pRef. Mirrors the inline ibValueIterator pattern above —
// runtime-internal value type, never touched from the compiler side
// (compile-side only emits OPER_LFUNC / OPER_CALL_LAMBDA).
//
// Storage: pointer into parentBc->m_listFunc + the funcIndex at
// which the lambda's ibByteFunction sits. Single source of truth —
// frame shape (paramCount / varCount / bCodeRet), m_listParam (with
// defaults + param names), m_listLocals (locals
// by name for eval), and the entry IP (m_lCodeLine) all live on
// ibByteFunction itself, the same path named functions use.
//
// Lifetime: parentBc points into the session's bytecode storage; the
// ibByteFunction inside m_listFunc is owned by parentBc. The value
// must not outlive the session that produced it.
class ibValueFunction : public ibValue, public ibEventDispatcher {
	public:
	ibValueFunction() : ibValue(ibValueTypes::TYPE_FUNCTION) {}

	// ibEventDispatcher — a lambda IS its own dispatcher: run its own body with the args (+ trailing cancel). NOT const
	// (the invoke may heap-promote / mutate the lambda's captured frames). IsEmpty is false: a bound lambda is set.
	virtual bool IsEmpty() const override { return false; }

	// NOT transferable across sessions: m_parentBc points into the COMPILING
	// session's bytecode (see the lifetime note above), and m_capturedFrames holds
	// that session's locals. Dispatched elsewhere this runs one session's bytecode
	// on another's interpreter, and once the owner closes the pointer dangles.
	virtual bool IsTransferable() const override { return false; }
	virtual bool Dispatch(ibProcUnit* runtime, ibValue** args, long argc, ibValue& outCancel) override;


	ibValueFunction(const ibByteCode* parentBc, long funcIndex)
		: ibValue(ibValueTypes::TYPE_FUNCTION),
		  m_parentBc(parentBc),
		  m_funcIndex(funcIndex)
	{}

	const ibByteCode*                  GetParentBc()   const { return m_parentBc; }
	long                               GetFuncIndex()  const { return m_funcIndex; }
	const ibByteCode::ibByteFunction*  GetFunction()   const {
		if (m_parentBc == nullptr) return nullptr;
		if (m_funcIndex < 0 || m_funcIndex >= (long)m_parentBc->m_listFunc.size())
			return nullptr;
		return &m_parentBc->m_listFunc[m_funcIndex];
	}

	// Captured enclosing-frame chain — populated at OPER_LFUNC
	// materialise. [0] = direct enclosing fn frame, [1] = outer-outer,
	// ... down to root. Only heap-promoted ancestors land here; stack-
	// allocated frames (caller's fn has m_needsHeapFrame=false) are
	// skipped by the weak_from_this().lock() guard at capture time.
	// Vector size = nesting depth of heap-promoted ancestors at
	// materialise site (≤ closure nesting count, not per-reference).
	// At OPER_CALL_LAMBDA invoke, shim's m_ppArrayContext[k+1] is wired to
	// m_capturedFrames[k]->m_pRefLocVars for the call duration.
	std::vector<std::shared_ptr<ibRunContext>> m_capturedFrames;

	// ⭐⭐ AND THE MODULE'S OWN FRAME, WHICH IS NOT ONE OF THEM. The chain above holds frames that
	// had to be HEAP-PROMOTED to survive the call that made them; a module body's frame needs no
	// promoting because it never ends while the module is loaded — it is a member of the
	// ibProcUnit (ibRunLifetime::Retained). So `weak_from_this().lock()` returns nothing for it and
	// the capture walk skipped it, silently: a lambda could not see the module's own variables at
	// all, and read whatever sat at that depth in the shim instead.
	//
	// Measured 2026-09-08, and it was quiet in the worst way — an ANSWER, not a refusal:
	//     var s = "hi"; var f = Function(x){ return s; };  f(0)  →  "Data"
	// The depth the compiler emitted (one frame up) landed on the session shim's parent layer,
	// whose slot 0 is the platform's `Data` scope. A named function reading the same variable was
	// always correct, which is what says this is a defect and not a rule: two roads to one name.
	//
	// It sits AFTER the captured frames in the installed list, because that is the order the
	// compiler counts in — a lambda inside a procedure reaches its procedure's locals at depth 1
	// and the module's at depth 2 (ibCompileContext::GetVariable, numParent - numContext).
	//
	// ⚠ A BARE POINTER, deliberately: the frame belongs to the module's ibProcUnit and outlives
	// every lambda made in it, exactly as `m_parentBc` — the bytecode of that same module — does.
	// This value already may not outlive its session (IsTransferable is false, and the note above
	// says why); this adds no lifetime that was not already there.
	ibRunContext* m_moduleFrame = nullptr;

	// Cached at OPER_LFUNC materialise from the bytecode-side fn's
	// m_needsHeapFrame. OPER_CALL_LAMBDA reads it as a single field
	// access on the lambda value, no need to dereference through
	// m_parentBc->m_listFunc[m_funcIndex]. True ⇒ this lambda's own
	// frame must be heap-promoted at invoke time (some inner lambda
	// captures from it).
	bool m_needsHeapFrame = false;

	// Dispatch the lambda body. Resolves the session's lambda runtime
	// (root mm's procUnit when session-bound; current dispatching
	// ProcUnit on codeRunner sandbox), swaps m_pByteCode = parentBc
	// for the call duration, runs Execute, restores bc on exit (incl.
	// exceptions). Re-entrant lambda-in-lambda calls are safe because
	// ProcUnit::Execute snapshots m_pByteCode at entry.
	void Execute(ibRunContext* pContext, ibValue* pvarRetValue, bool bDelta)
	{
		const auto* bfn = GetFunction();
		if (bfn == nullptr)
			ibBackendCoreException::Error(_("Cannot call: function value is not initialised"));

		ibProcUnitState* const state = ibSession::GetPUState();
		if (state == nullptr)
			ibBackendCoreException::Error(_("Cannot call function value without interpreter state"));

		ibProcUnit* runtime = state->GetLambdaRuntime();
		if (runtime == nullptr)
			ibBackendCoreException::Error(_("No runtime available for function-value call"));

		// Swap m_pByteCode to the lambda's parent for the dispatch
		// duration. Execute snapshots m_pByteCode at entry, so nested
		// lambda-in-lambda calls don't clobber an outer view. Restore
		// on both normal return and unwind.
		const ibByteCode* prevBc = runtime->m_pByteCode;
		runtime->m_pByteCode = m_parentBc;

		// Closure capture (Phase B) — install captured frames as
		// extra layers in shim's m_ppArrayContext for the call
		// duration. Lambda body OPER_GET / OPER_SET at depth k+1
		// (k in [0, N)) reads/writes through m_capturedFrames[k]'s
		// m_pRefLocVars. Pre-existing parent layers (root, common
		// modules) shift from positions [1..] to [N+1..] so the
		// previously-emitted depths still hit the correct frames.
		// Allocation done per invoke (could pool later); freed on
		// unwind. Nothing captured and no module frame → original list
		// untouched, zero-overhead fast path.
		ibRunContext** prevList = runtime->m_ppArrayContext;
		ibRunContext** newList  = nullptr;
		bool           ownsList = false;   // true = this invoke malloc'd it and must free it
		unsigned int origSize = 0;
		const unsigned int N  = (unsigned int)m_capturedFrames.size();

		// The module's own frame is one more layer, and it goes AFTER the captured ones — the
		// order the compiler counted the depths in. See ibValueFunction::m_moduleFrame.
		const unsigned int M  = (m_moduleFrame != nullptr) ? 1u : 0u;
		const unsigned int L  = N + M;   // layers this invoke inserts

		if (L > 0) {
			origSize = runtime->GetParentCount() + 2;

			// 🛑 NOT A FRESH ARRAY PER INVOKE. A pipeline calls its lambda ONCE PER ROW, and this
			// allocated and freed a context list every time — a malloc/free pair per row of every
			// filter and every projection that captured anything, for a list whose CONTENTS are
			// rebuilt identically each time. The buffer belongs to the lambda, which is the thing
			// that is invoked repeatedly; it is refilled (a handful of pointer writes) instead of
			// re-obtained.
			//
			// ⚠ ONE BUFFER, SO ONE USER AT A TIME. A lambda that re-enters itself — recursion, or
			// an inner pipeline over the same function value — would refill the list the outer
			// invoke is still reading. The depth counter says when that is happening and that
			// invoke allocates, exactly as before; the common path never does.
			if (m_listInUse == 0) {
				m_contextScratch.resize((size_t)origSize + L);
				newList = m_contextScratch.data();
			}
			else {
				newList  = new ibRunContext*[origSize + L];
				ownsList = true;
			}
			// [0] stays own — depth=0 in macros reads pRefLocVars directly,
			// m_ppArrayContext[0] is unused in normal execution but kept for
			// the bDelta=true case where slot=-1 lands here.
			newList[0] = prevList ? prevList[0] : nullptr;
			for (unsigned int k = 0; k < N; ++k) {
				newList[k + 1] = m_capturedFrames[k].get();
			}
			if (M != 0)
				newList[N + 1] = m_moduleFrame;
			for (unsigned int i = 1; i < origSize; ++i) {
				newList[i + L] = prevList ? prevList[i] : nullptr;
			}
			runtime->m_ppArrayContext = newList;
			++m_listInUse;
		}

		try {
			runtime->Execute(pContext, pvarRetValue, bDelta);
		}
		catch (...) {
			runtime->m_pByteCode = prevBc;
			if (newList) {
				runtime->m_ppArrayContext = prevList;
				--m_listInUse;
				if (ownsList) delete[] newList;
			}
			throw;
		}
		runtime->m_pByteCode = prevBc;
		if (newList) {
			runtime->m_ppArrayContext = prevList;
			--m_listInUse;
			if (ownsList) delete[] newList;
		}
	}

private:

	const ibByteCode* m_parentBc  = nullptr;
	long              m_funcIndex = -1;

	// The context list this lambda installs while it runs — see Execute. Held here because the
	// lambda is the thing that gets invoked over and over, so the buffer's life should be the
	// lambda's, not the call's. m_listInUse counts live invokes, so a re-entrant one does not
	// refill a buffer an outer invoke is still reading.
	std::vector<ibRunContext*> m_contextScratch;
	int                        m_listInUse = 0;

};

// Typed-tag fast casts — replace RTTI walk (ConvertToType<T>() →
// dynamic_cast through wxClassInfo) with a single m_typeClass compare
// + static_cast. Walk the TYPE_REFFER alias chain one step (CopyValue
// for TYPE_VALUE / TYPE_ENUM / TYPE_OLE / TYPE_FUNCTION / TYPE_ITERATOR
// emits a TYPE_REFFER wrapper around the original object, so callers
// often pass that wrapper rather than the underlying value).
//
// Contract: tag invariant on ibValueFunction / ibValueIterator —
// their ctors stamp m_typeClass = TYPE_FUNCTION / TYPE_ITERATOR
// respectively. Anyone subclassing must keep the tag, or AsFunction /
// AsIterator returns nullptr.
inline ibValueFunction* AsFunction(const ibValue* v) {
	if (v == nullptr) return nullptr;
	const ibValue* target = (v->IsReference()) ? v->m_pConstRef : v;
	return (target != nullptr && target->m_typeClass == ibValueTypes::TYPE_FUNCTION)
		? static_cast<ibValueFunction*>(const_cast<ibValue*>(target)) : nullptr;
}
inline ibValueFunction* AsFunction(const ibValue& v) { return AsFunction(&v); }

inline ibValueIterator* AsIterator(const ibValue* v) {
	if (v == nullptr) return nullptr;
	const ibValue* target = (v->IsReference()) ? v->m_pConstRef : v;
	return (target != nullptr && target->m_typeClass == ibValueTypes::TYPE_ITERATOR)
		? static_cast<ibValueIterator*>(const_cast<ibValue*>(target)) : nullptr;
}
inline ibValueIterator* AsIterator(const ibValue& v) { return AsIterator(&v); }

#pragma endregion

#pragma region value_helpers
// File-scope helpers shared by procUnit.cpp's Execute switch and
// procUnitLINQ.cpp's LINQ runtime. Kept inline (defined in this
// header) so both TUs see identical semantics; ODR holds because
// every TU sees the same single definition.

inline void CopyValue(ibValue& cValue1, ibValue& cValue2)
{
	if (&cValue1 == &cValue2)
		return;

	// STRING ONTO STRING KEEPS THE BUFFER, and the check has to come BEFORE Reset()
	// because Reset() is exactly what throws the buffer away. Overwriting the text
	// reuses the allocation, so a loop assigning into the same slot pays for one.
	if (!cValue1.m_bReadOnly &&
		cValue1.m_typeClass == ibValueTypes::TYPE_STRING && cValue1.m_pStr != nullptr &&
		cValue2.m_typeClass == ibValueTypes::TYPE_STRING) {
		if (cValue2.m_pStr != nullptr) *cValue1.m_pStr = *cValue2.m_pStr;
		else cValue1.m_pStr->Clear();                 // null source = the empty string
		return;
	}

	//checking variable availability and checking references
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(cValue2);
		return;
	}
	else {
		cValue1.Reset();   // DecrRef ref / delete string buffer / → TYPE_EMPTY
	}

	if (cValue2.m_typeClass == ibValueTypes::TYPE_REFFER) {
		cValue1 = cValue2.GetValue();
		return;
	}

	cValue1.m_typeClass = cValue2.m_typeClass;

	switch (cValue2.m_typeClass)
	{
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		cValue1.m_bData = cValue2.m_bData;
		break;
	case ibValueTypes::TYPE_NUMBER:
		cValue1.m_fData = cValue2.m_fData;
		break;
	case ibValueTypes::TYPE_STRING:
		// Buffer copy, exactly like ibValue::Copy. SetString(GetString()) went
		// ibString -> wxString -> ibString: two conversions and two allocations
		// per copied string, on the interpreter's per-instruction path. The
		// destination was Reset() above, so its m_pStr is already null — no
		// second Reset needed either.
		cValue1.m_pStr = cValue2.m_pStr ? new ibString(*cValue2.m_pStr) : nullptr;
		break;
	case ibValueTypes::TYPE_DATE:
		cValue1.m_dData = cValue2.m_dData;
		break;
	case ibValueTypes::TYPE_REFFER:
		cValue1.m_pRef = cValue2.m_pRef; cValue1.m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_CONST_REFFER:
		// weak non-owning const ref — share the pointer, NO IncrRef (the metadata
		// tree owns it). Object-write protection is by type, not m_bReadOnly.
		cValue1.m_pConstRef = cValue2.m_pConstRef;
		break;
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		cValue1.m_typeClass = ibValueTypes::TYPE_REFFER;
		cValue1.m_pRef = &cValue2; cValue1.m_pRef->IncrRef();
		break;
	default: cValue1.m_typeClass = ibValueTypes::TYPE_EMPTY;
	}
}

// CopyValue from rvalue — store into a local lvalue first, delegate
// to the mutable overload (so TYPE_OLE/ENUM/VALUE aliasing path can
// take &cValue2 of a stable address).
inline void CopyValue(ibValue& cValue1, ibValue&& cValue2)
{
	ibValue tmp = std::move(cValue2);
	CopyValue(cValue1, tmp);
}

// Lets go of one reference — the deleter of a guard that ADOPTS the reference a destination held, so the
// object is released once, when the guard goes, and never counted up and down in between.
struct ibReleaseRef {
	void operator()(ibValue* ref) const { ref->DecrRef(); }
};

// CopyValue from const source — direct field-copy without going
// through CloneValue(). Same semantics as the mutable overload: simple
// types are value-copied, TYPE_REFFER shares the m_pRef pointer,
// TYPE_OLE/ENUM/VALUE alias cValue2 via GetRef() (the aliasing IS
// the shared-state mechanism for these types). GetRef() is a const
// method on ibValue and encapsulates the necessary cast — caller
// stays cast-free.
inline void CopyValue(ibValue& cValue1, const ibValue& cValue2)
{
	// 🛑 THE DESTINATION'S PREVIOUS TAG, read before it is overwritten. It is the only
	// thing that says whether m_pStr is a buffer WE own — to be reused or released —
	// or a stale alias of m_pRef that must never be touched. Without it this function
	// either leaks the old buffer or deletes a pointer that was never an ibString;
	// SetString() used to supply the answer by Reset()ing, at the price of the buffer.
	const ibValueTypes destWas = cValue1.m_typeClass;
	ibString* const destBuffer =
		(destWas == ibValueTypes::TYPE_STRING) ? cValue1.m_pStr : nullptr;

	// A destination that HELD a string and is about to become something else releases
	// it here — once the tag has moved on, nothing downstream knows the pointer was a
	// buffer. (The old SetString() road freed it only when the new value was a string
	// too, so a string overwritten by a number was simply lost.)
	if (destBuffer != nullptr && cValue2.m_typeClass != ibValueTypes::TYPE_STRING) {
		delete cValue1.m_pStr;
		cValue1.m_pStr = nullptr;
	}

	// 🛑⭐⭐ AND A DESTINATION THAT HELD AN OBJECT LETS IT GO — this overload never did. It is the LET road
	// (`r = …` in a script: procUnit's OPER_LET reads its source const), and it overwrote an object
	// reference without the DecrRef every other road makes (the mutable overload through Reset(), the
	// destructor too). So the object a variable held before an assignment was never released: MEASURED
	// 2026-09-10 with a data breakpoint on one record set line's count — +1 when the call handed it back,
	// +1 when `r = rs.Add()` copied it in, -1 when the call's temporary died, and nothing at `r = Undefined`.
	// The line kept its set alive, and the set its rows: ~5 KB per register row a posting wrote, never
	// returned (a 32-bit client ran out of address space on a bench).
	//
	// The destination's reference passes to a guard that releases the object once the copy is done:
	// `x = x.Child` reads its source out of the object the destination is letting go, and the guard lets
	// it go on every way out of here, a throwing copy included. A bare pointer, not an ibValuePtr — that
	// one IS an ibValue, built and torn down on every assignment, and it cost the LET road 24% (measured
	// 2026-09-10: 1M × 3 assignments, 110 → 136 ms).
	const std::unique_ptr<ibValue, ibReleaseRef> destHeld(destWas == ibValueTypes::TYPE_REFFER ? cValue1.m_pRef : nullptr);

	cValue1.m_typeClass = cValue2.m_typeClass;
	switch (cValue2.m_typeClass)
	{
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		cValue1.m_bData = cValue2.m_bData;
		break;
	case ibValueTypes::TYPE_NUMBER:
		cValue1.m_fData = cValue2.m_fData;
		break;
	case ibValueTypes::TYPE_STRING:
		// STRING ONTO STRING KEEPS THE BUFFER. This is the LET road — `b = a` — so a
		// loop assigning into the same slot would otherwise free and remake the buffer
		// on every iteration; overwriting the text reuses the allocation instead.
		if (destBuffer != nullptr) {
			if (cValue2.m_pStr != nullptr) *destBuffer = *cValue2.m_pStr;
			else destBuffer->Clear();                    // null source = the empty string
		}
		else {
			cValue1.m_pStr = cValue2.m_pStr ? new ibString(*cValue2.m_pStr) : nullptr;
		}
		break;
	case ibValueTypes::TYPE_DATE:
		cValue1.m_dData = cValue2.m_dData;
		break;
	case ibValueTypes::TYPE_REFFER:
		cValue1.m_pRef = cValue2.m_pRef; cValue1.m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_CONST_REFFER:
		// weak non-owning const ref — share the pointer, NO IncrRef. Object-write
		// protection is by type, not m_bReadOnly.
		cValue1.m_pConstRef = cValue2.m_pConstRef;
		break;
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		cValue1.m_typeClass = ibValueTypes::TYPE_REFFER;
		cValue1.m_pRef = cValue2.GetRef(); cValue1.m_pRef->IncrRef();
		break;
	default: cValue1.m_typeClass = ibValueTypes::TYPE_EMPTY;
	}
}

// 🛑 A MOVE LETS GO OF WHAT THE DESTINATION HELD — a string buffer it owned, or the object it referenced —
// as ibValue::Move always did (through Reset) and the const CopyValue above does now. This one stamped the
// new value over both, the same defect as the LET road. It has no caller today, which is how it outlived
// that fix; it is kept correct so the next caller does not inherit the leak. Both are carried by guards
// and go when the function does — after the source is reset, since a source that lives inside the object
// the destination held must not be reset in freed memory.
inline void MoveValue(ibValue&& cValue1, ibValue&& cValue2)
{
	if (&cValue1 == &cValue2)
		return;

	const ibValueTypes destWas = cValue1.m_typeClass;
	const std::unique_ptr<ibString> destBuffer(destWas == ibValueTypes::TYPE_STRING ? cValue1.m_pStr : nullptr);
	const std::unique_ptr<ibValue, ibReleaseRef> destHeld(destWas == ibValueTypes::TYPE_REFFER ? cValue1.m_pRef : nullptr);   // adopts the destination's reference

	cValue1.m_typeClass = cValue2.m_typeClass;

	switch (cValue2.m_typeClass)
	{
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		cValue1.m_bData = std::move(cValue2.m_bData);
		break;
	case ibValueTypes::TYPE_NUMBER:
		cValue1.m_fData = std::move(cValue2.m_fData);
		break;
	case ibValueTypes::TYPE_STRING:
		// A move MOVES the buffer — same as ibValue::Move. The source is an
		// expiring value (Reset() at the tail of this function), so nulling
		// m_pStr here is what keeps that Reset from freeing what we took.
		cValue1.m_pStr = cValue2.m_pStr; cValue2.m_pStr = nullptr;
		break;
	case ibValueTypes::TYPE_DATE:
		cValue1.m_dData = std::move(cValue2.m_dData);
		break;
	case ibValueTypes::TYPE_REFFER:
		cValue1.m_pRef = cValue2.m_pRef;
		cValue1.m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_CONST_REFFER:
		// weak non-owning const ref — share the pointer, NO IncrRef. Object-write
		// protection is by type, not m_bReadOnly.
		cValue1.m_pConstRef = cValue2.m_pConstRef;
		break;
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		cValue1.m_typeClass = ibValueTypes::TYPE_REFFER;
		cValue1.m_pRef = &cValue2;
		cValue1.m_pRef->IncrRef();
		break;
	default: cValue1.m_typeClass = ibValueTypes::TYPE_EMPTY;
	}

	cValue2.Reset();
}

inline bool IsEmptyValue(const ibValue& cValue1)
{
	return cValue1.IsEmpty();
}

#define IsHasValue(cValue1) (!IsEmptyValue(cValue1))

// --- SQL three-valued (Kleene) NULL mode for LINQ-filter predicate eval ------------
// When set (by ibValueWhereState around the predicate lambda) a comparison with a NULL
// operand yields UNKNOWN (empty) instead of a Boolean, so the Where keeps the row only
// on a definite TRUE — matching the SQL push-down (LowerLambdaPredicate) and the L3 RAM
// fold (RamEvalPredicate). Scoped + restored so GENERAL script comparisons stay two-valued.
// SPIKE: covers single comparisons / `<>` / AND / OR (AND/OR fall out of IsHasValue);
// NOT(unknown) still diverges (a follow-up needs Kleene NOT). See test_queryParity.
extern thread_local bool ts_threeValuedNullCompare;

struct ScopedThreeValuedNull {
	bool m_prev;
	ScopedThreeValuedNull() : m_prev(ts_threeValuedNullCompare) { ts_threeValuedNullCompare = true; }
	~ScopedThreeValuedNull() { ts_threeValuedNullCompare = m_prev; }
};

// A genuine SQL NULL operand — the `Null` literal / a NULL DB column (TYPE_NULL). NOT TYPE_EMPTY
// (Undefined = a composite with no type chosen) and NOT IsEmptyValue (which also reports Boolean
// false). This is what the three-valued (Kleene) comparison path keys on, matching the DB.
inline bool IsNullOperand(const ibValue& v) {
	return v.IsNull();
}

inline void SetTypeBoolean(ibValue& cValue1, bool bValue)
{
	//check variable availability and reference check
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(bValue);
		return;
	}
	cValue1.Reset();
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = bValue;
}

inline void SetTypeNumber(ibValue& cValue1, const ibNumber& fValue)
{
	//check variable availability and reference check
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(fValue);
		return;
	}
	cValue1.Reset();
	cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
	cValue1.m_fData = fValue;
}

#pragma endregion

#endif // _IB_PROC_UNIT_LAMBDA_H__
