#ifndef _FRMCLIENT_BACKEND_MAIN_FRAME_H__
#define _FRMCLIENT_BACKEND_MAIN_FRAME_H__

#include "frmclient/frmclient.h"

class ibPropertyObject;

// THE WINDOW AN ENGINE OBJECT ASKS — the engine's ibBackendDocFrame (backend/backend_mainFrame.h), as the client's
// objects ask it: the property slot, what its inspector shows. The client's main window is the one (as the desktop's
// is), and it is the session's frame while it lives (ibSession::GetFrame).
class FRMCLIENT_API ibBackendDocFrame {
public:

	ibBackendDocFrame() { s_frame = this; }
	virtual ~ibBackendDocFrame() { if (s_frame == this) s_frame = nullptr; }

	virtual ibPropertyObject* GetProperty() const { return nullptr; }
	virtual bool SetProperty(ibPropertyObject* WXUNUSED(prop)) { return false; }

private:

	friend class ibSession;

	inline static ibBackendDocFrame* s_frame = nullptr;
};

#endif
