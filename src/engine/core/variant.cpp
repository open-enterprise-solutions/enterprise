#include "core/variant.h"

#include "core/exception.h"            // ibCoreException — a write denied, a text that is no number
#include "core/serialize/dataBuilder.h"   // ibDataNode — a value written, and read back

#include "core/localization/dictionary.h"   // _() — the refusals' words

// THE RUNTIME'S VALUE WITHOUT ITS RUNTIME — backend/compiler/value.cpp as it is, the lifecycle and the readings; what
// needs the factory (a class's name, an object made of a node) is the side's.

ibVariant::ibVariant()
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_refCount(0), m_dData()
{
}

//copy constructor:
ibVariant::ibVariant(const ibVariant& varValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_refCount(0), m_dData()
{
	Copy(varValue);
}

ibVariant::ibVariant(ibVariant&& varValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_refCount(0), m_dData()
{
	Move(std::move(varValue));
}

ibVariant::ibVariant(ibVariant* pValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_bReadOnly(false), m_refCount(0), m_pRef(pValue)
{
	if (m_pRef != nullptr) {
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}
}

ibVariant::ibVariant(int nYear, int nMonth, int nDay, unsigned short nHour, unsigned short nMinute, unsigned short nSecond)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_bReadOnly(false), m_refCount(0), m_dData()
{
	m_dData.FromParts(nYear, nMonth, nDay, nHour, nMinute, nSecond);   // no such day leaves the empty date
}

ibVariant::ibVariant(ibValueTypes type, bool readOnly)
	: m_typeClass(type), m_bReadOnly(readOnly), m_refCount(0), m_dData()
{
	switch (type)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		m_bData = false;
		break;
	case ibValueTypes::TYPE_NUMBER:
		m_fData.SetZero();
		break;
	case ibValueTypes::TYPE_DATE:
		break;      // the zeroed word IS the empty date
	case ibValueTypes::TYPE_STRING:
		break;      // the zeroed word IS the empty string
	default:
		m_pRef = nullptr;
		break;
	}
}

//Constructors by types:
#define CVARIANT_BYTYPE(v_parclass, v_type, v_value) \
ibVariant::ibVariant (v_parclass cParam) \
    : m_typeClass(v_type), m_bReadOnly(false), m_refCount(0), m_dData() \
{\
	v_value = cParam;\
}

CVARIANT_BYTYPE(bool, ibValueTypes::TYPE_BOOLEAN, m_bData);

CVARIANT_BYTYPE(signed int, ibValueTypes::TYPE_NUMBER, m_fData);
CVARIANT_BYTYPE(unsigned int, ibValueTypes::TYPE_NUMBER, m_fData);
CVARIANT_BYTYPE(double, ibValueTypes::TYPE_NUMBER, m_fData);
CVARIANT_BYTYPE(const ibNumber&, ibValueTypes::TYPE_NUMBER, m_fData);

CVARIANT_BYTYPE(const ibDateTime&, ibValueTypes::TYPE_DATE, m_dData);

#undef CVARIANT_BYTYPE

ibVariant::ibVariant(long cParam)
	: m_typeClass(ibValueTypes::TYPE_NUMBER), m_bReadOnly(false), m_refCount(0), m_dData()
{
	m_fData = ibNumber(cParam);
}

// String ctors — the text goes into the union's word as it is (a copy of an ibString is one more owner, not the
// characters); the empty string is the zeroed word itself.
ibVariant::ibVariant(const char* cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_refCount(0), m_dData()
{
	if (cParam && *cParam) m_sData = ibString(cParam);
}

ibVariant::ibVariant(const ibChar* cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_refCount(0), m_dData()
{
	m_sData = ibString(cParam);
}

ibVariant::ibVariant(const std::wstring& cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_refCount(0), m_dData()
{
	m_sData = ibString(cParam.c_str());
}

ibVariant::ibVariant(const ibString& cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_refCount(0), m_dData()
{
	m_sData = cParam;
}

ibVariant::ibVariant(ibString&& cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_refCount(0), m_dData()
{
	m_sData = std::move(cParam);
}

ibVariant::ibVariant(const wxString& cParam)
	: m_typeClass(ibValueTypes::TYPE_STRING), m_bReadOnly(false), m_refCount(0), m_dData()
{
	m_sData = ibString(cParam);
}

ibVariant::~ibVariant()
{
	if (m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef && m_pRef != this)
		m_pRef->DecrRef();
	else if (m_typeClass == ibValueTypes::TYPE_STRING)
		m_sData.~ibString();   // ONLY if STRING — the word aliases m_pRef in the union
	else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		m_fData.~ibNumber();   // a heap-tier number lets go of its BigImpl
}

// The write-denied refusal, OUT OF LINE. Built inside Reset, its message — a translation lookup and three
// wxStrings — gave Reset a 288-byte frame and a stack-cookie check, and Reset runs on every slot of every
// frame a script call releases: the price of a sentence that is almost never said, paid on each of them
// (the disassembly, 2026-09-28). Same shape as the raise helpers in procUnit.cpp.
IB_NOINLINE static void RaiseWriteDenied()
{
	ibCoreException::Error(_("Attempt to assign a value to a write-denied variable"));
}

void ibVariant::Reset()
{
	if (m_typeClass != ibValueTypes::TYPE_EMPTY && m_bReadOnly) RaiseWriteDenied();

	if (m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef)
		m_pRef->DecrRef();
	else if (m_typeClass == ibValueTypes::TYPE_STRING)
		m_sData.~ibString();   // lets go of the text
	else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		m_fData.~ibNumber();   // …and of a heap-tier number's BigImpl
	// TYPE_CONST_REFFER: non-owned — its pointer is just dropped below.

	m_typeClass = ibValueTypes::TYPE_EMPTY;
	m_dData = ibDateTime();   // the WHOLE word: the empty state of every member
}

//methods:
void ibVariant::Copy(const ibVariant& cOld)
{
	if (this == &cOld)
		return;

	// STRING ONTO STRING SHARES THE TEXT, NUMBER ONTO NUMBER assigns over itself — the runtime's.
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
		m_sData = cOld.m_sData;   // the text shared
		break;
	case ibValueTypes::TYPE_DATE:
		m_dData = cOld.m_dData;
		break;
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		// AN OBJECT COPIED IS AN OBJECT HELD: the copy refers to it, counted.
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef = const_cast<ibVariant*>(&cOld);
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_REFFER:
		m_pRef = cOld.m_pRef;
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_CONST_REFFER:
		m_pConstRef = cOld.m_pConstRef;   // weak, non-owning — NO IncrRef
		break;
	default:
		m_typeClass = ibValueTypes::TYPE_EMPTY;
		break;
	}
}

void ibVariant::Move(ibVariant&& cOld)
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
		m_pRef = const_cast<ibVariant*>(&cOld);
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_REFFER:
		m_pRef = cOld.m_pRef;
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_CONST_REFFER:
		m_pConstRef = cOld.m_pConstRef;
		break;
	default:
		m_typeClass = ibValueTypes::TYPE_EMPTY;
		break;
	}

	cOld.Reset();
}

void ibVariant::operator = (bool cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	m_bData = cParam;
}

void ibVariant::operator = (short cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibVariant::operator = (unsigned short cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibVariant::operator = (int cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibVariant::operator = (unsigned int cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibVariant::operator = (long cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = ibNumber(cParam);
}

void ibVariant::operator = (float cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibVariant::operator = (double cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibVariant::operator = (const ibNumber& cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void ibVariant::operator = (const ibDateTime& cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_DATE;
	m_dData = cParam;
}

void ibVariant::operator = (const ibString& cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = cParam;
}

void ibVariant::operator = (const wxString& cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = ibString(cParam);
}

// Character POINTERS assign as strings — without these `value = "text"` picked operator=(bool) (pointer-to-bool is a
// standard conversion) and quietly produced TRUE. The runtime's trap.
void ibVariant::operator = (const char* cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_STRING;
	if (cParam && *cParam) m_sData = ibString(cParam);
}

void ibVariant::operator = (const ibChar* cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = ibString(cParam);
}

void ibVariant::operator = (ibString&& cParam)
{
	Reset();
	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = std::move(cParam);   // take the text over
}

void ibVariant::operator = (const ibVariant& cParam)
{
	if (this != &cParam && !m_bReadOnly)
		Copy(cParam);
}

void ibVariant::operator = (ibVariant&& cParam)
{
	if (this != &cParam && !m_bReadOnly)
		Move(std::move(cParam));
}

void ibVariant::operator = (ibValueTypes type)
{
	ibVariant objValue(*this);

	// The text or the number of the kind the value WAS is let go (objValue holds it for SetData), and the word zeroed.
	if (m_typeClass == ibValueTypes::TYPE_STRING)
		m_sData.~ibString();
	else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		m_fData.~ibNumber();
	m_dData = ibDateTime();

	switch (type)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		m_bData = false;
		break;
	case ibValueTypes::TYPE_NUMBER:
	case ibValueTypes::TYPE_DATE:
	case ibValueTypes::TYPE_STRING:
		break;      // the zeroed word is the empty one of each
	default:
		m_pRef = nullptr;
		break;
	}

	m_typeClass = type;

	SetData(objValue);
}

void ibVariant::operator = (ibVariant* pValue)
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

void ibVariant::operator = (const ibVariant* pValue)
{
	// A const source — a NON-owning, read-only reference (TYPE_CONST_REFFER): never counted, never deleted through.
	if (this != pValue && !m_bReadOnly) {
		Reset();
		if (pValue != nullptr) {
			m_typeClass = ibValueTypes::TYPE_CONST_REFFER;
			m_pConstRef = pValue;
		}
	}
}

void ibVariant::SetValue(const ibVariant& varValue)
{
	if (this == &varValue)
		return;

	if (m_typeClass == ibValueTypes::TYPE_REFFER)
		m_pRef->SetValue(varValue);
	else
		Copy(varValue);
}

bool ibVariant::SetBoolean(const ibString& strBoolean)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->SetBoolean(strBoolean);

	Reset();

	ibBoolean bData;
	bData.FromString(strBoolean);

	m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	m_bData = bData;

	return true;
}

bool ibVariant::SetNumber(const ibString& strNumber)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->SetNumber(strNumber);

	Reset();

	ibNumber fData = 0;
	if (!fData.FromString(strNumber))
		return false;

	m_typeClass = ibValueTypes::TYPE_NUMBER;
	m_fData = fData;

	return true;
}

bool ibVariant::SetDate(const ibString& strDate)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->SetDate(strDate);

	Reset();

	ibDateTime dData;
	if (!strDate.IsEmpty() && !dData.FromString(strDate))
		return false;

	m_typeClass = ibValueTypes::TYPE_DATE;
	m_dData = dData;

	return true;
}

bool ibVariant::SetString(const ibString& strString)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->SetString(strString);

	// Already a string: the new text simply replaces the one held.
	if (!m_bReadOnly && m_typeClass == ibValueTypes::TYPE_STRING) {
		m_sData = strString;
		return true;
	}

	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = strString;

	return true;
}

bool ibVariant::SetString(ibString&& strString)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->SetString(strString);

	if (!m_bReadOnly && m_typeClass == ibValueTypes::TYPE_STRING) {
		m_sData = std::move(strString);
		return true;
	}

	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = std::move(strString);

	return true;
}

void ibVariant::SetData(const ibVariant& varValue)
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
	case ibValueTypes::TYPE_STRING:
		SetString(varValue.GetString());
		return;
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

bool ibVariant::GetBoolean() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData;
	case ibValueTypes::TYPE_NUMBER:
		return !m_fData.IsZero();
	case ibValueTypes::TYPE_STRING: {
		ibBoolean bData;
		bData.FromString(m_sData);
		return bData;
	}
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetBoolean();
	default:
		break;
	}

	return false;
}

ibNumber ibVariant::GetNumber() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return ibNumber(m_bData ? 1 : 0);
	case ibValueTypes::TYPE_NUMBER:
		return m_fData;
	case ibValueTypes::TYPE_STRING: {
		ibString strVal = m_sData;
		strVal.Trim(true);
		strVal.Trim(false);
		strVal.MakeUpper();
		ibNumber number;
		if (!number.FromString(strVal))
			ibCoreException::Error(_("Cannot convert string to number!"));
		return number;
	}
	case ibValueTypes::TYPE_DATE:
		// Seconds from the empty date - and GetDate reads a number back the same way.
		return ibNumber(m_dData.GetValue() / 1000);
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetNumber();
	default:
		break;
	}

	return 0;
}

ibString ibVariant::GetString() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData.ToString();
	case ibValueTypes::TYPE_NUMBER:
		return m_fData.ToString();
	case ibValueTypes::TYPE_STRING:
		return m_sData;   // the text SHARED — one more owner, no copy
	case ibValueTypes::TYPE_DATE:
		return m_dData.ToString();
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef != nullptr ? m_pRef->GetString() : ibString();
	default:
		break;      // Undefined, Null, and an object that names no text of its own
	}

	return ibString();
}

