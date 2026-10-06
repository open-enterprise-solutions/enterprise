////////////////////////////////////////////////////////////////////////////
//	Gridbox — ITS VERBS: compose the report, and arrange the settings it composes by
////////////////////////////////////////////////////////////////////////////

#include "gridBox.h"
#include "backend/picturePredefined.h"                   // the standard pictures for this control's two verbs
#include "backend/system/value/valueDataComposition.h"   // the composition the Generate verb runs
#include "backend/spreadsheetModel.h"   // the model this control is moving onto
#include "backend/settings/settingsComposer.h"            // ibSettingsCategory — which shelf a report's settings sit on
#include "backend/backend_exception.h"                    // ibBackendInterruptException — a read stopped, not failed
#include "backend/backend_mainFrame.h"                    // ibBackendDocFrame::ShowModalMessage — a refusal, said to the person
#include "backend/session/session.h"                      // ibSession — whose queue a compose is delivered on
#include "backend/diagnostics/journal.h"                  // ibJournalWarning — a delivery nobody waits for says its own failure
#include "sfrontend/visualView/choiceRequest.h"           // ibRequestChoice / ibChooseSavedSettings — a variant, a saved setting: "pick one of these"

// (No ids of its own any more: the verbs are the MODEL's and their ids are named there —
//  ibSpreadsheetModelCommand in spreadsheetModel.h. This control lays them out and hands them back.)

