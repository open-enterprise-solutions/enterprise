////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog action
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"

enum
{
	eDefActionAndClose = 1,
	eSave,
	eCopy,
	eGenerate, 
	eMarkAsDelete,
};

ibValueRecordDataObjectCatalog::ibActionCollection ibValueRecordDataObjectCatalog::GetActionCollection(const ibFormID &formType)
{
	ibActionCollection catalogActions(this);
	catalogActions.AddAction(wxT("SaveAndClose"), _("Save and close"), g_picSaveCLSID, true, eDefActionAndClose);
	catalogActions.AddAction(wxT("Save"), _("Save"), g_picSaveCLSID, true, eSave);
	catalogActions.AddSeparator();
	catalogActions.AddAction(wxT("Generate"), _("Generate"), g_picGenerateCLSID, true, eGenerate);
	catalogActions.AddSeparator();
	catalogActions.AddAction(wxT("Clone"), _("Clone"), g_picCloneCLSID, true, eCopy);
	return catalogActions;
}

void ibValueRecordDataObjectCatalog::CallAsAction(const ibActionID &action, ibBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteObject())
			srcForm->CloseForm();
		break;
	case eSave: WriteObject(); break;
	case eGenerate: Generate(); break;
	case eCopy: CopyObject(true); break;
	}
}
