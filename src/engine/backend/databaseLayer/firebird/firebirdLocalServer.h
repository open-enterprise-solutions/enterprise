#ifndef __FIREBIRD_LOCAL_SERVER_H__
#define __FIREBIRD_LOCAL_SERVER_H__

// Local Firebird server spawn — decouples FB engine memory from the
// OES process's address space. Used when OES is built x86 (3 GB
// usable address) but the FB engine needs to handle big working sets
// (archive-node holding a 100 GB mesh database, leader-election
// leader serving 30 TCP clients). The driver spawns `firebird.exe`
// as a child process listening on an auto-picked TCP port, and
// connects to it via `inet://localhost:<port>/<dbpath>`.
//
// Process-wide singleton — one local server per OES process. PID +
// port + parent-OES-PID recorded in a lock file under %TEMP% so a
// crash leaves trackable state for the next launch.
//
// Activation: `firebirdLeaderMode::InitForDatabase` calls
// `EnsureStarted` whenever the dbPath is UNC (`\\host\share\...`)
// and we won the leader byte-range lock. The resulting connection
// URL (`inet://localhost:<port>/<dbpath>`) is then passed to
// `firebirdDatabaseLayer::Open` for the actual attach. Local
// (non-UNC) paths bypass leader-mode entirely and never invoke
// the local server.
//
// **Build gate:** `OES_FB_LOCALSERVER` preprocessor flag (OFF by
// default). When undefined, the entire spawn / wait / signal
// implementation is excluded from the translation unit and the
// public methods become inline stubs returning 0 / empty. Leader-
// mode caller's existing `EnsureStarted() == 0` branch handles the
// degrade-to-embedded path automatically — no #ifdef leaks into
// callers. CMake: `-DOES_FB_LOCALSERVER=ON`; MSBuild: add the macro
// to project Preprocessor Definitions.
//
// **Asset requirement (when flag is ON):** `firebird.exe` (FB 5
// server binary) must be present in `_fb/`. Driver reports a clear
// runtime error if missing — the binary is **not** vendored in the
// OES repo by default to keep distributable size down (firebird.exe
// ~5 MB + symbol files). Operators who want local-server mode drop
// the binary in themselves.

#include "backend/backend.h"

#include <wx/string.h>

class BACKEND_API ibFirebirdLocalServer {
public:
	// Ensure a local FB server child process is running. Idempotent —
	// returns immediately if one is already up and healthy. On first
	// call, picks a free TCP port, spawns `_fb/firebird.exe -a -p
	// <port>`, waits up to 5 seconds for it to bind, and records
	// state in the PID file.
	//
	// Returns the TCP port the server is listening on, or 0 on
	// failure (firebird.exe missing, port grab failed, etc.). On
	// failure, error details surface via wxLogError.
	static int EnsureStarted();

	// Current port if `EnsureStarted` succeeded; 0 otherwise.
	static int GetPort();

	// Graceful shutdown — kills the child process and removes the
	// PID file. Called from process atexit hook; safe to call
	// multiple times.
	static void Stop();

	// Construct a connection URL for the locally-spawned server,
	// e.g. `inet://localhost:3055/<dbpath>`. Empty string if
	// `EnsureStarted` hasn't been called or failed.
	static wxString MakeConnectUrl(const wxString& dbPath);
};

#endif // __FIREBIRD_LOCAL_SERVER_H__
