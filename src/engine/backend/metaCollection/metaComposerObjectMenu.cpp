////////////////////////////////////////////////////////////////////////////
//	Description : composer menu
////////////////////////////////////////////////////////////////////////////

#include "metaComposerObject.h"
#include "backend/metaData.h"

// "Open composer" — the same gesture a template and a form answer to, and it opens the settings
// window that already exists (query / resources / output), not a second editor of its own.
bool ibValueMetaObjectComposer::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Object, wxT("Composer"), _("Open data composer"), this);
	return false;
}

