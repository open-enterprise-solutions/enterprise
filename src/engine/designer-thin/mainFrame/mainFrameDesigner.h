#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "frmclient/mainFrame/mainFrame.h"

// THE DESIGNER'S MAIN WINDOW — the desktop's ibFrontendMainFrameDesigner (designer/mainFrame) on the thin client.
// The base draws what both modes share — the menu, the tabs, the messages; this one logs in to the designer, whose
// configuration the server's session holds (ibClientFrameDesigner there).
class ibFrontendMainFrameDesigner : public ibFrontendMainFrame {
public:

	// The window is built around a communicator, and owns it from that moment on; it logs in to the designer as it is
	// shown.
	ibFrontendMainFrameDesigner(std::unique_ptr<ibCommunicator> communicator,
		const wxString& user, const wxString& password,
		const wxString& title = _("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);
};

#endif
