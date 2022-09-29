////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document action
////////////////////////////////////////////////////////////////////////////

#include "document.h"

enum
{
	eDefActionAndClose = 1,
	ePost,
	eClearPosting,
	eGenerate,
	eCopy,
	eMarkAsDelete,
};

CObjectDocument::actionData_t CObjectDocument::GetActions(const form_identifier_t &formType)
{
	actionData_t documentActions(this);
	documentActions.AddAction("postAndClose", _("Post and close"), eDefActionAndClose);
	documentActions.AddAction("post", _("Post"), ePost);
	documentActions.AddAction("clearPosting", _("Clear posting"), eClearPosting);
	documentActions.AddSeparator();
	documentActions.AddAction("generate", _("Generate"), eGenerate);
	documentActions.AddSeparator();
	documentActions.AddAction("copy", _("Copy"), eCopy);

	return documentActions;
}

#include "frontend/visualView/controls/form.h"

void CObjectDocument::ExecuteAction(const action_identifier_t &action, CValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteObject(eDocumentWriteMode::ePosting, eDocumentPostingMode::eRegular))
			srcForm->CloseForm();
		break;
	case ePost: WriteObject(eDocumentWriteMode::ePosting, eDocumentPostingMode::eRegular); break;
	case eClearPosting: WriteObject(eDocumentWriteMode::eUndoPosting, eDocumentPostingMode::eRegular); break;
	case eGenerate: Generate(); break;
	case eCopy: CopyObject().ShowValue(); break;
	}
}