////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value point 
////////////////////////////////////////////////////////////////////////////

#include "valuePoint.h"


#include "frontend/mainFrame.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

#include "utils/stringUtils.h"
#include "utils/typeconv.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValuePoint, CValue);

CValue::CMethodHelper CValuePoint::m_methodHelper;

CValuePoint::CValuePoint() : CValue(eValueTypes::TYPE_VALUE), m_point(wxDefaultPosition)
{
}

CValuePoint::CValuePoint(const wxPoint& point) : CValue(eValueTypes::TYPE_VALUE), m_point(point)
{
}

bool CValuePoint::Init(CValue** paParams, const long lSizeArray)
{
	if (paParams[0]->GetType() == eValueTypes::TYPE_STRING) {
		m_point = TypeConv::StringToPoint(paParams[0]->GetString());
		return true;
	}
	else {
		m_point = wxPoint(paParams[0]->GetInteger(), paParams[1]->GetInteger());
		return true;
	}
	return false;
}

CValuePoint::~CValuePoint()
{
}

enum
{
	eX,
	eY
};

void CValuePoint::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendProp(wxT("x"));
	m_methodHelper.AppendProp(wxT("y"));
}

bool CValuePoint::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	switch (lPropNum)
	{
	case eX:
		m_point.x = varPropVal.GetInteger();
		return true;
	case eY:
		m_point.y = varPropVal.GetInteger();
		return true;
	}

	return false;
}

bool CValuePoint::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eX:
		pvarPropVal = m_point.x;
		return true;
	case eY:
		pvarPropVal = m_point.y;
		return true;
	}

	return false;
}

wxString CValuePoint::GetTypeString()const
{
	return "point";
}

wxString CValuePoint::GetString()const
{
	return TypeConv::PointToString(m_point);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValuePoint, "point", TEXT2CLSID("VL_PONT"));