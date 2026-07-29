#include "guiSession.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/win/dlgs/authorization.h"

#include <wx/app.h>
#include <wx/frame.h>

ibGUISession::~ibGUISession() = default;

bool ibGUISession::OnShowAuthenticate(const wxString& user, const wxString& password)
{
	// Desktop dialog lives as a standalone free function — no frame
	// dependency on the call path. Dialog's OK handler runs under the
	// main-thread ibSessionScope bound to this session, so the InstallUser
	// it triggers writes m_userInfo / m_sessionRawPassword on this session
	// directly. Authenticate then re-submits Attach with those validated creds.
	return ibPromptAuthenticationDialog(user, password);
}

ibBackendDocFrame* ibGUISession::GetFrame() const
{
	return ibFrontendMainFrame::GetFrame();
}

bool ibGUISession::OnClose(bool force)
{
	// The one closing sequence for a desktop session. Both roads arrive
	// here: the user's [X] (the window vetoes the wx close and forwards
	// to us) and a backend Close() with no window event at all.
	//
	// No window (startup failed, or it is already gone) — nothing to
	// close, so the session ends the windowless way instead of waiting
	// for a holder release that will never come.
	auto* frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return ibSession::OnClose(force);

	// wx may only be touched from the main thread. A forced close often
	// arrives from elsewhere (script worker unwinding, admin kick), so the
	// sequence is deferred onto the main loop; nobody is waiting on the
	// answer in that case.
	if (!wxIsMainThread()) {
		if (wxTheApp != nullptr) {
			auto self = shared_from_this();
			wxTheApp->CallAfter([self]() { self->Close(true); });
		}
		return true;
	}

	// Hand it to the window's own close. Everything the close consists of —
	// asking the documents, running BeforeExit / OnExit, destroying the
	// window — happens inside there, in the one place that also serves the
	// user's [X]. Nothing about closing is decided here, which is why this
	// function has no logic of its own.
	//
	// The answer comes back for free: a veto inside makes Close return
	// false, and that is exactly what our caller needs to hear.
	return frame->Close(force);
}

