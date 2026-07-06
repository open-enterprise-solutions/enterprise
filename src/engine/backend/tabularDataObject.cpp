////////////////////////////////////////////////////////////////////////////
//	Description : ibTabularDataObject — the full tabular source (table hop walk)
////////////////////////////////////////////////////////////////////////////

#include "tabularDataObject.h"
#include "backend/srcDataObject.h"   // ibSourceDataObject::ResolvePath — the shared SCALAR walk

// THE table dot-walk — the table only STARTS the walk. The FIRST hop is a column of the ROW (the hop gate, with
// item), which yields a source object; the DEEPER hops are TRANSFERRED to that source object (the shared scalar
// ResolvePath). Metadata-free — each value self-describes the next id.
bool ibTabularDataObject::GetValueByPath(const ibDataViewItem& item, const std::vector<ibSourceHop>& path, size_t from, ibValue& out) const
{
	if (from >= path.size())
		return false;
	ibValue current;
	if (!GetValueBySourceHop(item, path[from], current))   // start at the row — the first hop yields a source
		return false;
	return ibSourceDataObject::ResolvePath(current, path, from + 1, out);   // transfer the rest to the source object
}
