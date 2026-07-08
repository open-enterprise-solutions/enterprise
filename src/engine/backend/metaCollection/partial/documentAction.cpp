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

ibValueRecordDataObjectDocument::ibActionCollection ibValueRecordDataObjectDocument::GetActionCollection(const ibFormID &formType)
{
	ibActionCollection documentActions(this);
	documentActions.AddAction(wxT("PostAndClose"), _("Post and close"), g_picPostCLSID, true, eDefActionAndClose);
	documentActions.AddAction(wxT("Post"), _("Post"), g_picPostCLSID, true, ePost);
	documentActions.AddAction(wxT("ClearPosting"), _("Clear posting"), g_picSaveCLSID, true, eClearPosting);
	documentActions.AddSeparator();
	documentActions.AddAction(wxT("Generate"), _("Generate"), g_picGenerateCLSID, true, eGenerate);
	documentActions.AddSeparator();
	documentActions.AddAction(wxT("Clone"), _("Clone"), g_picCloneCLSID, true, eCopy);

	return documentActions;
}

void ibValueRecordDataObjectDocument::CallAsAction(const ibActionID &action, ibBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteObject(ibDocumentWriteMode::ibDocumentWriteMode_Posting, ibDocumentPostingMode::ibDocumentPostingMode_Regular)) {
			srcForm->CloseForm();
		}
		break;
	case ePost: WriteObject(ibDocumentWriteMode::ibDocumentWriteMode_Posting, ibDocumentPostingMode::ibDocumentPostingMode_Regular); break;
	case eClearPosting: WriteObject(ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting, ibDocumentPostingMode::ibDocumentPostingMode_Regular); break;
	case eGenerate: Generate(); break;
	case eCopy: CopyObject(true); break;
	}
}