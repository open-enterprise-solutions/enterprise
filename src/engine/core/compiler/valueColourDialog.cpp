#include "valueColourDialog.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueColourDialog, CValue);

enum {
	enColour,
};

CValue::CMethodHelper CValueColourDialog::m_methodHelper;

enum {
	enChoose
};

void CValueColourDialog::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendProp("colour");
	m_methodHelper.AppendFunc("choose", "choose()");
}

#include "appData.h"

bool CValueColourDialog::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	if (appData->DesignerMode())
		return false;

	switch (lMethodNum)
	{
	case enChoose:
		pvarRetValue = m_colourDialog->ShowModal() != wxID_CANCEL;
		return true;
	}

	return false;
}

#include "frontend/visualView/special/valueColour.h"

bool CValueColourDialog::SetPropVal(const long lPropNum, CValue &varPropVal)
{
	return false;
}

bool CValueColourDialog::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColour: 
		pvarPropVal = new CValueColour(
			m_colourDialog->GetColourData().GetColour()
		);
		return true;
	}

	return false;
}

#include "frontend/mainFrame.h"

CValueColourDialog::CValueColourDialog() : CValue(eValueTypes::TYPE_VALUE),
m_colourDialog(new wxColourDialog(wxAuiDocMDIFrame::GetFrame()))
{
}

CValueColourDialog::~CValueColourDialog()
{
	wxDELETE(m_colourDialog);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueColourDialog, "colourDialog", TEXT2CLSID("VL_CLRD"));