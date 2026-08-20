////////////////////////////////////////////////////////////////////////////
//	Description : composer menu
////////////////////////////////////////////////////////////////////////////

#include "metaComposerObject.h"
#include "backend/metaData.h"

// "Open composer" — the same gesture a template and a form answer to, and it opens the settings
// window that already exists (query / resources / output), not a second editor of its own.
bool ibValueMetaObjectComposer::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_COMPOSER, _("Open data composer"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectComposer::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_COMPOSER)
		metaTree->OpenObjectForm(this);
}
