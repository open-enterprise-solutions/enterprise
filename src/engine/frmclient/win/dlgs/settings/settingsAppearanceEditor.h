#ifndef __SETTINGS_APPEARANCE_EDITOR_H__
#define __SETTINGS_APPEARANCE_EDITOR_H__

// ---------------------------------------------------------------------------
// THE APPEARANCE WINDOW — how a value is shown, as a list of the PLATFORM's parameters.
//
// An appearance is a description of its own (ibAppearanceDescription) whose parameters are a finite
// list the platform defines (ibAppearanceParameters): nobody adds one, and the window lists them all, one
// line each, ticked or not, with its value — each set the way any setting is (Max, 2026-09-29). Format is
// the whole list today.
//
// Here at the root beside the filter and sort editors, not inside the composer's window: a field of a
// report points at an appearance now, and a conditional-appearance rule — in a report and in a list — is
// expected to carry the same one.
// ---------------------------------------------------------------------------

#include <wx/string.h>
#include <wx/window.h>

#include "frmclient/backend/compositionDescription.h"   // ibAppearanceDescription — what is edited here

class ibMetaData;

// Edits `appearance` in place on OK. False — cancelled, or nothing changed. `metaData` names the languages a
// text parameter is written in.
bool ibEditAppearance(wxWindow* parent, ibAppearanceDescription& appearance, const ibMetaData* metaData,
	bool readOnly);

// WHAT A CELL SHOWS FOR AN APPEARANCE — its ticked parameters, `Format: NFD=2`, in the language in force, joined
// by commas.
// Empty when nothing is set.
wxString ibAppearanceSummary(const ibAppearanceDescription& appearance);

#endif
