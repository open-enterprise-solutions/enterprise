#include "textBox.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                                 Value TextBox                                   *
//***********************************************************************************

ibValueTextBox::ibValueTextBox() : ibValueWindow()
{
	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 50));
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool ibValueTextBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueTextBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}

//***********************************************************************************

bool ibValueTextBox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueTextBox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueTextBox, "Textbox", "Container", g_controlTextBoxCLSID);
