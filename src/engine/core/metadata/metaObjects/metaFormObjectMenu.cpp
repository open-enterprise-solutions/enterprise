////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform menu
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool IMetaFormObject::PrepareContextMenu(wxMenu *defultMenu)
{
	wxMenuItem *m_menuItem = defultMenu->Append(ID_METATREE_OPEN_FORM, _("open form"));
	m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_FORM));
	defultMenu->AppendSeparator();
	return false;
}

void IMetaFormObject::ProcessCommand(unsigned int id)
{
	IMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_FORM)
		metaTree->OpenFormMDI(this);
}