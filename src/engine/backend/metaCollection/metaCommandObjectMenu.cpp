////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : command metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaCommandObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectCommand::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	if (ibValueMetaObjectModule* module = m_propertyCommandModule->GetMetaObject())
		items.emplace_back(ibMetaMenuKind::Module, wxT("CommandModule"), _("Open command module"), module);
	return false;
}

