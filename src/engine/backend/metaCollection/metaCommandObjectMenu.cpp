////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : command metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaCommandObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectCommand::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_COMMAND_MODULE, _("Open command module"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectCommand::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	// Open the command's OWN handler module (its CommandProcessing code) — the inner module, not the command node,
	// exactly as a common module opens its code. OpenObjectForm on a module opens its code document.
	if (id == ID_METATREE_OPEN_COMMAND_MODULE)
		if (ibValueMetaObjectModule* module = m_propertyCommandModule->GetMetaObject())
			metaTree->OpenObjectForm(module);
}
