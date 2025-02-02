////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : base value  
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "backend/backend_exception.h"

#include <wx/datetime.h>
#include <wx/longlong.h>

wxIMPLEMENT_DYNAMIC_CLASS(CValue, wxObject);

//**********************************************************************
//*                       Value implementation                         *
//**********************************************************************

#if defined(DEBUG)
//#define DEBUG_VALUE
#endif

#ifdef DEBUG_VALUE
static unsigned int s_nCreateCount = 0;
#define DEBUG_VALUE_CREATE() \
	wxLogDebug("Create %d", s_nCreateCount++);
#else 
#define DEBUG_VALUE_CREATE() 
#endif

//**********************************************************************
BACKEND_API const CValue wxEmptyValue;
//**********************************************************************

CValue::CValue()
	: m_typeClass(eValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	DEBUG_VALUE_CREATE();
}

//конструктор копирования:
CValue::CValue(const CValue& varValue)
	: m_typeClass(eValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	Copy(varValue);
	DEBUG_VALUE_CREATE();
}

CValue::CValue(CValue* pValue)
	: m_typeClass(eValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(pValue), m_bReadOnly(false)
{
	if (m_pRef != nullptr) {
		m_typeClass = eValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}
	DEBUG_VALUE_CREATE();
}

CValue::CValue(IBackendValue* pParam)
	: m_typeClass(eValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(pParam ? pParam->GetImplValueRef() : nullptr), m_bReadOnly(false)
{
	if (m_pRef != nullptr) {
		m_typeClass = eValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}
	DEBUG_VALUE_CREATE();
}

CValue::CValue(const wxDateTime& cParam)
	: m_typeClass(eValueTypes::TYPE_DATE), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	const wxLongLong& llData = cParam.GetValue();
	m_dData = llData.GetValue();
	DEBUG_VALUE_CREATE();
}

CValue::CValue(int nYear, int nMonth, int nDay, unsigned short nHour, unsigned short nMinute, unsigned short nSecond)
	: m_typeClass(eValueTypes::TYPE_DATE), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	wxDateTime dataVal(nDay, (wxDateTime::Month)(nMonth - 1), nYear, nHour, nMinute, nSecond);
	if (dataVal.IsValid()) {
		const wxLongLong& llData = dataVal.GetValue();
		m_dData = llData.GetValue();
	}
	DEBUG_VALUE_CREATE();
}

CValue::CValue(eValueTypes type, bool readOnly)
	: m_typeClass(type), m_refCount(0), m_pRef(nullptr), m_bReadOnly(readOnly)
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
		m_sData.clear();
		break;
	default:
		m_pRef = nullptr;
		break;
	}

	DEBUG_VALUE_CREATE();
}

//Конструкторы по типам:
#define CVALUE_BYTYPE(v_parclass, v_type, v_value) \
CValue::CValue (v_parclass cParam) \
    : m_typeClass(v_type), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false) \
{\
	v_value = cParam;\
	DEBUG_VALUE_CREATE();\
}

#define CVALUE_BYTYPE_MOVE(v_parclass, v_type, v_value) \
CValue::CValue (v_parclass cParam) \
  : m_typeClass(v_type), v_value(std::move(cParam)), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false) \
{\
	DEBUG_VALUE_CREATE();\
}

CVALUE_BYTYPE(bool, eValueTypes::TYPE_BOOLEAN, m_bData);

CVALUE_BYTYPE(signed int, eValueTypes::TYPE_NUMBER, m_fData);
CVALUE_BYTYPE(unsigned int, eValueTypes::TYPE_NUMBER, m_fData);
CVALUE_BYTYPE(double, eValueTypes::TYPE_NUMBER, m_fData);
CVALUE_BYTYPE(const number_t&, eValueTypes::TYPE_NUMBER, m_fData);

CVALUE_BYTYPE(wxLongLong_t, eValueTypes::TYPE_DATE, m_dData);

CVALUE_BYTYPE_MOVE(char*, eValueTypes::TYPE_STRING, m_sData);
CVALUE_BYTYPE_MOVE(const wxString&, eValueTypes::TYPE_STRING, m_sData);

