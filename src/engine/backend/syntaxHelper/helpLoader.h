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

#include "backend/syntaxHelper/helpLoadError.h"

// Current on-disk schema version the loader produces and accepts. Bump
// rule: new optional field = no bump; rename / remove / semantics change
// = bump + migration test against a golden fixture.
constexpr int kIbHelpSchemaVersion = 1;

// Format identifier in the bucket file header. Aligns with the
// OES-JSON-1.0 / OES-XML-2.0 metadata-export convention so all OES
// JSON-on-disk artifacts share the same "format + version" header
// shape — see metadataConfigurationJSON.cpp for the metadata side.
// Loader rejects buckets whose format identifier does not match.
#define IB_HELP_FORMAT_NAME wxT("OES-HELP-1.0")

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

// Normalise any platform-locale string to the corpus's directory
// naming convention (bare 2-char language code, lowercase). Empty /
// 1-char input falls back to "en". Exported so ibHelpService can
// canonicalise the locale once at startup and use the same string
// throughout its lifetime — single source of truth for what counts
// as "the locale" inside this subsystem.
BACKEND_API wxString CanonicaliseLocale(const wxString& raw);

#endif // _IB_HELP_LOADER_H_
