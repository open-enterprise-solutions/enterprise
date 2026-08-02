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
	ibStandardCommandSet reportActions(this);
	reportActions.AddAction(wxT("Compose"), _("Compose"), g_picGenerateCLSID, true, eCompose).SetModify(false);
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
// It composes nothing yet, and says so rather than pretending: a standard composition needs a
// declared composition SCHEMA on the metaobject — what to read and how to lay it out — and the
// Report metaobject has no such property today (it carries modules, forms and a default form,
// see dataReport.h). Until that schema exists there is nothing for the platform to compose FROM,
// and inventing one here would be a second, private notion of a report's data.
//
// So this is the seam, not a stub hiding a missing feature: a report written today sets
// StandartProcessing to FALSE and composes in its handler (data through the query / composer
// layer, presentation through the spreadsheet's areas — docs/report-engine.md § 1), which is
// exactly how every report already worked before the command existed. When the schema lands,
// its execution goes HERE and every existing report keeps working unchanged.
bool ibValueRecordDataObjectReport::DoStandardCompose() const
{
	return false;
}
