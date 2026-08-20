#ifndef _FILTER_TREE_MODEL_H__
#define _FILTER_TREE_MODEL_H__

#include "backend/tabularModelView.h"
#include "backend/composition/listFilter.h"

#include <map>

////////////////////////////////////////////////////////////////////////////
// The filter as a TREE, for the settings form
////////////////////////////////////////////////////////////////////////////
//
// The filter tab used to be a flat grid over a list of lines. It is a TREE now,
// because the filter is: the root is a group, groups hold conditions and other
// groups, and `a AND (b OR c)` has no flat form (listFilter.h).
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

// One row of the tree — a condition or a group. It wraps the VALUE it stands
// for; the model owns these and hands the same one back for the same value, so
// selection and expansion survive a refresh.
class ibFilterTreeNode : public ibDataViewObject {
public:
	ibFilterTreeNode(const ibValue& node, ibFilterTreeNode* parent)
		: m_node(node), m_parent(parent) {
	}

	const ibValue& GetNode() const { return m_node; }

	// EVERY ROW IS ASKED BOTH QUESTIONS on every paint, and one of the two answers
	// is always no — so the question must be `ConvertToValue` (answers) and never
	// `ConvertToType` (asserts, and raises "variable type does not support this
	// operation" outside the Designer).
	ibValueFilterItem* GetItem() const {
		ibValueFilterItem* item = nullptr;
		return m_node.ConvertToValue(item) ? item : nullptr;
	}
	ibValueFilterGroup* GetGroup() const {
		ibValueFilterGroup* group = nullptr;
		return m_node.ConvertToValue(group) ? group : nullptr;
	}

	// A GROUP holds children; a condition is a leaf. That is the whole hierarchy
	// — and it is why only a group can be a node at all.
	virtual bool IsContainer() const override { return GetGroup() != nullptr; }

	virtual ibDataViewItem GetParentItem() const override {
		return m_parent != nullptr ? ibDataViewItem(m_parent) : ibDataViewItem();
	}

	// A row OUTLIVES the shape it was in: grouping moves it under a new group,
	// ungrouping lifts it back. It is the SAME row — only its parent changed — so
	// the model re-states the parent on each fetch instead of dropping the row and
	// losing the selection with it.
	void SetParent(ibFilterTreeNode* parent) { m_parent = parent; }

private:
	ibValue           m_node;     // the condition or group this row stands for
	ibFilterTreeNode* m_parent;   // owned by the model, outlives this row
};

class ibFilterTreeModel : public ibDataViewModel {
public:
	ibFilterTreeModel() = default;

	// The root group — the "Filter" line every other row hangs under.
	void SetRoot(ibValueFilterGroup* root);
	ibValueFilterGroup* GetRoot() const { return m_root; }

	// A STRUCTURAL change inside the SAME tree (added, deleted, moved, grouped).
	// The rows survive it — that is the whole reason they are keyed by the value
	// object — so this re-reads the shape without throwing away identity, and the
	// dialog can re-select the line the user just acted on.
	void Refresh();
	// The row standing for a value, so the caller can select it. Invalid item when
	// that value has no row yet (never fetched).
	ibDataViewItem ItemFor(const ibValue& value) const;
	// THE ROOT'S OWN ROW, created if the view has not fetched it yet — the dialog
	// needs it before the first paint to open the tree on it. A filter that shows
	// one collapsed line reads as an empty filter.
	ibDataViewItem RootItem() const;

	// The value behind a row, for the dialog's own commands (add, delete, group).
	ibValueFilterItem* GetItem(const ibDataViewItem& item) const;
	ibValueFilterGroup* GetGroup(const ibDataViewItem& item) const;

	// THE GROUP A ROW LIVES IN. A top-level row has NO parent row — the root group
	// is invisible — so asking the tree for its parent answers "none", and every
	// command that needs the owning group (delete, move, group) would give up on
	// exactly the rows the user has most of. The root is that group.
	ibValueFilterGroup* GetOwnerGroup(const ibDataViewItem& item) const;
	// The group a new row should go into: the selected group itself, or the
	// parent of the selected condition, or the root when nothing is selected.
	ibValueFilterGroup* GetTargetGroup(const ibDataViewItem& item) const;

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
	// One row object per value, kept alive by the model so a parent pointer
	// stays valid and so the same value keeps the same row across refreshes.
	ibFilterTreeNode* NodeFor(const ibValue& value, ibFilterTreeNode* parent) const;
	// THE ROOT IS A ROW LIKE ANY OTHER, and differs in exactly two ways — it says
	// it is the filter, and it cannot be switched off. Asked in one place so those
	// two do not drift into three.
	bool IsRootGroup(const ibValueFilterGroup* group) const {
		return group != nullptr && group == static_cast<ibValueFilterGroup*>(m_root);
	}

	ibValuePtr<ibValueFilterGroup> m_root;
	// Keyed by the VALUE OBJECT the row stands for (ibValue::GetRef), not by a
	// position and not by the address of some ibValue holding it: a line that
	// moves — grouping, drag, ungroup — must keep its row, or the selection
	// jumps to whatever now sits where it used to be.
	mutable std::map<const void*, wxObjectDataPtr<ibFilterTreeNode>> m_nodes;
	mutable std::vector<wxObjectDataPtr<ibFilterTreeNode>> m_owned;
};

#endif
