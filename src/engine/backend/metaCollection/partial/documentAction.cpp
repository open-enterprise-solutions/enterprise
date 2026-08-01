////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document action
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Alert
#include "backend/picturePredefined.h"       // g_picPostCLSID / g_picSaveCLSID — the command band

enum action {
	eDefActionAndClose = 1,
	ePost,
	eClearPosting,
	eGenerate,
	eCopy,
	eMarkAsDelete,
};

ibValueRecordDataObjectDocument::ibStandardCommandSet ibValueRecordDataObjectDocument::GetStandardCommands(const ibFormID &formType)
{
	ibStandardCommandSet documentActions(this);
	documentActions.AddAction(wxT("PostAndClose"), _("Post and close"), g_picPostCLSID, true, eDefActionAndClose);
	documentActions.AddAction(wxT("Post"), _("Post"), g_picPostCLSID, true, ePost);
	documentActions.AddAction(wxT("ClearPosting"), _("Clear posting"), g_picSaveCLSID, true, eClearPosting);
	documentActions.AddSeparator();
	documentActions.AddAction(wxT("Generate"), _("Generate"), g_picGenerateCLSID, true, eGenerate).SetModify(false);   // creates a NEW object — doesn't modify THIS one
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

// Document metaobject COMMAND interface (source-command layer) — the writeable base set + Post / ClearPosting.
// Post/UndoPosting load the document by key and WriteObject it in the posting mode; the rest goes to the base.
void ibValueMetaObjectDocument::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	ibValueMetaObjectRecordDataMutableRef::GetCommandCollection(formType, commands);   // Add/Copy/Edit/Delete/MarkAsDelete
	commands.emplace_back(ePostValue,         wxT("Post"),         _("Post"),          g_picPostCLSID, true);
	commands.emplace_back(eClearPostingValue, wxT("ClearPosting"), _("Clear posting"), g_picSaveCLSID);
}

void ibValueMetaObjectDocument::CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	if (id == ePostValue || id == eClearPostingValue) {
		if (!key.IsOk()) return;
		try {
			ibValuePtr<ibValueRecordDataObjectRef> obj(CreateObjectValue(key));
			ibValueRecordDataObjectDocument* doc = nullptr;
			if (obj != nullptr && obj->ConvertToValue(doc) && doc != nullptr) {
				const ibDocumentWriteMode wm = (id == ePostValue)
					? ibDocumentWriteMode::ibDocumentWriteMode_Posting
					: ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting;
				doc->WriteObject(wm, ibDocumentPostingMode::ibDocumentPostingMode_Regular);
				if (srcForm != nullptr) srcForm->UpdateForm();
			}
		}
		// Catch the BASE, not ibBackendCoreException: a denied right and a lock conflict are
		// ibBackendException too, and catching only Core dropped them into the catch(...) below —
		// posting refused for lack of rights, and the user saw nothing but a line in the log.
		catch (const ibBackendInterruptException&) {}   // the user stopped it — nothing to report
		catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
		catch (...) { wxLogError(wxT("ibValueMetaObjectDocument::CallAsCommand: unhandled non-ibBackend exception swallowed")); }
		return;
	}
	ibValueMetaObjectRecordDataMutableRef::CallAsCommand(id, anchor, key, srcForm);   // Add/Copy/Edit/Delete/MarkAsDelete
}

