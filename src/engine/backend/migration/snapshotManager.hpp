/////////////////////////////////////////////////////////////////////////////
// snapshotManager — per-mutation auto-snapshots for the oes-mcp surface.
//
// Each `meta_create / meta_edit / meta_delete / write_module` call dumps
// the affected object's prior state into a timestamped JSON file under
// `<configDir>/sys/oes-snapshots/<id>.json`. The MCP layer surfaces the
// snapshot id in the tool's structuredContent so the agent (and the
// user) can list and roll back without grepping the disk.
//
// Co-located with the BAS migration code because both modules share the
// "config IO outside the live metadata tree" theme — backend.dll
// already exports the namespace `migration::*`, so we keep the new
// surface there.
//
// Backend-only (no wx GUI). Uses wxFileName / wxFile / wxDir for path
// IO so the same code works on macOS / Linux / Windows.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_SNAPSHOT_MANAGER_HPP_
#define _IB_SNAPSHOT_MANAGER_HPP_

#include "3rdparty/nlohmann/json.hpp"

#include <wx/datetime.h>
#include <wx/string.h>

#include <cstddef>
#include <string>
#include <vector>

namespace migration {
namespace snapshots {

// Single snapshot's persisted metadata. The full prior/new state lives
// in the JSON body; this descriptor is the index entry returned by
// `List()` so the agent can pick which one to load.
struct SnapshotMetadata {
	wxString  id;               // e.g. "20260521T103045-0001"
	wxDateTime timestamp;        // UTC
	wxString  triggeredBy;       // tool name (meta_create / meta_edit / ...)
	wxString  fullName;          // affected metadata object
	wxString  operation;         // create / edit / delete / write_module
	wxString  description;       // human-readable summary
	wxString  filePath;          // absolute path to the .json on disk
	std::size_t sizeBytes = 0;
	bool      consumed = false;  // true when the snapshot's .consumed.json marker exists
};

// Capture mode — wired from the OES_MCP_AUTO_SNAPSHOT env var.
//   Full     — write priorState + newState verbatim (default)
//   HashOnly — write metadata + SHA-256 of priorState (smaller footprint)
//   Disabled — Capture* is a no-op; List/Load still work for inspection
enum class CaptureMode {
	Full,
	HashOnly,
	Disabled,
};

// Resolve OES_MCP_AUTO_SNAPSHOT to a CaptureMode. Unknown / empty → Full.
CaptureMode ParseCaptureModeFromEnv(const char* envValue);

// Cap on total snapshots per config — older ones are pruned on capture.
constexpr std::size_t kMaxSnapshotCount = 1000;
// Per-snapshot byte ceiling — priorState bigger than this is replaced
// with a stub `{"truncated":true,"reason":"size>10MB"}` to keep the
// disk footprint bounded.
constexpr std::size_t kMaxSnapshotBytes = 10u * 1024u * 1024u;
// Soft target retained after `save_config` clean-up (the rest are
// pruned; v2 will archive instead).
constexpr std::size_t kRetainAfterSave = 100;

class ibSnapshotManager {
public:
	// `configDir` — absolute path to the OES configuration root. The
	// snapshot store lives at `<configDir>/sys/oes-snapshots/`. The
	// directory is created lazily on the first capture.
	//
	// `mode` — capture behaviour for `Capture*` calls. List/Load/Mark*
	// ignore this so an operator can read the audit trail even after
	// disabling captures.
	ibSnapshotManager(const wxString& configDir, CaptureMode mode);

	// Capture BEFORE a mutation runs. Returns the assigned snapshot id
	// (empty string on failure — IO errors don't abort the mutation, we
	// log to stderr instead). `priorStateJson` is the serialised
	// metaBridge / meta_query payload; pass `"null"` when there is no
	// prior state (e.g. meta_create).
	wxString CaptureBeforeMutation(
		const wxString& toolName,
		const wxString& fullName,
		const wxString& priorStateJson);

	// List snapshots in newest-first order. `limit` caps the result
	// size (use a very large value for "no cap"); `since` filters out
	// snapshots older than the cut-off (pass `wxInvalidDateTime` for
	// no time filter).
	std::vector<SnapshotMetadata> List(
		std::size_t limit,
		const wxDateTime& since) const;

	// Load a snapshot's full JSON body by id. Returns empty string when
	// the id is unknown. Caller parses with nlohmann::json::parse.
	wxString Load(const wxString& id) const;

	// Mark a snapshot consumed (renamed to `<id>.consumed.json`) so a
	// double-rollback can't replay the same snapshot. Returns false on
	// IO failure or unknown id.
	bool MarkConsumed(const wxString& id);

	// Prune the oldest snapshots until at most `retain` remain. Returns
	// the number of files deleted.
	std::size_t PruneToCount(std::size_t retain);

	// Configuration directory the manager was initialised with.
	const wxString& ConfigDir() const { return m_configDir; }

	// Active capture mode. Useful for the tool layer to skip the
	// `priorStateJson` computation when capture is disabled.
	CaptureMode Mode() const { return m_mode; }

	// Where snapshot files live (absolute). Created on first capture.
	wxString StoreDir() const;

private:
	// Allocate the next id for an arbitrary timestamp. Format:
	//   YYYYMMDDTHHMMSS-<seq>
	// `seq` resets on a new second; `now` is the time captured at the
	// call site (lets tests pass deterministic instants).
	wxString AllocateId(const wxDateTime& now);

	// Ensure the snapshot store exists; returns false + sets `errOut`
	// when creation fails.
	bool EnsureStoreDir(wxString& errOut) const;

	wxString    m_configDir;
	CaptureMode m_mode;
	// Last-second + sequence counter used by AllocateId. Strictly
	// monotonic within the process lifetime.
	wxString    m_lastSecondTag;
	int         m_lastSecondSeq = 0;
};

} // namespace snapshots
} // namespace migration

#endif // _IB_SNAPSHOT_MANAGER_HPP_
