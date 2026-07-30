////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectConfiguration::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_INIT_MODULE, _("Open configuration module"));
	menuItem->SetBitmap((*m_propertyModuleConfiguration)->GetIcon());
	defaultMenu->AppendSeparator();
	wxMenuItem* homePageItem = defaultMenu->Append(ID_METATREE_EDIT_HOME_PAGE, _("Open home page workspace"));
	homePageItem->SetBitmap(ibBackendPicture::GetPicture(g_picHomePageCLSID));
	return true;
}

void ibValueMetaObjectConfiguration::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_INIT_MODULE)
		metaTree->OpenObjectForm(m_propertyModuleConfiguration->GetMetaObject());
	else if (id == ID_METATREE_EDIT_HOME_PAGE)
		metaTree->EditHomePage(this);
}