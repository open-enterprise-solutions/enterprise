////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaObjectDocument::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open module"));
	menuItem->SetBitmap(m_moduleObject->GetIcon());
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open manager"));
	menuItem->SetBitmap(m_moduleManager->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaObjectDocument::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenFormMDI(m_moduleObject);
	else if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenFormMDI(m_moduleManager);
}