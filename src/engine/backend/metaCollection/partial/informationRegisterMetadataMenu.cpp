////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : register metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "backend/metaData.h"

bool ibValueMetaObjectInformationRegister::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, wxT("RecordSetModule"), _("Open record set module"), m_propertyObjectModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, wxT("ManagerModule"), _("Open manager module"), m_propertyManagerModule->GetMetaObject());
	return false;
}

