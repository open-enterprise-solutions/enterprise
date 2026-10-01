////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the table's row-less hop - one body for every kind of table
////////////////////////////////////////////////////////////////////////////

#include "backend/tabularDataObject.h"

#include "backend/srcDataObject.h"                                // ResolvePath - the shared deep-hop loop
#include "backend/metaCollection/partial/reference/reference.h"   // CoerceHopType - the reference builds its OWN twin

// A STRUCTURE WALK STEPS BY TYPE, NOT BY VALUE. There is no row to read at form-design time, and none is
// wanted: what the walk needs is something OF THE COLUMN'S TYPE, so it can go on reading the referenced
// source's own columns. The table says what the column accepts (GetColumnTypeById); the REFERENCE builds
// the twin, because fabricating reference logic anywhere else is how a second, subtly different copy of it
// appears. The column's CURRENT type is handed over as the filter, so a column retyped in the designer
// leaves a stale pin unresolved instead of resolving to the dead old branch.
//
// Written ONCE, here. Before this it existed twice — on the value table and on the dynamic list — each added
// separately, each after the same symptom: a dotted reference column read back as "<not selected>" though the
// field picker showed it. Every other kind of table now gets it for free, and the model layer is not asked to
// know anything about references in order to answer about a column.
//
// Those two classes still carry a ONE-LINE override, and it is not a leftover: they are ALSO ibSourceDataObject,
// an unrelated base declaring this same signature — a separate virtual slot, which the scalar walk is the one
// to dispatch through. Deleting their bridge does not fail to build; it silently restores the symptom.
bool ibTabularDataObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	return ibValueReferenceDataObject::CoerceHopType(hop, out, GetColumnTypeById(hop.m_id), GetSourceMetaData());
}

// The ROW analog of ibSourceDataObject::GetValueByPath: the table owns the FIRST hop only - it is a column
// of this row, and only this object knows how to read a cell out of `item`. Whatever that cell yields
// self-describes the rest of the walk, so the deeper hops go through the SAME shared loop every dotted path
// in the engine uses. The table does not interpret them: it never asks what the cell IS.
bool ibTabularDataObject::GetValueByPath(const ibDataViewItem& item, const std::vector<ibSourceHop>& path, size_t from, ibValue& out) const
{
	if (from >= path.size())
		return false;
	ibValue current;
	if (!GetValueBySourceHop(item, path[from], current))
		return false;   // the row has no such column, or the cell could not be read
	return ibSourceDataObject::ResolvePath(current, path, from + 1, out);
}
