////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/mainFrameChild.h"

#include "backend/backend_exception.h"

#include <wx/weakref.h>

void ibAppEnterprise::OnKeyEvent(wxKeyEvent& event)
{
	if (!ibBackendException::IsErrorOutputProcessing() && event.GetEventType() == wxEVT_KEY_DOWN && event.GetKeyCode() == WXK_ESCAPE) {
		// ⭐⭐ ESCAPE CLOSES THE FORM IT WAS PRESSED IN, AND NOTHING ELSE. It is caught for the whole
		// application (FilterEvent), so an Escape meant for a window standing OVER the form - the list
		// settings, a picker, any modal dialog - closed the form underneath instead: the form destroyed
		// its children on the way out, the dialog still running its modal loop on the stack among them,
		// and the debug heap stopped the application (enterprise.dmp, 2026-09-16). A key pressed in
		// another top-level window is that window's to answer.
		wxWindow* const pressedIn = wxDynamicCast(event.GetEventObject(), wxWindow);
		if (pressedIn != nullptr && wxGetTopLevelParent(pressedIn) != mainFrame) {
			event.Skip();
			return;
		}
		wxAuiMDIChildFrame* childFrame = mainFrame->GetActiveChild();
		if (childFrame != nullptr) {
			// …and the frame is held WEAKLY: closed some other way before the call comes round, it is
			// gone - not a pointer into freed memory.
			wxWeakRef<wxAuiMDIChildFrame> frame(childFrame);
			CallAfter([frame]() { if (frame) frame->Close(); });
		}
	}

	event.Skip();
}

#include "frontend/visualView/visualHost.h"

void ibAppEnterprise::OnMouseEvent(wxMouseEvent& event)
{
	event.Skip();
}

void ibAppEnterprise::OnSetFocus(wxFocusEvent& event)
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
}

int ibAppEnterprise::FilterEvent(wxEvent& event)
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