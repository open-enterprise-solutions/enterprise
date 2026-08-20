////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "backend/picturePredefined.h"   // g_picGenerateCLSID — the command band

enum action {
	eCompose = 1,
};

// A report's ONE standard command. The platform owns the command; the report's object module
// owns what it means — exactly the split the document has between Post and its Posting handler.
//
// COMPOSE, not "generate": in this tree Generation already means entering one object ON THE BASIS
// of another (ibValueRecordDataObjectRef::Generate, the document's Generate command). A report
// does not generate anything — it COMPOSES a result, which is the word the rest of the stack
// already uses for it (docs/data-composer.md, ibQueryComposer).
//
// SetModify(false): a report reads, it never writes, so the command stays live on a view-only
// form (the greying reads modifiesData — see docs/view-only.md).
ibValueRecordDataObjectReport::ibStandardCommandSet ibValueRecordDataObjectReport::GetStandardCommands(const ibFormID &formType)
{
	// ⭐⭐ COMPOSE BELONGS TO THE COMPOSER, AND ONLY THERE (Max, 2026-08-20: "all of that logic is
	// inside the composer — I press Compose, the composer does everything and shows the result").
	//
	// The object used to carry a Compose command of its own. With a composer declared, that is the
	// SAME button twice: the box already carries Compose and Settings whenever its source resolves
	// to a composition, and a report object resolves to its default composer
	// (ibValueGridBox::ResolveComposition). Two buttons doing one thing is how they start meaning
	// slightly different things.
	//
	// The script seam is NOT gone: Composing() / DoStandardCompose() below are still the report's
	// twin of a document's Posting, callable from code — what was removed is a second doorway to
	// them, not the door. A report that needs a button of its own declares a command.
	ibStandardCommandSet reportActions(this);
	return reportActions;
}

void ibValueRecordDataObjectReport::CallAsAction(const ibActionID &action, ibBackendValueForm *srcForm)
{
	switch (action)
	{
	case eCompose: Composing(); break;
	}
}

// Run the report. The handler's argument is StandartProcessing, NOT a cancel flag — the same
// contract Filling / SetNewCode / SetNewNumber use: it arrives TRUE, meaning "the platform will
// compose this itself", and a handler that composes the result on its own sets it FALSE to say
// "already done, stand down". So the two roles are:
//
//   left TRUE   -> the PLATFORM composes (DoStandardCompose below)
//   set FALSE   -> the SCRIPT composed it; the platform does nothing further
//
// The handler is a DECLARED default procedure (ibValueMetaObjectReport's ctor), so the designer
// lists it in the object module the way it lists a document's Posting. A report that has not
// written one leaves the flag TRUE and takes the platform path.
bool ibValueRecordDataObjectReport::Composing() const
{
	ibValue standartProcessing = true;
	ExecAsProc(wxT("Composing"), standartProcessing);
	if (!standartProcessing.GetBoolean())
		return true;                 // the script composed it — nothing left for us to do
	return DoStandardCompose();
}

// The PLATFORM's own composition — the branch taken when the handler leaves StandartProcessing
// TRUE (including the report with no handler at all).
//
// ⭐ WHAT IT READS NOW EXISTS — the default composer says what to read and how to lay it out. What
// is still missing is the other half: WHERE the result goes. A composition composes INTO a
// spreadsheet document, and the document belongs to the control that shows it — the gridbox owns
// it, rents a background run and publishes the finished sheet in one step (gridBoxAction.cpp).
// The object has no window and no document, so composing here would produce a sheet nobody holds.
//
// So the running path is the gridbox's Compose command, which the generated form carries because a
// report object resolves to its default composer. This stays false until the object owns a document
// of its own — and false is the honest answer, not a stub: a report with no default composer
// composes in its handler exactly as it always did (docs/report-engine.md §1).
bool ibValueRecordDataObjectReport::DoStandardCompose() const
{
	return false;
}

// ⭐ THE MODEL'S VERB, for a report: the HANDLER SPEAKS FIRST, then the platform composes.
//
// `Composing` is the report's twin of a document's Posting — it arrives with StandartProcessing
// TRUE and a handler that filled the sheet itself sets it FALSE. So an author can take the
// composer's settings and write the result by hand, and the platform stands down (Max, 2026-08-20).
// Nothing written? Then the default composer composes into the very document the caller handed us.
bool ibValueRecordDataObjectReport::Compose(ibBackendSpreadsheetObject* document)
{
	if (Composing())
		return true;   // the script composed it — the sheet is filled

	ibValueDataComposition* composition = GetDefaultComposition();
	return composition != nullptr && composition->Compose(document);
}
