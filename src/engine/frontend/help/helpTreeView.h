/////////////////////////////////////////////////////////////////////////////
// "Contents" tab — hierarchical category tree.
//
// wxTreeCtrl populated from ibHelpCorpus::GetRoot(). Each tree node
// is one category or one entry; selection at an entry leaf fires the
// parent pane's OnEntryActivated.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_TREE_VIEW_H_
#define _IB_HELP_TREE_VIEW_H_

#include <wx/panel.h>
#include <wx/treectrl.h>

#include <memory>
#include <unordered_map>

class ibHelpCorpus;
class ibHelpPaneView;
struct ibHelpCategory;

class ibHelpTreeView : public wxPanel {
public:
	ibHelpTreeView(wxWindow* parent, ibHelpPaneView* pane);

	void Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus);

	// Highlight an entry node — called when the panel navigates to
	// an entry from a different surface (index / search / Ctrl+F1).
	// No-op if the id is not currently a leaf in this tree.
	void HighlightEntry(const wxString& entryId);

private:
	ibHelpPaneView* m_pane = nullptr;
	wxTreeCtrl*     m_tree = nullptr;

	// id → tree-item mapping for HighlightEntry. Built during
	// Rebuild; cleared on every Rebuild call so reload-safe.
	std::unordered_map<wxString, wxTreeItemId> m_entryItems;

	void OnSelChanged(wxTreeEvent& event);
	void OnItemActivated(wxTreeEvent& event);

	void AddCategoryNode(const ibHelpCategory* node,
	                      const wxTreeItemId& parent);
};

#endif // _IB_HELP_TREE_VIEW_H_
