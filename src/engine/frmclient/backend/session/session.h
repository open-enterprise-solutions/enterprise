#ifndef _FRMCLIENT_BACKEND_SESSION_H__
#define _FRMCLIENT_BACKEND_SESSION_H__

#include "frmclient/backend/backend_mainFrame.h"

// THE SESSION — the engine's ibSession (backend/session/session.h): on the client the process is one person's, and the
// session's frame is its main window (ibBackendDocFrame). The session itself is the server's.
class ibSession {
public:

	static ibSession* Current() {
		static ibSession session;
		return &session;
	}

	ibBackendDocFrame* GetFrame() const { return ibBackendDocFrame::s_frame; }
};

#endif
