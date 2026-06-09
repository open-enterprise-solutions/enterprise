#ifndef __QUERY_SELECTOR_TREE_H__
#define __QUERY_SELECTOR_TREE_H__

// ibSelectorTree — the NODE TREE a Selector folds from a flat snapshot. The snapshot
// (ibQueryRamTable) is a pure flat array built for fast extraction; SPLITTING it into folders /
// subtotal levels is the Selector's job, and the product is THIS: a tree of nodes, each carrying
// its cell values (group-key path + rolled aggregates) and its child nodes. The snapshot has NO
// tree; the tree has NO flat rows. L3 names no runtime type but ibValue — turning the tree into a
// runtime model is the RUNTIME's job. (docs/query-language-arc.md §22.1b)

#include "queryRamTable.h"   // ibQueryRamColumn + ibValue + ibMetaID + ibTypeDescription (the snapshot it folds from)

#include <map>
#include <vector>
#include <memory>

class BACKEND_API ibSelectorTree
{
public:
	using Row = std::map<ibMetaID, ibValue>;   // a node's values, keyed by column model id

	// A tree node: its values (group-key path + aggregates), its level (0 = root / grand total),
	// its child nodes, and whether it is EXPANDABLE — set from the data even before children load
	// (so the GUI shows the expand triangle ahead of a lazy sub-selection).
	struct Node
	{
		Row                                m_values;
		int                                m_level = 0;
		std::vector<std::unique_ptr<Node>> m_children;
		bool                               m_hasChildren = false;

		Node* AddChild(int level) { m_children.push_back(std::make_unique<Node>()); m_children.back()->m_level = level; return m_children.back().get(); }
	};

	// Move-only (owns nodes via unique_ptr) — same rationale as ibQueryRamTable: the BACKEND_API
	// export must not instantiate the ill-formed implicit copy ops through unique_ptr<Node>.
	ibSelectorTree() = default;
	ibSelectorTree(ibSelectorTree&&) = default;
	ibSelectorTree& operator=(ibSelectorTree&&) = default;
	ibSelectorTree(const ibSelectorTree&) = delete;
	ibSelectorTree& operator=(const ibSelectorTree&) = delete;

	Node&       Root()       { return m_root; }
	const Node& Root() const { return m_root; }

	// The columns the nodes carry (names/types for rendering) — the source snapshot's columns plus
	// the aggregate columns the fold adds.
	void AddColumn(ibMetaID id, const wxString& name, const ibTypeDescription& type) { m_columns.push_back({ id, name, type }); }
	const std::vector<ibQueryRamColumn>& Columns() const { return m_columns; }

private:
	std::vector<ibQueryRamColumn> m_columns;
	Node                          m_root;   // grand-total / invisible root
};

#endif // __QUERY_SELECTOR_TREE_H__
