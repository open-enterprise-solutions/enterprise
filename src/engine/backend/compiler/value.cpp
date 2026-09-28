////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : base value  
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "backend/backend_exception.h"

#include <wx/datetime.h>
#include <wx/longlong.h>

// ⭐ THE TWO DOORS A DATE COMES IN THROUGH, and what they refuse.
//
// A date is a wall-clock reading (fdate.h): the parts, counted as milliseconds with no zone in them.
// Parts a person or a program hands over are checked against the calendar before they become one -
// a 30th of February or a 25th hour is the empty date here, not the day after the 29th and not
// whatever a rolled-over clock would make of it.
namespace {

wxLongLong_t DateOfParts(int year, int month, int day, int hour, int minute, int second, int millisecond = 0)
{
	if (month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0 || millisecond < 0
		|| !ibPartsAreADate(year, static_cast<unsigned>(month), static_cast<unsigned>(day), static_cast<unsigned>(hour),
		                    static_cast<unsigned>(minute), static_cast<unsigned>(second), static_cast<unsigned>(millisecond)))
		return emptyDate;
	return ibWallFromParts(year, static_cast<unsigned>(month), static_cast<unsigned>(day),
		static_cast<unsigned>(hour), static_cast<unsigned>(minute), static_cast<unsigned>(second), static_cast<unsigned>(millisecond));
}

// A text as a date: by its digits first (ibWallOfText - the reference system's forms and ISO 8601, the
// same door the drivers read a TIMESTAMP's text through), and any other text handed to
// wxDateTime::ParseDateTime, the free-form reader, to come back through the bridge by its local parts.
bool DateOfText(const wxString& text, wxLongLong_t& out)
{
	if (ibWallOfText(text, out))
		return true;
	wxDateTime parsed;
	if (!parsed.ParseDateTime(text))
		return false;
	out = ibWallOfDateTime(parsed);
	return true;
}

} // namespace


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
#include "backend/utils/debugTrace.h"

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
	ibJournalInfo(wxT("value"),wxT("%s"), wxString::FromUTF8(os.str().c_str()));
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

ibValue::ibValue()
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	DEBUG_VALUE_CREATE();
}

