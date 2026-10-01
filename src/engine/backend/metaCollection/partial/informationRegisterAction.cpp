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

// 🛑 THE FORM WRITES `Write(False)` — a record is added or rewritten as itself, never onto another record's
// key. `Write(True)` replaces whatever stands under the key, which is what a script means by it; the form
// wrote that default, so a person giving a record dimensions another one already had silently overwrote
// that one (Max, 2026-09-24: "a new one, or an existing one changed to dimensions that are already there,
// must be refused — I remove or change that record myself"). Broken on 2026-09-17, when SaveData learned
// to replace on `Write(True)`. A record rewritten under its own key passes: SaveData takes the row it read
// out first, and the probe asks by the key the fields hold now.
void ibValueRecordManagerObjectInformationRegister::CallAsAction(const ibActionID &action, ibBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteRegister(false))
			srcForm->CloseForm();
		break;
	case eSave: WriteRegister(false);
		break;
	case eCopy: CopyRegister(true); 
		break;
	}
}