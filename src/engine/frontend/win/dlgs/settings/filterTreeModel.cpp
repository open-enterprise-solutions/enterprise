#include "filterTreeModel.h"

#include "backend/system/value/valueType.h"   // ibValueTypeDescription::AdjustValue — typed text lands as its type
#include "backend/system/value/composition/valueComposerSettings.h"   // the pickers a cell offers

// ===========================================================================
//  ibFilterTreeNode — a row is a PATH; resolving it is walking that path
// ===========================================================================

ibFilterNodeDescription* ibFilterTreeNode::Resolve() const
{
	if (m_filter == nullptr || m_path.empty())
		return nullptr;   // the root stands for no node of its own

	std::vector<ibFilterNodeDescription>* level = &m_filter->m_nodes;
	ibFilterNodeDescription* node = nullptr;
	for (const size_t idx : m_path) {
		if (level == nullptr || idx >= level->size())
			return nullptr;   // the path no longer leads anywhere — deleted under us
		node = &(*level)[idx];
		level = node->m_kind == ibFilterNodeKind_Group ? &node->m_children : nullptr;
	}
	return node;
}

std::vector<ibFilterNodeDescription>* ibFilterTreeNode::Children() const
{
	if (m_filter == nullptr)
		return nullptr;
	if (m_path.empty())
		return &m_filter->m_nodes;   // THE ROOT'S children are the description's top level

	ibFilterNodeDescription* node = Resolve();
	return (node != nullptr && node->m_kind == ibFilterNodeKind_Group) ? &node->m_children : nullptr;
}

// ===========================================================================
//  ibFilterTreeModel — see filterTreeModel.h
// ===========================================================================

wxString ibFilterTreeModel::KeyOf(const ibFilterPath& path)
{
	// The path, spelled — a map key that orders and compares as the chain it is.
	wxString key;
	for (const size_t idx : path)
		key << idx << wxT('.');
	return key;
}

void ibFilterTreeModel::SetFilter(ibFilterDescription* filter)
{
	// A DIFFERENT TREE. The row objects were read against the old one — drop them,
	// or a row resolves its path in a filter it never belonged to. Re-reading the
	// SAME tree after an edit is Refresh(), which keeps them.
	if (m_filter != filter) {
		m_nodes.clear();
		m_owned.clear();
	}
	m_filter = filter;
	Cleared();
}

void ibFilterTreeModel::Refresh()
{
	Cleared();
}

ibDataViewItem ibFilterTreeModel::ItemFor(const ibFilterPath& path) const
{
	auto found = m_nodes.find(KeyOf(path));
	return found != m_nodes.end() ? ibDataViewItem(found->second.get()) : ibDataViewItem();
}

ibDataViewItem ibFilterTreeModel::RootItem() const
{
	if (m_filter == nullptr)
		return ibDataViewItem();
	// CREATED, not looked up: the dialog opens the tree on this row before the view
	// has fetched anything, and ItemFor answers only for rows that already exist.
	ibFilterTreeNode* node = NodeFor(ibFilterPath(), nullptr);
	return node != nullptr ? ibDataViewItem(node) : ibDataViewItem();
}

ibFilterTreeNode* ibFilterTreeModel::NodeFor(const ibFilterPath& path, ibFilterTreeNode* parent) const
{
	const wxString key = KeyOf(path);
	auto found = m_nodes.find(key);
	if (found != m_nodes.end()) {
		// It may hang somewhere else now — grouping and ungrouping move rows.
		found->second->SetParent(parent);
		found->second->SetFilter(m_filter);
		return found->second.get();
	}

	wxObjectDataPtr<ibFilterTreeNode> node(new ibFilterTreeNode(m_filter, path, parent));
	m_owned.push_back(node);
	m_nodes[key] = node;
	return node.get();
}

ibFilterNodeDescription* ibFilterTreeModel::GetNode(const ibDataViewItem& item) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	return node != nullptr ? node->Resolve() : nullptr;
}

ibFilterPath ibFilterTreeModel::GetPath(const ibDataViewItem& item) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	return node != nullptr ? node->GetPath() : ibFilterPath();
}

