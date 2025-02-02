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

CRecordManagerObjectInformationRegister::actionData_t CRecordManagerObjectInformationRegister::GetActions(const form_identifier_t &formType)
{
	actionData_t registerActions(this);

	registerActions.AddAction("saveAndClose", _("Save and close"), eDefActionAndClose);
	registerActions.AddAction("save", _("Save"), eSave);
	registerActions.AddAction("copy", _("Copy"), eCopy);

	return registerActions;
}

void CRecordManagerObjectInformationRegister::ExecuteAction(const action_identifier_t &action, IBackendValueForm* srcForm)
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