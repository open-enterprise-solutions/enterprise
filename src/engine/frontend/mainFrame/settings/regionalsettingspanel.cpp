#include "regionalsettingspanel.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include "core/localization/translations.h"   // _() - the engine's own catalogs

ibPanelRegionalSettings::ibPanelRegionalSettings(wxWindow* parent, int id, wxPoint pos, wxSize size, int style)
	: wxPanel(parent, id, pos, size, style)
{
	wxBoxSizer* page = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* box = new wxStaticBoxSizer(
		new wxStaticBox(this, -1, _("Time zone of this base")), wxVERTICAL);

	wxStaticText* what = new wxStaticText(this, wxID_ANY,
		_("Every client of this base works in the base's time zone, wherever it sits: 'now' is the base's "
		  "server's clock in it, and the connections the base hands out stand in it. Dates themselves are kept "
		  "as written, with no zone in them."),
		wxDefaultPosition, wxDefaultSize, 0);
	what->Wrap(420);
	box->Add(what, 0, wxALL, 5);

	wxFlexGridSizer* grid = new wxFlexGridSizer(1, 2, 0, 0);
	grid->AddGrowableCol(1);
	grid->Add(new wxStaticText(this, wxID_ANY, _("Time zone:")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	m_zoneCtrl = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
	grid->Add(m_zoneCtrl, 1, wxALL | wxEXPAND, 5);
	box->Add(grid, 0, wxEXPAND, 5);

	m_zoneNote = new wxStaticText(this, wxID_ANY, wxEmptyString);
	m_zoneNote->Wrap(420);
	box->Add(m_zoneNote, 0, wxALL, 5);

	page->Add(box, 0, wxALL | wxEXPAND, 5);

	wxStaticBoxSizer* localeBox = new wxStaticBoxSizer(
		new wxStaticBox(this, -1, _("Locale of this base")), wxVERTICAL);

	wxStaticText* localeWhat = new wxStaticText(this, wxID_ANY,
		_("The locale the designer, the client and a launcher's child come up in when opened on this base - and "
		  "a thin client on a file base; over an application server it has none of its own. A name the platform "
		  "knows a language for: uk_UA, "
		  "en_GB, ru_RU (uk, uk-UA and Ukrainian are all uk_UA). Empty: the platform's own, from backend.conf or "
		  "the command line. In force for what is opened on the base from now on: this designer keeps the words "
		  "it came up with, though About and platform_state say the new name at once."),
		wxDefaultPosition, wxDefaultSize, 0);
	localeWhat->Wrap(420);
	localeBox->Add(localeWhat, 0, wxALL, 5);

	wxFlexGridSizer* localeGrid = new wxFlexGridSizer(1, 2, 0, 0);
	localeGrid->AddGrowableCol(1);
	localeGrid->Add(new wxStaticText(this, wxID_ANY, _("Locale:")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	m_localeCtrl = new wxTextCtrl(this, wxID_ANY);
	localeGrid->Add(m_localeCtrl, 1, wxALL | wxEXPAND, 5);
	localeBox->Add(localeGrid, 0, wxEXPAND, 5);

	page->Add(localeBox, 0, wxALL | wxEXPAND, 5);
	SetSizer(page);
	Layout();
}

void ibPanelRegionalSettings::Initialize()
{
	m_zoneCtrl->Clear();
	for (const wxString& zone : m_known)
		m_zoneCtrl->Append(zone);
	m_zoneCtrl->SetValue(m_zone);
	m_zoneNote->SetLabel(m_known.empty()
		? _("An IANA name, such as Europe/Kyiv. Empty: no zone is named, and each engine stands in its own.")
		: _("The names the base's server knows. Empty: no zone is named, and each engine stands in its own."));
	m_zoneNote->Wrap(420);
	m_localeCtrl->SetValue(m_locale);
	Layout();
}

void ibPanelRegionalSettings::SetTimeZone(const wxString& zone, const std::vector<wxString>& known)
{
	m_zone = zone;
	m_known = known;
}

wxString ibPanelRegionalSettings::GetTimeZone() const
{
	wxString zone = m_zoneCtrl->GetValue();
	return zone.Trim().Trim(false);
}

void ibPanelRegionalSettings::SetLocale(const wxString& locale)
{
	m_locale = locale;
}

wxString ibPanelRegionalSettings::GetLocale() const
{
	wxString locale = m_localeCtrl->GetValue();
	return locale.Trim().Trim(false);
}
