#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueButton, IValueWindow)

//****************************************************************************
//*                              Button                                      *
//****************************************************************************

CValueButton::CValueButton() : IValueWindow()
{
}

wxObject* CValueButton::Create(wxObject* parent, IVisualHost* visualHost)
{
	wxButton* m_button = new wxButton((wxWindow*)parent, wxID_ANY,
		m_propertyCaption->GetValueAsString(),
		wxDefaultPosition,
		wxDefaultSize);

	//setup event 
	m_button->Bind(wxEVT_BUTTON, &CValueButton::OnButtonPressed, this);

	return m_button;
}

void CValueButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueButton::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxButton* button = dynamic_cast<wxButton*>(wxobject);

	if (button != NULL) {
		button->SetLabel(m_propertyCaption->GetValueAsString());
		button->SetBitmap(m_propertyIcon->GetValueAsBitmap());
	}

	UpdateWindow(button);
}

void CValueButton::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
}

//*******************************************************************
//*                           Data									*
//*******************************************************************

bool CValueButton::LoadData(CMemoryReader& reader)
{
	m_propertyCaption->SetValue(reader.r_stringZ());
	return IValueWindow::LoadData(reader);
}

bool CValueButton::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());
	return IValueWindow::SaveData(writer);
}