////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : Processor unit 
////////////////////////////////////////////////////////////////////////////

#include "compileCode.h"
#include "procUnit.h"
#include "procUnitValues.h"    // ibValueIterator / ibValueFunction / AsFunction / AsIterator
#include "procUnitState.h"

#include "debugger/debugServer.h"
#include "system/systemManager.h"
#include "session/session.h"   // ibSession::GetPUState() / GetLambdaRuntime()

#include "appData.h"

#include <algorithm>
#include <utility>   // std::forward — the variadic Raise below

// Operand resolution for the bytecode interpreter. Three slot kinds:
//   slot <= 0           — local frame (mutable).
//   slot == DEF_VAR_CONST — constant in the bytecode template (read-only).
//   else                — outer frame (mutable).
//
// Two access flavours, picked at the call site by the opcode handler:
//   variable(x)  — write target / read-modify-write. const-slot is a
//                  logic error (writing to a constant); throws.
//   cvariable(x) — read-only source. const-slot is returned directly,
//                  no copy and no const_cast.
namespace {

// Outer-frame slot fetch with null guards. The bare deref
// `*pppArrayList[slot + offset][idx]` segfaults (SEH access violation)
// when the outer frame for that depth was never wired — the canonical
// trigger is debugger Evaluate inside a lambda body: eval's pppArrayList
// is laid out as [0=own, 1=host=lambda, 2=host's parent module, …], but
// a lambda has no parent-module frame, so depth ≥ 2 lands on null. The
// guards convert the segfault into an ibBackendException so the watch
// panel surfaces an error message instead of taking down the process.
inline ibValue*** ResolveOuterFrame(int slot, ibValue*** pppArrayList, bool bDelta)
{
	const int depth = slot + (bDelta ? 1 : 0);
	if (pppArrayList == nullptr || depth < 0)
		return nullptr;
	return &pppArrayList[depth];
}

// Operand resolution is hot/cold split, and the reason is visible in the
// disassembly rather than deduced: before the split, ResolveWrite and
// ResolveRead were the two most-called targets inside ibProcUnit::Execute —
// 163 and 100 call sites — and they stayed calls even under whole-program
// optimisation, which inlined ibNumber::Compare in the same function.
//
// They carried `inline` and were ignored, because a body containing
// ibBackendCoreException::Error (varargs formatting + gettext) is far past
// any inliner's budget, and the /GS stack-cookie prologue that came with it
// made the frame bigger still. The common case, meanwhile, is ONE line: a
// slot in the current frame. Every operand of every opcode paid a call with
// a security-cookie prologue to run it.
//
// Split keeps the frame-local (and, for reads, the constant-pool) case in the
// inlineable part and pushes the outer-frame walk and every raise out of line.
// Behaviour is identical — the cold helper is reached on exactly the inputs
// the old body would have fallen through to.

// --- cold halves: the outer-frame walk and every raise ---------------------
// Defined FIRST so the hot wrappers below can call them without a forward
// declaration — one signature per function instead of two kept in step by hand.

ibValue& ResolveWriteOuter(int slot, int idx, ibValue*** pppArrayList, bool bDelta)
{
	if (slot == DEF_VAR_CONST)
		ibBackendCoreException::Error(_("Attempt to write to a constant value"));
	auto* row = ResolveOuterFrame(slot, pppArrayList, bDelta);
	if (row == nullptr || *row == nullptr || (*row)[idx] == nullptr) {
		ibBackendCoreException::Error(
			_("Outer frame not bound at depth %d / idx %d (eval inside lambda?)"),
			slot + (bDelta ? 1 : 0), idx);
	}
	return *(*row)[idx];
}

const ibValue& ResolveReadOuter(int slot, int idx, ibValue*** pppArrayList, bool bDelta)
{
	auto* row = ResolveOuterFrame(slot, pppArrayList, bDelta);
	if (row == nullptr || *row == nullptr || (*row)[idx] == nullptr) {
		ibBackendCoreException::Error(
			_("Outer frame not bound at depth %d / idx %d (eval inside lambda?)"),
			slot + (bDelta ? 1 : 0), idx);
	}
	return *(*row)[idx];
}

// `inline` alone is not enough here and the disassembly says so: after the
// split above, ibProcUnit::Execute was byte-for-byte the same size and still
// carried the 163 + 100 calls. With 263 call sites in a function already 27 KB
// long, the inliner's size budget declines even a two-instruction body — so the
// split had added an indirection without removing the call. Forcing it is the
// point of the split, not a decoration.
#if defined(_MSC_VER)
#  define IB_FORCEINLINE __forceinline
#  define IB_NOINLINE    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#  define IB_FORCEINLINE inline __attribute__((always_inline))
#  define IB_NOINLINE    __attribute__((noinline))
#else
#  define IB_FORCEINLINE inline
#  define IB_NOINLINE
#endif

// --- raise helpers ---------------------------------------------------------
// Every ibBackendCoreException::Error(_("…"), arg) expands, at the call site,
// into wxString + wxFormatString construction and destruction. Inside
// ibProcUnit::Execute that added up to ~163 wx calls scattered through the
// interpreting loop — code that never runs in a healthy execution but is
// permanently part of the function: it inflates it (33 KB), competes for the
// instruction cache and constrains register allocation around the opcodes that
// DO run.
//
// Same medicine as the Resolve* split, and found the same way — by reading the
// disassembly of Execute rather than guessing. The formatting moves behind a
// noinline call, so an opcode's failure path costs one instruction here and the
// message machinery lives out of the loop.
// One raise entry point, not a family. The message is selected by CODE from the
// table the exception layer already owns (backend_exception.h enum +
// gs_listErrorString), so the call site carries a constant instead of building
// a wxString from a literal — which is the whole point: an inline _("…") drags
// wxString + wxFormatString construction into the interpreting loop.
//
// Three overloads because the argument differs, not the mechanism: none, a name
// already at hand, or a value whose text has to be spelled (GetString runs HERE,
// off the hot path).
template <class... Args>
IB_NOINLINE void Raise(int code, Args&&... args)
{
	ibBackendCoreException::Error(code, std::forward<Args>(args)...);
}

// NOT folded into the variadic above, and the distinction is load-bearing: this
// overload calls GetString() INSIDE itself, off the hot path. Forwarding an
// ibValue instead would put the wxString construction back at the call site —
// inside Execute — which is exactly what moving the raises out was for.
IB_NOINLINE void Raise(int code, const ibValue& value)
{
	ibBackendCoreException::Error(code, value.GetString());
}

// Both sit in OPER_CALL_METHOD / OPER_CALL — the hottest branches after the
// arithmetic ones, and the two that LINQ and recursion run through per element.


IB_FORCEINLINE ibValue& ResolveWrite(int slot, int idx,
							  ibValue** pRefLocVars,
							  ibValue*** pppArrayList,
							  bool bDelta)
{
	if (slot <= 0)
		return *pRefLocVars[idx];
	return ResolveWriteOuter(slot, idx, pppArrayList, bDelta);
}

IB_FORCEINLINE const ibValue& ResolveRead(int slot, int idx,
								   ibValue** pRefLocVars,
								   ibValue*** pppArrayList,
								   const ibByteCode* pByteCode,
								   bool bDelta)
{
	if (slot <= 0)
		return *pRefLocVars[idx];
	if (slot == DEF_VAR_CONST)
		return pByteCode->m_listConst[idx];
	return ResolveReadOuter(slot, idx, pppArrayList, bDelta);
}

} // namespace

#define curCode	m_pByteCode->m_listCode[lCodeLine]

#define index1	curCode.m_param1.m_numIndex
#define index2	curCode.m_param2.m_numIndex
#define index3	curCode.m_param3.m_numIndex
#define index4	curCode.m_param4.m_numIndex

#define array1	curCode.m_param1.m_numArray
#define array2	curCode.m_param2.m_numArray
#define array3	curCode.m_param3.m_numArray
#define array4	curCode.m_param4.m_numArray

#define locVariable1 *m_pRefLocVars[index1]
#define locVariable2 *m_pRefLocVars[index2]
#define locVariable3 *m_pRefLocVars[index3]
#define locVariable4 *m_pRefLocVars[index4]

#define variable(x)  ResolveWrite(array##x, index##x, pRefLocVars, m_pppArrayList, bDelta)

#define variable1 variable(1)
#define variable2 variable(2)
#define variable3 variable(3)
#define variable4 variable(4)

#define cvariable(x) ResolveRead (array##x, index##x, pRefLocVars, m_pppArrayList, m_pByteCode, bDelta)

#define cvariable1 cvariable(1)
#define cvariable2 cvariable(2)
#define cvariable3 cvariable(3)
#define cvariable4 cvariable(4)

//**************************************************************************************************************
//*                                              support error place                                           *
//**************************************************************************************************************

// Interpreter-state methods on ibProcUnitState — the canonical access
// point is ibSession::GetPUState()->X. Out-of-line here because they
// need the full ibRunContext / ibByteCode types (procUnitState.h only
// forward-declares them).

ibProcUnit* ibProcUnitState::GetLambdaRuntime()
{
	// Primary path: session's lambda runtime (allocated in CreateRoot,
	// parent = root mm's procUnit). All lambdas in the session route
	// through it.
	if (auto* sess = ibSession::Current())
		if (ibProcUnit* rt = sess->GetLambdaRuntime())
			return rt;
	// Fallback: no session bound (codeRunner sandbox path). Dispatch
	// on whoever is currently executing.
	return m_currentRunModule;
}

const ibByteCode* ibProcUnitState::GetCurrentByteCode() const
{
	const ibRunContext* rc = GetCurrentRunContext();
	return rc != nullptr ? rc->GetByteCode() : nullptr;
}

void ibProcUnitState::Raise()
{
	m_errorPlace.Reset();
	m_errorPlace.m_skipByteCode = GetCurrentByteCode();
}

void ibProcUnit::Reset()
{
	if (m_pppArrayList != nullptr) {
		wxDELETEA(m_pppArrayList);
	}

	if (m_ppArrayCode != nullptr) {
		wxDELETEA(m_ppArrayCode);
	}

	// Clear the active session's currentRunModule pointer if it was us.
	// Hits the per-session interpreter state directly through GetPUState
	// rather than going through any TLS forwarder.
	if (auto* state = ibSession::GetPUState()) {
		if (state->GetCurrentRunModule() == this)
			state->ClearCurrentRunModule();
	}

	m_numAutoDeleteParent = 0;

	m_pppArrayList = nullptr;
	m_ppArrayCode = nullptr;
	m_pByteCode = nullptr;
	m_bExecuted = false;
}

//**************************************************************************************************************
//*                                              stack support                                                 *
//**************************************************************************************************************

inline void BeginByteCode(ibRunContext* pCode) {
	if (auto* st = ibSession::GetPUState()) st->AddRunContext(pCode);
}
inline bool EndByteCode()
{
	auto* st = ibSession::GetPUState();
	unsigned int n = st ? st->GetCountRunContext() : 0;
	if (n > 0 && st)
		st->BackRunContext();
	n--;
	if (n <= 0)
		return false;
	return true;
}

//Stack reset
inline void ResetByteCode() { while (EndByteCode()); }

struct ibProcStackGuard {

	ibProcStackGuard(ibRunContext* runContext) {
		// Active state is required — ibProcUnit::Execute is reached only
		// through a bound session (ibSessionScope / ibSessionThreadBinding).
		auto* state = ibSession::GetPUState();
		wxASSERT(state != nullptr);
		if (state->m_recCount > MAX_REC_COUNT) { //critical error
			wxString strError;
			for (unsigned int i = 0; i < state->GetCountRunContext(); i++) {
				const ibRunContext* stackContext = state->GetRunContext(i);
				wxASSERT(stackContext);
				const ibByteCode* stackByteCode = stackContext->GetByteCode();
				wxASSERT(stackByteCode);
				strError += wxString::Format(wxT("\n%s (#line %d)"),
					stackByteCode->m_strModuleName,
					stackByteCode->m_listCode[stackContext->m_lCurLine].m_numLine + 1
				);
			}
			ibBackendCoreException::Error(_("Number of recursive calls exceeded the maximum allowed value!\nCall stack :") + strError);
		}
		state->m_recCount++;
		m_currentContext = runContext;
		BeginByteCode(runContext);
	}

