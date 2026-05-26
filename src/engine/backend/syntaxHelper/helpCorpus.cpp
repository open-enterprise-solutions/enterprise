/////////////////////////////////////////////////////////////////////////////
// ibHelpCorpus — immutable help-corpus snapshot.
//
// See helpCorpus.h and docs/syntax-helper-design.md §3 for the contract.
// All read paths are concurrent-safe by virtue of immutability — no locks
// in the hot path. Reload happens by building a NEW corpus and atomically
// swapping the published shared_ptr on appData.
/////////////////////////////////////////////////////////////////////////////

#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpResolver.h"

#include <wx/sstream.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace {

// Lower-cased, NFC-normalised view of a name for index lookups. Phase 1
// is conservative: ASCII tolower only, no Unicode case-folding. Real
// Unicode folding goes in once the corpus content reaches non-ASCII bulk
// (Phase 2 LLM fill); for now wxString::Lower covers the common case and
// is stable enough across platforms for hand-written fixture entries.
wxString NormaliseKey(const wxString& s) {
	return s.Lower();
}

// Tokenise a string on non-letter/digit boundaries. Used to build the
// token index and to tokenise search queries. Cyrillic identifiers are
// preserved as single tokens (locale-aware wxIsalnum); ASCII punctuation
// is dropped.
std::vector<wxString> Tokenise(const wxString& s) {
	std::vector<wxString> out;
	wxString cur;
	for (wxString::const_iterator it = s.begin(); it != s.end(); ++it) {
		wxUniChar c = *it;
		if (wxIsalnum(c) || c == wxT('_')) {
			cur += c;
		} else if (!cur.empty()) {
			out.push_back(NormaliseKey(cur));
			cur.clear();
		}
	}
	if (!cur.empty()) out.push_back(NormaliseKey(cur));
	return out;
}

// FNV-1a 64-bit hash. Used to derive a stable fingerprint from entry
// content in Phase 1 — the loader will replace this with a real SHA-256
// over source-file content once the loader is wired (design §6.1
// requires content-hash, not entry-hash, for ETag correctness across
// formatting changes).
std::uint64_t Fnv1a(const wxString& s) {
	std::uint64_t   h     = 1469598103934665603ull;
	const wxScopedCharBuffer utf = s.utf8_str();
	for (size_t i = 0; i < utf.length(); ++i) {
		h ^= static_cast<unsigned char>(utf.data()[i]);
		h *= 1099511628211ull;
	}
	return h;
}

wxString HashEntries(const std::vector<ibHelpEntry>& entries,
                     const wxString&                 locale) {
	// Sort by id so the fingerprint is order-independent. Snapshot the
	// hashable fields; layout-only changes (which we have none of yet)
	// would not bust the fingerprint.
	std::vector<const ibHelpEntry*> sorted;
	sorted.reserve(entries.size());
	for (const auto& e : entries) sorted.push_back(&e);
	std::sort(sorted.begin(), sorted.end(),
	          [](const ibHelpEntry* a, const ibHelpEntry* b) {
		          return a->id < b->id;
	          });

	std::uint64_t h = Fnv1a(locale);
	for (const ibHelpEntry* e : sorted) {
		h ^= Fnv1a(e->id);                 h *= 1099511628211ull;
		h ^= Fnv1a(e->nameLocal);          h *= 1099511628211ull;
		h ^= Fnv1a(e->nameEn);             h *= 1099511628211ull;
		h ^= Fnv1a(e->signature);          h *= 1099511628211ull;
		h ^= Fnv1a(e->description);        h *= 1099511628211ull;
	}
	return wxString::Format(wxT("%016llx"),
	                        static_cast<unsigned long long>(h));
}

} // namespace

// === ibHelpLoadResult::ok ===================================================

bool ibHelpLoadResult::ok() const {
	for (const auto& err : errors) {
		if (err.severity == ibHelpLoadSeverity::kFatal)
			return false;
	}
	return true;
}

// === Constructors ============================================================

ibHelpCorpus::ibHelpCorpus(const wxString& locale)
    : m_locale(locale) {
	m_root            = std::make_unique<ibHelpCategory>();
	m_fingerprint     = HashEntries(m_entries, m_locale);
}

