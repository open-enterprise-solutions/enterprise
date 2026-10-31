////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value colour  
////////////////////////////////////////////////////////////////////////////

#include "valueColour.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the packed form



//////////////////////////////////////////////////////////////////////

ibValueColour::ibValueColour() :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_colour()
{
}

ibValueColour::ibValueColour(const ibColour& colour) :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_colour(colour)
{
}

bool ibValueColour::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray > 0 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		m_colour.FromString(paParams[0]->GetString());
		return true;
	}
	else if (lSizeArray > 2) {
		m_colour = ibColour(
			static_cast<unsigned char>(paParams[0]->GetInteger()),
			static_cast<unsigned char>(paParams[1]->GetInteger()),
			static_cast<unsigned char>(paParams[2]->GetInteger())
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

void ibValueColour_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Red"));
	helper.AppendProp(ibT("Green"));
	helper.AppendProp(ibT("Blue"));
}

bool ibValueColour::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case enColorRed:
		m_colour = ibColour((unsigned char)varPropVal.GetUInteger(), m_colour.m_green, m_colour.m_blue);
		return true;
	case enColorGreen:
		m_colour = ibColour(m_colour.m_red, (unsigned char)varPropVal.GetUInteger(), m_colour.m_blue);
		return true;
	case enColorBlue:
		m_colour = ibColour(m_colour.m_red, m_colour.m_green, (unsigned char)varPropVal.GetUInteger());
		return true;
	}
	return false;
}

bool ibValueColour::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColorRed:
		pvarPropVal = m_colour.m_red;
		return true;
	case enColorGreen:
		pvarPropVal = m_colour.m_green;
		return true;
	case enColorBlue:
		pvarPropVal = m_colour.m_blue;
		return true;
	}
	return false;
}

namespace {
const ibString kColourText = ibT("v");
}

bool ibValueColour::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kColourText, m_colour.ToString());
	return true;
}

bool ibValueColour::DoDeserialize(const ibDataNode& node)
{
	m_colour.FromString(node.GetValue<ibString>(kColourText));
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueColour, "Colour", value_to_clsid("VL_COLOR"));
