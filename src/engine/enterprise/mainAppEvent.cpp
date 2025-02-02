////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/mainFrameChild.h"

void CEnterpriseApp::OnKeyEvent(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		wxAuiMDIChildFrame* childFrame = mainFrame->GetActiveChild();
		if (childFrame != nullptr)
			childFrame->Close();
	}

	event.Skip();
}

#include "frontend/visualView/visual.h"

void CEnterpriseApp::OnMouseEvent(wxMouseEvent& event)
{
#if wxUSE_MOUSEWHEEL
	if (event.GetEventType() == wxEVT_MOUSEWHEEL) event.m_linesPerAction = 1;
#endif
	event.Skip();
}

void CEnterpriseApp::OnSetFocus(wxFocusEvent& event)
{
	event.Skip();
}

int CEnterpriseApp::FilterEvent(wxEvent& event)
{
	if (event.GetEventType() == wxEVT_KEY_DOWN) {
		OnKeyEvent(
			static_cast<wxKeyEvent&>(event)
		);
	}
	else if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
		OnMouseEvent(
			static_cast<wxMouseEvent&>(event)
		);
	}
	else if (event.GetEventType() == wxEVT_LEFT_DOWN || event.GetEventType() == wxEVT_RIGHT_DOWN) {
		OnMouseEvent(
			static_cast<wxMouseEvent&>(event)
		);
	}
	else if (event.GetEventType() == wxEVT_SET_FOCUS) {
		OnSetFocus(
			static_cast<wxFocusEvent&> (event)
		);
	}

	return Event_Skip;
}