
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueRadioButton, ibValueWindow)

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

void ibValueRadioButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstСreated)
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

bool ibValueRadioButton::LoadData(ibReaderMemory &reader)
{
	return ibValueWindow::LoadData(reader);
}

bool ibValueRadioButton::SaveData(ibWriterMemory writer)
{
	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueRadioButton, "Radiobutton", "Widget", string_to_clsid("CT_RDBT"));