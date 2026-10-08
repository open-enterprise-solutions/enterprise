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

int ibAppEnterprise::FilterEvent(wxEvent& event)
{
	if (event.GetEventType() == wxEVT_KEY_DOWN) {
		OnKeyEvent(
			static_cast<wxKeyEvent&>(event)
		);
	}

	return Event_Skip;
}
