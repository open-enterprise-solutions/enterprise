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

// ⭐ WHAT A NODE IS — a HEADING or a ROW. The fold produces both: every level of the BY list makes
// headings, and a level with NO FIELDS makes one node per source row (a detail record IS an empty
// grouping). A reader lays the two out differently — a heading carries a group key and its
// totals, a row carries the values it was read with — so the node SAYS which it is instead of
// leaving the reader to infer it from the depth, which is exactly what a depth cannot answer once
// the tree has both.
enum class ibSelectorNodeKind { Group, Detail };

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
		ibSelectorNodeKind                 m_kind = ibSelectorNodeKind::Group;

		Node* AddChild(int level) { m_children.push_back(std::make_unique<Node>()); m_children.back()->m_level = level; return m_children.back().get(); }

		// ⭐ A CHILD AT A POSITION — what a CROSS-TABLE needs and an ordinary report never asks for.
		// A heading in a table stands over two different kinds of children: the cells that read
		// ACROSS it and the headings that read UNDER it. A pre-order walk is the only thing that
		// says which heading a cell belongs to, so the cells have to come first — and they are
		// discovered as the rows arrive, interleaved with the sub-headings. Inserting keeps the two
		// blocks apart without a second list on the node or a second question for the walk.
		Node* InsertChild(std::size_t at, int level) {
			if (at > m_children.size())
				at = m_children.size();
			auto it = m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(at), std::make_unique<Node>());
			(*it)->m_level = level;
			return it->get();
		}
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

	// ⭐ A DEEP COPY, because the tree is MOVE-ONLY and a folded tree may have more than one reader.
	//
	// A server-folded tree is handed to the selector as a SHARED one (ibDataQueryResult keeps it and
	// every Select() hands over the same shared_ptr), so a selector that MOVED out of it left the
	// next reader an empty tree: the second `Select()` — a script reading a result twice, a drill
	// asking for a node's children — walked nothing and the report came back blank.
	//
	// Copying is what a second reader needs and the cost is the SHAPE, not the data: a folded tree
	// is groups, and the rows it was folded from are not in it.
	ibSelectorTree Clone() const
	{
		ibSelectorTree copy;
		copy.m_columns = m_columns;
		CopyNode(m_root, copy.m_root);
		return copy;
	}

	// ⭐ A SUBTREE AS A TREE OF ITS OWN — the given node becomes the ROOT, so its children are the
	// level a walk over it visits. This is what a node's sub-selection is: the children are already
	// folded and in hand, and re-running a query to fetch what the tree already holds would be a
	// second answer to a question already answered (and, for a fold with no live source behind it —
	// a scripted TOTALS — no answer at all, which is how a descent came back empty).
	//
	// The node keeps its own level, so Level() stays ABSOLUTE across a descent: a walk two levels
	// down still reports 2, not 0.
	ibSelectorTree SubtreeOf(const Node& node) const
	{
		ibSelectorTree copy;
		copy.m_columns = m_columns;
		CopyNode(node, copy.m_root);
		return copy;
	}

	// The columns the nodes carry (names/types for rendering) — the source snapshot's columns plus
	// the aggregate columns the fold adds.
	void AddColumn(ibMetaID id, const wxString& name, const ibTypeDescription& type) { m_columns.push_back({ id, name, type }); }
	const std::vector<ibQueryRamColumn>& Columns() const { return m_columns; }

private:
	// One node and everything under it. Recursion follows the tree's own depth — the levels of a
	// grouping, never the number of rows.
	static void CopyNode(const Node& from, Node& to)
	{
		to.m_values      = from.m_values;
		to.m_level       = from.m_level;
		to.m_hasChildren = from.m_hasChildren;
		to.m_kind        = from.m_kind;
		to.m_children.clear();
		to.m_children.reserve(from.m_children.size());
		for (const std::unique_ptr<Node>& child : from.m_children) {
			if (child == nullptr)
				continue;
			to.m_children.push_back(std::make_unique<Node>());
			CopyNode(*child, *to.m_children.back());
		}
	}

	std::vector<ibQueryRamColumn> m_columns;
	Node                          m_root;   // grand-total / invisible root
};

#endif // __QUERY_SELECTOR_TREE_H__
