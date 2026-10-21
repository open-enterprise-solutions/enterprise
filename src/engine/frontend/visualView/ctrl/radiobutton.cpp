
#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

ibValueRadioButton::ibValueRadioButton() : ibValueWindow()
{
}

wxObject* ibValueRadioButton::Create(wxWindow* wxparent, ibVisualHost *visualHost) 
{
	wxRadioButton *radioButton = new wxRadioButton(wxparent, wxID_ANY,
		m_propertyTitle->GetValueAsTranslateString(),
		wxDefaultPosition,
		wxDefaultSize);

	return radioButton;
}

void ibValueRadioButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstCreated)
{
}

void ibValueRadioButton::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	wxRadioButton *radioButton = dynamic_cast<wxRadioButton *>(wxobject);

	if (radioButton != nullptr) {
		radioButton->SetLabel(m_propertyTitle->GetValueAsTranslateString());
		radioButton->SetValue(m_propertySelected->GetValueAsBoolean() != false);
	}

	UpdateWindow(radioButton);
}

void ibValueRadioButton::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool ibValueRadioButton::ReadData(const ibDataNode& node)
{
	// The caption and the starting selection are the radio button's own, so they travel with it - without
	// them every saved radio button came back as "Radio button", unselected.
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertySelected->SetNodeValue(node.GetProperty(m_propertySelected->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueRadioButton::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertySelected->GetName(), m_propertySelected->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueRadioButton, "Radiobutton", "Widget", control_to_clsid("CT_RDBT"));
