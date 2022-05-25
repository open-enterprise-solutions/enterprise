////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h" 
#include "metadata/metadata.h"
#include "common/docManager.h"

bool CMainFrame::ProcessEvent(wxEvent &event)
{
	// stops the same event being processed repeatedly
	if (m_pLastEvt == &event)
		return false;

	m_pLastEvt = &event;

	// let the active child (if any) process the event first.
	bool result = false;

	wxAuiMDIChildFrame* pActiveChild = GetActiveChild();

	if (pActiveChild &&
		event.IsCommandEvent() &&
		event.GetEventObject() != m_pClientWindow &&
		!(event.GetEventType() == wxEVT_ACTIVATE ||
			event.GetEventType() == wxEVT_SET_FOCUS ||
			event.GetEventType() == wxEVT_KILL_FOCUS ||
			event.GetEventType() == wxEVT_CHILD_FOCUS ||
			event.GetEventType() == wxEVT_COMMAND_SET_FOCUS ||
			event.GetEventType() == wxEVT_COMMAND_KILL_FOCUS)
		)
	{
		result = pActiveChild->GetEventHandler()->ProcessEvent(event);
	}

	if (!result) {

		bool designerMode = appData->DesignerMode();

		bool cancel = false;
		bool saveMetadata = false;

		// if the event was not handled this frame will handle it,
		// which is why we need the protection code at the beginning
		// of this method

		if (event.GetEventType() == wxEVT_CLOSE_WINDOW) {
			wxCloseEvent &closeEvent = static_cast<wxCloseEvent &>(event);
			if (!closeEvent.GetVeto()) {
				if (metadata->IsModified()) {
					int answer = wxMessageBox("Configuration '" + metadata->GetMetadataName() + "' has been changed. Save?", wxT("Save project"), wxYES | wxNO | wxCANCEL | wxCENTRE | wxICON_QUESTION, this);
					if (answer == wxYES) {
						saveMetadata = true;
					}
					else if (answer == wxCANCEL) {
						closeEvent.Veto();
					}
					else {
						cancel = true; 
					}
				}

				if (!designerMode) {
					if (!closeEvent.GetVeto()) {
						if (!metadata->CloseMetadata()) {
							closeEvent.Veto();
						}
					}
					else {
						closeEvent.Veto();
					}
				}
			}
			
			if (cancel) {
				std::exit(0);
			}
			else if (!closeEvent.GetVeto()) {
				if (wxEvtHandler::ProcessEvent(event)) {
					if (saveMetadata && !closeEvent.GetVeto()) {
						metadata->SaveMetadata(saveConfigFlag);
					}
					result = true;
				}
			}

			if (designerMode) {
				if (!closeEvent.GetVeto()) {
					if (!metadata->CloseMetadata()) {
						closeEvent.Veto();
					}
				}
			}
		}
		else if (wxEvtHandler::ProcessEvent(event)) {
			result = true;
		}
	}

	m_pLastEvt = NULL;

	return result;
}

//********************************************************************************
//*                                    System                                    *
//********************************************************************************

void CMainFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	this->Close();
}

void CMainFrame::OnCloseWindow(wxCloseEvent& event)
{
	bool allowClose = true;//docManager->Clear(!event.CanVeto());

	// The user decided not to close finally, abort.
	if (allowClose) event.Skip();
	// Just skip the event, base class handler will destroy the window.
	else event.Veto();
}