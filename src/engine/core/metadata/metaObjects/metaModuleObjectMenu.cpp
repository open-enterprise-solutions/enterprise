////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule menu
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaCommonModuleObject::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open module"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaCommonModuleObject::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenFormMDI(this);
}
