#include "debugClient.h"

#include "core/diagnostics/journal.h"

#include <algorithm>

#include <wx/app.h>   // wxTheApp — the window's loop, the worker a reply is handed to

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

// ⭐⭐ THE ONE PLACE A REPLY CHANGES THREADS. The engine's says "later" and its session's worker decides what later
// means; the thin designer has no session, and its window's loop is the worker the desktop designer's session answered
// with — so later is the window's CallAfter.
//
// ⚠ AND IT DOES NOT CARE HOW MANY LISTENERS THERE ARE. One hop, then the fan-out — deferring per listener would hop
// twice for one event.
//
// ⚠ NOT WAITED FOR, on purpose. This is a notification, not a call anybody waits on; waiting would park the socket
// thread on the main loop, which is the opposite of deferring.
void ibDebuggerClient::ibDebuggerClientAdapter::Defer(std::function<void()> call)
{
	if (!call)
		return;

	if (wxTheApp == nullptr)
		return;
	// Not waited for — so what the reply throws is said here, nobody holding its future. And the client may be gone by
	// then — it goes with the frame: a reply that finds no debugger has nobody left to reach (the engine's watched
	// session answered the same "gone").
	wxTheApp->CallAfter([call = std::move(call)]() {
		if (debugClient == nullptr)
			return;
		try {
			call();
		}
		catch (const std::exception& err) {
			ibJournalWarning(wxT("debugger"), wxT("a debugger's reply ended with an exception: %s"), wxString::FromUTF8(err.what()));
		}
		catch (...) {
			ibJournalWarning(wxT("debugger"), wxT("a debugger's reply ended with an exception"));
		}
	});
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
	const struct ibJobRunByteCodeState& state)
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
