/////////////////////////////////////////////////////////////////////////////
// ibPluginManagerDialog implementation. See header for the contract.
/////////////////////////////////////////////////////////////////////////////

#include "pluginManagerDialog.h"

#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"
#include "backend/plugin/pluginsConfig.h"
#include "backend/plugin/byokEnv.h"
#include "backend/plugin/pluginInstaller.h"
#include "backend/plugin/pluginApi.h"   // IB_PLUGIN_ABI_VERSION + ibPluginInfo

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/listctrl.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/utils.h>            // wxBusyCursor
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/uri.h>
#include <wx/socket.h>
#include <wx/clipbrd.h>
#include <wx/dynlib.h>

#include "backend/compiler/value.h"  // ibValue for Test connection

#include <chrono>
#include <unordered_set>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {
enum {
	ID_LIST = wxID_HIGHEST + 5100,
	ID_ENABLED,
	ID_ENDPOINT,
	ID_BYOK_REF,
	ID_EDIT_BYOK,
	ID_INSTALL,
	ID_UNINSTALL,
	ID_TEST_CONNECTION,
	ID_AUTOCOMPLETE_MODE,
	ID_RUN_DIAGNOSTICS,
	ID_COPY_DIAG_REPORT,
};

// Persistence key for the inline-completion operating mode. Stored under
// the active plugin's .env file (mode 0600). codeEditor reads it as a
// process env var at construction. Centralised here so both sides
// reference the same literal.
constexpr const char kAutocompleteModeKey[] = "OES_AI_AUTOCOMPLETE_MODE";
} // namespace

