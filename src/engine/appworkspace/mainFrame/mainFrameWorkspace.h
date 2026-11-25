#ifndef _MAINFRAME_WORKSPACE_H__
#define _MAINFRAME_WORKSPACE_H__

#include "frmclient/mainFrame/mainFrame.h"

// THE RUNTIME'S MAIN WINDOW — the desktop's ibFrontendMainFrameWorkspace (enterprise/mainFrame) on the thin client.
// The base draws what both modes share — the menu, the tabs, the messages; this one logs in to the runtime and shows
// its own schema, All functions.
class ibFrontendMainFrameWorkspace : public ibFrontendMainFrame {
public:

	// The window is built around a communicator, and owns it from that moment on; it logs in to the runtime as it is
	// shown.
	ibFrontendMainFrameWorkspace(std::unique_ptr<ibCommunicator> communicator,
		const wxString& user, const wxString& password,
		const wxString& title = _("OES Application Workspace"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

protected:

	// The section panel — the server's sections, once the start has run.
	virtual void CreateStartupPage() override;

	virtual void ShowSchema(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown) override;

	// The section panel on the left (mainFrameWorkspaceInterface.cpp), as the desktop's window docks it.
	void CreateSubSystem();
};

#endif
