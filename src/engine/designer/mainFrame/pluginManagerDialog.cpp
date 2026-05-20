/////////////////////////////////////////////////////////////////////////////
// ibPluginManagerDialog implementation. See header for the contract.
/////////////////////////////////////////////////////////////////////////////

#include "pluginManagerDialog.h"

#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"
#include "backend/plugin/pluginsConfig.h"
#include "backend/plugin/byokEnv.h"
#include "backend/plugin/pluginInstaller.h"

#include <wx/sizer.h>
#include <wx/listctrl.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>

#include <unordered_set>

namespace {
enum {
	ID_LIST = wxID_HIGHEST + 5100,
	ID_ENABLED,
	ID_ENDPOINT,
	ID_BYOK_REF,
	ID_EDIT_BYOK,
	ID_INSTALL,
	ID_UNINSTALL,
};
} // namespace

ibPluginManagerDialog::ibPluginManagerDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, _("Plugins"),
	            wxDefaultPosition, wxSize(720, 480),
	            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	auto* root = new wxBoxSizer(wxHORIZONTAL);

	m_list = new wxListView(this, ID_LIST, wxDefaultPosition,
	                          wxSize(280, -1),
	                          wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_list->InsertColumn(0, _("Plugin"), wxLIST_FORMAT_LEFT, 260);
	root->Add(m_list, 0, wxEXPAND | wxALL, 8);

	auto* right = new wxBoxSizer(wxVERTICAL);

	m_descLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
	wxFont boldFont = m_descLabel->GetFont();
	boldFont.MakeBold();
	m_descLabel->SetFont(boldFont);
	right->Add(m_descLabel, 0, wxBOTTOM, 2);

	m_versionLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
	wxFont smallFont = m_versionLabel->GetFont();
	smallFont.SetPointSize(smallFont.GetPointSize() - 1);
	m_versionLabel->SetFont(smallFont);
	m_versionLabel->SetForegroundColour(wxColour(128, 128, 128));
	right->Add(m_versionLabel, 0, wxBOTTOM, 2);

	m_descriptionLbl = new wxStaticText(this, wxID_ANY, wxEmptyString,
	                                       wxDefaultPosition, wxDefaultSize,
	                                       wxALIGN_LEFT);
	right->Add(m_descriptionLbl, 0, wxBOTTOM | wxEXPAND, 8);

	m_enabledCheck = new wxCheckBox(this, ID_ENABLED, _("Enabled"));
	right->Add(m_enabledCheck, 0, wxBOTTOM, 8);

	right->Add(new wxStaticText(this, wxID_ANY, _("Endpoint")), 0, wxBOTTOM, 2);
	m_endpointEdit = new wxTextCtrl(this, ID_ENDPOINT);
	right->Add(m_endpointEdit, 0, wxEXPAND | wxBOTTOM, 8);

	right->Add(new wxStaticText(this, wxID_ANY, _("BYOK file id")), 0, wxBOTTOM, 2);
	m_byokRefEdit = new wxTextCtrl(this, ID_BYOK_REF);
	right->Add(m_byokRefEdit, 0, wxEXPAND | wxBOTTOM, 8);

	auto* byokBtn = new wxButton(this, ID_EDIT_BYOK, _("Edit API token…"));
	right->Add(byokBtn, 0, wxBOTTOM, 8);

	auto* installRow = new wxBoxSizer(wxHORIZONTAL);
	installRow->Add(new wxButton(this, ID_INSTALL,   _("Install from file…")), 0, wxRIGHT, 4);
	installRow->Add(new wxButton(this, ID_UNINSTALL, _("Uninstall…")),         0);
	right->Add(installRow, 0, wxBOTTOM, 8);

	right->AddStretchSpacer(1);

	auto* dialogButtons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	if (dialogButtons) right->Add(dialogButtons, 0, wxEXPAND | wxTOP, 8);
	root->Add(right, 1, wxEXPAND | wxALL, 8);

	SetSizer(root);

	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnApply,     this, wxID_OK);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnEditByok,  this, ID_EDIT_BYOK);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnInstall,   this, ID_INSTALL);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnUninstall, this, ID_UNINSTALL);
	Bind(wxEVT_LIST_ITEM_SELECTED,
	     [this](wxListEvent& e) {
	         wxCommandEvent fake(wxEVT_LIST_ITEM_SELECTED, e.GetIndex());
	         fake.SetInt(static_cast<int>(e.GetIndex()));
	         OnSelectionChanged(fake);
	     },
	     ID_LIST);

	RebuildList();
	if (!m_snap.rows.empty()) {
		m_list->Select(0);
		m_activeRow = 0;
		wxCommandEvent fake(wxEVT_LIST_ITEM_SELECTED, 0);
		fake.SetInt(0);
		OnSelectionChanged(fake);
	}
}

