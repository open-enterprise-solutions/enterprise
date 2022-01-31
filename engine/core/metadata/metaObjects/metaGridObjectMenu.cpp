////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metagrid menu
////////////////////////////////////////////////////////////////////////////

#include "metaGridObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaGridObject::PrepareContextMenu(wxMenu *defultMenu)
{
	wxMenuItem *m_menuItem = defultMenu->Append(ID_METATREE_OPEN_TEMPLATE, _("open template"));
	m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_MAKET));
	defultMenu->AppendSeparator();
	return false;
}

void CMetaGridObject::ProcessCommand(unsigned int id)
{
	IMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_TEMPLATE)
		metaTree->OpenFormMDI(this);
}