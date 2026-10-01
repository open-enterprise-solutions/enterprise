/////////////////////////////////////////////////////////////////////////////
// Category tree node.
//
// Built by ibHelpCorpus from each entry's `categoryKeys` chain. The Phase 3
// tree view (helpTreeView) walks this tree. Display names come from the
// locale's _categories.json dictionary — the node stores both the stable
// key (for cross-locale stable selection) and the localised display name
// (for rendering).
//
// Nodes are owned by ibHelpCorpus and live for the corpus's lifetime —
// callers see them as `const ibHelpCategory*` borrowed from the corpus
// snapshot.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_CATEGORY_H_
#define _IB_HELP_CATEGORY_H_

#include "backend/backend.h"

#include <memory>
#include <vector>

struct ibHelpEntry;

// NOTE: deliberately NOT BACKEND_API. The struct holds
// std::unique_ptr<ibHelpCategory> children — under __declspec(dllexport)
// MSVC eagerly instantiates the implicit copy ops, which fail because
// unique_ptr is move-only. Consumers reach this type only through
// const-pointer accessors on ibHelpCorpus, so it does not need to live
// in the exported symbol table.
struct ibHelpCategory {
	// Locale-stable identifier — matches one segment of an entry's
	// categoryKeys. The root has an empty key.
	wxString key;

	// Localised display name, looked up from _categories.json at load
	// time. Empty until the loader resolves it.
	wxString displayName;

	// Direct children, ordered by `key` ascending (loader sorts). Owned.
	std::vector<std::unique_ptr<ibHelpCategory>> children;

	// Entries terminating at this category. Borrowed from the corpus's
	// entry storage — pointers stable for the snapshot's lifetime.
	std::vector<const ibHelpEntry*> entries;

	ibHelpCategory()                                  = default;
	~ibHelpCategory()                                 = default;
	ibHelpCategory(const ibHelpCategory&)             = delete;
	ibHelpCategory& operator=(const ibHelpCategory&)  = delete;
	ibHelpCategory(ibHelpCategory&&)                  = default;
	ibHelpCategory& operator=(ibHelpCategory&&)       = default;
};

#endif // _IB_HELP_CATEGORY_H_