//copy constructor:
ibValue::ibValue(const ibValue& varValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	Copy(varValue);
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValue&& varValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	Move(std::move(varValue));
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValue* pValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_pRef(pValue), m_refCount(0)
{
	if (m_pRef != nullptr) {
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibBackendValue* pParam)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_pRef(pParam ? pParam->GetImplValueRef() : nullptr), m_refCount(0)
{
	if (m_pRef != nullptr) {
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}
	DEBUG_VALUE_CREATE();
}

// ⭐⭐ A DATE IS A WALL-CLOCK READING (fdate.h): the parts a calendar and a clock show, counted as
// milliseconds with no zone in them. A wxDateTime is an INSTANT read through the machine's zone, so
// it comes in and goes out by its LOCAL PARTS and by nothing else (ibWallOfDateTime / ibDateTimeOfWall)
// - the reading 10:30 is the wxDateTime whose local time is 10:30, on whichever machine.
ibValue::ibValue(const wxDateTime& cParam)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	m_dData = ibWallOfDateTime(cParam);
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(int nYear, int nMonth, int nDay, unsigned short nHour, unsigned short nMinute, unsigned short nSecond)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	m_dData = DateOfParts(nYear, nMonth, nDay, nHour, nMinute, nSecond);   // no such day is the empty date
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValueTypes type, bool readOnly)
	: m_typeClass(type), m_bReadOnly(readOnly), m_dData(0), m_refCount(0)
{
	switch (type)
	{
	case TYPE_BOOLEAN:
		m_bData = false;
		break;
	case TYPE_NUMBER:
		m_fData.SetZero();
		break;
	case TYPE_DATE:
		m_dData = emptyDate;
		break;
	case TYPE_STRING:
		break;      // the zeroed word IS the empty string
	default:
		m_pRef = nullptr;
		break;
	}

	DEBUG_VALUE_CREATE();
}

//Constructors by types:
#define CVALUE_BYTYPE(v_parclass, v_type, v_value) \
ibValue::ibValue (v_parclass cParam) \
    : m_typeClass(v_type), m_bReadOnly(false), m_dData(0), m_refCount(0) \
{\
	v_value = cParam;\
	DEBUG_VALUE_CREATE();\
}

CVALUE_BYTYPE(bool, ibValueTypes::TYPE_BOOLEAN, m_bData);

CVALUE_BYTYPE(signed int, ibValueTypes::TYPE_NUMBER, m_fData);
CVALUE_BYTYPE(unsigned int, ibValueTypes::TYPE_NUMBER, m_fData);
CVALUE_BYTYPE(double, ibValueTypes::TYPE_NUMBER, m_fData);
CVALUE_BYTYPE(const ibNumber&, ibValueTypes::TYPE_NUMBER, m_fData);

CVALUE_BYTYPE(wxLongLong_t, ibValueTypes::TYPE_DATE, m_dData);

// String ctors — the text goes into the union's word as it is (a copy of an
// ibString is one more owner, not the characters); the empty string is the
// zeroed word itself. char* keeps the historical wxString(char*) conversion
// (NOT ibString's UTF-8 path).
ibValue::ibValue(const char* cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	if (cParam && *cParam) m_sData = ibString(wxString(cParam));
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(const wchar_t* cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	m_sData = ibString(cParam);
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(const wxString& cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	m_sData = ibString(cParam);
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibString&& cParam)   // native — takes the text over (runtime string functions)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_dData(0), m_refCount(0)
{
	m_sData = std::move(cParam);
	DEBUG_VALUE_CREATE();
}

#undef CVALUE_BYTYPE
#undef CVALUE_BYTYPE_MOVE

ibValue::~ibValue()
{
	if (m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef && m_pRef != this)
		m_pRef->DecrRef();
	else if (m_typeClass == ibValueTypes::TYPE_STRING)
		m_sData.~ibString();   // ONLY if STRING — the word aliases m_pRef in the union
	else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		m_fData.~ibNumber();   // a heap-tier number lets go of its BigImpl
	DEBUG_VALUE_DELETE();
}

// The write-denied refusal, OUT OF LINE. Built inside Reset, its message — a translation lookup and three
// wxStrings — gave Reset a 288-byte frame and a stack-cookie check, and Reset runs on every slot of every
// frame a script call releases: the price of a sentence that is almost never said, paid on each of them
// (the disassembly, 2026-09-28). Same shape as the raise helpers in procUnit.cpp.
IB_NOINLINE static void RaiseWriteDenied()
{
	ibBackendCoreException::Error(_("Attempt to assign a value to a write-denied variable"));
}

void ibValue::Reset()
{
	if (m_typeClass != ibValueTypes::TYPE_EMPTY && m_bReadOnly) RaiseWriteDenied();

	if (m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef)
		m_pRef->DecrRef();
	else if (m_typeClass == ibValueTypes::TYPE_STRING)
		m_sData.~ibString();   // lets go of the text
	else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		m_fData.~ibNumber();   // …and of a heap-tier number's BigImpl
	// TYPE_CONST_REFFER: non-owned read-only view — only TYPE_REFFER is DecrRef'd
	// above, so the const-ref's pointer is just dropped below (the metadata tree
	// owns the object; we never ref-count or delete it). The const-ref never sets
	// m_bReadOnly, so the write-denied guard above doesn't fire for it either.

	m_typeClass = ibValueTypes::TYPE_EMPTY;
	m_dData = 0;   // the WHOLE word: the empty state of every member (value.h, the union)
}

//methods:
void ibValue::Copy(const ibValue& cOld)
{
	if (this == &cOld)
		return;

	// STRING ONTO STRING SHARES THE TEXT: one more owner of the source's text, the
	// destination's own let go — no characters copied. This is the road `operator=`
	// takes, i.e. everything outside the interpreter: table cells, array elements,
	// record fields.
	//
	// NUMBER ONTO NUMBER likewise assigns over itself: a heap-tier number is shared,
	// an immediate one is a word — no Reset() and switch around one assignment.
	//
	// Read-only is excluded deliberately: writing through the tag would step over
	// Reset()'s write-denied check, which is where that error is raised.
	if (!m_bReadOnly && m_typeClass == cOld.m_typeClass) {
		if (m_typeClass == ibValueTypes::TYPE_STRING) { m_sData = cOld.m_sData; return; }
		if (m_typeClass == ibValueTypes::TYPE_NUMBER) { m_fData = cOld.m_fData; return; }
	}

	Reset();

	m_typeClass = cOld.m_typeClass;

	switch (m_typeClass) {
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		m_bData = cOld.m_bData;
		break;
	case ibValueTypes::TYPE_NUMBER:
		m_fData = cOld.m_fData;
		break;
	case ibValueTypes::TYPE_STRING:
		m_sData = cOld.m_sData;   // the text shared — the zeroed word after Reset() is an empty string
		break;
	case ibValueTypes::TYPE_DATE:
		m_dData = cOld.m_dData;
		break;
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef = const_cast<ibValue*>(&cOld);
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_REFFER:
		m_pRef = cOld.m_pRef;
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_CONST_REFFER:
		// weak, non-owning copy — NO IncrRef (the metadata tree owns the object).
		// Object-write protection is by type (TYPE_CONST_REFFER), not m_bReadOnly.
		m_pConstRef = cOld.m_pConstRef;
		break;
	default:
		m_typeClass = ibValueTypes::TYPE_EMPTY;
		break;
	}
}

void ibValue::Move(ibValue&& cOld)
{
	if (this == &cOld)
		return;

	Reset();

	m_typeClass = cOld.m_typeClass;

	switch (m_typeClass) {
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		m_bData = std::move(cOld.m_bData);
		break;
	case ibValueTypes::TYPE_NUMBER:
		m_fData = std::move(cOld.m_fData);
		break;
	case ibValueTypes::TYPE_STRING:
		m_sData = std::move(cOld.m_sData);   // take the text over
		break;
	case ibValueTypes::TYPE_DATE:
		m_dData = std::move(cOld.m_dData);
		break;
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef = const_cast<ibValue*>(&cOld);
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_REFFER:
		m_pRef = cOld.m_pRef;
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_CONST_REFFER:
		// weak non-owning const ref — share the pointer, NO IncrRef. Object-write
		// protection is by type (TYPE_CONST_REFFER), not m_bReadOnly.
		m_pConstRef = cOld.m_pConstRef;
		break;
	default:
		m_typeClass = ibValueTypes::TYPE_EMPTY;
		break;
	}

	cOld.Reset();
}

void ibValue::operator = (bool cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	m_bData = cParam;
}

void ibValue::operator = (short cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibValue::operator = (unsigned short cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibValue::operator = (int cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibValue::operator = (unsigned int cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibValue::operator = (float cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibValue::operator = (double cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibValue::operator = (const ibNumber& cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibValue::operator = (const wxDateTime& cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_DATE;
	m_dData = ibWallOfDateTime(cParam);
}

void ibValue::operator = (wxLongLong_t cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_DATE;
	m_dData = cParam;
}


void ibValue::operator = (const wxString& cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = ibString(cParam);
}

// Character POINTERS assign as strings. Without these two, `value = "text"` and
// `value = wxEmptyString` picked operator=(bool) — pointer-to-bool is a standard
// conversion and outranks the user-defined one to wxString — and quietly produced
// Boolean TRUE. Same trap the const ibValue* overload was added for.
void ibValue::operator = (const char* cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	if (cParam && *cParam) m_sData = ibString(wxString(cParam));
}

void ibValue::operator = (const wchar_t* cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = ibString(cParam);
}

void ibValue::operator = (ibString&& cParam)
{
	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = std::move(cParam);   // take the text over
}

void ibValue::operator = (const ibValue& cParam)
{
	if (this != &cParam && !m_bReadOnly)
		Copy(cParam);
}

void ibValue::operator=(ibValue&& cParam)
{
	if (this != &cParam && !m_bReadOnly)
		Move(std::move(cParam));
}

void ibValue::operator = (ibValueTypes type)
{
	ibValue objValue(*this);

	// The text or the number of the kind the value WAS is let go (objValue holds it for SetData),
	// and the word zeroed — the empty state of every kind (value.h, the union).
	if (m_typeClass == ibValueTypes::TYPE_STRING)
		m_sData.~ibString();
	else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		m_fData.~ibNumber();
	m_dData = 0;

	switch (type)
	{
	case TYPE_BOOLEAN:
		m_bData = false;
		break;
	case TYPE_NUMBER:
		break;      // the zeroed word is the number 0
	case TYPE_DATE:
		m_dData = emptyDate;
		break;
	case TYPE_STRING:
		break;      // the zeroed word is the empty string
	default:
		m_pRef = nullptr;
		break;
	}

	m_typeClass = type;

	SetData(objValue);
}

void ibValue::operator=(ibBackendValue* pValue)
{
	if (this != (pValue ? pValue->GetImplValueRef() : nullptr) && !m_bReadOnly) {
		Reset();
		if (pValue != nullptr) {
			m_typeClass = ibValueTypes::TYPE_REFFER;
			m_pRef = pValue->GetImplValueRef();
			m_pRef->IncrRef();
		}
	}
}

void ibValue::operator = (ibValue* pValue)
{
	if (this != pValue && !m_bReadOnly) {
		Reset();
		if (pValue != nullptr) {
			m_typeClass = ibValueTypes::TYPE_REFFER;
			m_pRef = pValue;
			m_pRef->IncrRef();
		}
	}
}

void ibValue::operator = (const ibValue* pValue)
{
	// const source (const ibValueMetaObject* from GetMetaObject(), const-meta
	// refactor) — store a NON-owning, read-only reference as TYPE_CONST_REFFER.
	// Unlike operator=(ibValue*): NO IncrRef (we don't own the object — the
	// metadata tree does) and Reset()/dtor must never delete it. m_bReadOnly
	// blocks mutation through this value. Without this overload the const ptr
	// fell through to operator=(bool) and the object became a Boolean.
	if (this != pValue && !m_bReadOnly) {
		Reset();
		if (pValue != nullptr) {
			m_typeClass = ibValueTypes::TYPE_CONST_REFFER;
			m_pConstRef = pValue;
			// No m_bReadOnly: the slot stays reassignable; object-write
			// protection is by TYPE_CONST_REFFER (SetPropVal/SetType), not the flag.
		}
	}
}

void ibValue::SetValue(const ibValue& varValue)
{
	if (this == &varValue)
		return;

	if (m_typeClass == ibValueTypes::TYPE_REFFER)
		m_pRef->SetValue(varValue);
	else
		Copy(varValue);
}

bool ibValue::SetBoolean(const wxString& strBoolean)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER) {
		return m_pRef->SetBoolean(strBoolean);
	}

	Reset();

	m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	m_bData = stringUtils::CompareString(strBoolean, wxT("True"));

	return true;
}

bool ibValue::SetNumber(const wxString& strNumber)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER) {
		return m_pRef->SetNumber(strNumber);
	}

	Reset();

	ibNumber fData = 0;
	if (!fData.FromString(strNumber))
		return false;

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = fData;

	return true;
}

bool ibValue::SetDate(const wxString& strDate)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER) {
		return m_pRef->SetDate(strDate);
	}

	Reset();

	wxLongLong_t dData = emptyDate;
	if (!strDate.IsEmpty() && !DateOfText(strDate, dData))
		return false;

	m_typeClass = ibValueTypes::TYPE_DATE;
	m_dData = dData;

	return true;
}

bool ibValue::SetString(const wxString& strString)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER) {
		return m_pRef->SetString(strString);
	}

	// Already a string: the new text simply replaces the one held.
	if (!m_bReadOnly && m_typeClass == ibValueTypes::TYPE_STRING) {
		m_sData = ibString(strString);
		return true;
	}

	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = ibString(strString);

	return true;
}

bool ibValue::SetString(ibString&& strString)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->SetString(strString.ToWxString());

	// Already a string: the text is taken over in place of the one held.
	if (!m_bReadOnly && m_typeClass == ibValueTypes::TYPE_STRING) {
		m_sData = std::move(strString);
		return true;
	}

	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = std::move(strString);

	return true;
}

bool ibValue::FindValue(const wxString& findData, std::vector<ibValue>& listValue) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->FindValue(findData, listValue);

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

void ibValue::SetData(const ibValue& varValue)
{
	if (this == &varValue)
		return;

	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		SetBoolean(varValue.GetString());
		return;
	case ibValueTypes::TYPE_NUMBER:
		SetNumber(varValue.GetString());
		return;
	case ibValueTypes::TYPE_STRING: {
		// A string source shares its text (one more owner, no copy); a coerced one
		// (number/bool/date) builds it. Either way the text is held BEFORE
		// SetString()'s Reset() runs, because varValue may be a reference that
		// resolves back to this very value.
		SetString(varValue.GetString());
		return;
	}
	case ibValueTypes::TYPE_DATE:
		SetDate(varValue.GetString());
		return;
	case ibValueTypes::TYPE_REFFER:
		if (m_pRef != nullptr)
			m_pRef->SetData(varValue);
		return;
	default:
		break;      // every other type is handled by the tail below
	}

	SetValue(varValue);
}

