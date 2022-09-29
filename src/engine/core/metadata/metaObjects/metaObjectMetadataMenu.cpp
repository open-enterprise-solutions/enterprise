////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaObject::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_INIT_MODULE, _("Open init module"));
	menuItem->SetBitmap(m_commonModule->GetIcon());
	return true;
}

void CMetaObject::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_INIT_MODULE)
		metaTree->OpenFormMDI(m_commonModule);
}