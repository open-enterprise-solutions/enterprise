////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "enumeration.h"
#include "backend/metaData.h"

bool ibValueMetaObjectEnumeration::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open manager module"));
	menuItem->SetBitmap((*m_propertyManagerModule)->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectEnumeration::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenObjectForm(m_propertyManagerModule->GetMetaObject());
}