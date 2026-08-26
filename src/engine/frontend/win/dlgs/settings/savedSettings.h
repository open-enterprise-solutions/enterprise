#ifndef _SAVED_SETTINGS_H__
#define _SAVED_SETTINGS_H__

// ---------------------------------------------------------------------------
// ibDialogSavedSettings — WHAT THIS PERSON KEPT, and what may be done to it.
//
// ⭐⭐ ONE WINDOW FOR A LIST AND FOR A REPORT. It takes a COMPOSER and an ADDRESS,
// never a model: a report's settings live under the composer the configuration
// declares, a list's under the control on the form, and the CATEGORY keeps the
// two vocabularies apart (Max, 2026-08-26). Everything else — the shelf, the
// verbs, the restore — is one piece of code, sitting at the root of
// win/dlgs/settings/ beside the other editors both worlds share.
//
// WHAT IT SHOWS: the entries, newest first, with the DEFAULT one in bold — the
// one that comes back by itself when the object is opened. Picking an entry
// restores it and closes; the buttons act on whatever is selected.
//
// ⚠ THE DEFAULT IS A TOGGLE. Pressing "Restore on open" while the selected entry
// is already the default CLEARS the mark — the same gesture undoes itself, so
// there is no second button that exists only to say "no".
// ---------------------------------------------------------------------------

#include "frontend/frontend.h"

#include "backend/settings/settingsComposer.h"   // the shelf itself — entries, the default pointer

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/button.h>

class ibDataComposer;
class ibMetaData;

class FRONTEND_API ibDialogSavedSettings : public wxDialog {
public:

	// WHICH ACT THE WINDOW IS FOR. The shelf is the same one either way — the entries, the bold
	// default, rename and delete — and only the MAIN button differs, because only the act does.
	enum class Mode {
		Save,      // …where to put what is in force. Offers a place that is not on the shelf yet.
		Restore,   // …which one to put on.
	};

	// THE DOOR. True when something changed — a setting was saved, restored, renamed, dropped, or
	// the default mark moved. The caller decides what that is worth: a report re-forms when the
	// person says Compose, a list refetches at once.
	static bool Show(wxWindow* parent, ibDataComposer& composer, Mode mode, ibSettingsCategory category,
	                 const ibGuid& objectKey, const ibMetaData* metaData);

	// ⭐ AND THE ACT NOBODY PRESSES: put on the setting this person marked "restore on open". Called
	// by a CONTROL the moment it is handed its model (Max, 2026-08-26: *"it fires when you assign
	// the model, or on the created event — it happens once anyway"*) — which is also the only place
	// that knows a list's address, and the only moment that happens once per opened window.
	//
	// Silent when nothing is marked. SAYS SO when a mark points at a setting that is not there.
	static void ApplyDefault(ibDataComposer& composer, ibSettingsCategory category,
	                         const ibGuid& objectKey, const ibMetaData* metaData);

private:
	ibDialogSavedSettings(wxWindow* parent, ibDataComposer& composer, Mode mode, ibSettingsCategory category,
	                      const ibGuid& objectKey, const ibMetaData* metaData);

	void Reload();                       // re-read the shelf and the default mark
	int  Selected() const;               // the selected row, wxNOT_FOUND when none
	void UpdateButtons();                // what may be pressed depends on what is selected

	void OnActivated(wxListEvent&);      // double click / Enter = the window's own act, then close
	void OnSelectionChanged(wxListEvent&);
	void OnMainAct(wxCommandEvent&);     // save into / restore from the selected entry
	void OnNew(wxCommandEvent&);         // save mode only — a place that is not on the shelf yet
	void OnDefault(wxCommandEvent&);     // toggle "restore on open" for the selected entry
	void OnRename(wxCommandEvent&);
	void OnRemove(wxCommandEvent&);

	bool ActOnSelected();                // the act this window is FOR, on the selected entry
	bool SaveInto(const ibGuid& id, const wxString& name);

	ibDataComposer&    m_composer;
	Mode               m_mode;
	ibSettingsCategory m_category;
	ibGuid             m_objectKey;
	const ibMetaData*  m_metaData;

	std::vector<ibComposerSettingsEntry> m_entries;
	ibGuid                               m_defaultId;   // invalid = nothing marked
	bool                                 m_changed = false;

	wxListCtrl* m_list       = nullptr;
	wxButton*   m_btnMain    = nullptr;   // "Save" / "Restore" — the act the window is for
	wxButton*   m_btnNew     = nullptr;   // save mode only
	wxButton*   m_btnDefault = nullptr;
	wxButton*   m_btnRename  = nullptr;
	wxButton*   m_btnRemove  = nullptr;
};

#endif // !_SAVED_SETTINGS_H__
