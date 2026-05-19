/////////////////////////////////////////////////////////////////////////////
// ibHelpPaneView — syntax-helper sidebar container.
//
// See helpPaneView.h and design v5 §5 for the contract.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpPaneView.h"

#include "frontend/help/helpDetailView.h"
#include "frontend/help/helpIndexView.h"
#include "frontend/help/helpSearchView.h"
#include "frontend/help/helpTreeView.h"

#include "backend/appData.h"
#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"

#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/splitter.h>

ibHelpPaneView::ibHelpPaneView(wxWindow* parent)
    : wxPanel(parent, wxID_ANY) {
	// Vertical splitter: notebook on top (navigation surfaces),
	// detail HTML view on the bottom. The split position is biased
	// toward navigation since most user time is spent browsing.
	auto* splitter = new wxSplitterWindow(this, wxID_ANY,
	                                        wxDefaultPosition, wxDefaultSize,
	                                        wxSP_LIVE_UPDATE | wxSP_3D);
	splitter->SetSashGravity(0.45);
	splitter->SetMinimumPaneSize(80);

	m_notebook   = new wxNotebook(splitter, wxID_ANY);
	m_treeView   = new ibHelpTreeView(m_notebook, this);
	m_indexView  = new ibHelpIndexView(m_notebook, this);
	m_searchView = new ibHelpSearchView(m_notebook, this);

	m_notebook->AddPage(m_treeView,   _("Contents"));
	m_notebook->AddPage(m_indexView,  _("Index"));
	m_notebook->AddPage(m_searchView, _("Search"));

	m_detailView = new ibHelpDetailView(splitter, this);

	splitter->SplitHorizontally(m_notebook, m_detailView, 280);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(splitter, 1, wxEXPAND);
	SetSizer(sizer);

	Bind(wxEVT_NOTEBOOK_PAGE_CHANGED,
	     &ibHelpPaneView::OnTabSelectionChanged, this);

	RefreshFromAppData();
}

ibHelpPaneView::~ibHelpPaneView() = default;

void ibHelpPaneView::RefreshFromAppData() {
	m_corpus = appData->GetHelpCorpus();
	m_treeView->Rebuild(m_corpus);
	m_indexView->Rebuild(m_corpus);
	m_searchView->Rebuild(m_corpus);
	m_detailView->ShowEntry(nullptr);
}

void ibHelpPaneView::ShowEntry(const wxString& entryId) {
	if (entryId.empty() || !m_corpus) return;
	const ibHelpEntry* e = m_corpus->FindById(entryId);
	if (e == nullptr) return;

	if (!m_currentId.empty() && m_currentId != entryId) {
		m_history.push_back(m_currentId);
		m_forward.clear();
	}
	m_currentId = entryId;
	m_detailView->ShowEntry(e);
	m_treeView->HighlightEntry(entryId);
}

bool ibHelpPaneView::CanNavigateBack()    const { return !m_history.empty(); }
bool ibHelpPaneView::CanNavigateForward() const { return !m_forward.empty(); }

void ibHelpPaneView::NavigateBack() {
	if (m_history.empty()) return;
	const wxString prev = m_history.back();
	m_history.pop_back();
	if (!m_currentId.empty()) m_forward.push_back(m_currentId);

	const ibHelpEntry* e = m_corpus ? m_corpus->FindById(prev) : nullptr;
	if (e == nullptr) return;
	m_currentId = prev;
	m_detailView->ShowEntry(e);
	m_treeView->HighlightEntry(prev);
}

void ibHelpPaneView::NavigateForward() {
	if (m_forward.empty()) return;
	const wxString next = m_forward.back();
	m_forward.pop_back();
	if (!m_currentId.empty()) m_history.push_back(m_currentId);

	const ibHelpEntry* e = m_corpus ? m_corpus->FindById(next) : nullptr;
	if (e == nullptr) return;
	m_currentId = next;
	m_detailView->ShowEntry(e);
	m_treeView->HighlightEntry(next);
}

void ibHelpPaneView::OnTabSelectionChanged(wxBookCtrlEvent& event) {
	event.Skip();
}

void ibHelpPaneView::OnEntryActivated(const wxString& entryId) {
	ShowEntry(entryId);
}
