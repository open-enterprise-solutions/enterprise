////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : window object
////////////////////////////////////////////////////////////////////////////

#include "window.h"
#include "form.h"


//***********************************************************************************
//*                                    ValueWindow                                  *
//***********************************************************************************

ibValueWindow::ibValueWindow() : ibValueControl()
{
}

//***********************************************************************************
//*                                  Update                                       *
//***********************************************************************************

void ibValueWindow::UpdateWindow(ibFrontendWindow* window)
{
	if (window == nullptr)
		return;

	// Detached controls — /demo synthetic trees and other
	// programmatically-built hierarchies that never got a form owner —
	// used to crash here on the wxASSERT(ownerForm). For those, treat
	// the form as "enabled" by default; the control's own property
	// still gets applied. Form-owned controls keep the combined check.
	ibValueForm* ownerForm = GetOwnerForm();
	const bool formEnabled = (ownerForm == nullptr) ? true : ownerForm->IsFormEnabled();

	// Unified setter sequence — desktop's wxWindow and web's ibWebWindow
	// share the same SetMinSize / SetMaxSize / SetFont /
	// SetForegroundColour / SetBackgroundColour / Enable / Show /
	// SetToolTip API, so one body covers both. Only window->Layout()
	// at the end is wx-specific — web uses flex layout on the client,
	// no re-layout call needed.
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		window->SetMinSize(m_propertyMinSize->GetValueAsSize());
	if (m_propertyMaxSize->GetValueAsSize() != wxDefaultSize)
		window->SetMaxSize(m_propertyMaxSize->GetValueAsSize());
	if (m_propertyFont->IsOk())
		window->SetFont(m_propertyFont->GetValueAsFont());
	if (m_propertyFG->IsOk())
		window->SetForegroundColour(m_propertyFG->GetValueAsColour());
	if (m_propertyBG->IsOk())
		window->SetBackgroundColour(m_propertyBG->GetValueAsColour());
	window->Enable(m_propertyEnabled->GetValueAsBoolean() && formEnabled);
	window->Show(m_propertyVisible->GetValueAsBoolean());
	window->SetToolTip(m_propertyTooltip->GetValueAsTranslateString());

#ifndef OES_USE_WEB
	// Size changes may require a re-layout; flex handles that on web.
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize ||
		m_propertyMaxSize->GetValueAsSize() != wxDefaultSize) {
		window->Layout();
	}
#endif
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

bool ibValueWindow::SaveData(ibWriterMemory& writer)
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