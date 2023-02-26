#include "metaInterfaceObject.h"
#include "frontend/metatree/metaTreeWnd.h"

bool CMetaInterfaceObject::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_INTERFACE, _("Open interface"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaInterfaceObject::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_INTERFACE)
		metaTree->OpenFormMDI(this);
}