////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform menu
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectFormBase::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_FORM, _("Open form"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectFormBase::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_FORM)
		metaTree->OpenObjectForm(this);
}