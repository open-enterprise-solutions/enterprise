////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - menu
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "backend/metaData.h"

bool ibValueMetaObjectReport::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, ibT("ObjectModule"), _("Open object module"),
		m_propertyObjectModule->GetMetaObject(), 0, ID_METATREE_OPEN_MODULE);
	items.emplace_back(ibMetaMenuKind::Module, ibT("ManagerModule"), _("Open manager module"),
		m_propertyManagerModule->GetMetaObject(), 0, ID_METATREE_OPEN_MANAGER);
	return false;
}