ibDateTime ibVariant::GetDate() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_NUMBER: {
		// Seconds from the empty date, the way GetNumber reads a date.
		long long seconds = 0;
		if (!m_fData.ToInt(seconds))
			return ibDateTime().AddMilliseconds(seconds * 1000);
		return ibDateTime();
	}
	case ibValueTypes::TYPE_STRING: {
		ibDateTime dData;
		dData.FromString(m_sData);   // a text that is no date leaves the empty date
		return dData;
	}
	case ibValueTypes::TYPE_DATE:
		return m_dData;
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetDate();
	default:
		break;
	}

	return ibDateTime();
}

ibVariant* ibVariant::GetRef() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetRef();
	return const_cast<ibVariant*>(this);
}

bool ibVariant::IsEmpty() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData == false;
	case ibValueTypes::TYPE_NUMBER:
		return m_fData.IsZero();
	case ibValueTypes::TYPE_DATE:
		return m_dData.IsEmpty();
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
		break;      // TYPE_EMPTY / TYPE_NULL — empty below
	}

	return true;
}

void ibVariant::SetType(ibValueTypes type)
{
	// A const reference must not retype the non-owned object.
	if (m_typeClass == ibValueTypes::TYPE_CONST_REFFER)
		ibCoreException::Error(_("Attempt to change the type of a read-only (const) object"));
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		m_pRef->SetType(type);
	else if (m_typeClass != type) {
		// A NEW KIND STARTS FROM THE ZEROED WORD: the text or the number of the old one is let go.
		if (m_typeClass == ibValueTypes::TYPE_STRING)
			m_sData.~ibString();
		else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
			m_fData.~ibNumber();
		m_dData = ibDateTime();
		m_typeClass = type;
	}
}

