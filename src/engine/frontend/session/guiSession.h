#ifndef __IB_GUI_SESSION_H__
#define __IB_GUI_SESSION_H__

// ibGUISession — desktop GUI session base, shared by enterprise.exe and
// designer.exe.
//
// It does NOT own or build a window. The relationship runs the other
// way: the main window is built around this session's holder and owns
// it, so the session lives exactly as long as the window and dies with
// it. What remains here is the part that cannot belong to a window —
// the login prompt, which runs before any window exists — plus the
// desktop meaning of "this session must stop now".

#include "frontend/frontend.h"
#include "backend/session/session.h"

class FRONTEND_API ibGUISession : public ibSession {
public:
	// Inherit (std::string, ibSessionKind) ctor so the registry factory
	// (make_shared<T>(id, kind)) works across every derived session class
	// without boilerplate.
	using ibSession::ibSession;

	~ibGUISession() override;

	// Shared interactive auth for designer + enterprise: the standalone
	// ibDialogAuthentication (via ibPromptAuthenticationDialog). Lives on
	// the session, not on a window, because it runs BEFORE the window
	// exists — that is what makes "authenticate first, then build the
	// form around the holder" possible.
	bool OnShowAuthenticate(const wxString& user, const wxString& password) override;

	// The desktop main window is a process singleton, so there is nothing
	// to store: both of these answer from ibFrontendMainFrame::GetFrame().
	ibBackendDocFrame* GetFrame() const override;

	// What closing means for a desktop session: close the main window.
	// The window then runs its own close path — which knows nothing
	// about sessions — and its destruction releases the holder.
	bool OnClose(bool force) override;
};

#endif
