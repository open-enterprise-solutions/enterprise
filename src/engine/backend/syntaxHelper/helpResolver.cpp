/////////////////////////////////////////////////////////////////////////////
// ResolveByName — free function over const ibHelpCorpus.
//
// The corpus exposes its name indexes to ResolveByName via the `friend`
// declaration in helpCorpus.h; this keeps the resolver outside the class
// body while still letting it use the pre-built O(1) lookup tables. Tests
// can substitute a mock corpus by satisfying the same friend contract.
/////////////////////////////////////////////////////////////////////////////

#include "backend/syntaxHelper/helpResolver.h"

#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpEntry.h"

#include <unordered_map>
#include <vector>

namespace {

// Mirror NormaliseKey from helpCorpus.cpp. Kept local — both translation
// units use the same convention; if it ever needs to be shared, lift to
// an internal header. For Phase 1 the conservative ASCII-tolower works
// for both indexes (corpus side) and queries (here).
wxString NormaliseKey(const wxString& s) { return s.Lower(); }

bool KindMatchesRole(ibHelpKind kind, ibHelpResolveHint::Role role) {
	using Role = ibHelpResolveHint::Role;
	switch (role) {
	case Role::kUnknown:
		return true;
	case Role::kTypeName:
		return kind == ibHelpKind::kPrimitiveType
		    || kind == ibHelpKind::kCollection
		    || kind == ibHelpKind::kMetaObjectType;
	case Role::kCallExpression:
		return kind == ibHelpKind::kSystemFunction
		    || kind == ibHelpKind::kMetaObjectMethod;
	case Role::kMemberAccess:
		return kind == ibHelpKind::kMetaObjectAttribute
		    || kind == ibHelpKind::kMetaObjectMethod
		    || kind == ibHelpKind::kEvent;
	case Role::kAssignmentTarget:
		return kind == ibHelpKind::kMetaObjectAttribute
		    || kind == ibHelpKind::kSystemConstant;
	}
	return true;
}

// True if the entry's id is rooted in the given metadata object — e.g.
// "attr.Document.Invoice.Code" is under "mo.Document.Invoice". Used to
// prefer attribute / method matches whose parent matches the hint's
// containing object.
bool EntryUnderObject(const ibHelpEntry& e, const wxString& objectId) {
	if (objectId.empty()) return false;

	// objectId looks like "mo.Document.Invoice"; strip the prefix to get
	// the bare "Document.Invoice" so it can match the middle of attr/meth
	// ids like "attr.Document.Invoice.Code".
	wxString bare = objectId;
	if (bare.StartsWith(wxT("mo."))) bare = bare.Mid(3);
	if (bare.empty()) return false;

	const wxString prefix = bare + wxT(".");
	if (e.id.StartsWith(wxT("attr.")) && e.id.Mid(5).StartsWith(prefix))
		return true;
	if (e.id.StartsWith(wxT("meth.")) && e.id.Mid(5).StartsWith(prefix))
		return true;
	if (e.id.StartsWith(wxT("ev.")) && e.id.Mid(3).StartsWith(prefix))
		return true;
	return false;
}

} // namespace

std::vector<const ibHelpEntry*>
ResolveByName(const ibHelpCorpus&      corpus,
              const wxString&          identifier,
              const ibHelpResolveHint& hint) {
	const wxString key = NormaliseKey(identifier);

	// Pick the primary index based on hint preference. Fall back to the
	// other if the preferred index has no hits (web clients may type the
	// English name even on a ru-RU corpus and vice versa).
	const auto& primary =
	    hint.preferLocalName ? corpus.m_byNameLocal : corpus.m_byNameEn;
	const auto& fallback =
	    hint.preferLocalName ? corpus.m_byNameEn    : corpus.m_byNameLocal;

	std::vector<ibHelpEntryIndex> candidates;
	auto it = primary.find(key);
	if (it != primary.end()) {
		candidates = it->second;
	} else {
		it = fallback.find(key);
		if (it != fallback.end()) candidates = it->second;
	}
	if (candidates.empty()) return {};

	// Filter by role-implied kind set. An kUnknown hint admits all kinds
	// (default behaviour matches the legacy autocomplete tooltip).
	std::vector<const ibHelpEntry*> roleFiltered;
	roleFiltered.reserve(candidates.size());
	for (ibHelpEntryIndex idx : candidates) {
		const ibHelpEntry& e = corpus.m_entries[idx];
		if (KindMatchesRole(e.kind, hint.expectedRole))
			roleFiltered.push_back(&e);
	}
	// If the role filter eliminated everything, fall back to the
	// unfiltered candidate set — better to surface a slightly off-kind
	// match than to silently return zero results from a typo-prone hint.
	if (roleFiltered.empty()) {
		roleFiltered.reserve(candidates.size());
		for (ibHelpEntryIndex idx : candidates)
			roleFiltered.push_back(&corpus.m_entries[idx]);
	}

	// Two-tier sort: matches under the hint's containing metadata object
	// come first; everything else preserves insertion order (which is
	// itself stable thanks to BuildIndexes walking m_entries front-to-back).
	if (!hint.containingMetaObjectId.empty()) {
		std::stable_partition(
		    roleFiltered.begin(), roleFiltered.end(),
		    [&](const ibHelpEntry* e) {
			    return EntryUnderObject(*e, hint.containingMetaObjectId);
		    });
	}

	return roleFiltered;
}
