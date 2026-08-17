////////////////////////////////////////////////////////////////////////////
// Name:        datavlayout.cpp
// Purpose:     column groups -> the (x, band) grid the whole control paints in
// Author:      Maxim Kornienko
////////////////////////////////////////////////////////////////////////////

#include <wx/wxprec.h>

// datavgen.h is not self-contained — the vendored base classes come from dataview.h
// and it expects them already declared, exactly as its sibling .cpp files do.
#include "dataview.h"
#include "datavgen.h"

#include <algorithm>   // std::find — the width ranges are looked up by pointer



// ----------------------------------------------------------------------------
// The node tree — built for the duration of one Rebuild and thrown away.
//
// It is derived, never stored: the columns' ORDER plus each column's group
// pointer is the whole truth, so there is no second structure to keep in step
// with the first (and no way to have a column in a group that does not own it).
// ----------------------------------------------------------------------------

struct ibDataViewColumnLayout::ibLayoutNode {
	ibDataViewColumnGroup* group = nullptr;   // nullptr => leaf (a column)
	ibDataViewColumn* column = nullptr;       // leaf only
	std::vector<ibLayoutNode> children;

	// measured
	int width = 0;
	int bodyBands = 1;
	int headerDepth = 1;
};

void ibDataViewColumnLayout::Clear()
{
	m_body.clear();
	m_header.clear();
	m_headerCells.clear();

	m_rowBands = 1;
	m_headerBands = 1;
	m_totalWidth = 0;
	m_flat = true;
}

void ibDataViewColumnLayout::Rebuild(const ibDataViewColumnGroup* root)
{
	Clear();

	if (root == nullptr || root->IsHidden())
		return;

	// STRAIGHT OFF THE TREE, and the ROOT IS A GROUP LIKE ANY OTHER — measured and
	// placed by the same two calls as everything under it. There is no "top level"
	// case: whatever the root's own orientation says happens to the whole table, so a
	// vertical root would stand every record on end for free.
	ibLayoutNode rootNode;
	rootNode.group = const_cast<ibDataViewColumnGroup*>(root);
	BuildNodes(root, rootNode.children);

	if (rootNode.children.empty())
		return;

	MeasureNode(rootNode);

	m_rowBands = wxMax(rootNode.bodyBands, 1);
	m_headerBands = wxMax(rootNode.headerDepth, 1);
	m_totalWidth = rootNode.width;

	PlaceNode(rootNode, 0, rootNode.width, 0, m_rowBands, 0, m_headerBands);
}

void ibDataViewColumnLayout::BuildNodes(const ibDataViewColumnGroup* group,
	std::vector<ibLayoutNode>& out)
{
	if (group == nullptr)
		return;

	for (const ibColumnMember& member : group->GetMembers()) {

		if (member.IsColumn()) {
			if (member.column->IsHidden())
				continue;   // hidden takes no width and no band

			ibLayoutNode leaf;
			leaf.column = member.column;
			out.push_back(std::move(leaf));
			continue;
		}

		if (!member.IsGroup() || member.group->IsHidden())
			continue;       // a hidden group hides everything under it

		// A group inside a group — one switch, however deep it goes.
		m_flat = false;

		ibLayoutNode node;
		node.group = member.group;
		BuildNodes(member.group, node.children);
		out.push_back(std::move(node));
	}
}

