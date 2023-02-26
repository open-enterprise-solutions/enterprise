#include "constant.h"

enum
{
	eDefActionAndClose = 1,
	eSave,
};


CConstantObject::actionData_t CConstantObject::GetActions(const form_identifier_t& formType)
{
	actionData_t catalogActions(this);
	catalogActions.AddAction("saveAndClose", _("Save and close"), eDefActionAndClose);
	catalogActions.AddAction("save", _("Save"), eSave);
	return catalogActions;
}

#include "frontend/visualView/controls/form.h"

void CConstantObject::ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm)
{
	switch (lNumAction)
	{
	case eDefActionAndClose:
		if (SetConstValue(m_constVal))
			srcForm->CloseForm();
		break;
	case eSave: 
		SetConstValue(m_constVal); 
		break;
	}
}