std::vector<ibFilterNodeDescription>* ibFilterTreeModel::GetOwnerChildren(const ibDataViewItem& item) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr || node->IsRoot())
		return nullptr;   // THE ROOT IS OWNED BY NOTHING — undeletable, unmovable, by construction
	const ibFilterTreeNode* parent = static_cast<const ibFilterTreeNode*>(node->GetParentItem().GetID());
	return parent != nullptr ? parent->Children() : (m_filter != nullptr ? &m_filter->m_nodes : nullptr);
}

std::vector<ibFilterNodeDescription>* ibFilterTreeModel::GetTargetChildren(const ibDataViewItem& item) const
{
	// A new line goes INTO the selected group, or NEXT TO the selected condition
	// (that is, into its owner's list). Nothing selected — into the top level,
	// which is what the user sees as the "Filter" line.
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node != nullptr) {
		if (std::vector<ibFilterNodeDescription>* own = node->Children())
			return own;
		if (std::vector<ibFilterNodeDescription>* owner = GetOwnerChildren(item))
			return owner;
	}
	return m_filter != nullptr ? &m_filter->m_nodes : nullptr;
}

ibFilterPath ibFilterTreeModel::GetTargetPath(const ibDataViewItem& item) const
{
	// The same three answers GetTargetChildren gives, said as a path: the selected
	// group itself, its owner, or the root. Said in one place beside the other so
	// the two cannot answer about different groups.
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr)
		return ibFilterPath();
	if (node->Children() != nullptr)
		return node->GetPath();          // a group (the root included) takes it directly
	ibFilterPath path = node->GetPath();
	if (!path.empty())
		path.pop_back();                 // a condition — its owner
	return path;
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
	if (node == nullptr)
		return true;
	if (!node->IsRoot()) {
		const ibFilterNodeDescription* line = node->Resolve();
		if (line == nullptr || line->m_kind != ibFilterNodeKind_Group)
			return true;   // a condition fills every column
		// A GROUP is a Use box and an operator, and nothing else: no comparison, no
		// right-hand side, no display mode.
		return col == kFilterColUse || col == kFilterColLeft;
	}

	// THE ROOT HAS NO USE BOX — clearing it switches the WHOLE filter off, which is
	// a rubber-band trap: one unnoticed click and every condition below still reads
	// as set while nothing filters.
	return col == kFilterColLeft;
}

void ibFilterTreeModel::GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr || m_filter == nullptr)
		return;

	// THE ROOT SAYS WHAT IT IS: it is the filter itself, and a bare "And" at the top
	// of the tree names an operator without saying what it operates on.
	if (node->IsRoot()) {
		if (col == kFilterColLeft)
			variant = wxString::Format(wxT("%s (%s)"), _("Filter"),
				ibValue::CreateEnumObject<ibValueEnumFilterGroupKind>(m_filter->m_rootKind).GetString());
		return;
	}

	const ibFilterNodeDescription* line = node->Resolve();
	if (line == nullptr)
		return;

	if (line->m_kind == ibFilterNodeKind_Group) {
		switch (col) {
		case kFilterColUse:  variant = line->m_use; break;
		// The operator, as the text the user reads AND edits (the renderer opens
		// the And / Or / Not choice on this cell).
		case kFilterColLeft:
			variant = ibValue::CreateEnumObject<ibValueEnumFilterGroupKind>(line->m_groupKind).GetString();
			break;
		default: break;
		}
		return;
	}

	switch (col) {
	case kFilterColUse:  variant = line->m_use; break;
	// EVERY CELL SHOWS ITS SIDE'S OWN TEXT. A field reads as its presentation, a
	// number as a number, an enumeration member as its caption — one rule, so
	// `Price > Cost`, `Amount > 100` and the comparison between them all read as
	// what they are.
	case kFilterColLeft:
		variant = line->m_left.IsField()
			? (line->m_left.m_presentation.IsEmpty() ? line->m_left.m_path : line->m_left.m_presentation)
			: line->m_left.m_value.GetString();
		break;
	case kFilterColRight:
		variant = line->m_right.IsField()
			? (line->m_right.m_presentation.IsEmpty() ? line->m_right.m_path : line->m_right.m_presentation)
			: line->m_right.m_value.GetString();
		break;
	case kFilterColComparison:
		variant = ibValue::CreateEnumObject<ibValueEnumComparisonKind>(line->m_comparison).GetString();
		break;
	case kFilterColDisplayMode:
		variant = ibValue::CreateEnumObject<ibValueEnumFilterDisplayMode>(line->m_display).GetString();
		break;
	case kFilterColPresentation: variant = line->m_presentation; break;
	default: break;
	}
}

