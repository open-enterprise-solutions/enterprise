#include "metaRoleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectRole::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = nullptr;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open role module"));
	menuItem->SetBitmap((*m_propertyRoleModule)->GetIcon());
	defaultMenu->AppendSeparator();
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_ROLE, _("Open role"));
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
	else if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenObjectForm(m_propertyRoleModule->GetMetaObject());
}