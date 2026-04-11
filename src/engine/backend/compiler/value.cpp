////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : base value  
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "backend/backend_exception.h"

#include <wx/datetime.h>
#include <wx/longlong.h>

wxIMPLEMENT_DYNAMIC_CLASS(ibValue, wxObject);

//**********************************************************************
//*                       Value implementation                         *
//**********************************************************************

#if defined(DEBUG)
#define DEBUG_VALUE
#endif

#ifdef DEBUG_VALUE
static unsigned int s_nCreateCount = 0;
#define DEBUG_VALUE_CREATE() \
	if (wxTheApp != NULL) wxLogDebug(wxT("Create %d"), s_nCreateCount++); 
#else 
#define DEBUG_VALUE_CREATE() 
#endif

//**********************************************************************
BACKEND_API const ibValue wxEmptyValue;
//**********************************************************************

ibValue::ibValue()
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	DEBUG_VALUE_CREATE();
}

//copy constructor:
ibValue::ibValue(const ibValue& varValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	Copy(varValue);
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValue&& varValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	Move(std::move(varValue));
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValue* pValue)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(pValue), m_bReadOnly(false)
{
	if (m_pRef != nullptr) {
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibBackendValue* pParam)
	: m_typeClass(ibValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(pParam ? pParam->GetImplValueRef() : nullptr), m_bReadOnly(false)
{
	if (m_pRef != nullptr) {
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(const wxDateTime& cParam)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	const wxLongLong& llData = cParam.GetValue();
	m_dData = llData.GetValue();
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(int nYear, int nMonth, int nDay, unsigned short nHour, unsigned short nMinute, unsigned short nSecond)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false)
{
	wxDateTime dataVal(nDay, (wxDateTime::Month)(nMonth - 1), nYear, nHour, nMinute, nSecond);
	if (dataVal.IsValid()) {
		const wxLongLong& llData = dataVal.GetValue();
		m_dData = llData.GetValue();
	}
	DEBUG_VALUE_CREATE();
}

ibValue::ibValue(ibValueTypes type, bool readOnly)
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
		m_sData.Clear();
		break;
	default:
		m_pRef = nullptr;
		break;
	}

	DEBUG_VALUE_CREATE();
}

//Constructors by types:
#define CVALUE_BYTYPE(v_parclass, v_type, v_value) \
ibValue::ibValue (v_parclass cParam) \
    : m_typeClass(v_type), m_refCount(0), m_pRef(nullptr), m_bReadOnly(false) \
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

CVALUE_BYTYPE(char*, ibValueTypes::TYPE_STRING, m_sData);
CVALUE_BYTYPE(wchar_t*, ibValueTypes::TYPE_STRING, m_sData);

CVALUE_BYTYPE(const wxString&, ibValueTypes::TYPE_STRING, m_sData);

#undef CVALUE_BYTYPE
#undef CVALUE_BYTYPE_MOVE

ibValue::~ibValue()
{
	if (m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef && m_pRef != this)
		m_pRef->DecrRef();
#ifdef DEBUG_VALUE
	if (wxTheApp != NULL) wxLogDebug(wxT("Delete %d"), --s_nCreateCount);
#endif
}

void ibValue::Reset()
{
	if (m_typeClass != ibValueTypes::TYPE_EMPTY && m_bReadOnly) ibBackendCoreException::Error(_("Attempt to assign a value to a write-denied variable"));

	if (m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef)
		m_pRef->DecrRef();

	m_typeClass = ibValueTypes::TYPE_EMPTY;
	m_pRef = nullptr;
}

//methods:
void ibValue::Copy(const ibValue& cOld)
{
	if (this == &cOld)
		return;

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
		m_sData = cOld.m_sData;
		break;
	case ibValueTypes::TYPE_DATE:
		m_dData = cOld.m_dData;
		break;
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef = const_cast<ibValue*>(&cOld);
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_REFFER:
		m_pRef = cOld.m_pRef;
		m_pRef->IncrRef();
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
		m_sData = std::move(cOld.m_sData);
		break;
	case ibValueTypes::TYPE_DATE:
		m_dData = std::move(cOld.m_dData);
		break;
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		m_typeClass = ibValueTypes::TYPE_REFFER;
		m_pRef = const_cast<ibValue*>(&cOld);
		m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_REFFER:
		m_pRef = cOld.m_pRef;
		m_pRef->IncrRef();
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
	const wxLongLong& llData = cParam.GetValue();
	m_dData = llData.GetValue();
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
	m_sData = cParam;
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
	ibValueTypes typeClass = m_typeClass; ibValue objValue(*this);

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
	unsigned int nSuccessful = fData.FromString(strNumber.ToStdWstring());
	if (nSuccessful > 0)
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

	m_typeClass = ibValueTypes::TYPE_DATE;
	m_dData = dData;

	return true;
}

bool ibValue::SetString(const wxString& strString)
{
	if (m_bReadOnly && m_typeClass == ibValueTypes::TYPE_REFFER) {
		return m_pRef->SetString(strString);
	}

	Reset();

	m_typeClass = ibValueTypes::TYPE_STRING;
	m_sData = strString;

	return true;
}

bool ibValue::FindValue(const wxString& findData, std::vector<ibValue>& listValue) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
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
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetBoolean();
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
		unsigned int nSuccessful = number.FromString(strVal.ToStdWstring());
		if (nSuccessful > 0) ibBackendCoreException::Error(_("Cannot convert string to number!"));
		return number;
	}
	case ibValueTypes::TYPE_DATE:
		return m_dData / 1000;
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetNumber();
	}

	return 0;
}