bool ibFilterTreeModel::SetValue(const wxVariant& variant, const ibDataViewItem& item, unsigned int col)
{
	const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
	if (node == nullptr || node->IsRoot())
		// The root has no Use box (HasValue); refusing the write too means a stray
		// column-wide toggle cannot switch the whole filter off behind the grid.
		return false;

	ibFilterNodeDescription* line = node->Resolve();
	if (line == nullptr)
		return false;

	if (line->m_kind == ibFilterNodeKind_Group) {
		// THE OPERATOR IS SET BY ITS OWN EDITOR (the And / Or / Not drop-down the
		// left cell opens), not through here: the list of operators belongs to that
		// drop-down, and routing the choice back as text would mean matching
		// translated strings to put the kind back.
		if (col == kFilterColUse) {
			line->m_use = variant.GetBool();
			return true;
		}
		return false;
	}

	switch (col) {
	case kFilterColUse: line->m_use = variant.GetBool(); return true;
	case kFilterColPresentation: line->m_presentation = variant.GetString(); return true;
	// TYPED-IN TEXT ON THE RIGHT is a legitimate way to fill a simple value (a
	// number, a string, a date): the cell allows typing for exactly those, and
	// refusing it here is what made the right-hand side impossible to change by
	// hand. The text is adjusted to the type the LEFT field lends before it lands,
	// so "12" becomes a number and a date becomes a date.
	case kFilterColRight:
		line->m_right.m_path.clear();   // typing a value is choosing a value, not a field
		line->m_right.m_value = ibValueTypeDescription::AdjustValue(
			line->m_left.m_type, ibValue(variant.GetString()));
		return true;
	// The comparison and the display mode are written by their own editor — the
	// quick choice over their enumeration — not as text through here.
	// THE TWO SIDES ARE NOT EDITED AS TEXT otherwise. They are a field picked from
	// the source tree, or a value entered through the runtime's own editor for its
	// type (quick choice for a reference, a two-item drop-down for a boolean). The
	// dialog opens that editor and writes the side back itself.
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
		if (m_filter == nullptr)
			return 0;
		if (ibFilterTreeNode* rootNode = NodeFor(ibFilterPath(), nullptr)) {
			out.Add(ibDataViewItem(rootNode));
			return 1;
		}
		return 0;
	}

	const std::vector<ibFilterNodeDescription>* children = parentNode->Children();
	if (children == nullptr)
		return 0;   // a condition has no children

	// NOT PAGED, deliberately: a filter is written by a person, so it is tens of
	// lines at most. Paging here would buy nothing and cost the tree its identity
	// across fetches.
	unsigned int added = 0;
	for (size_t i = 0; i < children->size(); ++i) {
		// ⭐ APPLIED, NEVER SHOWN — the whole meaning of `Inaccessible`, and the one place it is
		// asked. The line stays in the filter and runs; a READER simply does not see it. The
		// designer does, because that window is where it was written (2026-08-24: before this the
		// mode was authored, serialised and consulted by nothing at all).
		if (!m_authoring && (*children)[i].m_display == ibFilterDisplayMode_Inaccessible)
			continue;
		ibFilterPath path = parentNode->GetPath();
		path.push_back(i);
		if (ibFilterTreeNode* child = NodeFor(path, parentNode)) {
			out.Add(ibDataViewItem(child));
			++added;
		}
	}
	return added;
}
