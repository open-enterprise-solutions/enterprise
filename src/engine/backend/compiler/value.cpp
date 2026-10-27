////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : base value  
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "backend/backend_exception.h"


//**********************************************************************
//*                       Value implementation                         *
//**********************************************************************

#if defined(DEBUG)
#define DEBUG_VALUE
#endif

#ifdef DEBUG_VALUE
#include <atomic>
#include <iostream>
#include <sstream>
#include <thread>
#include <wx/log.h>
#include "core/diagnostics/debugTrace.h"

// Atomic counter — Create/Delete can race across the HTTP and worker
// threads on the web build, and even on desktop if the designer's debug
// thread manipulates values. A plain unsigned int would UB.
static std::atomic<unsigned int> s_nCreateCount{0};

// Cross-platform debugger sink. wxLogDebug already does the right
// thing per OS:
//   MSW  — OutputDebugString (visible in VS Output pane) + default
//          wxLogStderr sink when no wxApp (we have wxInitializer only).
//   GTK  — stderr.
//   OSX  — NSLog (visible in Xcode Console).
// Writing std::cerr on top of that duplicates every line on MSW.
static inline void DebugValueEmit(const char* tag, unsigned int count) {
	std::ostringstream os;
	os << tag << ' ' << count
	   << " tid=" << std::this_thread::get_id();
	ibJournalInfo(ibT("value"),ibT("%s"), ibString::FromUTF8(os.str().c_str()));
}

// OFF unless OES_TRACE_VALUES says otherwise — see utils/debugTrace.h. The counter itself keeps
// running either way: it costs one atomic, and it is what makes a later "how many are alive?"
// answerable without a rebuild. Only the ~18000 lines per run are conditional.
//
// ⚠ HELD BY A FUNCTION, NOT AT FILE SCOPE, the way that header asks and the way valueFactory.cpp
// now does: a value is created and destroyed during the STATIC INITIALISATION of other translation
// units, so a file-scope flag here would be read before its own initialiser had run. Its twin in
// valueFactory.cpp was doing exactly that, and ASan named it on 2026-09-22.
static bool TraceValues()
{
	static const bool s_traceValues = ibDebugTraceEnabled("OES_TRACE_VALUES");
	return s_traceValues;
}

#define DEBUG_VALUE_CREATE() \
	{ const unsigned int alive = s_nCreateCount.fetch_add(1) + 1; \
	  if (TraceValues()) DebugValueEmit("Create", alive); }
#define DEBUG_VALUE_DELETE() \
	{ const unsigned int alive = s_nCreateCount.fetch_sub(1) - 1; \
	  if (TraceValues()) DebugValueEmit("Delete", alive); }
#else
#define DEBUG_VALUE_CREATE()
#define DEBUG_VALUE_DELETE()
#endif

//**********************************************************************
BACKEND_API const ibValue wxEmptyValue;
//**********************************************************************

