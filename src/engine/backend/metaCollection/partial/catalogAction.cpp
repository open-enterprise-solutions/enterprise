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

CRecordDataObjectCatalog::actionData_t CRecordDataObjectCatalog::GetActions(const form_identifier_t &formType)
{
	actionData_t catalogActions(this);
	catalogActions.AddAction("saveAndClose", _("Save and close"), eDefActionAndClose);
	catalogActions.AddAction("save", _("Save"), eSave);
	catalogActions.AddSeparator();
	catalogActions.AddAction("generate", _("Generate"), eGenerate);
	catalogActions.AddSeparator();
	catalogActions.AddAction("copy", _("Copy"), eCopy);
	return catalogActions;
}

void CRecordDataObjectCatalog::ExecuteAction(const action_identifier_t &action, IBackendValueForm* srcForm)
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
