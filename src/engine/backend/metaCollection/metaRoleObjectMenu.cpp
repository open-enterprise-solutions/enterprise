#include "metaRoleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectRole::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, ibT("RoleModule"), _("Open role module"), m_propertyRoleModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Object, ibT("Role"), _("Open role"), this);
	return false;
}

