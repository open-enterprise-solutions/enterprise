/////////////////////////////////////////////////////////////////////////////
// ibHelpIndexView — "Index" tab.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpIndexView.h"

#include "frontend/help/helpPaneView.h"

#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"

#include <wx/sizer.h>

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

	for (const ibHelpEntry* e : matches) {
		const wxString label =
		    e->nameLocal.IsEmpty() ? e->nameEn : e->nameLocal;
		m_list->Append(label);
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
