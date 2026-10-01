////////////////////////////////////////////////////////////////////////////
//	Description : event handler metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaEventHandlerObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectEventHandler::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	if (ibValueMetaObjectManagerModule* module = m_propertyHandlerModule->GetMetaObject())
		items.emplace_back(ibMetaMenuKind::Module, wxT("HandlerModule"), _("Open handler module"), module);
	return false;
}
