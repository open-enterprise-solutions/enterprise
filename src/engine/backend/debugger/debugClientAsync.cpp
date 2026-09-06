#include "debugClient.h"

#include "backend/session/session.h"   // whose worker a reply is handed to

#include <algorithm>

// ONE EVENT, EVERY BRIDGE. Nothing here decides who cares about what; a bridge
// that is not interested simply does nothing with it.
//
// Written as one macro rather than eleven copies of the same three lines because
// the fan-out is the SAME in every case, and a hand-copied fan-out is where the
// twelfth event quietly reaches only half the list.
//
// ⚠ IT RUNS AFTER THE HOP, not before it: CallAfter defers ONE call, and the list is walked here, on
// the delivery thread. So a listener that RemoveBridge destroyed while a reply was in flight is
// simply not in the list — where deferring per listener would have had to capture a pointer to one.
#define IB_DEBUG_FANOUT(call)                                                   \
	do {                                                                        \
		for (const std::unique_ptr<ibDebuggerClientBridge>& bridge : m_debugBridges) \
			if (bridge) bridge->call;                                           \
	} while (false)

void ibDebuggerClient::ibDebuggerClientAdapter::AddBridge(ibDebuggerClientBridge* bridge)
{
	if (bridge == nullptr)
		return;

	// Adding the same bridge twice would deliver every event twice, which reads
	// downstream as the runtime having stopped twice.
	const auto found = std::find_if(m_debugBridges.begin(), m_debugBridges.end(),
		[bridge](const std::unique_ptr<ibDebuggerClientBridge>& held) {
			return held.get() == bridge;
		});

	if (found == m_debugBridges.end())
		m_debugBridges.emplace_back(bridge);
}

// ⭐ WHOSE WORKER, TAKEN AT BIRTH. The debug client — and so this — is built from the registry's
// first-connect notification, which is a session being connected: it exists and it is the current
// one. Later there is no such moment, because replies arrive on the socket thread, where nothing is
// bound at all.
ibDebuggerClient::ibDebuggerClientAdapter::ibDebuggerClientAdapter()
	: m_session(ibSession::Current())
{
}

// ⭐⭐ THE ONE PLACE A REPLY CHANGES THREADS. It used to be a wx event queued at this class, which is
// why this class was a wxEvtHandler and why the debug transport knew about a GUI at all. Now it says
// "later" and the WORKER decides what later means: the desktop session's pool is `wxTheApp::CallAfter`
// made into an object, a web client's is that session's FIFO worker, and a headless host has none —
// so `Submit` runs the task where it stands and fulfils the future before returning (session.h).
//
// ⚠ NOTHING HERE ASKS WHICH THREAD IT IS ON, and nothing here is entitled to: the pool answers that,
// and it is the only one that can answer it for all three hosts at once.
//
// ⚠ THE FUTURE IS DROPPED ON PURPOSE. This is a notification, not a call somebody waits on — holding
// the future would make every reply block the socket thread on the main loop, which is the opposite
// of what deferring is for.
// ⭐⭐ THE ONE PLACE A REPLY CHANGES THREADS, and the whole of what changed here. It used to be a wx
// event queued at this class — which is why this class was a wxEvtHandler, and why the debug
// TRANSPORT knew about a GUI at all. Now it says "later" and the WORKER decides what later means:
// the desktop session's pool is `wxTheApp::CallAfter` made into an object, a web client's is that
// session's FIFO worker, and a headless host has none — so Submit runs the task where it stands and
// fulfils the future before returning (session.h). *"One question, one door, three honest answers."*
//
// ⚠ NOTHING HERE ASKS WHICH THREAD IT IS ON, and nothing here is entitled to: the pool is the only
// one that can answer that for all three hosts at once.
//
// ⚠ AND IT DOES NOT CARE HOW MANY LISTENERS THERE ARE. One hop, then the fan-out — the shape the wx
// version had. Deferring per listener would submit to the same pool twice for one event, since every
// bridge in a process belongs to the same session.
//
// ⚠ THE FUTURE IS DROPPED ON PURPOSE. This is a notification, not a call anybody waits on; holding
// the future would park the socket thread on the main loop, which is the opposite of deferring.
//
// ⭐⭐ AND TODAY THIS CHANGES NOTHING, WHICH IS WHY THE REASON HAS TO BE WRITTEN DOWN. The designer's
// session answers with ibWorkerPoolGUI, so a reply still lands on the wx main loop — exactly where
// the wx event put it. What moved is WHO DECIDES: the pool, not the transport.
//
// The point of that is ahead of it (Max, 2026-09-06): to get this off the main thread, and to have
// it already be off it the day there is a COMPUTE SERVER. workerPool.h names that host — the
// headless pool serves "wenterprise-server.exe and the future oes-server.exe compute server" — and
// on the day a session answers with one, debug replies stop touching the main loop and NOTHING HERE
// CHANGES. That is the whole return on a change that is behaviour-neutral now, and it is invisible
// from the code: whoever finds this later and sees a hop that lands where it started will be right
// about what it does and wrong about what it is for.
//
// ⭐ THE MAIN LOOP IN THE PATH TODAY IS THE HOST, NOT THE DESIGN. Debugging for the compute server
// receives through brokers and hands the information to the assistant — no window is in that path at
// all. The wx loop is in this one only because the process holding the MCP server also happens to
// draw. So the hop is not something to optimise away later: it stops existing when the host does,
// provided nobody has hard-wired it in the meantime. That wiring is what came out here.
void ibDebuggerClient::ibDebuggerClientAdapter::Defer(std::function<void()> call)
{
	if (!call)
		return;

	// No session bound yet — nothing has installed a listener — so there is no other thread to hand
	// this to, and running it here is the only honest answer.
	if (m_session != nullptr)
		m_session->Submit(std::move(call));
	else
		call();
}

