////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main child window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameChild.h"

wxIMPLEMENT_CLASS(CAuiDocChildFrame, wxAuiMDIChildFrame);
wxIMPLEMENT_CLASS(CDialogDocChildFrame, wxDialog);

#include "mainFrame.h"

CAuiDocChildFrame::~CAuiDocChildFrame()
{
	wxAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();

	if (pParentFrame && CDocMDIFrame::GetFrame()) {
		if (pParentFrame->GetActiveChild() == this) {
			pParentFrame->SetActiveChild(nullptr);
			pParentFrame->SetChildMenuBar(nullptr);
		}
		wxAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT(pClientWindow);
		int idx = pClientWindow->GetPageIndex(this);
		if (idx != wxNOT_FOUND) {
			pClientWindow->RemovePage(idx);
		}
	}
}

void CAuiDocChildFrame::SetLabel(const wxString& label)
{
	wxAuiMDIChildFrame::SetLabel(label);
	wxAuiMDIChildFrame::SetTitle(label);
}