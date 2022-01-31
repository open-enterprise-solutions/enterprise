#include "valueFontDialog.h"
#include "methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueFontDialog, CValue);

enum {
	enFont,
};

CMethods CValueFontDialog::m_methods;

enum {
	enChoose
};

void CValueFontDialog::PrepareNames() const
{
	m_methods.AppendAttribute("font");
	m_methods.AppendMethod("choose", "choose()");
}

#include "appData.h"

CValue CValueFontDialog::Method(methodArg_t &aParams)
{
	if (appData->DesignerMode())
		return CValue();

	switch (aParams.GetIndex())
	{
	case enChoose:
		return m_fontDialog->ShowModal() != wxID_CANCEL;
	}

	return CValue();
}

#include "frontend/visualView/special/valueFont.h"

void CValueFontDialog::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
}

CValue CValueFontDialog::GetAttribute(attributeArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enFont: 
		return new CValueFont(
			m_fontDialog->GetFontData().GetChosenFont()
		);
	}

	return CValue();
}

#include "frontend/mainFrame.h"

CValueFontDialog::CValueFontDialog() : CValue(eValueTypes::TYPE_VALUE),
m_fontDialog(new wxFontDialog(CMainFrame::Get()))
{
}

CValueFontDialog::~CValueFontDialog()
{
	wxDELETE(m_fontDialog);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueFontDialog, "fontDialog", TEXT2CLSID("VL_FONT"));