void ibDebuggerClient::ibDebuggerClientAdapter::RemoveBridge(ibDebuggerClientBridge* bridge)
{
	// Erasing destroys it — the list owns what it holds.
	m_debugBridges.erase(
		std::remove_if(m_debugBridges.begin(), m_debugBridges.end(),
			[bridge](const std::unique_ptr<ibDebuggerClientBridge>& held) {
				return held.get() == bridge;
			}),
		m_debugBridges.end());
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSessionStart(wxSocketClient* sock)
{
	IB_DEBUG_FANOUT(OnSessionStart(sock));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSessionEnd(wxSocketClient* sock)
{
	IB_DEBUG_FANOUT(OnSessionEnd(sock));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	IB_DEBUG_FANOUT(OnEnterLoop(sock, data));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	IB_DEBUG_FANOUT(OnLeaveLoop(sock, data));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnAutoComplete(const ibDebugAutoCompleteData& data)
{
	IB_DEBUG_FANOUT(OnAutoComplete(data));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnMessageFromServer(const ibDebugLineData& data, const wxString& message)
{
	IB_DEBUG_FANOUT(OnMessageFromServer(data, message));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetToolTip(const ibDebugExpressionData& data, const wxString& strResult)
{
	IB_DEBUG_FANOUT(OnSetToolTip(data, strResult));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetStack(const ibStackData& data)
{
	IB_DEBUG_FANOUT(OnSetStack(data));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnScreenshot(const wxMemoryBuffer& png, const wxString& focus)
{
	IB_DEBUG_FANOUT(OnScreenshot(png, focus));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnEvalMessage(const wxString& message, MessageType type)
{
	IB_DEBUG_FANOUT(OnEvalMessage(message, type));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSandboxResult(bool ran, const wxString& answer,
	const wxString& json, wxLongLong_t microseconds)
{
	IB_DEBUG_FANOUT(OnSandboxResult(ran, answer, json, microseconds));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnJobState(unsigned int which,
	const ibJobRunByteCodeState& state)
{
	IB_DEBUG_FANOUT(OnJobState(which, state));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnComposed(bool answered, const wxString& refusal,
	const wxMemoryBuffer& result)
{
	IB_DEBUG_FANOUT(OnComposed(answered, refusal, result));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetLocalVariable(const ibLocalWindowData& data)
{
	IB_DEBUG_FANOUT(OnSetLocalVariable(data));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetVariable(const ibWatchWindowData& data)
{
	IB_DEBUG_FANOUT(OnSetVariable(data));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSetExpanded(const ibWatchWindowData& data)
{
	IB_DEBUG_FANOUT(OnSetExpanded(data));
}

#undef IB_DEBUG_FANOUT
