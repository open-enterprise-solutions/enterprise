////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : spreadsheet menu
////////////////////////////////////////////////////////////////////////////

#include "metaSpreadsheetObject.h"
#include "backend/metaData.h"

bool ibValueMetaObjectSpreadsheetBase::PrepareContextMenu(wxMenu *defaultMenu)
{
	wxMenuItem *menuItem = defaultMenu->Append(ID_METATREE_OPEN_TEMPLATE, _("Open template"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectSpreadsheetBase::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree *metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_TEMPLATE)
		metaTree->OpenObjectForm(this);
}