void ibPluginManagerDialog::RebuildList()
{
	m_snap.rows.clear();
	m_list->DeleteAllItems();

	const auto config = pluginsConfig::Load();

	// Union of loaded plugins + persisted entries: the dialog shows both
	// (a) plugins that are currently live in the manager and (b) entries
	// in plugins.json5 that may belong to a disabled or uninstalled
	// plugin the user can reactivate later.
	std::unordered_set<std::string> seen;

	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	if (pm != nullptr) {
		for (const auto& p : pm->Loaded()) {
			Snap::Row r;
			r.pluginId    = p.m_info && p.m_info->name
			                ? wxString::FromUTF8(p.m_info->name)
			                : wxFileName(p.m_path).GetName();
			r.displayName = r.pluginId;
			if (p.m_info) {
				if (p.m_info->version)     r.version     = wxString::FromUTF8(p.m_info->version);
				if (p.m_info->description) r.description = wxString::FromUTF8(p.m_info->description);
				if (p.m_info->vendor)      r.vendor      = wxString::FromUTF8(p.m_info->vendor);
			}
			r.enabled = true;
			r.loaded  = true;

			const std::string idNarrow(r.pluginId.utf8_str());
			auto it = config.plugins.find(idNarrow);
			if (it != config.plugins.end()) {
				r.endpoint = it->second.endpoint;
				r.byokRef  = it->second.byokRef;
				r.enabled  = it->second.enabled; // honour explicit override
			}
			seen.insert(idNarrow);
			m_snap.rows.push_back(std::move(r));
		}
	}

	// Disabled-but-persisted entries.
	for (const auto& [pluginId, entry] : config.plugins) {
		if (seen.count(pluginId)) continue;
		Snap::Row r;
		r.pluginId    = wxString::FromUTF8(pluginId);
		r.displayName = r.pluginId;
		r.enabled     = entry.enabled;
		r.endpoint    = entry.endpoint;
		r.byokRef     = entry.byokRef;
		r.loaded      = false;
		m_snap.rows.push_back(std::move(r));
	}

	for (std::size_t i = 0; i < m_snap.rows.size(); ++i) {
		const auto& r = m_snap.rows[i];
		wxString label = r.displayName;
		if (!r.version.IsEmpty()) label += wxT("  ") + r.version;
		if (!r.enabled)           label += wxT("  (disabled)");
		else if (!r.loaded)        label += wxT("  (not loaded)");
		m_list->InsertItem(static_cast<long>(i), label);
	}
}

void ibPluginManagerDialog::OnSelectionChanged(wxCommandEvent& event)
{
	CaptureCurrentRow();
	const int idx = event.GetInt();
	if (idx < 0 || idx >= static_cast<int>(m_snap.rows.size())) return;
	m_activeRow = idx;
	const auto& r = m_snap.rows[idx];
	m_descLabel->SetLabel(r.displayName);

	// Version / vendor line: "1.0.0 — Open Enterprise Solutions"
	wxString verLine;
	if (!r.version.IsEmpty()) verLine += r.version;
	if (!r.vendor.IsEmpty()) {
		if (!verLine.IsEmpty()) verLine += wxT(" — ");
		verLine += r.vendor;
	}
	m_versionLabel->SetLabel(verLine);

	// Description multi-line wrap. wxALIGN_LEFT plus Wrap() makes it
	// fit the right pane's natural width without horizontal scroll.
	m_descriptionLbl->SetLabel(r.description);
	m_descriptionLbl->Wrap(360);

	m_enabledCheck->SetValue(r.enabled);
	m_endpointEdit->SetValue(r.endpoint);
	m_byokRefEdit ->SetValue(r.byokRef);
	Layout();
}

void ibPluginManagerDialog::CaptureCurrentRow()
{
	if (m_activeRow < 0 || m_activeRow >= static_cast<int>(m_snap.rows.size())) return;
	auto& r = m_snap.rows[m_activeRow];
	r.enabled  = m_enabledCheck->GetValue();
	r.endpoint = m_endpointEdit->GetValue();
	r.byokRef  = m_byokRefEdit ->GetValue();
}

void ibPluginManagerDialog::OnEditByok(wxCommandEvent& /*event*/)
{
	if (m_activeRow < 0) return;
	const auto& r = m_snap.rows[m_activeRow];
	const std::string idNarrow(r.pluginId.utf8_str());

	auto env = byokEnv::LoadAll();
	std::string existing = byokEnv::Get(env, idNarrow, "TOKEN");

	wxTextEntryDialog dlg(this,
	    wxString::Format(_("API token for %s.\nStored at ~/.config/OES/plugins/%s.env (mode 0600)."),
	                     r.displayName, r.pluginId),
	    _("Edit API token"),
	    wxString::FromUTF8(existing),
	    wxOK | wxCANCEL | wxTE_PASSWORD);
	if (dlg.ShowModal() != wxID_OK) return;

	const wxString val = dlg.GetValue();
	byokEnv::KeyMap keys = env[idNarrow]; // preserve other keys
	if (val.IsEmpty()) {
		keys.erase("TOKEN");
	} else {
		keys["TOKEN"] = std::string(val.utf8_str());
	}
	if (byokEnv::Save(idNarrow, keys) != 0) {
		wxMessageBox(_("Failed to write env file. See log for details."),
		             _("BYOK"), wxICON_ERROR);
	}
}