void ibDataViewColumnLayout::MeasureNode(ibLayoutNode& node)
{
	if (node.group == nullptr) {
		node.width = wxMax(node.column->GetWidth(), 0);
		node.bodyBands = 1;
		node.headerDepth = 1;
		return;
	}

	int width = 0;
	int bands = 0;

	for (ibLayoutNode& child : node.children) {
		MeasureNode(child);

		if (node.group->GetKind() == ibColumnGroupVertical) {
			// One under another: the widest child sets the width, the heights add up.
			width = wxMax(width, child.width);
			bands += child.bodyBands;
		}
		else {
			// Side by side (horizontal AND in-cell): widths add up, the tallest child
			// sets the height. In-cell differs only in the HEADER, below.
			width += child.width;
			bands = wxMax(bands, child.bodyBands);
		}
	}

	node.width = width;
	node.bodyBands = wxMax(bands, 1);

	// HOW DEEP THE HEADER HAS TO BE, and it follows the direction the columns run in:
	//
	//   in-cell     one cell, one title — its own. Nothing under it gets a band.
	//   vertical    the columns are STACKED, so their titles stack too: the group's
	//               own band plus one per child. (Giving them a common band was the
	//               defect — every title in the stack drew over the one before it.)
	//   horizontal  the children sit side by side, so they share the bands below the
	//               group's own: as deep as the deepest of them, plus one.
	int childHeaderDepth = 0;
	for (const ibLayoutNode& child : node.children) {
		childHeaderDepth = node.group->GetKind() == ibColumnGroupVertical
			? childHeaderDepth + child.headerDepth
			: wxMax(childHeaderDepth, child.headerDepth);
	}

	// An in-cell group IS one header cell, whether or not it puts a title in it. Any
	// other group takes a band only when it SHOWS a title — untitled, it groups the
	// columns without costing the header any depth.
	node.headerDepth = OwnsHeaderCell(node)
		? (node.group->GetKind() == ibColumnGroupInCell ? 1 : wxMax(childHeaderDepth, 1) + 1)
		: wxMax(childHeaderDepth, 1);
}

// DOES THIS GROUP DRAW A CELL OF ITS OWN?
//
// The ROOT never does, whatever it is set to: it has no holder, and that is exactly
// what it means to be the store rather than a grouping — an accumulator that shows
// no title, no cell, nothing of itself, and only lets its members be seen. Below it,
// an in-cell group always draws its one merged cell (that IS the merge), and any
// other group draws one only when it shows a title.
bool ibDataViewColumnLayout::OwnsHeaderCell(const ibLayoutNode& node)
{
	if (node.group == nullptr || node.group->GetParent() == nullptr)
		return false;

	return node.group->GetKind() == ibColumnGroupInCell || node.group->IsTitleShown();
}

