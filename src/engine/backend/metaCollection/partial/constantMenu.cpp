////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"

bool CMetaObjectConstant::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = nullptr;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_CONSTANT_MANAGER, _("Open constant module"));
	menuItem->SetBitmap(m_moduleObject->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaObjectConstant::ProcessCommand(unsigned int id)
{
	IBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_CONSTANT_MANAGER)
		metaTree->OpenFormMDI(m_moduleObject);
}