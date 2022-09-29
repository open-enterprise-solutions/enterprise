////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaConstantObject::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = NULL;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_CONSTANT_MANAGER, _("Open constant module"));
	menuItem->SetBitmap(m_moduleObject->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void CMetaConstantObject::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_CONSTANT_MANAGER)
		metaTree->OpenFormMDI(m_moduleObject);
}