#undef CVALUE_BYTYPE
#undef CVALUE_BYTYPE_MOVE

CValue::~CValue()
{
	if (m_typeClass == eValueTypes::TYPE_REFFER && m_pRef && m_pRef != this)
		m_pRef->DecrRef();
#ifdef DEBUG_VALUE
	wxLogDebug("Delete %d", --s_nCreateCount);
#endif
}

void CValue::DecrRef()
{
	wxASSERT_MSG(m_refCount > 0, "invalid ref data count");
	if (--m_refCount == 0) delete this;
}

void CValue::Reset()
{
	if (m_typeClass != eValueTypes::TYPE_EMPTY && m_bReadOnly) CBackendException::Error("Attempt to assign a value to a write-denied variable");

	if (m_typeClass == eValueTypes::TYPE_REFFER && m_pRef)
		m_pRef->DecrRef();

	m_typeClass = eValueTypes::TYPE_EMPTY;
	m_pRef = nullptr;
}

//methods:
void CValue::Copy(const CValue& cOld)
{
	if (this == &cOld)
		return;

	Reset();

	m_typeClass = cOld.m_typeClass;

	switch (m_typeClass) {
	case eValueTypes::TYPE_NULL:
		break;
	case eValueTypes::TYPE_BOOLEAN:
		m_bData = cOld.m_bData;
		break;
	case eValueTypes::TYPE_NUMBER:
		m_fData = cOld.m_fData;
		break;
	case eValueTypes::TYPE_STRING:
		m_sData = cOld.m_sData;
		break;
	case eValueTypes::TYPE_DATE:
		m_dData = cOld.m_dData;
		break;
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		m_typeClass = eValueTypes::TYPE_REFFER;
		m_pRef = const_cast<CValue*>(&cOld);
		m_pRef->IncrRef();
		break;
	case eValueTypes::TYPE_REFFER:
		m_pRef = cOld.m_pRef;
		m_pRef->IncrRef();
		break;
	default:
		m_typeClass = eValueTypes::TYPE_EMPTY;
		break;
	}
}

void CValue::operator = (bool cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_BOOLEAN;
	m_bData = cParam;
}

