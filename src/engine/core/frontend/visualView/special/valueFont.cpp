////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value font 
////////////////////////////////////////////////////////////////////////////

#include "valueFont.h"
#include "compiler/methods.h"

#include "frontend/mainFrame.h"
#include "databaseLayer/databaseLayer.h"

#include "utils/stringUtils.h"
#include "utils/typeconv.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueFont, CValue);

CMethods CValueFont::m_methods;

CValueFont::CValueFont() : CValue(eValueTypes::TYPE_VALUE), m_font()
{
}

CValueFont::CValueFont(const wxFont &font) : CValue(eValueTypes::TYPE_VALUE), m_font(font)
{
}

bool CValueFont::Init(CValue **aParams)
{
	if (aParams[0]->GetType() == eValueTypes::TYPE_STRING) { m_font = TypeConv::StringToFont(aParams[0]->ToString()); return true; }
	else { m_font = wxFont((wxFontFamily)aParams[0]->ToInt(), (wxFontFamily)aParams[1]->ToInt(), (wxFontStyle)aParams[2]->ToInt(), (wxFontWeight)aParams[3]->ToInt(), aParams[4]->ToBool(), aParams[5]->ToString()); return true; }
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

void CValueFont::PrepareNames() const
{
	std::vector <SEng>aAttributes;

	SEng attribute;

	attribute.sName = "size";
	aAttributes.push_back(attribute);
	attribute.sName = "family";
	aAttributes.push_back(attribute);
	attribute.sName = "style";
	aAttributes.push_back(attribute);
	attribute.sName = "weight";
	aAttributes.push_back(attribute);
	attribute.sName = "underlined";
	aAttributes.push_back(attribute);
	attribute.sName = "face";
	aAttributes.push_back(attribute);

	m_methods.PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void CValueFont::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	switch (aParams.GetIndex())
	{
	case eSize: m_font.SetPointSize(cVal.ToInt()); break;
	case eFamily: m_font.SetFamily((wxFontFamily)cVal.ToInt()); break;
	case eStyle: m_font.SetStyle((wxFontStyle)cVal.ToInt()); break;
	case eWeight: m_font.SetWeight((wxFontWeight)cVal.ToInt()); break;
	case eUnderlined: m_font.SetUnderlined(cVal.ToBool()); break;
	case eFace: m_font.SetFaceName(cVal.ToString()); break;
	}
}

CValue CValueFont::GetAttribute(attributeArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case eSize: return m_font.GetPointSize();
	case eFamily: return m_font.GetFamily();
	case eStyle: return m_font.GetStyle();
	case eWeight: return m_font.GetWeight();
	case eUnderlined: return m_font.GetUnderlined();
	case eFace: return m_font.GetFaceName();
	}

	return CValue();
}

wxString CValueFont::GetTypeString()const
{
	return "font";
}

wxString CValueFont::GetString()const
{
	return TypeConv::FontToString(m_font);
}

CValueFont::~CValueFont()
{
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueFont, "font", TEXT2CLSID("VL_FONT"));