ibPluginManagerDialog::ibPluginManagerDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, _("Plugins"),
	            wxDefaultPosition, wxSize(760, 640),
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

	// Workmate-style inline-completion operating mode. Order MUST match
	// the integer encoding used by codeEditor's m_sigmaAutoMode:
	//   index 0 → 0 (off), index 1 → 1 (manual), index 2 → 2 (moderate),
	//   index 3 → 3 (intensive).
	right->Add(new wxStaticText(this, wxID_ANY,
	             _("AI inline completion")), 0, wxBOTTOM, 2);
	m_autocompleteMode = new wxChoice(this, ID_AUTOCOMPLETE_MODE);
	m_autocompleteMode->Append(_("Off"));
	m_autocompleteMode->Append(_("Только по горячей клавише"));
	m_autocompleteMode->Append(_("Авто (умеренно)"));
	m_autocompleteMode->Append(_("Авто (интенсивно)"));
	m_autocompleteMode->SetSelection(2);  // default — auto-moderate
	right->Add(m_autocompleteMode, 0, wxEXPAND | wxBOTTOM, 8);

	// Test-connection button — round-trips a 'ping' prompt through the
	// plugin's chat path so the user can verify token + endpoint are
	// correct without leaving the dialog. Result lands in a modal that
	// shows latency and the first ~512 chars of the response (or the
	// error envelope from the plugin if the call failed).
	auto* testBtn = new wxButton(this, ID_TEST_CONNECTION, _("Test connection"));
	right->Add(testBtn, 0, wxBOTTOM, 8);

	auto* installRow = new wxBoxSizer(wxHORIZONTAL);
	installRow->Add(new wxButton(this, ID_INSTALL,   _("Install from file…")), 0, wxRIGHT, 4);
	installRow->Add(new wxButton(this, ID_UNINSTALL, _("Uninstall…")),         0);
	right->Add(installRow, 0, wxBOTTOM, 8);

	// Diagnostics block — Workmate parity. "If something isn't working,
	// open the Diagnostics section and click Start. The automatic check
	// will identify the issue or generate a report for support."
	auto* diagBox = new wxStaticBox(this, wxID_ANY, _("Диагностика"));
	auto* diagSizer = new wxStaticBoxSizer(diagBox, wxVERTICAL);

	auto* diagBtnRow = new wxBoxSizer(wxHORIZONTAL);
	diagBtnRow->Add(new wxButton(this, ID_RUN_DIAGNOSTICS,
	                             _("Запустить диагностику")), 0, wxRIGHT, 4);
	diagBtnRow->Add(new wxButton(this, ID_COPY_DIAG_REPORT,
	                             _("Скопировать отчёт в буфер")), 0);
	diagSizer->Add(diagBtnRow, 0, wxBOTTOM | wxEXPAND, 4);

	m_diagOutput = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
	                                wxDefaultPosition, wxSize(-1, 160),
	                                wxTE_MULTILINE | wxTE_READONLY |
	                                wxTE_DONTWRAP | wxHSCROLL);
	// Monospace font so PASS/FAIL columns align across lines — easier
	// for the user (or support) to scan the report visually.
	wxFont mono = m_diagOutput->GetFont();
	mono.SetFamily(wxFONTFAMILY_TELETYPE);
	m_diagOutput->SetFont(mono);
	diagSizer->Add(m_diagOutput, 1, wxEXPAND);
	right->Add(diagSizer, 1, wxEXPAND | wxBOTTOM, 8);

	auto* dialogButtons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	if (dialogButtons) right->Add(dialogButtons, 0, wxEXPAND | wxTOP, 8);
	root->Add(right, 1, wxEXPAND | wxALL, 8);

	SetSizer(root);

	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnApply,     this, wxID_OK);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnEditByok,  this, ID_EDIT_BYOK);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnInstall,   this, ID_INSTALL);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnUninstall, this, ID_UNINSTALL);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnTestConnection, this, ID_TEST_CONNECTION);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnRunDiagnostics,
	     this, ID_RUN_DIAGNOSTICS);
	Bind(wxEVT_BUTTON,        &ibPluginManagerDialog::OnCopyDiagReport,
	     this, ID_COPY_DIAG_REPORT);
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
	// Pulled once outside the loop — LoadAll walks the plugins dir and
	// parses every <id>.env file, which is heavier than a per-row check.
	const auto envAll = byokEnv::LoadAll();

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
			// Inline-completion mode: pulled from this plugin's .env file.
			// Missing / unparseable → keep the row's -1 sentinel (the UI
			// renders that as "auto-moderate" default in the choice).
			{
				const std::string raw = byokEnv::Get(envAll, idNarrow, kAutocompleteModeKey);
				if (!raw.empty()) {
					try {
						const int v = std::stoi(raw);
						if (v >= 0 && v <= 3) r.autocompleteMode = v;
					} catch (...) { /* malformed value — ignore */ }
				}
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
		{
			const std::string raw = byokEnv::Get(envAll, pluginId, kAutocompleteModeKey);
			if (!raw.empty()) {
				try {
					const int v = std::stoi(raw);
					if (v >= 0 && v <= 3) r.autocompleteMode = v;
				} catch (...) { /* malformed value — ignore */ }
			}
		}
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
	// -1 sentinel means "unset" — show the default (index 2 = auto-moderate)
	// so the user sees what would actually be in effect.
	m_autocompleteMode->SetSelection(r.autocompleteMode >= 0
	                                  ? r.autocompleteMode
	                                  : 2);
	Layout();
}

