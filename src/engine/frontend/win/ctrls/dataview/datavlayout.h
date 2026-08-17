#ifndef _IB_DATAVIEW_COLUMN_LAYOUT_H_
#define _IB_DATAVIEW_COLUMN_LAYOUT_H_

#include "frontend/frontend.h"

#include <wx/object.h>
#include <wx/gdicmn.h>
#include <wx/string.h>

#include <vector>

class ibDataViewColumn;
class ibDataViewColumnGroup;

// THE ORIENTATION A GROUP CARRIES — the whole of what grouping means. Declared here,
// in the light header, because the form layer needs the kind (a control property) with
// no dataview type in sight.
//
// Plain enum, like the dataview's own ibDataViewSelectionMode / ibDataViewViewMode: the
// property system stores an enumeration's value as a long, so a scoped enum would need
// a cast at every one of those seams.
enum ibColumnGroupKind {
	ibColumnGroupHorizontal,   // columns side by side (the group's title spans them)
	ibColumnGroupVertical,     // columns one under another (the row grows taller)
	// IN CELL — the columns MERGE into one cell: they still sit side by side, but there
	// is only ONE header cell over them (the group's own, full depth) and no title of
	// their own. "Account / dimension" reads as one field, not as two columns that
	// happen to be adjacent.
	ibColumnGroupInCell,
};

// THE (x, band) GRID a table paints in — computed from the tree of column groups
// (declared in dataview.h, beside the columns they hold).
//
// A grid used to be a ROW of columns: every column a full-height stripe, left to
// right, with a header cell of its own above it. A GROUP changes that by carrying an
// ORIENTATION: side by side as before, or ONE UNDER ANOTHER inside the same width —
// the row then grows taller instead of wider (three account dimensions cost one
// column of width and three BANDS of height).
//
// A band is a sub-row. This is the ONE place the geometry is decided: the header, the
// cells, hit-testing and the editor all ask for the same rectangle, so "header +
// value + columns line up" holds by construction rather than by three implementations
// agreeing.


// A column's place inside the row: pixels across, BANDS down. `column` is the
// column it belongs to — null means "this display position shows nothing" (hidden,
// or hidden by its group), which is also the answer to "is it shown".
struct ibColumnPlacement {
	ibDataViewColumn* column = nullptr;
	int x = 0;             // px from the left edge of the whole column area
	int width = 0;         // px
	int band = 0;          // first sub-row it occupies
	int bandSpan = 1;      // how many sub-rows it occupies
	// MERGED with the column to its right (an in-cell group): no rule is drawn
	// between them, because they are one cell showing two values.
	bool mergedRight = false;

	// WHERE IT SITS IN A STACK, which is what decides who shares its width. `stack`
	// is the nearest vertical group above it (null = not stacked), `slot` its place
	// among the columns of its OWN row there. Columns line up by slot: the first
	// column of every row is one width, the second another. Giving one width to
	// everything under the stack instead made a row of two columns twice as wide as
	// the drag asked for — and the next drag measured from that, so it ran away.
	ibDataViewColumnGroup* stack = nullptr;
	int slot = 0;
};

// One cell of the header: either a GROUP title (group != nullptr) or a column's
// own title (place.column != nullptr). Same (x, band) grid as the body, one band
// deeper per group level.
struct ibHeaderCell {
	ibDataViewColumnGroup* group = nullptr;
	ibColumnPlacement place;
};

