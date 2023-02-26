////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value font 
////////////////////////////////////////////////////////////////////////////

#include "valueFont.h"
#include "frontend/mainFrame.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

#include "utils/stringUtils.h"
#include "utils/typeconv.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueFont, CValue);

CValue::CMethodHelper CValueFont::m_methodHelper;

CValueFont::CValueFont() : CValue(eValueTypes::TYPE_VALUE), m_font()
{
}

CValueFont::CValueFont(const wxFont& font) : CValue(eValueTypes::TYPE_VALUE), m_font(font)
{
}

bool CValueFont::Init(CValue** paParams, const long lSizeArray)
{
	if (paParams[0]->GetType() == eValueTypes::TYPE_STRING) { 
		m_font = TypeConv::StringToFont(paParams[0]->GetString()); 
		return true;
	}
	else { 
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

void CValueFont::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendProp(wxT("size"));
	m_methodHelper.AppendProp(wxT("family"));
	m_methodHelper.AppendProp(wxT("style"));
	m_methodHelper.AppendProp(wxT("weight"));
	m_methodHelper.AppendProp(wxT("underlined"));
	m_methodHelper.AppendProp(wxT("face"));
}

bool CValueFont::SetPropVal(const long lPropNum, const CValue& varPropVal)
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

bool CValueFont::GetPropVal(const long lPropNum, CValue& pvarPropVal)
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