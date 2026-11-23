#include "backend/composition/drivers/compositionColumnLayout.h"

#include <algorithm>   // std::max

// THE TREE MEASURED — built for one Rebuild and thrown away, as the table's is. The ROOT is a group like any other
// (side by side, no title of its own), measured and placed by the same two calls as everything under it.
struct ibCompositionColumnLayout::ibLayoutNode
{
	const ibCompositionColumnNode* source = nullptr;   // nullptr = the root
	std::vector<ibLayoutNode> children;

	int width = 1;         // sheet columns
	int bodyBands = 1;     // sheet rows of a printed line
	int headerDepth = 1;   // sheet rows of the header

	bool IsLeaf() const { return source != nullptr && !source->IsGroup(); }
	ibColumnGroupKind Kind() const { return source != nullptr ? source->m_placement : ibColumnGroupHorizontal; }
};

void ibCompositionColumnLayout::Rebuild(const std::vector<ibCompositionColumnNode>& tree, int firstColumn, int headerBands)
{
	m_body.clear();
	m_header.clear();
	m_rowBands = 1;
	m_headerBands = std::max(headerBands, 1);
	m_columnCount = 0;

	ibLayoutNode root;
	for (const ibCompositionColumnNode& node : tree) {
		ibLayoutNode child;
		child.source = &node;
		root.children.push_back(child);
	}
	if (root.children.empty())
		return;

	// A group's own members are taken in by MeasureNode — the tree under it is the source's, read where it stands.
	MeasureNode(root);
	m_rowBands = std::max(root.bodyBands, 1);
	m_headerBands = std::max(root.headerDepth, m_headerBands);
	m_columnCount = root.width;

	PlaceNode(root, firstColumn, root.width, 0, m_rowBands, 0, m_headerBands, false);
}

void ibCompositionColumnLayout::MeasureNode(ibLayoutNode& node)
{
	if (node.IsLeaf()) {
		node.width = 1;
		node.bodyBands = 1;
		node.headerDepth = 1;
		return;
	}

	// The root's children are in already; a group's are taken from the tree it stands for.
	if (node.source != nullptr && node.children.empty())
		for (const ibCompositionColumnNode& member : node.source->m_children) {
			ibLayoutNode child;
			child.source = &member;
			node.children.push_back(child);
		}

	const bool vertical = node.Kind() == ibColumnGroupVertical;
	int width = 0, bands = 0, childHeaderDepth = 0;
	for (ibLayoutNode& child : node.children) {
		MeasureNode(child);
		// One under another: the widest sets the width, the heights add up — and so do the titles, each in a band of
		// its own. Side by side (horizontal AND in-cell): the widths add up, the tallest sets the height, and the
		// titles share the bands below the group's own.
		width            = vertical ? std::max(width, child.width) : width + child.width;
		bands            = vertical ? bands + child.bodyBands : std::max(bands, child.bodyBands);
		childHeaderDepth = vertical ? childHeaderDepth + child.headerDepth : std::max(childHeaderDepth, child.headerDepth);
	}
	node.width = std::max(width, 1);
	node.bodyBands = std::max(bands, 1);

	// An in-cell group IS one header cell, whatever is under it; any other group takes a band only when it has a
	// title — untitled, it groups the columns without costing the header any depth.
	node.headerDepth = OwnsHeaderCell(node)
		? (node.Kind() == ibColumnGroupInCell ? 1 : std::max(childHeaderDepth, 1) + 1)
		: std::max(childHeaderDepth, 1);
}

// DOES THIS GROUP DRAW A CELL OF ITS OWN? The root never does — it is the line, not a grouping. Below it, an in-cell
// group always draws its one merged cell (that IS the merge), any other group only when it has a title.
bool ibCompositionColumnLayout::OwnsHeaderCell(const ibLayoutNode& node)
{
	if (node.source == nullptr || node.IsLeaf())
		return false;
	return node.Kind() == ibColumnGroupInCell || !node.source->m_title.IsEmpty();
}

void ibCompositionColumnLayout::PlaceNode(const ibLayoutNode& node, int col, int width, int band, int bandSpan,
	int headerBand, int headerSpan, bool merged)
{
	if (node.IsLeaf()) {
		ibCompositionColumnPlacement body;
		body.m_column   = node.source->m_column;
		body.m_col      = col;
		body.m_colSpan  = std::max(width, 1);
		body.m_band     = band;
		body.m_bandSpan = std::max(bandSpan, 1);
		m_body.push_back(body);

		// Under an in-cell group the group's one cell is the title; the column has none of its own. Otherwise its
		// title fills the slice it was given — the rest of the header for a plain column, one band in a stack.
		if (merged)
			return;
		ibCompositionHeaderCell cell;
		cell.m_column   = node.source->m_column;
		cell.m_col      = col;
		cell.m_colSpan  = body.m_colSpan;
		cell.m_band     = headerBand;
		cell.m_bandSpan = std::max(headerSpan, 1);
		m_header.push_back(cell);
		return;
	}

	const bool inCell = node.Kind() == ibColumnGroupInCell;
	const bool ownsCell = OwnsHeaderCell(node);
	if (!merged && ownsCell) {
		ibCompositionHeaderCell cell;
		cell.m_title    = node.source->m_title;
		cell.m_col      = col;
		cell.m_colSpan  = std::max(width, 1);
		cell.m_band     = headerBand;
		cell.m_bandSpan = inCell ? std::max(headerSpan, 1) : 1;
		m_header.push_back(cell);
	}
	merged = merged || inCell;   // everything under an in-cell group is merged, however deep

	const size_t count = node.children.size();
	if (count == 0)
		return;

	// A group that draws no cell costs the header nothing: its children start where it starts.
	const bool takesBand = ownsCell && !inCell;
	const int childHeaderBand = takesBand ? headerBand + 1 : headerBand;
	const int childHeaderSpan = takesBand ? std::max(headerSpan - 1, 1) : headerSpan;

	if (node.Kind() == ibColumnGroupVertical) {
		// Stacked: each takes the group's whole width, the LAST takes what height is left — of the line and of the
		// header alike.
		int usedBands = 0, usedHeaderBands = 0;
		for (size_t i = 0; i < count; ++i) {
			const ibLayoutNode& child = node.children[i];
			const bool lastOne = i + 1 == count;
			const int childBands = lastOne ? std::max(bandSpan - usedBands, 1) : child.bodyBands;
			const int childHeader = lastOne ? std::max(childHeaderSpan - usedHeaderBands, 1) : child.headerDepth;
			PlaceNode(child, col, width, band + usedBands, childBands,
				childHeaderBand + usedHeaderBands, childHeader, merged);
			usedBands += childBands;
			usedHeaderBands += childHeader;
		}
		return;
	}

	// Side by side: the LAST takes what width is left, and they share the bands below the group's title.
	int usedWidth = 0;
	for (size_t i = 0; i < count; ++i) {
		const ibLayoutNode& child = node.children[i];
		const bool lastOne = i + 1 == count;
		const int childWidth = lastOne ? std::max(width - usedWidth, 1) : child.width;
		PlaceNode(child, col + usedWidth, childWidth, band, bandSpan, childHeaderBand, childHeaderSpan, merged);
		// Inside a merged group the columns are ONE cell: no rule between them. The child was just placed.
		if (merged && !lastOne && child.IsLeaf() && !m_body.empty())
			m_body.back().m_mergedRight = true;
		usedWidth += childWidth;
	}
}