void ibDataViewColumnLayout::PlaceNode(ibLayoutNode& node,
	int x, int width, int band, int bandSpan, int headerBand, int headerSpan, bool merged,
	ibDataViewColumnGroup* stack, int* slotCounter)
{
	if (node.group == nullptr) {

		ibColumnPlacement body;
		body.column = node.column;
		body.x = x;
		body.width = width;
		body.band = band;
		body.bandSpan = wxMax(bandSpan, 1);
		body.stack = stack;
		// Its number among the columns of its own row in the stack — the counter is
		// restarted per row, so the first column of every row gets 0.
		if (slotCounter != nullptr)
			body.slot = (*slotCounter)++;
		m_body.push_back(body);

		// A column's title fills the slice it was GIVEN. For a plain column that is the
		// rest of the header (nothing is drawn under a column title, so a gap there
		// would just be a hole); for a column in a STACK it is one band, because the
		// titles of the columns below it live in the next ones.
		ibColumnPlacement header = body;
		header.band = headerBand;
		header.bandSpan = wxMax(headerSpan, 1);
		m_header.push_back(header);

		// MERGED into an in-cell group: the group above owns the one header cell over
		// all of them, so this column draws none of its own. Its header PLACEMENT still
		// points at that cell, so anything asking "where is this column's title" lands
		// on the merged one rather than on nothing.
		if (merged)
			return;

		ibHeaderCell cell;
		cell.place = header;
		m_headerCells.push_back(cell);
		return;
	}

	const bool inCell = node.group->GetKind() == ibColumnGroupInCell;
	const bool ownsCell = OwnsHeaderCell(node);

	// The group's own header cell spans everything under it: one band deep normally,
	// and the WHOLE slice it was given for an in-cell group — the merged cell is the
	// only title there is over those columns.
	if (!merged && ownsCell) {
		ibHeaderCell cell;
		cell.group = node.group;
		cell.place.x = x;
		cell.place.width = width;
		cell.place.band = headerBand;
		cell.place.bandSpan = inCell ? wxMax(headerSpan, 1) : 1;
		m_headerCells.push_back(cell);
	}

	// Everything under an in-cell group is merged, however deep it goes.
	merged = merged || inCell;

	const size_t count = node.children.size();
	if (count == 0)
		return;

	// A group that draws no cell of its own costs the header nothing: its children
	// start where IT starts. Otherwise its title takes the top band and the rest goes
	// to them. (A merged group shares its single cell with everything below.)
	const bool takesBand = ownsCell && !inCell;
	const int childHeaderBand = takesBand ? headerBand + 1 : headerBand;
	const int childHeaderSpan = takesBand ? wxMax(headerSpan - 1, 1) : headerSpan;

	if (node.group->GetKind() == ibColumnGroupVertical) {
		// Children stacked; each takes the group's full width. The LAST one absorbs
		// whatever height is left over so the group has no gap at its bottom.
		//
		// THEIR TITLES STACK TOO — each gets its OWN band of the header, exactly as
		// its cells get their own band of the row. One shared band meant every title
		// in the stack painted over the previous one.
		//
		// THIS is the stack its columns line up in, and every child starts a new ROW
		// of it — hence a fresh slot counter per child.
		int usedBands = 0;
		int usedHeaderBands = 0;
		for (size_t i = 0; i < count; i++) {
			ibLayoutNode& child = node.children[i];
			const bool lastOne = (i + 1 == count);
			const int childBands = lastOne
				? wxMax(bandSpan - usedBands, 1)
				: child.bodyBands;
			const int childBandsInHeader = lastOne
				? wxMax(childHeaderSpan - usedHeaderBands, 1)
				: child.headerDepth;
			int rowSlot = 0;
			PlaceNode(child, x, width, band + usedBands, childBands,
				childHeaderBand + usedHeaderBands, childBandsInHeader, merged,
				node.group, &rowSlot);
			usedBands += childBands;
			usedHeaderBands += childBandsInHeader;
		}
	}
	else {
		// Children side by side; the last one absorbs the leftover width for the
		// same reason. They share the bands below the group's own title.
		int usedWidth = 0;
		for (size_t i = 0; i < count; i++) {
			ibLayoutNode& child = node.children[i];
			const bool lastOne = (i + 1 == count);
			const int childWidth = lastOne
				? wxMax(width - usedWidth, 0)
				: child.width;
			// Side by side INSIDE a stack row — the slot counter runs on, so the pair
			// "kind + value" gets slots 0 and 1 in every row of the stack.
			PlaceNode(child, x + usedWidth, childWidth, band, bandSpan,
				childHeaderBand, childHeaderSpan, merged, stack, slotCounter);
			// Inside a merged group the columns are ONE cell: no rule between them.
			// The child has just been placed, so its entry is the last one added.
			if (merged && !lastOne && child.group == nullptr && !m_body.empty())
				m_body.back().mergedRight = true;
			usedWidth += childWidth;
		}
	}
}

bool ibDataViewColumnLayout::GetBodyPlacement(const ibDataViewColumn* column, ibColumnPlacement& out) const
{
	for (const ibColumnPlacement& place : m_body) {
		if (place.column == column) {
			out = place;
			return true;
		}
	}
	return false;
}

bool ibDataViewColumnLayout::GetHeaderPlacement(const ibDataViewColumn* column, ibColumnPlacement& out) const
{
	for (const ibColumnPlacement& place : m_header) {
		if (place.column == column) {
			out = place;
			return true;
		}
	}
	return false;
}

