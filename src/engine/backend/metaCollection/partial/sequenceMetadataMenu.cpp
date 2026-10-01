////////////////////////////////////////////////////////////////////////////
//	Description : sequence metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "sequence.h"
#include "backend/metaData.h"

// The two modules a full record set has — offered here the way every kind with modules offers them.
bool ibValueMetaObjectSequence::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, wxT("RecordSetModule"), _("Open record set module"), m_propertyObjectModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, wxT("ManagerModule"), _("Open manager module"), m_propertyManagerModule->GetMetaObject());
	return false;
}
