////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame action
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "appData.h"
#include "metadata/objects/baseObject.h"

enum
{
	enClose = 10000,
	enUpdate, 
	enHelp,
};

//****************************************************************************
//*                              actions                                     *
//****************************************************************************

CValueForm::actionData_t CValueForm::GetActions(form_identifier_t formType)
{
	actionData_t action(this);
	action.AddAction("close", enClose);
	action.AddAction("update", enUpdate);
	action.AddAction("help", enHelp);

	if (m_sourceObject) {
		IActionSource *srcAction =
			dynamic_cast<IActionSource *>(m_sourceObject);
		if (srcAction) {
			srcAction->AddActions(action, formType);
		}
	}

	return action;
}

void CValueForm::ExecuteAction(action_identifier_t action, CValueForm *srcForm)
{
	if (!appData->EnterpriseMode()) {
		return;
	}

	switch (action)
	{
	case enClose: CloseForm(); break;
	case enUpdate: UpdateForm(); break;
	case enHelp: HelpForm(); break;
	default:
	{
		IActionSource *srcAction =
			dynamic_cast<IActionSource *>(CValueForm::GetSourceObject());
		if (srcAction) {
			srcAction->ExecuteAction(action, srcForm);
		} break;
	}
	}
}