bool ibValue::GetBoolean() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData;
	case ibValueTypes::TYPE_NUMBER:
		return !m_fData.IsZero();
	case ibValueTypes::TYPE_STRING:
		return stringUtils::CompareString(wxT("True"), stringUtils::TrimAll(GetString()));
	case ibValueTypes::TYPE_DATE:
		return false;
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetBoolean();
	default:
		break;      // every other type is not boolean-convertible — false below
	}

	return false;
}

ibNumber ibValue::GetNumber() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData;
	case ibValueTypes::TYPE_NUMBER:
		return m_fData;
	case ibValueTypes::TYPE_STRING: {
		wxString strVal = GetString();
		strVal.Trim(true);
		strVal.Trim(false);
		strVal.MakeUpper();
		ibNumber number;
		if (!number.FromString(strVal))
			ibBackendCoreException::Error(_("Cannot convert string to number!"));
		return number;
	}
	case ibValueTypes::TYPE_DATE:
		return m_dData / 1000;
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetNumber();
	default:
		break;      // every other type has no numeric payload — zero below
	}

	return 0;
}

ibString ibValue::GetString() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		return ibString();
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData ? wxT("True") : wxT("False");
	case ibValueTypes::TYPE_NUMBER:
		return m_fData.ToString();
	case ibValueTypes::TYPE_STRING:
		return m_sData;   // the text SHARED — one more owner, no copy
	case ibValueTypes::TYPE_DATE: {
		ibDateParts p;
		ibWallToParts(m_dData, p);
		return wxString::Format(wxT("%02u.%02u.%04d %02u:%02u:%02u"), p.m_day, p.m_month, p.m_year, p.m_hour, p.m_minute, p.m_second);
	}
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		// A REFERENCE TO A STRING IS STILL A STRING: the target shares its text the same way.
		return m_pRef != nullptr ? m_pRef->GetString() : ibString();
	default:
		break;      // object kinds present as their class name — tail below
	}

	return GetClassName();
}

