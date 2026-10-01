#ifndef __SPREADSHEET_MODEL_H__
#define __SPREADSHEET_MODEL_H__

#include "backend/compiler/value.h"
#include "backend/backend_spreadsheet.h"
#include "backend/standardCommand.h"   // ibCommandItem — the model is a command STORE, like a table's
#include "backend/composition/dataComposer.h"   // L5 — every sheet model has one; a drawn document's is empty

#include <functional>
#include <vector>

// ⭐ THE VERBS A MODEL MAY OFFER, named once so the store and whoever runs them cannot disagree.
// A COMPOSER lists both (it reads, and it has settings); a spreadsheet document lists neither — it
// is already its own result (Max, 2026-08-20: "the document has no commands, it just returns empty;
// the composer returns its own set").
//
// The ids live HERE rather than inside a control because the store names them: a gridbox lays out
// what it is given and hands the id back, and a second control showing the same model would too.
enum ibSpreadsheetModelCommand
{
	ibSpreadsheetModelCommand_Compose = 21000,
	ibSpreadsheetModelCommand_Settings,
	// ⭐ THE VARIANTS — the ones the author named, offered as a menu. Picking one IS setting the
	// reader's setting (ibDialogComposerSettings::ShowVariantPicker), so this is a verb of the
	// COMPOSITION exactly as the two above are: it reads by a setting, and a variant is a setting
	// somebody named.
	ibSpreadsheetModelCommand_Variants,
	// ⭐ THE READER'S OWN — the settings THEY saved, and TWO verbs rather than one menu (Max,
	// 2026-08-26): saving asks WHERE to put what is in force, restoring asks WHICH one to put on.
	// They are opposite acts and a person reaches for one of them knowing which; a single button
	// would make them choose the act inside the menu instead of before opening it.
	ibSpreadsheetModelCommand_RestoreSettings,
	ibSpreadsheetModelCommand_SaveSettings,
};

// ⭐⭐ THE SPREADSHEET MODEL — ABSTRACT, and the centre of everything a gridbox does, exactly the
// way ibValueModel (backend/tabularModel.h) is the centre of a table. It inherits the runtime directly, so
// the things below it ARE values, and it is held by reference like any other.
//
//      runtime
//        └── spreadsheet model          (this — abstract: nobody instantiates it)
//              ├── spreadsheet document (the hand-filled sheet)
//              └── composer             (builds one)
//
// ⚠ THE FETCH HERE IS NOT THE TABLE'S. A list reads in PORTIONS — anchor, direction, page — and
// lives by them. A spreadsheet document does not: the request goes out and the result arrives
// WHOLE, one sheet (Max, 2026-08-20: "the model of the spreadsheet doc has its own fetch — there it
// is portions, here it is sent and shown at once; different behaviours"). That is why this base
// exists at all instead of the composer riding the cursor model it was copied from.
//
// Three things it answers, and the thing below says what it is:
//
//   * GetSpreadsheetDocument — the sheet it holds, which the rendering control draws;
//   * Compose                — fill that sheet;
//   * GetCommandCollection   — what may be done to it (a composer has verbs, a sheet has none).
class BACKEND_API ibValueSpreadsheetModel : public ibValueDynamicMembers {
public:

	// ⭐ THE SHEET IS CREATED HERE, ALWAYS — not by each descendant (Max, 2026-08-20). Holding one is
	// what makes something a model of a spreadsheet, so a model without a sheet is not a state that
	// exists: a composer that has never run and a document nobody typed into both have an empty one.
	ibValueSpreadsheetModel()
		: ibValueDynamicMembers(), m_spreadsheetDoc(new ibBackendSpreadsheetObject()) {}
	explicit ibValueSpreadsheetModel(const ibSpreadsheetDescription& spreadsheetDesc)
		: ibValueDynamicMembers(), m_spreadsheetDoc(new ibBackendSpreadsheetObject(spreadsheetDesc)) {}