namespace {

// ⭐⭐ THE VARIANT PICKER — what the author named, put to the person as a choice, and picking one IS setting
// a setting (Max, 2026-08-26: "you press it on a list or a report, the variants drop down, and you just pick
// — it is the same as if you had set a user setting"). No mechanism of its own: a variant is a WRAPPER over a
// setting, so the act is `SetUserSettingsDesc(variants[n].m_settings)` and nothing else — there is no stored
// "active variant" to keep in step.
void ibChooseVariant(ibDataComposer& composer)
{
	const std::vector<ibVariantDescription>& variants = composer.GetVariants();
	const ibSettingsDescription inForce = composer.GetCurrentSettingsDesc();

	std::vector<ibChoiceItem> items;
	for (size_t i = 0; i < variants.size(); ++i) {
		ibChoiceItem item;
		item.id = static_cast<s32>(i);
		// WHAT THE PICKER SHOWS — the synonym in the configuration's language, else the name, else its place
		// in the list: not a caption anybody wrote, but what an unnamed variant HAS, and a blank entry cannot
		// be picked with any confidence.
		item.caption = variants[i].GetPresentation();
		if (item.caption.IsEmpty())
			item.caption = wxString::Format(_("Variant %u"), static_cast<unsigned>(i + 1));
		// ⭐ AND THE ONE IN FORCE IS MARKED — by COMPARING the settings against what COMPOSES, because there is
		// no stored "active variant" to read. Asked of the reader's section instead, nothing would be marked
		// until they had picked something, while variant zero plainly composes.
		item.selected = inForce == variants[i].m_settings;
		items.push_back(item);
	}

	s32 chosen = 0;
	if (!ibRequestChoice(wxString(), items, chosen))
		return;   // closed without choosing — nothing changes

	// …AND THAT IS THE WHOLE ACT. The same call accepting the settings makes.
	composer.SetUserSettingsDesc(variants[static_cast<size_t>(chosen)].m_settings);
}

}   // namespace

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
		// it asks for data and shows what came back. Whoever is behind the source — a composer that
		// reads, a sheet that already is its own result — answers the same call.
		//
		// COMPOSED IN THE BACKGROUND, the way a list reads its pages: a report can take seconds, and a
		// session frozen for those seconds cannot answer its client, scroll or cancel. The fetch belongs
		// to the model, so this control never learns how — or whether — a run was rented.
		// (THE SETTING IS ALREADY IN THE COMPOSER — put there when the model arrived, and replaced
		//  when the reader accepted their settings. Supplying it again here would be a second place
		//  deciding what is in force.)
		++m_composeRuns;   // the progress the client shows (Composing), until this run is delivered

		ibValuePtr<ibValueSpreadsheetModel> keepModel(model);   // the run outlives this call
		// 🛑 THE CONTROL IS NOT KEPT ALIVE BY THE RUN, and must not be: a form closes when its reader
		// closes it, whatever a report is doing. So the delivery hop carries a TOKEN and asks whether
		// this control is still there before touching it — the same one the paged dataview carries,
		// for the same reason (a weak reference writes into the control as it dies, racing the worker's copy).
		std::shared_ptr<bool> alive = m_aliveToken;
		ibValueGridBox* self = this;
		// …and WHOSE QUEUE it comes back on: the session the press came in on, which this box and the
		// person it reports to both belong to.
		ibSession* const session = ibSession::Current();
		model->SubmitFetchAsync([self, alive, keepModel, session]() {
			wxString failure;
			bool cancelled = false;
			try {
				// THE MODEL FILLS ITS OWN SHEET — this control asked for data, not for a document.
				keepModel->Compose();
			}
			catch (const ibBackendInterruptException&) {
				// ⭐ STOPPED, NOT FAILED — the form was closed, or Compose pressed again, while this run read.
				// Nothing to report: whoever cancelled it is the one who asked.
				cancelled = true;
			}
			catch (const ibBackendException& error) {
				failure = error.GetErrorDescription();
			}

			// BACK ON THE SESSION'S OWN THREAD to deliver it — the box is the session's, and so is the
			// person a refusal is said to.
			std::function<void()> deliver = [self, alive, keepModel, failure, cancelled]() {
				if (!*alive)
					return;   // the form closed while the report was being built
				self->m_composeRuns--;

				if (cancelled)
					return;   // the sheet on show stays what it was; the run that replaced this one delivers its own

				if (!failure.IsEmpty()) {
					// 🛑 SAID WHERE IT IS SEEN. A refusal routed to the log ends up in a panel that
					// may not be open, and a report that simply never appears reads as "the button
					// does nothing" (Max, 2026-08-20). The description is DATA, never a format
					// string (docs/private/exceptions.md).
					if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
						frame->ShowModalMessage(failure, _("Compose"), wxOK | wxICON_ERROR);
					return;
				}

				// The composer swapped the sheet it holds, so the box shows it, and its version moves.
				self->m_shownDocument = keepModel->GetSpreadsheetDocument();
				++self->m_documentVersion;
			};
			// Delivered on the session's queue and not waited for — so what the delivery throws is said in the task,
			// nobody holding its future.
			if (session != nullptr)
				session->Submit([deliver = std::move(deliver)]() {
					try {
						deliver();
					}
					catch (const std::exception& err) {
						ibJournalWarning(wxT("gridbox"), wxT("a composed report was not delivered: %s"), wxString::FromUTF8(err.what()));
					}
					catch (...) {
						ibJournalWarning(wxT("gridbox"), wxT("a composed report was not delivered"));
					}
				});
			else
				deliver();   // no session to go back to — delivered where the read ended
		});
		break;
	}
	case ibSpreadsheetModelCommand_Settings:
		// THE COMPOSER'S OWN SETTINGS, arranged by the person reading the report. The composer LIVES IN
		// THE MODEL: when the model is one, it IS the settings — through the model's own pair, the road
		// the list takes: a COPY of the setting in force is edited, and on acceptance it is set back on
		// the model (SetUserSettingsDesc) and the report composed again. Nothing is kept on this side —
		// the active setting lives in the model's composer, which the schema does not serialise; the
		// author's default is untouched (Max, 2026-08-24).
		//
		// The edit itself is an editor of its own, outside the runtime — a service of the protocol, as
		// the person's own arrangement of a form is (ibValueForm::ChangeForm).
		break;
	case ibSpreadsheetModelCommand_Variants:
		// ⭐ THE SAME ACT AS ACCEPTING THE SETTINGS, said in one gesture: the variant's setting becomes
		// the reader's. The sheet on show stays the one that was BUILT — a report is not a list, and it
		// is re-formed when the person says so (Compose), which is when the setting is taken into account.
		//
		// 🛑 THE VARIANTS REACH THE COMPOSER LAZILY, and asking without saying so read an empty list: a
		// report just opened has its variants in the DESCRIPTION and not yet in the composer — the first
		// press showed nothing and the second one worked (Max, 2026-08-26). The model's own verb brings
		// the composer up to date, and the composer is read after it.
		if (ibValueDataComposition* const composition = ResolveComposition())
			composition->RefreshComposerSettings();
		ibChooseVariant(model->GetModelComposer());
		break;
	case ibSpreadsheetModelCommand_RestoreSettings:
		// ⭐ THE READER'S OWN SHELF — which of the settings THEY kept to put on. Ends in the same
		// single call as the variant picker above (SetUserSettingsDesc), so a restored setting and a
		// chosen variant are indistinguishable from here down. The sheet on show stays the one that
		// was BUILT — a report is re-formed when the person says Compose.
		//
		// ⭐ ADDRESSED BY THE BINDING'S LEAF — the composer's metaID for a report. The composer itself
		// comes off the base model, so nothing is cast anywhere.
		ibChooseSavedSettings(model->GetModelComposer(), ibSettingsCategory::Composer,
			SettingsObjectKey(), GetMetaData());
		break;
	case ibSpreadsheetModelCommand_SaveSettings:
		// …and the opposite act: WHERE to put what is in force — an entry of the shelf to save over, or
		// a new one under a name the person types (ibSaveComposerSettings). Not a choice among what is
		// there, so not a Choice: a request of its own kind, outside the runtime as the settings editor is.
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
