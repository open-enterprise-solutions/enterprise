////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include <wx/weakref.h>

#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/mainFrameChild.h"

void ibAppEnterprise::OnKeyEvent(wxKeyEvent& event)
{
	if (mainFrame != nullptr && event.GetEventType() == wxEVT_KEY_DOWN && event.GetKeyCode() == WXK_ESCAPE) {
		// ⭐⭐ ESCAPE CLOSES THE FORM IT WAS PRESSED IN, AND NOTHING ELSE — the desktop's rule (enterprise/mainAppEvent):
		// a key pressed in another top-level window — a question the server asks, any dialog over the form — is that
		// window's to answer.
		wxWindow* const pressedIn = wxDynamicCast(event.GetEventObject(), wxWindow);
		if (pressedIn != nullptr && wxGetTopLevelParent(pressedIn) != mainFrame) {
			event.Skip();
			return;
		}
		wxAuiMDIChildFrame* childFrame = mainFrame->GetActiveChild();
		if (childFrame != nullptr) {
			// …and the frame is held WEAKLY: closed some other way before the call comes round, it is gone — not a
			// pointer into freed memory. Its close asks the server (ibAuiChildFrame::OnCloseWindow).
			wxWeakRef<wxAuiMDIChildFrame> frame(childFrame);
			CallAfter([frame]() { if (frame) frame->Close(); });
		}
	}

	event.Skip();
}

void ibAppEnterprise::OnSetFocus(wxFocusEvent& event)
{
	// THE FOCUS GONE INTO A FORM BRINGS ITS TAB UP — the desktop's: a click into a tab's window that is not the selected
	// one (the notebook split in two) selects it, and the selection is told to the server as a tab picked is.
	wxWindow* child = dynamic_cast<wxWindow*>(event.GetEventObject());
	while (child != nullptr && !child->IsKindOf(CLASSINFO(wxAuiMDIChildFrame))) {
		child = child->GetParent();
	}

	if (child != nullptr && mainFrame != nullptr) {
		wxAuiMDIClientWindow* client = mainFrame->GetClientWindow();
		const int new_selection = client->FindPage(child);
		if (new_selection != wxNOT_FOUND && new_selection != client->GetSelection())
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
	else if (event.GetEventType() == wxEVT_SET_FOCUS) {
		OnSetFocus(
			static_cast<wxFocusEvent&>(event)
		);
	}

	return Event_Skip;
}
