////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : spreadsheet menu
////////////////////////////////////////////////////////////////////////////

#include "metaSpreadsheetObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectSpreadsheetBase::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Object, ibT("Template"), _("Open template"), this);
	return false;
}

