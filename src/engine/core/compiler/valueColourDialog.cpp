#include "valueColourDialog.h"
#include "methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueColourDialog, CValue);

enum {
	enColour,
};

CMethods CValueColourDialog::m_methods;

enum {
	enChoose
};

void CValueColourDialog::PrepareNames() const
{
	m_methods.AppendAttribute("colour");
	m_methods.AppendMethod("choose", "choose()");
}

#include "appData.h"

CValue CValueColourDialog::Method(methodArg_t &aParams)
{
	if (appData->DesignerMode())
		return CValue();

	switch (aParams.GetIndex())
	{
	case enChoose:
		return m_colourDialog->ShowModal() != wxID_CANCEL;
	}

	return CValue();
}

#include "frontend/visualView/special/valueColour.h"

void CValueColourDialog::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
}

CValue CValueColourDialog::GetAttribute(attributeArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enColour: 
		return new CValueColour(
			m_colourDialog->GetColourData().GetColour()
		);
	}

	return CValue();
}

#include "frontend/mainFrame.h"

CValueColourDialog::CValueColourDialog() : CValue(eValueTypes::TYPE_VALUE),
m_colourDialog(new wxColourDialog(CMainFrame::Get()))
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