void CValue::operator = (short cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void CValue::operator = (unsigned short cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void CValue::operator = (int cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void CValue::operator = (unsigned int cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void CValue::operator = (float cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void CValue::operator = (double cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void CValue::operator = (const number_t& cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = cParam;
}

void CValue::operator = (const wxDateTime& cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_DATE;
	const wxLongLong& llData = cParam.GetValue();
	m_dData = llData.GetValue();
}

void CValue::operator = (wxLongLong_t cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_DATE;
	m_dData = cParam;
}


void CValue::operator = (const wxString& cParam)
{
	Reset();

	m_typeClass = eValueTypes::TYPE_STRING;
	m_sData = cParam;
}

void CValue::operator = (const CValue& cParam)
{
	if (this != &cParam && !m_bReadOnly)
		Copy(cParam);
}

void CValue::operator = (eValueTypes type)
{
	eValueTypes typeClass = m_typeClass; CValue objValue(*this);

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
		m_sData.clear();
		break;
	default:
		m_pRef = nullptr;
		break;
	}

	m_typeClass = type;

	SetData(objValue);
}

void CValue::operator=(IBackendValue* pValue)
{
	if (this != (pValue ? pValue->GetImplValueRef() : nullptr) && !m_bReadOnly) {
		Reset();
		if (pValue != nullptr) {
			m_typeClass = eValueTypes::TYPE_REFFER;
			m_pRef = pValue->GetImplValueRef();
			m_pRef->IncrRef();
		}
	}
}

void CValue::operator = (CValue* pValue)
{
	if (this != pValue && !m_bReadOnly) {
		Reset();
		if (pValue != nullptr) {
			m_typeClass = eValueTypes::TYPE_REFFER;
			m_pRef = pValue;
			m_pRef->IncrRef();
		}
	}
}

void CValue::SetValue(const CValue& varValue)
{
	if (this == &varValue)
		return;

	if (m_typeClass == eValueTypes::TYPE_REFFER)
		m_pRef->SetValue(varValue);
	else
		Copy(varValue);
}

bool CValue::SetBoolean(const wxString& strBoolean)
{
	if (m_bReadOnly && m_typeClass == eValueTypes::TYPE_REFFER) {
		return m_pRef->SetBoolean(strBoolean);
	}

	Reset();

	m_typeClass = eValueTypes::TYPE_BOOLEAN;
	m_bData = stringUtils::CompareString(strBoolean, wxT("true"));

	return true;
}

bool CValue::SetNumber(const wxString& strNumber)
{
	if (m_bReadOnly && m_typeClass == eValueTypes::TYPE_REFFER) {
		return m_pRef->SetNumber(strNumber);
	}

	Reset();

	number_t fData = 0;
	unsigned int nSuccessful = fData.FromString(strNumber.ToStdWstring());
	if (nSuccessful > 0)
		return false;

	m_typeClass = eValueTypes::TYPE_NUMBER;
	m_fData = fData;

	return true;
}

bool CValue::SetDate(const wxString& strDate)
{
	if (m_bReadOnly && m_typeClass == eValueTypes::TYPE_REFFER) {
		return m_pRef->SetDate(strDate);
	}

	Reset();

	wxDateTime strTime; wxLongLong_t dData = emptyDate;
	if (!strDate.IsEmpty()) {
		if (strTime.ParseFormat(strDate, "%d.%m.%Y %H:%M:%S")) {
			const wxLongLong& llData = strTime.GetValue();
			dData = llData.GetValue();
		}
		else if (strTime.ParseFormat(strDate, "%Y%m%d%H%M%S")) {
			const wxLongLong& llData = strTime.GetValue();
			dData = llData.GetValue();
		}
		else if (strTime.ParseFormat(strDate, "%Y%m%d")) {
			const wxLongLong& llData = strTime.GetValue();
			dData = llData.GetValue();
		}
		else if (strTime.ParseDateTime(strDate)) {
			const wxLongLong& llData = strTime.GetValue();
			dData = llData.GetValue();
		}
		else {
			return false;
		}
	}

	m_typeClass = eValueTypes::TYPE_DATE;
	m_dData = dData;

	return true;
}

bool CValue::SetString(const wxString& strString)
{
	if (m_bReadOnly && m_typeClass == eValueTypes::TYPE_REFFER) {
		return m_pRef->SetString(strString);
	}

	Reset();

	m_typeClass = eValueTypes::TYPE_STRING;
	m_sData = strString;

	return true;
}

bool CValue::FindValue(const wxString& findData, std::vector<CValue>& foundedObjects) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->FindValue(findData, foundedObjects);

	try {
		if (m_typeClass == eValueTypes::TYPE_BOOLEAN) {
			CValue cFounded;
			cFounded.SetBoolean(findData);
			foundedObjects.emplace_back(cFounded);
			if (cFounded.GetBoolean()) foundedObjects.emplace_back(false);
			else foundedObjects.emplace_back(true);
			return true;
		}
		else if (m_typeClass == eValueTypes::TYPE_NUMBER) {
			foundedObjects.emplace_back().SetNumber(findData);
			return true;
		}
		else if (m_typeClass == eValueTypes::TYPE_DATE) {
			foundedObjects.emplace_back().SetDate(findData);
			return true;
		}
		else if (m_typeClass == eValueTypes::TYPE_STRING) {
			foundedObjects.emplace_back().SetString(findData);
			return true;
		}
	}
	catch (...) {
		return false;
	}

	return false;
}

void CValue::SetData(const CValue& varValue)
{
	if (this == &varValue)
		return;

	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		SetBoolean(varValue.GetString());
		return;
	case eValueTypes::TYPE_NUMBER:
		SetNumber(varValue.GetString());
		return;
	case eValueTypes::TYPE_STRING:
		SetString(varValue.GetString());
		return;
	case eValueTypes::TYPE_DATE:
		SetDate(varValue.GetString());
		return;
	case eValueTypes::TYPE_REFFER:
		if (m_pRef != nullptr)
			m_pRef->SetData(varValue);
		return;
	}

	SetValue(varValue);
}

bool CValue::GetBoolean() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		return m_bData;
	case eValueTypes::TYPE_NUMBER:
		return !m_fData.IsZero();
	case eValueTypes::TYPE_STRING:
		return stringUtils::CompareString(wxT("true"), stringUtils::TrimAll(GetString()));
	case eValueTypes::TYPE_DATE:
		return false;
	case eValueTypes::TYPE_REFFER:
		return m_pRef->GetBoolean();
	}

	return false;
}

number_t CValue::GetNumber() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		return m_bData;
	case eValueTypes::TYPE_NUMBER:
		return m_fData;
	case eValueTypes::TYPE_STRING: {
		wxString strVal = GetString();
		strVal.Trim(true);
		strVal.Trim(false);
		strVal.MakeUpper();
		number_t number;
		unsigned int nSuccessful = number.FromString(strVal.ToStdWstring());
		if (nSuccessful > 0) CBackendException::Error("Cannot convert string to number!");
		return number;
	}
	case eValueTypes::TYPE_DATE:
		return m_dData / 1000;
	case eValueTypes::TYPE_REFFER:
		return m_pRef->GetNumber();
	}

	return 0;
}

