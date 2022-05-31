////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : base value  
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "valueole.h"
#include "valueArray.h"
#include "valueMap.h"
#include "methods.h"
#include "functions.h"
#include "utils/stringUtils.h"

#include <map>
#include <wx/datetime.h>
#include <wx/longlong.h>

wxIMPLEMENT_ABSTRACT_CLASS(ITypeValue, wxObject);

//**********************************************************************
//*                        Type implementation                         *
//**********************************************************************

void ITypeValue::SetType(eValueTypes type)
{
	if (m_typeClass == eValueTypes::TYPE_REFFER) {
		GetRef()->SetType(type);
	}
	else {
		m_typeClass = type;
	}
}

eValueTypes ITypeValue::GetType() const
{
	if (m_typeClass == eValueTypes::TYPE_REFFER)
		return GetRef()->GetType();
	else
		return m_typeClass;
}

CLASS_ID ITypeValue::GetClassType() const
{
	if (m_typeClass == eValueTypes::TYPE_REFFER)
		return GetRef()->GetClassType();
	else if (m_typeClass == eValueTypes::TYPE_VALUE)
		return CValue::GetTypeIDByRef(this);
	else
		return CValue::GetIDByVT(m_typeClass);
}

wxIMPLEMENT_DYNAMIC_CLASS(CValue, wxObject);

//**********************************************************************
//*                       Value implementation                         *
//**********************************************************************

#ifdef _DEBUG
static unsigned int m_nCreateCount = 0;
#endif

#define _DEBUG_VALUE_CREATE() \
	wxLogDebug("Create %d", m_nCreateCount++);\