wxLongLong_t ibValue::GetDate() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return emptyDate;
	case ibValueTypes::TYPE_NUMBER: {
		wxLongLong_t dTemp = 0;
		if (!m_fData.ToInt(dTemp))
			return dTemp * 1000;
		return emptyDate;
	}
	case ibValueTypes::TYPE_STRING: {
		// The same reading SetDate gives the text - one door for "this text as a date". (This one used
		// to skip the eight-digit form the setter took, so `"20260315"` was a date when assigned and
		// the empty date when read.)
		wxLongLong_t dData = emptyDate;
		return DateOfText(m_sData.ToWxString(), dData) ? dData : emptyDate;
	}
	case ibValueTypes::TYPE_DATE:
		return m_dData;
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetDate();
	default:
		break;      // every other type carries no date — emptyDate below
	}

	return emptyDate;
}

ibValue* ibValue::GetRef() const
{
	// Resolve through both owned and const (non-owned) references — m_pRef and
	// m_pConstRef alias the same union pointer, so the const object is reachable
	// for read dispatch. The const-cast is inherent to GetRef's non-const return;
	// write protection is enforced separately via m_bReadOnly, not here.
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetRef();
	return const_cast<ibValue*>(this);
}

void ibValue::ShowValue()
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->ShowValue();
}

// A number of seconds moves a date along the wall, where every day is 86 400 of them and no hour is
// ever skipped or repeated: the reading IS the calendar (fdate.h), so a shift and a span are plain
// arithmetic on it. (When the reading was an instant, both went through the machine's calendar to
// get the same answer across a clock change; there is nothing left to go through.)
wxLongLong_t ibValue::ShiftDate(wxLongLong_t date, wxLongLong_t milliseconds)
{
	return date + milliseconds;
}

wxLongLong_t ibValue::DateSpan(wxLongLong_t later, wxLongLong_t earlier)
{
	return later - earlier;
}

wxDateTime ibValue::GetDateTime() const
{
	return ibDateTimeOfWall(GetDate());
}

void ibValue::FromDate(int& nYear, int& nMonth, int& nDay) const
{
	ibDateParts p;
	ibWallToParts(GetDate(), p);
	nYear = p.m_year;
	nMonth = static_cast<int>(p.m_month);
	nDay = static_cast<int>(p.m_day);
}