ibDataViewColumn* ibDataViewColumnLayout::FindColumnAt(int x, int band) const
{
	for (const ibColumnPlacement& place : m_body) {
		if (place.column == nullptr)
			continue;
		if (x >= place.x && x < place.x + place.width
			&& band >= place.band && band < place.band + place.bandSpan)
			return place.column;
	}
	return nullptr;
}

ibDataViewColumn* ibDataViewColumnLayout::FindLastColumnOf(const ibDataViewColumnGroup* group) const
{
	if (group == nullptr)
		return nullptr;

	ibDataViewColumn* found = nullptr;
	for (const ibColumnPlacement& place : m_body) {
		if (place.column == nullptr)
			continue;
		// Under this group at ANY depth — a nested group's columns are its columns too.
		for (const ibDataViewColumnGroup* node = place.column->GetParent();
			node != nullptr; node = node->GetParent()) {
			if (node == group) {
				found = place.column;
				break;
			}
		}
	}
	return found;
}

const ibHeaderCell* ibDataViewColumnLayout::FindHeaderCellAt(int x, int band) const
{
	// Deepest match wins: a column's cell sits under its group's, and the click
	// belongs to whichever is drawn at that band.
	const ibHeaderCell* found = nullptr;
	for (const ibHeaderCell& cell : m_headerCells) {
		const ibColumnPlacement& place = cell.place;
		if (x >= place.x && x < place.x + place.width
			&& band >= place.band && band < place.band + place.bandSpan) {
			if (found == nullptr || place.band >= found->place.band)
				found = &cell;
		}
	}
	return found;
}

// ----------------------------------------------------------------------------
// ibDataViewCtrl — the tree it owns, the geometry it derives
// ----------------------------------------------------------------------------

// THE TREE CHANGED — a member was taken in, moved or let go.
//
// wx and the model address columns by position, and that position MEANS "the nth column
// of the tree", so nothing is rebuilt here: what is dropped are the caches keyed by that
// position — the per-column best widths and the geometry.
void ibDataViewCtrl::WXColumnTreeChanged()
{
	InvalidateColBestWidths();
	OnColumnsCountChanged();
}

// DETACH, not delete — exactly as taking a column out is. The wx object belongs to
// whoever created it (the visual host deletes it right after Cleanup with wxDELETE), so
// freeing it here would free it twice: it crashed on closing a form.
void ibDataViewCtrl::RemoveColumnGroup(ibDataViewColumnGroup* group)
{
	if (group == nullptr)
		return;

	// WHAT IT HELD GOES TO ITS HOLDER, in place of the group — its columns and nested
	// groups still belong somewhere, and nothing may keep pointing at a group that is
	// no longer in the tree. Taking the members in ONE BY ONE is what re-parents them:
	// InsertColumn / InsertGroup take each off this group as it goes.
	ibDataViewColumnGroup* holder = group->GetParent();
	if (holder == nullptr)
		holder = m_rootGroup;

	const int where = holder->GetMemberPosition(group);
	unsigned int at = where != wxNOT_FOUND ? (unsigned int)where : holder->GetMemberCount();
	while (group->GetMemberCount() > 0) {
		const ibColumnMember member = group->GetMember(0);
		if (member.IsColumn())
			holder->InsertColumn(at, member.column);
		else if (member.IsGroup())
			holder->InsertGroup(at, member.group);
		else
			group->RemoveAllMembers();   // neither — nothing to hand over
		at++;
	}

	holder->RemoveGroup(group);
	group->SetParent(nullptr);
	InvalidateColumnLayout();
}

// Frees every GROUP hanging under `group` (not the group itself, and not the columns —
// those are handed back to the root by the caller). Depth first: a group is freed only
// once nothing it held points at it.
void ibDataViewCtrl::FreeGroupsUnder(ibDataViewColumnGroup* group)
{
	for (const ibColumnMember& member : group->GetMembers()) {
		if (!member.IsGroup())
			continue;

		FreeGroupsUnder(member.group);
		member.group->RemoveAllMembers();
		member.group->SetParent(nullptr);
		delete member.group;
	}
}