ibValueTypes ibVariant::GetType() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetType();
	return m_typeClass;
}

ibClassID ibVariant::GetClassType() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetClassType();
	if (m_typeClass < ibValueTypes::TYPE_REFFER)
		return GetIDByVT(m_typeClass);
	return 0;   // an object that names no class of its own
}

ibClassID ibVariant::GetIDByVT(const ibValueTypes& valueType)
{
	return ::GetIDByVT(valueType);   // the core's table (core/core.h)
}

ibValueTypes ibVariant::GetVTByID(const ibClassID& clsid)
{
	if (clsid == g_valueUndefinedCLSID)
		return ibValueTypes::TYPE_EMPTY;
	else if (clsid == g_valueBooleanCLSID)
		return ibValueTypes::TYPE_BOOLEAN;
	else if (clsid == g_valueNumberCLSID)
		return ibValueTypes::TYPE_NUMBER;
	else if (clsid == g_valueDateCLSID)
		return ibValueTypes::TYPE_DATE;
	else if (clsid == g_valueStringCLSID)
		return ibValueTypes::TYPE_STRING;
	else if (clsid == g_valueNullCLSID)
		return ibValueTypes::TYPE_NULL;

	// Not a primitive — an object's class.
	return ibValueTypes::TYPE_VALUE;
}