CValue::CValue()
	: ITypeValue(eValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(NULL), m_bReadOnly(false)
{
	_DEBUG_VALUE_CREATE();
}

//конструктор копирования:
CValue::CValue(const CValue& cVal)
	: ITypeValue(eValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(NULL), m_bReadOnly(false)
{
	Copy(cVal);

	_DEBUG_VALUE_CREATE();
}

CValue::CValue(CValue* pValue)
	: ITypeValue(eValueTypes::TYPE_EMPTY), m_refCount(0), m_pRef(pValue), m_bReadOnly(false)
{
	if (m_pRef) {
		m_typeClass = eValueTypes::TYPE_REFFER;
		m_pRef->IncrRef();
	}

	_DEBUG_VALUE_CREATE();
}

CValue::CValue(const wxDateTime& cParam)
	: ITypeValue(eValueTypes::TYPE_DATE), m_refCount(0), m_pRef(NULL), m_bReadOnly(false)
{
	wxLongLong m_llData = cParam.GetValue();
	m_dData = m_llData.GetValue();

	_DEBUG_VALUE_CREATE();
}

CValue::CValue(int nYear, int nMonth, int nDay, unsigned short nHour, unsigned short nMinute, unsigned short nSecond)
	: ITypeValue(eValueTypes::TYPE_DATE), m_refCount(0), m_pRef(NULL), m_bReadOnly(false)
{
	wxDateTime m_dataVal(nDay, (wxDateTime::Month)(nMonth - 1), nYear, nHour, nMinute, nSecond);

	if (m_dataVal.IsValid()) {
		wxLongLong m_llData = m_dataVal.GetValue();
		m_dData = m_llData.GetValue();
	}

	_DEBUG_VALUE_CREATE();
}

CValue::CValue(eValueTypes type, bool readOnly)
	: ITypeValue(type), m_refCount(0), m_pRef(NULL), m_bReadOnly(readOnly)
{
	switch (type)
	{
	case TYPE_BOOLEAN: m_bData = false; break;
	case TYPE_NUMBER: m_fData.SetZero(); break;
	case TYPE_DATE: m_dData = emptyDate; break;
	case TYPE_STRING: m_sData.clear(); break;
	default: m_pRef = NULL; break;
	}

	_DEBUG_VALUE_CREATE();
}

//Конструкторы по типам:
#define CVALUE_BYTYPE(v_parclass, v_type, v_value) \
CValue::CValue (v_parclass cParam) \
    : ITypeValue(v_type), m_refCount(0), m_pRef(NULL), m_bReadOnly(false) \
{\
	v_value = cParam;\
	_DEBUG_VALUE_CREATE();\
}

#define CVALUE_BYTYPE_MOVE(v_parclass, v_type, v_value) \
CValue::CValue (v_parclass cParam) \
  : ITypeValue(v_type), v_value(std::move(cParam)), m_refCount(0), m_pRef(NULL), m_bReadOnly(false) \
{\
	_DEBUG_VALUE_CREATE();\
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

#ifdef _DEBUG
	wxLogDebug("Delete %d", --m_nCreateCount);
#endif
}

void CValue::Reset()
{
	if (m_typeClass != eValueTypes::TYPE_EMPTY && m_bReadOnly)
		CTranslateError::Error(_("Attempt to assign a value to a write-denied variable"));

	if (m_typeClass == eValueTypes::TYPE_REFFER && m_pRef)
		m_pRef->DecrRef();

	m_pRef = NULL;

	m_typeClass = eValueTypes::TYPE_EMPTY;
}

//methods:
void CValue::Copy(const CValue& cOld)
{
	if (this == &cOld)
		return;

	Reset();

	m_typeClass = cOld.m_typeClass;

	switch (cOld.m_typeClass)
	{
	case eValueTypes::TYPE_NULL: break;
	case eValueTypes::TYPE_BOOLEAN: m_bData = cOld.m_bData; break;
	case eValueTypes::TYPE_NUMBER: m_fData = cOld.m_fData; break;
	case eValueTypes::TYPE_STRING: m_sData = cOld.m_sData; break;
	case eValueTypes::TYPE_DATE: m_dData = cOld.m_dData; break;

	case eValueTypes::TYPE_VALUE:
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_MODULE:
		m_pRef = const_cast<CValue*>(&cOld); m_pRef->IncrRef();
		m_typeClass = eValueTypes::TYPE_REFFER;
		break;

	case eValueTypes::TYPE_REFFER:
		if (cOld.m_pRef) {
			m_pRef = cOld.m_pRef; m_pRef->IncrRef();
		} break;
	default: m_typeClass = eValueTypes::TYPE_EMPTY;
	}
}

void CValue::operator = (bool bValue)
{
	SetValue(bValue);
}

void CValue::operator = (signed int iValue)
{
	SetValue(iValue);
}

void CValue::operator = (unsigned int iValue)
{
	SetValue(iValue);
}

void CValue::operator = (double dValue)
{
	SetValue(dValue);
}

void CValue::operator = (const wxString& sValue)
{
	SetValue(sValue);
}

void CValue::operator = (const CValue& cVal)
{
	if (this != &cVal && !m_bReadOnly) Copy(cVal);
}

void CValue::operator = (eValueTypes type)
{
	unsigned long m_typeClassOld = m_typeClass;

	CValue objValue(*this);

	switch (type)
	{
	case TYPE_BOOLEAN: m_bData = false; break;
	case TYPE_NUMBER: m_fData.SetZero(); break;
	case TYPE_DATE: m_dData = emptyDate; break;
	case TYPE_STRING: m_sData.clear(); break;
	default: m_pRef = NULL; break;
	}

	m_typeClass = type;

	if (m_typeClassOld != eValueTypes::TYPE_EMPTY) {
		SetData(objValue);
	}
}

void CValue::operator = (CValue* pParam)
{
	if (this != pParam && !m_bReadOnly) {
		if (pParam) {
			m_pRef = pParam;
			m_pRef->IncrRef();
			m_typeClass = eValueTypes::TYPE_REFFER;
		}
		else {
			m_typeClass = eValueTypes::TYPE_EMPTY;
		}
	}
}

void CValue::operator = (const wxDateTime& cParam)
{
	SetValue(cParam);
}

void CValue::operator = (wxLongLong_t cParam)
{
	SetValue(cParam);
}

void CValue::SetValue(const CValue& cVal)
{
	if (this == &cVal) return;

	if (m_typeClass == eValueTypes::TYPE_REFFER) {
		m_pRef->SetValue(cVal);
	}
	else {
		Copy(cVal);
	}
}

void CValue::SetData(const CValue& cVal)
{
	if (this == &cVal) return;

	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		SetBoolean(cVal.GetString());
		return;
	case eValueTypes::TYPE_NUMBER:
		SetNumber(cVal.GetString());
		return;
	case eValueTypes::TYPE_STRING:
		SetString(cVal.GetString());
		return;
	case eValueTypes::TYPE_DATE:
		SetDate(cVal.GetString());
		return;
	case eValueTypes::TYPE_REFFER:
		if (m_pRef) m_pRef->SetData(cVal);
		return;
	}

	SetValue(cVal);
}

void CValue::SetBoolean(const wxString& sBoolean)
{
	if (m_bReadOnly && m_typeClass == eValueTypes::TYPE_REFFER) {
		m_pRef->SetBoolean(sBoolean);
		return;
	}

	Reset();

	m_typeClass = eValueTypes::TYPE_BOOLEAN;
	m_bData = sBoolean.CompareTo(wxT("true"), wxString::ignoreCase) == 0;
}

void CValue::SetNumber(const wxString& sNumber)
{
	if (m_bReadOnly && m_typeClass == eValueTypes::TYPE_REFFER) {
		m_pRef->SetNumber(sNumber); return;
	}

	Reset();

	unsigned int nSuccessful = m_fData.FromString(sNumber.ToStdWstring());
	if (nSuccessful > 0) {
		CTranslateError::Error(_("Cannot convert string to number!"));
	}
	m_typeClass = eValueTypes::TYPE_NUMBER;
}

void CValue::SetString(const wxString& sString)
{
	if (m_bReadOnly && m_typeClass == eValueTypes::TYPE_REFFER)
	{
		m_pRef->SetString(sString);
		return;
	}

	Reset();

	m_typeClass = eValueTypes::TYPE_STRING;
	m_sData = sString;
}

void CValue::SetDate(const wxString& strDate)
{
	wxDateTime strTime; CValue cRes = eValueTypes::TYPE_DATE;

	if (strTime.ParseFormat(strDate, "%d.%m.%Y %H:%M:%S")) {
		wxLongLong m_llData = strTime.GetValue();
		cRes.m_dData = m_llData.GetValue();
	}
	else if (strTime.ParseFormat(strDate, "%Y%m%d%H%M%S")) {
		wxLongLong m_llData = strTime.GetValue();
		cRes.m_dData = m_llData.GetValue();
	}
	else if (strTime.ParseFormat(strDate, "%Y%m%d")) {
		wxLongLong m_llData = strTime.GetValue();
		cRes.m_dData = m_llData.GetValue();
	}
	else if (strTime.ParseDateTime(strDate)) {
		wxLongLong m_llData = strTime.GetValue();
		cRes.m_dData = m_llData.GetValue();
	}
	else {
		CTranslateError::Error(_("Cannot convert string to date!"));
	}

	Copy(cRes);
}

bool CValue::FindValue(const wxString& findData, std::vector<CValue>& foundedObjects)
{
	try {
		switch (m_typeClass)
		{
		case eValueTypes::TYPE_BOOLEAN: SetBoolean(findData); foundedObjects.push_back(this); return true;
		case eValueTypes::TYPE_NUMBER: SetNumber(findData); foundedObjects.push_back(this); return true;
		case eValueTypes::TYPE_STRING: SetString(findData); foundedObjects.push_back(this); return true;
		case eValueTypes::TYPE_DATE: SetDate(findData); foundedObjects.push_back(this); return true;

		case eValueTypes::TYPE_REFFER: if (m_pRef) {
			return m_pRef->FindValue(findData, foundedObjects);
		}
		}
	}
	catch (...) {
		//wxMessageBox(err->what(), wxT("error converting value!"));
		return false;
	}
	return false;
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
	{
		wxString sBool = GetString();
		sBool.Trim(true);
		sBool.Trim(false);
		sBool.MakeUpper();

		return sBool.CompareTo(wxT("true"), wxString::ignoreCase) == 0;
	}
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
	case eValueTypes::TYPE_STRING:
	{
		wxString strVal = GetString();

		strVal.Trim(true);
		strVal.Trim(false);
		strVal.MakeUpper();

		number_t number;
		unsigned int nSuccessful = number.FromString(strVal.ToStdWstring());
		if (nSuccessful > 0) CTranslateError::Error(_("Cannot convert string to number!"));
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
	case eValueTypes::TYPE_NULL: return wxT("null");
	case eValueTypes::TYPE_BOOLEAN:	return m_bData ? wxT("true") : wxT("false");
	case eValueTypes::TYPE_NUMBER: return m_fData.ToString();
	case eValueTypes::TYPE_STRING: return m_sData;
	case eValueTypes::TYPE_DATE:
	{
		wxLongLong dateTime = m_dData;
		wxDateTime m_datetime(dateTime);
		return m_datetime.Format("%d.%m.%Y %H:%M:%S");
	}
	case eValueTypes::TYPE_REFFER:
	{
		if (m_pRef != NULL)
			return m_pRef->GetString();
		wxEmptyString;
	}
	};

	return wxEmptyString;
}

wxLongLong_t CValue::GetDate() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		return emptyDate;
	case eValueTypes::TYPE_NUMBER:
	{
		wxLongLong_t dTemp = 0;
		if (!m_fData.ToInt(dTemp)) {
			return dTemp * 1000;
		} break;
	}
	case eValueTypes::TYPE_STRING:
	{
		wxDateTime dateTime;
		if (dateTime.ParseFormat(m_sData, "%d.%m.%Y %H:%M:%S")) {
			wxLongLong m_llData = dateTime.GetValue();
			return m_llData.GetValue();
		}
		else if (dateTime.ParseFormat(m_sData, "%Y%m%d%H%M%S")) {
			wxLongLong m_llData = dateTime.GetValue();
			return m_llData.GetValue();
		}
		else if (dateTime.ParseDateTime(m_sData)) {
			wxLongLong m_llData = dateTime.GetValue();
			return m_llData.GetValue();
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
	if (m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->GetRef();

	return const_cast<CValue*>(this);
}

void CValue::FromDate(int& nYear, int& nMonth, int& nDay) const
{
	wxLongLong nCurDate = wxLongLong(GetDate());
	wxDateTime currentTime(nCurDate);

	nYear = currentTime.GetYear();
	nMonth = currentTime.GetMonth() + 1;
	nDay = currentTime.GetDay();
}

void CValue::FromDate(int& nYear, int& nMonth, int& nDay, unsigned short& nHour, unsigned short& nMinute, unsigned short& nSecond) const
{
	wxLongLong nCurDate = wxLongLong(GetDate());
	wxDateTime currentTime(nCurDate);

	nYear = currentTime.GetYear();
	nMonth = currentTime.GetMonth() + 1;
	nDay = currentTime.GetDay();
	nHour = currentTime.GetHour();
	nMinute = currentTime.GetMinute();
	nSecond = currentTime.GetSecond();
}

void CValue::FromDate(int& nYear, int& nMonth, int& nDay, int& DayOfWeek, int& DayOfYear, int& WeekOfYear) const
{
	wxLongLong nCurDate = wxLongLong(GetDate());
	wxDateTime currentTime(nCurDate);

	nYear = currentTime.GetYear();
	nMonth = currentTime.GetMonth() - 1;
	nDay = currentTime.GetDay();

	WeekOfYear = DayOfWeek = DayOfYear = 0;

	wxDateTime m_DateTime(nDay, (wxDateTime::Month)nMonth, nYear);
	DayOfYear = m_DateTime.GetDayOfYear();
	DayOfWeek = m_DateTime.GetWeekDay() - 1;

	if (DayOfWeek < 1) DayOfWeek = 7;

	WeekOfYear = 1 + (DayOfYear - 1) / 7;

	int nD = (1 + (DayOfYear - 1) % 7);
	if (nD > DayOfWeek) WeekOfYear++;
}

void CValue::Detach()
{
	if (m_typeClass == eValueTypes::TYPE_REFFER)
		m_pRef->Detach();
}

void CValue::Attach(void* pObj)
{
	if (m_typeClass == eValueTypes::TYPE_REFFER)
		m_pRef->Attach(pObj);
}

void* CValue::GetAttach()
{
	return NULL;
}

bool CValue::IsEmpty() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN: return m_bData == false;
	case eValueTypes::TYPE_NUMBER:  return m_fData.IsZero();
	case eValueTypes::TYPE_DATE:    return m_dData == emptyDate;
	case eValueTypes::TYPE_STRING:  return m_sData.IsEmpty();
	case eValueTypes::TYPE_VALUE:   return false;
	case eValueTypes::TYPE_ENUM:    return false;
	case eValueTypes::TYPE_OLE:     return false;
	case eValueTypes::TYPE_MODULE:  return false;
	case eValueTypes::TYPE_REFFER:  return m_pRef ? m_pRef->IsEmpty() : true;
	};

	return true;
}

wxString CValue::GetTypeString() const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
		return wxT("boolean");
	case eValueTypes::TYPE_NUMBER:
		return wxT("number");
	case eValueTypes::TYPE_STRING:
		return wxT("string");
	case eValueTypes::TYPE_DATE:
		return wxT("date");
	case eValueTypes::TYPE_NULL:
		return wxT("null");
	case eValueTypes::TYPE_REFFER:
		return m_pRef->GetTypeString();
	};

	return NOT_DEFINED;
}