wxString ibValue::GetString() const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return wxEmptyString;
	case ibValueTypes::TYPE_NULL:
		return wxEmptyString;
	case ibValueTypes::TYPE_BOOLEAN:
		return m_bData ? wxT("True") : wxT("False");
	case ibValueTypes::TYPE_NUMBER:
		return m_fData.ToString();
	case ibValueTypes::TYPE_STRING:
		return m_sData;
	case ibValueTypes::TYPE_DATE: {
		const wxDateTime& dateTime = wxLongLong(m_dData);
		return dateTime.Format("%d.%m.%Y %H:%M:%S");
	}
	case ibValueTypes::TYPE_REFFER:
		return m_pRef ? m_pRef->GetString() : wxString(wxEmptyString);
	};

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
	case ibValueTypes::TYPE_DATE:
		return m_dData;
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->GetDate();
	};

	return emptyDate;
}

ibValue* ibValue::GetRef() const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetRef();
	return const_cast<ibValue*>(this);
}

void ibValue::ShowValue()
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->ShowValue();
}

void ibValue::FromDate(int& nYear, int& nMonth, int& nDay) const
{
	const wxLongLong& llData = wxLongLong(GetDate());
	wxDateTime dateTime(llData);

	nYear = dateTime.GetYear();
	nMonth = dateTime.GetMonth() + 1;
	nDay = dateTime.GetDay();
}

void ibValue::FromDate(int& nYear, int& nMonth, int& nDay, unsigned short& nHour, unsigned short& nMinute, unsigned short& nSecond) const
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

void ibValue::FromDate(int& nYear, int& nMonth, int& nDay, int& DayOfWeek, int& DayOfYear, int& WeekOfYear) const
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
		return false;
	case ibValueTypes::TYPE_REFFER:
		return m_pRef ? m_pRef->IsEmpty() : true;
	};

	return true;
}

void ibValue::SetType(ibValueTypes type)
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		m_pRef->SetType(type);
	else
		m_typeClass = type;
}

ibValueTypes ibValue::GetType() const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetType();
	return m_typeClass;
}

//*************************************************************

wxString ibValue::GetClassName() const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetClassName();
	const ibClassID& clsid = GetClassType();
	if (clsid == 0) ibBackendCoreException::Error(_("Class not registered"));
	return ibValue::GetNameObjectFromID(clsid);
}

ibClassID ibValue::GetClassType() const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
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
	if (m_pRef && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetAt(varKeyValue, pvarValue);
	const long lPropNum = FindProp(varKeyValue.GetString());
	if (lPropNum != wxNOT_FOUND)
		return GetPropVal(lPropNum, pvarValue);
	return false;
}

//*************************************************************
//*                    iterator support                       *
//*************************************************************

