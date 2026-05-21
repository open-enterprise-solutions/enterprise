/////////////////////////////////////////////////////////////////////////////
// Configuration-directory lock for cross-process coordination between
// Designer (interactive GUI editor), oes-mcp (headless metadata server),
// codeRunner, daemon, and any future tool that opens an OES configuration
// from a `*.fdb` directory.
//
// Why this exists
// ===============
// OES already has an in-memory `ExclusiveMode` flag at the
// ibSessionRegistry level (one process, one session pinned), but nothing
// arbitrates between DIFFERENT processes touching the same config
// directory. If Designer is open editing config X, and oes-mcp is also
// launched against config X via Claude Code, both write to the same
// metadata tree + sys.fdb — last-write-wins, lost edits, corrupted state.
//
// This file introduces a small JSON-backed lock manifest in the config
// directory that every cooperating process must consult before opening.
// It is not a kernel-level mandatory lock — a malicious / out-of-band
// writer can still corrupt the directory. The goal is to coordinate
// EXPECTED concurrent tools (Designer + oes-mcp + codeRunner). For the
// uncooperative-tool case the platform falls back to "last-saved wins
// after restart", as it always has.
//
// File format
// -----------
// Path:    <configDir>/sys/.oes.lock
// Content: {"holders":[{"pid":1234,"mode":"shared","since":"2026-05-21T15:30:00Z","program":"oes-mcp"}, ...]}
//
// One file, N holder entries. `shared` mode is reentrant (any number),
// `exclusive` mode is unique (at most one, and only when no shared
// holders are live).
//
// Liveness probe
// --------------
// Before honouring an existing holder we re-check whether the recorded
// pid is alive. Dead-pid entries are reaped automatically — this is the
// "Designer crashed and left a stale lock" recovery path. The probe is
// best-effort cross-platform: on POSIX we send signal 0; on Windows we
// open the process handle (PROCESS_QUERY_LIMITED_INFORMATION) and check
// for STILL_ACTIVE.
//
// Atomicity
// ---------
// Writes go through write-temp-then-rename so a SIGKILL mid-update
// never leaves a torn JSON. The lock file is itself protected by an
// advisory file lock (flock on POSIX, LockFileEx on Windows) held only
// during the read-modify-write window — never long-term.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_CONFIG_LOCK_H_
#define _IB_CONFIG_LOCK_H_

#include "backend/backend.h"

#include <wx/string.h>

#include <cstdint>
#include <string>
#include <vector>

class BACKEND_API ibConfigLock {
public:
	// Sharing mode requested by the caller.
	enum class Mode {
		Shared,     // multiple readers, multiple cooperating writers
		Exclusive,  // sole holder — Designer's default for legacy compat
	};

	// Outcome of TryAcquire — distinguishes "you got it" from each
	// rejection class so the caller can produce a useful diagnostic.
	enum class Acquire {
		Ok,                    // lock acquired; call Release before destroying state.
		ConflictExclusive,     // another process holds an exclusive lock.
		ConflictShared,        // we asked for exclusive but shared holders exist.
		IoError,               // filesystem refused (permissions / disk full / ...).
		MalformedManifest,     // existing manifest could not be parsed.
	};

	// One live entry in the manifest. Exposed for diagnostics and the
	// "what's holding it?" branch of the MCP error message.
	struct Holder {
		std::int64_t pid       = 0;          // OS process id
		Mode         mode      = Mode::Shared;
		std::string  since;                  // ISO-8601 acquisition timestamp
		std::string  program;                // optional — "designer" / "oes-mcp" / etc
	};

	// Best-effort: returns the entries in the lock manifest, reaping any
	// dead-pid rows on the way. Empty vector if no manifest exists.
	// Does NOT mutate the on-disk file when reaping — purely a read.
	// Pure-read callers that want the reaped state persisted should call
	// SweepDeadHolders separately.
	static std::vector<Holder> Inspect(const wxString& configDir);

	// Reap dead-pid rows in-place. Returns the count of removed entries.
	// Safe to call repeatedly; no-op when the manifest doesn't exist.
	static std::size_t SweepDeadHolders(const wxString& configDir);

	// Try to acquire a lock in the requested mode. On success the caller
	// MUST call Release with the returned holder id before the process
	// exits. The id is opaque — currently the pid — but treat it as a
	// handle. On failure, holdersOut (when non-null) receives the live
	// holders so the caller can show a useful error.
	static Acquire TryAcquire(const wxString& configDir,
	                          Mode mode,
	                          const std::string& program,
	                          std::int64_t* outHolderId,
	                          std::vector<Holder>* holdersOut);

	// Release the holder by id (the value TryAcquire wrote to outHolderId).
	// No-op if the manifest doesn't exist or the id isn't found.
	// Returns true on a successful write, false on IO failure.
	static bool Release(const wxString& configDir, std::int64_t holderId);

	// True iff at least one EXCLUSIVE holder is alive in the manifest.
	// Convenience for per-tool re-probe at mutation time (Layer 2).
	static bool HasLiveExclusiveHolder(const wxString& configDir);

	// True iff the holder with this id is still present + alive in the
	// manifest. Used by oes-mcp's per-tool re-check to confirm the
	// session-start lock wasn't stolen out from under us (e.g. someone
	// hand-deleted the file).
	static bool IsHolderStillLive(const wxString& configDir,
	                              std::int64_t holderId);

	// =====================================================================
	// Change broadcast — Layer 3 of the Designer/MCP coordination spec.
	// =====================================================================
	//
	// Each successful mutation in oes-mcp writes a small JSON marker to
	//   <configDir>/sys/.oes-mcp-mutation
	// Designer polls this file via a wxTimer (see frontend-side
	// ibExternalMutationNotifier) and surfaces a toast when the marker
	// changes. The marker is atomic — write-temp-then-rename — so
	// Designer never reads a partial JSON.
	//
	// Schema:
	//   {"ts":"2026-05-21T15:30:00Z","tool":"meta_create",
	//    "fullName":"Catalog.X","pluginId":"mcp-server","seq":N}
	//
	// `seq` is a monotonic counter Designer uses to dedupe across reads.
	// The counter lives in the marker itself; Designer remembers the
	// last seq it surfaced.
	struct MutationMarker {
		std::string ts;          // ISO-8601 UTC
		std::string tool;        // "meta_create" / "meta_edit" / ...
		std::string fullName;    // affected object path (may be empty)
		std::string pluginId;    // "mcp-server" / ...
		std::int64_t seq = 0;    // monotonic counter
	};

	// Append-style write. Reads the current seq, increments by 1, writes
	// atomically. Returns false on IO failure (sys/ missing, disk full).
	static bool WriteMutationMarker(const wxString& configDir,
	                                const MutationMarker& m);

	// Read the latest marker. Returns false when none present or the
	// file can't be parsed (caller treats both as "no event").
	static bool ReadMutationMarker(const wxString& configDir,
	                               MutationMarker& out);

	// Path helpers — exposed for tests / diagnostics. configDir is the
	// directory containing sys.fdb (NOT the sys/ subdir).
	static wxString LockFilePath(const wxString& configDir);
	static wxString MutationMarkerPath(const wxString& configDir);
};

#endif // _IB_CONFIG_LOCK_H_
