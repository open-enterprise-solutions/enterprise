////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : window object
////////////////////////////////////////////////////////////////////////////

#include "window.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
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

bool ibValueWindow::ReadData(const ibDataNode& node)
{
	m_propertyMinSize->ReadNodeValue(node.GetProperty(m_propertyMinSize->GetName()));
	m_propertyMaxSize->ReadNodeValue(node.GetProperty(m_propertyMaxSize->GetName()));
	m_propertyFont->ReadNodeValue(node.GetProperty(m_propertyFont->GetName()));
	m_propertyFG->ReadNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->ReadNodeValue(node.GetProperty(m_propertyBG->GetName()));
	m_propertyTooltip->ReadNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyEnabled->ReadNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->ReadNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueWindow::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinSize->GetName(), m_propertyMinSize->GetNodeValue());
	node.SetProperty(m_propertyMaxSize->GetName(), m_propertyMaxSize->GetNodeValue());
	node.SetProperty(m_propertyFont->GetName(), m_propertyFont->GetNodeValue());
	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueControl::WriteData(node);
}