//*************************************************************
//*                        array support                      *
//*************************************************************

void CValue::SetAt(const CValue& cKey, CValue& cVal)
{
	switch (m_typeClass)
	{
	case TYPE_REFFER: return m_pRef->SetAt(cKey, cVal);
	}

	SetAttribute(cKey.GetString(), cVal);
}

CValue CValue::GetAt(const CValue& cKey)
{
	switch (m_typeClass)
	{
	case TYPE_REFFER: return m_pRef->GetAt(cKey);
	}

	return GetAttribute(cKey.GetString());
}

//*************************************************************
//*                    iterator support                       *
//*************************************************************

bool CValue::HasIterator() const
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) return m_pRef->HasIterator();
	return false;
}

CValue CValue::GetItEmpty()
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) return m_pRef->GetItEmpty();
	return CValue();
}

CValue CValue::GetItAt(unsigned int idx)
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) return m_pRef->GetItAt(idx);
	return CValue();
}

unsigned int CValue::GetItSize() const
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) return m_pRef->GetItSize();
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
	case eValueTypes::TYPE_EMPTY: return false;
	case eValueTypes::TYPE_NULL:  return false;

	case eValueTypes::TYPE_BOOLEAN: return GetBoolean() > cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER: return GetNumber() > cParam.GetNumber();
	case eValueTypes::TYPE_DATE: return GetDate() > cParam.GetDate();
	case eValueTypes::TYPE_STRING: return GetString() > cParam.GetString();

	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE: return GetString() > cParam.GetString();

	case eValueTypes::TYPE_REFFER: return m_pRef->CompareValueGT(cParam);
	};

	return false;
}