void ibValue::FromDate(int& nYear, int& nMonth, int& nDay, unsigned short& nHour, unsigned short& nMinute, unsigned short& nSecond) const
{
	ibDateParts p;
	ibWallToParts(GetDate(), p);
	nYear = p.m_year;
	nMonth = static_cast<int>(p.m_month);
	nDay = static_cast<int>(p.m_day);
	nHour = static_cast<unsigned short>(p.m_hour);
	nMinute = static_cast<unsigned short>(p.m_minute);
	nSecond = static_cast<unsigned short>(p.m_second);
}

void ibValue::FromDate(int& nYear, int& nMonth, int& nDay, int& DayOfWeek, int& DayOfYear, int& WeekOfYear) const
{
	// The day's place in the week (ISO: Monday = 1 ... Sunday = 7), in the year and in the ISO week
	// numbering are read off the same parts as the date - one calendar, so BegOfWeek / EndOfWeek and
	// GetWeekOfYear cannot disagree with the query engine's WEEKDAY / WEEK over the same value.
	ibDateParts p;
	ibWallToParts(GetDate(), p);
	nYear = p.m_year;
	nMonth = static_cast<int>(p.m_month);
	nDay = static_cast<int>(p.m_day);
	DayOfWeek = static_cast<int>(p.m_weekDay);
	DayOfYear = static_cast<int>(p.m_yearDay);
	WeekOfYear = static_cast<int>(p.m_isoWeek);
}

bool ibValue::IsEmpty() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData == false;
	case ibValueTypes::TYPE_NUMBER:
		return m_fData.IsZero();
	case ibValueTypes::TYPE_DATE:
		return m_dData == emptyDate;
	case ibValueTypes::TYPE_STRING:
		return m_sData.IsEmpty();
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		return false;
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef ? m_pRef->IsEmpty() : true;
	default:
		break;      // TYPE_EMPTY / TYPE_NULL and anything new — empty below
	}

	return true;
}

void ibValue::SetType(ibValueTypes type)
{
	// Write path: a const reference must not retype the non-owned object.
	// A plain (owned) reference still delegates — it may be a transparent
	// read-only wrapper over a mutable object. The assert catches a future
	// caller loudly in Debug; the Error keeps Release graceful.
	wxASSERT_MSG(!IsConstReference(), wxT("SetType on a const reference (TYPE_CONST_REFFER)"));
	if (m_typeClass == ibValueTypes::TYPE_CONST_REFFER)
		ibBackendCoreException::Error(_("Attempt to change the type of a read-only (const) object"));
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		m_pRef->SetType(type);
	else if (m_typeClass != type) {
		// A NEW KIND STARTS FROM THE ZEROED WORD (value.h, the union): the text or the number of
		// the old one is let go — retagging alone would read a boolean's byte as a number and keep
		// a string's text forever.
		if (m_typeClass == ibValueTypes::TYPE_STRING)
			m_sData.~ibString();
		else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
			m_fData.~ibNumber();
		m_dData = 0;
		m_typeClass = type;
	}
}

ibValueTypes ibValue::GetType() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetType();
	return m_typeClass;
}

//*************************************************************

wxString ibValue::GetClassName() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetClassName();
	const ibClassID& clsid = GetClassType();
	if (clsid == 0) ibBackendCoreException::Error(_("Class not registered"));
	return ibValue::GetNameObjectFromID(clsid);
}

ibClassID ibValue::GetClassType() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetClassType();
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
		return m_pRef->SetAt(varKeyValue, varValue);
	const long lPropNum = FindProp(varKeyValue.GetString());
	if (lPropNum != wxNOT_FOUND)
		return SetPropVal(lPropNum, varValue);
	return false;
}

bool ibValue::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	if (m_pRef && IsReference())
		return m_pRef->GetAt(varKeyValue, pvarValue);
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
		return m_pRef->CreateIterator();
	return nullptr;
}

//*************************************************************
//*                    compare support                        *
//*************************************************************

