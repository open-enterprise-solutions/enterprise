////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h" 

//********************************************************************************
//*                                    System                                    *
//********************************************************************************

void wxAuiDocMDIFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	this->Close();
}

void wxAuiDocMDIFrame::OnCloseWindow(wxCloseEvent& event)
{
	bool allowClose = event.CanVeto() ? 
		AllowClose() : true;
	// The user decided not to close finally, abort.
	if (allowClose) event.Skip();
	// Just skip the event, base class handler will destroy the window.
	else event.Veto();
}