// compare '>='
bool CValue::CompareValueGE(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY: return false;
	case eValueTypes::TYPE_NULL:  return false;

	case eValueTypes::TYPE_BOOLEAN: return GetBoolean() >= cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER: return GetNumber() >= cParam.GetNumber();
	case eValueTypes::TYPE_DATE: return GetDate() >= cParam.GetDate();
	case eValueTypes::TYPE_STRING: return GetString() >= cParam.GetString();

	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE: return GetString() >= cParam.GetString();

	case eValueTypes::TYPE_REFFER: return m_pRef->CompareValueGE(cParam);
	};

	return false;
}

// compare '<'
bool CValue::CompareValueLS(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY: return false;
	case eValueTypes::TYPE_NULL:  return false;

	case eValueTypes::TYPE_BOOLEAN: return GetBoolean() < cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER: return GetNumber() < cParam.GetNumber();
	case eValueTypes::TYPE_DATE: return GetDate() < cParam.GetDate();
	case eValueTypes::TYPE_STRING: return GetString() < cParam.GetString();

	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE: return GetString() < cParam.GetString();

	case eValueTypes::TYPE_REFFER: return m_pRef->CompareValueLS(cParam);
	};

	return false;
}

// compare '<='
bool CValue::CompareValueLE(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY: return false;
	case eValueTypes::TYPE_NULL:  return false;

	case eValueTypes::TYPE_BOOLEAN: return GetBoolean() <= cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER: return GetNumber() <= cParam.GetNumber();
	case eValueTypes::TYPE_DATE: return GetDate() <= cParam.GetDate();
	case eValueTypes::TYPE_STRING: return GetString() <= cParam.GetString();

	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE: return GetString() <= cParam.GetString();

	case eValueTypes::TYPE_REFFER: return m_pRef->CompareValueLE(cParam);
	};

	return false;
}

