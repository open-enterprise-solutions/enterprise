#include "about.h"

#include "backend/metaCollection/metaObjectMetadata.h"
#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"

// Contributors string is built per-call so wxGetTranslation hits the live
// catalog (a `static const wxString = _(...)` would freeze the English
// form before the locale's .mo is loaded).
static wxString ContributorsText() {
	return _("wxWidgets and wxFormBuilder, Unknown Worlds Entertainment team")
	       + wxString(wxT("\n"))
	       + _("2C team, whose ideas were taken as the basis for building the interpreter")
	       + wxString(wxT("\n"))
	       + _("Tomasz Sowa who developed ttmath")
	       + wxString(wxT("\n"))
	       + _("And also everyone who was not mentioned here");
}

namespace {

// Compact read-only value field: no border, transparent background, fills
// the 2nd column of the info grid. Looks like a label but the user can still
// copy text out of it.
wxTextCtrl* AddInfoRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& value)
{
	wxStaticText* lbl = new wxStaticText(parent, wxID_ANY, label + wxT(":"));
	grid->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);

	wxTextCtrl* field = new wxTextCtrl(parent, wxID_ANY, value,
		wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxBORDER_NONE);
	field->SetBackgroundColour(parent->GetBackgroundColour());
	grid->Add(field, 1, wxEXPAND);

	return field;
}

} // namespace

ibDialogAbout::ibDialogAbout(wxWindow* parent, int id)
	: wxDialog(parent, id, _("About..."), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	// System font so the dialog looks native on each platform/theme.
	const wxFont sysFont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	wxFont headerFont = sysFont; headerFont.SetPointSize(sysFont.GetPointSize() + 2); headerFont.MakeBold();
	wxFont linkFont = sysFont; linkFont.MakeItalic();

	const int kPad = FromDIP(4);
	const int kRowGap = FromDIP(2);

	wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

	// --- Header block (title, subtitle, copyright link) ---
	m_staticTextHeader = new wxStaticText(this, wxID_ANY,
		wxString::Format(_("Open Enterprise Solutions, build %i"), GetBuildId()),
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	m_staticTextHeader->SetFont(headerFont);
	root->Add(m_staticTextHeader, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, kPad * 2);

	m_staticTextFramework = new wxStaticText(this, wxID_ANY,
		_("a RAD tool powered by wxWidgets framework"),
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	root->Add(m_staticTextFramework, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kPad);

	m_staticTextCommunity = new wxStaticText(this, wxID_ANY,
		_("(c) 2026 OES community"),
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	m_staticTextCommunity->SetFont(linkFont);
	m_staticTextCommunity->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT));
	m_staticTextCommunity->SetCursor(wxCURSOR_HAND);
	m_staticTextCommunity->Bind(wxEVT_LEFT_UP,
		[](wxMouseEvent& event) {
			wxLaunchDefaultBrowser(wxT("https://github.com/open-enterprise-solutions/enterprise"));
			event.Skip();
		});
	root->Add(m_staticTextCommunity, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kPad);

	m_staticlineHeader = new wxStaticLine(this, wxID_ANY);
	root->Add(m_staticlineHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, kPad);

	// --- Info block: 2-col flex grid (label | value) ---
	wxStaticBoxSizer* infoSizer = new wxStaticBoxSizer(
		new wxStaticBox(this, wxID_ANY, _("Info")), wxVERTICAL);
	wxWindow* infoHost = infoSizer->GetStaticBox();

	wxFlexGridSizer* grid = new wxFlexGridSizer(/*rows*/0, /*cols*/2, kRowGap, kPad * 2);
	grid->AddGrowableCol(1, 1);

	if (appData->GetDatabaseMode() != ibDatabaseMode::eNONE) {
		m_textCtrl1 = AddInfoRow(infoHost, grid,
			appData->GetDatabaseModeDescr(), appData->GetDatabaseDescription());
	}
	else {
		m_textCtrl1 = nullptr;
	}

	m_textCtrl2 = AddInfoRow(infoHost, grid, _("Application"), appData->GetRunModeDescr());
	m_textCtrl3 = AddInfoRow(infoHost, grid, _("User"),        appData->GetUserName());
	m_textCtrl4 = AddInfoRow(infoHost, grid, _("Locale"),      appData->GetLocale());

	infoSizer->Add(grid, 0, wxEXPAND | wxALL, kPad);
	root->Add(infoSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kPad);

	// --- Plugins block: only if any are loaded; otherwise skip entirely ---
	if (ibPluginManager* pm = appData->GetPluginManager()) {
		const auto& plugins = pm->Loaded();
		if (!plugins.empty()) {

			wxStaticBoxSizer* pluginsSizer = new wxStaticBoxSizer(
				new wxStaticBox(this, wxID_ANY, _("Plugins")), wxVERTICAL);

			wxString text;
			for (const auto& p : plugins) {
				text << wxString::FromUTF8(p.m_info->name ? p.m_info->name : "?")
					<< wxT("  ")
					<< wxString::FromUTF8(p.m_info->version ? p.m_info->version : "")
					<< wxT("\n");
			}
			if (!text.empty() && text.Last() == wxT('\n'))
				text.RemoveLast();

			wxTextCtrl* list = new wxTextCtrl(pluginsSizer->GetStaticBox(), wxID_ANY,
				text, wxDefaultPosition, FromDIP(wxSize(-1, 48)),
				wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
			list->SetBackgroundColour(pluginsSizer->GetStaticBox()->GetBackgroundColour());
			pluginsSizer->Add(list, 1, wxEXPAND | wxALL, kPad);

			root->Add(pluginsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kPad);
		}
	}

	// --- Thanks block ---
	wxStaticBoxSizer* thanksSizer = new wxStaticBoxSizer(
		new wxStaticBox(this, wxID_ANY, _("Thanks")), wxVERTICAL);
	m_staticTextThanks = nullptr;

	m_textCtrlContributors = new wxTextCtrl(thanksSizer->GetStaticBox(), wxID_ANY,
		ContributorsText(), wxDefaultPosition, FromDIP(wxSize(-1, 64)),
		wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
	m_textCtrlContributors->SetBackgroundColour(thanksSizer->GetStaticBox()->GetBackgroundColour());
	thanksSizer->Add(m_textCtrlContributors, 1, wxEXPAND | wxALL, kPad);

	root->Add(thanksSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kPad);

	// --- OK button, right-aligned ---
	wxStdDialogButtonSizer* btns = new wxStdDialogButtonSizer();
	m_buttonOK = new wxButton(this, wxID_OK);
	btns->AddButton(m_buttonOK);
	btns->Realize();
	root->Add(btns, 0, wxALIGN_RIGHT | wxALL, kPad * 2);

	SetSizer(root);

	// Fit to content first, then enforce a sensible minimum width so the
	// header and copyright lines don't wrap on a short translation.
	root->SetSizeHints(this);
	const wxSize minSize = FromDIP(wxSize(440, -1));
	if (GetSize().x < minSize.x)
		SetSize(minSize.x, GetSize().y);

	Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_metaCommonMetadataCLSID));
	SetIcon(dlg_icon);
	SetFocus();

	// Suppress the unused-member warning — m_staticDataBaseInfo/m_staticAppInfo/
	// m_staticUserInfo/m_staticLocaleInfo are declared in the header but the
	// labels are now local (added via AddInfoRow) to keep the layout compact.
	m_staticDataBaseInfo = nullptr;
	m_staticAppInfo = nullptr;
	m_staticUserInfo = nullptr;
	m_staticLocaleInfo = nullptr;
}
