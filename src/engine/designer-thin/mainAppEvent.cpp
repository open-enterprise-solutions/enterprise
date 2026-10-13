////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/mainFrameChild.h"

void ibAppDesigner::OnSetFocus(wxFocusEvent& event)
{
	// THE FOCUS GONE INTO A TAB BRINGS IT UP — the desktop designer's: a click into a tab's window that is not the
	// selected one (the notebook split in two) selects it, and the selection is told to the server as a tab picked is.
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

	event.Skip();
}

int ibAppDesigner::FilterEvent(wxEvent& event)
{
	if (event.GetEventType() == wxEVT_SET_FOCUS) {
		OnSetFocus(
			static_cast<wxFocusEvent&>(event)
		);
	}

	return Event_Skip;
}
