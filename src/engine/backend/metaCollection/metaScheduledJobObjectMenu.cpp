////////////////////////////////////////////////////////////////////////////
//	Description : scheduled job metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaScheduledJobObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectScheduledJob::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	if (ibValueMetaObjectManagerModule* module = m_propertyJobModule->GetMetaObject())
		items.emplace_back(ibMetaMenuKind::Module, wxT("JobModule"), _("Open job module"), module);
	return false;
}

