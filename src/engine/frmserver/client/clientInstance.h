#ifndef __CLIENT_INSTANCE_H__
#define __CLIENT_INSTANCE_H__

// THE INSTANCE OF ONE CLIENT ON THE SERVER — a person working through the protocol, from a browser, the wx
// renderer or an assistant over MCP, as the server holds them: the id the client comes back by, who logged in,
// when it was last heard from, and its frame. Beside ibApplicationInstance, the instance of a base: the same
// life — start, work, exit. Not a connection (a transport's connection comes and goes; the instance stays, by
// its id) and not the session: the frame owns the session (ibClientFrame), and the instance's life is the
// frame's — log in (a session in its base, opened on its own worker), start (the frame, then the
// configuration's start), exit (the documents, the configuration's exit, the runtime — on the worker — then
// the frame, which ends the session).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include <wx/string.h>

#include "core/fileSystem/types.h"   // s32
#include "backend/session/sessionHolder.h"
#include "frmserver/frmserver.h"

#include "protocol/protocol.h"   // ibProtocolMode

class ibApplicationInstance;
class ibClientFrame;
class ibClientSession;

class FRMSERVER_API ibClientInstance {
public:

	// The base is handed in: a process serves several, and the session belongs to one of them. The mode says which
	// application's frame the client gets (ibClientFrame::Create).
	ibClientInstance(ibApplicationInstance* applicationInstance, const wxString& id, const wxString& address,
		ibProtocolMode mode);
	~ibClientInstance();

	// Opens a session in the base as `user`, on the session's own worker. False: refused (the reason is
	// journalled where it happened).
	bool Login(const wxString& user, const wxString& password);

	// ON THE SESSION'S WORKER, after Login: the frame of the client's mode first — a start may already open forms or
	// ask the person something, and the client reaches both through it — then the mode's start (AllowRun), which may
	// refuse; refused, the frame goes and the session with it. Then the mode's own tabs (CreateStartupPage). A
	// separate step because the start may wait for the person, and the client has to be told its id before it can
	// answer.
	bool Start();

	void OnExit();

	const wxString& Id()   const { return m_id; }
	const wxString& User() const { return m_user; }

	ibClientFrame*   GetFrame() const { return m_frame.get(); }
	ibClientSession* Session() const;

	// The session from the login on — before the frame takes it, and while the exit tears it down.
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
	wxString               m_id;
	wxString               m_user;
	wxString               m_address;
	ibProtocolMode           m_mode;

	mutable std::mutex m_mutex;          // the activity time
	std::int64_t       m_lastActiveMs;

	// A login and an exit on one instance never overlap — a client coming back racing the removal of its
	// instance. Recursive: the destructor's exit may be reached from inside another.
	std::recursive_mutex m_lifecycleMutex;

	std::atomic<bool> m_closeRequested{ false };

	// The opened session between Login and Start — the frame takes it from here and owns it from then on.
	ibSessionHolder                m_holder;
	std::unique_ptr<ibClientFrame> m_frame;
	// Only a watch: it lets the exit hold the session alive while it tears down.
	ibSessionWatch                 m_session;
};

#endif
