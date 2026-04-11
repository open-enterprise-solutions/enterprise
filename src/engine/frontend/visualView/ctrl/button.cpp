#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueButton, ibValueWindow)

//****************************************************************************
//*                              Button                                      *
//****************************************************************************

ibValueButton::ibValueButton() : ibValueWindow()
{
}

wxObject* ibValueButton::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxButton* wxbutton = new wxButton(wxparent, wxID_ANY, m_propertyTitle->GetValueAsTranslateString());
	//setup event 
	wxbutton->Bind(wxEVT_BUTTON, &ibValueButton::OnButtonPressed, this);
	return wxbutton;
}

void ibValueButton::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueButton::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxButton* button = dynamic_cast<wxButton*>(wxobject);

	if (button != nullptr) {

		if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Auto) {
			button->SetLabel(m_propertyTitle->GetValueAsTranslateString());
			button->SetBitmap(m_propertyPicture->GetValueAsBitmap());
		}
		else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_PictureAndText) {
			button->SetLabel(m_propertyTitle->GetValueAsTranslateString());
			button->SetBitmap(m_propertyPicture->GetValueAsBitmap());
		}
		else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Picture) {
			button->SetLabel(wxEmptyString);
			button->SetBitmap(m_propertyPicture->GetValueAsBitmap());
		}
		else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Text) {
			button->SetLabel(m_propertyTitle->GetValueAsTranslateString());
			button->SetBitmap(wxNullBitmap);
		}
	}

	UpdateWindow(button);
}

void ibValueButton::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*                           Data									*
//*******************************************************************

bool ibValueButton::LoadData(ibReaderMemory& reader)
{
	m_propertyTitle->LoadData(reader);
	m_propertyRepresentation->LoadData(reader);
	m_propertyPicture->LoadData(reader);

	//events
	m_onButtonPressed->LoadData(reader);

	return ibValueWindow::LoadData(reader);
}

bool ibValueButton::SaveData(ibWriterMemory writer)
{
	m_propertyTitle->SaveData(writer);
	m_propertyRepresentation->SaveData(writer);
	m_propertyPicture->SaveData(writer);

	//events
	m_onButtonPressed->SaveData(writer);

	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueButton, "Button", "Widget", string_to_clsid("CT_BUTN"));