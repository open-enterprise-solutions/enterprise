#include "fullTextSearch.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/gauge.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

namespace {

namespace fts = ibProtocol::ibSchema::ibFullTextSearch;

// The hidden columns of the hits: the metaobject's id and the record's key, which Open is asked with.
enum ibHitColumn { HitTitle, HitMetadata, HitMetaId, HitKey };

bool CallSchema(ibProtocol::ibSchema::ibKind kind, const ibProtocolNode& params, ibProtocolNode& answer)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr && frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

}

//////////////////////////////////////////////////////////////////////
//  Administration
//////////////////////////////////////////////////////////////////////

ibDialogFullTextSearch::ibDialogFullTextSearch(wxWindow* parent, const ibProtocolNode& shown)
	: wxDialog(parent, wxID_ANY, _("Full-text search index"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
	wxBoxSizer* const mainSizer = new wxBoxSizer(wxVERTICAL);

	m_enabled = new wxCheckBox(this, wxID_ANY, _("Full-text search is enabled in this base"));
	mainSizer->Add(m_enabled, 0, wxALL | wxEXPAND, FromDIP(8));

	m_state = new wxStaticText(this, wxID_ANY, wxEmptyString);
	mainSizer->Add(m_state, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(8));
	m_updated = new wxStaticText(this, wxID_ANY, wxEmptyString);
	mainSizer->Add(m_updated, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(8));

	m_gauge = new wxGauge(this, wxID_ANY, 1000, wxDefaultPosition, FromDIP(wxSize(360, -1)));
	mainSizer->Add(m_gauge, 0, wxALL | wxEXPAND, FromDIP(8));

	wxBoxSizer* const buttons = new wxBoxSizer(wxHORIZONTAL);
	m_update = new wxButton(this, wxID_ANY, _("Update index"));
	m_stop = new wxButton(this, wxID_ANY, _("Stop"));
	m_clear = new wxButton(this, wxID_ANY, _("Clear index"));
	m_close = new wxButton(this, wxID_CANCEL, _("Close"));
	buttons->Add(m_update, 0, wxRIGHT, FromDIP(4));
	buttons->Add(m_stop, 0, wxRIGHT, FromDIP(4));
	buttons->Add(m_clear, 0, wxRIGHT, FromDIP(4));
	buttons->AddStretchSpacer();
	buttons->Add(m_close, 0);
	mainSizer->Add(buttons, 0, wxALL | wxEXPAND, FromDIP(8));

	SetSizerAndFit(mainSizer);
	Centre(wxBOTH);

	m_enabled->Bind(wxEVT_CHECKBOX, &ibDialogFullTextSearch::OnEnabled, this);
	m_update->Bind(wxEVT_BUTTON, &ibDialogFullTextSearch::OnUpdate, this);
	m_stop->Bind(wxEVT_BUTTON, &ibDialogFullTextSearch::OnStop, this);
	m_clear->Bind(wxEVT_BUTTON, &ibDialogFullTextSearch::OnClear, this);
	Bind(wxEVT_CLOSE_WINDOW, &ibDialogFullTextSearch::OnClose, this);

	Show(shown);
}

void ibDialogFullTextSearch::Show(const ibProtocolNode& shown)
{
	const bool enabled = shown.GetBool(fts::Enabled);
	m_indexTrue = shown.GetBool(fts::IndexTrue);
	m_indexed = shown.GetInt(fts::Indexed);
	m_total = shown.GetInt(fts::Total);

	m_enabled->SetValue(enabled);
	if (!enabled)
		m_state->SetLabel(_("Full-text search is switched off: nothing is indexed and nothing can be found."));
	else if (m_indexTrue)
		m_state->SetLabel(ibString::Format(_("The index is up to date: %lld records are indexed."), m_total));
	else
		m_state->SetLabel(ibString::Format(_("The index needs updating: %lld of %lld records are indexed."), m_indexed, m_total));

	const ibString updated = shown.GetString(fts::Updated);
	m_updated->SetLabel(updated.IsEmpty() ? wxString(_("The index has not been built completely yet."))
		: wxString(ibString::Format(_("Last built completely: %s"), updated)));

	m_gauge->SetValue(m_total > 0 ? static_cast<int>(m_indexed * 1000 / m_total) : (m_indexTrue ? 1000 : 0));

	m_enabled->Enable(!m_running);
	m_update->Enable(enabled && !m_running && !m_indexTrue);
	m_stop->Enable(m_running);
	m_clear->Enable(enabled && !m_running);
	m_close->Enable(!m_running);
	Layout();
}

bool ibDialogFullTextSearch::Refresh()
{
	ibProtocolNode params, shown;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::FullTextSearch));
	if (!CallSchema(ibProtocol::ibSchema::ibKind::FullTextSearch, params, shown))
		return false;
	Show(shown);
	return true;
}

bool ibDialogFullTextSearch::Command(int command, bool withEnabled, bool enabled)
{
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::FullTextSearch));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(command));
	if (withEnabled)
		params.Child(ibProtocol::ibSchema::Args).SetValue(fts::Enabled, enabled);
	return CallSchema(ibProtocol::ibSchema::ibKind::FullTextSearch, params, answer);
}

