#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "frmclient/mainFrame/mainFrame.h"

// The syntax helper's pane and commands — the desktop's (frontend/mainFrame/mainFrame.h), the designer's alone here.
#define wxAUI_PANE_HELP     wxT("syntaxHelperWindow")

enum {
	wxID_FRONTEND_SYNTAX_HELPER        = wxID_HIGHEST + 4500,
	wxID_FRONTEND_SYNTAX_HELPER_LOOKUP = wxID_HIGHEST + 4501,

	// Help → About — the desktop's (designer/mainFrame/mainFrameDesigner.h), the server's About schema.
	wxID_DESIGNER_ABOUT                = wxID_HIGHEST + 4502,
};

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

	// Shown — logged in — and the person's assistant access started with it when they switched it on (the desktop's).
	virtual bool Show(bool show = true) override;

protected:

	// The designer's Users and Options — the desktop's dialogs (Administration → Users, Tools → Options) — over the
	// base's schemas.
	virtual void ShowSchema(ibProtocolSchema schema, const ibProtocolNode& shown) override;

	// The designer's Help — the syntax helper's toggle — over the base's File and Edit (the desktop's).
	virtual void InitializeDefaultMenu() override;
	// …and its key over the base's.
	virtual void SetDefaultHotKeys() override;

private:

	// THE SYNTAX HELPER — the desktop's pane on the right, made the first time it is asked for.
	void EnsureHelpPane();
	void ToggleHelpPane();

	class ibHelpPaneView* m_helpPane = nullptr;
};

#endif