// compare '=='
bool CValue::CompareValueEQ(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY: return eValueTypes::TYPE_EMPTY == cParam.GetType();
	case eValueTypes::TYPE_NULL:  return eValueTypes::TYPE_NULL == cParam.GetType();

	case eValueTypes::TYPE_BOOLEAN: return GetBoolean() == cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER: return GetNumber() == cParam.GetNumber();
	case eValueTypes::TYPE_DATE: return GetDate() == cParam.GetDate();
	case eValueTypes::TYPE_STRING: return GetString() == cParam.GetString();

	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE: return GetString() == cParam.GetString() && GetTypeString() == cParam.GetTypeString();

	case eValueTypes::TYPE_REFFER: return m_pRef->CompareValueEQ(cParam);
	};

	return false;
}

// compare '!='
bool CValue::CompareValueNE(const CValue& cParam) const
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_EMPTY: return eValueTypes::TYPE_EMPTY != cParam.GetType();
	case eValueTypes::TYPE_NULL:  return eValueTypes::TYPE_NULL != cParam.GetType();

	case eValueTypes::TYPE_BOOLEAN: return GetBoolean() != cParam.GetBoolean();
	case eValueTypes::TYPE_NUMBER: return GetNumber() != cParam.GetNumber();
	case eValueTypes::TYPE_DATE: return GetDate() != cParam.GetDate();
	case eValueTypes::TYPE_STRING: return GetString() != cParam.GetString();

	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_VALUE: return GetString() != cParam.GetString() && GetTypeString() != cParam.GetTypeString();

	case eValueTypes::TYPE_REFFER: return m_pRef->CompareValueNE(cParam);
	};

	return false;
}

