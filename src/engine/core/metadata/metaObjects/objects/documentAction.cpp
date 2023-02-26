////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document action
////////////////////////////////////////////////////////////////////////////

#include "document.h"

enum action {
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
		if (WriteObject(eDocumentWriteMode::eDocumentWriteMode_Posting, eDocumentPostingMode::eDocumentPostingMode_Regular))
			srcForm->CloseForm();
		break;
	case ePost: WriteObject(eDocumentWriteMode::eDocumentWriteMode_Posting, eDocumentPostingMode::eDocumentPostingMode_Regular); break;
	case eClearPosting: WriteObject(eDocumentWriteMode::eDocumentWriteMode_UndoPosting, eDocumentPostingMode::eDocumentPostingMode_Regular); break;
	case eGenerate: Generate(); break;
	case eCopy: CopyObject(true); break;
	}
}