	// ⭐⭐ THE MODEL HOLDS THE BACKEND SHEET AND HANDS IT BACK. That is what the two kinds have in
	// common and the whole reason this base exists (Max, 2026-08-20: "the model contains the backend
	// spreadsheet object; its job is to return it, and it knows how to get a value out of it").
	//
	// The rendering control asks for it and draws it — it never learns whether a query ran.
	wxObjectDataPtr<ibBackendSpreadsheetObject> GetSpreadsheetDocument() const { return m_spreadsheetDoc; }

	// ⭐ AND THE ONE ACT THAT DIFFERS: produce the data.
	//
	//   * a COMPOSER reads its query, lays the result out and INSTALLS the finished sheet as its own;
	//   * a hand-filled DOCUMENT already IS the result — nothing to produce.
	virtual bool Compose() = 0;

	// ⭐⭐ EVERY SHEET MODEL HAS A COMPOSER — a drawn document's is simply EMPTY (Max, 2026-08-23:
	// "just let the spreadsheet document have an empty composer, and that is all").
	//
	// That one sentence removes the whole reason this base used to refuse to know the query engine:
	// the two kinds stopped differing in WHAT THEY HAVE and differ only in what is in it. A report's
	// composer reads; a drawn document's never runs, because nothing was ever put in it. Nobody has
	// to ask which kind they are holding, and nothing has to answer for a thing it is not.
	ibDataDBComposer&       GetModelComposer()       { return m_composer; }
	const ibDataDBComposer& GetModelComposer() const { return m_composer; }

	// ⭐ THE MODEL IS A COMMAND STORE, exactly as a table's model is (ibValueModelTableBox merges
	// `model->GetCommandCollection` into its own band). It only LISTS what can be done — the control
	// lays the list out into real actions and decorates it with its own verbs, so what a source
	// offers is stated once and drawn wherever it is shown.
	//
	// A composer forwards its SOURCE's commands; a plain sheet offers none.
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const {}
	
	// …and runs one of them. The id is the store's own — the control passes back what it was given.
	virtual void CallAsModelCommand(const ibActionID& id, class ibBackendValueForm* srcForm) {}

	// ⭐⭐ THE FETCH IS AN EVENT HERE, NOTHING MORE (Max, 2026-08-20: "make the async fetch just an
	// event on the base model and let the composer override it itself").
	//
	// The base has no thread and no run to rent, because holding a sheet says nothing about where its
	// content comes from: a hand-filled document produces its data by ALREADY BEING it, so "do this
	// work" means doing it, here, now. Whoever actually READS — the composer — overrides this and
	// rents a background run, and that is the only place a job manager is named.
	//
	// Callers never learn which way it went: work handed over is work done.
	virtual void SubmitFetchAsync(std::function<void()> work) { if (work) work(); }

	// Tell the read in flight to stop, and wait it out — a window closing on a report nobody is
	// waiting for must not leave a connection held. Nothing to stop when nothing reads.
	virtual void CancelFetch() {}

protected:

	// The sheet itself. Held here because holding it IS what makes something a model of a spreadsheet;
	// a composer replaces its content on every run, a hand-filled document simply keeps it.
	wxObjectDataPtr<ibBackendSpreadsheetObject> m_spreadsheetDoc;

	// …and the composer that fills it. EMPTY on a drawn document — no source, no query, nothing set
	// — which is the honest description of a sheet somebody typed by hand.
	ibDataDBComposer m_composer;
};

// ⚠ NO WRAPPER CLASS. Both things that show a sheet derive this base DIRECTLY — the composer and the
// spreadsheet document (Max, 2026-08-20: "no intermediate layers"). A wrapper would be a third object
// with a lifetime of its own between the control and the value it is pointed at, and every question
// would travel one hop further for nothing.

#endif // !__SPREADSHEET_MODEL_H__
