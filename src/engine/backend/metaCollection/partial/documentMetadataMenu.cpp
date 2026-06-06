////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/metaData.h"

bool ibValueMetaObjectDocument::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open object module"));
	menuItem->SetBitmap((*m_propertyObjectModule)->GetIcon());
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open manager module"));
	menuItem->SetBitmap((*m_propertyManagerModule)->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectDocument::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenObjectForm(m_propertyObjectModule->GetMetaObject());
	else if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenObjectForm(m_propertyManagerModule->GetMetaObject());
}