wxString CValue::GetString() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY:
		return wxEmptyString;
	case eValueTypes::TYPE_NULL:
		return wxEmptyString;
	case eValueTypes::TYPE_BOOLEAN:
		return m_bData ? wxT("true") : wxT("false");
	case eValueTypes::TYPE_NUMBER:
		return m_fData.ToString();
	case eValueTypes::TYPE_STRING:
		return m_sData;
	case eValueTypes::TYPE_DATE: {
		const wxDateTime& dateTime = wxLongLong(m_dData);
		return dateTime.Format("%d.%m.%Y %H:%M:%S");
	}
	case eValueTypes::TYPE_REFFER:
		return m_pRef ? m_pRef->GetString() : wxEmptyString;
	};

	return GetClassName();
}

wxLongLong_t CValue::GetDate() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		return emptyDate;
	case eValueTypes::TYPE_NUMBER: {
		wxLongLong_t dTemp = 0;
		if (!m_fData.ToInt(dTemp))
			return dTemp * 1000;
		return emptyDate;
	}
	case eValueTypes::TYPE_STRING: {
		wxDateTime dateTime;
		if (dateTime.ParseFormat(m_sData, "%d.%m.%Y %H:%M:%S")) {
			const wxLongLong& llData = dateTime.GetValue();
			return llData.GetValue();
		}
		else if (dateTime.ParseFormat(m_sData, "%Y%m%d%H%M%S")) {
			const wxLongLong& llData = dateTime.GetValue();
			return llData.GetValue();
		}
		else if (dateTime.ParseDateTime(m_sData)) {
			const wxLongLong& llData = dateTime.GetValue();
			return llData.GetValue();
		}
		return emptyDate;
	}
	case eValueTypes::TYPE_DATE:
		return m_dData;
	case eValueTypes::TYPE_REFFER:
		return m_pRef->GetDate();
	};

	return emptyDate;
}

CValue* CValue::GetRef() const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetRef();
	return const_cast<CValue*>(this);
}

void CValue::ShowValue()
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->ShowValue();
}

void CValue::FromDate(int& nYear, int& nMonth, int& nDay) const
{
	const wxLongLong& llData = wxLongLong(GetDate());
	wxDateTime dateTime(llData);

	nYear = dateTime.GetYear();
	nMonth = dateTime.GetMonth() + 1;
	nDay = dateTime.GetDay();
}

