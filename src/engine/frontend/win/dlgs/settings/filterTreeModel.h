#ifndef _FILTER_TREE_MODEL_H__
#define _FILTER_TREE_MODEL_H__

#include "backend/tabularModelView.h"
#include "backend/compositionDescription.h"   // ibFilterDescription — the filter IS this

#include <map>
#include <vector>

////////////////////////////////////////////////////////////////////////////
// The filter as a TREE, for the settings form
////////////////////////////////////////////////////////////////////////////
//
// The filter tab used to be a flat grid over a list of lines. It is a TREE now,
// because the filter is: the root is a group, groups hold conditions and other
// groups, and `a AND (b OR c)` has no flat form.
//
// ⭐⭐ IT STANDS OVER THE DESCRIPTION, NOT OVER OBJECTS (2026-08-23). A filter used
// to be a live tree of ibValueFilterGroup / ibValueFilterItem values, and this
// model wrapped them one row per value. Those went under the knife with
// composition/listFilter.h: the filter is DATA now — nodes in vectors, a group
// owning its children — and a row stands for a PLACE in that data.
//
// A ROW IS ITS PATH. Nodes live in vectors, so a pointer to one dies the moment a
// sibling is added; the index chain from the root does not. Resolving a row means
// walking that chain, which is a handful of steps for a filter a person wrote.
//
// A GROUP ROW IS THE OPERATOR IT APPLIES. "And / Or / Not" is not decoration to
// paint across the row — it is the one thing about a group the user changes, so
// it lives in the LEFT cell as an editable choice, in the place where a condition
// keeps its own left-hand side. A group therefore fills exactly two columns —
// Use and Left — and HasValue says so, which is what leaves the rest of its row
// clean.
//
// (A spanned caption was the first shape here. It read as one thing and behaved
// as another: the span starts at the expander, so it painted straight over the
// Use checkbox, and a caption cannot be edited — leaving no way to say "make this
// group an OR", which is the only reason a group exists.)
////////////////////////////////////////////////////////////////////////////

// Column ids — column 0 is reserved by the ibDataViewCtrl fork (a 0 model
// column paints blank rows), so these start at 1.
enum ibFilterTreeColumn {
	kFilterColUse = 1,
	kFilterColLeft,
	kFilterColComparison,
	kFilterColRight,
	kFilterColDisplayMode,
	kFilterColPresentation,
};

// The chain of child indexes from the root down to one node. Empty = the root
// itself, which is a row like any other and the only one with no index.
using ibFilterPath = std::vector<size_t>;

// One row of the tree — a condition or a group, named by WHERE it sits. The
// model owns these and hands the same one back for the same path, so selection
// and expansion survive a refresh.
class ibFilterTreeNode : public ibDataViewObject {
public:
	ibFilterTreeNode(ibFilterDescription* filter, ibFilterPath path, ibFilterTreeNode* parent)
		: m_filter(filter), m_path(std::move(path)), m_parent(parent) {
	}

	const ibFilterPath& GetPath() const { return m_path; }
	bool IsRoot() const { return m_path.empty(); }

	// THE NODE THIS ROW STANDS FOR, or null when the path no longer leads
	// anywhere — a row outliving its line for the moment between a delete and the
	// refresh that follows it. Every caller asks and checks; there is no second
	// road that assumes the node is there.
	ibFilterNodeDescription* Resolve() const;

	// The children a row owns: a group's own list, the description's top level for
	// the root, null for a condition (which owns nothing).
	std::vector<ibFilterNodeDescription>* Children() const;

	// A GROUP holds children; a condition is a leaf. That is the whole hierarchy.
	virtual bool IsContainer() const override { return Children() != nullptr; }

	virtual ibDataViewItem GetParentItem() const override {
		return m_parent != nullptr ? ibDataViewItem(m_parent) : ibDataViewItem();
	}

	// A row OUTLIVES the shape it was in: grouping moves it under a new group,
	// ungrouping lifts it back. The model re-states the parent on each fetch
	// instead of dropping the row and losing the selection with it.
	void SetParent(ibFilterTreeNode* parent) { m_parent = parent; }
	void SetFilter(ibFilterDescription* filter) { m_filter = filter; }

private:
	ibFilterDescription* m_filter;   // the tree this path is read against — borrowed
	ibFilterPath         m_path;     // where the row sits; empty = the root
	ibFilterTreeNode*    m_parent;   // owned by the model, outlives this row
};

