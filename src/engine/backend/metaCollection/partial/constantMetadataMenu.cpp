////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"

bool ibValueMetaObjectConstant::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, wxT("ConstantModule"), _("Open constant module"), m_propertyModule->GetMetaObject());
	return false;
}