void ibPluginManagerDialog::CaptureCurrentRow()
{
	if (m_activeRow < 0 || m_activeRow >= static_cast<int>(m_snap.rows.size())) return;
	auto& r = m_snap.rows[m_activeRow];
	r.enabled  = m_enabledCheck->GetValue();
	r.endpoint = m_endpointEdit->GetValue();
	r.byokRef  = m_byokRefEdit ->GetValue();
	const int sel = m_autocompleteMode->GetSelection();
	r.autocompleteMode = (sel >= 0 && sel <= 3) ? sel : -1;
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

void ibPluginManagerDialog::OnTestConnection(wxCommandEvent& /*event*/)
{
	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	if (pm == nullptr) {
		wxMessageBox(_("Plugin manager not initialised."),
		             _("Test connection"), wxICON_ERROR, this);
		return;
	}

	// Discover the call path: prefer LLMQuery (covers v3 Pugi + the
	// legacy-shim bridge over v3); fall back to a v4 AI provider's
	// Query when one is registered without LLMQuery.
	const ibPluginManager::RegisteredFunction* llmFn = nullptr;
	for (const auto& f : pm->Functions()) {
		if (f.m_name == "LLMQuery") { llmFn = &f; break; }
	}
	const bool haveV4Provider = pm->HasAIProviderFor("chat");

	if (llmFn == nullptr && !haveV4Provider) {
		wxMessageBox(_("No AI provider is registered.\n\n"
		                "Enable a plugin that exposes LLMQuery, or install "
		                "an ABI v4 plugin that registers a chat provider."),
		             _("Test connection"), wxICON_WARNING, this);
		return;
	}

	// Block the UI thread while the call runs — for a one-off ping
	// that's fine. Anvil's typical first-token-to-final is ~2-8s.
	wxBusyCursor busy;
	const auto t0 = std::chrono::steady_clock::now();

	wxString resultText;
	bool ok = false;

	if (llmFn != nullptr) {
		ibValue vPrompt; vPrompt.SetString(wxT("ping"));
		ibValue vLocale; vLocale.SetString(wxT("uk-UA"));
		ibValue* args[2] = { &vPrompt, &vLocale };
		ibValue ret;
		ok = pm->CallFunction(*llmFn, ret, args, 2);
		if (ok) resultText = ret.GetString();
	} else {
		// v4 path — direct provider.Query call. We synthesise a request
		// envelope shaped like a chat.send so the plugin can route it
		// through its normal handler. Phase 4 wire-back: the response
		// arrives over chat.delta/end via the pane; for the dialog we
		// just measure that the Query trampoline returned 0 (acceptance).
		const auto& providers = pm->AIProviders();
		if (!providers.empty() && providers[0].Query != nullptr) {
			const char* req = R"({"prompt":"ping","locale":"uk-UA"})";
			const int rc = providers[0].Query(req, "test-conn", providers[0].userData);
			ok = (rc == 0);
			resultText = ok
			    ? _("Provider accepted the test prompt. Reply will arrive in the chat pane.")
			    : _("Provider returned a non-zero status — check log.");
		}
	}

	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	    std::chrono::steady_clock::now() - t0).count();

	if (!ok) {
		wxMessageBox(
		    wxString::Format(_("Test FAILED after %lld ms.\n\n"
		                        "Likely causes: missing/invalid API token, "
		                        "wrong endpoint URL, or network error.\n\n"
		                        "Open Designer's stderr (run from terminal) "
		                        "to see the plugin's HTTP error."),
		                      static_cast<long long>(ms)),
		    _("Test connection"), wxICON_ERROR, this);
		return;
	}

	// Truncate long responses for readability.
	wxString shown = resultText;
	if (shown.length() > 512) shown = shown.Left(512) + wxT("…");

	wxMessageBox(
	    wxString::Format(_("Test OK in %lld ms.\n\n%s"),
	                      static_cast<long long>(ms),
	                      shown),
	    _("Test connection"), wxICON_INFORMATION, this);
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

	// Persist the inline-completion mode under each plugin's .env file.
	// We re-read existing keys + merge so the AI-token key (and any other
	// values an installer may have written) survive this save. -1 means
	// "leave unset" — the user didn't explicitly choose a mode, so we
	// don't poison their env with a default.
	auto envAll = byokEnv::LoadAll();
	for (const auto& r : m_snap.rows) {
		if (r.autocompleteMode < 0) continue;
		const std::string idNarrow(r.pluginId.utf8_str());
		byokEnv::KeyMap keys = envAll[idNarrow];
		keys[kAutocompleteModeKey] = std::to_string(r.autocompleteMode);
		if (byokEnv::Save(idNarrow, keys) != 0) {
			wxLogMessage(_("Failed to persist AI autocomplete mode for plugin %s"),
			             r.pluginId);
		}
	}

	if (toggleChanged) {
		wxMessageBox(_("Plugin enable/disable changes take effect after restart."),
		             _("Plugins"), wxICON_INFORMATION);
	}
	event.Skip();   // proceed with default OK handling (close dialog)
}

