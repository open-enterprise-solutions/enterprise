#ifndef __TABULAR_DATA_OBJECT_H__
#define __TABULAR_DATA_OBJECT_H__

#include "backend/srcObject.h"   // ibTabularObject (minimal base) + BACKEND_API
#include "backend/compiler/value.h"     // ibValue (inline GetValueByPath default-constructs one)
#include "backend/sourceDescription.h"  // ibSourceHop — a binding path is {id, expected type} per hop

// Fwd — the hop gate takes these BY REF, so declarations suffice here (full types come in the .cpp's that
// implement it): ibSourceHop (sourceDescription.h), ibDataViewItem (modelView.h), ibValue (compiler/value.h).
class BACKEND_API ibDataViewItem;

// ibTabularDataObject — the FULL tabular source: the TABLE mirror of ibSourceDataObject (the full SCALAR source
// over ibSourceObject). It is ibTabularObject + THE table hop gate. A table value is PER-ROW, so the hop gate
// carries the ROW (item) alongside the {id, type} hop — otherwise it is the SAME single entry as the scalar gate.
// The table only STARTS a dot-walk (the first hop off a row yields a source cell) and TRANSFERS the deeper hops
// to that source object. Default false; concrete table models override. Gateway bool = did the hop RESOLVE.
class BACKEND_API ibTabularDataObject : public ibTabularObject {
public:
	virtual ~ibTabularDataObject() {}
	virtual bool GetValueBySourceHop(const ibDataViewItem& item, const ibSourceHop& hop, ibValue& out) const { return false; }
	virtual bool SetValueBySourceHop(const ibDataViewItem& item, const ibSourceHop& hop, const ibValue& value) { return false; }

	// Walk a whole hop path off THIS table's ROW to the leaf value — the ROW analog of ibSourceDataObject::
	// GetValueByPath. The table only STARTS the walk: the FIRST hop (path[from]) is a column of the ROW (the hop
	// gate above, with `item`), which yields a SOURCE object; the DEEPER hops are TRANSFERRED to that source
	// object (the shared scalar ibSourceDataObject::ResolvePath). from = the first row-relative hop.
	bool GetValueByPath(const ibDataViewItem& item, const std::vector<ibSourceHop>& path, size_t from, ibValue& out) const;
};

#endif // !__TABULAR_DATA_OBJECT_H__
