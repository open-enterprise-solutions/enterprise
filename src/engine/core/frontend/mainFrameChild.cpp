////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main child window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameChild.h"

wxIMPLEMENT_CLASS(wxAuiMDIDocChildFrame, wxAuiMDIChildFrame);
wxIMPLEMENT_CLASS(wxDialogDocChildFrame, wxDialog);

#include "mainFrame.h"

wxAuiMDIDocChildFrame::~wxAuiMDIDocChildFrame()
{
	wxAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();

	if (pParentFrame && wxAuiDocMDIFrame::GetFrame()) {
		if (pParentFrame->GetActiveChild() == this) {
			pParentFrame->SetActiveChild(NULL);
			pParentFrame->SetChildMenuBar(NULL);
		}
		wxAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT(pClientWindow);
		int idx = pClientWindow->GetPageIndex(this);
		if (idx != wxNOT_FOUND) {
			pClientWindow->RemovePage(idx);
		}
	}
}

void wxAuiMDIDocChildFrame::SetLabel(const wxString& label)
{
	wxAuiMDIChildFrame::SetLabel(label);
	wxAuiMDIChildFrame::SetTitle(label);
}