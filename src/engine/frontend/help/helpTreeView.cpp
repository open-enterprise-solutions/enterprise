/////////////////////////////////////////////////////////////////////////////
// ibHelpTreeView — "Contents" tab.
//
// Walks the category tree owned by the corpus snapshot and builds a
// wxTreeCtrl. Each tree node carries an entry id in its client data
// when it represents a leaf (entry); category nodes have a null
// client data pointer.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpTreeView.h"

#include "frontend/help/helpPaneView.h"

#include "backend/help/helpCategory.h"
#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"

#include <wx/sizer.h>

namespace {

// Tree-item client data wrapping an entry id. wxTreeCtrl takes
// ownership of the pointer — we allocate one per leaf and the tree
// deletes them in DeleteAllItems / dtor.
struct EntryIdData : public wxTreeItemData {
	wxString id;
	explicit EntryIdData(const wxString& s) : id(s) {}
};

} // namespace

ibHelpTreeView::ibHelpTreeView(wxWindow* parent, ibHelpPaneView* pane)
    : wxPanel(parent, wxID_ANY), m_pane(pane) {
	m_tree = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                          wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT |
	                          wxTR_SINGLE);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tree, 1, wxEXPAND);
	SetSizer(sizer);

	Bind(wxEVT_TREE_SEL_CHANGED,    &ibHelpTreeView::OnSelChanged,     this);
	Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibHelpTreeView::OnItemActivated, this);
}

void ibHelpTreeView::Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus) {
	m_tree->DeleteAllItems();
	m_entryItems.clear();

	const wxTreeItemId root = m_tree->AddRoot(wxT(""));
	if (!corpus) return;
	const ibHelpCategory* topRoot = corpus->GetRoot();
	if (!topRoot) return;

	for (const auto& child : topRoot->children) {
		AddCategoryNode(child.get(), root);
	}
}

void ibHelpTreeView::AddCategoryNode(const ibHelpCategory* node,
                                       const wxTreeItemId&    parent) {
	// Category label: use the display name if the loader populated
	// it; fall back to the locale-stable key so the tree shows
	// SOMETHING even before _categories.json reads land in Phase 3.5.
	const wxString label =
	    node->displayName.IsEmpty() ? node->key : node->displayName;
	const wxTreeItemId item = m_tree->AppendItem(parent, label);

	for (const auto& child : node->children) {
		AddCategoryNode(child.get(), item);
	}
	for (const ibHelpEntry* entry : node->entries) {
		const wxTreeItemId leaf =
		    m_tree->AppendItem(item, entry->BilingualLabel(), -1, -1,
		                        new EntryIdData(entry->id));
		m_entryItems[entry->id] = leaf;
	}
}

void ibHelpTreeView::HighlightEntry(const wxString& entryId) {
	auto it = m_entryItems.find(entryId);
	if (it == m_entryItems.end()) return;
	m_tree->EnsureVisible(it->second);
	m_tree->SelectItem(it->second);
}

void ibHelpTreeView::OnSelChanged(wxTreeEvent& event) {
	const wxTreeItemId item = event.GetItem();
	if (!item.IsOk()) return;
	auto* data = dynamic_cast<EntryIdData*>(m_tree->GetItemData(item));
	if (data == nullptr) return; // category node, no entry
	if (m_pane) m_pane->OnEntryActivated(data->id);
}

void ibHelpTreeView::OnItemActivated(wxTreeEvent& event) {
	OnSelChanged(event);
}
