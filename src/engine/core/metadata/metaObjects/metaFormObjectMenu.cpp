////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform menu
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "frontend/metatree/metatreeWnd.h"

bool IMetaFormObject::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_FORM, _("Open form"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void IMetaFormObject::ProcessCommand(unsigned int id)
{
	IMetadataWrapperTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_FORM)
		metaTree->OpenFormMDI(this);
}