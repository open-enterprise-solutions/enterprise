////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule menu
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaCommonModuleObject::PrepareContextMenu(wxMenu *defultMenu)
{
	wxMenuItem *m_menuItem = defultMenu->Append(ID_METATREE_OPEN_MODULE, _("open module"));
	m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_MODULE));
	defultMenu->AppendSeparator();
	return false;
}

void CMetaCommonModuleObject::ProcessCommand(unsigned int id)
{
	IMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenFormMDI(this);
}
