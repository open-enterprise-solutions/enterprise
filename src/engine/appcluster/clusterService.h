#ifndef __CLUSTER_SERVICE_H__
#define __CLUSTER_SERVICE_H__

// THE CLUSTER AS A SERVICE OF THE SYSTEM — Windows' service manager: installed to start with the system
// (`appcluster --install`), run under it (`--service`), removed (`--uninstall`). The cluster does the same work either
// way; the manager only starts it and asks it to stop. Elsewhere a systemd unit runs the cluster as it is
// (`appcluster --service`), and SIGTERM stops it: there is nothing to connect to.

#include <functional>

#include "core/fstring.h"

namespace ibClusterService {

	// Installed — `"<this program>" --service --dir="<folder>"`, started with the system as LocalSystem — or removed,
	// stopped first. False, with the reason.
	bool Install(const ibString& folder, ibString& error);
	bool Uninstall(ibString& error);

	// UNDER THE MANAGER: its connection kept on a thread of its own, and `stop` called there when it asks the service to
	// stop. False, with the reason, when this process was not started by it.
	bool Connect(std::function<void()> stop, ibString& error);
	// The service gone — said to the manager with the cluster's exit code, the last thing the process does. Nothing,
	// when it is not connected.
	void Disconnect(int code);
}

#endif
