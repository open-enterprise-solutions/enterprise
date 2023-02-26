////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "core/frontend/mainFrame.h"
#include "core/frontend/mainFrameChild.h"

void CMainApp::OnKeyEvent(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		if (!appData->DesignerMode()) {
			wxAuiMDIChildFrame* childFrame =
				mainFrame->GetActiveChild();
			if (childFrame != NULL)
				childFrame->Close();
		}
	}

	event.Skip();
}

#include "core/frontend/visualView/visualInterface.h"

void CMainApp::OnMouseEvent(wxMouseEvent& event)
{
	if (event.LeftIsDown() || event.RightIsDown()) {
		wxWindow* currentWindow = dynamic_cast<wxWindow*>(event.GetEventObject()); wxWindow* windowFocus = currentWindow;
		while (windowFocus && !windowFocus->IsKindOf(CLASSINFO(IVisualHost))) {
			windowFocus = windowFocus->GetParent();
		}
		if (windowFocus) {
			IVisualHost* visualEditor = dynamic_cast<IVisualHost*>(windowFocus);
			if (visualEditor != NULL) {
				if (event.GetEventType() == wxEVT_LEFT_DOWN
					|| event.GetEventType() == wxEVT_RIGHT_DOWN) {
					visualEditor->OnClickFromApp(currentWindow, event);
				}
			}
		}
	}

	event.Skip();
}

void CMainApp::OnSetFocus(wxFocusEvent& event)
{
	wxWindow* windowFocus = dynamic_cast<wxWindow*>(event.GetEventObject());
	while (windowFocus && !windowFocus->IsKindOf(CLASSINFO(wxAuiMDIChildFrame))) {
		windowFocus = windowFocus->GetParent();
	}

	if (windowFocus != NULL) {
		wxAuiNotebook* noteBook = mainFrame->GetNotebook();
		size_t newSelPos = noteBook->FindPage(windowFocus);
		if (newSelPos != noteBook->GetSelection())
			noteBook->SetSelection(newSelPos);
	}

	event.Skip();
}

int CMainApp::FilterEvent(wxEvent& event)
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