const ibDataViewColumnLayout& ibDataViewCtrl::GetColumnLayout() const
{
	if (!m_columnLayoutDirty)
		return m_columnLayout;

	// THERE IS ALWAYS A LAYOUT. Everything that paints a cell reads its rectangle from
	// here, so an empty one is not "no geometry yet" — it is a table with no columns on
	// screen at all.
	//
	// Built straight off the ROOT GROUP: order and nesting are its members', so there
	// is nothing to reconcile with the header's display-order array (indexing which,
	// mid-insert, is what used to crash) and no order to infer.
	m_columnLayout.Rebuild(m_rootGroup);
	// Cleared either way: a cell asks for its rectangle through here, so leaving the
	// flag up would rebuild the whole layout once per painted cell.
	m_columnLayoutDirty = false;

	return m_columnLayout;
}

int ibDataViewCtrl::GetBandHeight() const
{
	return m_bandHeight > 0 ? m_bandHeight : GetDefaultRowHeight();
}

// THE INVERSE OF GetColumnCellRect, and it must read the same grid: a point names a CELL,
// so it needs the band as well as the x. See the declaration for what answering by x alone
// cost — a group whose members below the first could not be reached at all.
ibDataViewColumn* ibDataViewCtrl::WXColumnAtRowPoint(int x, int y) const
{
	const ibDataViewColumnLayout& layout = GetColumnLayout();

	const unsigned int row = GetLineAt(y);

	// Which band of THIS row the point is in. The row's own height, not the nominal one:
	// with wxDV_VARIABLE_LINE_HEIGHT they differ, and then a fixed divisor names the wrong
	// band in every taller row.
	const int lineStart = GetLineStart(row);
	const int lineHeight = wxMax(GetLineHeight(row), 1);
	const int bands = wxMax(layout.GetRowBandCount(), 1);
	const int band = wxMin(wxMax(((y - lineStart) * bands) / lineHeight, 0), bands - 1);

	return layout.FindColumnAt(x, band);
}

bool ibDataViewCtrl::GetColumnCellRect(const ibDataViewColumn* column,
	int rowTop, int lineHeight, wxRect& rect) const
{
	const ibDataViewColumnLayout& layout = GetColumnLayout();

	ibColumnPlacement place;
	if (!layout.GetBodyPlacement(column, place))
		return false;

	const int bands = wxMax(layout.GetRowBandCount(), 1);
	// Divide the row we were GIVEN rather than multiplying the stored band height:
	// with wxDV_VARIABLE_LINE_HEIGHT a row can be taller than the nominal one, and
	// the bands have to add up to whatever it actually is — a leftover pixel row at
	// the bottom is a line the grid draws over nothing.
	const int bandTop = (lineHeight * place.band) / bands;
	const int bandEnd = (lineHeight * (place.band + place.bandSpan)) / bands;

	rect = wxRect(place.x, rowTop + bandTop, place.width, bandEnd - bandTop);
	return true;
}

void ibDataViewCtrl::ForEachWidthPeer(ibDataViewColumn* column,
	const std::function<void(ibDataViewColumn*)>& apply)
{
	if (column == nullptr)
		return;

	const ibDataViewColumnLayout& layout = GetColumnLayout();

	// Its place in a stack, if it is in one. The layout knows it: the nearest vertical
	// group above the column, and the column's number within its own row there.
	ibColumnPlacement own;
	if (!layout.GetBodyPlacement(column, own) || own.stack == nullptr) {
		// Not stacked — the plain case, the column alone answers for its width.
		apply(column);
		return;
	}

	// STACKED: THE COLUMNS OF ONE SLOT MOVE TOGETHER. The rows of a stack sit in the
	// same x range, so their borders have to agree — but only column-for-column: the
	// first column of every row shares one width, the second another. Widening the
	// first must not widen the second as well, which is what made a dragged column
	// run away (the row doubled, and the next drag measured from the doubled edge).
	for (const ibColumnPlacement& place : layout.GetBodyPlacements()) {
		if (place.column != nullptr && place.stack == own.stack && place.slot == own.slot)
			apply(place.column);
	}
}