void ibPluginManagerDialog::OnInstall(wxCommandEvent& /*event*/)
{
	wxFileDialog dlg(this, _("Install plugin"), wxEmptyString, wxEmptyString,
	    _("Plugin bundles (*.zip)|*.zip|All files|*"),
	    wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) return;
	const wxString zipPath = dlg.GetPath();

	pluginInstaller::Manifest m;
	wxString err;
	if (!pluginInstaller::ReadManifest(zipPath, m, &err)) {
		wxMessageBox(wxString::Format(_("Cannot read manifest: %s"), err),
		             _("Install plugin"), wxICON_ERROR);
		return;
	}

	const wxString summary = wxString::Format(
	    _("Install %s %s (ABI %d)?\n\nFrom: %s"),
	    wxString::FromUTF8(m.pluginId), wxString::FromUTF8(m.version),
	    m.abiVersion, zipPath);
	const auto choice = wxMessageBox(summary, _("Confirm install"),
	    wxYES_NO | wxICON_QUESTION);
	if (choice != wxYES) return;

	auto rc = pluginInstaller::Install(zipPath, /*allowOverwrite*/ false, &err);
	if (rc == pluginInstaller::InstallResult::AlreadyExists) {
		const auto over = wxMessageBox(
		    _("Same version already installed. Overwrite?"),
		    _("Install plugin"), wxYES_NO | wxICON_QUESTION);
		if (over != wxYES) return;
		rc = pluginInstaller::Install(zipPath, /*allowOverwrite*/ true, &err);
	}
	if (rc != pluginInstaller::InstallResult::OK) {
		wxMessageBox(wxString::Format(_("Install failed: %s"), err),
		             _("Install plugin"), wxICON_ERROR);
		return;
	}

	wxMessageBox(_("Plugin installed. Restart Designer to load it."),
	             _("Install plugin"), wxICON_INFORMATION);
	RebuildList();
}

void ibPluginManagerDialog::OnUninstall(wxCommandEvent& /*event*/)
{
	if (m_activeRow < 0) return;
	const auto& r = m_snap.rows[m_activeRow];

	const auto choice = wxMessageBox(
	    wxString::Format(_("Uninstall %s?\n\nThis removes the plugin "
	                        "files and your saved API key for it."),
	                      r.displayName),
	    _("Uninstall plugin"), wxYES_NO | wxICON_WARNING);
	if (choice != wxYES) return;

	const std::string idNarrow(r.pluginId.utf8_str());
	if (pluginInstaller::Uninstall(idNarrow) != 0) {
		wxMessageBox(_("Uninstall reported errors. See log for details."),
		             _("Uninstall plugin"), wxICON_ERROR);
	}

	// Drop plugins.json5 entry too — uninstall = remove every trace.
	auto snap = pluginsConfig::Load();
	snap.plugins.erase(idNarrow);
	snap.policies.erase(idNarrow);
	pluginsConfig::Save(snap);

	RebuildList();
}

void ibPluginManagerDialog::OnApply(wxCommandEvent& event)
{
	CaptureCurrentRow();

	// Build a pluginsConfig::Snapshot from the in-memory model. Policies
	// preserved from the loaded config — Phase 4.3 dialog doesn't yet
	// edit policy; that ships with the per-op permission editor.
	pluginsConfig::Snapshot snap = pluginsConfig::Load();
	bool toggleChanged = false;
	for (const auto& r : m_snap.rows) {
		const std::string idNarrow(r.pluginId.utf8_str());
		auto& entry = snap.plugins[idNarrow];
		if (entry.enabled != r.enabled) toggleChanged = true;
		entry.enabled  = r.enabled;
		entry.endpoint = r.endpoint;
		entry.byokRef  = r.byokRef;
	}
	if (pluginsConfig::Save(snap) != 0) {
		wxMessageBox(_("Failed to write plugins.json5 — your changes were not saved."),
		             _("Plugins"), wxICON_ERROR);
		return;
	}

	if (toggleChanged) {
		wxMessageBox(_("Plugin enable/disable changes take effect after restart."),
		             _("Plugins"), wxICON_INFORMATION);
	}
	event.Skip();   // proceed with default OK handling (close dialog)
}