const CValue& CValue::operator+(const CValue& cParam)
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_NUMBER: m_fData = m_fData + cParam.GetNumber(); break;
	case eValueTypes::TYPE_DATE: m_dData = m_dData + cParam.GetDate(); break;
	}

	return *this;
}

const CValue& CValue::operator-(const CValue& cParam)
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_NUMBER: m_fData = m_fData - cParam.GetNumber(); break;
	case eValueTypes::TYPE_DATE: m_dData = m_dData - cParam.GetDate(); break;
	}

	return *this;
}

bool CValue::ToBool() const
{
	return GetBoolean();
}

int CValue::ToInt() const
{
	number_t number = GetNumber();
	return number.ToInt();
}

unsigned int CValue::ToUInt() const
{
	number_t number = GetNumber();
	return number.ToUInt();
}

double CValue::ToDouble() const
{
	number_t number = GetNumber();
	return number.ToDouble();
}

wxLongLong_t CValue::ToDate() const
{
	return GetDate();
}

wxDateTime CValue::ToDateTime() const
{
	return GetDate();
}

wxString CValue::ToString() const
{
	return GetString();
}

//*************************************************************
//*                        showing value                      *
//*************************************************************

#define st_showValue wxT("ShowValue")
#define st_true wxT("true")
#define st_false wxT("false")