//*************************************************************
//*                    compare support                        *
//*************************************************************

namespace {
template <class T>
inline int CompareOrder(const T& a, const T& b) { return (b < a) - (a < b); }

inline int OrderOfDifference(const int difference) { return (difference > 0) - (difference < 0); }

// A value's text, read IN PLACE when it is a string (or a reference to one).
inline const ibString& AsText(const ibVariant& value, ibString& built)
{
	const ibVariant* held = &value;
	while ((held->m_typeClass == ibValueTypes::TYPE_REFFER || held->m_typeClass == ibValueTypes::TYPE_CONST_REFFER)
		&& held->m_pRef != nullptr)
		held = held->m_pRef;
	if (held->m_typeClass == ibValueTypes::TYPE_STRING)
		return held->m_sData;
	built = held->GetString();
	return built;
}

inline int CompareAsText(const ibVariant& a, const ibVariant& b)
{
	ibString builtA, builtB;
	return AsText(a, builtA).Cmp(AsText(b, builtB));
}

// WHICH STRETCH OF THE ORDER A KIND OCCUPIES — the runtime's (backend/compiler/value.cpp, KindRank).
inline int KindRank(const ibValueTypes type)
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

int ibVariant::CompareValueLS(const ibVariant& cParam) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->CompareValueLS(cParam);