// THE RANGES, LEFT TO RIGHT. Collected from the tree order, a stack counted ONCE — the
// one description of "who holds a width between them", read by the stretch and by the
// drag alike so they cannot disagree about it.
void ibDataViewCtrl::CollectWidthRanges(std::vector<ibWidthRange>& out)
{
	out.clear();

	// Tens of entries, looked up by pointer — the same choice the layout makes; a set
	// here would be a second index over the same truth.
	std::vector<ibDataViewColumn*> seen;

	const unsigned int count = GetColumnCount();
	for (unsigned int idx = 0; idx < count; idx++) {

		ibDataViewColumn* column = GetColumn(idx);
		if (column == nullptr || column->IsHidden()
			|| std::find(seen.begin(), seen.end(), column) != seen.end())
			continue;

		ibWidthRange range;
		ForEachWidthPeer(column, [&](ibDataViewColumn* peer) {
			seen.push_back(peer);
			range.columns.push_back(peer);
			range.specified = wxMax(range.specified, peer->WXGetSpecifiedWidth());

			// ⭐ HOW FAR IT MAY BE SQUEEZED: down to its TITLE, and no further.
			//
			// Shrinking is proportional like the stretch is, but a column narrowed past its
			// own heading stops saying what it holds — "Accou…" ×4 is not a table, it is a
			// hint that the table needs a scrollbar. So the title is the floor, and when the
			// floors together do not fit, that is exactly when scrolling starts.
			//
			// Never ABOVE what the column asks for, though: a column the user pulled
			// narrower than its title stays where it was put.
			// A COLUMN MAY NOT BE DRAGGED OUT OF EXISTENCE. Its own minimum is usually 0,
			// and a range that reaches 0 stops being drawn at all — that is how "Record
			// type" and "Debit account" vanished off the left while a neighbour was being
			// widened. A sliver keeps the column on screen and its border grabbable.
			range.minimum = wxMax(range.minimum,
				wxMax(peer->GetMinWidth(), FromDIP(24)));


		});

		if (!range.columns.empty())
			out.push_back(range);
	}
}

// A DRAG BEGINS — remember the picture it starts from, so every mouse-move that follows is
// measured against the same numbers (see WXApplyColumnWidth).
void ibDataViewCtrl::WXBeginColumnDrag(ibDataViewColumn* column)
{
	std::vector<ibWidthRange> ranges;
	CollectWidthRanges(ranges);

	m_dragColumn = column;
	m_dragWidths.clear();
	m_dragWidths.reserve(ranges.size());

	for (const ibWidthRange& range : ranges)
		m_dragWidths.push_back(range.columns.front()->GetWidth());

}

void ibDataViewCtrl::WXEndColumnDrag()
{
m_dragColumn = nullptr;
	m_dragWidths.clear();

	// THE SCROLLBAR IS SETTLED ONCE, HERE — it was left alone during the drag on purpose
	// (see WXIsColumnDragActive), because re-deriving it per mouse-move pulls a window
	// geometry cascade with it.
	SyncHorizontalScrollbar();
}

