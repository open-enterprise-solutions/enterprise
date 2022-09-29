////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame action
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "appData.h"
#include "metadata/metaObjects/objects/object.h"

enum
{
	enClose = 10000,
	enUpdate,
	enHelp,
};

//****************************************************************************
//*                              actions                                     *
//****************************************************************************

CValueForm::actionData_t CValueForm::GetActions(const form_identifier_t& formType)
{
	actionData_t action(this);

	IActionSource* srcAction =
		dynamic_cast<IActionSource*>(CValueForm::GetSourceObject());

	if (srcAction != NULL) {
		srcAction->AddActions(action, formType);
	}

	action.AddAction("close", _("Close"), enClose);
	action.AddAction("update", _("Update"), enUpdate);
	action.AddAction("help", _("Help"), enHelp);

	return action;
}

void CValueForm::ExecuteAction(const action_identifier_t& action, CValueForm* srcForm)
{
	if (appData->DesignerMode()) {
		return;
	}

	switch (action)
	{
	case enClose: CloseForm(); break;
	case enUpdate: UpdateForm(); break;
	case enHelp: HelpForm(); break;
	default:
	{
		IActionSource* srcAction =
			dynamic_cast<IActionSource*>(CValueForm::GetSourceObject());
		if (srcAction != NULL) {
			srcAction->ExecuteAction(action, srcForm);
		} break;
	}
	}
}
