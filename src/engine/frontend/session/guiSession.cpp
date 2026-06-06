#include "guiSession.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/win/dlgs/authorization.h"

#include "backend/appData.h"
#include "backend/session/sessionRegistry.h"

#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/frame.h>

ibGUISession::~ibGUISession()
{
	// Clear the back-link if the frame is still around. We intentionally
	// DO NOT delete the frame here — wx widgets must be torn down on the
	// main thread, and ~ibGUISession can run on the registry worker
	// thread if the last shared_ptr drop happens there (force-exit path).
	// The graceful path runs OnDestroySession first and clears m_frame
	// before the dtor ever sees it.
	if (m_frame != nullptr) m_frame->SetGUISession(nullptr);
}

ibBackendDocFrame* ibGUISession::GetFrame() const
{
	return m_frame;
}

void ibGUISession::AttachFrame(ibFrontendMainFrame* frame)
{
	m_frame = frame;
	if (m_frame == nullptr) return;

	// Two-way link: frame knows its owning session, session knows its
	// frame. Keeps frame event handlers reaching per-session runtime
	// (module manager, ProcUnit map, ibValueSystemFunction once it goes
	// non-static) without detouring through wxApp singletons.
	frame->SetGUISession(this);

	// Bind ibSession* on the frame too, so legacy GetSession() (used by
	// AllowRun/AllowClose and session-aware UI) resolves immediately —
	// Initialize() is otherwise a separate explicit step in mainApp.
	frame->Initialize(this);

	// Register the singleton ibFrontendMainFrame::s_instance so legacy
	// `backend_mainFrame` macro / ibFrontendMainFrame::GetFrame() keep
	// resolving to the same window that session now owns.
	ibFrontendMainFrame::InitFrame(frame);

	// Reload listener — admin's "reload" signal on this session's
	// sys_session row fires NotifyReload from the registry worker.
	// Hop to the main thread, notify the user, close the frame.
	// Process exits through wxApp's normal shutdown.
	ibSession* sessSelf = this;
	auto* reg = ibApplicationData::GetSessionRegistry();
	if (reg == nullptr) return;
	reg->OnReload([sessSelf](ibSession* target) {
		if (target != sessSelf) return;
		if (wxTheApp == nullptr) return;
		wxTheApp->CallAfter([]() {
			wxMessageBox(_("Session reloaded by an administrator. The application will close - please re-open it from the launcher."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_INFORMATION);
			if (auto* frame = ibFrontendMainFrame::GetFrame())
				frame->Close(true);
		});
	});
}

bool ibGUISession::OnShowAuthenticate(const wxString& user, const wxString& password)
{
	// Desktop dialog lives as a standalone free function — no frame
	// dependency on the call path. Dialog's OK handler runs under the
	// main-thread ibSessionScope bound to this session, so the InstallUser
	// it triggers writes m_userInfo / m_sessionRawPassword on this session
	// directly. Authenticate then re-submits Attach with those validated creds.
	return ibPromptAuthenticationDialog(user, password);
}

void ibGUISession::OnForceExit()
{
	// Deferred wx exit — CallAfter so the trigger thread (could be the
	// script thread that just hit a force-exit instruction) doesn't
	// drain the wx loop while still on its callstack. Mirrors the
	// process-level path that ibApplicationData::ForceExit used to
	// follow when no exit hook was installed.
	if (wxTheApp != nullptr)
		wxTheApp->CallAfter([]() { wxTheApp->Exit(); });
}

bool ibGUISession::OnDestroySession(bool force)
{
	if (m_frame == nullptr) return true;

	// Soft-close path mirrors the wx [X]-button path: AllowClose runs
	// the same hook that fires from wxEVT_CLOSE_WINDOW, so EndJob(false)
	// from script and the user clicking [X] go through the same veto:
	//   - enterprise: ExitMainModule -> BeforeExit / OnExit script events
	//   - designer:   unsaved-configuration confirmation dialog
	// A veto (return false) propagates up through ibSession::Close,
	// which short-circuits without submitting Remove — the session
	// stays Added and the frame keeps running. force=true skips this
	// path entirely; used by debug Destroy, admin Kick, and shutdown
	// flows where the close cannot be cancelled.
	//
	// AllowClose touches GUI state (script run via ProcUnit, dialogs);
	// only safe on main thread. When force=true, callers (debug kill,
	// admin kick) hit this from worker threads — skip the soft path,
	// the destroy itself is marshalled below.
	if (!force) {
		if (!wxIsMainThread())
			return false;
		if (!m_frame->AllowClose())
			return false;
	}

	// Marshal teardown to the main wx thread. Debug-listener kill and
	// admin-kick reach Close(true) on background threads; touching
	// wxFrame::Destroy / Freeze / docManager from anywhere but main
	// crashes wx (its window/list state isn't synchronised across
	// threads). shared_from_this keeps the session alive until the
	// lambda fires — Close() returns true to caller and ProcessRemove
	// may run concurrently, but the frame teardown is sequenced on
	// the main loop.
	auto* frame = m_frame;
	m_frame = nullptr;
	frame->SetGUISession(nullptr);

	auto runDestroy = [frame]() {
		frame->Destroy();
	};

	if (wxIsMainThread() || wxTheApp == nullptr) {
		runDestroy();
	}
	else {
		wxTheApp->CallAfter(runDestroy);
	}
	return true;
}

bool ibGUISession::ShowFrame()
{
	// Frameless variant of a GUI session shouldn't happen in practice
	// (designer / enterprise both AttachFrame in OnCreateSession), but
	// keep it as a tolerant no-op success rather than UAF.
	if (m_frame == nullptr || ibSession::IsCurrentForceExit())
		return true;
	if (m_frame->IsShown())
		return true;
	m_frame->CreateGUI();
	m_frame->SetClientSize(m_frame->FromDIP(wxSize(800, 600)));
	m_frame->SetFocus();
	m_frame->Center();
	if (!m_frame->Show())
		return false;
	m_frame->Raise();
	return true;
}