void CValue::FromDate(int& nYear, int& nMonth, int& nDay, unsigned short& nHour, unsigned short& nMinute, unsigned short& nSecond) const
{
	const wxLongLong& llData = wxLongLong(GetDate());
	wxDateTime dateTime(llData);

	nYear = dateTime.GetYear();
	nMonth = dateTime.GetMonth() + 1;
	nDay = dateTime.GetDay();
	nHour = dateTime.GetHour();
	nMinute = dateTime.GetMinute();
	nSecond = dateTime.GetSecond();
}

void CValue::FromDate(int& nYear, int& nMonth, int& nDay, int& DayOfWeek, int& DayOfYear, int& WeekOfYear) const
{
	const wxLongLong& llData = wxLongLong(GetDate());
	wxDateTime dateTime(llData);

	nYear = dateTime.GetYear();
	nMonth = dateTime.GetMonth() - 1;
	nDay = dateTime.GetDay();

	WeekOfYear = DayOfWeek = DayOfYear = 0;

	wxDateTime partDateTime(nDay, (wxDateTime::Month)nMonth, nYear);
	DayOfYear = partDateTime.GetDayOfYear();
	DayOfWeek = partDateTime.GetWeekDay() - 1;

	if (DayOfWeek < 1)
		DayOfWeek = 7;

	WeekOfYear = 1 + (DayOfYear - 1) / 7;

	int nD = (1 + (DayOfYear - 1) % 7);
	if (nD > DayOfWeek) WeekOfYear++;
}

bool CValue::IsEmpty() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		return m_bData == false;
	case eValueTypes::TYPE_NUMBER:
		return m_fData.IsZero();
	case eValueTypes::TYPE_DATE:
		return m_dData == emptyDate;
	case eValueTypes::TYPE_STRING:
		return m_sData.IsEmpty();
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		return false;
	case eValueTypes::TYPE_REFFER:
		return m_pRef ? m_pRef->IsEmpty() : true;
	};

	return true;
}

void CValue::SetType(eValueTypes type)
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		m_pRef->SetType(type);
	else
		m_typeClass = type;
}

eValueTypes CValue::GetType() const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetType();
	return m_typeClass;
}

//*************************************************************

wxString CValue::GetClassName() const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetClassName();
	const class_identifier_t& clsid = GetClassType();
	if (clsid == 0) CBackendException::Error("Not founded in wxClassInfo!");
	return CValue::GetNameObjectFromID(clsid);
}

class_identifier_t CValue::GetClassType() const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetClassType();
	if (m_typeClass < eValueTypes::TYPE_REFFER)
		return CValue::GetIDByVT(m_typeClass);
	return CValue::GetTypeIDByRef(this);
}

//*************************************************************
//*                        array support                      *
//*************************************************************

bool CValue::SetAt(const CValue& varKeyValue, const CValue& varValue)
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->SetAt(varKeyValue, varValue);
	const long lPropNum = FindProp(varKeyValue.GetString());
	if (lPropNum != wxNOT_FOUND)
		return SetPropVal(lPropNum, varValue);
	return false;
}

bool CValue::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetAt(varKeyValue, pvarValue);
	const long lPropNum = FindProp(varKeyValue.GetString());
	if (lPropNum != wxNOT_FOUND)
		return GetPropVal(lPropNum, pvarValue);
	return false;
}

//*************************************************************
//*                    iterator support                       *
//*************************************************************

CValue CValue::GetIteratorEmpty()
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetIteratorEmpty();
	return CValue();
}

bool CValue::HasIterator() const
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->HasIterator();
	return false;
}

CValue CValue::GetIteratorAt(unsigned int idx)
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetIteratorAt(idx);
	return CValue();
}

unsigned int CValue::GetIteratorCount() const
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetIteratorCount();
	return 0;
}

//*************************************************************
//*                    compare support                        *
//*************************************************************

// compare '>'
bool CValue::CompareValueGT(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY:
		return false;
	case eValueTypes::TYPE_NULL:
		return false;
	case eValueTypes::TYPE_BOOLEAN:
		return GetBoolean() > cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER:
		return GetNumber() > cParam.GetNumber();
	case eValueTypes::TYPE_DATE:
		return GetDate() > cParam.GetDate();
	case eValueTypes::TYPE_STRING:
		return GetString() > cParam.GetString();
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		return GetString() > cParam.GetString();
	case eValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueGT(cParam);
	};

	return false;
}

