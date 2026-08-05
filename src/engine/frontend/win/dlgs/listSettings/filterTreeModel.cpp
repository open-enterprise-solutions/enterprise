#include "filterTreeModel.h"

#include "backend/system/value/valueType.h"   // ibValueTypeDescription::AdjustValue — typed text lands as its type

// ===========================================================================
//  ibFilterTreeModel — see filterTreeModel.h
// ===========================================================================

void ibFilterTreeModel::SetRoot(ibValueFilterGroup* root)
{
	// A DIFFERENT TREE. The row objects belonged to the old one — drop them, or a
	// stale parent pointer outlives what it pointed at. Re-reading the SAME tree
	// after an edit is Refresh(), which keeps them.
	if (m_root != root) {
		m_nodes.clear();
		m_owned.clear();
	}
	m_root = root;
	// THE ROOT IS ALWAYS IN USE. Its box is gone from the grid, so a filter saved
	// while the root happened to be off would be a filter that shows every
	// condition and applies none, with nothing on screen to explain it.
	if (m_root)
		m_root->SetUse(true);
	Cleared();
}

void ibFilterTreeModel::Refresh()
{
	Cleared();
}

ibDataViewItem ibFilterTreeModel::ItemFor(const ibValue& value) const
{
	const void* key = value.GetRef();
	if (key == nullptr)
		return ibDataViewItem();
	auto found = m_nodes.find(key);
	return found != m_nodes.end() ? ibDataViewItem(found->second.get()) : ibDataViewItem();
}

ibDataViewItem ibFilterTreeModel::RootItem() const
{
	if (!m_root)
		return ibDataViewItem();
	// CREATED, not looked up: the dialog opens the tree on this row before the view
	// has fetched anything, and ItemFor answers only for rows that already exist.
	ibFilterTreeNode* node = NodeFor(ibValue(m_root), nullptr);
	return node != nullptr ? ibDataViewItem(node) : ibDataViewItem();
}

ibFilterTreeNode* ibFilterTreeModel::NodeFor(const ibValue& value, ibFilterTreeNode* parent) const
{
	// KEYED BY THE VALUE OBJECT, not by position: a row that moves (grouping,
	// drag) keeps its identity, so selection and expansion survive the move.
	const void* key = value.GetRef();
	if (key == nullptr)
		return nullptr;
	auto found = m_nodes.find(key);
	if (found != m_nodes.end()) {
		// It may hang somewhere else now — grouping and ungrouping move rows.
		found->second->SetParent(parent);
		return found->second.get();
	}

	wxObjectDataPtr<ibFilterTreeNode> node(new ibFilterTreeNode(value, parent));
	m_owned.push_back(node);
	m_nodes[key] = node;
	return node.get();
}

ibValueFilterItem* ibFilterTreeModel::GetItem(const ibDataViewItem& item) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	return node != nullptr ? node->GetItem() : nullptr;
}

ibValueFilterGroup* ibFilterTreeModel::GetGroup(const ibDataViewItem& item) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	return node != nullptr ? node->GetGroup() : nullptr;
}

ibValueFilterGroup* ibFilterTreeModel::GetOwnerGroup(const ibDataViewItem& item) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr)
		return nullptr;
	// THE ROOT IS OWNED BY NOTHING, and saying so is what makes it undeletable and
	// unmovable without a second rule anywhere: every command that needs an owning
	// group gives up on it, and only on it. Every other row now HAS a parent row —
	// the root is visible — so this is a plain walk up, not a special case.
	return GetGroup(node->GetParentItem());
}

ibValueFilterGroup* ibFilterTreeModel::GetTargetGroup(const ibDataViewItem& item) const
{
	// A new line goes INTO the selected group, or NEXT TO the selected condition
	// (that is, into its parent). Nothing selected — into the root, which is what
	// the user sees as the "Filter" line.
	if (ibValueFilterGroup* group = GetGroup(item))
		return group;

	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node != nullptr) {
		const ibDataViewItem parent = node->GetParentItem();
		if (ibValueFilterGroup* parentGroup = GetGroup(parent))
			return parentGroup;
	}
	return m_root;
}

ibDataViewItem ibFilterTreeModel::GetParent(const ibDataViewItem& item) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	return node != nullptr ? node->GetParentItem() : ibDataViewItem();
}

bool ibFilterTreeModel::IsContainer(const ibDataViewItem& item) const
{
	// The invisible root is a container, or the first fetch never happens.
	if (!item.IsOk())
		return true;
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	return node != nullptr && node->IsContainer();
}

bool ibFilterTreeModel::HasValue(const ibDataViewItem& item, unsigned col) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr || node->GetGroup() == nullptr)
		return true;   // a condition fills every column

	// A GROUP is a Use box and an operator, and nothing else: no comparison, no
	// right-hand side, no display mode. THE ROOT HAS NO USE BOX — clearing it
	// switches the WHOLE filter off (BuildFilterGroup returns nothing for a group
	// that is not in use), which is a rubber-band trap: one unnoticed click and
	// every condition below still reads as set while nothing filters.
	if (IsRootGroup(node->GetGroup()))
		return col == kFilterColLeft;
	return col == kFilterColUse || col == kFilterColLeft;
}