ibValue ibValue::GetIteratorEmpty()
{
	if (m_pRef && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetIteratorEmpty();
	return ibValue();
}

bool ibValue::HasIterator() const
{
	if (m_pRef && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->HasIterator();
	return false;
}

ibValue ibValue::GetIteratorAt(unsigned int idx)
{
	if (m_pRef && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetIteratorAt(idx);
	return ibValue();
}

unsigned int ibValue::GetIteratorCount() const
{
	if (m_pRef && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetIteratorCount();
	return 0;
}

//*************************************************************
//*                    compare support                        *
//*************************************************************

// compare '>'
bool ibValue::CompareValueGT(const ibValue& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return false;
	case ibValueTypes::TYPE_NULL:
		return false;
	case ibValueTypes::TYPE_BOOLEAN:
		return GetBoolean() > cParam.GetBoolean();
	case ibValueTypes::TYPE_NUMBER:
		return GetNumber() > cParam.GetNumber();
	case ibValueTypes::TYPE_DATE:
		return GetDate() > cParam.GetDate();
	case ibValueTypes::TYPE_STRING:
		return GetString() > cParam.GetString();
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		return GetString() > cParam.GetString();
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueGT(cParam);
	};

	return false;
}

// compare '>='
bool ibValue::CompareValueGE(const ibValue& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return false;
	case ibValueTypes::TYPE_NULL:
		return false;
	case ibValueTypes::TYPE_BOOLEAN:
		return GetBoolean() >= cParam.GetBoolean();
	case ibValueTypes::TYPE_NUMBER:
		return GetNumber() >= cParam.GetNumber();
	case ibValueTypes::TYPE_DATE:
		return GetDate() >= cParam.GetDate();
	case ibValueTypes::TYPE_STRING:
		return GetString() >= cParam.GetString();
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		return GetString() >= cParam.GetString();
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueGE(cParam);
	};

	return false;
}

// compare '<'
bool ibValue::CompareValueLS(const ibValue& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return false;
	case ibValueTypes::TYPE_NULL:
		return false;
	case ibValueTypes::TYPE_BOOLEAN:
		return GetBoolean() < cParam.GetBoolean();
	case ibValueTypes::TYPE_NUMBER:
		return GetNumber() < cParam.GetNumber();
	case ibValueTypes::TYPE_DATE:
		return GetDate() < cParam.GetDate();
	case ibValueTypes::TYPE_STRING:
		return GetString() < cParam.GetString();
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		return GetString() < cParam.GetString();
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueLS(cParam);
	};

	return false;
}

// compare '<='
bool ibValue::CompareValueLE(const ibValue& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return false;
	case ibValueTypes::TYPE_NULL:
		return false;
	case ibValueTypes::TYPE_BOOLEAN:
		return GetBoolean() <= cParam.GetBoolean();
	case ibValueTypes::TYPE_NUMBER:
		return GetNumber() <= cParam.GetNumber();
	case ibValueTypes::TYPE_DATE:
		return GetDate() <= cParam.GetDate();
	case ibValueTypes::TYPE_STRING:
		return GetString() <= cParam.GetString();
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		return GetString() <= cParam.GetString();
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueLE(cParam);
	};

	return false;
}

// compare '=='
bool ibValue::CompareValueEQ(const ibValue& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return ibValueTypes::TYPE_EMPTY == cParam.GetType();
	case ibValueTypes::TYPE_NULL:
		return ibValueTypes::TYPE_NULL == cParam.GetType();
	case ibValueTypes::TYPE_BOOLEAN:
		return GetBoolean() == cParam.GetBoolean() &&
			ibValueTypes::TYPE_BOOLEAN == cParam.GetType();
	case ibValueTypes::TYPE_NUMBER:
		return GetNumber() == cParam.GetNumber() &&
			ibValueTypes::TYPE_NUMBER == cParam.GetType();
	case ibValueTypes::TYPE_DATE:
		return GetDate() == cParam.GetDate() &&
			ibValueTypes::TYPE_DATE == cParam.GetType();
	case ibValueTypes::TYPE_STRING:
		return GetString() == cParam.GetString() &&
			ibValueTypes::TYPE_STRING == cParam.GetType();
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		return GetString() == cParam.GetString() &&
			GetClassType() == cParam.GetClassType();
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueEQ(cParam);
	};

	return false;
}

