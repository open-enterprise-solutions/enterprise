////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/metaData.h"

bool ibValueMetaObjectDocument::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, ibT("ObjectModule"), _("Open object module"), m_propertyObjectModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, ibT("ManagerModule"), _("Open manager module"), m_propertyManagerModule->GetMetaObject());
	return false;
}

