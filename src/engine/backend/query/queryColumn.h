#ifndef __QUERY_COLUMN_H__
#define __QUERY_COLUMN_H__

// ibBackendQueryColumn — the column counterpart of ibBackendQueryable: ONE logical,
// typed field of a queryable. It is a PURELY L3 descriptor —
//   name      : the logical column name (the requisite the script names),
//   physical  : the base physical column (db field name) it maps to,
//   type      : its ibTypeDescription (CLSIDs + number / string / date qualifiers).
// That is all L3 needs to reason about a column (projection, validation, virtual
// tables). The physical multi-column split (TYPE / _N / _S / _RRRef) and the value
// binding are LOWERING — derived from (physical, type) below the L3 surface, never
// on this interface. No statement, no cursor, no positions here.
//
// Lives in its OWN light header (only typeDescription.h) so the fundamental
// attribute metaobject can derive from it without dragging in the full queryable.h
// (and tableInfo.h) weight: ibValueMetaObjectAttributeBase IS a query column — it
// implements this interface directly, no adapter.

#include "backend/typeDescription.h"    // ibTypeDescription (the column's L3 type)

class BACKEND_API ibBackendQueryColumn
{
public:
	virtual ~ibBackendQueryColumn() = default;

	// Logical column name — what a script / the L4 parser refers to.
	virtual wxString GetName() const = 0;

	// Base physical column (db field name). The lowering derives the actual
	// per-type physical columns from this plus the type description.
	virtual wxString GetPhysicalName() const = 0;

	// The column's L3 type — CLSIDs + number / string / date qualifiers. The single
	// source of "what this column holds"; the physical layout is a function of it.
	// This is the SAME accessor the attribute already exposes, so an attribute
	// implements it for free — no separate method, no copy.
	virtual ibTypeDescription& GetTypeDesc() const = 0;
};

#endif
