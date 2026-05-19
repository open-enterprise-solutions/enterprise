/////////////////////////////////////////////////////////////////////////////
// ibHelpSearchView — "Search" tab.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpSearchView.h"

#include "frontend/help/helpPaneView.h"

#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"
#include "backend/compiler/compileCode.h"

#include <wx/sizer.h>
#include <wx/dnd.h>
#include <wx/dataobj.h>

ibHelpSearchView::ibHelpSearchView(wxWindow* parent, ibHelpPaneView* pane)
    : wxPanel(parent, wxID_ANY), m_pane(pane) {
	m_search = new wxSearchCtrl(this, wxID_ANY);
	m_badge  = new wxStaticText(this, wxID_ANY, wxT(""));
	m_list   = new wxListBox(this, wxID_ANY);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_search, 0, wxEXPAND | wxALL, 4);
	sizer->Add(m_badge,  0, wxLEFT  | wxBOTTOM, 4);
	sizer->Add(m_list,   1, wxEXPAND | wxALL, 4);
	SetSizer(sizer);

	Bind(wxEVT_TEXT,    &ibHelpSearchView::OnQueryChanged, this);
	Bind(wxEVT_LISTBOX, &ibHelpSearchView::OnSelection,    this);

	m_list->Bind(wxEVT_LEFT_DOWN, &ibHelpSearchView::OnListMouseDown, this);
	m_list->Bind(wxEVT_MOTION,    &ibHelpSearchView::OnListMouseMove, this);
}

void ibHelpSearchView::Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus) {
	m_corpus = corpus;
	RefreshList(m_search ? m_search->GetValue() : wxString());
}

void ibHelpSearchView::RefreshList(const wxString& query) {
	m_list->Clear();
	m_ids.clear();
	if (!m_corpus) {
		m_badge->SetLabel(wxT(""));
		return;
	}

	if (query.IsEmpty()) {
		m_badge->SetLabel(_("Found: 0 (enter a query)"));
		return;
	}

	const auto matches = m_corpus->SearchText(query);
	const short mode = ibCompileCode::GetCodeStyle();
	size_t visible = 0;
	for (const ibHelpEntry* e : matches) {
		if (!e->AppliesToMode(mode)) continue;
		m_list->Append(e->BilingualLabel());
		m_ids.push_back(e->id);
		++visible;
	}
	m_badge->SetLabel(wxString::Format(_("Found: %zu"), visible));
}

void ibHelpSearchView::OnQueryChanged(wxCommandEvent& event) {
	RefreshList(event.GetString());
}

void ibHelpSearchView::OnSelection(wxCommandEvent& event) {
	const int idx = event.GetSelection();
	if (idx < 0 || idx >= static_cast<int>(m_ids.size())) return;
	if (m_pane) m_pane->OnEntryActivated(m_ids[idx]);
}

void ibHelpSearchView::OnListMouseDown(wxMouseEvent& event) {
	m_dragStart = event.GetPosition();
	m_dragId.clear();
	if (m_list != nullptr) {
		const int idx = m_list->HitTest(m_dragStart);
		if (idx >= 0 && idx < static_cast<int>(m_ids.size()))
			m_dragId = m_ids[idx];
	}
	event.Skip();
}

void ibHelpSearchView::OnListMouseMove(wxMouseEvent& event) {
	if (!event.LeftIsDown() || m_dragId.empty() || m_corpus == nullptr) {
		event.Skip();
		return;
	}
	const wxPoint delta = event.GetPosition() - m_dragStart;
	if (std::abs(delta.x) + std::abs(delta.y) < 6) {
		event.Skip();
		return;
	}
	const ibHelpEntry* entry = m_corpus->FindById(m_dragId);
	if (entry == nullptr) { m_dragId.clear(); return; }
	const wxString tpl = entry->InsertTemplate(ibCompileCode::GetCodeStyle());
	if (tpl.IsEmpty()) { m_dragId.clear(); return; }
	wxTextDataObject data(tpl);
	wxDropSource src(data, m_list);
	src.DoDragDrop(wxDrag_DefaultMove);
	m_dragId.clear();
	event.Skip();
}
