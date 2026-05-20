/////////////////////////////////////////////////////////////////////////////
// ibPluginManagerDialog — Tools → Plugins UI.
//
// Lists every loaded plugin from ibPluginManager + every entry recorded in
// plugins.json5 (so disabled plugins still show up — user can re-enable
// without re-installing). For each row the dialog renders:
//
//   [✓] <name> <version>                                   "endpoint…"
//   [ Edit BYOK token ]   [ Open .env file ]
//
// On OK:
//   - Builds a pluginsConfig::Snapshot reflecting the current toggles.
//   - Calls pluginsConfig::Save to persist atomically.
//   - Shows a "Restart Designer to apply" hint if any enabled flag flipped
//     (plugin DLLs are loaded once at appData boot — toggling alone does
//     not load/unload a live DLL in Phase 4.3).
//
// BYOK token edit opens a small modal that reads existing
// keys via byokEnv::LoadAll, lets the user edit one named entry (default
// "TOKEN"), and writes back with the proper mode-0600 permissions.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_MANAGER_DIALOG_H_
#define _IB_PLUGIN_MANAGER_DIALOG_H_

#include "frontend/mainFrame/mainFrame.h"  // FRONTEND_API

#include <wx/dialog.h>

class wxListView;
class wxCheckBox;
class wxTextCtrl;
class wxStaticText;

class FRONTEND_API ibPluginManagerDialog : public wxDialog {
public:
	ibPluginManagerDialog(wxWindow* parent);
	~ibPluginManagerDialog() override = default;

private:
	void OnApply(wxCommandEvent& event);
	void OnEditByok(wxCommandEvent& event);
	void OnSelectionChanged(wxCommandEvent& event);
	void OnInstall(wxCommandEvent& event);
	void OnUninstall(wxCommandEvent& event);

	// Reload the listbox + right-pane fields from the current snapshot.
	void RebuildList();

	// Apply the in-memory edits back into the snapshot for the active row.
	void CaptureCurrentRow();

	wxListView*    m_list           = nullptr;
	wxCheckBox*    m_enabledCheck   = nullptr;
	wxTextCtrl*    m_endpointEdit   = nullptr;
	wxTextCtrl*    m_byokRefEdit    = nullptr;
	wxStaticText*  m_descLabel      = nullptr;
	wxStaticText*  m_versionLabel   = nullptr;
	wxStaticText*  m_authorLabel    = nullptr;
	wxStaticText*  m_descriptionLbl = nullptr;
	int            m_activeRow      = -1;

	// In-memory snapshot — initialised from pluginsConfig::Load() at
	// open, mutated by the UI, persisted via pluginsConfig::Save on OK.
	struct Snap {
		struct Row {
			wxString  pluginId;
			wxString  displayName;
			wxString  version;
			wxString  description;
			wxString  vendor;
			bool      enabled  = true;
			bool      loaded   = false;     // currently live in pm.Loaded()?
			wxString  endpoint;
			wxString  byokRef;
		};
		std::vector<Row> rows;
	};
	Snap m_snap;
};

#endif // _IB_PLUGIN_MANAGER_DIALOG_H_
