////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject root metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectApplication::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, ibT("ApplicationModule"), _("Open application module"), m_propertyModuleApplication->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, ibT("SessionModule"), _("Open session module"), m_propertyModuleSession->GetMetaObject());
	items.emplace_back(ibT("HomePage"), _("Open home page workspace"), ID_METATREE_EDIT_HOME_PAGE, g_picHomePageCLSID);
	return true;
}

// The remainder — a MODAL editor, which has no metaobject for an item to name.
void ibValueMetaObjectApplication::ProcessCommand(unsigned int id)
{
	if (id == ID_METATREE_EDIT_HOME_PAGE)
		m_metaData->EditHomePage(this);
}