// Three-way ordering primitive: <0 this orders before cParam, >0 after, 0 equal. operator< and the
// derived GT/GE/LE all route through this, so ordering lives in ONE place per type and a subclass
// overrides it once (see ibValueReferenceDataObject). A value with NO scalar payload (TYPE_NULL = SQL
// null, OR TYPE_EMPTY = Undefined) sorts to the BOTTOM — SQL-aligned (NULLS FIRST asc / LAST desc) and,
// crucially, a TOTAL order, so std::sort / std::set / std::map keys over ibValue are well-defined (the
// old two-valued < returned false BOTH ways for such an operand, leaving it unordered). The SQL-null
// SEMANTICS (three-valued filter / join-key skip) key strictly on TYPE_NULL via IsNull(); ordering is
// the one place both "no value" tags share a position.
namespace {
// -1 / 0 / +1 from ONE pass of whatever ordering the type already has. Written
// once because every arm of the switch below wanted it and each was spelling it
// out again — `a < b ? -1 : (b < a ? 1 : 0)`, which asks twice.
template <class T>
inline int CompareOrder(const T& a, const T& b) { return (b < a) - (a < b); }

// Same result, from the primitives that hand back a DIFFERENCE rather than a
// sign (std::wstring::compare, ibNumber::Compare): a raw difference must not be
// passed on as if it were already -1/0/1.
inline int OrderOfDifference(const int difference) { return (difference > 0) - (difference < 0); }

// A value's text, read IN PLACE when it is a string (or a reference to one): its own text by
// reference, not even a count taken — a sort compares, and a hash reads, without touching the
// counters. Anything else (number, date, object) builds its text into `built`.
inline const ibString& AsText(const ibValue& value, ibString& built)
{
	const ibValue* held = &value;
	while ((held->m_typeClass == ibValueTypes::TYPE_REFFER || held->m_typeClass == ibValueTypes::TYPE_CONST_REFFER)
		&& held->m_pRef != nullptr)
		held = held->m_pRef;
	if (held->m_typeClass == ibValueTypes::TYPE_STRING)
		return held->m_sData;
	built = held->GetString();
	return built;
}

// Both sides as text, each the cheapest reading it has.
inline int CompareAsText(const ibValue& a, const ibValue& b)
{
	ibString builtA, builtB;
	return AsText(a, builtA).Cmp(AsText(b, builtB));
}

// WHICH STRETCH OF THE ORDER A KIND OCCUPIES. Values of different rank are
// separated by rank and never compared by payload, because no coercion between
// them states a fact: "abc" as a number is 0, `1` as text is "1" — and an OBJECT
// as a number is 0 too, which is how an empty array came to order EQUAL to the
// number zero (found by ValueHashContract.OrderEqualImpliesHashEqual, not by
// reading — the tree had been quietly answering that for as long as it existed).
//
// EVERY SCALAR KIND GETS ITS OWN RANK, and that is not tidiness — the coercions
// they used to meet on are NOT TRANSITIVE, so an order built on them is not a
// strict weak ordering and std::sort over it is formally undefined:
//
//   True == 2 and True == 3   (any non-zero number reads as True)
//   but 2 != 3
//
//   1 == date(1500) and 1 == date(1999)   (a date reads as instant/1000)
//   but date(1500) != date(1999)
//
// Both were live before this change; the hash contract test surfaced them by
// making a second implementation of "equal" disagree with the first. Separated
// by rank, a comparison only ever meets a kind it can answer about exactly.
//
//   1 Boolean · 2 Number · 3 Date
//   4 — String and the kinds whose order IS their presentation (enum, OLE,
//       function, iterator). They stay TOGETHER because text is transitive:
//       everything in this rank reduces to the same string comparison.
//   5 — TYPE_VALUE: arrays, containers, references, moments. Each carries its
//       own CompareValueLS, and what "equal" means is theirs to say.
//
// COERCION IS NOT GONE from the language — GetNumber() on a string still parses,
// and every arithmetic op still coerces. What is gone is coercion inside the
// ORDER, where it could make two unequal values compare equal to a third.
//
// Totality is preserved, which is the point — a std::map key and a sort need
// every pair placed — while GetValueHash only has to agree WITHIN a rank.
// FORCED for the same reason ibNumber::Compare is (backend.h): `inline` on a
// switch this small was still emitting a real `call`, twice per mixed-kind
// comparison, in the /FAsc output. The body is a jump table over eight tags.
IB_FORCEINLINE int KindRank(const ibValueTypes type)
{
	switch (type) {
	case ibValueTypes::TYPE_BOOLEAN: return 1;
	case ibValueTypes::TYPE_NUMBER:  return 2;
	case ibValueTypes::TYPE_DATE:    return 3;
	case ibValueTypes::TYPE_STRING:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		return 4;
	default:
		return 5;
	}
}
}

