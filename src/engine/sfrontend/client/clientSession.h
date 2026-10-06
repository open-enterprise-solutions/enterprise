#ifndef __CLIENT_SESSION_H__
#define __CLIENT_SESSION_H__

// THE SESSION OF ONE CLIENT — the registry's row, the runtime, the rights, all of what ibSession is, for a
// person working through the protocol. Its frame (ibClientFrame) is built around its holder and owns it, so
// the session lives exactly as long as the client's frame; the frame tells the session where it is from its
// constructor, since a process holds many clients and has no single main window to ask.

#include "backend/session/session.h"
#include "sfrontend/sfrontend.h"

class ibClientFrame;

class SFRONTEND_API ibClientSession : public ibSession {
public:
	using ibSession::ibSession;   // (std::string, ibSessionKind) ctor

	ibBackendDocFrame* GetFrame() const override;
	ibClientFrame*     GetClientFrame() const { return m_frame; }
	void               SetFrame(ibClientFrame* frame);

	// "Close this session" for a client: the client instance goes — its frame and finally this session's
	// holder, the same chain a desktop window closing takes. Queued, not done here: the caller is usually this
	// session's own worker (a script's EndJob) or the registry's thread, and the teardown drains that worker.
	bool OnClose(bool force) override;

private:
	ibClientFrame* m_frame = nullptr;
};

#endif
