#include "metaInterfaceObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectInterface::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_INTERFACE, _("Open interface"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectInterface::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_INTERFACE)
		metaTree->OpenObjectForm(this);
}
