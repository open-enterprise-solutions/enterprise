////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value colour  
////////////////////////////////////////////////////////////////////////////

#include "valueColour.h"



//////////////////////////////////////////////////////////////////////

ibValue::ibValueMethodHelper ibValueColour::m_methodHelper;

ibValueColour::ibValueColour() :
	ibValue(ibValueTypes::TYPE_VALUE), m_colour()
{
}

ibValueColour::ibValueColour(const wxColour& colour) :
	ibValue(ibValueTypes::TYPE_VALUE), m_colour(colour)
{
}

bool ibValueColour::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray > 0 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		m_colour = typeConv::StringToColour(paParams[0]->GetString());
		return true;
	}
	else if (lSizeArray > 2) {
		m_colour = wxColour(
			paParams[0]->GetInteger(),
			paParams[1]->GetInteger(),
			paParams[2]->GetInteger()
		);
		return true;
	}
	return false;
}

enum
{
	enColorRed,
	enColorGreen,
	enColorBlue
};

void ibValueColour::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendProp(wxT("Red"));
	m_methodHelper.AppendProp(wxT("Green"));
	m_methodHelper.AppendProp(wxT("Blue"));
}

bool ibValueColour::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case enColorRed:
		m_colour.Set((unsigned char)varPropVal.GetUInteger(), m_colour.Green(), m_colour.Blue());
		return true;
	case enColorGreen:
		m_colour.Set(m_colour.Red(), (unsigned char)varPropVal.GetUInteger(), m_colour.Blue());
		return true;
	case enColorBlue:
		m_colour.Set(m_colour.Red(), m_colour.Green(), (unsigned char)varPropVal.GetUInteger());
		return true;
	}
	return false;
}

bool ibValueColour::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColorRed:
		pvarPropVal = m_colour.Red();
		return true;
	case enColorGreen:
		pvarPropVal = m_colour.Green();
		return true;
	case enColorBlue:
		pvarPropVal = m_colour.Blue();
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueColour, "Colour", string_to_clsid("VL_COLOR"));