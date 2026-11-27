////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document action
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Alert, CurrentDate — the day a posting is asked about
#include "backend/session/session.h"        // ibSession::CurrentFrame — whom the posting mode is asked of
#include "backend/backend_mainFrame.h"      // ibBackendDocFrame::ShowModalMessage
#include "protocol/protocol.h"              // ibStyle — the question's buttons, and the one pressed
#include "core/picturePredefined.h"       // g_picPostCLSID / g_picSaveCLSID — the command band

#include <optional>

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
	documentActions.AddAction(ibT("PostAndClose"), _("Post and close"), g_picPostCLSID, true, eDefActionAndClose).SetDefault(true);
	documentActions.AddAction(ibT("Post"), _("Post"), g_picPostCLSID, true, ePost);
	documentActions.AddAction(ibT("ClearPosting"), _("Clear posting"), g_picSaveCLSID, true, eClearPosting);
	documentActions.AddSeparator();
	documentActions.AddAction(ibT("Generate"), _("Generate"), g_picGenerateCLSID, true, eGenerate).SetModify(false);   // creates a NEW object — doesn't modify THIS one
	documentActions.AddSeparator();
	documentActions.AddAction(ibT("Clone"), _("Clone"), g_picCloneCLSID, true, eCopy);

	return documentActions;
}

namespace {

// The mode a PERSON posts a document in — asked before the posting opens its transaction. A document dated
// today may be posted in real time, taking the moment it is posted as its date, or regularly, keeping the
// date it has: Yes is real time, No is regular, Cancel posts nothing. A document of another day is posted
// regularly — that is how one is posted back-dated — and so is one posted where there is no window to ask in,
// as the "save changes?" question of a closing form answers itself (docView.cpp).
std::optional<ibDocumentPostingMode> AskPostingMode(const ibValueRecordDataObjectDocument& document)
{
	const ibDateTime date = document.GetValueByMetaID(*document.GetMetaObject()->GetDocumentDate()).GetDate();
	const ibDateTime now = ibValueSystemFunction::CurrentDate().GetDate();
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || date.BeginOfPeriod(ibTotalsPeriod::Day) != now.BeginOfPeriod(ibTotalsPeriod::Day))
		return ibDocumentPostingMode::ibDocumentPostingMode_Regular;

	namespace style = ibProtocol::ibRequest::ibMessage::ibStyle;
	switch (frame->ShowModalMessage(
		ibString::Format(_("Post %s in real time?\nYes: the document is dated by the current moment. No: its date stays as it is."),
			document.GetSourceCaption()),
		ibString(),   // the client's own caption
		style::YesNo | style::Cancel | style::IconQuestion | style::Centre)) {
	case style::Yes: return ibDocumentPostingMode::ibDocumentPostingMode_RealTime;
	case style::No:  return ibDocumentPostingMode::ibDocumentPostingMode_Regular;
	}
	return std::nullopt;
}

}

void ibValueRecordDataObjectDocument::CallAsAction(const ibActionID &action, ibBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (const std::optional<ibDocumentPostingMode> mode = AskPostingMode(*this)) {
			if (WriteObject(ibDocumentWriteMode::ibDocumentWriteMode_Posting, *mode))
				srcForm->CloseForm();
		}
		break;
	case ePost:
		if (const std::optional<ibDocumentPostingMode> mode = AskPostingMode(*this))
			WriteObject(ibDocumentWriteMode::ibDocumentWriteMode_Posting, *mode);
		break;
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
	commands.emplace_back(ePostValue,         ibT("Post"),         _("Post"),          g_picPostCLSID, true);
	commands.emplace_back(eClearPostingValue, ibT("ClearPosting"), _("Clear posting"), g_picSaveCLSID);
}

// The document's row, in three states: marked for deletion, posted, or neither — each read off the row's own
// cell, as the reference is (commonObjectAction.cpp). The mark is asked first: a row marked for deletion says
// so whatever its posting.
ibPictureID ibValueMetaObjectDocument::GetRowPicture(const ibRowMetaValues& rowValues) const
{
	const auto flag = [&rowValues](const ibValueMetaObjectAttributePredefined* attribute) {
		if (attribute == nullptr)
			return false;
		const ibRowMetaValues::const_iterator it = rowValues.find(attribute->GetMetaID());
		return it != rowValues.end() && it->second.GetBoolean();
	};

	if (flag(GetDataDeletionMark()))
		return g_picRowDocumentDeletedCLSID;
	return flag(GetDocumentPosted()) ? g_picRowDocumentPostedCLSID : g_picRowDocumentCLSID;
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
				// A posting asks its mode, as the form's does; undoing one has none to ask.
				const std::optional<ibDocumentPostingMode> mode = (id == ePostValue)
					? AskPostingMode(*doc)
					: ibDocumentPostingMode::ibDocumentPostingMode_Regular;
				if (mode) {
					doc->WriteObject(wm, *mode);
					if (srcForm != nullptr) srcForm->UpdateForm();
				}
			}
		}
		// Catch the BASE, not ibBackendCoreException: a denied right and a lock conflict are
		// ibBackendException too, and catching only Core dropped them into the catch(...) below —
		// posting refused for lack of rights, and the user saw nothing but a line in the log.
		catch (const ibBackendInterruptException&) {}   // the user stopped it — nothing to report
		catch (const ibCoreException&) {}   // already reported where it happened - see ProcessExceptionError
		catch (...) { ibJournalError(ibT("metadata.action"),ibT("ibValueMetaObjectDocument::CallAsCommand: unhandled non-ibBackend exception swallowed")); }
		return;
	}
	ibValueMetaObjectRecordDataMutableRef::CallAsCommand(id, anchor, key, srcForm);   // Add/Copy/Edit/Delete/MarkAsDelete
}

