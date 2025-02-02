#include "metaRoleObject.h"
#include "backend/metaData.h"

bool CMetaObjectRole::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_ROLE, _("Open role"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaObjectRole::ProcessCommand(unsigned int id)
{
	IBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_ROLE)
		metaTree->OpenFormMDI(this);
}