void ibDialogFullTextSearch::OnEnabled(wxCommandEvent& WXUNUSED(event))
{
	const bool enabled = m_enabled->GetValue();
	if (!enabled && wxMessageBox(_("Switching full-text search off clears the index: it will have to be built again. Switch it off?"),
		GetTitle(), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
		m_enabled->SetValue(true);
		return;
	}
	Command(static_cast<int>(ibProtocol::ibSchema::ibCommand::Set), true, enabled);
	Refresh();
}

void ibDialogFullTextSearch::OnUpdate(wxCommandEvent& WXUNUSED(event))
{
	m_running = true;
	m_stopAsked = false;
	const long long before = m_indexed;
	bool refused = false;

	// A portion a call: between them the window draws the gauge and hears Stop.
	while (!m_stopAsked) {
		if (!Command(static_cast<int>(ibProtocol::ibSchema::ibCommand::Start)) || !Refresh()) {
			refused = true;
			break;
		}
		if (m_indexTrue)
			break;
		wxSafeYield(this, true);
	}

	m_running = false;
	Refresh();
	if (m_stopAsked && !refused && !m_indexTrue)
		wxMessageBox(ibString::Format(_("The update was stopped: %lld records were indexed in this run, %lld of %lld are indexed now."),
			m_indexed - before, m_indexed, m_total), GetTitle(), wxOK | wxICON_INFORMATION, this);
}

void ibDialogFullTextSearch::OnStop(wxCommandEvent& WXUNUSED(event))
{
	m_stopAsked = true;
}

void ibDialogFullTextSearch::OnClear(wxCommandEvent& WXUNUSED(event))
{
	if (wxMessageBox(_("Clear the full-text index? Nothing can be found until it is built again."),
		GetTitle(), wxYES_NO | wxICON_QUESTION, this) != wxYES)
		return;
	Command(static_cast<int>(ibProtocol::ibSchema::ibCommand::Clear));
	Refresh();
}

void ibDialogFullTextSearch::OnClose(wxCloseEvent& event)
{
	// A build under way is stopped first, between its portions.
	if (m_running && event.CanVeto()) {
		m_stopAsked = true;
		event.Veto();
		return;
	}
	event.Skip();
}

//////////////////////////////////////////////////////////////////////
//  Find
//////////////////////////////////////////////////////////////////////

ibDialogSearch::ibDialogSearch(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, _("Find in data"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* const mainSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* const line = new wxBoxSizer(wxHORIZONTAL);
	m_text = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_text->SetHint(_("Words to find - each may be the beginning of a word, or misspelt"));
	wxButton* const find = new wxButton(this, wxID_ANY, _("Find"));
	line->Add(m_text, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(4));
	line->Add(find, 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(line, 0, wxALL | wxEXPAND, FromDIP(8));

	m_found = new wxStaticText(this, wxID_ANY, wxEmptyString);
	mainSizer->Add(m_found, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(8));

	m_hits = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(560, 320)), wxLC_REPORT | wxLC_SINGLE_SEL);
	m_hits->AppendColumn(_("Found"), wxLIST_FORMAT_LEFT, FromDIP(330));
	m_hits->AppendColumn(_("Where"), wxLIST_FORMAT_LEFT, FromDIP(210));
	m_hits->AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, 0);   // HitMetaId
	m_hits->AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, 0);   // HitKey
	mainSizer->Add(m_hits, 1, wxALL | wxEXPAND, FromDIP(8));

	SetSizerAndFit(mainSizer);
	Centre(wxBOTH);

	find->Bind(wxEVT_BUTTON, &ibDialogSearch::OnFind, this);
	m_text->Bind(wxEVT_TEXT_ENTER, &ibDialogSearch::OnFind, this);
	m_hits->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibDialogSearch::OnOpen, this);
	Bind(wxEVT_CLOSE_WINDOW, &ibDialogSearch::OnClose, this);
	m_text->SetFocus();
}

void ibDialogSearch::OnFind(wxCommandEvent& WXUNUSED(event))
{
	const ibString text = m_text->GetValue();
	if (text.TrimAll().IsEmpty())
		return;

	ibProtocolNode params, shown;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Search));
	params.Child(ibProtocol::ibSchema::Args).SetValue(fts::Text, text);
	if (!CallSchema(ibProtocol::ibSchema::ibKind::Search, params, shown))
		return;

	m_hits->DeleteAllItems();
	if (!shown.GetBool(fts::Enabled)) {
		m_found->SetLabel(_("Full-text search is switched off in this base (Administration - Full-text search index)."));
		return;
	}

	const std::vector<ibProtocolNode> rows = shown.FindChild(fts::Hits).Children();
	const long long total = shown.GetInt(fts::Total);
	for (const ibProtocolNode& row : rows) {
		const long index = m_hits->InsertItem(m_hits->GetItemCount(), row.GetString(fts::Title));
		m_hits->SetItem(index, HitMetadata, row.GetString(fts::Metadata));
		m_hits->SetItem(index, HitMetaId, ibString::Format(ibT("%lld"), row.GetInt(fts::MetaId)));
		m_hits->SetItem(index, HitKey, row.GetString(fts::Key));
	}

	if (total == 0)
		m_found->SetLabel(_("Nothing was found."));
	else if (static_cast<long long>(rows.size()) < total)
		m_found->SetLabel(ibString::Format(_("Found: %lld, the first %zu are shown."), total, rows.size()));
	else
		m_found->SetLabel(ibString::Format(_("Found: %lld."), total));
	Layout();
}

void ibDialogSearch::OnOpen(wxListEvent& event)
{
	const long row = event.GetIndex();
	if (row < 0)
		return;
	long long metaId = 0;
	m_hits->GetItemText(row, HitMetaId).ToLongLong(&metaId);

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Search));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Open));
	params.Child(ibProtocol::ibSchema::Args).SetValue(fts::MetaId, metaId).SetValue(fts::Key, ibString(m_hits->GetItemText(row, HitKey)));
	CallSchema(ibProtocol::ibSchema::ibKind::Search, params, answer);
}

void ibDialogSearch::OnClose(wxCloseEvent& WXUNUSED(event))
{
	Destroy();
}
