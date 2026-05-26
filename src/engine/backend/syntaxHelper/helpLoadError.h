/////////////////////////////////////////////////////////////////////////////
// Loader error records.
//
// LoadHelpCorpus() is non-throwing — bad JSON in production must never
// crash Designer startup or unwind a cpp-httplib handler. Errors are
// returned as a collection alongside the (possibly partial) corpus.
//
// Severity model:
//   kFatal         — bucket cannot be loaded at all (file unreadable,
//                    top-level JSON malformed, schema_version newer than
//                    the loader). The whole bucket is dropped; other
//                    buckets in the same source corpus still load.
//   kEntrySkipped  — one entry inside an otherwise-good bucket failed
//                    parsing (per-entry try/catch caught a json or
//                    std exception). The entry is dropped; the rest of
//                    the bucket loads.
//   kWarning       — non-fatal anomaly: dangling see_also id, overlay
//                    (per-config entry shadowing a platform entry),
//                    unknown category key. Entry still loads.
//
// See docs/syntax-helper-design.md §3.3 for the contract.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_LOAD_ERROR_H_
#define _IB_HELP_LOAD_ERROR_H_

#include "backend/backend.h"

#include <memory>
#include <vector>

class ibHelpCorpus;

enum class ibHelpLoadSeverity {
	kFatal,
	kEntrySkipped,
	kWarning,
};

struct BACKEND_API ibHelpLoadError {
	wxString           bucketPath; // absolute path of the offending file
	int                line = 0;   // 1-based; 0 if not localizable to a line
	ibHelpLoadSeverity severity = ibHelpLoadSeverity::kEntrySkipped;
	wxString           message;    // human-readable, localised at the call site
};

// Outcome of LoadHelpCorpus(). `corpus` is never null — on total failure
// it points to an empty corpus snapshot so call sites can keep their
// `appData->GetHelpCorpus()` invariant (returns non-null shared_ptr).
struct BACKEND_API ibHelpLoadResult {
	std::shared_ptr<const ibHelpCorpus> corpus;
	std::vector<ibHelpLoadError>        errors;

	// True iff no kFatal severity entries. kEntrySkipped / kWarning do
	// not flip ok() to false — a partial corpus is still a corpus.
	bool ok() const;
};

#endif // _IB_HELP_LOAD_ERROR_H_
