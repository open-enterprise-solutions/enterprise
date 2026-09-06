#include "debugClientBridge.h"
#include "debugClient.h"

#include "backend/backend_exception.h"

// MINTED HERE AND BY NOBODY ELSE — see GetBridgeId. A bridge cannot be constructed without one, and
// two cannot be given the same.
ibDebuggerClientBridge::ibDebuggerClientBridge()
	: m_bridgeId(ibGuid(ibGuid::newGuid()).str())
{
}

void ibDebuggerClientBridge::SetDebuggerClientBridge(ibDebuggerClientBridge* bridge) {
	// Both nullptr cases are bugs to surface, not states to silently
	// tolerate. Old behaviour was to no-op when debugClient was null —
	// that hid an ordering bug where the IDE wired the bridge before
	// metadataCreate had constructed debugClient, so OnEnterLoop /
	// OnSessionStart fired into the void and F5 looked broken.
	if (bridge == nullptr)
		ibBackendCoreException::Error(_("SetDebuggerClientBridge: bridge is null"));
	if (debugClient == nullptr)
		ibBackendCoreException::Error(_("SetDebuggerClientBridge: debugClient is null (call after metadataCreate)"));
	debugClient->SetBridge(bridge);
}
