////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : boundary -- a position and which side of it is meant
////////////////////////////////////////////////////////////////////////////

#include "valueBoundary.h"

//////////////////////////////////////////////////////////////////////

ibValueBoundary::ibValueBoundary() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueBoundary::ibValueBoundary(const ibValue& value, ibBoundaryKind kind)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_value(value), m_kind(kind)
{
}

bool ibValueBoundary::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1 || lSizeArray > 2)
		return false;

	m_value = *paParams[0];
	if (lSizeArray == 2)
		m_kind = static_cast<ibBoundaryKind>(paParams[1]->GetInteger());

	return true;
}

wxString ibValueBoundary::GetString() const
{
	const wxString side = (m_kind == ibBoundaryKind_Excluding) ? wxT("Excluding") : wxT("Including");
	return m_value.GetString() + wxT(", ") + side;
}

enum
{
	eValue,
	eKind
};

void ibValueBoundary_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Value"));
	helper.AppendProp(wxT("Kind"));
}

bool ibValueBoundary::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eValue:
		m_value = varPropVal;
		return true;
	case eKind:
		m_kind = static_cast<ibBoundaryKind>(varPropVal.GetInteger());
		return true;
	}

	return false;
}

bool ibValueBoundary::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eValue:
		pvarPropVal = m_value;
		return true;
	case eKind:
		pvarPropVal = static_cast<int>(m_kind);
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

ENUM_TYPE_REGISTER(ibValueEnumBoundaryKind, "BoundaryKind");
VALUE_TYPE_REGISTER(ibValueBoundary, "Boundary");
