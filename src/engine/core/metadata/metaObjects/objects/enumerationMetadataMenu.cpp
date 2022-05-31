////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "enumeration.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaObjectEnumeration::PrepareContextMenu(wxMenu *defultMenu)
{
	wxMenuItem *m_menuItem = NULL;
	m_menuItem = defultMenu->Append(ID_METATREE_OPEN_MANAGER, _("open manager"));
	m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_MODULE));
	defultMenu->AppendSeparator();
	return false;
}

void CMetaObjectEnumeration::ProcessCommand(unsigned int id)
{
	IMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenFormMDI(m_moduleManager);
}