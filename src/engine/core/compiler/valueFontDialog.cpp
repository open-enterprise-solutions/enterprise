#include "valueFontDialog.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueFontDialog, CValue);

CValue::CMethodHelper CValueFontDialog::m_methodHelper;

void CValueFontDialog::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendProp("font");
	m_methodHelper.AppendFunc("choose", "choose()");
}

#include "appData.h"

bool CValueFontDialog::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	if (appData->DesignerMode())
		return false;

	switch (lMethodNum)
	{
	case enChoose:
		pvarRetValue = m_fontDialog->ShowModal() != wxID_CANCEL;
		return true;
	}

	return false;
}

#include "frontend/visualView/special/valueFont.h"

bool CValueFontDialog::SetPropVal(const long lPropNum, CValue &varPropVal)
{
	return false;
}

bool CValueFontDialog::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enFont: 
		pvarPropVal = new CValueFont(
			m_fontDialog->GetFontData().GetChosenFont()
		);
		return true;
	}

	return false;
}

#include "frontend/mainFrame.h"

CValueFontDialog::CValueFontDialog() : CValue(eValueTypes::TYPE_VALUE),
m_fontDialog(new wxFontDialog(wxAuiDocMDIFrame::GetFrame()))
{
}

CValueFontDialog::~CValueFontDialog()
{
	wxDELETE(m_fontDialog);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueFontDialog, "fontDialog", TEXT2CLSID("VL_FOTD"));