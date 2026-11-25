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

	// NOT FORCED — a script's EndJob(False), an assistant's app_close: the person's [X], arriving from the engine. The
	// client is told to close and asks what it keeps, as from its own window; taken off the host in silence instead, a
	// studio's unsaved application was lost without the question its window asks. The answer is the person's, later.
	m_frame->CloseClient();
	return true;
}
