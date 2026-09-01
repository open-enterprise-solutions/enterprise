////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectConfiguration::CollectContextMenu(std::vector<ibMetaMenuItem>& items)
{
	items.emplace_back(ibMetaMenuKind::Module, wxT("ConfigurationModule"), _("Open configuration module"), m_propertyModuleConfiguration->GetMetaObject());
	items.emplace_back(ibMetaMenuKind::Module, wxT("SessionModule"), _("Open session module"), m_propertyModuleSession->GetMetaObject());
	items.emplace_back(wxT("HomePage"), _("Open home page workspace"), ID_METATREE_EDIT_HOME_PAGE, g_picHomePageCLSID);
	return true;
}

// The remainder — a MODAL editor, which has no metaobject for an item to name.
void ibValueMetaObjectConfiguration::ProcessCommand(unsigned int id)
{
	if (id == ID_METATREE_EDIT_HOME_PAGE)
		m_metaData->EditHomePage(this);
}
