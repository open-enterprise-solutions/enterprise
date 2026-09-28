#include "metaFunctionalOptionObject.h"

// What an option offers first is its COMPOSITION — the members it takes away when it is off, as a tree to tick
// (the option's own document, the designer's functionalOptionEditor) — the way a role offers itself; the stored
// value's module follows.
bool ibValueMetaObjectFunctionalOption::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Object, wxT("Composition"), _("Open functional option composition"), this);
	return ibValueMetaObjectStoredValue::CollectContextMenu(items);
}
