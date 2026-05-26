/////////////////////////////////////////////////////////////////////////////
// ibHelpPaneView — syntax-helper sidebar container.
//
// See helpPaneView.h and design v5 §5 for the contract.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/syntaxHelper/helpPaneView.h"

#include "frontend/syntaxHelper/helpDetailView.h"
#include "frontend/syntaxHelper/helpIndexView.h"
#include "frontend/syntaxHelper/helpSearchView.h"
#include "frontend/syntaxHelper/helpTreeView.h"

#include "backend/appData.h"
#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpEntry.h"
#include "backend/syntaxHelper/helpResolver.h"
#include "backend/syntaxHelper/helpService.h"

#include <wx/artprov.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/toolbar.h>
#include <wx/xml/xml.h>

// Local ids for the pane's own toolbar — start above wxID_HIGHEST so we
// don't collide with anything wx maps automatically.
namespace {
constexpr int IDC_HelpBack    = wxID_HIGHEST + 5500;
constexpr int IDC_HelpForward = wxID_HIGHEST + 5501;
constexpr int IDC_HelpReload  = wxID_HIGHEST + 5502;
constexpr int IDC_HelpSync    = wxID_HIGHEST + 5503;
constexpr int IDC_HelpZoomIn  = wxID_HIGHEST + 5504;
constexpr int IDC_HelpZoomOut = wxID_HIGHEST + 5505;
} // namespace

ibHelpPaneView::ibHelpPaneView(wxWindow* parent)
    : wxPanel(parent, wxID_ANY) {
	// Toolbar across the top — back / forward navigation, reload of
	// the live corpus snapshot, and a "sync from editor cursor" button
	// that mirrors what RawCtrl+F1 does without leaving the pane.
	m_toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition,
	                            wxDefaultSize,
	                            wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	const wxSize iconSize = FromDIP(wxSize(16, 16));
	m_toolbar->SetToolBitmapSize(iconSize);
	m_toolbar->AddTool(IDC_HelpBack,    _("Back"),
	    wxArtProvider::GetBitmapBundle(wxART_GO_BACK, wxART_TOOLBAR, iconSize),
	    _("Back (Alt+Left)"));
	m_toolbar->AddTool(IDC_HelpForward, _("Forward"),
	    wxArtProvider::GetBitmapBundle(wxART_GO_FORWARD, wxART_TOOLBAR, iconSize),
	    _("Forward (Alt+Right)"));
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(IDC_HelpSync,    _("Sync from editor"),
	    wxArtProvider::GetBitmapBundle(wxART_FIND, wxART_TOOLBAR, iconSize),
	    _("Look up identifier under the editor caret"));
	m_toolbar->AddTool(IDC_HelpReload,  _("Reload"),
	    wxArtProvider::GetBitmapBundle(wxART_REDO, wxART_TOOLBAR, iconSize),
	    _("Reload the help corpus from disk"));
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(IDC_HelpZoomIn,  _("Larger text"),
	    wxArtProvider::GetBitmapBundle(wxART_PLUS, wxART_TOOLBAR, iconSize),
	    _("Increase the help text size"));
	m_toolbar->AddTool(IDC_HelpZoomOut, _("Smaller text"),
	    wxArtProvider::GetBitmapBundle(wxART_MINUS, wxART_TOOLBAR, iconSize),
	    _("Decrease the help text size"));
	m_toolbar->Realize();

	// Vertical splitter: notebook on top (navigation surfaces), detail
	// HTML view on the bottom. The split position is biased toward
	// navigation since most user time is spent browsing.
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
	sizer->Add(m_toolbar, 0, wxEXPAND);
	sizer->Add(splitter,  1, wxEXPAND);
	SetSizer(sizer);

	Bind(wxEVT_NOTEBOOK_PAGE_CHANGED,
	     &ibHelpPaneView::OnTabSelectionChanged, this);
	Bind(wxEVT_TOOL, &ibHelpPaneView::OnToolbarCommand, this);
	Bind(wxEVT_UPDATE_UI, &ibHelpPaneView::OnUpdateToolbarUi, this,
	     IDC_HelpBack, IDC_HelpForward);

	RefreshFromAppData();
}