// A BORDER DRAGGED TO `width` ON SCREEN — and it lands there, to the pixel.
//
// FOUR RULES, IN ORDER, AND THAT IS THE WHOLE LAW:
//
//   1. the dragged range is exactly as wide as the pointer says;
//   2. nothing to its LEFT moves, so its left edge — and the row before it — stands still;
//   3. the ranges to its RIGHT take what the table has left, in proportion to the widths the
//      drag STARTED from, each floored at a sliver wide enough to see;
//   4. if even those floors do not fit, the row is wider than the table: that is the scrollbar.
//
// A FUNCTION OF WHERE THE POINTER IS, not of how it got there. Every move is computed from the
// picture the drag began with, so nothing accumulates: dragging back to the start restores the
// widths it started with, exactly, and no sequence of moves can drift. That is what the
// snapshot in WXBeginColumnDrag is for.
//
// WHAT IT REPLACED, so nobody rebuilds it: the same law expressed as changes against the
// PREVIOUS state needed a spare-room budget, four rounds of proportional collection, a clamp
// for exhausted donors and a total-restoring correction — and it still disagreed with the
// automatic fit, one handing out pixels and the other taking them back on alternate frames.
void ibDataViewCtrl::WXApplyColumnWidth(ibDataViewColumn* column, int width)
{
	if (column == nullptr || width <= 0)
		return;

	std::vector<ibWidthRange> ranges;
	CollectWidthRanges(ranges);

	size_t own = ranges.size();
	for (size_t idx = 0; idx < ranges.size() && own == ranges.size(); idx++) {
		for (ibDataViewColumn* member : ranges[idx].columns) {
			if (member == column) {
				own = idx;
				break;
			}
		}
	}

	if (own == ranges.size())
		return;

	// THE WIDTHS THE DRAG STARTED FROM, not the ones the previous mouse-move left behind.
	//
	// A move is often a single pixel and a pixel cannot be split among twelve columns, so
	// measuring each move on its own meant one column paid the whole pixel — over a 200px
	// drag, 200 columns-worth of single payers, never a proportion. Measured from the start,
	// the same drag is one 200px change shared properly; and dragging back to where it began
	// restores exactly the widths it began with, which is what made "return it and the
	// proportions are ruined" unfixable before.
	if (m_dragColumn != column || m_dragWidths.size() != ranges.size())
		WXBeginColumnDrag(column);

	std::vector<int> shown(m_dragWidths);

	const int room = GetTableAreaWidth();

	// WHAT THE RIGHT-HAND SIDE IS MEASURED AGAINST — the visible width, or the ROW'S OWN width
	// when the row is already wider than that.
	//
	// The squeeze exists to keep a row that FITS fitting: one column takes, the others give, and
	// only when they are down to slivers does the row grow past the table. But once the row is
	// wider than the table — the user made it so, and the scrollbar says as much — cramming it
	// back into the visible width is not the law's business. Measured with the viewport at 527 and
	// the row at 1456: touching any border made every range to its right collapse to its 24 px
	// sliver in one step, because "what the table has left" was −929.
	int before = 0;
	for (size_t idx = 0; idx < ranges.size(); idx++)
		before += shown[idx];

	const int target = wxMax(room, before);

	// 1. THE DRAGGED RANGE GETS EXACTLY WHAT THE POINTER ASKS FOR. Not "as much as the others
	//    can afford" — that is how a border stops following the cursor.
	shown[own] = wxMax(width, ranges[own].minimum);

	// 2. NOTHING TO THE LEFT MOVES. The pointer holds this range's RIGHT edge, so its LEFT edge
	//    must stand still; if the columns before it changed, the whole row would slide under the
	//    mouse. (Measured when the automatic fit still had a say here: the left edge alternating
	//    504 / 516 per mouse-move and the border 47 px from the pointer.)
	int left = 0;
	for (size_t idx = 0; idx < own; idx++)
		left += shown[idx];

	// 3. THE RANGES TO THE RIGHT TAKE WHAT IS LEFT OF THE TARGET, in proportion to the widths the
	//    drag started from — they shrink as it grows and grow back as it shrinks, both by the
	//    same rule. Anyone whose share falls under its floor is pinned to the floor (the "small
	//    sliver, just enough to see it is there") and the rest is shared out again, which is
	//    exact and needs no fixed number of rounds. When even the floors do not fit, the row is
	//    wider than the table — and that is precisely what the scrollbar is for.
	std::vector<size_t> pool;
	int weight = 0;
	for (size_t idx = own + 1; idx < ranges.size(); idx++) {
		pool.push_back(idx);
		weight += m_dragWidths[idx];
	}

	int budget = target - left - shown[own];

	while (!pool.empty()) {

		bool pinned = false;

		for (size_t n = 0; n < pool.size(); n++) {

			const size_t idx = pool[n];
			const int share = weight > 0
				? (int)((wxLongLong_t)budget * m_dragWidths[idx] / weight)
				: 0;

			if (share >= ranges[idx].minimum)
				continue;

			shown[idx] = ranges[idx].minimum;
			budget -= ranges[idx].minimum;
			weight -= m_dragWidths[idx];
			pool.erase(pool.begin() + n);
			pinned = true;
			break;
		}

		if (pinned)
			continue;

		// Nobody is under its floor any more: hand out the budget, the odd pixel to the last of
		// them, so the row ends exactly on the table's right edge.
		int handed = 0;
		for (size_t n = 0; n < pool.size(); n++) {

			const size_t idx = pool[n];
			const bool lastOne = (n + 1 == pool.size());
			const int share = lastOne
				? budget - handed
				: (int)((wxLongLong_t)budget * m_dragWidths[idx] / weight);

			shown[idx] = share;
			handed += share;
		}

		break;
	}

	// 4. AND THE ONE BORDER THAT CANNOT BE MOVED: the LAST range's right edge IS the table's
	//    right edge. With nothing standing to its right there is nobody to hand the freed room
	//    to, and the row still has to fill the table — so narrowing it is not a width the law
	//    can grant. Widening it is fine: the row grows past the table and the scrollbar says so.
	//
	//    "NOTHING TO ITS RIGHT" MEANS NO RANGE THERE AT ALL — not "they are all at their
	//    floors". Reading the empty POOL as that condition made a squeezed row snap the dragged
	//    column out to the table's edge and hold it there: measured ask=251 answered with 446,
	//    ten ranges floored, the border 195 px from the pointer.
	if (own + 1 == ranges.size() && left + shown[own] < room)
		shown[own] = room - left;

	// Recorded as ASKED FOR — that is what keeps the automatic stretch from taking it back once
	// the drag ends — and announced ONCE, after all of them are set: per column the whole fit
	// would run over widths only half applied.
	//
	// THE WHOLE LAW IS A FUNCTION OF WHERE THE POINTER IS. Nothing accumulates: every move is
	// computed from the widths the drag STARTED with, so dragging back to where it began restores
	// exactly those widths, and no sequence of moves can drift. The previous version measured
	// changes against the previous state and needed a spare-room budget, four rounds of
	// collection and a total-restoring correction to stay honest — and still flip-flopped with
	// the automatic fit, one handing out pixels and the other taking them back.
	for (size_t idx = 0; idx < ranges.size(); idx++) {
		for (ibDataViewColumn* member : ranges[idx].columns)
			member->WXSetSpecifiedWidth(shown[idx]);
	}

	InvalidateColBestWidth(column);
	OnColumnResized();
}
void ibDataViewCtrl::InvalidateColumnLayout()
{
	m_columnLayoutDirty = true;

	const ibDataViewColumnLayout& layout = GetColumnLayout();

	// A row is as tall as the bands it holds...
	m_lineHeight = GetBandHeight() * wxMax(layout.GetRowBandCount(), 1);
	// ...and the header as deep as the group titles standing above them.
	ApplyHeaderHeight();

	// AND THE SCREEN IS RE-DERIVED FROM IT. A row that just became two bands tall makes
	// every total on this control wrong — the virtual height, the scrollbars, the rows
	// that fit — and recomputing the heights here without saying so left the window
	// still laid out for the old ones: the second band of the last row was cut off, and
	// re-opening the form (which recomputes everything) "fixed" it.
	UpdateDisplay();
}
