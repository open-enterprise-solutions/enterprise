#ifndef _IB_TABLE_COLUMN_LAYOUT_H_
#define _IB_TABLE_COLUMN_LAYOUT_H_

// THE ORIENTATION A GROUP OF COLUMNS CARRIES — the whole of what grouping means, and a property of the
// group control (ibValueModelTableBoxColumnGroup). The model half of the desktop grid's column layout
// header: the kind alone, since the geometry it decides is the client's to draw.
//
// Plain enum, like the table's own ibDataViewSelectionMode / ibDataViewViewMode: the property system
// stores an enumeration's value as a long, so a scoped enum would need a cast at every one of those seams.
enum ibColumnGroupKind {
	ibColumnGroupHorizontal,   // columns side by side (the group's title spans them)
	ibColumnGroupVertical,     // columns one under another (the row grows taller)
	// IN CELL — the columns MERGE into one cell: they still sit side by side, but there
	// is only ONE header cell over them (the group's own, full depth) and no title of
	// their own. "Account / dimension" reads as one field, not as two columns that
	// happen to be adjacent.
	ibColumnGroupInCell,
};

#endif // !_IB_TABLE_COLUMN_LAYOUT_H_