// THE LIFECYCLE IS THE VARIANT'S (core/variant.cpp): the word, the count, the copy that makes a reference, the
// write-denied refusal. A runtime value adds its count of the living (DEBUG_VALUE) and nothing else here.
ibValue::ibValue()
	: ibVariant()
{
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(const ibValue& varValue)
	: ibVariant(varValue)
{
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValue&& varValue)
	: ibVariant(std::move(varValue))
{
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValue* pValue)
	: ibVariant(static_cast<ibVariant*>(pValue))
{
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibBackendValue* pParam)
	: ibVariant(static_cast<ibVariant*>(pParam ? pParam->GetImplValueRef() : nullptr))
{
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(int nYear, int nMonth, int nDay, unsigned short nHour, unsigned short nMinute, unsigned short nSecond)
	: ibVariant(nYear, nMonth, nDay, nHour, nMinute, nSecond)
{
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValueTypes type, bool readOnly)
	: ibVariant(type, readOnly)
{
	DEBUG_VALUE_CREATE();
}

//Constructors by types:
#define CVALUE_BYTYPE(v_parclass) \
ibValue::ibValue (v_parclass cParam) \
    : ibVariant(cParam) \
{\
	DEBUG_VALUE_CREATE();\
}

CVALUE_BYTYPE(bool);

CVALUE_BYTYPE(signed int);
CVALUE_BYTYPE(unsigned int);
CVALUE_BYTYPE(double);
CVALUE_BYTYPE(const ibNumber&);

CVALUE_BYTYPE(const ibDateTime&);

CVALUE_BYTYPE(const char*);
CVALUE_BYTYPE(const ibChar*);
CVALUE_BYTYPE(const std::wstring&);
CVALUE_BYTYPE(const ibString&);

#undef CVALUE_BYTYPE

ibValue::ibValue(ibString&& cParam)   // native — takes the text over (runtime string functions)
	: ibVariant(std::move(cParam))
{
	DEBUG_VALUE_CREATE();
}

// A VARIANT INTO THE RUNTIME — its word as it is. An object comes along only when it is one of the runtime's own:
// GetRef here answers with an ibValue, and an object made elsewhere (a property's live one) has no members to call.
ibValue::ibValue(const ibVariant& variant)
	: ibVariant()
{
	// Refused aloud, not left empty: a property's variant that reached the runtime is a mistake on the road that
	// brought it, and an empty value would carry that mistake on unseen. GetRef() is a static_cast on this.
	const ibVariant* held = variant.GetRef();
	if (held->m_typeClass >= ibValueTypes::TYPE_REFFER && dynamic_cast<const ibValue*>(held) == nullptr)
		ibBackendCoreException::Error(_("The value is not a value of the runtime"));
	Copy(variant);
	DEBUG_VALUE_CREATE();
}

ibValue::~ibValue()
{
	DEBUG_VALUE_DELETE();
}

// THE ASSIGNMENTS ARE THE VARIANT'S. Declared here because the runtime's own (`ibValue*`, `ibBackendValue*`) would
// hide them otherwise; each goes straight through.
void ibValue::operator = (bool cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (short cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (unsigned short cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (int cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (unsigned int cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (float cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (double cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (const ibNumber& cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (const ibDateTime& cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (const ibString& cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (const char* cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (const ibChar* cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (ibString&& cParam) { ibVariant::operator=(std::move(cParam)); }
void ibValue::operator = (const ibValue& cParam) { ibVariant::operator=(cParam); }
void ibValue::operator = (ibValue&& cParam) { ibVariant::operator=(std::move(cParam)); }
void ibValue::operator = (ibValueTypes type) { ibVariant::operator=(type); }

void ibValue::operator = (ibBackendValue* pValue)
{
	ibVariant::operator=(static_cast<ibVariant*>(pValue ? pValue->GetImplValueRef() : nullptr));
}

void ibValue::operator = (ibValue* pValue) { ibVariant::operator=(static_cast<ibVariant*>(pValue)); }
void ibValue::operator = (const ibValue* pValue) { ibVariant::operator=(static_cast<const ibVariant*>(pValue)); }

bool ibValue::FindValue(const ibString& findData, std::vector<ibValue>& listValue) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->FindValue(findData, listValue);

	try {
		if (m_typeClass == ibValueTypes::TYPE_BOOLEAN) {
			ibValue cFounded;
			cFounded.SetBoolean(findData);
			listValue.emplace_back(cFounded);
			if (cFounded.GetBoolean()) listValue.emplace_back(false);
			else listValue.emplace_back(true);
			return true;
		}
		else if (m_typeClass == ibValueTypes::TYPE_NUMBER) {
			listValue.emplace_back().SetNumber(findData);
			return true;
		}
		else if (m_typeClass == ibValueTypes::TYPE_DATE) {
			listValue.emplace_back().SetDate(findData);
			return true;
		}
		else if (m_typeClass == ibValueTypes::TYPE_STRING) {
			listValue.emplace_back().SetString(findData);
			return true;
		}
	}
	catch (...) {
		return false;
	}

	return false;
}

// An object of the runtime reads as its class's name — the factory's; everything else as the variant reads it.
ibString ibValue::GetString() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		return GetClassName();
	default:
		return ibVariant::GetString();
	}
}

// Everything a runtime value holds is a runtime value (ibValue(const ibVariant&) lets in no other object).
ibValue* ibValue::GetRef() const
{
	return static_cast<ibValue*>(ibVariant::GetRef());
}

void ibValue::ShowValue()
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->ShowValue();
}

//*************************************************************

ibString ibValue::GetClassName() const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetClassName();
	const ibClassID& clsid = GetClassType();
	if (clsid == 0) ibBackendCoreException::Error(_("Class not registered"));
	return ibValue::GetNameObjectFromID(clsid);
}

ibClassID ibValue::GetClassType() const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetClassType();
	if (m_typeClass < ibValueTypes::TYPE_REFFER)
		return ibValue::GetIDByVT(m_typeClass);
	return ibValue::GetTypeIDByRef(this);
}

//*************************************************************
//*                        array support                      *
//*************************************************************

bool ibValue::SetAt(const ibValue& varKeyValue, const ibValue& varValue)
{
	if (m_pRef && m_typeClass == ibValueTypes::TYPE_REFFER)
		return GetRef()->SetAt(varKeyValue, varValue);
	const long lPropNum = FindProp(varKeyValue.GetString());
	if (lPropNum != wxNOT_FOUND)
		return SetPropVal(lPropNum, varValue);
	return false;
}

bool ibValue::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	if (m_pRef && IsReference())
		return GetRef()->GetAt(varKeyValue, pvarValue);
	const long lPropNum = FindProp(varKeyValue.GetString());
	if (lPropNum != wxNOT_FOUND)
		return GetPropVal(lPropNum, pvarValue);
	return false;
}

//*************************************************************
//*                    iterator support                       *
//*************************************************************

std::shared_ptr<ibValueIteratorState> ibValue::CreateIterator()
{
	if (m_pRef && IsReference())
		return GetRef()->CreateIterator();
	return nullptr;
}

//*************************************************************
//*                    compare support                        *
//*************************************************************

// THE ORDER IS THE VARIANT'S (core/variant.cpp, CompareValueLS and KindRank): an object of the runtime orders as
// its text, and its text is GetString above. What the runtime adds is '==' for an object: its presentation AND its
// class, which only the factory knows.
bool ibValue::CompareValueEQ(const ibVariant& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		return GetString() == cParam.GetString() &&
			GetClassType() == cParam.GetClassType();
	default:
		return ibVariant::CompareValueEQ(cParam);
	}
}

//*************************************************************
//*          identity — the hash bound to the order           *
//*************************************************************

// Placed AFTER the whole compare family, not inside it: the contract this obeys
// is stated over CompareValueLS (see value.h), so it reads in order — the
// ordering first, then the hash that has to agree with it.

// A whole scalar payload, fed through the shared mixer OCTET BY OCTET — which is
// what FNV-1a actually is, and what a raw 64-bit payload needs: the composite
// hashes above fold values that are already well-mixed hashes, these fold a date
// or an integer straight off the field.
static inline std::uint64_t HashStep(std::uint64_t h, std::uint64_t v)
{
	for (int i = 0; i < 8; ++i, v >>= 8)
		h = ibHashCombine(h, v & 0xFF);
	return h;
}

size_t ibValue::GetValueHash() const
{
	switch (m_typeClass)
	{
	// Both "no scalar payload" tags order equal to each other and below
	// everything, so they share one bucket.
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		return 0;

	// EACH SCALAR KIND HASHES ITS OWN PAYLOAD, exactly, because each is its own
	// rank in the order now (see KindRank) — a boolean is never order-equal to a
	// number, so nothing forces their hashes together and neither has to be
	// blurred to meet the other. A date keeps its full count for the same
	// reason: the /1000 it used to carry existed only to meet GetNumber().
	case ibValueTypes::TYPE_BOOLEAN:
		return (size_t)HashStep(kIbHashBasis, m_bData ? 1u : 0u);
	case ibValueTypes::TYPE_DATE:
		return (size_t)HashStep(kIbHashBasis, (uint64_t)m_dData.GetValue());
	// A number still blurs to its integer part, and that one is NOT optional:
	// 1 and 1.0 are the same number and must share a bucket. 1.5 joining them
	// costs one comparison.
	case ibValueTypes::TYPE_NUMBER: {
		long long whole = 0;
		if (m_fData.ToInt(whole) != 0)
			return (size_t)HashStep(kIbHashBasis, ~0ULL);   // past int64 — one bucket for all of them
		return (size_t)HashStep(kIbHashBasis, (uint64_t)whole);
	}

	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return GetRef()->GetValueHash();   // a reference hashes as what it references

	// Text, and the object kinds that ORDER as text (CompareValueLS routes them
	// through CompareAsText) — so an enum and the string of its presentation land
	// in the same bucket, which is what their order says about them.
	default: {
		ibString built;
		const ibString& text = m_typeClass == ibValueTypes::TYPE_STRING ? m_sData : (built = GetString());   // a string's own text, read in place
		std::uint64_t h = kIbHashBasis;
		for (const ibChar* p = text.c_str(); *p != ibT('\0'); ++p)
			h = ibHashCombine(h, *p);
		return (size_t)h;
	}
	}
}

const ibValue& ibValue::operator+(const ibValue& cParam)
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_NUMBER:
		m_fData = m_fData + cParam.GetNumber();
		break;
	case ibValueTypes::TYPE_DATE:
		// The operand moves the date by its distance from the empty date: a number is that many seconds
		// (GetDate), a date its own count - the arithmetic a date on the right always had.
		m_dData = m_dData.AddMilliseconds(cParam.GetDate() - ibDateTime());
		break;
	case ibValueTypes::TYPE_STRING:
		// Text is joined, as the script's own '+' joins it (procUnit.cpp, AddValue). Without this
		// branch a string was left as it was, and Sum() of "1", "2", "3" answered "1".
		m_sData += cParam.GetString();
		break;
	default:
		break;      // '+' is defined for number, date and string; others unchanged
	}

	return *this;
}

const ibValue& ibValue::operator-(const ibValue& cParam)
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_NUMBER:
		m_fData = m_fData - cParam.GetNumber();
		break;
	case ibValueTypes::TYPE_DATE:
		m_dData = m_dData.AddMilliseconds(-(cParam.GetDate() - ibDateTime()));   // as '+', the other way
		break;
	default:
		break;      // '-' is defined for number and date only; others unchanged
	}
	return *this;
}

//*************************************************************
//				WORK AS AN AGGREGATE OBJECT                   *
//*************************************************************

long ibValue::GetNProps() const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetNProps();
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNProps();
	return 0;
}

long ibValue::FindProp(const ibString& strPropName) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->FindProp(strPropName);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->FindProp(strPropName);
	return wxNOT_FOUND;
}

