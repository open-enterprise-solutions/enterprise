////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : window object
////////////////////////////////////////////////////////////////////////////

#include "window.h"
#include "form.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibValueWindow, ibValueControl)

//***********************************************************************************
//*                                    ValueWindow                                  *
//***********************************************************************************

ibValueWindow::ibValueWindow() : ibValueControl()
{
}

//***********************************************************************************
//*                                  Update                                       *
//***********************************************************************************

void ibValueWindow::UpdateWindow(wxWindow* window)
{
	// All of the properties of the wxWindow object are applied in this function
	if (window == nullptr)
		return;

	// Minimum size
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		window->SetMinSize(m_propertyMinSize->GetValueAsSize());

	// Maximum size
	if (m_propertyMaxSize->GetValueAsSize() != wxDefaultSize)
		window->SetMaxSize(m_propertyMaxSize->GetValueAsSize());

	// Font
	if (m_propertyFont->IsOk())
		window->SetFont(m_propertyFont->GetValueAsFont());

	// Foreground
	if (m_propertyFG->IsOk())
		window->SetForegroundColour(m_propertyFG->GetValueAsColour());

	// Background
	if (m_propertyBG->IsOk())
		window->SetBackgroundColour(m_propertyBG->GetValueAsColour());

	ibValueForm* ownerForm = GetOwnerForm();
	wxASSERT(ownerForm);

	// Enabled
	window->Enable(m_propertyEnabled->GetValueAsBoolean() && ownerForm->IsFormEnabled());

	// Hidden
	window->Show(m_propertyVisible->GetValueAsBoolean());

	// Tooltip
	window->SetToolTip(m_propertyTooltip->GetValueAsTranslateString());

	//after lay out 
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize ||
		m_propertyMaxSize->GetValueAsSize() != wxDefaultSize) {
		window->Layout();
	}
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueWindow::LoadData(ibReaderMemory& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyMinSize->SetValue(typeConv::StringToSize(propValue));
	reader.r_stringZ(propValue);
	m_propertyMaxSize->SetValue(typeConv::StringToSize(propValue));
	reader.r_stringZ(propValue);
	m_propertyFont->SetValue(typeConv::StringToFont(propValue));
	reader.r_stringZ(propValue);
	m_propertyFG->SetValue(typeConv::StringToColour(propValue));
	reader.r_stringZ(propValue);
	m_propertyBG->SetValue(typeConv::StringToColour(propValue));

	reader.r_stringZ(propValue);
	m_propertyTooltip->SetValue(propValue);

	m_propertyEnabled->SetValue(reader.r_u8());
	m_propertyVisible->SetValue(reader.r_u8());

	return ibValueControl::LoadData(reader);
}

bool ibValueWindow::SaveData(ibWriterMemory writer)
{
	writer.w_stringZ(
		m_propertyMinSize->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyMaxSize->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyFont->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyFG->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyBG->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyTooltip->GetValueAsString()
	);
	writer.w_u8(
		m_propertyEnabled->GetValueAsBoolean()
	);
	writer.w_u8(
		m_propertyVisible->GetValueAsBoolean()
	);

	return ibValueControl::SaveData(writer);
}