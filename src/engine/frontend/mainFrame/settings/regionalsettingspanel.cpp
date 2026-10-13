#include "regionalsettingspanel.h"

#include <wx/sizer.h>
#include <wx/statbox.h>

ibPanelRegionalSettings::ibPanelRegionalSettings(wxWindow* parent, int id, wxPoint pos, wxSize size, int style)
	: wxPanel(parent, id, pos, size, style)
{
	wxBoxSizer* page = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* box = new wxStaticBoxSizer(
		new wxStaticBox(this, -1, _("Time zone of this base")), wxVERTICAL);

	wxStaticText* what = new wxStaticText(this, wxID_ANY,
		_("Every client of this base works in the base's time zone, wherever it sits: the base's connections "
		  "stand in it, and the base's server does the zone arithmetic. Dates themselves are kept as written, "
		  "with no zone in them."),
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
