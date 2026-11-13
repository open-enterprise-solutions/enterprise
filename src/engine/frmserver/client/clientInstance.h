#ifndef __CLIENT_INSTANCE_H__
#define __CLIENT_INSTANCE_H__

// THE INSTANCE OF ONE CLIENT ON THE SERVER — a person working through the protocol, from a browser, the wx
// renderer or an assistant over MCP, as the server holds them: the id the client comes back by, who logged in,
// when it was last heard from, and its frame. Beside ibApplicationInstance, the instance of a base: the same
// life — start, work, exit. Not a connection (a transport's connection comes and goes; the instance stays, by
// its id) and not the session: the frame owns the session (ibClientFrame), and the instance's life is the
// frame's — the session and its frame (made together, before any login), open (the login, on the session's
// worker), start (the configuration's start), exit (the documents, the configuration's exit, the runtime — on the
// worker — then the frame, which ends the session).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include "core/fstring.h"            // ibString — the address the client called from, as the call carried it
#include "backend/session/session.h"         // ibSession::OpenResult — what Open came to
#include "backend/session/sessionHolder.h"
#include "frmserver/frmserver.h"

#include "protocol/protocol.h"   // ibProtocol::ibLogin::ibMode

class ibApplicationInstance;
class ibClientFrame;
class ibClientSession;

class FRMSERVER_API ibClientInstance {
public:

	// The base is handed in: a process serves several, and the session belongs to one of them. The mode says which
	// application's frame the client gets (ibClientFrame::Create).
	ibClientInstance(ibApplicationInstance* applicationInstance, const ibString& id, const ibString& address,
		ibProtocol::ibLogin::ibMode mode);
	~ibClientInstance();

	// ⭐ THE SESSION AND ITS FRAME FIRST, before any login: the session made in the base (its row shows "logging in"),
	// and at once the frame of the client's mode — the session's way to the person, there before anything of the login
	// runs, so what the login's compile says reaches the frame's error window (BackendError) as a failure of the start
	// does (Max, 2026-11-13: "it must be made at once, ready to pass messages to the other side"). False: no session,
	// or no frame of that mode. Throws (ibCoreException) what refuses a session before any login — another designer at
	// work — so the person is told why, not asked to log in again.
	bool CreateSession();

	// ON THE SESSION'S WORKER, inside the host's Run: the login as `user` — and with it the configuration compiled for
	// the session (ibSession::CompileRoot, at OnAuthenticated). Run, because the error window is a question to the
	// person, and a question is answered only from work the host runs. Not authenticated, or thrown, the frame goes and
	// the session with it.
	ibSession::OpenResult Open(const ibString& user, const ibString& password);

	// ON THE SESSION'S WORKER, after Open: the frame's own parts (OnCreate — the menus by the rights), the mode's start
	// (AllowRun), which may refuse — refused, the frame goes and the session with it — then the mode's own tabs
	// (CreateStartupPage).
	bool Start();

	void OnExit();

	const ibString& Id()   const { return m_id; }
	const ibString& User() const { return m_user; }

	ibClientFrame*   GetFrame() const { return m_frame.get(); }
	ibClientSession* Session() const;

	// The session from CreateSession on — and while the exit tears it down.
	std::shared_ptr<class ibSession> ShareSession() const { return m_session.Share(); }

	// When the client was last heard from — what an idle instance is dropped by.
	std::int64_t LastActiveMs() const;
	void         Touch();

	// THE INSTANCE ASKS TO GO — its session closing (a kick, the configuration's own exit). Not done here: the
	// caller is usually the session's own worker, and the exit drains that worker. The host takes the instance
	// down on its next round (ibClientHost).
	bool RequestClose();
	bool IsCloseRequested() const { return m_closeRequested.load(); }

private:

	ibApplicationInstance* m_applicationInstance;
	ibString               m_id;
	ibString               m_user;
	ibString               m_address;
	ibProtocol::ibLogin::ibMode           m_mode;

	mutable std::mutex m_mutex;          // the activity time
	std::int64_t       m_lastActiveMs;

	// A login and an exit on one instance never overlap — a client coming back racing the removal of its
	// instance. Recursive: the destructor's exit may be reached from inside another.
	std::recursive_mutex m_lifecycleMutex;

	std::atomic<bool> m_closeRequested{ false };

	// The frame owns the session from CreateSession on.
	std::unique_ptr<ibClientFrame> m_frame;
	// Only a watch: it lets the exit hold the session alive while it tears down.
	ibSessionWatch                 m_session;
};

#endif