void CValue::ShowValue()
{
	switch (m_typeClass)
	{
	case eValueTypes::TYPE_BOOLEAN:
	{
		wxMessageBox(m_bData ? st_true : st_false, st_showValue);
		break;
	}
	case eValueTypes::TYPE_NUMBER:
	{
		wxString m_string = m_fData.ToString();

		StringUtils::TrimRight(m_string, '0');
		StringUtils::TrimRight(m_string, '.');

		wxMessageBox(m_string, st_showValue);
		break;
	}
	case eValueTypes::TYPE_STRING:
	{
		wxMessageBox(m_sData, st_showValue);
		break;
	}
	case eValueTypes::TYPE_DATE:
	{
		int nYear, nMonth, nDay;

		wxLongLong m_llDate(m_dData);
		wxDateTime nDate(m_llDate);

		nYear = nDate.GetYear();
		nMonth = nDate.GetMonth() - 1;
		nDay = nDate.GetDay();

		wxString strDate;
		strDate = wxString::Format("%02d.%02d.%04d", nDay, nMonth, nYear);
		wxMessageBox(strDate, st_showValue);
		break;
	}
	case eValueTypes::TYPE_REFFER:
	{
		m_pRef->ShowValue();
		break;
	}
	};
}

//*************************************************************
//           РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА                   *
//*************************************************************

#define GET_THIS \
CValue *pThis;\
if(m_typeClass==eValueTypes::TYPE_REFFER&&m_pRef)\
	pThis=m_pRef;\
else\
	pThis=this;

CValue CValue::Method(const wxString& sName, CValue** aParams)
{
	GET_THIS
		int iName = FindMethod(sName);
	if (iName != wxNOT_FOUND) {
		methodArg_t aMethParams(aParams, sizeof(aParams) / sizeof(aParams[0]), iName, sName);
		CValue retData = pThis->Method(aMethParams);
		aMethParams.CheckParams(); return retData;
	}

	CTranslateError::Error(_("Value does not represent an aggregate object (%s, %s)"), GetTypeString().wc_str(), sName.wc_str());
	return *this;
}

CValue CValue::Method(methodArg_t& aParams)
{
	GET_THIS
		if (aParams.GetIndex() != wxNOT_FOUND) {
			return pThis->Method(aParams);
		}

	CTranslateError::Error(_("Value does not represent an aggregate object"));
	return *this;
}

void CValue::SetAttribute(const wxString& sName, CValue& cVal)
{
	int iName = FindAttribute(sName);
	GET_THIS
		if (iName == wxNOT_FOUND || pThis == NULL)
			CTranslateError::Error(_("Value does not represent an aggregate object (%s)"), sName.wc_str());

	attributeArg_t aParams(iName, sName);
	pThis->SetAttribute(aParams, cVal);
}

void CValue::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	GET_THIS
		if (aParams.GetIndex() == wxNOT_FOUND || pThis == NULL)
			CTranslateError::Error(_("Value does not represent an aggregate object"));
	pThis->SetAttribute(aParams, cVal);
}

CValue CValue::GetAttribute(const wxString& sName)
{
	int iName = FindAttribute(sName);
	GET_THIS
		if (iName == wxNOT_FOUND || pThis == NULL)
			CTranslateError::Error(_("Value does not represent an aggregate object (%s)"), sName.wc_str());

	attributeArg_t aParams(iName, sName);
	return pThis->GetAttribute(aParams);
}

CValue CValue::GetAttribute(attributeArg_t& aParams)
{
	GET_THIS
		if (aParams.GetIndex() != wxNOT_FOUND && pThis)
			return pThis->GetAttribute(aParams);
	CTranslateError::Error(_("Value does not represent an aggregate object"));
	return *this;
}

