////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister action
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"

enum
{
	eDefActionAndClose = 1,
	eSave,
	eCopy,
};

ibValueRecordManagerObjectInformationRegister::ibStandardCommandSet ibValueRecordManagerObjectInformationRegister::GetStandardCommands(const ibFormID &formType)
{
	ibStandardCommandSet registerActions(this);

	registerActions.AddAction(wxT("SaveAndClose"), _("Save and close"), g_picSaveCLSID, true, eDefActionAndClose);
	registerActions.AddAction(wxT("Save"), _("Save"), g_picSaveCLSID, true, eSave);
	registerActions.AddAction(wxT("Clone"), _("Clone"), g_picCloneCLSID, true, eCopy);

	return registerActions;
}

void ibValueRecordManagerObjectInformationRegister::CallAsAction(const ibActionID &action, ibBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteRegister())
			srcForm->CloseForm();
		break;
	case eSave: WriteRegister(); 
		break;
	case eCopy: CopyRegister(true); 
		break;
	}
}