void ibFilterTreeModel::GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr)
		return;

	if (const ibValueFilterGroup* group = node->GetGroup()) {
		switch (col) {
		case kFilterColUse:  variant = group->GetUse(); break;
		// The operator, as the text the user reads AND edits (the renderer opens
		// the And / Or / Not choice on this cell). THE ROOT SAYS WHAT IT IS as
		// well: it is the filter itself, and a bare "And" at the top of the tree
		// names an operator without saying what it operates on.
		case kFilterColLeft:
			variant = IsRootGroup(group)
				? wxString::Format(wxT("%s (%s)"), _("Filter"), group->GetString())
				: group->GetString();
			break;
		default: break;
		}
		return;
	}

	const ibValueFilterItem* line = node->GetItem();
	if (line == nullptr)
		return;

	switch (col) {
	case kFilterColUse:  variant = line->GetUse(); break;
	// EVERY CELL SHOWS ITS VALUE'S OWN TEXT. A field reads as its presentation, a
	// number as a number, an enumeration member as its caption — one rule, so
	// `Price > Cost`, `Amount > 100` and the comparison between them are all just
	// values presenting themselves.
	case kFilterColLeft:  variant = line->GetLeft().GetString(); break;
	case kFilterColRight: variant = line->GetRight().GetString(); break;
	case kFilterColComparison:
		variant = ibValue::CreateEnumObject<ibValueEnumComparisonKind>(line->GetComparison()).GetString();
		break;
	case kFilterColDisplayMode:
		variant = ibValue::CreateEnumObject<ibValueEnumFilterDisplayMode>(line->GetDisplayMode()).GetString();
		break;
	case kFilterColPresentation: variant = line->GetPresentation(); break;
	default: break;
	}
}

bool ibFilterTreeModel::SetValue(const wxVariant& variant, const ibDataViewItem& item, unsigned int col)
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr)
		return false;

	if (ibValueFilterGroup* group = node->GetGroup()) {
		// THE OPERATOR IS SET BY ITS OWN EDITOR (the And / Or / Not drop-down the
		// left cell opens), not through here: the list of operators belongs to that
		// drop-down, and routing the choice back as text would mean matching
		// translated strings to put the kind back.
		// The root has no Use box (HasValue); refusing the write too means a stray
		// column-wide toggle cannot switch the whole filter off behind the grid.
		if (col == kFilterColUse && !IsRootGroup(group)) {
			group->SetUse(variant.GetBool());
			return true;
		}
		return false;
	}

	ibValueFilterItem* line = node->GetItem();
	if (line == nullptr)
		return false;

	switch (col) {
	case kFilterColUse: line->SetUse(variant.GetBool()); return true;
	case kFilterColPresentation: line->SetPresentation(variant.GetString()); return true;
	// TYPED-IN TEXT ON THE RIGHT is a legitimate way to fill a simple value (a
	// number, a string, a date): the cell allows typing for exactly those, and
	// refusing it here is what made the right-hand side impossible to change by
	// hand. The text is adjusted to the type the LEFT field lends before it lands,
	// so "12" becomes a number and a date becomes a date.
	case kFilterColRight:
		line->SetRight(ibValueTypeDescription::AdjustValue(
			line->GetRightTypeDescription(), ibValue(variant.GetString())));
		return true;
	// The comparison and the display mode are written by their own editor — the
	// quick choice over their enumeration — not as text through here.
	// THE TWO SIDES ARE NOT EDITED AS TEXT. They are values — a field picked from
	// the source tree, or a value entered through the runtime's own editor for
	// its type (quick choice for a reference, a two-item drop-down for a
	// boolean). The dialog opens that editor and writes the value back through
	// SetLeft / SetRight; typing into the cell would only produce a string that
	// happens to look like one.
	default: return false;
	}
}

unsigned int ibFilterTreeModel::GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& /*anchor*/,
	int /*count*/, ibDataViewItemArray& out) const
{
	// THE ROOT GROUP IS A ROW LIKE ANY OTHER. It was invisible once, and its
	// operator went with it: the top level is joined by And / Or / Not exactly as
	// a nested group is, but with nowhere to show it, "a AND b" and "a OR b" looked
	// identical and only one of them was reachable. Worse, every top-level row then
	// had no parent row, which is the test the commands use for "this is the root"
	// — so Delete, Move and Group were greyed on precisely the rows a user has most
	// of. One visible root fixes the reading and the commands together.
	ibFilterTreeNode* parentNode = parent.IsOk() ? static_cast<ibFilterTreeNode*>(parent.GetID()) : nullptr;
	if (parentNode == nullptr) {
		if (!m_root)
			return 0;
		if (ibFilterTreeNode* rootNode = NodeFor(ibValue(m_root), nullptr)) {
			out.Add(ibDataViewItem(rootNode));
			return 1;
		}
		return 0;
	}

	ibValueFilterGroup* group = parentNode->GetGroup();
	if (group == nullptr)
		return 0;   // a condition has no children

	// NOT PAGED, deliberately: a filter is written by a person, so it is tens of
	// lines at most. Paging here would buy nothing and cost the tree its identity
	// across fetches.
	unsigned int added = 0;
	for (size_t i = 0; i < group->Count(); ++i) {
		if (ibFilterTreeNode* child = NodeFor(group->GetChild(i), parentNode)) {
			out.Add(ibDataViewItem(child));
			++added;
		}
	}
	return added;
}
