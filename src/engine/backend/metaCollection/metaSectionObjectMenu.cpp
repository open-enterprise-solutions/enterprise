#include "metaSectionObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectSection::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Object, wxT("Interface"), _("Open interface"), this);
	return false;
}

