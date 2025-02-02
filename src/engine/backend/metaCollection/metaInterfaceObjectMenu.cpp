#include "metaInterfaceObject.h"
#include "backend/metaData.h"

bool CMetaObjectInterface::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_INTERFACE, _("Open interface"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaObjectInterface::ProcessCommand(unsigned int id)
{
	IBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_INTERFACE)
		metaTree->OpenFormMDI(this);
}