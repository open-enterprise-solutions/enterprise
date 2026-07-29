#ifndef __WEB_CLIENT_SESSION_H__
#define __WEB_CLIENT_SESSION_H__

// ibWebClientSession — per-tab session class for the wes web frontend.
//
// Mirror of the desktop pair: the tab's main window (ibWebFrame) is
// built around this session's holder and owns it, so the session lives
// exactly as long as the tab's window. The frame registers itself on the
// session (ibSession::GetFrame) from its constructor — no typed storage
// and no casting on either side.
//
// Instantiated only by ibWebSession::Login through
// appData->CreateSession<ibWebClientSession>(presetGuid, address).

#include "backend/session/session.h"

class ibWebClientSession : public ibSession {
public:
	using ibSession::ibSession;   // (std::string, ibSessionKind) ctor

	// This tab's window. Unlike the desktop there is no singleton — wes
	// runs many tabs at once — so the frame tells its session where it
	// is when it takes the holder.
	ibBackendDocFrame* GetFrame() const override;
	void SetFrame(ibWebFrame* frame);

	// Web meaning of "close this session". For wes a forced close on a
	// parked client session means "designer killed debug" → break
	// listen_after_bind through the wfrontend exit hook (svr.stop).
	// Filtered to wfrontendDebugMode() so a regular multi-user wes does
	// not die when one tab gets force-closed.
	bool OnClose(bool force) override;

private:
	ibWebFrame* m_frame = nullptr;
};

#endif