	const ibValueTypes kindThere = cParam.IsReference() ? cParam.GetType() : cParam.m_typeClass;

	const bool aNull = (m_typeClass == ibValueTypes::TYPE_EMPTY || m_typeClass == ibValueTypes::TYPE_NULL);
	const bool bNull = (kindThere == ibValueTypes::TYPE_EMPTY || kindThere == ibValueTypes::TYPE_NULL);
	if (aNull || bNull)
		return aNull == bNull ? 0 : (aNull ? -1 : 1);   // no scalar payload = smallest; both -> equal

	if (m_typeClass != kindThere) {
		const int rankHere = KindRank(m_typeClass);
		const int rankThere = KindRank(kindThere);
		if (rankHere != rankThere)
			return CompareOrder(rankHere, rankThere);
	}

	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return CompareOrder(m_bData,
			cParam.m_typeClass == ibValueTypes::TYPE_BOOLEAN ? cParam.m_bData : ibBoolean(cParam.GetBoolean()));
	case ibValueTypes::TYPE_NUMBER:
		return OrderOfDifference(cParam.m_typeClass == ibValueTypes::TYPE_NUMBER
			? m_fData.Compare(cParam.m_fData)
			: m_fData.Compare(cParam.GetNumber()));
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
	default:                          break;
	}

	return 0;
}

int  ibVariant::CompareValueGT(const ibVariant& cParam) const { return CompareValueLS(cParam); }
bool ibVariant::CompareValueGE(const ibVariant& cParam) const { return CompareValueGT(cParam) >= 0; }
bool ibVariant::CompareValueLE(const ibVariant& cParam) const { return CompareValueLS(cParam) <= 0; }

// compare '==' — type-strict, the tag first.
bool ibVariant::CompareValueEQ(const ibVariant& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return ibValueTypes::TYPE_EMPTY == cParam.GetType();
	case ibValueTypes::TYPE_NULL:
		return ibValueTypes::TYPE_NULL == cParam.GetType();
	case ibValueTypes::TYPE_BOOLEAN:
		if (cParam.GetType() != ibValueTypes::TYPE_BOOLEAN) return false;
		return m_bData == (cParam.m_typeClass == ibValueTypes::TYPE_BOOLEAN ? cParam.m_bData : ibBoolean(cParam.GetBoolean()));
	case ibValueTypes::TYPE_NUMBER:
		if (cParam.GetType() != ibValueTypes::TYPE_NUMBER) return false;
		return 0 == (cParam.m_typeClass == ibValueTypes::TYPE_NUMBER
			? m_fData.Compare(cParam.m_fData)
			: m_fData.Compare(cParam.GetNumber()));
	case ibValueTypes::TYPE_DATE:
		if (cParam.GetType() != ibValueTypes::TYPE_DATE) return false;
		return m_dData == (cParam.m_typeClass == ibValueTypes::TYPE_DATE ? cParam.m_dData : cParam.GetDate());
	case ibValueTypes::TYPE_STRING:
		return cParam.GetType() == ibValueTypes::TYPE_STRING && CompareAsText(*this, cParam) == 0;
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		// An object that does not answer for itself is the same only as itself — or one holding it.
		return this == cParam.GetRef();
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueEQ(cParam);
	case ibValueTypes::TYPE_LAST:
		break;      // sentinel, never a live tag
	}

	return false;
}

