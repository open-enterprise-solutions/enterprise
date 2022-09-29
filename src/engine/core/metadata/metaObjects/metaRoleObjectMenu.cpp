#include "metaRoleObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaRoleObject::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_ROLE, _("Open role"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaRoleObject::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_ROLE)
		metaTree->OpenFormMDI(this);
}