// ===========================================================================
// Diagnostics — Workmate parity
//
// Streams pass/fail markers into m_diagOutput so the user sees progress as
// each check runs. Output uses ASCII PASS/FAIL prefixes + a mono font so
// columns align across lines — easier for the user (or support) to scan
// visually after copying the report. Russian text for the descriptions;
// secrets (tokens, passwords) are NEVER printed — the redacted-placeholder
// rule lives in the env-keys check and in LooksLikeSecretKey().
// ===========================================================================
namespace {

const wxString kPass = wxT("[ OK ] ");
const wxString kFail = wxT("[FAIL] ");
const wxString kInfo = wxT("  ...  ");

// Heuristic: a key whose name suggests a secret. Used by the env-keys
// check so we never print the actual value of credentials in the report.
bool LooksLikeSecretKey(const std::string& key)
{
	std::string lower = key;
	for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return lower.find("token")    != std::string::npos
	    || lower.find("secret")   != std::string::npos
	    || lower.find("password") != std::string::npos
	    || lower.find("apikey")   != std::string::npos
	    || lower.find("api_key")  != std::string::npos
	    || lower.find("key")      != std::string::npos;
}

// Split "https://host:port/path" → (scheme, host, port). port defaults to
// the scheme-implied value (443 for https, 80 for http). Returns false
// when the URL is malformed or the scheme is neither http nor https.
bool ParseEndpoint(const wxString& url,
                   wxString& scheme, wxString& host, int& port)
{
	if (url.IsEmpty()) return false;
	wxURI uri(url);
	if (!uri.HasScheme() || !uri.HasServer()) return false;
	scheme = uri.GetScheme().Lower();
	host = uri.GetServer();
	if (uri.HasPort()) {
		long p = 0;
		if (uri.GetPort().ToLong(&p) && p > 0 && p < 65536) {
			port = static_cast<int>(p);
		} else {
			return false;
		}
	} else {
		port = (scheme == wxT("https")) ? 443 : (scheme == wxT("http") ? 80 : -1);
	}
	if (scheme != wxT("https") && scheme != wxT("http")) return false;
	return !host.IsEmpty() && port > 0;
}

// Plain TCP connect with a hard timeout. Returns true when the 3-way
// handshake completes. SetTimeout on the client governs the blocking
// Connect(); we always Close() to avoid leaving a half-open socket behind.
bool TcpConnectWithTimeout(const wxString& host, int port,
                            int timeoutSec, wxString* outDetail)
{
	wxIPV4address addr;
	if (!addr.Hostname(host)) {
		if (outDetail) *outDetail = wxString::Format(_("DNS не разрешён: %s"), host);
		return false;
	}
	addr.Service(static_cast<wxUint16>(port));

	wxSocketClient sock(wxSOCKET_BLOCK | wxSOCKET_WAITALL);
	sock.SetTimeout(timeoutSec);
	if (!sock.Connect(addr, true)) {
		if (outDetail) {
			*outDetail = wxString::Format(_("connect() не удался (host=%s:%d)"),
			                              host, port);
		}
		return false;
	}
	sock.Close();
	return true;
}

} // namespace

void ibPluginManagerDialog::DiagClear()
{
	if (m_diagOutput != nullptr) m_diagOutput->Clear();
}

void ibPluginManagerDialog::DiagLine(bool pass, const wxString& message)
{
	if (m_diagOutput == nullptr) return;
	m_diagOutput->AppendText((pass ? kPass : kFail) + message + wxT("\n"));
	// Force the user to see progress as we run — without an explicit
	// Update() wx batches AppendText through the event loop, which on
	// macOS holds the whole report back until the run completes.
	m_diagOutput->Update();
}

