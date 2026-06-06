////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : register metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "backend/metadataConfiguration.h"

bool ibValueMetaObjectAccumulationRegister::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = nullptr;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open record set module"));
	menuItem->SetBitmap((*m_propertyObjectModule)->GetIcon());
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open manager module"));
	menuItem->SetBitmap((*m_propertyManagerModule)->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectAccumulationRegister::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenObjectForm(m_propertyObjectModule->GetMetaObject());
	else if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenObjectForm(m_propertyManagerModule->GetMetaObject());
}