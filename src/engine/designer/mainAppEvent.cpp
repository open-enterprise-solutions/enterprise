////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/mainFrameChild.h"

void CDesignerApp::OnKeyEvent(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		wxAuiMDIChildFrame* childFrame = mainFrame->GetActiveChild();
		if (childFrame != nullptr)
			childFrame->Close();
	}

	event.Skip();
}

#include "frontend/visualView/visual.h"

void CDesignerApp::OnMouseEvent(wxMouseEvent& event)
{
	if (event.LeftIsDown() || event.RightIsDown()) {
		wxWindow* currentWindow = dynamic_cast<wxWindow*>(event.GetEventObject()); wxWindow* windowFocus = currentWindow;
		while (windowFocus && !windowFocus->IsKindOf(CLASSINFO(IVisualHost))) {
			windowFocus = windowFocus->GetParent();
		}
		if (windowFocus) {
			IVisualHost* visualEditor = dynamic_cast<IVisualHost*>(windowFocus);
			if (visualEditor != nullptr) {
				if (event.GetEventType() == wxEVT_LEFT_DOWN
					|| event.GetEventType() == wxEVT_RIGHT_DOWN) {
					visualEditor->OnClickFromApp(currentWindow, event);
				}
			}
		}
	}

#if wxUSE_MOUSEWHEEL
	if (event.GetEventType() == wxEVT_MOUSEWHEEL) event.m_linesPerAction = 1;
#endif

	event.Skip();
}

void CDesignerApp::OnSetFocus(wxFocusEvent& event)
{
	event.Skip();
}

int CDesignerApp::FilterEvent(wxEvent& event)
{
	if (event.GetEventType() == wxEVT_KEY_DOWN) {
		OnKeyEvent(
			static_cast<wxKeyEvent&>(event)
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