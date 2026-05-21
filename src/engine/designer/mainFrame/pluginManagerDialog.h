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
#include "backend/plugin/pluginsConfig.h"

#include <wx/dialog.h>

class wxListView;
class wxCheckBox;
class wxTextCtrl;
class wxStaticText;
class wxChoice;

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
	void OnTestConnection(wxCommandEvent& event);

	// Diagnostics — Workmate parity. Runs a series of checks against
	// the currently selected plugin and streams pass/fail markers into
	// m_diagOutput. RunDiagnostics is the entry point; the individual
	// Diag* helpers each append one line and return true/false.
	void OnRunDiagnostics(wxCommandEvent& event);
	void OnCopyDiagReport(wxCommandEvent& event);
	void RunDiagnostics();
	void DiagLine(bool pass, const wxString& message);
	void DiagInfo(const wxString& message);
	void DiagClear();

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
	// Workmate-parity inline-completion operating mode. Persisted into
	// the active plugin's .env file under OES_AI_AUTOCOMPLETE_MODE; the
	// codeEditor reads the value as a process env var at construction.
	wxChoice*      m_autocompleteMode = nullptr;
	wxTextCtrl*    m_slashCommandsEdit = nullptr;
	int            m_activeRow      = -1;

	// Diagnostics widgets — wxTextCtrl in read-only multi-line mode is
	// the cheapest streaming console. We append text + scroll-to-end
	// after each Diag* call so the user sees progress as checks run.
	wxTextCtrl*    m_diagOutput     = nullptr;

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
			// 0..3, -1 = "inherit / unset". Mirrors codeEditor's
			// m_sigmaAutoMode enum: 0 off, 1 manual-only, 2 auto-moderate,
			// 3 auto-intensive.
			int       autocompleteMode = -1;
		};
		std::vector<Row> rows;
	};
	Snap m_snap;
	std::vector<pluginsConfig::SlashCommandEntry> m_slashCommands;
};

#endif // _IB_PLUGIN_MANAGER_DIALOG_H_