	~ibProcStackGuard() {
		if (auto* state = ibSession::GetPUState()) state->m_recCount--;
		EndByteCode();
	}

private:
	ibRunContext* m_currentContext;
};

//**************************************************************************************************************
//*                                              inline functions                                              *
//**************************************************************************************************************

//checking variable availability
#define CHECK_READONLY(Operation)\
if(cValue1.m_bReadOnly)\
{\
 ibValue cVal;\
 Operation(cVal,cValue2,cValue3);\
 cValue1.SetValue(cVal);\
 return;\
}\
if(cValue1.m_typeClass==ibValueTypes::TYPE_REFFER)\
 cValue1.m_pRef->DecrRef();\

//Functions for quickly working with the ibValue type
inline void AddValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(AddValue);
	// Dispatch on the SOURCE type — do NOT pre-stamp cValue1.m_typeClass.
	// The string branch goes through SetString(), which Reset()s cValue1 on
	// its CURRENT type and frees the correct union member. Pre-stamping
	// TYPE_STRING (the old code) made Reset() treat a stale NON-string union
	// value (m_pStr aliases m_pRef) as an ibString* and delete it -> AV.
	// It only bit when the result slot already held a non-string value (a
	// reused temp inside a loop), so a single `s = s + "x"` was fine but the
	// same line in a While loop access-violated.
	const ibValueTypes resultType = cValue2.GetType();
	if (resultType == ibValueTypes::TYPE_NUMBER) {
		cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
		cValue1.m_fData = cValue2.GetNumber() + cValue3.GetNumber();
	}
	else if (resultType == ibValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == ibValueTypes::TYPE_DATE) { //date + date -> number
			cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
			// static_cast: wxLongLong_t is `long long`, while ibNumber's 64-bit ctor takes
			// int64_t — same width, different type on LP64 (there int64_t is `long`). MSVC
			// spells both __int64 and picks it; GCC finds every ctor equally far away and
			// calls the conversion ambiguous. Naming the target type settles it everywhere.
			cValue1.m_fData = cValue2.GetDate() + cValue3.GetDate();
		}
		else {
			cValue1.m_typeClass = ibValueTypes::TYPE_DATE;
			cValue1.m_dData = cValue2.m_dData + cValue3.GetDate();
		}
	}
	else {
		// Fused `s = s + expr`: the shortLet peephole rewrote the ADD dest to be
		// the LHS slot, so cValue1 (dest) and cValue2 (left) resolve to the SAME
		// ibValue. Append onto s in place instead of building `s + expr` into a
		// fresh string and copying it back — turns accumulate-in-a-loop from
		// O(n^2) into O(n). Fast path only when the slot holds a LIVE (non-null)
		// string buffer we own: a reused / moved-out slot can read TYPE_STRING
		// with m_pStr == null (see note above), which SetString() rebuilds safely
		// (GetString() is null-safe, so `"" + expr` is the correct result).
		if (&cValue1 == &cValue2 && &cValue1 != &cValue3 &&
			cValue1.m_typeClass == ibValueTypes::TYPE_STRING && cValue1.m_pStr) {
			ibString scratch;
			*cValue1.m_pStr += cValue3.GetString(scratch);
		}
		else {
			cValue1.SetString(cValue2.GetString() + cValue3.GetString());
		}
	}
}

