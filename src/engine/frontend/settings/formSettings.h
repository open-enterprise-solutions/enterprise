#ifndef __FORM_SETTINGS_H__
#define __FORM_SETTINGS_H__

/////////////////////////////////////////////////////////////////////////////
// HOW A PERSON ARRANGED A FORM FOR THEMSELVES — written to the settings storage and read back.
//
// ⭐⭐ THIS IS A DOOR, NOT A MECHANISM. Everything the user-side form editor does has worked for a
// while — the tree, the whitelist, Apply — and none of it outlived the open window, because there
// was nowhere to put it (Max, 2026-08-30: *"everything works except one mechanism — saving and
// restoring the setting, because we had no value storage"*). There is one now, so all that is
// needed is to write the form's state into it and read it back.
//
// ⭐ THE SAME CHANNEL A REPORT'S AND A LIST'S SETTINGS USE. A form is another tenant of
// `ibSettingsStorage` — category `Form`, the form metaobject's guid as the object, the current user
// — exactly as `settingsComposer.cpp` is for a composer. ⚠ ONE ARRANGEMENT PER FORM PER PERSON, so
// clearing the key clears all of it.
//
// ⭐ AND NOTHING IS CACHED. Restore reads the row, lays it on and lets go, so the SAME PERSON's
// other session is visible the next time the form is opened.
//
// ⚠ NOT IN THE CONTROLS FOLDER AND NOT AMONG THE DIALOGS (Max: *"settings must not live in the folder with the controls"*)
// and NOT in `form.h`: free functions cost one translation unit to change, methods on ibValueForm
// cost a full rebuild of both DLLs.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"

class ibValueForm;

// RESTORE — read what this person saved for this form and lay it over the controls that have
// already loaded, by id, one after another. Called from ibValueForm::InitializeFormModule: after
// the control tree exists and BEFORE the module runs, so the author's form is the base, the
// person's arrangement goes over it, and the module — which may hide a control because of a right
// or a value — has the last word.
//
// ⭐⭐ SOFT DEGRADATION LIVES HERE (Max: *"with soft degradation, because it can change in the
// Designer"*): a control id that is gone, a property renamed, a property taken off the list a user
// may touch, a value the property no longer accepts — each is one entry skipped, and the rest still
// goes on. 🛑 And nothing raises: `get_cell_variant` throws when a stored value is not of the kind
// the property expects, and a raise inside the form's initialisation is a form that does not open.
FRONTEND_API void ibRestoreFormSettings(ibValueForm* form);

// ⭐⭐ WHY, NOT JUST WHETHER. Three quite different things make a save impossible and they were all
// coming back as one `false`, so the window could only say "the form setting could not be saved" —
// which is a report of a failure where two of the three are not failures at all.
//
//   NoStorage — there is no settings storage in this run at all (pre- / post-appData).
//   NoAddress — this form has nothing to be saved UNDER: a form generated from a source has no
//               metaobject, so there is no guid to key it by. A permanent, ordinary state of that
//               form, and the person deserves to be told that rather than to try again.
//   Refused   — the address is good and the write did not land; the journal has the reason.
enum class ibFormSettingsResult {
	Ok,
	NoStorage,
	NoAddress,
	Refused,
};

// SAVE — write down how the form stands now: for every control, the properties a person is allowed
// to touch, and where it sits among its siblings.
FRONTEND_API ibFormSettingsResult ibSaveFormSettings(const ibValueForm* form);

// RESET — drop the row, so the form comes back the way the engine laid it out. The default is not a
// saved state of its own, it is the ABSENCE of one; nothing here has to know what the author's form
// looked like.
FRONTEND_API ibFormSettingsResult ibResetFormSettings(const ibValueForm* form);

// Is there anything saved for this form and this person? What the dialog asks to decide whether
// «restore the default» has anything to undo.
FRONTEND_API bool ibHasFormSettings(const ibValueForm* form);

#endif // !__FORM_SETTINGS_H__