// compare '>='
bool CValue::CompareValueGE(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY:
		return false;
	case eValueTypes::TYPE_NULL:
		return false;
	case eValueTypes::TYPE_BOOLEAN:
		return GetBoolean() >= cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER:
		return GetNumber() >= cParam.GetNumber();
	case eValueTypes::TYPE_DATE:
		return GetDate() >= cParam.GetDate();
	case eValueTypes::TYPE_STRING:
		return GetString() >= cParam.GetString();
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		return GetString() >= cParam.GetString();
	case eValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueGE(cParam);
	};

	return false;
}

// compare '<'
bool CValue::CompareValueLS(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY:
		return false;
	case eValueTypes::TYPE_NULL:
		return false;
	case eValueTypes::TYPE_BOOLEAN:
		return GetBoolean() < cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER:
		return GetNumber() < cParam.GetNumber();
	case eValueTypes::TYPE_DATE:
		return GetDate() < cParam.GetDate();
	case eValueTypes::TYPE_STRING:
		return GetString() < cParam.GetString();
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		return GetString() < cParam.GetString();
	case eValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueLS(cParam);
	};

	return false;
}

// compare '<='
bool CValue::CompareValueLE(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY:
		return false;
	case eValueTypes::TYPE_NULL:
		return false;
	case eValueTypes::TYPE_BOOLEAN:
		return GetBoolean() <= cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER:
		return GetNumber() <= cParam.GetNumber();
	case eValueTypes::TYPE_DATE:
		return GetDate() <= cParam.GetDate();
	case eValueTypes::TYPE_STRING:
		return GetString() <= cParam.GetString();
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		return GetString() <= cParam.GetString();
	case eValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueLE(cParam);
	};

	return false;
}

// compare '=='
bool CValue::CompareValueEQ(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY:
		return eValueTypes::TYPE_EMPTY == cParam.GetType();
	case eValueTypes::TYPE_NULL:
		return eValueTypes::TYPE_NULL == cParam.GetType();
	case eValueTypes::TYPE_BOOLEAN:
		return GetBoolean() == cParam.GetBoolean() &&
			eValueTypes::TYPE_BOOLEAN == cParam.GetType();
	case eValueTypes::TYPE_NUMBER:
		return GetNumber() == cParam.GetNumber() &&
			eValueTypes::TYPE_NUMBER == cParam.GetType();
	case eValueTypes::TYPE_DATE:
		return GetDate() == cParam.GetDate() &&
			eValueTypes::TYPE_DATE == cParam.GetType();
	case eValueTypes::TYPE_STRING:
		return GetString() == cParam.GetString() &&
			eValueTypes::TYPE_STRING == cParam.GetType();
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		return GetString() == cParam.GetString() &&
			GetClassType() == cParam.GetClassType();
	case eValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueEQ(cParam);
	};

	return false;
}

// compare '!='
bool CValue::CompareValueNE(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY:
		return eValueTypes::TYPE_EMPTY != cParam.GetType();
	case eValueTypes::TYPE_NULL:
		return eValueTypes::TYPE_NULL != cParam.GetType();
	case eValueTypes::TYPE_BOOLEAN:
		return eValueTypes::TYPE_BOOLEAN != cParam.GetType() ||
			GetBoolean() != cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER:
		return eValueTypes::TYPE_NUMBER != cParam.GetType() ||
			GetNumber() != cParam.GetNumber();
	case eValueTypes::TYPE_DATE:
		return eValueTypes::TYPE_DATE != cParam.GetType() ||
			GetDate() != cParam.GetDate();
	case eValueTypes::TYPE_STRING:
		return eValueTypes::TYPE_STRING != cParam.GetType() ||
			GetString() != cParam.GetString();
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE:
		return GetString() != cParam.GetString() ||
			GetClassType() != cParam.GetClassType();
	case eValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueNE(cParam);
	};

	return false;
}

