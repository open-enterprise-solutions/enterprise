#include "debugClientBridge.h"
#include "debugClient.h"

void SetDebuggerClientBridge(IDebuggerClientBridge* bridge) {
	if (debugClient != nullptr) {
		debugClient->SetBridge(bridge);
	}
}
