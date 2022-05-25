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
	eMarkAsDelete,
};

CObjectCatalog::actionData_t CObjectCatalog::GetActions(const form_identifier_t &formType)
{
	actionData_t catalogActions(this);
	catalogActions.AddAction("saveAndClose", _("Save and close"), eDefActionAndClose);
	catalogActions.AddAction("save", _("Save"), eSave);
	catalogActions.AddAction("copy", _("Copy"), eCopy);
	return catalogActions;
}

#include "frontend/visualView/controls/form.h"

void CObjectCatalog::ExecuteAction(const action_identifier_t &action, CValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteObject())
			srcForm->CloseForm();
		break;
	case eSave: WriteObject(); break;
	case eCopy: CopyObject().ShowValue(); break;
	}
}