// '!=' IS '==' NEGATED, and nothing else — an override of one is an override of both.
bool ibVariant::CompareValueNE(const ibVariant& cParam) const
{
	return !CompareValueEQ(cParam);
}

//*************************************************************
//*                       serialization                       *
//*************************************************************

bool ibVariant::Serialize(ibDataNode& node) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->Serialize(node);

	node.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), (unsigned long long)GetClassType()));
	return DoSerialize(node);
}

bool ibVariant::Deserialize(const ibDataNode& node)
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->Deserialize(node);
	return DoDeserialize(node);
}

bool ibVariant::DoSerialize(ibDataNode& node) const
{
	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:    return true;
	case ibValueTypes::TYPE_BOOLEAN: node.SetValue(kValueFieldData, static_cast<bool>(m_bData)); return true;
	case ibValueTypes::TYPE_NUMBER:  node.SetValue(kValueFieldData, m_fData); return true;
	case ibValueTypes::TYPE_DATE:    node.SetValue(kValueFieldData, m_dData); return true;
	case ibValueTypes::TYPE_STRING:  node.SetValue(kValueFieldData, m_sData); return true;
	default:                         return false;
	}
}

bool ibVariant::DoDeserialize(const ibDataNode& node)
{
	const auto data = node.FindField(kValueFieldData);
	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:    return true;
	case ibValueTypes::TYPE_BOOLEAN: m_bData = data && data->AsBool(); return true;
	case ibValueTypes::TYPE_NUMBER:  m_fData = data ? data->AsNumber() : ibNumber(); return true;
	case ibValueTypes::TYPE_DATE:    m_dData = data ? data->AsDate() : ibDateTime(); return true;
	case ibValueTypes::TYPE_STRING:  m_sData = data ? data->AsString() : ibString(); return true;
	default:                         return false;
	}
}

ibVariant ibVariant::FromNode(const ibDataNode& node)
{
	unsigned long long parsed = 0;
	const ibClassID clsid = node.GetValue<ibString>(kValueFieldClsid).ToULongLong(&parsed) && parsed != 0
		? static_cast<ibClassID>(parsed) : g_valueUndefinedCLSID;
	const auto data = node.FindField(kValueFieldData);

	switch (GetVTByID(clsid)) {
	case ibValueTypes::TYPE_NULL:    return ibVariant(ibValueTypes::TYPE_NULL);
	case ibValueTypes::TYPE_BOOLEAN: return ibVariant(data && data->AsBool());
	case ibValueTypes::TYPE_NUMBER:
		// JSON carries a number as one; text is taken as the number it spells.
		if (data && data->Kind() == ibDataKind::String)
			return ibVariant(ibNumber(data->AsString()));
		return ibVariant(data ? data->AsNumber() : ibNumber());
	case ibValueTypes::TYPE_DATE: {
		// …and a date as text: it says it is a Date, so the text is read as one.
		if (data && data->Kind() == ibDataKind::String) {
			ibDateTime date;
			date.FromString(data->AsString());
			return ibVariant(date);
		}
		return ibVariant(data ? data->AsDate() : ibDateTime());
	}
	case ibValueTypes::TYPE_STRING:  return ibVariant(data ? data->AsString() : ibString());
	default:                         return ibVariant();   // Undefined — or an object, the side's to make
	}
}