// compare '!='
bool ibValue::CompareValueNE(const ibValue& cParam) const
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_EMPTY:
		return ibValueTypes::TYPE_EMPTY != cParam.GetType();
	case ibValueTypes::TYPE_NULL:
		return ibValueTypes::TYPE_NULL != cParam.GetType();
	case ibValueTypes::TYPE_BOOLEAN:
		return ibValueTypes::TYPE_BOOLEAN != cParam.GetType() ||
			GetBoolean() != cParam.GetBoolean();
	case ibValueTypes::TYPE_NUMBER:
		return ibValueTypes::TYPE_NUMBER != cParam.GetType() ||
			GetNumber() != cParam.GetNumber();
	case ibValueTypes::TYPE_DATE:
		return ibValueTypes::TYPE_DATE != cParam.GetType() ||
			GetDate() != cParam.GetDate();
	case ibValueTypes::TYPE_STRING:
		return ibValueTypes::TYPE_STRING != cParam.GetType() ||
			GetString() != cParam.GetString();
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_VALUE:
		return GetString() != cParam.GetString() ||
			GetClassType() != cParam.GetClassType();
	case ibValueTypes::TYPE_REFFER:
		return m_pRef->CompareValueNE(cParam);
	};

	return false;
}

const ibValue& ibValue::operator+(const ibValue& cParam)
{
	switch (m_typeClass)
	{
	case ibValueTypes::TYPE_NUMBER:
		m_fData = m_fData + cParam.GetNumber();
		break;
	case ibValueTypes::TYPE_DATE:
		m_dData = m_dData + cParam.GetDate();
		break;
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
		m_dData = m_dData - cParam.GetDate();
		break;
	}
	return *this;
}

//*************************************************************
//				WORK AS AN AGGREGATE OBJECT                   *
//*************************************************************

long ibValue::GetNProps() const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetNProps();
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNProps();
	return 0;
}

long ibValue::FindProp(const wxString& strPropName) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->FindProp(strPropName);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->FindProp(strPropName);
	return wxNOT_FOUND;
}

wxString ibValue::GetPropName(const long lPropNum) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetPropName(lPropNum);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetPropName(lPropNum);
	return wxEmptyString;
}

bool ibValue::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetPropVal(lPropNum, pvarPropVal);
	return false;
}

bool ibValue::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->SetPropVal(lPropNum, varPropVal);
	return false;
}

bool ibValue::IsPropReadable(const long lPropNum) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->IsPropReadable(lPropNum);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropReadable(lPropNum);
	return true;
}

bool ibValue::IsPropWritable(const long lPropNum) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->IsPropWritable(lPropNum);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->IsPropWritable(lPropNum);
	return true;
}

long ibValue::GetNMethods() const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetNMethods();
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNMethods();
	return 0;
}

long ibValue::FindMethod(const wxString& strMethodName) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->FindMethod(strMethodName);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->FindMethod(strMethodName);
	return wxNOT_FOUND;
}

wxString ibValue::GetMethodName(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetMethodName(lMethodNum);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodName(lMethodNum);
	return wxEmptyString;
}

wxString ibValue::GetMethodHelper(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetMethodHelper(lMethodNum);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetMethodHelper(lMethodNum);
	return wxEmptyString;
}

long ibValue::GetNParams(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetNParams(lMethodNum);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->GetNParams(lMethodNum);
	return 0;
}

bool ibValue::GetParamDefValue(const long lMethodNum,
	const long lParamNum,
	ibValue& pvarParamDefValue) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
	return false;
}

bool ibValue::HasRetVal(const long lMethodNum) const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->HasRetVal(lMethodNum);
	ibValueMethodHelper* const methodHelper = GetPMethods();
	if (methodHelper != nullptr)
		return methodHelper->HasRetVal(lMethodNum);
	return false;
}

bool ibValue::CallAsProc(const long lMethodNum,
	ibValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->CallAsProc(lMethodNum, paParams, lSizeArray);
	return false;
}

bool ibValue::CallAsFunc(const long lMethodNum,
	ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}

void ibValue::PrepareNames() const
{
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		m_pRef->PrepareNames();
}

//get the current value (relevant for aggregate objects or dialog objects)
ibValue ibValue::GetValue(bool getThis)
{
	if (getThis)
		return this;
	if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
		return m_pRef->GetValue(true); // true - a sign of creating a new variable - a reference to an aggregate object
	return *this;
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