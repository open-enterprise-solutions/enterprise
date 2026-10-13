////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value font 
////////////////////////////////////////////////////////////////////////////

#include "valueFont.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the packed form

//////////////////////////////////////////////////////////////////////


ibValueFont::ibValueFont() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_font()
{
}

ibValueFont::ibValueFont(const ibFont& font) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_font(font)
{
}

bool ibValueFont::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray == 1 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		m_font.FromString(paParams[0]->GetString());
		return true;
	}
	else if (lSizeArray > 4) {
		// Size, family, style, weight, underlined, face — wxFont's constructor's order, as it always was.
		ibFont font;
		font.m_pointSize = paParams[0]->GetInteger();
		font.m_family = static_cast<ibFontFamily>(paParams[1]->GetInteger());
		font.m_style = static_cast<ibFontStyle>(paParams[2]->GetInteger());
		font.m_weight = paParams[3]->GetInteger();
		font.m_underlined = paParams[4]->GetBoolean();
		if (lSizeArray > 5)
			font.m_faceName = paParams[5]->GetString();
		m_font = font;
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
		m_font.m_pointSize = varPropVal.GetInteger();
		return true;
	case eFamily:
		m_font.m_family = static_cast<ibFontFamily>(varPropVal.GetInteger());
		return true;
	case eStyle:
		m_font.m_style = static_cast<ibFontStyle>(varPropVal.GetInteger());
		return true;
	case eWeight:
		m_font.m_weight = varPropVal.GetInteger();
		return true;
	case eUnderlined:
		m_font.m_underlined = varPropVal.GetBoolean();
		return true;
	case eFace:
		m_font.m_faceName = varPropVal.GetString();
		return true;
	}
	return false;
}

bool ibValueFont::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eSize:
		pvarPropVal = static_cast<int>(m_font.m_pointSize);
		return true;
	case eFamily:
		pvarPropVal = static_cast<int>(m_font.m_family);
		return true;
	case eStyle:
		pvarPropVal = static_cast<int>(m_font.m_style);
		return true;
	case eWeight:
		pvarPropVal = m_font.m_weight;
		return true;
	case eUnderlined:
		pvarPropVal = m_font.m_underlined;
		return true;
	case eFace:
		pvarPropVal = m_font.m_faceName;
		return true;
	}

	return false;
}

namespace {
const wxString kFontText = wxT("v");
}

bool ibValueFont::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kFontText, m_font.ToString());
	return true;
}

bool ibValueFont::DoDeserialize(const ibDataNode& node)
{
	m_font.FromString(node.GetValue<wxString>(kFontText));
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueFont, "Font", value_to_clsid("VL_FONT"));