class ibFilterTreeModel : public ibDataViewModel {
public:
	ibFilterTreeModel() = default;

	// THE FILTER THIS TREE SHOWS — borrowed, never owned. It belongs to the
	// description the window is editing, which is a copy of the model's own; that
	// is what makes Cancel need nothing undone.
	void SetFilter(ibFilterDescription* filter);
	ibFilterDescription* GetFilter() const { return m_filter; }

	// ⭐ SHOW THE LINES MARKED **Inaccessible**, or leave them out — see ibFilterEditor::SetAuthoring.
	// True is the designer, where those lines are written; false is a reader, whom they are hidden
	// from while still being applied. The enumeration below is the only place it is asked.
	void SetAuthoring(bool authoring) { m_authoring = authoring; }

	// A STRUCTURAL change inside the SAME tree (added, deleted, moved, grouped).
	// The rows survive it — they are keyed by path — so this re-reads the shape
	// without throwing away identity, and the dialog can re-select the line the
	// user just acted on.
	void Refresh();
	// The row at a path, so the caller can select it. Invalid item when that path
	// has no row yet (never fetched).
	ibDataViewItem ItemFor(const ibFilterPath& path) const;
	// THE ROOT'S OWN ROW, created if the view has not fetched it yet — the dialog
	// needs it before the first paint to open the tree on it. A filter that shows
	// one collapsed line reads as an empty filter.
	ibDataViewItem RootItem() const;

	// The node behind a row, for the dialog's own commands (add, delete, group).
	// Null for the root, which stands for no node of its own.
	ibFilterNodeDescription* GetNode(const ibDataViewItem& item) const;
	// …and its path, which is what a command needs to delete or move it.
	ibFilterPath GetPath(const ibDataViewItem& item) const;

	// THE LIST A ROW LIVES IN — its owning group's children, or the description's
	// top level. Null for the root, which lives in nothing: that is what makes it
	// undeletable and unmovable without a second rule anywhere.
	std::vector<ibFilterNodeDescription>* GetOwnerChildren(const ibDataViewItem& item) const;
	// The list a NEW row should go into: the selected group's own children, or
	// those of the selected condition's owner, or the top level when nothing is
	// selected. GetTargetPath names the same place, so a caller that has just
	// appended can say WHERE the new line landed.
	std::vector<ibFilterNodeDescription>* GetTargetChildren(const ibDataViewItem& item) const;
	ibFilterPath GetTargetPath(const ibDataViewItem& item) const;

	// ---- ibDataViewModel ----
	void GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const override;
	bool SetValue(const wxVariant& variant, const ibDataViewItem& item, unsigned int col) override;
	ibDataViewItem GetParent(const ibDataViewItem& item) const override;
	bool IsContainer(const ibDataViewItem& item) const override;

	// WHICH CELLS A ROW ACTUALLY HAS. A group has a Use box and its caption, and
	// nothing else — no comparison, no right-hand value. Saying so is what makes
	// the grid give the caption their space instead of drawing empty cells.
	bool HasValue(const ibDataViewItem& item, unsigned col) const override;

	// 🛑 VIEW ONLY, AND THE ONLY DOOR THAT REACHES AN ACTIVATABLE CELL. The "Use" tick is toggled
	// straight from the click and from Space, without the start-editing event the editor vetoes —
	// the model's IsEnabled is the single gate the fork consults on both roads (final audit,
	// 2026-08-20).
	void SetReadOnly(bool readOnly) { m_readOnly = readOnly; }
	bool IsEnabled(const ibDataViewItem&, unsigned int) const override { return !m_readOnly; }

	unsigned int GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor,
		int count, ibDataViewItemArray& out) const override;

private:
	bool m_readOnly = false;   // view only — see SetReadOnly
	// One row object per PATH, kept alive by the model so a parent pointer stays
	// valid and so the same place keeps the same row across refreshes.
	ibFilterTreeNode* NodeFor(const ibFilterPath& path, ibFilterTreeNode* parent) const;
	static wxString KeyOf(const ibFilterPath& path);

	ibFilterDescription* m_filter = nullptr;
	bool m_authoring = true;   // see SetAuthoring — the designer sees the inaccessible lines
	// Keyed by the PATH, not by an address: a node lives in a vector, and adding a
	// sibling moves every one of them. The path is what a row IS.
	mutable std::map<wxString, wxObjectDataPtr<ibFilterTreeNode>> m_nodes;
	mutable std::vector<wxObjectDataPtr<ibFilterTreeNode>> m_owned;
};

#endif
