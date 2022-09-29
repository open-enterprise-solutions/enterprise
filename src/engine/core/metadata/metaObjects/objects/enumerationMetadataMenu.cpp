////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "enumeration.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaObjectEnumeration::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open manager"));
	menuItem->SetBitmap(m_moduleManager->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaObjectEnumeration::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenFormMDI(m_moduleManager);
}