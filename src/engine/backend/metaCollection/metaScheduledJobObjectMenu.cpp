////////////////////////////////////////////////////////////////////////////
//	Description : scheduled job metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaScheduledJobObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectScheduledJob::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_JOB_MODULE, _("Open job module"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectScheduledJob::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	// Opens the handler module's code document — the module, not the job node, exactly as a common
	// module or a command opens its own code.
	if (id == ID_METATREE_OPEN_JOB_MODULE)
		if (ibValueMetaObjectManagerModule* module = m_propertyJobModule->GetMetaObject())
			metaTree->OpenObjectForm(module);
}
