////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — designer menu
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJob.h"

#include "backend/metaData.h"

bool ibValueMetaObjectParameterizedJob::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, ibT("ObjectModule"), _("Open object module"), m_propertyObjectModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, ibT("JobModule"), _("Open job module"), m_propertyManagerModule->GetMetaObject());
	return false;
}
