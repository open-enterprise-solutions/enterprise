////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value point 
////////////////////////////////////////////////////////////////////////////

#include "valuePoint.h"

//////////////////////////////////////////////////////////////////////

ibValuePoint::ibValuePoint() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_point(wxDefaultPosition)
{
}

ibValuePoint::ibValuePoint(const wxPoint& point) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_point(point)
{
}

bool ibValuePoint::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray == 1 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		m_point = typeConv::StringToPoint(paParams[0]->GetString());
		return true;
	}
	else if (lSizeArray > 1) {
		m_point = wxPoint(paParams[0]->GetInteger(), paParams[1]->GetInteger());
		return true;
	}
	return false;
}

enum
{
	eLeft,
	eTop
};

void ibValuePoint_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Left"));
	helper.AppendProp(wxT("Top"));
}

bool ibValuePoint::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eLeft:
		m_point.x = varPropVal.GetInteger();
		return true;
	case eTop:
		m_point.y = varPropVal.GetInteger();
		return true;
	}

	return false;
}

bool ibValuePoint::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eLeft:
		pvarPropVal = m_point.x;
		return true;
	case eTop:
		pvarPropVal = m_point.y;
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValuePoint, "Point", value_to_clsid("VL_PONT"));
