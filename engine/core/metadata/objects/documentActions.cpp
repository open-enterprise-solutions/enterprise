////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document action
////////////////////////////////////////////////////////////////////////////

#include "document.h"

enum
{
	eWrite = 1,
	eFill,
	eCopy,
	eMarkAsDelete,
	eDelete
};

CObjectDocumentValue::actionData_t CObjectDocumentValue::GetActions(form_identifier_t formType)
{
	actionData_t documentActions(this);

	documentActions.AddAction("write", eWrite);
	documentActions.AddAction("fill", eFill);
	documentActions.AddAction("copy", eCopy);

	return documentActions;
}

void CObjectDocumentValue::ExecuteAction(action_identifier_t action, CValueForm *srcForm)
{
	switch (action)
	{
	case eWrite: WriteObject(); break;
	case eFill: FillObject(CValue(this)); break;
	case eCopy: CopyObject().ShowValue(); break;
	case eDelete: DeleteObject(); break;
	}
}