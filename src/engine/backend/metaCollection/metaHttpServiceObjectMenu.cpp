////////////////////////////////////////////////////////////////////////////
//	Description : HTTP service metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaHttpServiceObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectHttpService::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	if (ibValueMetaObjectManagerModule* module = m_propertyServiceModule->GetMetaObject())
		items.emplace_back(ibMetaMenuKind::Module, ibT("ServiceModule"), _("Open service module"), module);
	return false;
}
