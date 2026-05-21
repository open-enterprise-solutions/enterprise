/////////////////////////////////////////////////////////////////////////////
// headless_app.h — boots an ibApplicationData in eDESIGNER_MODE without a
// wx GUI loop. Same path codeRunner / daemon use, but stripped of the
// frame/UI surface so the MCP server can drive metaBridge against a live
// configuration from a single stdio thread.
//
// MCP: the lifecycle is intentionally explicit (Init → LoadDatabase →
// Shutdown) so SIGINT handling can call Shutdown deterministically.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_MCP_HEADLESS_APP_H_
#define _IB_MCP_HEADLESS_APP_H_

#include <string>

namespace mcpServer {

struct HeadlessConfig {
	// Path to the OES configuration. Two shapes accepted:
	//   * directory containing sys.fdb — opens the embedded Firebird
	//     storage and the contained configuration is live immediately.
	//   * path ending in `.OES-DB` (a zipped config snapshot) — we still
	//     need a transient sys.fdb to host the load; createTempDir gates
	//     that path when supplied.
	std::string configPath;

	// Optional IB user / password for the system session. Empty values
	// trigger anonymous attach (most file-mode configs don't have
	// sys_user rows).
	std::string ibUser;
	std::string ibPassword;

	// BCP-47 locale code. Empty = inherit appData default.
	std::string locale;
};

// MCP: every diagnostic surfaces here so callers can pipe it to stderr.
// We never write to stdout from the headless layer — stdout is reserved
// for the JSON-RPC channel.
using DiagSink = void(*)(const char* line);

// Bring up appData and load the configuration. Returns true on success;
// on failure the diagSink (if non-null) receives a human-readable cause.
// Idempotent: a second Init call after success is a no-op and returns true.
bool Init(const HeadlessConfig& cfg, DiagSink diagSink);

// Tear down appData. Safe to call multiple times.
void Shutdown();

// True once Init has completed and activeMetaData is reachable. Tools
// short-circuit with "no configuration" when this is false.
bool IsReady();

// Save the live configuration back to a .OES-DB zip at `path`. Empty
// path persists to the currently-loaded config's source path when one
// was known; in directory-mode this falls back to writing into the
// embedded sys.fdb (which appData already flushes on each AddChild).
// Returns true on success and writes diagnostic on failure.
bool SaveConfiguration(const std::string& path, std::string& errOut);

// Last-loaded source path (or empty when never loaded).
const std::string& LoadedConfigPath();

} // namespace mcpServer

#endif // _IB_MCP_HEADLESS_APP_H_
