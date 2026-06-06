////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"

bool ibValueMetaObjectConstant::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = nullptr;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_CONSTANT_MANAGER, _("Open constant module"));
	menuItem->SetBitmap(m_propertyModule->GetMetaObject()->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectConstant::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_CONSTANT_MANAGER)
		metaTree->OpenObjectForm(m_propertyModule->GetMetaObject());
}