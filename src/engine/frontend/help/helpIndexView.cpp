/////////////////////////////////////////////////////////////////////////////
// ibHelpIndexView — "Index" tab.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpIndexView.h"

#include "frontend/help/helpPaneView.h"

#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"
#include "backend/compiler/compileCode.h"

#include <wx/sizer.h>
#include <wx/dnd.h>
#include <wx/dataobj.h>

ibHelpIndexView::ibHelpIndexView(wxWindow* parent, ibHelpPaneView* pane)
    : wxPanel(parent, wxID_ANY), m_pane(pane) {
	m_filter = new wxTextCtrl(this, wxID_ANY);
	m_list   = new wxListBox(this, wxID_ANY);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_filter, 0, wxEXPAND | wxALL, 4);
	sizer->Add(m_list,   1, wxEXPAND | wxALL, 4);
	SetSizer(sizer);

	Bind(wxEVT_TEXT,    &ibHelpIndexView::OnFilterChanged, this);
	Bind(wxEVT_LISTBOX, &ibHelpIndexView::OnSelection,     this);

	// Drag the selected entry's syntax template out to wxStyledTextCtrl-
	// backed editors. wxListBox doesn't ship a BEGIN_DRAG event; we
	// approximate it by capturing the press point and starting a
	// wxDropSource once the cursor moves past a small threshold.
	m_list->Bind(wxEVT_LEFT_DOWN, &ibHelpIndexView::OnListMouseDown, this);
	m_list->Bind(wxEVT_MOTION,    &ibHelpIndexView::OnListMouseMove, this);
}

void ibHelpIndexView::Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus) {
	m_corpus = corpus;
	RefreshList(m_filter ? m_filter->GetValue() : wxString());
}

void ibHelpIndexView::RefreshList(const wxString& prefix) {
	m_list->Clear();
	m_ids.clear();
	if (!m_corpus) return;

	std::vector<const ibHelpEntry*> matches;
	if (prefix.empty()) {
		matches = m_corpus->AllEntries();
	} else {
		matches = m_corpus->SearchPrefix(prefix);
	}

	const short mode = ibCompileCode::GetCodeStyle();
	for (const ibHelpEntry* e : matches) {
		if (!e->AppliesToMode(mode)) continue;
		m_list->Append(e->BilingualLabel());
		m_ids.push_back(e->id);
	}
}

void ibHelpIndexView::OnFilterChanged(wxCommandEvent& event) {
	RefreshList(event.GetString());
}

void ibHelpIndexView::OnSelection(wxCommandEvent& event) {
	const int idx = event.GetSelection();
	if (idx < 0 || idx >= static_cast<int>(m_ids.size())) return;
	if (m_pane) m_pane->OnEntryActivated(m_ids[idx]);
}

void ibHelpIndexView::OnListMouseDown(wxMouseEvent& event) {
	m_dragStart = event.GetPosition();
	m_dragId.clear();
	if (m_list != nullptr) {
		const int idx = m_list->HitTest(m_dragStart);
		if (idx >= 0 && idx < static_cast<int>(m_ids.size()))
			m_dragId = m_ids[idx];
	}
	event.Skip();
}

void ibHelpIndexView::OnListMouseMove(wxMouseEvent& event) {
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