int ibValue::CompareValueLS(const ibValue& cParam) const
{
	// A reference answers as its target, and it answers FIRST — every rule below
	// reads m_typeClass, which for a reffer says only "a reference". What "the same"
	// means is the target's to say, including that it is itself.
	if (m_pRef != nullptr && IsReference())
		return m_pRef->CompareValueLS(cParam);

	// THE OTHER SIDE'S KIND, RESOLVED ONCE, and only when it can differ from the
	// raw tag. GetType() is virtual, and a virtual call is opaque to the
	// optimiser — it cannot prove two of them return the same thing. Written as
	// two comparisons against GetType(), MSVC emitted TWO indirect calls for the
	// null test alone, on every comparison, before any of the fast paths below
	// (verified in /FAsc output, 2026-08-15). A non-reference answers from its
	// own tag, so the common case now makes no virtual call at all.
	const ibValueTypes kindThere = cParam.IsReference() ? cParam.GetType() : cParam.m_typeClass;

	const bool aNull = (m_typeClass == ibValueTypes::TYPE_EMPTY || m_typeClass == ibValueTypes::TYPE_NULL);
	const bool bNull = (kindThere == ibValueTypes::TYPE_EMPTY || kindThere == ibValueTypes::TYPE_NULL);
	if (aNull || bNull)
		return aNull == bNull ? 0 : (aNull ? -1 : 1);   // no scalar payload = smallest; both -> equal

	// Different stretches of the order never meet on payload — see KindRank.
	//
	// GUARDED BY A TAG COMPARE: two raw tags that are equal already answer the
	// question — same tag, same rank — and that is what a real index is made of,
	// a column against a column. Without the guard, classification ran on every
	// comparison and cost 78% of a probe (198.8 -> 353.3 ns, §9). The left side
	// cannot be a reffer here (forwarded above), so equal tags also rule out a
	// reference hiding on one side only.
	if (m_typeClass != kindThere) {
		const int rankHere  = KindRank(m_typeClass);
		const int rankThere = KindRank(kindThere);
		if (rankHere != rankThere)
			return CompareOrder(rankHere, rankThere);
	}

	// EACH ARM ASKS ITS TYPE FOR ONE THREE-WAY ANSWER, off the FIELD where the tag
	// already says what the field is. Two things were being paid for nothing:
	//
	//  - Two comparisons per compare. `a < b ? -1 : (b < a ? 1 : 0)` runs the whole
	//    comparison twice to learn what one call returns — ibNumber::Compare and
	//    std::wstring::compare are three-way primitives already.
	//  - A getter round-trip. Inside `case TYPE_NUMBER` the type is known in the
	//    open, yet GetNumber() was a virtual call returning BY VALUE; on the string
	//    arm GetString() returned a wxString by value, so an ORDERING comparison
	//    allocated two strings. Under every sort and every keyed lookup.
	//
	// When both sides carry the same tag the payload is read straight off the
	// member. Mixed tags still go through the getters, which is where coercion
	// lives (a number compared against its own spelling) — that behaviour is
	// unchanged, it just stopped being the price everyone pays.
	switch (m_typeClass)
	{
	// SAME RANK FROM HERE ON — KindRank separated the rest above, so every arm
	// below meets only kinds it can honestly be compared with, and the coercions
	// it does are facts (True is 1, a date is its instant) rather than inventions.
	//
	// TWO ACCESSORS, NOT INTERCHANGEABLE:
	//   GetType()   — WHAT THE VALUE IS; follows a reffer. Classification asks it.
	//   m_typeClass — HOW IT IS STORED; asked only to decide whether the payload
	//                 can be read straight off the field. A reffer says REFFER
	//                 and falls through to the getter, which is correct.
	//
	// The LEFT side is always raw here: a reference was forwarded to its target
	// at the top of the function.
	case ibValueTypes::TYPE_BOOLEAN:
		return CompareOrder(m_bData,
			cParam.m_typeClass == ibValueTypes::TYPE_BOOLEAN ? cParam.m_bData : cParam.GetBoolean());
	case ibValueTypes::TYPE_NUMBER:
		return OrderOfDifference(cParam.m_typeClass == ibValueTypes::TYPE_NUMBER
			? m_fData.Compare(cParam.m_fData)          // both raw
			: m_fData.Compare(cParam.GetNumber()));    // another numeric kind, or a reffer to one
	case ibValueTypes::TYPE_DATE:
		return CompareOrder(m_dData,
			cParam.m_typeClass == ibValueTypes::TYPE_DATE ? cParam.m_dData : cParam.GetDate());
	case ibValueTypes::TYPE_STRING:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		return CompareAsText(*this, cParam);
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:   return m_pRef->CompareValueLS(cParam);
	default:                          break;   // EMPTY / NULL already returned above
	}

	return 0;
}

// '>' is the second three-way primitive: by default the SAME total order as '<' (a > b iff LS > 0),
// but a virtual hook of its own so a class can retune the `>` direction independently. '>=' / '<='
// are boolean and pair with their family ('>=' with GT, '<=' with LS), all going through the virtual
// calls so overriding LS (or GT) flows through. A class normally overrides only CompareValueLS.
int  ibValue::CompareValueGT(const ibValue& cParam) const { return CompareValueLS(cParam); }
bool ibValue::CompareValueGE(const ibValue& cParam) const { return CompareValueGT(cParam) >= 0; }
bool ibValue::CompareValueLE(const ibValue& cParam) const { return CompareValueLS(cParam) <= 0; }

// compare '=='
bool ibValue::CompareValueEQ(const ibValue& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return ibValueTypes::TYPE_EMPTY == cParam.GetType();
	case ibValueTypes::TYPE_NULL:
		return ibValueTypes::TYPE_NULL == cParam.GetType();
	// TYPE-STRICT, SO THE TAG COMES FIRST — and once it matches, both payloads are
	// known and read straight off the field.
	//
	// Each of these used to coerce and then check the tag: GetString() twice, BY
	// VALUE, before discovering the other side was a number and answering false.
	// The strictness was already there; it was just applied after paying for the
	// conversion it makes irrelevant. Same answers, no getters, no copies.
	// The kind check asks GetType(), which follows a reffer — a value must stay
	// equal to itself when it arrives by reference. The field access asks
	// m_typeClass, which does not: that one is about where the bytes are.
	case ibValueTypes::TYPE_BOOLEAN:
		if (cParam.GetType() != ibValueTypes::TYPE_BOOLEAN) return false;
		return m_bData == (cParam.m_typeClass == ibValueTypes::TYPE_BOOLEAN ? cParam.m_bData : cParam.GetBoolean());
	case ibValueTypes::TYPE_NUMBER:
		if (cParam.GetType() != ibValueTypes::TYPE_NUMBER) return false;
		return 0 == (cParam.m_typeClass == ibValueTypes::TYPE_NUMBER
			? m_fData.Compare(cParam.m_fData)
			: m_fData.Compare(cParam.GetNumber()));
	case ibValueTypes::TYPE_DATE:
		if (cParam.GetType() != ibValueTypes::TYPE_DATE) return false;
		return m_dData == (cParam.m_typeClass == ibValueTypes::TYPE_DATE ? cParam.m_dData : cParam.GetDate());
	case ibValueTypes::TYPE_STRING:
		// GetString() shares the text of a string AND of a reference to one, so
		// the text path needs no special case here.
		return cParam.GetType() == ibValueTypes::TYPE_STRING && CompareAsText(*this, cParam) == 0;
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		return GetString() == cParam.GetString() &&
			GetClassType() == cParam.GetClassType();
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueEQ(cParam);
	case ibValueTypes::TYPE_LAST:
		break;      // sentinel, never a live tag. Deliberately NOT `default:` —
		            // this switch covers every real type, so a new one must show
		            // up here as a warning rather than silently compare unequal.
	}

	return false;
}

