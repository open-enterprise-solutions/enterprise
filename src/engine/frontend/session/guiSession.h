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
#include "frontend/session/workerPoolGUI.h"   // the desktop's answer to "where does this run"

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

	// WHERE THIS SESSION'S WORK RUNS. The desktop answer is "the wx main thread",
	// because a form handler touches widgets and wx tolerates that from one
	// thread only — and ibWorkerPoolGUI is precisely that answer made into an
	// object. The backend never names it: it asks ibSession::GetWorkerPool() and
	// gets an ibWorkerPool*, so the same Submit means "the wx main thread" here,
	// "this session's FIFO worker" under the web server, and "right here" where
	// no view is watching. One question, one door, three honest answers.
	//
	// Nothing about today's behaviour changes by holding one: the GUI pool runs a
	// task INLINE when the caller is already on the main thread, which every
	// existing desktop caller is. What it adds is the case that had no answer
	// before — a background read handing its result back to the session that
	// asked for it, from another thread.
	//
	// Held per session rather than on the registry: the registry's pool serves the
	// background and scheduled sessions of this process, whose entire purpose is
	// to stay OFF the UI thread. Two different questions.
	class ibWorkerPool* GetWorkerPool() const override;

private:
	// mutable: GetWorkerPool is const (it answers a question about the session,
	// it does not change it), but the pool it hands out is used to run work.
	mutable ibWorkerPoolGUI m_workerPool;
};

#endif
