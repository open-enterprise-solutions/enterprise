////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of accounts action
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"

enum { eDefActionAndClose = 1, eSave, eCopy, eGenerate, eMarkAsDelete };

ibValueRecordDataObjectChartOfAccounts::ibActionCollection ibValueRecordDataObjectChartOfAccounts::GetActionCollection(const ibFormID& formType)
{
	ibActionCollection actions(this);
	actions.AddAction(wxT("SaveAndClose"), _("Save and close"), g_picSaveCLSID, true, eDefActionAndClose);
	actions.AddAction(wxT("Save"), _("Save"), g_picSaveCLSID, true, eSave);
	actions.AddSeparator();
	actions.AddAction(wxT("Generate"), _("Generate"), g_picGenerateCLSID, true, eGenerate);
	actions.AddSeparator();
	actions.AddAction(wxT("Clone"), _("Clone"), g_picCloneCLSID, true, eCopy);
	return actions;
}

void ibValueRecordDataObjectChartOfAccounts::ExecuteAction(const ibActionID& action, ibBackendValueForm* srcForm)
{
	switch (action) {
	case eDefActionAndClose: if (WriteObject()) srcForm->CloseForm(); break;
	case eSave: WriteObject(); break;
	case eGenerate: Generate(); break;
	case eCopy: CopyObject(true); break;
	}
}
