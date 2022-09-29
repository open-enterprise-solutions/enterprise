
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueRadioButton, IValueWindow)

//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

CValueRadioButton::CValueRadioButton() : IValueWindow()
{
}

wxObject* CValueRadioButton::Create(wxObject* parent, IVisualHost *visualHost) 
{
	wxRadioButton *radioButton = new wxRadioButton((wxWindow *)parent, wxID_ANY,
		m_propertyCaption->GetValueAsString(),
		wxDefaultPosition,
		wxDefaultSize);

	return radioButton;
}

void CValueRadioButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueRadioButton::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxRadioButton *radioButton = dynamic_cast<wxRadioButton *>(wxobject);

	if (radioButton != NULL) {
		radioButton->SetLabel(m_propertyCaption->GetValueAsString());
		radioButton->SetValue(m_propertySelected->GetValueAsBoolean() != false);
	}

	UpdateWindow(radioButton);
}

void CValueRadioButton::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool CValueRadioButton::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueRadioButton::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}