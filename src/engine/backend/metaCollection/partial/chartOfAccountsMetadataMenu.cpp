////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of accounts metaData - menu
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"
#include "backend/metaData.h"

bool ibValueMetaObjectChartOfAccounts::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = nullptr;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open object module"));
	menuItem->SetBitmap((*m_propertyModuleObject)->GetIcon());
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open manager module"));
	menuItem->SetBitmap((*m_propertyModuleManager)->GetIcon());
	defaultMenu->AppendSeparator();
	menuItem = defaultMenu->Append(ID_METATREE_EDIT_PREDEFINED, _("Open predefined values"));
	menuItem->SetBitmap(ibBackendPicture::GetPicture(g_metaAttributeCLSID));
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectChartOfAccounts::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);
	if (id == ID_METATREE_OPEN_MODULE) metaTree->OpenFormMDI(m_propertyModuleObject->GetMetaObject());
	else if (id == ID_METATREE_OPEN_MANAGER) metaTree->OpenFormMDI(m_propertyModuleManager->GetMetaObject());
	else if (id == ID_METATREE_EDIT_PREDEFINED) metaTree->EditPredefinedValues(this);
}