// ----------------------------------------------------------------------------
// ibDataViewColumnLayout — computes the (x, band) grid from the columns.
//
// Rebuild() is fed the VISIBLE columns in DISPLAY order; everything else reads
// off the result. Sizes are in pixels across and bands down — turning bands into
// pixels is the caller's business (the body multiplies by the sub-row height,
// the header by the header-button height), which keeps this class free of any
// device context.
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewColumnLayout {
public:

	ibDataViewColumnLayout() = default;

	// Lays out the tree hanging off `root` — the control's root group. Order and
	// nesting are read from the members; nothing is inferred from anywhere else.
	void Rebuild(const ibDataViewColumnGroup* root);

	// Sub-rows a data row is split into (always >= 1: a flat grid is one band).
	int GetRowBandCount() const { return m_rowBands; }
	// Bands of the header — deeper than the body by the group titles above it.
	int GetHeaderBandCount() const { return m_headerBands; }

	// True when no column is grouped — the flat grid, where every caller can keep
	// its old left-to-right arithmetic.
	bool IsFlat() const { return m_flat; }

	int GetTotalWidth() const { return m_totalWidth; }

	// Where a COLUMN sits — asked by the column itself, since the tree, not a position
	// in some list, is what decides its place. False when it is not laid out (hidden,
	// or hidden by a group above it).
	bool GetBodyPlacement(const ibDataViewColumn* column, ibColumnPlacement& out) const;
	// Its place in the HEADER (a column under a group starts below the group titles).
	bool GetHeaderPlacement(const ibDataViewColumn* column, ibColumnPlacement& out) const;

	// Every header cell — group titles AND column titles — in paint order (groups first
	// per level, top level down).
	const std::vector<ibHeaderCell>& GetHeaderCells() const { return m_headerCells; }

	// Every column that IS laid out, in the order the tree puts them. Whoever asks
	// "which columns share this one's width" walks these instead of re-deriving the
	// relationship from the group tree — the slots were decided here.
	const std::vector<ibColumnPlacement>& GetBodyPlacements() const { return m_body; }

	// The column whose body rectangle contains (x, band); nullptr when none does.
	ibDataViewColumn* FindColumnAt(int x, int band) const;

	// The header cell containing (x, headerBand); nullptr when none does.
	const ibHeaderCell* FindHeaderCellAt(int x, int band) const;

	// The column a DRAG ON THIS GROUP'S EDGE should resize — its last one, since the
	// group's right edge is that column's right edge. A group is not a column and has
	// no width of its own, so widening one is widening the columns under it.
	ibDataViewColumn* FindLastColumnOf(const ibDataViewColumnGroup* group) const;

private:

	void Clear();

	struct ibLayoutNode;

	// Mirror the group's members into layout nodes, skipping what is not shown. A
	// group whose every member is hidden contributes nothing (an EMPTY group still
	// contributes — it is a group with no members, and it takes its title's room).
	void BuildNodes(const ibDataViewColumnGroup* group, std::vector<ibLayoutNode>& out);

	// Measure a node (fills width / bodyBands / headerDepth).
	void MeasureNode(ibLayoutNode& node);

	// Does the node's group draw a header cell of its own? The ROOT never does — it is
	// the store, not a grouping, and shows nothing of itself.
	static bool OwnsHeaderCell(const ibLayoutNode& node);

	// Place a node inside the rectangle it was given, emitting placements + header cells.
	// `headerBand` / `headerSpan` are its slice of the HEADER: a column stretches over
	// all of what it is given, which is the rest of the header for a plain column and
	// exactly one band for a column inside a STACK (otherwise it would paint over the
	// titles below it — that was the defect). `merged` = an ancestor is an IN-CELL
	// group and owns the one header cell for everything under it, so the nodes below
	// emit their body rectangles and no header cell of their own.
	// `stack` / `slotCounter` carry the stack a node is under and the running number
	// of columns already placed in the CURRENT row of it — that number is a column's
	// slot, and the counter restarts for every row.
	void PlaceNode(ibLayoutNode& node, int x, int width, int band, int bandSpan,
		int headerBand, int headerSpan, bool merged = false,
		ibDataViewColumnGroup* stack = nullptr, int* slotCounter = nullptr);

	// The columns that ARE laid out, each carrying the column it belongs to. Kept as
	// plain vectors and looked up by pointer: the sets are tens of entries, and a map
	// would be a second index over the same truth.
	//
	// m_header and m_headerCells are NOT two copies of one thing, and the difference is
	// the IN-CELL group: m_header answers "where is THIS column's title", and for a
	// merged column that is the group's one shared cell, so it has an entry; m_headerCells
	// is what gets DRAWN, and a merged column draws nothing of its own, so it has none.
	std::vector<ibColumnPlacement> m_body;
	std::vector<ibColumnPlacement> m_header;
	std::vector<ibHeaderCell> m_headerCells;

	int m_rowBands = 1;
	int m_headerBands = 1;
	int m_totalWidth = 0;
	bool m_flat = true;
};

#endif // _IB_DATAVIEW_COLUMN_LAYOUT_H_
