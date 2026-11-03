////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : template menu
////////////////////////////////////////////////////////////////////////////

#include "metaTemplateObject.h"
#include "backend/metaData.h"

// "Open" — the kind's editor, as the provider of the kind says.
bool ibValueMetaObjectTemplateBase::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	GetTemplateProvider().CollectContextMenu(this, items);
	return false;
}

