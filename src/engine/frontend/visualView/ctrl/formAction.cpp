////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame action
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "backend/appData.h"
#include "backend/srcDataObject.h"        // ibSourceDataObject COMPLETE — the dynamic_cast<ibStandardCommandSource*>(GetSourceObject())
#include "backend/picturePredefined.h"    // g_pic*FormCLSID — the form-level chrome commands

enum
{
	enClose = 10000,
	enUpdate,
	enHelp,

	enChange,
};

//****************************************************************************
//*                              actionData                                     *
//****************************************************************************

// Find WHERE the form's MAIN attribute currently lives — the control that reports IsMainSourceBound (its WHOLE
// binding IS the main attribute). Asks the BASE virtual ibValueFrame::IsMainSourceBound (false by default; a view
// bound single-hop to the main attribute overrides it) — no per-type cast. "Search by source": move the main
// attribute from table-1 to table-2 and the very next walk finds table-2, because IsMainSourceBound re-reads the
// holder's Main flag live. Never cached — nothing to go stale in the designer. null on an object form.
static ibValueFrame* FindMainCommandView(ibValueFrame* top)
{
	if (top->IsMainSourceBound())
		return top;
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++)
		if (ibValueFrame* found = FindMainCommandView(top->GetChild(idx)))
			return found;
	return nullptr;
}

// The form's command provider — ALWAYS an ibStandardCommandSource: the main data VIEW (a control that IS-A
// ibStandardCommandSource via ibValueFrame, adapting its dumb ibStandardCommandTabular model — view-state band + current
// row + dispatch), or — an object form — the self-commanding source object itself (both ibSourceDataObject and
// ibStandardCommandSource). The form NEVER touches the dumb ibStandardCommandTabular directly: composing/dispatching
// table commands is the view's own job.
ibStandardCommandSource* ibValueForm::GetCommandProvider()
{
	if (ibValueFrame* mainView = FindMainCommandView(this))
		return mainView;   // ibValueFrame IS-A ibStandardCommandSource
	return dynamic_cast<ibStandardCommandSource*>(ibValueForm::GetSourceObject());
}

ibValueForm::ibStandardCommandSet ibValueForm::GetStandardCommands(const ibFormID& formType)
{
	ibStandardCommandSet actionData(this);

	// The form is a WRAPPER: it surfaces its command provider's set, then adds the form-level chrome. The
	// provider distributes internally (a tablebox: its own view-state band vs its model's object commands).
	if (ibStandardCommandSource* provider = GetCommandProvider())
		actionData.AppendFrom(provider->GetStandardCommands(formType));

	// ⭐ A RULE OFF THE SUBJECT'S VERBS. What the provider gave is about the DATA (compose it, post
	// it, add a row); what follows is about the WINDOW. Run together they read as one list of
	// equals, and the eye has to know the vocabulary to tell them apart (Max, 2026-08-20: "and
	// there is no separator").
	if (actionData.GetCount() > 0)
		actionData.AddSeparator();

	// Form chrome is navigation / view — never data-modifying — so it stays live in a view-only form.
	actionData.AddAction(wxT("Close"), _("Close"), g_picCloseFormCLSID, false, enClose).SetModify(false);
	actionData.AddAction(wxT("Update"), _("Update"), g_picUpdateFormCLSID, false, enUpdate).SetModify(false);
	actionData.AddAction(wxT("Help"), _("Help"), g_picHelpFormCLSID, false, enHelp).SetModify(false);

	actionData.AddSeparator();
	actionData.AddAction(wxT("Change"), _("Change form"), g_picChangeFormCLSID, false, enChange).SetModify(false);

	return actionData;
}

void ibValueForm::CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	if (appData->DesignerMode()) {
		return;
	}

	switch (lNumAction)
	{
	case enClose:
		CloseForm();
		break;
	case enUpdate:
		UpdateForm();
		break;
	case enHelp:
		HelpForm();
		break;
	case enChange:
		ChangeForm();
		break;
	default:
		// Not a form-level id — hand it to the command provider. A tablebox routes it internally (a view-state
		// Command_* or down to its model's CallAsCommand with the front-owned row); an object source runs its
		// own object command.
		if (ibStandardCommandSource* provider = GetCommandProvider())
			provider->CallAsAction(lNumAction, srcForm);
		break;
	}
}
