////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculation register metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"
#include "backend/metaData.h"

bool ibValueMetaObjectCalculationRegister::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, ibT("RecordSetModule"), _("Open record set module"), m_propertyObjectModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, ibT("ManagerModule"), _("Open manager module"), m_propertyManagerModule->GetMetaObject());
	return false;
}
