#ifndef __TABULAR_DATA_OBJECT_H__
#define __TABULAR_DATA_OBJECT_H__

#include "backend/srcObject.h"   // ibTabularObject (minimal base) + BACKEND_API
#include "backend/compiler/value.h"     // ibValue (inline GetValueByPath default-constructs one)
#include "backend/sourceDescription.h"  // ibSourceHop — a binding path is {id, expected type} per hop
#include "backend/typeDescription.h"    // ibTypeDescription — what a column accepts, the table's own answer

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

	// WHAT THIS COLUMN ACCEPTS — the structure hop's only input. Declared here because the hop below is
	// here; ANSWERED once by ibValueModel, off the model's own column collection, which is already kept in
	// step with whatever the model's columns really are (a section's column info reads the attribute, a
	// list's wraps the queryable column, a value-table's holds the edited type property). Empty default for
	// an id the table does not have — and for any future table that is not a model.
	virtual ibTypeDescription GetColumnTypeById(const ibMetaID& id) const { return ibTypeDescription(); }

	// THE SAME HOP WITH NO ROW — the STRUCTURE step. A form under construction has no rows, and the walk
	// does not want a datum anyway: it wants something OF THE COLUMN'S TYPE, to go on reading that source's
	// own columns. The base turns the type above into the pinned empty twin (body in tabularDataObject.cpp,
	// which is where the reference machinery may be named — the model layer must not learn about references
	// to answer a question about a column).
	//
	// VIRTUAL with a shared BODY, and the distinction is the point. It used to be one copy per table,
	// written twice on two separate occasions after the same symptom each time — a dotted reference column
	// read back as "<not selected>" though the picker showed it; that is what the shared body ends.
	// But the body is the DEFAULT, not the law: the twin it builds is a reference, which is one PARTICULAR
	// way a column can be stepped into. A table whose columns step differently — a filter model carrying its
	// own fields and values — MAY override here and materialise its own, instead of the mechanism deciding on
	// its behalf that every dot in the product is a reference. Nothing overrides it that way yet.
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const;

	// Walk a whole hop path off THIS table's ROW to the leaf value — the ROW analog of ibSourceDataObject::
	// GetValueByPath. The table only STARTS the walk: the FIRST hop (path[from]) is a column of the ROW (the hop
	// gate above, with `item`), which yields a SOURCE object; the DEEPER hops are TRANSFERRED to that source
	// object (the shared scalar ibSourceDataObject::ResolvePath). from = the first row-relative hop.
	bool GetValueByPath(const ibDataViewItem& item, const std::vector<ibSourceHop>& path, size_t from, ibValue& out) const;
};

#endif // !__TABULAR_DATA_OBJECT_H__
