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

CRecordDataObjectDocument::actionData_t CRecordDataObjectDocument::GetActions(const form_identifier_t &formType)
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

void CRecordDataObjectDocument::ExecuteAction(const action_identifier_t &action, IBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteObject(eDocumentWriteMode::eDocumentWriteMode_Posting, eDocumentPostingMode::eDocumentPostingMode_Regular)) {
			srcForm->CloseForm();
		}
		break;
	case ePost: WriteObject(eDocumentWriteMode::eDocumentWriteMode_Posting, eDocumentPostingMode::eDocumentPostingMode_Regular); break;
	case eClearPosting: WriteObject(eDocumentWriteMode::eDocumentWriteMode_UndoPosting, eDocumentPostingMode::eDocumentPostingMode_Regular); break;
	case eGenerate: Generate(); break;
	case eCopy: CopyObject(true); break;
	}
}