////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaConstantObject::PrepareContextMenu(wxMenu *defultMenu)
{
	wxMenuItem *m_menuItem = NULL;
	m_menuItem = defultMenu->Append(ID_METATREE_OPEN_CONSTANT_MANAGER, _("open constant module"));
	m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_MODULE));
	defultMenu->AppendSeparator();
	return false;
}

void CMetaConstantObject::ProcessCommand(unsigned int id)
{
	IMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_CONSTANT_MANAGER)
		metaTree->OpenFormMDI(m_moduleObject);
}