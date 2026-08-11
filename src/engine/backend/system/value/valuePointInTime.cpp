////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : point in time -- a date and, optionally, the record at it
////////////////////////////////////////////////////////////////////////////

#include "valuePointInTime.h"

//////////////////////////////////////////////////////////////////////

ibValuePointInTime::ibValuePointInTime() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValuePointInTime::ibValuePointInTime(const wxDateTime& date, const ibValue& reference)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_date(date), m_reference(reference)
{
}

// New PointInTime(date [, reference]) -- the date first, the reference second and allowed to be
// undefined. Building one BY HAND is the rare road: what everything already at a moment (a document,
// a reference, a record subordinate to a recorder) offers is its own PointInTime() method, which
// arrives with the value already set. Two doors to the same value would be one door too many, so
// this one takes exactly what it says and nothing clever.
bool ibValuePointInTime::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1 || lSizeArray > 2)
		return false;
	if (paParams[0]->GetType() != ibValueTypes::TYPE_DATE)
		return false;   // a moment with no date is not a moment

	m_date = paParams[0]->GetDateTime();
	if (lSizeArray == 2 && !paParams[1]->IsEmpty())
		m_reference = *paParams[1];

	return true;
}

wxString ibValuePointInTime::GetString() const
{
	if (!m_date.IsValid())
		return wxEmptyString;

	const wxString date = m_date.Format(wxT("%d.%m.%Y %H:%M:%S"));
	return m_reference.IsEmpty() ? date : date + wxT(", ") + m_reference.GetString();
}

// ⭐ THE DATE FIRST, THE REFERENCE INSIDE IT.
//
// A moment with NO reference sorts BEFORE every moment of the same date that has one: it is the
// instant itself, and nothing recorded in it has happened yet. That reading is what makes a bare
// date usable as an opening boundary -- "from this instant onward" needs a value that no record of
// that instant sorts below.
//
// Compared against a plain DATE the date alone decides, because a date IS a moment whose reference
// is not set. One order, not two.
int ibValuePointInTime::CompareValueLS(const ibValue& cParam) const
{
	wxDateTime otherDate;
	ibValuePointInTime* other = nullptr;

	if (cParam.ConvertToValue(other) && other != nullptr)
		otherDate = other->m_date;
	else if (cParam.GetType() == ibValueTypes::TYPE_DATE)
		otherDate = cParam.GetDateTime();
	else
		return ibValue::CompareValueLS(cParam);   // not a moment at all -- the base decides

	if (m_date.IsValid() != otherDate.IsValid())
		return m_date.IsValid() ? 1 : -1;         // an unset moment is the smallest, as NULL is
	if (m_date.IsValid() && m_date != otherDate)
		return m_date < otherDate ? -1 : 1;

	const ibValue otherRef = other != nullptr ? other->m_reference : ibValue();
	if (m_reference.IsEmpty() != otherRef.IsEmpty())
		return m_reference.IsEmpty() ? -1 : 1;
	if (m_reference.IsEmpty())
		return 0;

	return m_reference.CompareValueLS(otherRef);  // references order by metaID, then by guid
}

bool ibValuePointInTime::CompareValueEQ(const ibValue& cParam) const
{
	ibValuePointInTime* other = nullptr;
	if (!cParam.ConvertToValue(other) || other == nullptr)
		return false;   // type-strict equality: a date is ORDERED with a moment, but is not one
	return CompareValueLS(cParam) == 0;
}

bool ibValuePointInTime::CompareValueNE(const ibValue& cParam) const
{
	return !CompareValueEQ(cParam);
}

enum
{
	eDate,
	eRef
};

enum
{
	eCompare
};

void ibValuePointInTime_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Date"));
	helper.AppendProp(wxT("Ref"));

	helper.AppendFunc(wxT("Compare"), 1, wxT("Compare(pointInTime)"));
}

bool ibValuePointInTime::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eDate:
		m_date = varPropVal.GetDateTime();
		return true;
	case eRef:
		m_reference = varPropVal;
		return true;
	}

	return false;
}

bool ibValuePointInTime::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eDate:
		pvarPropVal = m_date;
		return true;
	case eRef:
		pvarPropVal = m_reference;
		return true;
	}

	return false;
}

// Compare(other) -> 1 / 0 / -1. The SAME order the operators use, vended as a number for the
// scripts that want the three-way answer in one call instead of two comparisons.
bool ibValuePointInTime::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCompare: {
		if (lSizeArray < 1)
			return false;
		const int order = CompareValueLS(*paParams[0]);
		pvarRetValue = order < 0 ? -1 : (order > 0 ? 1 : 0);
		return true;
	}
	}

	return ibValueStaticMembers::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValuePointInTime, "PointInTime");
