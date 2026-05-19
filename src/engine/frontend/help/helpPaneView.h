/////////////////////////////////////////////////////////////////////////////
// Syntax-helper sidebar panel for Designer.
//
// One panel widget containing:
//   - top: wxNotebook with three tabs (Зміст / Індекс / Пошук) that
//     surface the corpus through different navigation patterns
//   - bottom: wxHtmlWindow detail view rendering the currently
//     selected entry as structured HTML
//
// Selection in any of the three tabs invokes ShowEntry(id); the panel
// owns the navigation history stack (Alt+← / Alt+→).
//
// See docs/syntax-helper-design.md §5 for the binding contract.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_PANE_VIEW_H_
#define _IB_HELP_PANE_VIEW_H_

#include <wx/bookctrl.h>
#include <wx/panel.h>

#include <memory>
#include <vector>

class ibHelpCorpus;
class ibHelpTreeView;
class ibHelpIndexView;
class ibHelpSearchView;
class ibHelpDetailView;
class wxNotebook;

class ibHelpPaneView : public wxPanel {
public:
	ibHelpPaneView(wxWindow* parent);
	~ibHelpPaneView() override;

	// Re-read the corpus snapshot from appData and refresh views.
	// Called once on construction and again after a reload event.
	void RefreshFromAppData();

	// Activate the pane on a specific entry id. Records the previous
	// entry in the navigation history. No-op if the id is not in the
	// active corpus snapshot.
	void ShowEntry(const wxString& entryId);

	// History navigation — wired to Alt+← / Alt+→ accelerators owned
	// by the host frame. NavigateBack pops the current entry onto the
	// forward stack; NavigateForward does the reverse.
	bool CanNavigateBack()    const;
	bool CanNavigateForward() const;
	void NavigateBack();
	void NavigateForward();

private:
	std::shared_ptr<const ibHelpCorpus> m_corpus;

	wxNotebook*       m_notebook       = nullptr;
	ibHelpTreeView*   m_treeView       = nullptr;
	ibHelpIndexView*  m_indexView      = nullptr;
	ibHelpSearchView* m_searchView     = nullptr;
	ibHelpDetailView* m_detailView     = nullptr;

	// History stacks. m_currentId is the entry currently displayed;
	// Back pops m_history.back() onto m_forward and moves it into
	// m_currentId.
	wxString              m_currentId;
	std::vector<wxString> m_history;
	std::vector<wxString> m_forward;

	void OnTabSelectionChanged(wxBookCtrlEvent&);

	// Tab views fire entry-selection notifications through here so
	// the pane controls the detail view and history state in one
	// place. Tab views don't talk to the detail view directly.
	void OnEntryActivated(const wxString& entryId);

	friend class ibHelpTreeView;
	friend class ibHelpIndexView;
	friend class ibHelpSearchView;
};

#endif // _IB_HELP_PANE_VIEW_H_
