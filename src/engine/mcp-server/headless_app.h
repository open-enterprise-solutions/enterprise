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

#include <cstdint>
#include <string>

namespace mcpServer {

// MCP: classify Init failures so main() can map to the right process exit
// code. The Designer-concurrency spec (2026-05-21) calls for exit code 2
// when an exclusive lock is held by Designer; other failures keep the
// generic exit code 1.
enum class InitOutcome {
	Ok = 0,
	GenericFailure = 1,        // config didn't load, auth refused, etc.
	LockedExclusive = 2,       // another process holds an exclusive lock
};

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

// Bring up appData and load the configuration. Returns Ok on success; on
// failure the diagSink (if non-null) receives a human-readable cause and
// the outcome distinguishes lock conflicts from generic load failures.
// Idempotent: a second Init call after a success returns Ok without
// re-running the bring-up sequence.
InitOutcome Init(const HeadlessConfig& cfg, DiagSink diagSink);

// Back-compat overload — older callers can ignore the outcome enum.
// Returns true iff the underlying Init returned Ok.
bool InitLegacy(const HeadlessConfig& cfg, DiagSink diagSink);

// Tear down appData. Safe to call multiple times. Also releases any
// configuration-directory lock acquired by Init.
void Shutdown();

// MCP-concurrency Layer 2: returns true while our cooperatively-acquired
// lock on the loaded configuration is still valid (the manifest still
// lists our entry AND no exclusive holder has appeared since we acquired).
// Per-mutation guard for tools.cpp.
bool IsLockStillHeld();

// MCP-concurrency Layer 3: write a mutation marker so Designer's
// notifier can surface a "config changed externally" toast. Best-effort —
// on IO failure the mutation still succeeded; only the broadcast is lost.
void NotifyMutation(const std::string& toolName, const std::string& fullName);

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
