/////////////////////////////////////////////////////////////////////////////
// Immutable help-corpus snapshot.
//
// Holds every help entry for one locale of one OES process. Single-corpus-
// per-process model (design v5 §3.1 / §6.1) — there is no per-session
// corpus accessor. Session identity flows into HTTP auth / cache-key
// concerns only, not into corpus ownership.
//
// Lifecycle:
//   1. LoadHelpCorpus(locale, platformDir, configDir) — non-throwing
//      factory in helpLoader.h. Builds two source corpora (platform +
//      per-config) and merges them through the merging constructor below
//      into a single immutable snapshot.
//   2. appData publishes the resulting shared_ptr<const ibHelpCorpus>
//      via atomic_store_explicit (§3.4). Readers grab a snapshot through
//      appData->GetHelpCorpus() and hold it through their operation —
//      no locks on the hot path, no UAF on reload.
//   3. ReloadHelpCorpus() builds a new snapshot on a worker thread,
//      serialised by appData's reload mutex, and atomically swaps the
//      published shared_ptr. Old snapshots stay alive until the last
//      reader releases.
//
// Indexes are built once in the constructor. All read paths are
// concurrent-safe by virtue of immutability.
//
// See docs/syntax-helper-design.md §3 for the full contract.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_CORPUS_H_
#define _IB_HELP_CORPUS_H_

#include "backend/backend.h"
#include "backend/help/helpCategory.h"
#include "backend/help/helpEntry.h"
#include "backend/help/helpLoadError.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

// Internal entry-index type — opaque slot number into m_entries. Public
// API returns const ibHelpEntry* pointers; this is used only inside the
// index implementation.
using ibHelpEntryIndex = std::uint32_t;

class BACKEND_API ibHelpCorpus final {
public:
	// Source tag for the merging constructor. The platform corpus ships
	// with OES (data/help/<locale>/), the per-config corpus is rebuilt
	// from the active configuration (configCacheDir/help/<locale>/).
	// On id collision the per-config entry wins as an overlay; the merge
	// records a kWarning in the result's errors list. See §3.6.
	enum class Source {
		kPlatform,
		kPerConfiguration,
	};

	// Construct an empty corpus. Used as the fallback when LoadHelpCorpus
	// fails completely so appData's accessor invariant
	// (returns non-null shared_ptr) holds without special-case code.
	explicit ibHelpCorpus(const wxString& locale);

	// Single-source constructor. The loader produces one of these per
	// source (platform / per-config), then hands them to the merging
	// constructor below.
	ibHelpCorpus(const wxString&                   locale,
	             Source                            source,
	             std::vector<ibHelpEntry>          entries,
	             std::vector<ibHelpLoadError>      loadErrors);

	// Merging constructor — composes platform + per-config into a single
	// immutable snapshot. Platform entries are added first; per-config
	// entries with the same id overlay the platform entry (recorded as
	// kWarning in the merged errors list) and are tagged
	// fromConfiguration=true.
	ibHelpCorpus(std::shared_ptr<const ibHelpCorpus> platform,
	             std::shared_ptr<const ibHelpCorpus> perConfig,
	             const wxString&                     locale);

	// No copy / move — the corpus is owned through shared_ptr; accidental
	// duplication of the indexes would defeat the snapshot-share design.
	ibHelpCorpus(const ibHelpCorpus&)            = delete;
	ibHelpCorpus& operator=(const ibHelpCorpus&) = delete;
	ibHelpCorpus(ibHelpCorpus&&)                 = delete;
	ibHelpCorpus& operator=(ibHelpCorpus&&)      = delete;

	// === Read path ========================================================

	// Tree root for the helpTreeView (Phase 3 Contents tab). Owned by the
	// corpus; non-null even on an empty corpus (the root has no children
	// in that case).
	const ibHelpCategory* GetRoot() const;

	// id → entry. Returns nullptr if the id is not in the corpus.
	const ibHelpEntry* FindById(const wxString& id) const;

	// All entries in deterministic order (by id ascending). Cheap — entries
	// are stored contiguously; this returns pointers into that storage.
	std::vector<const ibHelpEntry*> AllEntries() const;

	// Prefix search over `nameLocal` and `nameEn` (case-insensitive,
	// NFC-normalised). Used by the Phase 3 Index tab's filter textbox.
	std::vector<const ibHelpEntry*> SearchPrefix(const wxString& prefix) const;

	// Token search over name + signature tokens (Phase 1 — no description
	// tokens; description tokens land in Phase 6 behind a feature flag,
	// §3.5). Used by the Phase 3 Search tab.
	std::vector<const ibHelpEntry*> SearchText(const wxString& query) const;

	// === Diagnostics ======================================================

	// Stable digest of the corpus's on-disk source — SHA-256 over all
	// loaded files (buckets + _categories.json) of the locale,
	// concatenated with locale and schema_version. Truncated to 16 hex
	// chars. Used as the HTTP ETag (§6.1).
	wxString Fingerprint() const;

	int             EntryCount() const;
	const wxString& Locale()     const;

	// Errors collected during construction (and during merge, if this
	// corpus came out of the merging constructor). Never throws — even
	// in the empty-fallback path the vector is well-formed.
	const std::vector<ibHelpLoadError>& LoadErrors() const;

private:
	// Backing storage. Entries are stored by value so pointers remain
	// stable for the corpus's lifetime (§3.4 lifetime contract).
	std::vector<ibHelpEntry> m_entries;

	// id → slot in m_entries.
	std::unordered_map<wxString, ibHelpEntryIndex> m_byId;

	// Case-insensitive name lookups, used by the resolver.
	std::unordered_map<wxString, std::vector<ibHelpEntryIndex>> m_byNameEn;
	std::unordered_map<wxString, std::vector<ibHelpEntryIndex>> m_byNameLocal;

	// Ordered prefix index. Value is a vector because multiple entries
	// can share the same normalised prefix key (e.g. several entries
	// with the same identifier in different kinds).
	std::map<wxString, std::vector<ibHelpEntryIndex>> m_prefixIndex;

	// Token → entries. Built from name + signature tokens only in Phase 1.
	std::unordered_map<wxString, std::vector<ibHelpEntryIndex>> m_tokenIndex;

	// Root of the category tree. Owned.
	std::unique_ptr<ibHelpCategory> m_root;

	// Source-file digest used by Fingerprint(). Computed once at
	// construction time so the accessor is trivial.
	wxString m_fingerprint;

	wxString                     m_locale;
	std::vector<ibHelpLoadError> m_loadErrors;

	// Internal helpers — used by the constructors only.
	void BuildIndexes();
	void BuildCategoryTree();

	// Free-function accessor used by helpResolver — keeps the resolver
	// independent of the corpus's internal storage layout. BACKEND_API
	// on the friend declaration must match the linkage on the real
	// declaration in helpResolver.h, otherwise MSVC raises C2375 on
	// a dllexport build. clang/gcc accept either form, but matching is
	// the portable spelling.
	friend BACKEND_API std::vector<const ibHelpEntry*> ResolveByName(
	    const ibHelpCorpus&, const wxString&, const struct ibHelpResolveHint&);
};

#endif // _IB_HELP_CORPUS_H_
