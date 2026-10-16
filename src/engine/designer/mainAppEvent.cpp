////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "backend/session/session.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/mainFrameChild.h"

void ibAppDesigner::OnKeyEvent(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		wxAuiMDIChildFrame* childFrame = mainFrame->GetActiveChild();
		//if (childFrame != nullptr)
		//	childFrame->Close();
	}

	event.Skip();
}

#include "frontend/visualView/visualHost.h"

void ibAppDesigner::OnMouseEvent(wxMouseEvent& event)
{
	if (event.LeftIsDown() || event.RightIsDown()) {
		wxWindow* currentWindow = dynamic_cast<wxWindow*>(event.GetEventObject()); wxWindow* windowFocus = currentWindow;
		while (windowFocus && !windowFocus->IsKindOf(CLASSINFO(ibVisualHost))) {
			windowFocus = windowFocus->GetParent();
		}
		if (windowFocus) {
			ibVisualHost* visualEditor = dynamic_cast<ibVisualHost*>(windowFocus);
			if (visualEditor != nullptr) {
				if (event.GetEventType() == wxEVT_LEFT_DOWN
					|| event.GetEventType() == wxEVT_RIGHT_DOWN) {
					visualEditor->OnClickFromApp(currentWindow, event);
				}
			}
		}
	}

	event.Skip();
}

void ibAppDesigner::OnSetFocus(wxFocusEvent& event)
{
	wxWindow* child = dynamic_cast<wxWindow*>(event.GetEventObject());
	while (child != nullptr && !child->IsKindOf(CLASSINFO(wxAuiMDIChildFrame))) {
		child = child->GetParent();
	}

	if (child != nullptr && mainFrame != nullptr) {
		wxAuiMDIClientWindow* client = mainFrame->GetClientWindow();
		const int new_selection = client->FindPage(child);
		if (new_selection != client->GetSelection())
			client->SetSelection(new_selection);
	}
	
	event.Skip();
}

int ibAppDesigner::FilterEvent(wxEvent& event)
{
	const wxEventType type = event.GetEventType();
	if (type == wxEVT_KEY_DOWN || type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN) {
		if (ibSession* session = ibSession::Current())
			session->NoteActivity();
	}
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