ibHelpCorpus::ibHelpCorpus(const wxString&              locale,
                           Source                       source,
                           std::vector<ibHelpEntry>     entries,
                           std::vector<ibHelpLoadError> loadErrors)
    : m_entries(std::move(entries)),
      m_locale(locale),
      m_loadErrors(std::move(loadErrors)) {
	// Tag every entry with its source so the merging constructor and the
	// UI can distinguish later. Loader does not set this — keeping the
	// invariant in one place avoids contradictions.
	const bool fromCfg = (source == Source::kPerConfiguration);
	for (auto& e : m_entries) e.fromConfiguration = fromCfg;

	m_root        = std::make_unique<ibHelpCategory>();
	m_fingerprint = HashEntries(m_entries, m_locale);

	BuildIndexes();
	BuildCategoryTree();
}

ibHelpCorpus::ibHelpCorpus(std::shared_ptr<const ibHelpCorpus> platform,
                           std::shared_ptr<const ibHelpCorpus> perConfig,
                           const wxString&                     locale)
    : m_locale(locale) {
	// Carry forward source errors so callers see one combined diagnostics
	// stream from the merged snapshot. Source corpora's shared_ptr's stay
	// alive only for the duration of this constructor — once we copy out
	// the entries we can drop them.
	auto absorb = [this](const ibHelpCorpus* src) {
		if (!src) return;
		for (const auto& err : src->m_loadErrors) m_loadErrors.push_back(err);
	};
	absorb(platform.get());
	absorb(perConfig.get());

	// Platform first — per-config entries with colliding ids overlay
	// (§3.6). Use a side index (id → slot in m_entries) for O(1) overlay
	// lookups; the entry storage itself stays a std::vector so iteration
	// order is deterministic across compilers / libstdc++ versions.
	std::unordered_map<wxString, size_t> slotById;
	if (platform) {
		m_entries.reserve(platform->m_entries.size());
		for (const auto& e : platform->m_entries) {
			slotById.emplace(e.id, m_entries.size());
			m_entries.push_back(e);
		}
	}
	if (perConfig) {
		for (const auto& e : perConfig->m_entries) {
			auto it = slotById.find(e.id);
			if (it != slotById.end()) {
				// Overlay — record warning, replace platform entry.
				ibHelpLoadError w;
				w.severity = ibHelpLoadSeverity::kWarning;
				w.message  = wxString::Format(
				    wxT("Per-configuration entry '%s' overlays platform entry."),
				    e.id);
				m_loadErrors.push_back(std::move(w));
				m_entries[it->second] = e;
			} else {
				slotById.emplace(e.id, m_entries.size());
				m_entries.push_back(e);
			}
		}
	}

	m_root        = std::make_unique<ibHelpCategory>();
	m_fingerprint = HashEntries(m_entries, m_locale);

	BuildIndexes();
	BuildCategoryTree();
}

// === BuildIndexes ============================================================

void ibHelpCorpus::BuildIndexes() {
	for (ibHelpEntryIndex i = 0;
	     i < static_cast<ibHelpEntryIndex>(m_entries.size()); ++i) {
		const ibHelpEntry& e = m_entries[i];

		// id lookup — duplicate ids inside one corpus are a loader-level
		// invariant (LoadHelpCorpus enforces it). emplace+log on collision
		// instead of operator[]= so the index does not silently clobber
		// when a future loader bug lets a duplicate slip through. The
		// first occurrence wins (it's already in the map).
		auto ins = m_byId.emplace(e.id, i);
		if (!ins.second) {
			ibHelpLoadError w;
			w.severity = ibHelpLoadSeverity::kFatal;
			w.message  = wxString::Format(
			    wxT("Internal: duplicate id '%s' reached BuildIndexes — entry dropped from index."),
			    e.id);
			m_loadErrors.push_back(std::move(w));
			continue;
		}

		// Name indexes — vector-valued because multiple entries can
		// legitimately share a localised name (primitive type vs Document
		// attribute vs MomentTime attribute vs function).
		if (!e.nameLocal.empty())
			m_byNameLocal[NormaliseKey(e.nameLocal)].push_back(i);
		if (!e.nameEn.empty())
			m_byNameEn[NormaliseKey(e.nameEn)].push_back(i);

		// Prefix index — covers BOTH localised and English names so the
		// Index tab finds entries no matter which alphabet the user types.
		if (!e.nameLocal.empty())
			m_prefixIndex[NormaliseKey(e.nameLocal)].push_back(i);
		if (!e.nameEn.empty() && e.nameEn != e.nameLocal)
			m_prefixIndex[NormaliseKey(e.nameEn)].push_back(i);

		// Token index — name + signature only in Phase 1 (no description
		// tokens, see §3.5). Each token maps to a vector of entry slots.
		auto addTokens = [this, i](const wxString& text) {
			for (const wxString& t : Tokenise(text))
				m_tokenIndex[t].push_back(i);
		};
		addTokens(e.nameLocal);
		addTokens(e.nameEn);
		addTokens(e.signature);
	}
}

