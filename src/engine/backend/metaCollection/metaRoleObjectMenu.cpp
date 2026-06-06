#include "metaRoleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectRole::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_ROLE, _("Open role"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectRole::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_ROLE)
		metaTree->OpenObjectForm(this);
}