ibHelpPaneView::~ibHelpPaneView() = default;

void ibHelpPaneView::RefreshFromAppData() {
	auto* svc = appData ? appData->GetHelpService() : nullptr;
	m_corpus = svc ? svc->GetCorpus() : nullptr;
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

void ibHelpPaneView::OnToolbarCommand(wxCommandEvent& event) {
	switch (event.GetId()) {
		case IDC_HelpBack:    NavigateBack();        break;
		case IDC_HelpForward: NavigateForward();     break;
		case IDC_HelpReload:  ReloadCorpus();        break;
		case IDC_HelpSync:    SyncFromEditorCaret(); break;
		case IDC_HelpZoomIn:  if (m_detailView) m_detailView->AdjustFontSize(+1); break;
		case IDC_HelpZoomOut: if (m_detailView) m_detailView->AdjustFontSize(-1); break;
		default: event.Skip();
	}
}

void ibHelpPaneView::OnUpdateToolbarUi(wxUpdateUIEvent& event) {
	switch (event.GetId()) {
		case IDC_HelpBack:    event.Enable(CanNavigateBack());    break;
		case IDC_HelpForward: event.Enable(CanNavigateForward()); break;
		default: event.Skip();
	}
}

void ibHelpPaneView::ReloadCorpus() {
	if (auto* svc = appData ? appData->GetHelpService() : nullptr)
		svc->Reload();
	RefreshFromAppData();
}

void ibHelpPaneView::SaveStateToXml(wxXmlNode* parent) const {
	if (parent == nullptr) return;
	auto* node = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("helpPane"));
	node->AddAttribute(wxT("currentId"),    m_currentId);
	node->AddAttribute(wxT("tab"),          wxString::Format(wxT("%d"),
	                    m_notebook ? m_notebook->GetSelection() : 0));
	node->AddAttribute(wxT("fontBoost"),    wxString::Format(wxT("%d"),
	                    m_detailView ? m_detailView->GetFontSizeBoost() : 0));
	parent->AddChild(node);
}

void ibHelpPaneView::LoadStateFromXml(const wxXmlNode* parent) {
	if (parent == nullptr) return;
	for (const wxXmlNode* node = parent->GetChildren();
	     node != nullptr;
	     node = node->GetNext()) {
		if (node->GetName() != wxT("helpPane")) continue;

		long boost = 0;
		if (node->GetAttribute(wxT("fontBoost"), wxEmptyString).ToLong(&boost) &&
		    m_detailView)
			m_detailView->SetFontSizeBoost(static_cast<int>(boost));

		// Restore entry BEFORE tab — ShowEntry might shift the tab as
		// a side-effect (highlighting in the tree, etc); applying tab
		// last preserves the user's saved choice.
		const wxString id = node->GetAttribute(wxT("currentId"), wxEmptyString);
		if (!id.IsEmpty() && m_corpus && m_corpus->FindById(id))
			ShowEntry(id);

		long tab = 0;
		if (node->GetAttribute(wxT("tab"), wxEmptyString).ToLong(&tab) &&
		    m_notebook && tab >= 0 && tab < static_cast<long>(m_notebook->GetPageCount()))
			m_notebook->SetSelection(static_cast<int>(tab));
		break;
	}
}

void ibHelpPaneView::SyncFromEditorCaret() {
	// Delegate to the same routing path the editor's right-click menu
	// uses — wxID_FRONTEND_SYNTAX_HELPER_LOOKUP propagates up to the
	// designer frame, which calls OpenHelpForCursor(). Keeps the
	// codepath single-sourced.
	wxCommandEvent up(wxEVT_MENU, wxID_FRONTEND_SYNTAX_HELPER_LOOKUP);
	up.SetEventObject(this);
	if (wxTheApp) {
		if (wxWindow* top = wxTheApp->GetTopWindow())
			top->ProcessWindowEvent(up);
	}
}
