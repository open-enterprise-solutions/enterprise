#include "regionalsettingspanel.h"

#include "backend/appData.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/uilocale.h>

ibPanelRegionalSettings::ibPanelRegionalSettings(wxWindow* parent, int id, wxPoint pos, wxSize size, int style)
	: wxPanel(parent, id, pos, size, style)
{
	wxBoxSizer* page = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* box = new wxStaticBoxSizer(
		new wxStaticBox(this, -1, _("Regional settings of this base")), wxVERTICAL);

	wxStaticText* what = new wxStaticText(this, wxID_ANY,
		_("Every client of this base works in the base's time zone, wherever it sits: CurrentDate() "
		  "is the base's clock, and the base's server does the zone arithmetic. Dates themselves "
		  "are kept as written, with no zone in them."),
		wxDefaultPosition, wxDefaultSize, 0);
	what->Wrap(420);
	box->Add(what, 0, wxALL, 5);

	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 0, 0);
	grid->AddGrowableCol(1);
	grid->Add(new wxStaticText(this, wxID_ANY, _("Time zone:")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	m_zoneCtrl = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
	grid->Add(m_zoneCtrl, 1, wxALL | wxEXPAND, 5);
	grid->Add(new wxStaticText(this, wxID_ANY, _("Locale:")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	m_localeCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString);
	grid->Add(m_localeCtrl, 1, wxALL | wxEXPAND, 5);
	box->Add(grid, 0, wxEXPAND, 5);

	m_zoneNote = new wxStaticText(this, wxID_ANY, wxEmptyString);
	m_zoneNote->Wrap(420);
	box->Add(m_zoneNote, 0, wxALL, 5);

	page->Add(box, 0, wxALL | wxEXPAND, 5);
	SetSizer(page);
	Layout();
}

void ibPanelRegionalSettings::Initialize()
{
	// The zones the server knows, where it names them (Firebird does); elsewhere the name is typed.
	const std::vector<wxString> zones = ibRegionalSettings::KnownTimeZones();
	m_zoneCtrl->Clear();
	for (const wxString& zone : zones)
		m_zoneCtrl->Append(zone);
	m_zoneCtrl->SetValue(m_settings.m_timeZone);
	m_localeCtrl->SetValue(m_settings.m_locale);

	const wxString platformLocale = ibApplicationData::Get() != nullptr ? ibApplicationData::Get()->GetPlatformLocale() : wxString();
	m_zoneNote->SetLabel(zones.empty()
		? _("An IANA name, such as Europe/Kyiv. Empty: the zone the server process stands in. The locale is a tag such as uk-UA; "
		    "empty: the platform's default from backend.conf") + (platformLocale.IsEmpty() ? wxString() : wxT(" (") + platformLocale + wxT(")")) + wxT(".")
		: _("The names the base's server knows. Empty: no zone is named, and 'now' is each machine's own clock. The locale is a tag such as uk-UA; "
		    "empty: the platform's default from backend.conf") + (platformLocale.IsEmpty() ? wxString() : wxT(" (") + platformLocale + wxT(")")) + wxT("."));
	m_zoneNote->Wrap(420);
	Layout();
}

void ibPanelRegionalSettings::SetSettings(const ibRegionalSettings& settings)
{
	m_settings = settings;
}

const ibRegionalSettings& ibPanelRegionalSettings::GetSettings()
{
	m_settings.m_timeZone = m_zoneCtrl->GetValue().Trim().Trim(false);
	m_settings.m_locale = m_localeCtrl->GetValue().Trim().Trim(false);
	return m_settings;
}

bool ibPanelRegionalSettings::LocaleIsKnown() const
{
	const wxString tag = m_localeCtrl->GetValue().Trim().Trim(false);
	return tag.IsEmpty() || !wxLocaleIdent::FromTag(tag).IsEmpty();
}