// compare '!='
// '!=' IS '==' NEGATED, and nothing else. It used to be the whole EQ switch
// written again with every branch inverted — a second copy of the same rule,
// carrying the same coercions, and free to drift.
//
// It already had: a class that overrode CompareValueEQ (there are a dozen —
// enums, guid, OLE, composition field, the module managers) inherited THIS
// method unchanged, so its `<>` answered by the base's rule while its `=`
// answered by its own. The two could disagree about the same pair of values.
// Going through the virtual EQ makes an override of one an override of both.
bool ibValue::CompareValueNE(const ibValue& cParam) const
{
	return !CompareValueEQ(cParam);
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
	// blurred to meet the other. A date keeps its full instant for the same
	// reason: the /1000 it used to carry existed only to meet GetNumber().
	case ibValueTypes::TYPE_BOOLEAN:
		return (size_t)HashStep(kIbHashBasis, m_bData ? 1u : 0u);
	case ibValueTypes::TYPE_DATE:
		return (size_t)HashStep(kIbHashBasis, (uint64_t)m_dData);
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
		return m_pRef->GetValueHash();        // a reference hashes as what it references

	// Text, and the object kinds that ORDER as text (CompareValueLS routes them
	// through CompareAsText) — so an enum and the string of its presentation land
	// in the same bucket, which is what their order says about them.
	default: {
		ibString built;
		const ibString& text = AsText(*this, built);   // a string's own text, read in place
		std::uint64_t h = kIbHashBasis;
		for (const wchar_t* p = text.wc_str(); *p != L'\0'; ++p)
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
		// A number of seconds moves the date on the calendar (ShiftDate); a date is not a distance
		// and keeps the arithmetic it always had.
		m_dData = cParam.GetType() == ibValueTypes::TYPE_DATE ? m_dData + cParam.GetDate()
		                                                     : ShiftDate(m_dData, cParam.GetDate());
		break;
	default:
		break;      // '+' is defined for number and date only; others unchanged
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
		m_dData = cParam.GetType() == ibValueTypes::TYPE_DATE ? m_dData - cParam.GetDate()
		                                                     : ShiftDate(m_dData, -cParam.GetDate());
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
		return m_pRef->GetNProps();
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNProps();
	return 0;
}

long ibValue::FindProp(const ibString& strPropName) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->FindProp(strPropName);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->FindProp(strPropName);
	return wxNOT_FOUND;
}

const ibString& ibValue::GetPropName(const long lPropNum) const
{
	static const ibString s_absent;
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetPropName(lPropNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetPropName(lPropNum);
	return s_absent;
}

bool ibValue::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetPropVal(lPropNum, pvarPropVal);
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
		return m_pRef->SetPropVal(lPropNum, varPropVal);
	return false;
}

bool ibValue::IsPropReadable(const long lPropNum) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->IsPropReadable(lPropNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropReadable(lPropNum);
	return true;
}

bool ibValue::IsPropWritable(const long lPropNum) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->IsPropWritable(lPropNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropWritable(lPropNum);
	return true;
}

bool ibValue::IsPropScoped(const long lPropNum) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->IsPropScoped(lPropNum);
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
		return m_pRef->GetNMethods();
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
		return m_pRef->FindMethod(strMethodName);
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
		return m_pRef->GetMethodName(lMethodNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodName(lMethodNum);
	return s_absent;
}

const ibString& ibValue::GetMethodHelper(const long lMethodNum) const
{
	static const ibString s_absent;
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetMethodHelper(lMethodNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodHelper(lMethodNum);
	return s_absent;
}

long ibValue::GetNParams(const long lMethodNum) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetNParams(lMethodNum);
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
		return m_pRef->GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
	return false;
}

bool ibValue::HasRetVal(const long lMethodNum) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->HasRetVal(lMethodNum);
	ibMemberTable* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->HasRetVal(lMethodNum);
	return false;
}

bool ibValue::CallAsProc(const long lMethodNum,
	ibValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->CallAsProc(lMethodNum, paParams, lSizeArray);
	return false;
}

bool ibValue::CallAsFunc(const long lMethodNum,
	ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
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
		return m_pRef->GetValue(true); // true - a sign of creating a new variable - a reference to an aggregate object
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
		return m_pRef->AdjustOutValue(varValue, out);

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

PRIMITIVE_TYPE_REGISTER(ibValue, "Undefined", ibValueTypes::TYPE_EMPTY, g_valueUndefinedCLSID);

PRIMITIVE_TYPE_REGISTER(ibValue, "Boolean", ibValueTypes::TYPE_BOOLEAN, g_valueBooleanCLSID);
PRIMITIVE_TYPE_REGISTER(ibValue, "Number", ibValueTypes::TYPE_NUMBER, g_valueNumberCLSID);
PRIMITIVE_TYPE_REGISTER(ibValue, "Date", ibValueTypes::TYPE_DATE, g_valueDateCLSID);
PRIMITIVE_TYPE_REGISTER(ibValue, "String", ibValueTypes::TYPE_STRING, g_valueStringCLSID);

PRIMITIVE_TYPE_REGISTER(ibValue, "Null", ibValueTypes::TYPE_NULL, g_valueNullCLSID);