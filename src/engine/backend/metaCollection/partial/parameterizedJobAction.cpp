////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — actions & source commands
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJob.h"

#include "backend/system/systemManager.h"   // ibValueSystemFunction::Alert
#include "backend/picturePredefined.h"       // the command band's pictures

enum action {
	eExecute = 1,
	eDefActionAndClose,
	eSave,
	eCopy,
	eMarkAsDelete,
};

// The CARD's command band. EXECUTE COMES FIRST — it is what this object exists for; writing it is
// the preparation, not the point. Then a separator, then the ordinary write pair, then the rest.
//
// It carries SetModify(false): running a job changes the WORLD, not this open object, so the form
// must not come back thinking it has unsaved edits.
ibValueRecordDataObjectParameterizedJob::ibStandardCommandSet ibValueRecordDataObjectParameterizedJob::GetStandardCommands(const ibFormID& formType)
{
	ibStandardCommandSet jobActions(this);
	jobActions.AddAction(wxT("Execute"), _("Execute"), g_picExecuteJobCLSID, true, eExecute).SetModify(false);
	jobActions.AddSeparator();
	jobActions.AddAction(wxT("SaveAndClose"), _("Save and close"), g_picSaveCLSID, true, eDefActionAndClose);
	jobActions.AddAction(wxT("Save"), _("Save"), g_picSaveCLSID, true, eSave);
	jobActions.AddSeparator();
	jobActions.AddAction(wxT("Clone"), _("Clone"), g_picCloneCLSID, true, eCopy);
	return jobActions;
}


void ibValueRecordDataObjectParameterizedJob::CallAsAction(const ibActionID& action, ibBackendValueForm* srcForm)
{
	switch (action)
	{
	case eDefActionAndClose:
		if (WriteObject())
			srcForm->CloseForm();
		break;
	case eSave: WriteObject(); break;
	case eExecute:
	{
		// WRITE FIRST, then run. The job reads its own row, so an edit still on screen would
		// simply not be there when the handler looked — and a write that must not happen says so
		// itself, through its own refusal, which is why nothing is asked here.
		if (!WriteObject())
			break;

		try {
			// The run is QUEUED, so nothing here waits for it. The dates on the card move when the
			// run finishes and the card is next refreshed — the alternative is a window frozen for
			// the length of somebody's exchange.
			ExecuteJob();
		}
		catch (const ibBackendInterruptException&) {}   // the user stopped it — nothing to report
		catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
		break;
	}
	case eCopy: CopyObject(true); break;
	}
}

// The LIST's command interface (source-command layer) — the writeable + folder base set plus
// Execute. Same shape a document's Post has: load the row by key and run it, knowing nothing about
// who pressed the button.
void ibValueMetaObjectParameterizedJob::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	ibValueMetaObjectRecordDataHierarchyMutableRef::GetCommandCollection(formType, commands);
	commands.emplace_back(eExecuteValue, wxT("Execute"), _("Execute"), g_picPostCLSID, true);
}

void ibValueMetaObjectParameterizedJob::CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	if (id == eExecuteValue) {
		if (!key.IsOk()) return;
		try {
			ibValuePtr<ibValueRecordDataObjectRef> obj(CreateObjectValue(ibObjectMode::OBJECT_ITEM, key));
			ibValueRecordDataObjectParameterizedJob* job = nullptr;
			if (obj != nullptr && obj->ConvertToValue(job) && job != nullptr) {
				job->ExecuteJob();
				if (srcForm != nullptr) srcForm->UpdateForm();
			}
		}
		// Catch the BASE, not ibBackendCoreException: a denied right and a lock conflict are
		// ibBackendException too, and catching only Core would drop them into catch(...) — the run
		// refused for lack of rights, with nothing but a line in the log to say so.
		catch (const ibBackendInterruptException&) {}
		catch (const ibBackendException& err) { ibValueSystemFunction::Alert(err.GetErrorDescription()); }
		catch (...) { wxLogError(wxT("ibValueMetaObjectParameterizedJob::CallAsCommand: unhandled non-ibBackend exception swallowed")); }
		return;
	}

	ibValueMetaObjectRecordDataHierarchyMutableRef::CallAsCommand(id, anchor, key, srcForm);
}
