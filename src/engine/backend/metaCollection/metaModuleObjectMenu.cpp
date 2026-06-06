////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule menu
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectCommonModule::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open module"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectCommonModule::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenObjectForm(this);
}
