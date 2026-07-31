#ifndef _IB_PROC_UNIT_VALUES_H__
#define _IB_PROC_UNIT_VALUES_H__

// Runtime-internal first-class ibValue subclasses:
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
// the same DLL — currently procUnitLinq.cpp — can hold these by value
// (state classes embed an ibValueFunction predicate / projection).
// The g_value{Iterator,Function} CLSIDs are header-defined inline constexpr
// below (constexpr + ODR-safe across DLLs); wxIMPLEMENT_DYNAMIC_CLASS still
// lives in procUnit.cpp.

#include "compileCode.h"
#include "procUnit.h"          // class ibProcUnit (friend grant for ibValueFunction)
#include "procUnitState.h"     // ibProcUnitState::GetLambdaRuntime()
#include "session/session.h"   // ibSession::GetPUState() — used by ibValueFunction::Execute
#include "backend/eventDispatcher.h"   // ibValueFunction IS-A dispatcher — a lambda dispatches by running its own body
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
	// At OPER_CALL_LAMBDA invoke, shim's m_pppArrayList[k+1] is wired to
	// m_capturedFrames[k]->m_pRefLocVars for the call duration.
	std::vector<std::shared_ptr<ibRunContext>> m_capturedFrames;

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
		// extra layers in shim's m_pppArrayList for the call
		// duration. Lambda body OPER_GET / OPER_SET at depth k+1
		// (k in [0, N)) reads/writes through m_capturedFrames[k]'s
		// m_pRefLocVars. Pre-existing parent layers (root, common
		// modules) shift from positions [1..] to [N+1..] so the
		// previously-emitted depths still hit the correct frames.
		// Allocation done per invoke (could pool later); freed on
		// unwind. Empty m_capturedFrames → original list untouched,
		// zero-overhead fast path.
		ibValue*** prevList   = runtime->m_pppArrayList;
		ibValue*** newList    = nullptr;
		unsigned int origSize = 0;
		const unsigned int N  = (unsigned int)m_capturedFrames.size();
		if (N > 0) {
			origSize = runtime->GetParentCount() + 2;
			newList  = new ibValue**[origSize + N];
			// [0] stays own — depth=0 in macros reads pRefLocVars directly,
			// m_pppArrayList[0] is unused in normal execution but kept for
			// the bDelta=true case where slot=-1 lands here.
			newList[0] = prevList ? prevList[0] : nullptr;
			for (unsigned int k = 0; k < N; ++k) {
				newList[k + 1] = m_capturedFrames[k]->m_pRefLocVars;
			}
			for (unsigned int i = 1; i < origSize; ++i) {
				newList[i + N] = prevList ? prevList[i] : nullptr;
			}
			runtime->m_pppArrayList = newList;
		}

		try {
			runtime->Execute(pContext, pvarRetValue, bDelta);
		}
		catch (...) {
			runtime->m_pByteCode = prevBc;
			if (newList) {
				runtime->m_pppArrayList = prevList;
				delete[] newList;
			}
			throw;
		}
		runtime->m_pByteCode = prevBc;
		if (newList) {
			runtime->m_pppArrayList = prevList;
			delete[] newList;
		}
	}

private:

	const ibByteCode* m_parentBc  = nullptr;
	long              m_funcIndex = -1;
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
// procUnitLinq.cpp's LINQ runtime. Kept inline (defined in this
// header) so both TUs see identical semantics; ODR holds because
// every TU sees the same single definition.

inline void CopyValue(ibValue& cValue1, ibValue& cValue2)
{
	if (&cValue1 == &cValue2)
		return;

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
		cValue1.SetString(cValue2.GetString());
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

// CopyValue from const source — direct field-copy without going
// through Clone(). Same semantics as the mutable overload: simple
// types are value-copied, TYPE_REFFER shares the m_pRef pointer,
// TYPE_OLE/ENUM/VALUE alias cValue2 via GetRef() (the aliasing IS
// the shared-state mechanism for these types). GetRef() is a const
// method on ibValue and encapsulates the necessary cast — caller
// stays cast-free.
inline void CopyValue(ibValue& cValue1, const ibValue& cValue2)
{
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
		cValue1.SetString(cValue2.GetString());
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

inline void MoveValue(ibValue&& cValue1, ibValue&& cValue2)
{
	if (&cValue1 == &cValue2)
		return;

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
		cValue1.SetString(cValue2.GetString());
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

#endif // _IB_PROC_UNIT_VALUES_H__