inline void SubValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(SubValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() - cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == ibValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == ibValueTypes::TYPE_DATE) { //date - date -> number
			cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() - cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData - cValue3.GetDate();
		}
	}
	else {
		ibBackendCoreException::Error(_("Subtraction operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	}
}

inline void MultValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(MultValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() * cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == ibValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == ibValueTypes::TYPE_DATE) { //date * date -> number
			cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() * cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData * cValue3.GetDate();
		}
	}
	else {
		ibBackendCoreException::Error(_("Multiplication operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	}
}

inline void DivValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(DivValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		const ibNumber& flNumber3 = cValue3.GetNumber();
		if (flNumber3.IsZero())
			Raise(ERROR_DIVIDE_BY_ZERO);
		cValue1.m_fData = cValue2.GetNumber() / flNumber3;
	}
	else {
		ibBackendCoreException::Error(_("Division operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	};
}

inline void ModValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(ModValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		const ibNumber num3 = cValue3.GetNumber();
		if (num3.IsZero())
			Raise(ERROR_DIVIDE_BY_ZERO);
		const ibNumber num2 = cValue2.GetNumber();
		cValue1.m_fData = num2 % num3;
	}
	else {
		ibBackendCoreException::Error(_("Modulo operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	}
}

// Definition of the LINQ-filter three-valued NULL flag (declared in procUnitValues.h).
thread_local bool ts_threeValuedNullCompare = false;

// SQL three-valued NULL: under the flag, a comparison with a NULL operand yields UNKNOWN
// instead of a Boolean. UNKNOWN is represented as TYPE_NULL (SQL: unknown ≡ null), so it both
// reads as "null" to the Kleene NOT/AND/OR (IsNullOperand) and drops in the WHERE (IsHasValue is
// false for TYPE_NULL). Returns true when it set the result to UNKNOWN (caller skips).
inline bool CompareYieldsUnknown(ibValue& out, const ibValue& a, const ibValue& b)
{
	if (!ts_threeValuedNullCompare) return false;
	if (!IsNullOperand(a) && !IsNullOperand(b)) return false;
	out.m_typeClass = ibValueTypes::TYPE_NULL;
	return true;
}

//Implementation of comparison operators
inline void CompareValueGT(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueGT);
	if (CompareYieldsUnknown(cValue1, cValue2, cValue3)) return;
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGT(cValue3) > 0;   // three-way int -> boolean '>'
}

inline void CompareValueGE(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueGE);
	if (CompareYieldsUnknown(cValue1, cValue2, cValue3)) return;
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGE(cValue3);
}

inline void CompareValueLS(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueLS);
	if (CompareYieldsUnknown(cValue1, cValue2, cValue3)) return;
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLS(cValue3) < 0;   // three-way int -> boolean '<'
}

inline void CompareValueLE(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueLE);
	if (CompareYieldsUnknown(cValue1, cValue2, cValue3)) return;
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLE(cValue3);
}

inline void CompareValueEQ(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueEQ);
	if (CompareYieldsUnknown(cValue1, cValue2, cValue3)) return;
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueEQ(cValue3);
}

inline void CompareValueNE(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueNE);
	if (CompareYieldsUnknown(cValue1, cValue2, cValue3)) return;
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueNE(cValue3);
}

// CopyValue / MoveValue / IsEmptyValue / IsHasValue / SetTypeBoolean /
// SetTypeNumber moved to procUnitValues.h — procUnitLinq.cpp uses
// them as well. Math + compare helpers stay below (only Execute uses
// them).

// Was a macro that expanded BOTH raises at each of its three call sites, i.e.
// six formatted-message blocks inside the interpreting loop. The message choice
// is a property of the failure, not of the opcode, so it belongs in one
// out-of-line place — see the raise-helper note above.
IB_NOINLINE void RaiseMemberNotFound(const ibValue& variable, const wxString& name)
{
	// Which of the two it is depends on the VALUE, not on the opcode — so the
	// choice belongs here rather than at each of the three call sites.
	Raise(variable.IsReference() ? ERROR_MEMBER_NOT_FOUND : ERROR_MEMBER_NOT_AGGREGATE, name);
}

#define CheckAndError(variable, name) RaiseMemberNotFound(variable, name)

//Index arrays
inline bool SetArrayValue(ibValue& cValue1, const ibValue& cValue2, ibValue& cValue3)
{
	return cValue1.SetAt(cValue2, cValue3);
}

inline bool SetArrayValue(ibValue& cValue1, const ibValue& cValue2, ibValue&& cValue3)
{
	ibValue tmp = std::move(cValue3);
	return cValue1.SetAt(cValue2, tmp);
}

inline bool GetArrayValue(ibValue& cValue1, ibValue& cValue2, const ibValue& cValue3)
{
	ibValue retValue;
	if (cValue2.GetAt(cValue3, retValue)) {
		CopyValue(cValue1, retValue);
		return true;
	}
	return false;
}

inline ibValue GetValue(const ibValue& cValue1)
{
	if (cValue1.m_bReadOnly
		&& !cValue1.IsReference()) {
		ibValue cVal;
		CopyValue(cVal, cValue1);
		return cVal;
	}
	return cValue1;
}

// ibValueIterator / ibValueFunction class definitions moved to
// procUnitValues.h so procUnitLinq.cpp (and any other future LINQ-
// adjacent TU) can hold them by value. Implementation-side artefacts —
// wxIMPLEMENT_DYNAMIC_CLASS + the CLSID statics + ibValueFunction's
// out-of-line bits — stay in this TU.

// g_valueIterator / g_valueFunction are now header-defined inline constexpr (procUnitValues.h).

// LINQ machinery — CallLambdaWithArg / CallLambdaWith2Args /
// InvokeLambdaWithArg, the iterator-state classes, ibValueQuery,
// ibValueLinqDispatchImpl, ibValue::DispatchLinqMethod, and the
// FindLinqMethodByName resolver all live in procUnitLinq.cpp.

//////////////////////////////////////////////////////////////////////
//						Construction/Destruction                    //
//////////////////////////////////////////////////////////////////////

void ibProcUnit::Execute(ibRunContext* pContext, ibValue* pvarRetValue, bool bDelta)
{
	struct ibTryLabel {

		ibTryLabel() :m_lStartLine(0), m_lEndLine(0) {}
		ibTryLabel(const long& lStartLine, const long& lEndLine) :m_lStartLine(lStartLine), m_lEndLine(lEndLine) {}

		long m_lStartLine, m_lEndLine;
	};

#ifdef DEBUG
	if (pContext == nullptr) {
		ibBackendCoreException::Error(_("No execution context defined!"));
		if (m_pByteCode == nullptr)
			ibBackendCoreException::Error(_("No execution code set!"));
	}
#endif

	pContext->SetProcUnit(this);

	ibProcStackGuard stackGuard(pContext);

	ibValue** pRefLocVars = pContext->m_pRefLocVars;

	// Snapshot bc at entry. The session's lambda runtime swaps
	// m_pByteCode per OPER_CALL_LAMBDA dispatch; a nested lambda call
	// would clobber the outer frame's view if we read this->m_pByteCode
	// inside the loop. Local snapshot keeps each Execute invocation
	// pinned to the bc it started with.
	const ibByteCode* m_pByteCode = this->m_pByteCode;

	long lCodeLine = pContext->m_lStart;
	// Loop walks to the bytecode tail; explicit termination is on
	// OPER_RET / OPER_ENDFUNC / OPER_ENDLFUNC / OPER_END (all set
	// lCodeLine = lFinish; break). Lambda invocation lands m_lStart
	// at info->lo + 1 and runs until OPER_ENDLFUNC — same termination
	// path as named functions.
	long lFinish = (long)m_pByteCode->m_listCode.size();
	long lPrevLine = wxNOT_FOUND;

	std::vector<ibTryLabel> tryList;

	// Cache once per Execute — interpreter state pointer + session
	// pointer. Both stable for the whole Execute (binding doesn't
	// change while the script thread runs); going through GetPUState
	// per opcode would pay shared_lock + map find via Current() each
	// time. Single cache-line reads in the hot loop instead.
	ibSession*       const cancelSession = ibSession::Current();
	ibProcUnitState* const state         = ibSession::GetPUState();

	// evalMode is a session-level flag the debugger toggles ONLY around a
	// watch-eval (a separate nested Execute); it never changes within one
	// Execute frame. Resolve it ONCE here instead of calling
	// ibBackendException::IsEvalMode() (= Current() + acquire-load) twice per
	// opcode in the hot loop below. cancelSession already IS Current().
	const bool evalMode = (cancelSession != nullptr && cancelSession->IsEvalMode());

	// Cooperative cancel / force-exit abort long-running loops; a sub-us
	// latency is invisible, so poll them once every kCancelPoll opcodes rather
	// than paying two acquire-loads on EVERY instruction. opTick is global to
	// the run, so even a 3-opcode tight loop is polled ~every kCancelPoll/3
	// iterations. (docs: interpreter hot-loop per-opcode overhead trim.)
	constexpr unsigned kCancelPoll = 1024;   // power of two -> mask test, not modulo
	unsigned opTick = 0;

start_label:

	try { //slower by 2-3% for each nested module
		while (lCodeLine < lFinish) {

			if (!evalMode) {
				pContext->m_lCurLine = lCodeLine;
				if (state != nullptr) state->SetCurrentRunModule(this);
			}

			// Cooperative cancel + force-exit, polled every kCancelPoll opcodes
			// (see prologue) instead of two acquire-loads on the per-opcode path
			// — the hot-loop win; abort latency stays sub-microsecond.
			//   force-exit: admin kick / GUI close / debug Destroy -> break, let
			//               the host clean up.
			//   cancel:     admin Kick / pool CancelSession -> unwind via
			//               ibBackendInterruptException.
			if (((++opTick & (kCancelPoll - 1)) == 0) && cancelSession != nullptr) {
				if (cancelSession->IsForceExit())
					break;
				if (cancelSession->IsCancelRequested()) {
					cancelSession->ClearCancel();
					ibBackendInterruptException::Error();
				}
			}

			//enter in debugger
			if (debugServer != nullptr && !evalMode)
				debugServer->EnterDebugger(pContext, curCode, lPrevLine);

			switch (curCode.m_numOper)
			{
			case OPER_CONST: CopyValue(variable1, m_pByteCode->m_listConst[index2]); break;
			case OPER_CONSTN: SetTypeNumber(variable1, index2); break;
			case OPER_ADD: AddValue(variable1, cvariable2, cvariable3); break;
			case OPER_SUB: SubValue(variable1, cvariable2, cvariable3); break;
			case OPER_DIV: DivValue(variable1, cvariable2, cvariable3); break;
			case OPER_MOD: ModValue(variable1, cvariable2, cvariable3); break;
			case OPER_MULT: MultValue(variable1, cvariable2, cvariable3); break;
			case OPER_LET: CopyValue(variable1, cvariable2); break;
			case OPER_INVERT: SetTypeNumber(variable1, -cvariable2.GetNumber()); break;
			case OPER_NOT:
				// Kleene NOT(UNKNOWN)=UNKNOWN under the LINQ three-valued flag; else two-valued.
				if (ts_threeValuedNullCompare && IsNullOperand(cvariable2)) variable1.m_typeClass = ibValueTypes::TYPE_NULL;   // UNKNOWN == SQL NULL (IsNullOperand keys on TYPE_NULL)
				else SetTypeBoolean(variable1, IsEmptyValue(cvariable2));
				break;
			case OPER_AND:
				if (ts_threeValuedNullCompare) {            // FALSE dominates; else UNKNOWN if any; else TRUE
					const bool aF = !IsHasValue(cvariable2) && !IsNullOperand(cvariable2);
					const bool bF = !IsHasValue(cvariable3) && !IsNullOperand(cvariable3);
					if (aF || bF) SetTypeBoolean(variable1, false);
					else if (IsNullOperand(cvariable2) || IsNullOperand(cvariable3)) variable1.m_typeClass = ibValueTypes::TYPE_NULL;   // UNKNOWN == SQL NULL (IsNullOperand keys on TYPE_NULL)
					else SetTypeBoolean(variable1, true);
				}
				else if (IsHasValue(cvariable2) && IsHasValue(cvariable3)) SetTypeBoolean(variable1, true);
				else SetTypeBoolean(variable1, false);
				break;
			case OPER_OR:
				if (ts_threeValuedNullCompare) {            // TRUE dominates; else UNKNOWN if any; else FALSE
					if (IsHasValue(cvariable2) || IsHasValue(cvariable3)) SetTypeBoolean(variable1, true);
					else if (IsNullOperand(cvariable2) || IsNullOperand(cvariable3)) variable1.m_typeClass = ibValueTypes::TYPE_NULL;   // UNKNOWN == SQL NULL (IsNullOperand keys on TYPE_NULL)
					else SetTypeBoolean(variable1, false);
				}
				else if (IsHasValue(cvariable2) || IsHasValue(cvariable3)) SetTypeBoolean(variable1, true);
				else SetTypeBoolean(variable1, false);
				break;
			case OPER_EQ: CompareValueEQ(variable1, cvariable2, cvariable3); break;
			case OPER_NE: CompareValueNE(variable1, cvariable2, cvariable3); break;
			case OPER_GT: CompareValueGT(variable1, cvariable2, cvariable3); break;
			case OPER_LS: CompareValueLS(variable1, cvariable2, cvariable3); break;
			case OPER_GE: CompareValueGE(variable1, cvariable2, cvariable3); break;
			case OPER_LE: CompareValueLE(variable1, cvariable2, cvariable3); break;
			case OPER_IF:
				if (IsEmptyValue(cvariable1))
					lCodeLine = index2 - 1;
				break;
			case OPER_FOR:
				if (cvariable1.m_typeClass != ibValueTypes::TYPE_NUMBER)
					ibBackendCoreException::Error(_("Only variables with type can be used to organize the loop \"number\""));
				if (cvariable1.m_fData == cvariable2.m_fData)
					lCodeLine = index3 - 1;
				break;
			case OPER_FOREACH:
			{
				bool needsCreate = (g_valueIterator != variable3.GetClassType());
				if (!needsCreate) {
					// Slot already holds an iterator. If we did NOT arrive
					// via OPER_NEXT_ITER, an enclosing-loop iteration left
					// the cursor mid-walk (Break leak). Drop and rebuild
					// from variable2 — the OPER_LET above may have reassigned
					// the iterable, so keeping the old snapshot would walk
					// stale data.
					ibValueIterator* it = AsIterator(variable3);
					if (!it->ConsumeHot()) {
						variable3.Reset();
						needsCreate = true;
					}
				}
				if (needsCreate) {
					auto newIterator = variable2.CreateIterator();
					if (!newIterator)
						ibBackendCoreException::Error(_("Undefined value iterator"));
					CopyValue(variable3, ibValue(new ibValueIterator(std::move(newIterator))));
				}
				ibValueIterator* iterator = AsIterator(variable3);
				if (!iterator->MoveNext(variable1)) {
					variable3.Reset(); lCodeLine = index4 - 1;
				}
			} break;
			case OPER_NEXT:
			{
				if (variable1.m_typeClass == ibValueTypes::TYPE_NUMBER)
					variable1.m_fData++;
				lCodeLine = index2 - 1;
			} break;
			case OPER_NEXT_ITER:
			{
				// Sign the slot "hot" so the next OPER_FOREACH knows we
				// arrived through normal iteration (not through Break +
				// outer-loop re-entry). Advance happens inside
				// OPER_FOREACH's MoveNext.
				if (variable1.GetClassType() == g_valueIterator) {
					ibValueIterator* it = AsIterator(variable1);
					it->MarkHot();
				}
				lCodeLine = index2 - 1;
			} break;
			case OPER_ITER:
			{
				if (IsHasValue(cvariable2))
					CopyValue(variable1, cvariable3);
				else
					CopyValue(variable1, cvariable4);
			}  break;
			case OPER_NEW:
			{
				ibValue* pRetValue = &variable1;
				ibRunContextSmall cRunContext(array2);
				cRunContext.m_lParamCount = array2;
				const wxString className = m_pByteCode->m_listConst[index2].GetString();
				//load parameters
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (index1 >= 0) {
						// Read-resolve via cvariable1 first — read-only check
						// safely passes a DEF_VAR_CONST slot. variable1 (write-
						// resolve) would throw on const slots before reaching
						// the m_bReadOnly check, breaking const literal args
						// like `New Foo(3, 5)`. Mirrors OPER_CALL_METHOD's pattern.
						if (cvariable1.m_bReadOnly && !cvariable1.IsReference()) {
							CopyValue(cRunContext.m_pLocVars[i], cvariable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}
				CopyValue(*pRetValue, ibValue::CreateObject(className, cRunContext.m_lParamCount > 0 ? cRunContext.m_pRefLocVars : nullptr, cRunContext.m_lParamCount));
			} break;
			case OPER_SET_A:
			case OPER_SET_SCOPE://writable member of a scope binding — identical parent+prop write
			{//setting attribute
				const wxString& strPropName = m_pByteCode->m_listConst[index2].GetString();
				const long lPropNum = variable1.FindProp(strPropName);
				if (lPropNum < 0) CheckAndError(variable1, strPropName);
				if (!variable1.IsPropWritable(lPropNum)) Raise(ERROR_PROP_NOT_WRITABLE, strPropName);
				variable1.SetPropVal(lPropNum, GetValue(cvariable3));
			} break;
			case OPER_GET_A://get attribute
			case OPER_GET_SCOPE://bare member of a scope binding — identical parent+prop resolve
			{
				ibValue* pRetValue = &variable1;
				const wxString& strPropName = m_pByteCode->m_listConst[index3].GetString();
				const long lPropNum = variable2.FindProp(strPropName);
				if (lPropNum < 0) CheckAndError(variable2, strPropName);
				if (!variable2.IsPropReadable(lPropNum)) Raise(ERROR_PROP_NOT_READABLE, strPropName);
				// Scope-local props (ThisObject / ThisForm / RegisterRecords)
				// are bc-internal: host's own code reaches them through
				// its own m_listVar (compile-time), not through chain
				// access. In eval mode (`m_bExpressionOnly`) any chain
				// hop landing on such a prop must be denied — debugger
				// watch should not be able to walk into a foreign
				// object's "self" handle.
				if (m_pByteCode->m_bExpressionOnly && variable2.IsPropScoped(lPropNum))
					Raise(ERROR_PROP_SCOPE_LOCAL, strPropName);
				ibValue vRet; bool result = variable2.GetPropVal(lPropNum, vRet);
				if (result && vRet.IsReference())
					*pRetValue = vRet;
				else if (result)
					CopyValue(*pRetValue, vRet);
				break;
			}
			case OPER_GET_EXTERN://named binder handle (export) — value staged in the slot (m_param2)
			case OPER_GET_CONTEXT://named binder handle (context: ThisObject/ThisForm) — same
			{
				// Lazy: the relaxed pre-flight leaves an unbound slot Undefined;
				// copy the slot value out to the dest temp, preserving references
				// so member access (`.X`) walks the live handle.
				ibValue* pRetValue = &variable1;
				if (variable2.IsReference())
					*pRetValue = variable2;
				else
					CopyValue(*pRetValue, variable2);
				break;
			}
			case OPER_SET_EXTERN://assign back to a named handle's slot (rare — handles are read-only)
			case OPER_SET_CONTEXT:
			{
				CopyValue(variable1, GetValue(cvariable3));
				break;
			}
			case OPER_CALL_METHOD://method call
			{
				ibValue* pRetValue = &variable1;
				ibValue* pVariable2 = &variable2;

				const wxString& funcName = m_pByteCode->m_listConst[index3].GetString();
				// Resolve method number on every call. Bytecode is a const
				// template at runtime — no opcode-level cache patching.
				// (The previous "cache" path stored resolved method # /
				// object ref into m_param4 on first call; never actually
				// improved warm-path performance and complicated AOT.)
				// LINQ pipeline ops (Where / Select / ...) are NOT handled
				// here — compile-side emits OPER_CALL_LINQ for them.
				long lMethodNum = pVariable2->FindMethod(funcName);

				if (lMethodNum < 0)
					CheckAndError(variable2, funcName);

				ibRunContextSmall cRunContext(std::max(array3, MAX_STATIC_VAR));
				cRunContext.m_lParamCount = array3;

				// too many parameters — per-class methods have a meaningful
				// compile-time GetNParams.
				{
					const long paramCount = pVariable2->GetNParams(lMethodNum);
					if (paramCount < cRunContext.m_lParamCount)
						Raise(ERROR_MANY_PARAMS, funcName, funcName);
					else if (paramCount == wxNOT_FOUND && cRunContext.m_lParamCount == 0)
						Raise(ERROR_MANY_PARAMS, funcName, funcName);
				}

				//load parameters
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					// OPER_SETCONST passes a constant directly from the
					// const-pool. Without this branch the default OPER_SET
					// path resolves param1 as a frame slot (m_numArray=0,
					// m_numIndex=const-pool-idx) — fetches garbage, breaks
					// `obj.Method("name", value)` where the name string is
					// emitted as OPER_SETCONST. Mirrors the OPER_CALL
					// arg-load below; missing here until first script call
					// chain emitted via select-{ } anon Structure exposed it.
					if (curCode.m_numOper == OPER_SETCONST) {
						CopyValue(cRunContext.m_pLocVars[i], m_pByteCode->m_listConst[index1]);
					}
					else if (index1 >= 0 && !pVariable2->GetParamDefValue(lMethodNum, i, *cRunContext.m_pRefLocVars[i])) {
						if (cvariable1.m_bReadOnly && !cvariable1.IsReference()) {
							CopyValue(cRunContext.m_pLocVars[i], cvariable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}

				if (pVariable2->HasRetVal(lMethodNum)) {
					pVariable2->CallAsFunc(lMethodNum, *pRetValue, cRunContext.m_pRefLocVars, cRunContext.m_lParamCount);
				}
				else {
					// operator =
					if (m_pByteCode->m_listCode[lCodeLine + 1].m_numOper == OPER_LET) {
						lCodeLine++;
						ibValue* pNextVariable2 = &variable2;
						lCodeLine--;
						if (pRetValue == pNextVariable2)
							Raise(ERROR_USE_PROCEDURE_AS_FUNCTION, funcName, funcName);
					}
					else if (m_pByteCode->m_listCode[lCodeLine + 1].m_numOper == OPER_RET) {
						lCodeLine++;
						ibValue* pNextVariable1 = &variable1;
						lCodeLine--;
						if (pRetValue == pNextVariable1)
							Raise(ERROR_USE_PROCEDURE_AS_FUNCTION, funcName, funcName);
					}
					pVariable2->CallAsProc(lMethodNum, cRunContext.m_pRefLocVars, cRunContext.m_lParamCount);
				} break;
			}
			case OPER_CALL_LINQ:
			{ //universal pipeline method on an iterable receiver — Where /
				// Select / OrderBy / GroupBy / Join / Skip / Take / ... .
				// Compile-side (compileCode.cpp method-call emit) detects
				// LINQ method names via ibValue::FindLinqMethodByName and
				// chooses this opcode over OPER_CALL_METHOD. Operand layout
				// mirrors OPER_CALL_METHOD with one diff:
				//   m_param3.m_numIndex = ibLinqMethod enum value
				//                         (NOT a const-string index)
				//   m_param3.m_numArray = caller arg count
				// Runtime reads the enum id straight, no FindMethod string
				// walk, no const-pool lookup.
				ibValue* pRetValue = &variable1;
				ibValue* pVariable2 = &variable2;
				const long enumId   = index3;   // ibLinqMethod (as long)
				const long argCount = array3;

				ibRunContextSmall cRunContext(std::max(argCount, (long)MAX_STATIC_VAR));
				cRunContext.m_lParamCount = argCount;

				//load parameters — same SET/SETCONST tape as OPER_CALL_METHOD
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (curCode.m_numOper == OPER_SETCONST) {
						CopyValue(cRunContext.m_pLocVars[i], m_pByteCode->m_listConst[index1]);
					}
					else {
						if (cvariable1.m_bReadOnly && !cvariable1.IsReference()) {
							CopyValue(cRunContext.m_pLocVars[i], cvariable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}

				// Virtual dispatch — subclasses can specialise behavior
				// (e.g. ibValueArray short-circuiting Count / ElementAt /
				// Contains to direct vector access). Default base impl
				// walks TYPE_REFFER chain and delegates to the file-scope
				// ibValueLinqDispatchImpl state-class machinery.
				pVariable2->DispatchLinqMethod(
					static_cast<ibValue::ibLinqMethod>(enumId),
					*pRetValue,
					cRunContext.m_pRefLocVars,
					cRunContext.m_lParamCount);
				break;
			}
			case OPER_CALL:
			{ //call a regular function — stack-frame fast path (target
				// has no inner lambda, m_needsHeapFrame=false). Compile
				// emits OPER_CALL_CLOSURE instead when heap promotion needed.
				const long lModuleNumber = array2;
				ibRunContext cRunContext(index3);
				cRunContext.m_lStart = index2;
				cRunContext.m_lParamCount = array3;
				cRunContext.m_parentRunContext = pContext;
				// Function bodies exit via OPER_RET / OPER_ENDFUNC /
				// OPER_ENDLFUNC (all fall through to lCodeLine = lFinish;
				// break), so callers don't need to range-bound the
				// invocation — the terminator opcodes handle exit.
				const ibByteCode* pLocalByteCode = m_ppArrayCode[lModuleNumber]->m_pByteCode;
				// m_currentFunction is no longer set here — the OPER_FUNC
				// opcode at function entry sets it as part of tape
				// execution flow (opcode-driven runtime state).
				ibValue* pRetValue = &variable1;
				//load parameters
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (curCode.m_numOper == OPER_SETCONST) {
						CopyValue(cRunContext.m_pLocVars[i], pLocalByteCode->m_listConst[index1]);
					}
					else {
						// Read-resolve via cvariable1 — read-only check safely
						// passes a DEF_VAR_CONST slot. variable1 (write-resolve)
						// would throw on const slots before reaching the
						// m_bReadOnly check, breaking const literal args like
						// `someFunc(3, 5)`. Mirrors OPER_CALL_METHOD and OPER_CALL_LAMBDA.
						if (cvariable1.m_bReadOnly || index2 == 1) {//pass parameter by value
							CopyValue(cRunContext.m_pLocVars[i], cvariable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}
				m_ppArrayCode[lModuleNumber]->Execute(&cRunContext, pRetValue, false);
				break;
			}
			case OPER_CALL_CLOSURE:
			{ //function call — heap-frame variant. Target has
				// m_needsHeapFrame=true (some inner lambda captures
				// a local from it). Allocate frame via make_shared so
				// escaping lambdas can hold a shared_ptr at OPER_LFUNC
				// materialise time, keeping the frame alive past return.
				// Operand layout identical to OPER_CALL.
				const long lModuleNumber = array2;
				std::shared_ptr<ibRunContext> heapCtx = std::make_shared<ibRunContext>(index3);
				heapCtx->m_lStart = index2;
				heapCtx->m_lParamCount = array3;
				heapCtx->m_parentRunContext = pContext;
				const ibByteCode* pLocalByteCode = m_ppArrayCode[lModuleNumber]->m_pByteCode;
				ibValue* pRetValue = &variable1;
				//load parameters — same SET/SETCONST tape as OPER_CALL
				for (long i = 0; i < heapCtx->m_lParamCount; i++) {
					lCodeLine++;
					if (curCode.m_numOper == OPER_SETCONST) {
						CopyValue(heapCtx->m_pLocVars[i], pLocalByteCode->m_listConst[index1]);
					}
					else {
						if (cvariable1.m_bReadOnly || index2 == 1) {
							CopyValue(heapCtx->m_pLocVars[i], cvariable1);
						}
						else {
							heapCtx->m_pRefLocVars[i] = &variable1;
						}
					}
				}
				m_ppArrayCode[lModuleNumber]->Execute(heapCtx.get(), pRetValue, false);
				break;
			}
			case OPER_SET_ARRAY:
				if (!SetArrayValue(variable1, cvariable2, GetValue(cvariable3)))
					Raise(ERROR_ARRAY_SET, cvariable3);
				break; //setting the array value
			case OPER_GET_ARRAY:
				if (!GetArrayValue(variable1, variable2, cvariable3))
					Raise(ERROR_ARRAY_GET, cvariable3);
				break; //getting the array value
			case OPER_GOTO: case OPER_ENDTRY:
			{
				const long tryCodeLine = index1;
				const long trySize = tryList.size() - 1;
				if (trySize >= 0) {
					if (tryCodeLine >= tryList[trySize].m_lEndLine ||
						tryCodeLine <= tryList[trySize].m_lStartLine) {
						tryList.resize(trySize);//exit from try..catch scope
					}
				}
				lCodeLine = tryCodeLine - 1;//since we'll add 1 later
			} break;
			case OPER_TRY:
				tryList.emplace_back(lCodeLine, index1);
				break; //transition on error
			case OPER_RAISE: ibBackendCoreException::Error(ibBackendException::GetLastError()); break;
			case OPER_RAISE_T: ibBackendCoreException::Error(m_pByteCode->m_listConst[index1].GetString()); break;
			case OPER_RET:
				if (index1 != DEF_VAR_NORET) {
					if (pvarRetValue == nullptr)
						ibBackendCoreException::Error(_("Cannot set return value in procedure!"));
					CopyValue(*pvarRetValue, cvariable1);
				}
			case OPER_ENDFUNC:
			case OPER_ENDLFUNC:
			case OPER_END:
				lCodeLine = lFinish;
				break; //exit
			case OPER_FUNC: if (bDelta) {
				// Module-init walk skips through the named-function body
				// to its OPER_ENDFUNC. Anonymous lambdas use a distinct
				// opcode pair (OPER_LFUNC / OPER_ENDLFUNC) so an inline
				// lambda nested inside a named function body does NOT
				// terminate this skip prematurely — the matcher here
				// is opcode-specific.
				while (lCodeLine < lFinish) {
					if (curCode.m_numOper != OPER_ENDFUNC) {
						lCodeLine++;
					}
					else break;
				}
			}
			else {
				// Function entry — opcode-driven runtime state update.
				// Replaces the legacy FindFunctionByEntry call sites at
				// OPER_CALL / CallAsProc / CallAsFunc — m_currentFunction
				// is now established as the tape itself executes, which
				// is the natural "ibRunContext updated as commands run"
				// model that the AOT-friendly self-describing tape design
				// requires (see project_bytecode_tape_design memory).
				pContext->m_currentFunction = m_pByteCode->FindFunctionByEntry(lCodeLine);
			}
			break;
			case OPER_LFUNC: {
				// Operand layout (stamped at compile time):
				//   m_param1                = dest slot for the resulting
				//                             ibValueFunction value
				//   m_param2.m_numIndex     = end IP (matching OPER_ENDLFUNC)
				//   m_param3.m_numIndex     = varCount   (frame size)
				//   m_param3.m_numArray     = paramCount
				//   m_param4.m_numIndex     = bCodeRet (1 = function, 0 = procedure)
				// Start IP is the current opcode itself (lCodeLine).
				//
				// Materialisation runs in BOTH bDelta paths — unlike
				// OPER_FUNC (named function declaration; module-init
				// only registers, doesn't execute), OPER_LFUNC is a
				// value-producing expression. Module-level
				// `var f = Function() ... EndFunction` runs the LFUNC
				// at module-init (bDelta=true) so the dest slot
				// holds a real ibValueFunction by the time the
				// follow-on OPER_LET copies it into `f`.
				const long endIp     = (long)index2;
				const long funcIdx   = (long)index3;
				// Range sanity — guards against a corrupted OPER_LFUNC
				// (compile-time bug or AOT cache mismatch).
				if (m_pByteCode == nullptr ||
				    lCodeLine < 0 || endIp <= lCodeLine ||
				    endIp >= (long)m_pByteCode->m_listCode.size() ||
				    funcIdx < 0 || funcIdx >= (long)m_pByteCode->m_listFunc.size())
				{
					ibBackendCoreException::Error(_("Cannot create function value (invalid lambda operands)"));
				}
				ibValueFunction* newFn = new ibValueFunction(m_pByteCode, funcIdx);
				// Cache m_needsHeapFrame from the bytecode fn once at
				// materialise so OPER_CALL_LAMBDA doesn't have to go
				// through m_parentBc->m_listFunc[funcIdx] each invoke.
				{
					const auto* bfnLfunc = (funcIdx >= 0 && funcIdx < (long)m_pByteCode->m_listFunc.size())
						? &m_pByteCode->m_listFunc[funcIdx] : nullptr;
					if (bfnLfunc) newFn->m_needsHeapFrame = bfnLfunc->m_needsHeapFrame;
				}
				// Closure capture (Phase B) — walk the call-stack chain
				// via m_parentRunContext; each heap-promoted ancestor
				// (weak_from_this().lock() returns non-null) gets its
				// shared_ptr copied into the new lambda's
				// m_capturedFrames. Stack-allocated frames return an
				// expired weak_ptr and are skipped — they couldn't be
				// captured anyway (frame dies on return; no inner
				// lambda flagged the enclosing fn at compile time so
				// no heap promotion happened at OPER_CALL).
				//
				// Order: index 0 = direct enclosing frame (the
				// materialising lambda's caller), index 1 = next outer,
				// .... Matches the depth math from Phase A's GetVariable
				// (numParent - numContext counting): emit depth = 1
				// reads m_capturedFrames[0], depth = 2 reads [1], etc.
				for (ibRunContext* p = pContext; p != nullptr; p = p->m_parentRunContext) {
					std::shared_ptr<ibRunContext> sp = p->weak_from_this().lock();
					if (sp)
						newFn->m_capturedFrames.push_back(std::move(sp));
				}
				CopyValue(variable1, ibValue(newFn));
				// Skip past the body — body opcodes are inert at
				// module-init walk and reached only via OPER_CALL_LAMBDA
				// during normal execution.
				lCodeLine = endIp;
				break;
			}
			case OPER_CALL_LAMBDA: {
				
				// m_param4 = source ibValue holding the callable (must
				//            wrap an ibValueFunction via TYPE_REFFER + m_pRef)
				// m_param1 = return value dest
				// Frame size + paramCount + body range — on the captured
				// ibLambdaInfo. Lambda body lives inline in info->parentBc;
				// dispatch routes through the session's lambda runtime
				// (root mm's procUnit, parent chain anchors Catalogs /
				// Documents / Manager / system functions in its frame
				// array). The runtime's m_pByteCode is swapped to
				// info->parentBc for the call duration and restored
				// after — Execute snapshots it at entry, so re-entrant
				// lambda calls don't clobber an outer view.
				// Caller-supplied arg count — stamped at compile by EmitCallVal
				// in m_param2.m_numIndex (= listParam.size() at the call site).
				// Read before the param-bind loop shifts curCode off CALL_VAL.
				const long callerArgCount = (long)index2;
				ibValueFunction* fn = AsFunction(cvariable4);
				const ibByteCode::ibByteFunction* bfn = fn ? fn->GetFunction() : nullptr;
				if (bfn == nullptr)
					ibBackendCoreException::Error(_("Cannot call: value is not a callable function"));

				const ibByteCode* pLocalByteCode = fn->GetParentBc();
				const long lambdaParamCount = (long)bfn->m_listParam.size();
				const long lambdaVarCount   = bfn->m_lVarCount;
				const long lambdaEntryIp    = bfn->m_lCodeLine;

				// Arg-count validation — too many is a hard error;
				// too few is OK iff missing tail has defaults (checked
				// in phase 2 below).
				if (callerArgCount > lambdaParamCount) {
					ibBackendCoreException::Error(
						_("Too many arguments to function value: passed %ld, expected at most %ld"),
						callerArgCount, lambdaParamCount);
				}

				// Heap-promote the lambda's frame when its body captures
				// from yet-deeper enclosing fns. Read the flag directly
				// off the ibValueFunction value — cached at OPER_LFUNC
				// materialise; one indirection, no detour through
				// m_parentBc->m_listFunc[funcIdx].
				const bool useHeapFrame = fn->m_needsHeapFrame;
				std::shared_ptr<ibRunContext> heapCtx;
				ibRunContext  stackCtx;
				ibRunContext* pNewCtx = nullptr;
				if (useHeapFrame) {
					heapCtx = std::make_shared<ibRunContext>(lambdaVarCount);
					pNewCtx = heapCtx.get();
				} else {
					stackCtx.SetLocalCount(lambdaVarCount);
					pNewCtx = &stackCtx;
				}
				pNewCtx->m_lStart          = lambdaEntryIp + 1;  // first body opcode
				pNewCtx->m_lParamCount     = lambdaParamCount;
				// Stamp the lambda's ibByteFunction so debugger / call-stack
				// renders "<lambda@N>" with proper params + locals; eval
				// host detection uses bfn->m_listLocals to resolve names.
				pNewCtx->m_currentFunction = bfn;
				// For OPER_LFUNC chain-walks inside this lambda body to
				// reach the lambda's LEXICAL outer scope (not its dynamic
				// caller), wire m_parentRunContext to the first captured
				// frame. The captured chain (fn->m_capturedFrames) holds
				// shared_ptrs to the enclosing fn frames at materialise
				// time — that's the closure's defining scope. Empty chain
				// (top-level lambda with no captures) falls back to the
				// dynamic caller so debugger / stack-walk still sees
				// something sensible.
				pNewCtx->m_parentRunContext = !fn->m_capturedFrames.empty()
					? fn->m_capturedFrames[0].get()
					: pContext;
				ibValue* pRetValue = &variable1;

				// Phase 1 — consume caller-supplied OPER_SET / OPER_SETCONST.
				// Strict opcode validation: a non-SET opcode here means
				// the compiler stamped a wrong arg count on CALL_VAL,
				// or the bytecode tape was corrupted — both are invariant
				// violations, not user errors.
				for (long i = 0; i < callerArgCount; i++) {
					lCodeLine++;
					if (curCode.m_numOper == OPER_SETCONST) {
						CopyValue(pNewCtx->m_pLocVars[i], pLocalByteCode->m_listConst[index1]);
					}
					else if (curCode.m_numOper == OPER_SET) {
						if (cvariable1.m_bReadOnly || index2 == 1) {
							CopyValue(pNewCtx->m_pLocVars[i], cvariable1);
						}
						else {
							pNewCtx->m_pRefLocVars[i] = &variable1;
						}
					}
					else {
						ibBackendCoreException::Error(
							_("Lambda call: malformed argument tape (expected OPER_SET/SETCONST at param %ld)"),
							i);
					}
				}

				// Phase 2 — fill missing tail params from compile-time
				// defaults on the lambda's m_listParam (same structure
				// named-function calls read from at PushCallFunction).
				for (long i = callerArgCount; i < lambdaParamCount; i++) {
					if (i >= (long)bfn->m_listParam.size()) {
						ibBackendCoreException::Error(
							_("Lambda call: m_listParam shorter than paramCount at param %ld"), i);
					}
					const ibParamRunUnit& puDef = bfn->m_listParam[i].m_defaultValue;
					if (puDef.m_numArray == DEF_VAR_SKIP) {
						const wxString& nm = (i < (long)bfn->m_listParam.size())
							? bfn->m_listParam[i].m_strName
							: wxString::Format(wxT("p%ld"), i);
						ibBackendCoreException::Error(
							_("Missing required argument '%s' to function value"),
							nm);
					}
					CopyValue(pNewCtx->m_pLocVars[i], pLocalByteCode->m_listConst[puDef.m_numIndex]);
				}

				fn->Execute(pNewCtx, pRetValue, false);
				break;
			}
			// Tape declarators (self-describing bytecode, AOT-ready).
			// NOP at runtime — they exist to let an AOT loader / debugger
			// reconstruct ibByteFunction's m_listParam / m_listLocals and
			// block scopes by walking the tape, without parallel maps.
			// Compile-time still populates the parallel maps for the live
			// path; AOT phase makes them derived from these declarators.
			case OPER_FUNC_PARAM:
			case OPER_FUNC_LOCAL:
				break;
			case OPER_CTX_BEGIN:
				// Block enter — bump runtime scope depth. SendLocalVariables
				// filter (entry.m_scopeDepth <= m_currentScopeDepth) then
				// includes vars stamped at this depth or shallower.
				++pContext->m_currentScopeDepth;
				break;
			case OPER_CTX_END:
				// Block exit — drop depth. No slot Reset: block-locals
				// are stamped m_scopeDepth > new current, so they're
				// invisible to Locals via the filter; user code can't
				// reach them through compile-time scope rules either.
				// Loop re-entry's first OPER_LET overwrites the slot
				// anyway.
				if (pContext->m_currentScopeDepth > 0)
					--pContext->m_currentScopeDepth;
				break;
			case OPER_SET_TYPE:
			{
				// A DECLARED TYPE MEETING A VALUE — one question, two outcomes.
				//
				// The TYPE answers whether the value may pass (AllowValue on its
				// runtime factory). It may, and the value is left exactly as it is; it
				// may not, and that is a type mismatch — raised here rather than
				// converted around, because a declaration states what a value IS and
				// is not a request to change it.
				//
				// Nothing here knows which types say yes to what. A barrier admitting a
				// whole family (`AnyRef`, `CatalogRef`), a class admitting only itself,
				// a plugin's type admitting whatever it likes — all answer the same
				// question, so a family that grows later needs no edit in the
				// interpreter. This used to be a chain of special cases; it is one call.
				const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(array2);
				if (typeCtor == nullptr || !typeCtor->AllowValue(variable1.GetClassType())) {
					ibBackendCoreException::Error(
						_("Type mismatch: a value of type '%s' does not fit the declared type '%s'"),
						variable1.GetClassName(), ibValue::GetNameObjectFromID(array2));
				}
				break;
			}
				//Operators for working with typed data
				//NUMBER
			case OPER_ADD + TYPE_DELTA1: variable1.m_fData = cvariable2.m_fData + cvariable3.m_fData; break;
			case OPER_SUB + TYPE_DELTA1: variable1.m_fData = cvariable2.m_fData - cvariable3.m_fData; break;
			case OPER_DIV + TYPE_DELTA1: if (cvariable3.m_fData.IsZero()) { Raise(ERROR_DIVIDE_BY_ZERO); } variable1.m_fData = cvariable2.m_fData / cvariable3.m_fData; break;
			case OPER_MOD + TYPE_DELTA1: if (cvariable3.m_fData.IsZero()) { Raise(ERROR_DIVIDE_BY_ZERO); } variable1.m_fData = cvariable2.m_fData.Round() % cvariable3.m_fData.Round(); break;
			case OPER_MULT + TYPE_DELTA1: variable1.m_fData = cvariable2.m_fData * cvariable3.m_fData; break;
			case OPER_LET + TYPE_DELTA1: variable1.m_fData = cvariable2.m_fData; break;
			case OPER_NOT + TYPE_DELTA1: variable1.m_fData = cvariable2.m_fData.IsZero(); break;
			case OPER_INVERT + TYPE_DELTA1: variable1.m_fData = -cvariable2.m_fData; break;
			case OPER_EQ + TYPE_DELTA1: variable1.m_fData = (cvariable2.m_fData == cvariable3.m_fData); break;
			case OPER_NE + TYPE_DELTA1: variable1.m_fData = (cvariable2.m_fData != cvariable3.m_fData); break;
			case OPER_GT + TYPE_DELTA1: variable1.m_fData = (cvariable2.m_fData > cvariable3.m_fData); break;
			case OPER_LS + TYPE_DELTA1: variable1.m_fData = (cvariable2.m_fData < cvariable3.m_fData); break;
			case OPER_GE + TYPE_DELTA1: variable1.m_fData = (cvariable2.m_fData >= cvariable3.m_fData); break;
			case OPER_LE + TYPE_DELTA1: variable1.m_fData = (cvariable2.m_fData <= cvariable3.m_fData); break;
			case OPER_SET_ARRAY + TYPE_DELTA1:
				if (!SetArrayValue(variable1, cvariable2, GetValue(cvariable3)))
					Raise(ERROR_ARRAY_SET, cvariable3);
				break;//set array value
			case OPER_GET_ARRAY + TYPE_DELTA1:
				if (!GetArrayValue(variable1, variable2, cvariable3))
					Raise(ERROR_ARRAY_GET, cvariable3);
				break; //getting the array value
			case OPER_IF + TYPE_DELTA1: if (cvariable1.m_fData.IsZero()) lCodeLine = index2 - 1; break;
				//STRING
			case OPER_ADD + TYPE_DELTA2: variable1.SetString(cvariable2.GetString() + cvariable3.GetString()); break;
			case OPER_LET + TYPE_DELTA2: variable1.SetString(cvariable2.GetString()); break;
			case OPER_SET_ARRAY + TYPE_DELTA2:
				if (!SetArrayValue(variable1, cvariable2, GetValue(cvariable3)))
					Raise(ERROR_ARRAY_SET, cvariable3);
				break; //set array value
			case OPER_GET_ARRAY + TYPE_DELTA2:
				if (!GetArrayValue(variable1, variable2, cvariable3))
					Raise(ERROR_ARRAY_GET, cvariable3);
				break; //getting the array value
			case OPER_IF + TYPE_DELTA2: if (cvariable1.IsEmpty()) lCodeLine = index2 - 1; break;
				//DATE
			case OPER_ADD + TYPE_DELTA3: variable1.m_dData = cvariable2.m_dData + cvariable3.m_dData; break;
			case OPER_SUB + TYPE_DELTA3: variable1.m_dData = cvariable2.m_dData - cvariable3.m_dData; break;
			case OPER_DIV + TYPE_DELTA3: if (cvariable3.m_dData == 0) { Raise(ERROR_DIVIDE_BY_ZERO); } variable1.m_dData = cvariable2.m_dData / cvariable3.GetInteger(); break;
			case OPER_MOD + TYPE_DELTA3: if (cvariable3.m_dData == 0) { Raise(ERROR_DIVIDE_BY_ZERO); } variable1.m_dData = (int)cvariable2.m_dData % cvariable3.GetInteger(); break;
			case OPER_MULT + TYPE_DELTA3: variable1.m_dData = cvariable2.m_dData * cvariable3.m_dData; break;
			case OPER_LET + TYPE_DELTA3: variable1.m_dData = cvariable2.m_dData; break;
			case OPER_NOT + TYPE_DELTA3: variable1.m_dData = ~cvariable2.m_dData; break;
			case OPER_INVERT + TYPE_DELTA3: variable1.m_dData = -cvariable2.m_dData; break;
			case OPER_EQ + TYPE_DELTA3: variable1.m_dData = (cvariable2.m_dData == cvariable3.m_dData); break;
			case OPER_NE + TYPE_DELTA3: variable1.m_dData = (cvariable2.m_dData != cvariable3.m_dData); break;
			case OPER_GT + TYPE_DELTA3: variable1.m_dData = (cvariable2.m_dData > cvariable3.m_dData); break;
			case OPER_LS + TYPE_DELTA3: variable1.m_dData = (cvariable2.m_dData < cvariable3.m_dData); break;
			case OPER_GE + TYPE_DELTA3: variable1.m_dData = (cvariable2.m_dData >= cvariable3.m_dData); break;
			case OPER_LE + TYPE_DELTA3: variable1.m_dData = (cvariable2.m_dData <= cvariable3.m_dData); break;
			case OPER_SET_ARRAY + TYPE_DELTA3:
				if (!SetArrayValue(variable1, cvariable2, GetValue(cvariable3)))
					Raise(ERROR_ARRAY_SET, cvariable3);
				break; //setting the array value
			case OPER_GET_ARRAY + TYPE_DELTA3:
				if (!GetArrayValue(variable1, variable2, cvariable3))
					Raise(ERROR_ARRAY_GET, cvariable3);
				break; //getting the array value
			case OPER_IF + TYPE_DELTA3: if (!cvariable1.m_dData) lCodeLine = index2 - 1; break;
				//BOOLEAN
			case OPER_ADD + TYPE_DELTA4: variable1.m_bData = cvariable2.m_bData + cvariable3.m_bData; break;
			case OPER_LET + TYPE_DELTA4: variable1.m_bData = cvariable2.m_bData; break;
			case OPER_NOT + TYPE_DELTA4:
				// Boolean-tier NOT — the typed path a `Not (comparison)` lambda hits. Kleene
				// NOT(UNKNOWN)=UNKNOWN under the LINQ three-valued flag (the comparison left an
				// empty/UNKNOWN operand); else the usual two-valued boolean NOT.
				//
				// THE OPERATOR TYPES ITS OWN RESULT. Writing m_bData alone assumed the slot
				// had already been made BOOLEAN — which a declared type used to do as a side
				// effect of OPER_SET_TYPE. A declared type is a GATE (it permits a write, it
				// does not convert), so nothing types the slot beforehand any more: an
				// untyped slot took the value and stayed EMPTY, and the row read as no
				// result at all. Both branches here now say what they produced, exactly as
				// the untyped tier does through SetTypeBoolean.
				if (ts_threeValuedNullCompare && IsNullOperand(cvariable2)) variable1.m_typeClass = ibValueTypes::TYPE_NULL;   // UNKNOWN == SQL NULL (IsNullOperand keys on TYPE_NULL)
				else SetTypeBoolean(variable1, !cvariable2.m_bData);
				break;
			case OPER_INVERT + TYPE_DELTA4: variable1.m_bData = !cvariable2.m_bData; break;
			case OPER_EQ + TYPE_DELTA4: variable1.m_bData = (cvariable2.m_bData == cvariable3.m_bData); break;
			case OPER_NE + TYPE_DELTA4: variable1.m_bData = (cvariable2.m_bData != cvariable3.m_bData); break;
			case OPER_GT + TYPE_DELTA4: variable1.m_bData = (cvariable2.m_bData > cvariable3.m_bData); break;
			case OPER_LS + TYPE_DELTA4: variable1.m_bData = (cvariable2.m_bData < cvariable3.m_bData); break;
			case OPER_GE + TYPE_DELTA4: variable1.m_bData = (cvariable2.m_bData >= cvariable3.m_bData); break;
			case OPER_LE + TYPE_DELTA4: variable1.m_bData = (cvariable2.m_bData <= cvariable3.m_bData); break;
			case OPER_IF + TYPE_DELTA4: if (!cvariable1.m_bData) lCodeLine = index2 - 1; break;
			}
			lCodeLine++;
		}
	}
	catch (const ibBackendInterruptException& err) {

		ibValueSystemFunction::Message(err.GetErrorDescription(),
			ibStatusMessage::ibStatusMessage_Error);

		while (lCodeLine < lFinish) {
			if (curCode.m_numOper != OPER_GOTO
				&& curCode.m_numOper != OPER_NEXT
				&& curCode.m_numOper != OPER_NEXT_ITER) {
				lCodeLine++;
			}
			else {
				lCodeLine++;
				goto start_label;
				break;
			}
		}

		if (auto* puState = ibSession::GetPUState())
			puState->m_errorPlace.Reset(); //Error is handled in this module - erase the error location

	}
	catch (const ibBackendException& err) {

		const long trySize = tryList.size() - 1;
		if (trySize >= 0) {

			if (auto* puState = ibSession::GetPUState())
				puState->m_errorPlace.Reset(); //Error is handled in this module - erase the error location

			const long tryCodeLine = tryList[trySize].m_lEndLine;
			tryList.resize(trySize);
			lCodeLine = tryCodeLine;
			goto start_label;
		}

		//there is no handler in this module - save the error location for the following modules
		//But we don't throw an error right away, because we don't know if there are any handlers further
		if (auto* puState = ibSession::GetPUState()) {
			if (puState->m_errorPlace.m_byteCode == nullptr && m_pByteCode != puState->m_errorPlace.m_skipByteCode) { //the Error system function throws an exception only for child modules

				//previously saved the original error location (i.e. the error didn't occur in this module)
				puState->m_errorPlace.m_byteCode = m_pByteCode;
				puState->m_errorPlace.m_errorLine = lCodeLine;
			}
		}

		//show and throw error message (ProcessError rethrows via `throw;`)
		ibBackendException::ProcessError(err, m_pByteCode->m_listCode[lCodeLine]);
	}
}

//nRunModule parameters:
//false-do not run
//true-run
void ibProcUnit::Execute(const ibByteCode& cByteCode, ibByteBinder& br, ibValue* pvarRetValue)
{
	const std::vector<ibValue*>& bindings = br.GetSlots();
	// `bDelta` here is the unified flag — historically the API split
	// it into bRunModule (run-or-skip) + bDelta (skip-function-bodies-
	// during-initial-pass), but the two always moved together. The
	// variable() macro expands to use bDelta directly, so keep the name.
	const bool bDelta = br.IsDelta();

	// Build the frame only when it isn't already built-and-clean for THIS
	// bytecode. AttachRuntime runs each module twice — Run(false) builds the
	// structure (no body, leaves it CLEAN), Run(true) executes it. The frame
	// (locals + parent tables) depends only on the bytecode, so Run(true) REUSES
	// the frame prepared by Run(false) instead of Reset + re-allocating (which
	// would leak the first frame's heap locals). Rebuild when: bytecode changed,
	// no frame yet, or the frame is DIRTY — m_bExecuted means a previous body run
	// left live state in the locals, so a fresh run needs a clean frame. (Pre-
	// flight below still refreshes live bindings on every pass, reused or not.)
	const bool bNeedsBuild = (m_pByteCode != &cByteCode || m_ppArrayCode == nullptr || m_bExecuted);
	if (bNeedsBuild) {
	Reset();

	if (!cByteCode.m_bCompile)
		ibBackendCoreException::Error(_("Module: %s not compiled!"), cByteCode.m_strModuleName);

	auto* state = ibSession::GetPUState();
	wxASSERT(state != nullptr);
	if (state != nullptr) state->m_recCount = 0;

	// Clear any leftover cancel request from a previous task that may
	// have set the flag right after that task already exited the loop.
	// Each Execute starts with a clean slate; the flag is only checked
	// inside the dispatch loop below.
	if (auto* s = ibSession::Current())
		s->ClearCancel();

	m_pByteCode = &cByteCode;

	//check the conformity of modules (compiled and running)
	if (GetParent() && GetParent()->m_pByteCode != m_pByteCode->m_parent) {
		m_pByteCode = nullptr;
		ibBackendCoreException::Error(_("System error - compilation failed (#1)\nModule:%s\nParent1:%s\nParent2:%s"),
			cByteCode.m_strModuleName,
			cByteCode.m_parent->m_strModuleName,
			GetParent()->m_pByteCode->m_strModuleName
		);
	}
	else if (!GetParent() && m_pByteCode->m_parent) {
		m_pByteCode = nullptr;
		ibBackendCoreException::Error(_("System error - compilation failed (#2)\nModule1:%s\nParent1:%s"),
			cByteCode.m_strModuleName,
			cByteCode.m_parent->m_strModuleName
		);
	}

	m_cCurContext.SetLocalCount(cByteCode.m_lVarCount);
	m_cCurContext.m_lStart = cByteCode.m_lStartModule;

	unsigned int nParentCount = GetParentCount();

	m_ppArrayCode = new ibProcUnit * [nParentCount + 1];
	m_ppArrayCode[0] = this;

	m_pppArrayList = new ibValue * *[nParentCount + 2];
	m_pppArrayList[0] = m_cCurContext.m_pRefLocVars;
	m_pppArrayList[1] = m_cCurContext.m_pRefLocVars;//start with 1, because 0 means local context

	for (unsigned int i = 0; i < nParentCount; i++) {
		ibProcUnit* pCurUnit = GetParent(i);
		m_ppArrayCode[i + 1] = pCurUnit;
		m_pppArrayList[i + 2] = pCurUnit->m_cCurContext.m_pRefLocVars;
	}
	} // end frame allocation (bNeedsBuild) — structure reused on a repeat Execute

	// Pre-flight runs on EVERY Execute, including the reused-frame pass: it copies
	// the binder's LIVE values into the frame. So a Run(true) following a Run(false)
	// prepare pulls the CURRENT bindings (e.g. common-module values initialised in
	// between the two passes), not the stale ones captured on the first pass.
	// Pre-flight: every required binding (m_listVar entry with
	// kind ∈ {External, Context}) must be wired and its class type
	// must match. Catches missed SetVar() calls (slot still null) and
	// type mismatches early — before the dispatch loop hits an
	// OPER_GET / OPER_GET_A on a stale slot. Bytecode is a const
	// template; only the binder supplies live values. Slot index in
	// the binder's m_slots vector matches the entry's m_slotIndex
	// (= runtime frame slot), so we copy 1:1 into m_pRefLocVars.
	for (const auto& v : cByteCode.m_listVar) {
		const size_t slot = static_cast<size_t>(v.m_slotIndex);
		ibValue* val = (slot < bindings.size()) ? bindings[slot] : nullptr;
		if (!v.IsBindRequired()) {
			// Bound LOCAL (e.g. a constant's Value backed by &m_constValue): the
			// binder seeded its slot — fill it, no required/type pre-flight. An
			// ordinary unbound local leaves a null binding and keeps its frame
			// default; ContextProp/Export carry no binder slot at all.
			if (val != nullptr)
				m_cCurContext.m_pRefLocVars[slot] = val;
			continue;
		}
		if (val == nullptr) {
			// EXPORT: present-or-not is fine — the slot stays Undefined and the
			// OPER_GET_EXTERN handler copies it out lazily.
			if (v.IsExternal()) continue;
			// CONTEXT (ThisObject / ThisForm): a required, typed self-handle —
			// it must be wired before the module runs.
			ibBackendCoreException::Error(
				_("Required binding not provided: '%s' (slot %zu)"),
				v.m_strRealName, slot);
		}
		// Type check only for CONTEXT bindings (ThisObject / ThisForm) — a stable,
		// typed self-handle worth validating against the compile-time stamp (its
		// VALUE mutates, hence lazy access, but its TYPE is fixed). EXPORT bindings
		// (RegisterRecords / Filter / Controls / DataSource) are loose: present or
		// not, this type or that — no difference to the script, so the check is
		// pointless there.
		if (v.IsContext() && v.m_clsid != 0 && val->GetClassType() != v.m_clsid) {
			ibBackendCoreException::Error(
				_("Binding type mismatch for '%s': expected clsid %u, got %u"),
				v.m_strRealName,
				(unsigned)v.m_clsid,
				(unsigned)val->GetClassType());
		}
		m_cCurContext.m_pRefLocVars[slot] = val;
	}

	//Initial initialization of module variables
	unsigned int lFinish = m_pByteCode->m_listCode.size();
	ibValue** pRefLocVars = m_cCurContext.m_pRefLocVars;

	// Only MODULE-level OPER_SET_TYPE is initialised here. A lambda body is inlined into the
	// parent bc between OPER_LFUNC / OPER_ENDLFUNC and carries its OWN OPER_SET_TYPE whose slots
	// index the LAMBDA frame, not this module frame — skip those (the main interpreter skips the
	// whole fence on module-init too, see codeDef.h). A typed lambda temp (e.g. `Not (...)` emits
	// a BOOLEAN temp) otherwise resolved a lambda slot against the module frame -> out of range -> AV.
	// Resolve from THIS opcode (`byte`), not the stale main-loop `curCode` the variable1/array2 macros read.
	int lambdaDepth = 0;
	for (unsigned int lCodeLine = 0; lCodeLine < lFinish; lCodeLine++) {
		const ibByteUnit& byte = m_pByteCode->m_listCode[lCodeLine];
		if (byte.m_numOper == OPER_LFUNC) { ++lambdaDepth; continue; }
		if (byte.m_numOper == OPER_ENDLFUNC) { if (lambdaDepth > 0) --lambdaDepth; continue; }
		if (lambdaDepth == 0 && byte.m_numOper == OPER_SET_TYPE) {
			ResolveWrite(byte.m_param1.m_numArray, byte.m_param1.m_numIndex, pRefLocVars, m_pppArrayList, bDelta)
				.SetType(ibValue::GetVTByID(byte.m_param2.m_numArray));
		}
	}

	// Constants are marked read-only at compile finalize — bc is a
	// const template at runtime, no per-Execute mutation here.

	if (bDelta) {
		// Module-body entry — m_currentFunction stays null (frame is
		// not inside a function). Runtime carries no compile-context
		// at all — eval / debugger pull all metadata from bytecode.
		// Mark the frame DIRTY: once the body has run, its locals hold
		// live state, so the next Execute on the same bytecode must
		// rebuild (bNeedsBuild) rather than reuse. A Run(false) prepare
		// pass never reaches here, so it leaves the flag clean and the
		// following Run(true) reuses the prepared frame.
		m_bExecuted = true;
		m_cCurContext.m_currentFunction = nullptr;
		Execute(&m_cCurContext, pvarRetValue, bDelta);
	}
}

//Search for a function in a module by name
//bExportOnly=0-search for any functions in the current module + exported ones in parent modules
//bExportOnly=1-search for exported functions in the current and parent modules
//bExportOnly=2-search for exported functions in the current module only
long ibProcUnit::FindMethod(const wxString& strMethodName, bool bError, int bExportOnly) const
{
	if (m_pByteCode == nullptr ||
		!m_pByteCode->m_bCompile) {
		ibBackendCoreException::Error(_("Module not compiled!"));
	}

	long lCodeLine = bExportOnly ?
		m_pByteCode->FindExportMethod(strMethodName) :
		m_pByteCode->FindMethod(strMethodName);

	if (bError && lCodeLine < 0)
		ibBackendCoreException::Error(_("Procedure or function \"%s\" not found!"), strMethodName);

	if (lCodeLine >= 0) {
		return lCodeLine;
	}
	if (GetParent() &&
		bExportOnly <= 1) {
		unsigned int nCodeSize = m_pByteCode->m_listCode.size();
		lCodeLine = GetParent()->FindMethod(strMethodName, false, 1);
		if (lCodeLine >= 0) {
			return nCodeSize + lCodeLine;
		}
	}
	return wxNOT_FOUND;
}

long ibProcUnit::FindFunction(const wxString& strMethodName, bool bError, int bExportOnly) const
{
	if (m_pByteCode == nullptr ||
		!m_pByteCode->m_bCompile) {
		ibBackendCoreException::Error(_("Module not compiled!"));
	}

	long lCodeLine = bExportOnly ?
		m_pByteCode->FindExportFunction(strMethodName) :
		m_pByteCode->FindFunction(strMethodName);

	if (bError && lCodeLine < 0)
		ibBackendCoreException::Error(_("Function \"%s\" not found!"), strMethodName);

	if (lCodeLine >= 0)
		return lCodeLine;

	if (GetParent() &&
		bExportOnly <= 1) {
		unsigned int nCodeSize = m_pByteCode->m_listCode.size();
		lCodeLine = GetParent()->FindFunction(strMethodName, false, 1);
		if (lCodeLine >= 0) {
			return nCodeSize + lCodeLine;
		}
	}
	return wxNOT_FOUND;
}

long ibProcUnit::FindProcedure(const wxString& strMethodName, bool bError, int bExportOnly) const
{
	if (m_pByteCode == nullptr ||
		!m_pByteCode->m_bCompile) {
		ibBackendCoreException::Error(_("Module not compiled!"));
	}

	long lCodeLine = bExportOnly ?
		m_pByteCode->FindExportProcedure(strMethodName) :
		m_pByteCode->FindProcedure(strMethodName);

	if (bError && lCodeLine < 0)
		ibBackendCoreException::Error(_("Procedure \"%s\" not found!"), strMethodName);

	if (lCodeLine >= 0) {
		return lCodeLine;
	}
	if (GetParent() &&
		bExportOnly <= 1) {
		unsigned int nCodeSize = m_pByteCode->m_listCode.size();
		lCodeLine = GetParent()->FindProcedure(strMethodName, false, 1);
		if (lCodeLine >= 0) {
			return nCodeSize + lCodeLine;
		}
	}
	return wxNOT_FOUND;
}

//Calling a procedure by name
//The call is made only in the current module
bool ibProcUnit::CallAsProc(const wxString& funcName, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode != nullptr) {
		const long lCodeLine = m_pByteCode->FindMethod(funcName);
		if (lCodeLine != wxNOT_FOUND) {
			CallAsProc(lCodeLine, ppParams, lSizeArray);
			return true;
		}
	}
	return false;
}

//Calling a function by name
//The call is made only in the current module
bool ibProcUnit::CallAsFunc(const wxString& funcName, ibValue& pvarRetValue, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode != nullptr) {
		const long lCodeLine = m_pByteCode->FindMethod(funcName);
		if (lCodeLine != wxNOT_FOUND) {
			CallAsFunc(lCodeLine, pvarRetValue, ppParams, lSizeArray);
			return true;
		}
	}
	return false;
}

//Calling a procedure by its address in the byte code array
//The call is made incl. and in the parent module
void ibProcUnit::CallAsProc(const long lCodeLine, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode == nullptr || !m_pByteCode->m_bCompile)
		ibBackendCoreException::Error(_("Module not compiled!"));

	const long lCodeSize = m_pByteCode->m_listCode.size();
	if (lCodeLine >= lCodeSize) {
		if (!GetParent())
			ibBackendCoreException::Error(_("Error calling module procedure!"));
		// Delegate to the parent module's PU and RETURN — without the
		// early return we fall through to `m_listCode[lCodeLine]` below
		// with an out-of-range index and crash on debug-bounds-check.
		GetParent()->CallAsProc(lCodeLine - lCodeSize, ppParams, lSizeArray);
		return;
	}

	ibRunContext cRunContext(index3);// number of local variables

	cRunContext.m_lParamCount = array3;//number of formal parameters
	cRunContext.m_lStart = lCodeLine;
	// m_currentFunction set by OPER_FUNC handler in Execute (tape-driven).

	//load parameters
	memcpy(&cRunContext.m_pRefLocVars[0], &ppParams[0], std::min(lSizeArray, cRunContext.m_lParamCount) * sizeof(ibValue*));

	//execute arbitrary code
	Execute(&cRunContext, nullptr, false);
}

//Calling a function by its address in the byte code array
//The call is made incl. and in the parent module
void ibProcUnit::CallAsFunc(const long lCodeLine, ibValue& pvarRetValue, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode == nullptr || !m_pByteCode->m_bCompile)
		ibBackendCoreException::Error(_("Module not compiled!"));

	const long lCodeSize = m_pByteCode->m_listCode.size();
	if (lCodeLine >= lCodeSize) {
		if (!GetParent())
			ibBackendCoreException::Error(_("Error calling module function!"));
		// Same missing-return bug as CallAsProc — delegate to parent
		// and bail out before indexing m_listCode with an out-of-range
		// lCodeLine.
		GetParent()->CallAsFunc(lCodeLine - lCodeSize, pvarRetValue, ppParams, lSizeArray);
		return;
	}

	ibRunContext cRunContext(index3);// number of local variables

	cRunContext.m_lParamCount = array3;//number of formal parameters
	cRunContext.m_lStart = lCodeLine;
	// m_currentFunction set by OPER_FUNC handler in Execute (tape-driven).

	//load parameters
	memcpy(&cRunContext.m_pRefLocVars[0], &ppParams[0], std::min(lSizeArray, cRunContext.m_lParamCount) * sizeof(ibValue*));

	//execute arbitrary code
	Execute(&cRunContext, &pvarRetValue, false);
}

long ibProcUnit::FindProp(const wxString& strPropName) const
{
	// Module-level exports are user-declared frame vars with kind=Export.
	// Skip Local (private), External / Context (ambient bindings — not
	// props of the owning module's value) and ContextProp (their
	// m_slotIndex is a prop-idx, not a frame slot).
	auto iterator = std::find_if(m_pByteCode->m_listVar.begin(), m_pByteCode->m_listVar.end(),
		[&strPropName](const auto& v) {
			if (!v.IsExport()) return false;
			return stringUtils::CompareString(strPropName, v.m_strRealName);
		});
	if (iterator != m_pByteCode->m_listVar.end())
		return (long)*iterator;
	return wxNOT_FOUND;
}

bool ibProcUnit::SetPropVal(const long lPropNum, const ibValue& varPropVal)//setting attribute
{
	*m_cCurContext.m_pRefLocVars[lPropNum] = varPropVal;
	return true;
}

bool ibProcUnit::SetPropVal(const wxString& strPropName, const ibValue& varPropVal)//setting attribute
{
	long lPropNum = FindProp(strPropName);
	if (lPropNum != wxNOT_FOUND) {
		*m_cCurContext.m_pRefLocVars[lPropNum] = varPropVal;
	}
	else {
		const long lPropPos = m_cCurContext.GetLocalCount();
		m_cCurContext.SetLocalCount(lPropPos + 1);
		m_cCurContext.m_cLocVars[lPropPos] = ibValue(strPropName);
		*m_cCurContext.m_pRefLocVars[lPropPos] = varPropVal;
	}
	return true;
}

bool ibProcUnit::GetPropVal(const long lPropNum, ibValue& pvarPropVal) //attribute value
{
	// Dereference: copy the VALUE of the local-var slot. Assigning the bare
	// ibValue* (operator=(ibValue*)) would make pvarPropVal a TYPE_REFFER to
	// &m_pLocVars[lPropNum] — an element of the by-value local array, refcount
	// 0. The caller's REFFER then DecrRefs it to 0 on destruction and runs
	// `delete this` on a non-heap array element → heap corruption. All runtime
	// access dereferences (locVariable macros); this must too.
	pvarPropVal = *m_cCurContext.m_pRefLocVars[lPropNum];
	return true;
}

bool ibProcUnit::GetPropVal(const wxString& strPropName, ibValue& pvarPropVal) //setting attribute
{
	const long lPropNum = FindProp(strPropName);
	if (lPropNum != wxNOT_FOUND) {
		pvarPropVal = *m_cCurContext.m_pRefLocVars[lPropNum]; // value copy, not a REFFER to the array slot (see lPropNum overload)
		return true;
	}
	return false;
}

// Specialized compile module for eval / watch expressions. Captures
// the host bc + the host function frame at construction so resolve-
// side code (compileContext.cpp's bc walk) can expose host's locals
// to the expression. Mirrors expression-only mode onto bc-side
// (m_cByteCode.m_bExpressionOnly) so runtime's OPER_GET_A scoped-prop
// check fires for chain access through eval
// (`Catalogs.X.CreateElement().ThisObject`).
//
// Lives in procUnit.cpp (the runtime module) because it's the only
// place that constructs eval modules. Base ibCompileCode exposes
// virtuals (GetEvalHostFunction / IsExpressionOnly) that return
// false-by-default; only ibCompileEval overrides — keeps the header
// clean.
class ibCompileEval : public ibCompileCode {
public:
	// Construct from a runtime context — pulls host bc + host fn out
	// of pRunContext, then delegates to the (host bc, host fn) ctor
	// below (the single setup site).
	explicit ibCompileEval(ibRunContext* pRunContext)
		: ibCompileEval(pRunContext ? pRunContext->GetByteCode() : nullptr,
		                pRunContext ? pRunContext->m_currentFunction : nullptr)
	{
	}

	// Construct from a (host bc, host fn) pair — for paths where
	// ibRunContext isn't directly available (AOT / standalone eval).
	ibCompileEval(const ibByteCode* hostBc, const ibByteCode::ibByteFunction* hostFn)
		: ibCompileCode(),
		  m_evalHostFunction(hostFn)
	{
		m_cByteCode.m_bExpressionOnly = true;
		m_cByteCode.m_parent = hostBc;
		m_rootContext->m_numFindLocalInParent = 2;

	}

	bool IsExpressionOnly() const override { return true; }
	const ibByteCode::ibByteFunction* GetEvalHostFunction() const override {
		return m_evalHostFunction;
	}

private:
	const ibByteCode::ibByteFunction* m_evalHostFunction;
};

bool ibProcUnit::Evaluate(const wxString& strExpression, ibRunContext* pRunContext, ibValue& pvarRetValue, bool compileBlock)
{
	if (pRunContext == nullptr) {
		if (auto* st = ibSession::GetPUState())
			pRunContext = st->GetCurrentRunContext();
	}

	if (strExpression.IsEmpty() || pRunContext == nullptr)
		return false;


	ibBackendException::ibEvalModeScope evalScope;

	// Helper — render an exception into the watch result slot so the
	// debugger panel can show *what* went wrong instead of an empty
	// row + silent `false` return. Watch handler in the designer prints
	// the result via ibValue::ToString; a string-typed ibValue with
	// "<error: msg>" reads naturally.
	auto reportFailure = [&pvarRetValue](const wxString& msg) {
		pvarRetValue = ibValue(wxString(wxT("<error: ")) + msg + wxT(">"));
	};

	auto iterator = std::find_if(pRunContext->m_listEval.begin(), pRunContext->m_listEval.end(),
		[strExpression](const auto pair) {return stringUtils::CompareString(strExpression, pair.first); });

	std::shared_ptr<ibProcUnitEvaluate> runEvaluate = nullptr;
	if (iterator == pRunContext->m_listEval.end()) { //this text has not yet been compiled
		try {
			auto compileExpression = std::make_unique<ibCompileEval>(pRunContext);
			compileExpression->Load(strExpression);

			auto evalUnit = std::make_shared<ibProcUnitEvaluate>();
			// Transfer ownership BEFORE CompileExpression so the unique_ptr
			// cleans up on any throw / failure path. No back-pointer wiring
			// on bytecode side; eval finds its compileCode via GetCompileCode().
			ibCompileCode& cModuleRef = *compileExpression;
			evalUnit->TakeCompileCode(std::move(compileExpression));
			if (!evalUnit->CompileExpression(pRunContext, pvarRetValue, cModuleRef, compileBlock)) {
				// CompileExpression's own diagnostics path already wrote the
				// reason into pvarRetValue when it can; if it's still empty
				// (compile aborted without producing a value), fill in a
				// generic note so the watch row isn't blank.
				if (pvarRetValue.GetType() == ibValueTypes::TYPE_EMPTY)
					reportFailure(_("compile failed"));
				return false;
			}

			runEvaluate = evalUnit;

			//everything is OK
			pRunContext->m_listEval.insert_or_assign(stringUtils::MakeUpper(strExpression), runEvaluate);
		}
		catch (const ibBackendException& e) {
			reportFailure(e.GetErrorDescription());
			return false;
		}
		catch (const std::exception& e) {
			reportFailure(wxString::FromUTF8(e.what()));
			return false;
		}
	}
	else {
		runEvaluate = iterator->second;
	}

	// Eval bytecode is a single expression — never has OPER_FUNC, so the
	// "skip module body" semantics of bDelta don't apply. Frame offset
	// is fixed by layout: own = pppArrayList[0]; host = pppArrayList[1]
	// (set by CompileExpression override to pRunContext->m_pRefLocVars);
	// host's parent module = pppArrayList[2] (set by outer Execute via
	// SetParent → GetParent loop). bDelta=false aligns the emitted
	// depth chain (own=0, parent=1, parent²=2, …) with this layout
	// directly — no +1 shift in pppArrayList[depth + (bDelta?1:0)].
	try {
		runEvaluate->Execute(&runEvaluate->m_cCurContext, &pvarRetValue, /*bDelta=*/false);
	}
	catch (const ibBackendException& e) {
		// Carry the message into the watch result. Previous behaviour
		// (bare `return false`) made the panel show a blank row for
		// any runtime failure — user couldn't tell apart a deliberately-
		// undefined identifier from a deadlock during the eval.
		reportFailure(e.GetErrorDescription());
		return false;
	}
	catch (const std::exception& e) {
		reportFailure(wxString::FromUTF8(e.what()));
		return false;
	}

	return true;
}

bool ibProcUnit::CompileExpression(ibRunContext* pRunContext, ibValue& pvarRetValue, ibCompileCode& cModule, bool bCompileBlock)
{
	const ibByteCode* const byteCode = pRunContext->GetByteCode();

	// Eval state (m_bExpressionOnly, m_cByteCode.m_parent, host fn,
	// m_numFindLocalInParent) is pre-set by ibCompileEval's ctor —
	// CompileExpression's caller passes a fully-prepared eval module.
	cModule.m_numCurrentCompile = wxNOT_FOUND;

	try {
		if (!cModule.PrepareLexem()) {
			return false;
		}
	}
	catch (...) {
		return false;
	}

	//process the bytecode array to return the expression result
	ibByteUnit code;
	code.m_numOper = OPER_RET;

	try {
		if (bCompileBlock)
			cModule.CompileBlock(cModule.GetContext());
		else
			code.m_param1 = cModule.GetExpression(cModule.GetContext());
	}
	catch (...)
	{
		return false;
	}

	if (!bCompileBlock) {
		cModule.m_cByteCode.m_listCode.push_back(code);
	}

	ibByteUnit code2;
	code2.m_numOper = OPER_END;

	cModule.m_cByteCode.m_listCode.push_back(code2);
	cModule.m_cByteCode.m_lVarCount = cModule.m_rootContext->m_listVariable.size();

	// Constants read-only — finalize sweep, mirrors ibCompileCode's normal
	// finalize (compileCode.cpp). Must run here (after the eval pool is fully
	// built), NOT at insertion: ibValue's move ctor drops m_bReadOnly
	// (value.cpp:73), so a vector realloc would wipe an insert-time flag.
	// Eval used to skip this entirely → a const arg read as writable and
	// ResolveWrite hit DEF_VAR_CONST ("Attempt to write to a constant value").
	for (auto& c : cModule.m_cByteCode.m_listConst)
		c.m_bReadOnly = true;

	//flag of compilation completion
	cModule.m_cByteCode.m_bCompile = true;

	//Project in memory
	SetParent(pRunContext->m_procUnit);

	try {
		Execute(cModule.m_cByteCode, pvarRetValue, false);
		// Frame-array setup — bytecode-driven walk. If host frame is
		// already expression-only (eval-inside-eval), count nested
		// expression-only ancestors; pick m_pppArrayList from N levels
		// up the host's array. Else fall back to host's local frame.
		if (pRunContext->IsExpressionOnly()) {
			int nParentNumber = 1;
			for (const ibByteCode* bc = pRunContext->GetByteCode();
				 bc != nullptr && bc->m_bExpressionOnly;
				 bc = bc->m_parent) {
				nParentNumber++;
			}
			m_pppArrayList[nParentNumber] = pRunContext->m_procUnit->m_pppArrayList[nParentNumber - 1];
		}
		else {
			// Layout (eval bDelta=false):
			//   [0] = eval own
			//   [1] = host's current frame (function-frame if inside
			//         a function — pRunContext->m_pRefLocVars; module-
			//         body frame if not; same memory either way)
			//   [2] = host's module-body frame (GetParent(0); == [1]
			//         iff host is not inside a function — that's a
			//         semantic coincidence, not a duplicate slot)
			//   [3+] = host's parent modules (GetParent(1+))
			m_pppArrayList[1] = pRunContext->m_pRefLocVars;

			// Eval inside a lambda body: pRunContext->m_procUnit is the
			// session's lambda shim, whose m_cCurContext is empty (the
			// shim never runs its own module body — frames live directly
			// in m_pppArrayList, wired by ibSession::CompileRoot). The
			// outer Execute populated eval[2..] from each parent's
			// m_cCurContext, so eval[2] landed on the shim's empty frame.
			// Splice the shim's actual chain in at offset +1 (eval has one
			// more own-frame layer above the shim) so depth ≥ 2 in the
			// eval expression resolves into root mm's bound slots —
			// Catalogs / Documents / Manager / common-module exports.
			if (pRunContext->m_currentFunction != nullptr
				&& pRunContext->m_currentFunction->IsLambda())
			{
				ibProcUnit* const shim = pRunContext->m_procUnit;
				if (shim != nullptr && shim->m_pppArrayList != nullptr) {
					const unsigned int shimSize = shim->GetParentCount() + 2;
					const unsigned int evalSize = GetParentCount() + 2;
					for (unsigned int k = 2; k < evalSize; ++k) {
						const unsigned int srcIdx = k - 1;
						if (srcIdx < shimSize)
							m_pppArrayList[k] = shim->m_pppArrayList[srcIdx];
					}
				}
			}
		}
	}
	catch (...) {
		return false;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////
//						Construction/Destruction                    //
//////////////////////////////////////////////////////////////////////

// ~ibProcUnitEvaluate is now `= default` in the header — m_compileCode
// (unique_ptr) handles cleanup automatically. The old manual delete
// through m_pByteCode->m_compileModule back-pointer is gone, and so is
// the back-pointer field on ibByteCode itself.

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueIterator, "Iterator", g_valueIterator);
SYSTEM_TYPE_REGISTER(ibValueFunction, "Function", g_valueFunction);
// ibValueQuery + g_valueQuery moved to procUnitLinq.cpp along with the
// rest of the LINQ runtime; SYSTEM_TYPE_REGISTER for it lives there.
