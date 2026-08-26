////////////////////////////////////////////////////////////////////////////
//	Gridbox — ITS VERBS: compose the report, open its settings, print
////////////////////////////////////////////////////////////////////////////

#include "gridBox.h"
#include "backend/picturePredefined.h"                   // the standard pictures for this control's two verbs
#include "backend/system/value/valueDataComposition.h"   // the composition the Generate verb runs
#include "backend/spreadsheetModel.h"   // the model this control is moving onto
#include "frontend/win/dlgs/settings/composer/composerSettings.h"  // the Settings verb opens the composition's own window
#include "frontend/win/editor/gridEditor/gridPrintout.h"

// (No ids of its own any more: the verbs are the MODEL's and their ids are named there —
//  ibSpreadsheetModelCommand in spreadsheetModel.h. This control lays them out and hands them back.)

ibValueGridBox::ibStandardCommandSet ibValueGridBox::GetStandardCommands(const ibFormID& formType)
{
	// The commands are the MODEL's — read straight off the field.
	ibValueSpreadsheetModel* model = m_spreadsheetModel;
	if (model == nullptr)
		return ibStandardCommandSet();

	// ⭐⭐ THE BOX DECLARES NOTHING OF ITS OWN. What can be done is a fact about the MODEL — a
	// composer offers Compose and Settings, a spreadsheet document offers nothing at all — and this
	// control only lays the store out into real actions, carrying each one's modify flag. The same
	// shape a tablebox uses, and the reason the same verbs appear wherever a composition is shown.
	ibStandardCommandSet actionData(this);

	std::vector<ibCommandItem> commands;
	model->GetCommandCollection(formType, commands);
	for (const ibCommandItem& c : commands) {
		if (c.m_actionId == wxNOT_FOUND)
			actionData.AddSeparator();
		else
			actionData.AddAction(c.m_name, c.m_caption, c.m_pictureDescription, c.m_pictureAndText, c.m_actionId)
				.SetModify(c.m_modifiesData);
	}

	return actionData;
}

void ibValueGridBox::CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	// ⭐ EVERYTHING GOES THROUGH THE MODEL. Whatever stands behind the source — a composer that reads
	// and lays out a result, or a sheet that simply copies itself — answers the same call (Max,
	// 2026-08-20: "everything that acts as a source copies itself into what it is handed").
	ibValueSpreadsheetModel* model = m_spreadsheetModel;
	if (model == nullptr)
		return;

	switch (lNumAction)
	{
	case ibSpreadsheetModelCommand_Compose:
	{
		// ⭐⭐ THE MODEL FILLS ITS OWN SHEET. This control neither builds a document nor installs one:
		// it asks for data and redraws what came back. Whoever is behind the source — a composer that
		// reads, a sheet that already is its own result — answers the same call.
		//
		// COMPOSED IN THE BACKGROUND, the way a list reads its pages: a report can take seconds, and a
		// window frozen for those seconds cannot be moved, scrolled or cancelled. The fetch belongs to
		// the model, so this control never learns how — or whether — a run was rented.
		// (THE SETTING IS ALREADY IN THE COMPOSER — put there when the model arrived, and replaced
		//  when the reader accepted the settings window. Supplying it again here would be a second
		//  place deciding what is in force.)
		ibGridEditor* gridWindow = dynamic_cast<ibGridEditor*>(GetInnerWx());
		if (gridWindow != nullptr)
			gridWindow->ShowComposeProgress(true);

		ibValuePtr<ibValueSpreadsheetModel> keepModel(model);   // the run outlives this call
		// 🛑 THE CONTROL IS NOT KEPT ALIVE BY THE RUN, and must not be: a form closes when its reader
		// closes it, whatever a report is doing. So the delivery hop carries a TOKEN and asks whether
		// this control is still there before touching it — the same one the paged dataview carries,
		// for the same reason (a wxWeakRef writes into the control as it dies, racing the worker's copy).
		std::shared_ptr<bool> alive = m_aliveToken;
		ibValueGridBox* self = this;
		model->SubmitFetchAsync([self, alive, keepModel]() {
			wxString failure;
			try {
				// THE MODEL FILLS ITS OWN SHEET — this control asked for data, not for a document.
				keepModel->Compose();
			}
			catch (const ibBackendException& error) {
				failure = error.GetErrorDescription();
			}

			// BACK ON THE UI THREAD to show it — the redraw is GUI, and so is a refusal.
			wxTheApp->CallAfter([self, alive, keepModel, failure]() {
				if (!*alive)
					return;   // the form closed while the report was being built
				ibGridEditor* target = dynamic_cast<ibGridEditor*>(self->GetInnerWx());
				if (target != nullptr)
					target->ShowComposeProgress(false);

				if (!failure.IsEmpty()) {
					// 🛑 SAID WHERE IT IS SEEN. A refusal routed to the log ends up in a panel that
					// may not be open, and a report that simply never appears reads as "the button
					// does nothing" (Max, 2026-08-20). The description is DATA, never a format
					// string (docs/exceptions.md).
					wxMessageBox(failure, _("Compose"), wxOK | wxICON_ERROR);
					return;
				}

				// The composer swapped the sheet it holds, so the window is re-pointed at it once.
				if (target != nullptr)
					target->LoadDocument(keepModel->GetSpreadsheetDocument());
			});
		});
		break;
	}
	case ibSpreadsheetModelCommand_Settings:
		// THE COMPOSER'S OWN WINDOW — the one the designer opens, opened here by the person reading
		// the report. The composer LIVES IN THE MODEL: when the model is one, it IS the settings.
		// Through the ONE place that knows a composer is what has settings — no second cast here.
		//
		// ⭐ THROUGH THE MODEL'S OWN PAIR — the same road the list's window takes: the setting in
		// force goes in, and what they chose is assigned back. Nothing is cast down to a composition
		// to change a filter, and the report itself is not written.
		//
		// The sheet on screen stays the one that was BUILT: a report is not a list, and it is
		// re-formed when the person says so — that is when the setting is taken into account.
		// ⭐ A COPY OF THE ACTIVE SETTING GOES IN, AND ON OK IT IS SET BACK ON THE MODEL. Nothing is
		// kept on this side — the active setting lives in the model's composer, which the schema does
		// not serialise; the author's default is untouched (Max, 2026-08-24).
		ibDialogComposerSettings::ShowUserSettings(dynamic_cast<wxWindow*>(GetInnerWx()), model);
		break;
	case ibSpreadsheetModelCommand_Variants:
		// ⭐ THE SAME ACT AS PRESSING OK IN THAT WINDOW, said in one gesture: the variant's setting
		// becomes the reader's. The sheet on screen stays the one that was BUILT — a report is not a
		// list, and it is re-formed when the person says so (Compose), which is when the setting is
		// taken into account.
		ibDialogComposerSettings::ShowVariantPicker(dynamic_cast<wxWindow*>(GetInnerWx()), model);
		break;
	default:
		// ⭐ ANYTHING ELSE IS THE MODEL'S OWN COMMAND — the id came out of its store, so it goes
		// straight back there. Same rule the tablebox follows: this control's ids are its own (high
		// base), everything unknown belongs to whatever is bound.
		model->CallAsModelCommand(lNumAction, srcForm);
		break;
	}
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************