void ibPluginManagerDialog::DiagInfo(const wxString& message)
{
	if (m_diagOutput == nullptr) return;
	m_diagOutput->AppendText(kInfo + message + wxT("\n"));
	m_diagOutput->Update();
}

void ibPluginManagerDialog::OnRunDiagnostics(wxCommandEvent& /*event*/)
{
	RunDiagnostics();
}

void ibPluginManagerDialog::OnCopyDiagReport(wxCommandEvent& /*event*/)
{
	if (m_diagOutput == nullptr) return;
	const wxString text = m_diagOutput->GetValue();
	if (text.IsEmpty()) {
		wxMessageBox(_("Отчёт пуст — сначала запустите диагностику."),
		             _("Диагностика"), wxICON_INFORMATION, this);
		return;
	}
	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(text));
		wxTheClipboard->Close();
		wxLogStatus(_("Отчёт скопирован в буфер обмена"));
	} else {
		wxMessageBox(_("Не удалось открыть буфер обмена."),
		             _("Диагностика"), wxICON_ERROR, this);
	}
}

void ibPluginManagerDialog::RunDiagnostics()
{
	DiagClear();
	if (m_activeRow < 0 || m_activeRow >= static_cast<int>(m_snap.rows.size())) {
		DiagInfo(_("Выберите плагин в списке слева, затем повторите запуск."));
		return;
	}

	// Snapshot the current UI state so the diagnostics reflect what the
	// user just typed (e.g. endpoint edit they haven't OK'd yet).
	CaptureCurrentRow();
	const auto& r = m_snap.rows[m_activeRow];
	const std::string idNarrow(r.pluginId.utf8_str());

	DiagInfo(wxString::Format(_("Плагин: %s"), r.pluginId));
	DiagInfo(wxString::Format(_("Версия: %s"),
	    r.version.IsEmpty() ? wxString(_("неизвестно")) : r.version));
	DiagInfo(wxString::Format(_("Endpoint: %s"),
	    r.endpoint.IsEmpty() ? wxString(_("не задан")) : r.endpoint));
	m_diagOutput->AppendText(wxT("\n"));

	// ---------------- Check 1 — plugin file exists ----------------------
	// We probe the path ibPluginManager actually scans. On macOS the
	// extension is .bundle; the spec naming matches that. On Win/Linux
	// the file may live as .dll / .so — we check the platform-native
	// extension first AND .bundle so the macOS-shaped spec still
	// produces an actionable result on any platform.
	const wxString pluginsDir = ibPluginManager::GetPluginsDir();
	const wxString nativeExt  = wxDynamicLibrary::GetDllExt(wxDL_MODULE);
	wxFileName fnNative(pluginsDir, wxString(wxT("lib")) + r.pluginId + nativeExt);
	wxFileName fnAlt(pluginsDir, wxString(wxT("lib")) + r.pluginId + wxT(".bundle"));
	wxFileName fnBare(pluginsDir, r.pluginId + nativeExt);
	wxString pluginFilePath;
	if (fnNative.FileExists())    pluginFilePath = fnNative.GetFullPath();
	else if (fnAlt.FileExists())  pluginFilePath = fnAlt.GetFullPath();
	else if (fnBare.FileExists()) pluginFilePath = fnBare.GetFullPath();
	if (!pluginFilePath.IsEmpty()) {
		DiagLine(true, wxString::Format(_("Файл плагина найден: %s"), pluginFilePath));
	} else {
		DiagLine(false, wxString::Format(_("Файл плагина НЕ найден в %s"), pluginsDir));
	}

	// ---------------- Check 2 — ABI version >= 3 ------------------------
	int abi = -1;
	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	if (pm != nullptr) {
		for (const auto& p : pm->Loaded()) {
			const wxString id = p.m_info && p.m_info->name
			    ? wxString::FromUTF8(p.m_info->name)
			    : wxFileName(p.m_path).GetName();
			if (id == r.pluginId && p.m_info) {
				abi = p.m_info->abi_version;
				break;
			}
		}
	}
	if (abi < 0) {
		DiagLine(false, _("ABI плагина: плагин не загружен — невозможно прочитать abi_version"));
	} else if (abi >= 3) {
		DiagLine(true, wxString::Format(
		    _("ABI плагина: %d (требуется >= 3, текущая хост-версия %d)"),
		    abi, IB_PLUGIN_ABI_VERSION));
	} else {
		DiagLine(false, wxString::Format(
		    _("ABI плагина: %d (требуется >= 3) — обновите плагин"), abi));
	}

	// ---------------- Check 3 — BYOK env file exists --------------------
	const wxString envPath = byokEnv::GetEnvFilePath(idNarrow);
	const bool envExists = wxFileName::FileExists(envPath);
	if (envExists) {
		DiagLine(true, wxString::Format(_("BYOK env найден: %s"), envPath));
	} else {
		DiagLine(false, wxString::Format(_("BYOK env НЕ найден: %s"), envPath));
	}

	// ---------------- Check 4 — BYOK file mode 0600 ---------------------
	// Unix-only. On Windows we acknowledge that ACLs aren't checked here
	// (byokEnv::Save enforces user-only ACL at write time).
#ifndef _WIN32
	if (envExists) {
		struct stat st{};
		if (::stat(std::string(envPath.utf8_str()).c_str(), &st) == 0) {
			const mode_t mode = st.st_mode & 0777;
			if (mode == 0600) {
				DiagLine(true, _("Права BYOK env: 0600 — корректно"));
			} else {
				DiagLine(false, wxString::Format(
				    _("Права BYOK env: 0%o — должно быть 0600 (chmod 600 %s)"),
				    static_cast<unsigned>(mode), envPath));
			}
		} else {
			DiagLine(false, _("Не удалось прочитать stat() для BYOK env"));
		}
	} else {
		DiagInfo(_("Проверка прав 0600 пропущена — файл отсутствует"));
	}
#else
	if (envExists) {
		DiagLine(true, _("Права BYOK env: NTFS ACL (проверка пропущена на Windows)"));
	} else {
		DiagInfo(_("Проверка прав пропущена — файл отсутствует"));
	}
#endif

	// ---------------- Check 5 — required env keys present ---------------
	// For aiBridge: TOKEN + TENANT. Other plugins: TOKEN only.
	// VALUES ARE NEVER PRINTED. Secret-looking keys are flagged
	// <redacted> in the "other keys" inventory.
	const auto envAll = byokEnv::LoadAll();
	auto pluginEnvIt = envAll.find(idNarrow);
	const byokEnv::KeyMap keys = (pluginEnvIt != envAll.end())
	    ? pluginEnvIt->second
	    : byokEnv::KeyMap();

	std::vector<std::string> requiredKeys;
	requiredKeys.emplace_back("TOKEN");
	if (r.pluginId.Lower().Contains(wxT("aibridge")) ||
	    r.pluginId.Lower().Contains(wxT("ai_bridge"))) {
		requiredKeys.emplace_back("TENANT");
	}
	for (const auto& key : requiredKeys) {
		auto it = keys.find(key);
		const bool present = (it != keys.end() && !it->second.empty());
		if (present) {
			DiagLine(true, wxString::Format(
			    _("Ключ %s: присутствует (значение скрыто <redacted>)"),
			    wxString::FromUTF8(key.c_str())));
		} else {
			DiagLine(false, wxString::Format(_("Ключ %s: отсутствует"),
			    wxString::FromUTF8(key.c_str())));
		}
	}
	if (!keys.empty()) {
		wxString other;
		for (const auto& [k, v] : keys) {
			bool isRequired = false;
			for (const auto& req : requiredKeys) {
				if (k == req) { isRequired = true; break; }
			}
			if (isRequired) continue;
			if (!other.IsEmpty()) other += wxT(", ");
			other += wxString::FromUTF8(k.c_str());
			if (LooksLikeSecretKey(k)) other += wxT("=<redacted>");
		}
		if (!other.IsEmpty()) {
			DiagInfo(wxString::Format(_("Другие ключи в env: %s"), other));
		}
	}

	// ---------------- Check 6 — TCP reach (5s timeout) ------------------
	// "Reachable" here means the kernel saw a SYN-ACK — enough to
	// distinguish "captive portal / DNS broken" from "host is fine
	// but token is wrong" without needing TLS material.
	if (r.endpoint.IsEmpty()) {
		DiagLine(false, _("Endpoint не задан — TCP-проверка пропущена"));
	} else {
		wxString scheme, host;
		int port = 0;
		if (!ParseEndpoint(r.endpoint, scheme, host, port)) {
			DiagLine(false, wxString::Format(
			    _("Некорректный URL endpoint: %s (ожидается http(s)://host[:port])"),
			    r.endpoint));
		} else {
			DiagInfo(wxString::Format(_("Проверка TCP %s:%d (timeout 5s)…"), host, port));
			wxBusyCursor busy;
			wxString detail;
			const bool ok = TcpConnectWithTimeout(host, port, 5, &detail);
			if (ok) {
				DiagLine(true, wxString::Format(
				    _("TCP %s:%d — соединение установлено"), host, port));
			} else {
				DiagLine(false, wxString::Format(_("TCP %s:%d — %s"),
				    host, port, detail.IsEmpty() ? _("ошибка") : detail));
			}
		}
	}

	// ---------------- Check 7 — Auth (llm_query ping) -------------------
	// Reuses the existing TestConnection path: ibPluginManager's
	// LLMQuery script function or, for v4 plugins, AIProvider.Query.
	// Response body is NOT logged (could echo prompt fragments that we
	// don't want in a support report).
	if (pm == nullptr) {
		DiagLine(false, _("Plugin manager не инициализирован — auth-ping пропущен"));
	} else {
		const ibPluginManager::RegisteredFunction* llmFn = nullptr;
		for (const auto& f : pm->Functions()) {
			if (f.m_name == "LLMQuery") { llmFn = &f; break; }
		}
		const bool haveV4Provider = pm->HasAIProviderFor("chat");
		if (llmFn == nullptr && !haveV4Provider) {
			DiagLine(false, _("Auth-ping пропущен: ни LLMQuery, ни v4 chat-провайдер не зарегистрированы"));
		} else {
			DiagInfo(_("Отправка llm_query ping…"));
			wxBusyCursor busy;
			const auto t0 = std::chrono::steady_clock::now();
			bool ok = false;
			int rcStatus = -1;
			if (llmFn != nullptr) {
				ibValue vPrompt; vPrompt.SetString(wxT("ping"));
				ibValue vLocale; vLocale.SetString(wxT("uk-UA"));
				ibValue* args[2] = { &vPrompt, &vLocale };
				ibValue ret;
				ok = pm->CallFunction(*llmFn, ret, args, 2);
				rcStatus = ok ? 200 : -1;
			} else {
				const auto& providers = pm->AIProviders();
				if (!providers.empty() && providers[0].Query != nullptr) {
					const char* req = R"({"prompt":"ping","locale":"uk-UA"})";
					rcStatus = providers[0].Query(req, "diag-ping",
					                              providers[0].userData);
					ok = (rcStatus == 0);
				}
			}
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			    std::chrono::steady_clock::now() - t0).count();
			if (ok) {
				DiagLine(true, wxString::Format(
				    _("Auth-ping OK за %lld ms (HTTP %d)"),
				    static_cast<long long>(ms), rcStatus));
			} else {
				DiagLine(false, wxString::Format(
				    _("Auth-ping НЕ удался за %lld ms (rc=%d) — проверьте токен и endpoint"),
				    static_cast<long long>(ms), rcStatus));
			}
		}
	}

	m_diagOutput->AppendText(wxT("\n"));
	DiagInfo(_("Диагностика завершена. Используйте «Скопировать отчёт в буфер» для отправки в поддержку."));
}
