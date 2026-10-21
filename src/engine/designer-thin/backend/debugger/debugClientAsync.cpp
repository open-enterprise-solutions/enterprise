#include "debugClient.h"

#include <algorithm>

// ONE EVENT, EVERY BRIDGE. Nothing here decides who cares about what; a bridge
// that is not interested simply does nothing with it.
//
// Written as one macro rather than eleven copies of the same three lines because
// the fan-out is the SAME in every case, and a hand-copied fan-out is where the
// twelfth event quietly reaches only half the list.
//
// ⚠ IT RUNS AFTER THE HOP, not before it: the news is handed to the window's loop once (ibDebuggerClient::Listen), and
// the list is walked here, on that loop. So a listener that RemoveBridge destroyed while the news was in flight is
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

void ibDebuggerClient::ibDebuggerClientAdapter::OnSessionStart()
{
	IB_DEBUG_FANOUT(OnSessionStart());
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnSessionEnd()
{
	IB_DEBUG_FANOUT(OnSessionEnd());
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnEnterLoop(const ibDebugLineData& data)
{
	IB_DEBUG_FANOUT(OnEnterLoop(data));
}

void ibDebuggerClient::ibDebuggerClientAdapter::OnLeaveLoop(const ibDebugLineData& data)
{
	IB_DEBUG_FANOUT(OnLeaveLoop(data));
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
