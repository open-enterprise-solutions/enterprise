////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of characteristic types action
////////////////////////////////////////////////////////////////////////////

#include "chartOfCharacteristicTypes.h"

enum
{
	eDefActionAndClose = 1,
	eSave,
	eCopy,
	eGenerate,
	eMarkAsDelete,
};

ibValueRecordDataObjectChartOfCharacteristicTypes::ibActionCollection ibValueRecordDataObjectChartOfCharacteristicTypes::GetActionCollection(const ibFormID& formType)
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

void ibValueRecordDataObjectChartOfCharacteristicTypes::ExecuteAction(const ibActionID& action, ibBackendValueForm* srcForm)
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