const ibString& ibValue::GetPropName(const long lPropNum) const
{
	static const ibString s_absent;
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetPropName(lPropNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetPropName(lPropNum);
	return s_absent;
}

bool ibValue::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetPropVal(lPropNum, pvarPropVal);
	return false;
}

bool ibValue::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	// Write path: a const reference must not mutate the non-owned object's field.
	// A plain (owned) reference still delegates — it may be a transparent
	// read-only wrapper over a mutable object. The assert catches a future
	// caller loudly in Debug; the Error keeps Release graceful.
	wxASSERT_MSG(!IsConstReference(), wxT("SetPropVal on a const reference (TYPE_CONST_REFFER)"));
	if (m_typeClass == ibValueTypes::TYPE_CONST_REFFER)
		ibBackendCoreException::Error(_("Attempt to write to a field of a read-only (const) object"));
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return GetRef()->SetPropVal(lPropNum, varPropVal);
	return false;
}

bool ibValue::IsPropReadable(const long lPropNum) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->IsPropReadable(lPropNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropReadable(lPropNum);
	return true;
}

bool ibValue::IsPropWritable(const long lPropNum) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->IsPropWritable(lPropNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropWritable(lPropNum);
	return true;
}

bool ibValue::IsPropScoped(const long lPropNum) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->IsPropScoped(lPropNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropScoped(lPropNum);
	return false;
}

