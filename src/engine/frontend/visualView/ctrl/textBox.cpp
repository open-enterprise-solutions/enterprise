#include "textBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueTextBox::ibValueTextBox() : ibValueWindow()
{
	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 50));
}

#include "frontend/visualView/ctrl/form.h"

wxObject* ibValueTextBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibTextEditor* textWindow = new ibTextEditor(nullptr, wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	return textWindow;
}

void ibValueTextBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
	ibTextEditor* textWindow = dynamic_cast<ibTextEditor*>(wxobject);
}

void ibValueTextBox::OnSelected(wxObject* wxobject)
{
}

void ibValueTextBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibTextEditor* textWindow = dynamic_cast<ibTextEditor*>(wxobject);

	if (textWindow) {
	}

	UpdateWindow(textWindow);
}

void ibValueTextBox::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
}

//**********************************************************************************

#include "frontend/win/editor/textEditor/textEditorPrintOut.h"

wxPrintout* ibValueTextBox::CreatePrintout() const
{
	ibTextEditor* gridWindow = dynamic_cast<ibTextEditor*>(GetWxObject());
	if (gridWindow != nullptr)
		return new ibTextEditorPrintout(gridWindow);

	return nullptr;
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

CONTROL_TYPE_REGISTER(ibValueTextBox, "Textbox", "Container", control_to_clsid("CT_TEXT"));
