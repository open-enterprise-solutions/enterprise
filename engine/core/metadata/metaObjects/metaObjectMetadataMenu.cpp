////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metadata - menu
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool CMetaObject::PrepareContextMenu(wxMenu *defultMenu)
{
	wxMenuItem *m_menuItem = defultMenu->Append(ID_METATREE_OPEN_INIT_MODULE, _("open init module"));
	m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_MODULE));
	return true;
}

void CMetaObject::ProcessCommand(unsigned int id)
{
	IMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_INIT_MODULE)
		metaTree->OpenFormMDI(m_commonModule);
}