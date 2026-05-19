/////////////////////////////////////////////////////////////////////////////
// LoadHelpCorpus — non-throwing JSON → ibHelpCorpus factory.
//
// Reads `data/help/<locale>/*.json` (platform corpus) and optionally
// `<configCacheDir>/help/<locale>/*.json` (per-config corpus), parses
// each bucket, builds two source corpora, and merges them through
// ibHelpCorpus's merging constructor into a single immutable snapshot.
//
// Contract (§3.3):
//   - Never throws. ibBackendException is reserved for the backend's
//     own runtime — corpus loading is best-effort.
//   - Bad JSON → ibHelpLoadError records; the bucket is skipped, others
//     keep loading.
//   - Per-entry JSON exceptions (missing keys, type mismatch) caught and
//     recorded as kEntrySkipped; the surrounding bucket still loads.
//   - On total failure the returned `corpus` is still non-null — it
//     points to an empty ibHelpCorpus, so call sites can keep their
//     `appData->GetHelpCorpus()` non-null invariant without special casing.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_LOADER_H_
#define _IB_HELP_LOADER_H_

#include "backend/backend.h"

#include "backend/help/helpLoadError.h"

// Current on-disk schema version the loader produces and accepts. Bump
// rule: new optional field = no bump; rename / remove / semantics change
// = bump + migration test against a golden fixture.
constexpr int kIbHelpSchemaVersion = 1;

// Load the help corpus for one locale.
//
// `platformDir` — usually `appData->GetInstallDataDir() / "help"`. The
// loader looks for `<platformDir>/<localeCode>/*.json`.
//
// `configCacheDir` — optional. When non-empty the loader also reads
// `<configCacheDir>/<localeCode>/*.json` and overlays per-config entries
// over the platform corpus (§3.6 merge rule).
//
// `localeCode` — canonical OES locale string ("ru-RU", "uk-UA", "en-US").
// The loader is responsible for normalising any platform-locale string
// (wxLocale returns "en" or "en_US") to one of the canonical forms via
// helpers in the loader implementation.
BACKEND_API ibHelpLoadResult
LoadHelpCorpus(const wxString& localeCode,
               const wxString& platformDir,
               const wxString& configCacheDir = wxEmptyString);

#endif // _IB_HELP_LOADER_H_
