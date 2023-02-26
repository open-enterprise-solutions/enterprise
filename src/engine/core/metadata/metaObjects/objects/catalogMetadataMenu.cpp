////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "frontend/metatree/metaTreeWnd.h"

bool CMetaObjectCatalog::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = NULL;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open module"));
	menuItem->SetBitmap(m_moduleObject->GetIcon());
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open manager"));
	menuItem->SetBitmap(m_moduleManager->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaObjectCatalog::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenFormMDI(m_moduleObject);
	else if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenFormMDI(m_moduleManager);
}