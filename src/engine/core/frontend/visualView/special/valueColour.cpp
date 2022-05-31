////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value colour  
////////////////////////////////////////////////////////////////////////////

#include "valueColour.h"
#include "compiler/methods.h"

#include "frontend/mainFrame.h"
#include "databaseLayer/databaseLayer.h"

#include "utils/stringUtils.h"
#include "utils/typeconv.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueColour, CValue);

CMethods CValueColour::m_methods;

CValueColour::CValueColour() : CValue(eValueTypes::TYPE_VALUE), m_colour()
{
}

CValueColour::CValueColour(wxColour colour) : CValue(eValueTypes::TYPE_VALUE),
m_colour(colour)
{
}

bool CValueColour::Init(CValue **aParams)
{
	if (aParams[0]->GetType() == eValueTypes::TYPE_STRING) { 
		m_colour = TypeConv::StringToColour(aParams[0]->ToString()); 
		return true; 
	}
	else { 
		m_colour = wxColour(aParams[0]->ToInt(), aParams[1]->ToInt(), aParams[2]->ToInt()); 
		return true; 
	}
	return false;
}

enum
{
	enColorR,
	enColorG,
	enColorB
};

void CValueColour::PrepareNames() const
{
	std::vector <SEng>aAttributes;

	SEng attribute;

	attribute.sName = wxT("r");
	aAttributes.push_back(attribute);
	attribute.sName = wxT("g");
	aAttributes.push_back(attribute);
	attribute.sName = wxT("b");
	aAttributes.push_back(attribute);

	m_methods.PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void CValueColour::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	switch (aParams.GetIndex())
	{
	case enColorR: m_colour.Set(cVal.ToDouble(), m_colour.Green(), m_colour.Blue()); break;
	case enColorG: m_colour.Set(m_colour.Red(), cVal.ToDouble(), m_colour.Blue()); break;
	case enColorB: m_colour.Set(m_colour.Red(), m_colour.Green(), cVal.ToDouble()); break;
	}
}

CValue CValueColour::GetAttribute(attributeArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enColorR: return m_colour.Red();
	case enColorG: return m_colour.Green();
	case enColorB: return m_colour.Blue();
	}

	return CValue();
}

wxString CValueColour::GetTypeString()const
{
	return wxT("colour");
}

wxString CValueColour::GetString()const
{
	return TypeConv::ColourToString(m_colour);
}

CValueColour::~CValueColour()
{
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueColour, "colour", TEXT2CLSID("VL_COLO"));