const CValue& CValue::operator+(const CValue& cParam)
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_NUMBER:
		m_fData = m_fData + cParam.GetNumber();
		break;
	case eValueTypes::TYPE_DATE:
		m_dData = m_dData + cParam.GetDate();
		break;
	}

	return *this;
}

const CValue& CValue::operator-(const CValue& cParam)
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_NUMBER:
		m_fData = m_fData - cParam.GetNumber();
		break;
	case eValueTypes::TYPE_DATE:
		m_dData = m_dData - cParam.GetDate();
		break;
	}
	return *this;
}

//*************************************************************
//           РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА                   *
//*************************************************************

void CValue::PrepareNames() const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		m_pRef->PrepareNames();
}

long CValue::GetNProps() const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetNProps();
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNProps();
	return 0;
}

long CValue::FindProp(const wxString& strPropName) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->FindProp(strPropName);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->FindProp(strPropName);
	return wxNOT_FOUND;
}

wxString CValue::GetPropName(const long lPropNum) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetPropName(lPropNum);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetPropName(lPropNum);
	return wxEmptyString;
}

bool CValue::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetPropVal(lPropNum, pvarPropVal);
	return false;
}

bool CValue::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->SetPropVal(lPropNum, varPropVal);
	return false;
}

bool CValue::IsPropReadable(const long lPropNum) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->IsPropReadable(lPropNum);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropReadable(lPropNum);
	return true;
}

bool CValue::IsPropWritable(const long lPropNum) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->IsPropWritable(lPropNum);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropWritable(lPropNum);
	return true;
}

long CValue::GetNMethods() const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetNMethods();
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNMethods();
	return 0;
}

long CValue::FindMethod(const wxString& strMethodName) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->FindMethod(strMethodName);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->FindMethod(strMethodName);
	return wxNOT_FOUND;
}

wxString CValue::GetMethodName(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetMethodName(lMethodNum);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodName(lMethodNum);
	return wxEmptyString;
}

wxString CValue::GetMethodHelper(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetMethodHelper(lMethodNum);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodHelper(lMethodNum);
	return wxEmptyString;
}

long CValue::GetNParams(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetNParams(lMethodNum);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNParams(lMethodNum);
	return 0;
}

bool CValue::GetParamDefValue(const long lMethodNum,
	const long lParamNum,
	CValue& pvarParamDefValue) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
	return false;
}

bool CValue::HasRetVal(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->HasRetVal(lMethodNum);
	CMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->HasRetVal(lMethodNum);
	return false;
}

bool CValue::CallAsProc(const long lMethodNum,
	CValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->CallAsProc(lMethodNum, paParams, lSizeArray);
	return false;
}

bool CValue::CallAsFunc(const long lMethodNum,
	CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}

//получить текущее значение (актуального для агрегатных объектов или объектов диалога)
CValue CValue::GetValue(bool getThis)
{
	if (getThis)
		return this;
	if (m_pRef != nullptr && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetValue(true); // true - признак создания новой переменной - ссылки на агрегатный объект
	return *this;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

PRIMITIVE_TYPE_REGISTER(CValue, "undefined", eValueTypes::TYPE_EMPTY, string_to_clsid("VL_UNDF"));

PRIMITIVE_TYPE_REGISTER(CValue, "boolean", eValueTypes::TYPE_BOOLEAN, string_to_clsid("VL_BOOL"));
PRIMITIVE_TYPE_REGISTER(CValue, "number", eValueTypes::TYPE_NUMBER, string_to_clsid("VL_NUMB"));
PRIMITIVE_TYPE_REGISTER(CValue, "date", eValueTypes::TYPE_DATE, string_to_clsid("VL_DATE"));
PRIMITIVE_TYPE_REGISTER(CValue, "string", eValueTypes::TYPE_STRING, string_to_clsid("VL_STRI"));

PRIMITIVE_TYPE_REGISTER(CValue, "null", eValueTypes::TYPE_NULL, string_to_clsid("VL_NULL"));