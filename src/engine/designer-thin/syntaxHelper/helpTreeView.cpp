/////////////////////////////////////////////////////////////////////////////
// ibHelpTreeView — "Contents" tab.
//
// Walks the category tree owned by the corpus snapshot and builds a
// wxTreeCtrl. Each tree node carries an entry id in its client data
// when it represents a leaf (entry); category nodes have a null
// client data pointer.
/////////////////////////////////////////////////////////////////////////////

#include "designer-thin/syntaxHelper/helpTreeView.h"

#include "designer-thin/syntaxHelper/helpPaneView.h"

#include "core/syntaxHelper/helpCategory.h"
#include "core/syntaxHelper/helpCorpus.h"
#include "core/syntaxHelper/helpEntry.h"

#include <wx/sizer.h>
#include <wx/dnd.h>
#include <wx/dataobj.h>

#include <functional>

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
    : ibPanel(parent, wxID_ANY), m_pane(pane) {
	m_tree = new ibTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                          wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT |
	                          wxTR_SINGLE);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tree, 1, wxEXPAND);
	SetSizer(sizer);

	Bind(wxEVT_TREE_SEL_CHANGED,    &ibHelpTreeView::OnSelChanged,     this);
	Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibHelpTreeView::OnItemActivated, this);
	Bind(wxEVT_TREE_BEGIN_DRAG,     &ibHelpTreeView::OnBeginDrag,      this);
}

void ibHelpTreeView::Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus) {
	m_corpus = corpus;
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
	// Skip nodes that contain nothing visible in the active syntax
	// mode — keeps the tree compact in CES configurations where the
	// "VES terminators" sub-category collapses to empty.
	const short mode = m_pane->GetCodeStyle();
	auto entryVisible = [mode](const ibHelpEntry* e) {
		return e->AppliesToMode(mode);
	};
	// Cheap walk over the existing tree: the node's own article, its entries, its children's.
	std::function<bool(const ibHelpCategory*)> any = [&](const ibHelpCategory* n) -> bool {
		if (n->article != nullptr && entryVisible(n->article)) return true;
		for (const ibHelpEntry* e : n->entries)
			if (entryVisible(e)) return true;
		for (const auto& c : n->children)
			if (any(c.get())) return true;
		return false;
	};
	if (!any(node)) return;

	// A node that IS an article (a class over its members, an enum type over its values) is shown, opened and dragged
	// as that article; a section is its name.
	wxTreeItemId item;
	if (node->article != nullptr) {
		item = m_tree->AppendItem(parent, node->article->BilingualLabel(), -1, -1,
		                          new EntryIdData(node->article->id));
		m_entryItems[node->article->id] = item;
	}
	else
		item = m_tree->AppendItem(parent, node->displayName.IsEmpty() ? node->key : node->displayName);

	for (const auto& child : node->children) {
		AddCategoryNode(child.get(), item);
	}
	for (const ibHelpEntry* entry : node->entries) {
		if (!entryVisible(entry)) continue;
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

void ibHelpTreeView::OnBeginDrag(wxTreeEvent& event) {
	if (!m_corpus) return;
	auto* data = dynamic_cast<EntryIdData*>(m_tree->GetItemData(event.GetItem()));
	if (data == nullptr) return; // category node — nothing to drag
	const ibHelpEntry* entry = m_corpus->FindById(data->id);
	if (entry == nullptr) return;
	const wxString tpl = entry->InsertTemplate(m_pane->GetCodeStyle());
	if (tpl.IsEmpty()) return;
	wxTextDataObject payload(tpl);
	wxDropSource src(payload, m_tree);
	src.DoDragDrop(wxDrag_DefaultMove);
}