long ibValue::ibMemberTable::AppendProp(const ibString& strPropName, bool readable, bool writable, bool scoped, const long lPropNum, const long lPropAlias)
{
	const unsigned int flags =
		(readable ? eProp_Readable : 0u) |
		(writable ? eProp_Writable : 0u) |
		(scoped   ? eProp_Scoped   : 0u);
	return AppendProp(strPropName, flags, lPropNum, lPropAlias);
}

long ibValue::GetNMethods() const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetNMethods();
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNMethods();
	return 0;
}

// Per-class method resolver. LINQ pipeline ops bypass this entirely —
// compile-side emits OPER_CALL_LINQ via FindLinqMethodByName before
// reaching the OPER_CALL_METHOD path.
long ibValue::FindMethod(const ibString& strMethodName) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->FindMethod(strMethodName);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr) {
		const long n = methodHelper->FindMethod(strMethodName);
		if (n >= 0) return n;
	}
	return wxNOT_FOUND;
}

const ibString& ibValue::GetMethodName(const long lMethodNum) const
{
	static const ibString s_absent;
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetMethodName(lMethodNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodName(lMethodNum);
	return s_absent;
}

const ibString& ibValue::GetMethodHelper(const long lMethodNum) const
{
	static const ibString s_absent;
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetMethodHelper(lMethodNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodHelper(lMethodNum);
	return s_absent;
}

long ibValue::GetNParams(const long lMethodNum) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetNParams(lMethodNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNParams(lMethodNum);
	return 0;
}

bool ibValue::GetParamDefValue(const long lMethodNum,
	const long lParamNum,
	ibValue& pvarParamDefValue) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
	return false;
}

bool ibValue::HasRetVal(const long lMethodNum) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->HasRetVal(lMethodNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->HasRetVal(lMethodNum);
	return false;
}

bool ibValue::CallAsProc(const long lMethodNum,
	ibValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->CallAsProc(lMethodNum, paParams, lSizeArray);
	return false;
}

bool ibValue::CallAsFunc(const long lMethodNum,
	ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}

// Out-of-line: a member contributor (ibNameFiller) is called on the owning value,
// which must be complete here. Free contributors get (helper, ctx); member
// contributors are dispatched as (value->*fn)(helper).
void ibValue::ibMemberTable::Build()
{
	if (!HasBinders())
		return;
	ClearHelper();
	const auto run = [this](const ibBoundNames& b) {
		if (b.m_freeFn != nullptr)
			b.m_freeFn(*this, b.m_ctx);
		else if (b.m_memberFn != nullptr)
			(b.m_ctx->*b.m_memberFn)(*this);
	};
	ForEachBinder([&run](const auto& b) { if (!b.m_tail) run(b); });
	ForEachBinder([&run](const auto& b) { if (b.m_tail)  run(b); });   // module exports last — keeps fixed-method indices stable
	// Release — pairs with the acquire load in EnsureBuilt(), so any thread that sees
	// kBuilt (fast path or the wait loop) also sees every m_props/m_methods write above.
	m_buildState.store(kBuilt, std::memory_order_release);
}

//get the current value (relevant for aggregate objects or dialog objects)
ibValue ibValue::GetValue(bool getThis) const
{
	if (getThis)
		return const_cast<ibValue*>(this);  // legacy: returns this-as-pointer-via-converting-ctor
	if (m_pRef != nullptr && IsReference())
		return GetRef()->GetValue(true); // true - a sign of creating a new variable - a reference to an aggregate object
	return *this;
}

// ⭐ AN ORDINARY VALUE NARROWS TO ITS OWN CLASS, and that is the whole of the default: a field governed
// by this one may hold what THIS is, and nothing else. What came in comes back out untouched — there is
// nothing to narrow it to beyond being of that class. The two values that mean something more — a type
// description, and a reference whose metaobject governs it — say so by overriding; nobody else has to
// know they exist.
//
// ⚠ THROUGH THE REFERENCE, as GetClassType above does it. A slot holds an object as TYPE_REFFER, so
// asking the slot must reach the object, or every answer here would be "a reference" — the class of
// the wrapper rather than of what stands in it.
bool ibValue::AdjustOutValue(const ibValue& varValue, ibValue& out) const
{
	if (m_pRef != nullptr && IsReference())
		return GetRef()->AdjustOutValue(varValue, out);

	const ibClassID& limit = GetClassType();
	if (varValue.GetClassType() == limit) {
		out = varValue;
		return true;
	}

	// ⚠ …AND OTHERWISE THE EMPTY VALUE OF THE LIMIT, not nothing: whoever asked gets a value of the
	// right type either way, so the type of the answer is the answer to "what does this narrow to"
	// without a second question being asked (Max, 2026-09-24). A class the registry cannot make — a
	// configuration whose metaobjects were described but not registered, a headless tool before it
	// opens a base — is the undefined value, as it is everywhere else this asks.
	out = IsRegisterCtor(limit) ? CreateObject(limit) : ibValue();
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// The primitives' names are the core's one table (core/core.h, GetNameObjectFromVT).
PRIMITIVE_TYPE_REGISTER(ibValue, GetNameObjectFromVT(ibValueTypes::TYPE_EMPTY), ibValueTypes::TYPE_EMPTY, g_valueUndefinedCLSID);

PRIMITIVE_TYPE_REGISTER(ibValue, GetNameObjectFromVT(ibValueTypes::TYPE_BOOLEAN), ibValueTypes::TYPE_BOOLEAN, g_valueBooleanCLSID);
PRIMITIVE_TYPE_REGISTER(ibValue, GetNameObjectFromVT(ibValueTypes::TYPE_NUMBER), ibValueTypes::TYPE_NUMBER, g_valueNumberCLSID);
PRIMITIVE_TYPE_REGISTER(ibValue, GetNameObjectFromVT(ibValueTypes::TYPE_DATE), ibValueTypes::TYPE_DATE, g_valueDateCLSID);
PRIMITIVE_TYPE_REGISTER(ibValue, GetNameObjectFromVT(ibValueTypes::TYPE_STRING), ibValueTypes::TYPE_STRING, g_valueStringCLSID);

PRIMITIVE_TYPE_REGISTER(ibValue, GetNameObjectFromVT(ibValueTypes::TYPE_NULL), ibValueTypes::TYPE_NULL, g_valueNullCLSID);