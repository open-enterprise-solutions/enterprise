////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metagrid menu
////////////////////////////////////////////////////////////////////////////

#include "metaGridObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaGridObject::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_TEMPLATE, _("Open template"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaGridObject::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_TEMPLATE)
		metaTree->OpenFormMDI(this);
}