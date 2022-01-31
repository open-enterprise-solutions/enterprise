////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog action
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"

enum
{
	eWrite = 1,
	eFill,
	eCopy,
	eMarkAsDelete,
	eDelete
};

CObjectCatalogValue::actionData_t CObjectCatalogValue::GetActions(form_identifier_t formType)
{
	actionData_t catalogActions(this);
	catalogActions.AddAction("write", eWrite);
	catalogActions.AddAction("fill", eFill);
	catalogActions.AddAction("copy", eCopy);
	return catalogActions;
}

void CObjectCatalogValue::ExecuteAction(action_identifier_t action, CValueForm *srcForm)
{
	switch (action)
	{
	case eWrite: WriteObject(); break;
	case eFill: FillObject(CValue(this)); break;
	case eCopy: CopyObject().ShowValue(); break;
	case eDelete: DeleteObject(); break;
	}
}
