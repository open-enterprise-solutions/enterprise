////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value font 
////////////////////////////////////////////////////////////////////////////

#include "valueFont.h"

//////////////////////////////////////////////////////////////////////


ibValueFont::ibValueFont() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_font()
{
}

ibValueFont::ibValueFont(const wxFont& font) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_font(font)
{
}

bool ibValueFont::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray == 1 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		m_font = typeConv::StringToFont(paParams[0]->GetString()); 
		return true;
	}
	else if (lSizeArray > 4) {
		m_font = wxFont(
			(wxFontFamily)paParams[0]->GetInteger(), 
			(wxFontFamily)paParams[1]->GetInteger(), 
			(wxFontStyle)paParams[2]->GetInteger(), 
			(wxFontWeight)paParams[3]->GetInteger(), 
			paParams[4]->GetBoolean(), 
			paParams[5]->GetString()); 
		return true; 
	}
	return false;
}

enum
{
	eSize,
	eFamily,
	eStyle,
	eWeight,
	eUnderlined,
	eFace
};

void ibValueFont_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Size"));
	helper.AppendProp(wxT("Family"));
	helper.AppendProp(wxT("Style"));
	helper.AppendProp(wxT("Weight"));
	helper.AppendProp(wxT("Underlined"));
	helper.AppendProp(wxT("Face"));
}

bool ibValueFont::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eSize:
		m_font.SetPointSize(varPropVal.GetInteger());
		return true;
	case eFamily:
		m_font.SetFamily((wxFontFamily)varPropVal.GetInteger());
		return true;
	case eStyle:
		m_font.SetStyle((wxFontStyle)varPropVal.GetInteger());
		return true;
	case eWeight:
		m_font.SetWeight((wxFontWeight)varPropVal.GetInteger());
		return true;
	case eUnderlined:
		m_font.SetUnderlined(varPropVal.GetBoolean());
		return true;
	case eFace:
		m_font.SetFaceName(varPropVal.GetString());
		return true;
	}
	return false; 
}

bool ibValueFont::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eSize:
		pvarPropVal = m_font.GetPointSize();
		return true;
	case eFamily:
		pvarPropVal = m_font.GetFamily();
		return true;
	case eStyle:
		pvarPropVal = m_font.GetStyle();
		return true;
	case eWeight:
		pvarPropVal = m_font.GetWeight();
		return true;
	case eUnderlined:
		pvarPropVal = m_font.GetUnderlined();
		return true;
	case eFace:
		pvarPropVal = m_font.GetFaceName();
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueFont, "Font", value_to_clsid("VL_FONT"));
