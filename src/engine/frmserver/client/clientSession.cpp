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

	// FORCED — a debugger's Stop, an administrator's kick: the client is TOLD to go (Exit), as the desktop's window is
	// taken down by the same close, and goes as from its own Exit — its logout takes the session. Taken off the host in
	// silence instead, it learnt of it only from the refusal of its next call, if it made one.
	if (force)
		return m_frame->ExitClient(true);

	// Nobody is asked yet whether the client may close (unsaved input, a running report): when it grows that
	// question, it is answered on the !force road, and a refusal returns false from here.
	return m_frame->GetClientInstance()->RequestClose();
}
