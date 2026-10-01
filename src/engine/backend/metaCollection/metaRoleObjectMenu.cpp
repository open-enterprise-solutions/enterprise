#include "metaRoleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectRole::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, wxT("RoleModule"), _("Open role module"), m_propertyRoleModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Object, wxT("Role"), _("Open role"), this);
	return false;
}

