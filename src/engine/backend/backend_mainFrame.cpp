#include "backend_mainFrame.h"

IBackendDocMDIFrame* IBackendDocMDIFrame::sm_mainFrame = nullptr;

///////////////////////////////////////////////////////////////////////////

IBackendDocMDIFrame::IBackendDocMDIFrame()
{
	wxASSERT(sm_mainFrame == nullptr);

	if (sm_mainFrame == nullptr) {
		sm_mainFrame = this;
	}
}

IBackendDocMDIFrame* IBackendDocMDIFrame::GetDocMDIFrame()
{
	return sm_mainFrame;
}

///////////////////////////////////////////////////////////////////////////

IBackendDocMDIFrame::~IBackendDocMDIFrame() {

	wxASSERT(sm_mainFrame != nullptr);

	if (sm_mainFrame != nullptr) {
		sm_mainFrame = nullptr;
	}
}

void IBackendDocMDIFrame::RaiseFrame() {
	wxFrame* const mainFrame = GetFrameHandler();
	if (mainFrame != nullptr && mainFrame->IsFocusable()) {
		mainFrame->Iconize(false); // restore the window if minimized
		mainFrame->SetFocus();     // focus on my window
		mainFrame->Raise();        // bring window to front
	}
}