// === BuildCategoryTree ======================================================

void ibHelpCorpus::BuildCategoryTree() {
	for (const ibHelpEntry& e : m_entries) {
		ibHelpCategory* cur = m_root.get();
		for (const wxString& key : e.categoryKeys) {
			// Find or create the child node for this key. Keep children
			// sorted by key so the tree view shows a deterministic order.
			auto it = std::lower_bound(
			    cur->children.begin(), cur->children.end(), key,
			    [](const std::unique_ptr<ibHelpCategory>& node,
			       const wxString&                       k) {
				    return node->key < k;
			    });
			if (it == cur->children.end() || (*it)->key != key) {
				auto node = std::make_unique<ibHelpCategory>();
				node->key = key;
				it        = cur->children.insert(it, std::move(node));
			}
			cur = it->get();
		}
		cur->entries.push_back(&e);
	}
}

// === Read path ==============================================================

const ibHelpCategory* ibHelpCorpus::GetRoot() const {
	return m_root.get();
}

const ibHelpEntry* ibHelpCorpus::FindById(const wxString& id) const {
	auto it = m_byId.find(id);
	if (it == m_byId.end()) return nullptr;
	return &m_entries[it->second];
}

std::vector<const ibHelpEntry*> ibHelpCorpus::AllEntries() const {
	std::vector<const ibHelpEntry*> out;
	out.reserve(m_entries.size());
	for (const auto& e : m_entries) out.push_back(&e);
	std::sort(out.begin(), out.end(),
	          [](const ibHelpEntry* a, const ibHelpEntry* b) {
		          return a->id < b->id;
	          });
	return out;
}

std::vector<const ibHelpEntry*>
ibHelpCorpus::SearchPrefix(const wxString& prefix) const {
	const wxString key = NormaliseKey(prefix);
	std::vector<const ibHelpEntry*> out;
	auto it = m_prefixIndex.lower_bound(key);
	for (; it != m_prefixIndex.end(); ++it) {
		if (!it->first.StartsWith(key)) break;
		for (ibHelpEntryIndex idx : it->second)
			out.push_back(&m_entries[idx]);
	}
	return out;
}

std::vector<const ibHelpEntry*>
ibHelpCorpus::SearchText(const wxString& query) const {
	const std::vector<wxString> tokens = Tokenise(query);
	if (tokens.empty()) return {};

	// Intersect token postings — entry must contain ALL query tokens.
	// For Phase 1 we score by (hit count, name-exact-bonus) — BM25 is a
	// Phase 6 enhancement once HTTP search hits real load (§3.5).
	std::unordered_map<ibHelpEntryIndex, int> score;
	for (const wxString& t : tokens) {
		auto it = m_tokenIndex.find(t);
		if (it == m_tokenIndex.end()) continue;
		for (ibHelpEntryIndex idx : it->second) score[idx] += 1;
	}

	std::vector<std::pair<int, ibHelpEntryIndex>> ranked;
	ranked.reserve(score.size());
	const int allTokens = static_cast<int>(tokens.size());
	for (const auto& kv : score) {
		if (kv.second < allTokens) continue; // require all tokens hit
		int s = kv.second;
		// Name-exact bonus — query exactly equals the entry's local or
		// English name. Boosts identifier-looking lookups above prose.
		const ibHelpEntry& e = m_entries[kv.first];
		if (NormaliseKey(e.nameLocal) == NormaliseKey(query) ||
		    NormaliseKey(e.nameEn)    == NormaliseKey(query)) {
			s += 100;
		}
		ranked.emplace_back(s, kv.first);
	}
	std::sort(ranked.begin(), ranked.end(),
	          [](const auto& a, const auto& b) { return a.first > b.first; });

	std::vector<const ibHelpEntry*> out;
	out.reserve(ranked.size());
	for (const auto& p : ranked) out.push_back(&m_entries[p.second]);
	return out;
}

wxString ibHelpCorpus::Fingerprint() const {
	return m_fingerprint;
}

int ibHelpCorpus::EntryCount() const {
	return static_cast<int>(m_entries.size());
}

const wxString& ibHelpCorpus::Locale() const {
	return m_locale;
}

const std::vector<ibHelpLoadError>& ibHelpCorpus::LoadErrors() const {
	return m_loadErrors;
}
