#include "metaCommonAttributeObject.h"
#include "backend/metaData.h"

// The two context menus of the common-attribute pair, kept apart from the mechanism the
// way every other metatype keeps them (metaSectionObjectMenu.cpp, metaScheduledJobObjectMenu.cpp).
//
// They are opposites, and that is the whole story of this feature in two functions: the
// DECLARATION offers the one thing worth doing to it — saying which objects carry it —
// while the COPY offers nothing but a way back to the declaration, because everything
// about a copy is decided there.

//***********************************************************************
//*                     The declaration, under Common                   *
//***********************************************************************

bool ibValueMetaObjectCommonAttribute::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Object, wxT("Composition"), _("Open common attributes composition"), this);
	return false;
}


//***********************************************************************
//*                   The copy that lives in an object                  *
//***********************************************************************

bool ibValueMetaObjectCommonAttributeColumn::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	if (ibValueMetaObject* source = GetSource())
		items.emplace_back(ibMetaMenuKind::Object, wxT("Declaration"), _("Go to common attribute"), source);
	return true;
}