#define GET_PMETHODS \
const CValue *pThis = m_typeClass==eValueTypes::TYPE_REFFER ? m_pRef : this; \
CMethods* pMethods=pThis->GetPMethods(); \

int CValue::FindMethod(const wxString& sName) const
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) {
		if (m_pRef->m_typeClass == eValueTypes::TYPE_OLE)
			return m_pRef->FindMethod(sName);
	}

	GET_PMETHODS
		if (pMethods) return pMethods->FindMethod(sName);

	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->FindMethod(sName);

	return wxNOT_FOUND;
}

int CValue::FindAttribute(const wxString& sName) const
{
	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) {
		if (m_pRef->m_typeClass == eValueTypes::TYPE_OLE) return m_pRef->FindAttribute(sName);
	}

	GET_PMETHODS
		if (pMethods) {
			int nRes = pMethods->FindAttribute(sName);
			if (nRes >= 0)
				return nRes;
		}

	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return m_pRef->FindAttribute(sName);

	return wxNOT_FOUND;
}

wxString CValue::GetMethodName(unsigned int nNumber) const
{
	GET_PMETHODS
		if (pMethods) return pMethods->GetMethodName(nNumber);

	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) return pThis->GetMethodName(nNumber);
	return wxEmptyString;
}

wxString CValue::GetMethodDescription(unsigned int nNumber) const
{
	GET_PMETHODS
		if (pMethods)
			return pMethods->GetMethodDescription(nNumber);

	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return pThis->GetMethodDescription(nNumber);

	return wxEmptyString;
}

wxString CValue::GetAttributeName(unsigned int nNumber) const
{
	GET_PMETHODS
		if (pMethods) {
			wxString sResult = pMethods->GetAttributeName(nNumber);
			if (!sResult.empty()) return sResult;
		}

	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return pThis->GetAttributeName(nNumber);

	return wxEmptyString;
}

unsigned int CValue::GetNMethods() const
{
	GET_PMETHODS
		if (pMethods)
			return pMethods->GetNMethods();

	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER)
		return pThis->GetNMethods();

	return 0;
}

unsigned int CValue::GetNAttributes() const
{
	GET_PMETHODS
		if (pMethods) {
			int nRes = pMethods->GetNAttributes();
			if (nRes > 0)
				return nRes;
		}

	if (m_pRef && m_typeClass == eValueTypes::TYPE_REFFER) {
		return pThis->GetNAttributes();
	}

	return 0;
}

//получить текущее значение (актуального для агрегатных объектов или объектов диалога)
CValue CValue::GetValue(bool getThis)
{
	if (getThis) 
		return this;

	if (m_typeClass == eValueTypes::TYPE_REFFER) {
		return m_pRef->GetValue(true); // true - признак создания новой переменной - ссылки на агрегатный объект
	}

	return *this;
}

CValue CValue::CallFunctionV(const wxString& sName, CValue** p)
{
	return Method(sName, p);
}

CValue CValue::CallFunction(const wxString sName, ...)
{
	va_list lst;
	va_start(lst, sName);
	CValue** ppParams = (CValue**)lst;
	va_end(lst);
	return Method(sName, ppParams);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

S_VALUE_REGISTER(CValue, "undefined", eValueTypes::TYPE_EMPTY, 0, TEXT2CLSID("VL_UNDF"));

S_VALUE_REGISTER(CValue, "boolean", eValueTypes::TYPE_BOOLEAN, 1, TEXT2CLSID("VL_BOOL"));
S_VALUE_REGISTER(CValue, "number", eValueTypes::TYPE_NUMBER, 2, TEXT2CLSID("VL_NUMB"));
S_VALUE_REGISTER(CValue, "date", eValueTypes::TYPE_DATE, 3, TEXT2CLSID("VL_DATE"));
S_VALUE_REGISTER(CValue, "string", eValueTypes::TYPE_STRING, 4, TEXT2CLSID("VL_STRI"));

S_VALUE_REGISTER(CValue, "null", eValueTypes::TYPE_NULL, 5, TEXT2CLSID("VL_NULL"));