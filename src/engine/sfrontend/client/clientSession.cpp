#include "clientSession.h"
#include "clientFrame.h"
#include "clientInstance.h"

ibBackendDocFrame* ibClientSession::GetFrame() const
{
	return m_frame;
}

void ibClientSession::SetFrame(ibClientFrame* frame)
{
	m_frame = frame;
}

bool ibClientSession::OnClose(bool force)
{
	// No frame — the login failed before there was one: nothing to take down but the session itself.
	if (m_frame == nullptr || m_frame->GetClientInstance() == nullptr)
		return ibSession::OnClose(force);

	// Nobody is asked yet whether the client may close (unsaved input, a running report): when it grows that
	// question, it is answered on the !force road, and a refusal returns false from here.
	return m_frame->GetClientInstance()->RequestClose();
}
