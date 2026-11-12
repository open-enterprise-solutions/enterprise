////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main child window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameChild.h"

wxIMPLEMENT_CLASS(ibAuiDocChildFrame, ibAuiMDIChildFrame);
wxIMPLEMENT_CLASS(ibDialogDocChildFrame, ibDialog);

#include "mainFrame.h"

ibAuiDocChildFrame::~ibAuiDocChildFrame()
{
	ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();

	if (pParentFrame && ibFrontendMainFrame::GetFrame()) {
		if (pParentFrame->GetActiveChild() == this) {
			pParentFrame->SetActiveChild(nullptr);
			pParentFrame->SetChildMenuBar(nullptr);
		}
		ibAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT(pClientWindow);
		int idx = pClientWindow->GetPageIndex(this);
		if (idx != wxNOT_FOUND) {
			pClientWindow->RemovePage(idx);
		}
	}
}

// ⚠ DELETED WITH THE MODAL IT WAS OPENED OVER — that dialog's child, deleted as one, never closed: its view still shows
// in it. The view lets go of it and closes while it is still here — its focus watch comes off it, the form's close is
// asked of the server, which still has the tab — and goes, and with the last view the document, out of the frame's forms.
ibDialogDocChildFrame::~ibDialogDocChildFrame()
{
	ibFrontendView* const view = GetView();
	if (view == nullptr)
		return;
	view->SetDocChildFrame(nullptr);
	SetView(nullptr);
	view->Close(false);
	delete view;
}

// The others disabled before it is shown, as the progress dialog disables them (wxGenericProgressDialog), and enabled
// again before it is hidden, as a modal enables them (wxDialog::Show): hidden first, Windows activates another window, one
// still enabled. Deleted shown — with the modal it is over — they are enabled as it goes.
bool ibDialogDocChildFrame::Show(bool show)
{
	if (show && m_winDisabler == nullptr)
		m_winDisabler = std::make_unique<wxWindowDisabler>(this);
	else if (!show)
		m_winDisabler.reset();
	return ibDocChildFrameAny::Show(show);
}

void ibAuiDocChildFrame::SetLabel(const wxString& label)
{
	if (label != GetLabel()) {
		ibAuiMDIChildFrame::SetLabel(label);
		ibAuiMDIChildFrame::SetTitle(label);
	}
}
