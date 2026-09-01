////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "backend/metaData.h"

bool ibValueMetaObjectCatalog::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, wxT("ObjectModule"), _("Open object module"), m_propertyObjectModule->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, wxT("ManagerModule"), _("Open manager module"), m_propertyManagerModule->GetMetaObject());
	items.emplace_back(wxT("PredefinedValues"), _("Open predefined values"), ID_METATREE_EDIT_PREDEFINED, g_metaAttributeCLSID);
	return false;
}

// The remainder — a MODAL editor, which has no metaobject for an item to name.
void ibValueMetaObjectCatalog::ProcessCommand(unsigned int id)
{
	if (id == ID_METATREE_EDIT_PREDEFINED)
		m_metaData->EditPredefinedValues(this);
}
