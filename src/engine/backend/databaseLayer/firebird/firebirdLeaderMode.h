#ifndef __FIREBIRD_LEADER_MODE_H__
#define __FIREBIRD_LEADER_MODE_H__

// Leader-election orchestrator — wires the `ibFirebirdLease`
// primitive into the driver's `Open` path. Activation gate is
// PATH-based: only UNC / SMB paths (`\\host\share\...` or
// `//host/share/...`) enter leader-election. Local paths always
// run STANDALONE single-process embedded — same as the driver
// behaved before leader-mode landed, regardless of any `.lease`
// sidecar that may travel with the database when a share is
// copied to a local disk.
//
// When activated, the orchestrator routes to either:
//
//   - embedded leader mode (this process holds the lock, opens the
//     `.fdb` locally and exposes a TCP listener for peers); or
//   - TCP follower mode (lease points at another OES process which
//     holds the database open; we connect to it over the network).
//
// The orchestrator decides the role at `InitForDatabase` time and
// starts a background heartbeat thread:
//
//   - Leader thread: refreshes the lease timestamp every 5 s so
//     followers don't mistakenly force-takeover.
//   - Follower thread: re-reads lease every 5 s, watches for
//     generation bump (= handoff to a different leader). When
//     generation changes, current FB attachment is dead — the
//     driver level surfaces this as a connection-lost error and
//     the session reconnects.
//
// Lifetime is process-global: one orchestrator per OES process,
// covering one `.fdb`. Multiple databases per process aren't
// currently supported in leader-mode (would need a registry keyed
// by db path).
//
// **Local-server dependency.** Leader role requires
// `_fb/firebird.exe` to be present so the in-process embedded FB
// can additionally expose a TCP listener. If the binary is
// missing, the orchestrator degrades to embedded-only and writes
// `port=0` into the lease — followers seeing `port=0` know this
// leader can't accept TCP connections, surface a clear error, and
// the operator either drops `firebird.exe` into `_fb/` or moves
// the deployment to a different leader-election topology.

#include "backend/backend.h"

#include <wx/string.h>

class BACKEND_API ibFirebirdLeaderMode {
public:
	enum class Role {
		Standalone,   // no lease file — caller proceeds with normal Open()
		Leader,       // we hold the lease; FB attached locally via TCP to own server
		Follower      // someone else is leader; we attach over TCP to them
	};

	struct Status {
		Role     role             = Role::Standalone;
		wxString connectUrl;       // for fbclient.dll attach (`inet://host:port/path` or empty for Standalone)
		wxString errorMessage;     // populated when ok=false
		bool     ok               = false;
	};

	// Detect role + start background heartbeat thread.
	// `dbPath` is the absolute path to the `.fdb` (or whatever
	// physical file FB will attach). The sidecar `<dbPath>.lease`
	// presence is what triggers leader-election mode; absence means
	// `Standalone` and caller continues with normal Open() path.
	//
	// Subsequent calls with the same `dbPath` return cached status.
	// Calls with a different path while already initialised return
	// failure (multi-database leader mode not supported yet).
	static Status InitForDatabase(const wxString& dbPath);

	// Stop background thread, release lease if leader. Called on
	// graceful shutdown; idempotent.
	static void Shutdown();

	// Read-only access to current role / connect URL. Used by the
	// driver's reconnect path when a follower's TCP connection
	// drops — caller polls this until the orchestrator reports a
	// new leader (generation bump detected), then retries Open.
	static Role     CurrentRole();
	static wxString CurrentConnectUrl();
};

#endif // __FIREBIRD_LEADER_MODE_H__
