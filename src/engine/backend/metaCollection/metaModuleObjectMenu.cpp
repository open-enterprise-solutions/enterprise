////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule menu
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectCommonModule::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, ibT("Module"), _("Open module"), this);
	return false;
}

