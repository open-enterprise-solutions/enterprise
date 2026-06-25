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

#include <vector>

// ibFieldTypes — the persisted VARIANT TAG stored in a composite column's _TYPE field: which of
// the column's allowed types the row actually holds. The L3 column vocabulary, shared by the
// layout tier, the value codec and the (forwarding) attribute. Moved here out of the heavy
// attribute header so the tier can speak it without dragging the metadata class in.
enum ibFieldTypes {
	ibFieldTypes_Empty = 0,
	ibFieldTypes_Boolean,
	ibFieldTypes_Number,
	ibFieldTypes_Date,
	ibFieldTypes_String,
	ibFieldTypes_Null,
	ibFieldTypes_Enum,
	ibFieldTypes_Reference,
};

// (ibSQLField — the structured "_TYPE + per-type field" projection — is REMOVED. Its analog is the
//  column-layout tier: DescribeColumnLayout (slots) / ColumnFieldNames / ColumnFieldList. The enum
//  ibFieldTypes above stays — it is the persisted _TYPE discriminator value, used by the codec.)

// ibBackendSourceColumn — the SOURCE/UI face of a column: name, synonym, type. The minimal
// "column, like a DB column". It is the BASE of ibBackendQueryColumn (every query/DB column IS a
// source column) and, through it, of the attribute metaobject — so the source-binding dot-walk
// (ibBackendTypeSourceFactory) returns THIS, blind to the concrete class: a metaobject attribute
// OR a dynamic list's queryable column, both already ARE an ibBackendSourceColumn, no adapter.
class BACKEND_API ibBackendSourceColumn
{
public:
	virtual ~ibBackendSourceColumn() = default;

	// Logical column name — what a script / the L4 parser refers to.
	virtual wxString GetName() const = 0;

	// Display name (UI caption). Defaults to the logical name; a metaobject column overrides it
	// with its synonym.
	virtual wxString GetSynonym() const { return GetName(); }

	// The column's L3 type — CLSIDs + number / string / date qualifiers. The single source of
	// "what this column holds". The SAME accessor the attribute already exposes — free.
	virtual ibTypeDescription& GetTypeDesc() const = 0;

	// Is the column usable / shown? A plain (queryable) column always is; a metaobject attribute
	// overrides — a deleted or access-denied field is not. The metadata-agnostic source explorer
	// gates on THIS instead of poking the metaobject.
	virtual bool IsAllowed() const { return true; }
};

class BACKEND_API ibBackendQueryColumn : public ibBackendSourceColumn
{
public:
	virtual ~ibBackendQueryColumn() = default;

	// Base physical column (db field name). The lowering derives the actual
	// per-type physical columns from this plus the type description.
	virtual wxString GetPhysicalName() const = 0;

	// The column's id WITHIN its source/model — the key its source reads a value by.
	// For a DB / attribute column this IS the metaID; for a computed / temp column it
	// is the source's own internal unique id ("source id") — whatever the queryable
	// keys its rows by. The point: each column self-describes its read key, so a model
	// (RAM) read is GetValueByMetaID(col->GetColumnId()) with NO attribute resolution —
	// and a non-metaobject temp column fits the same shape. UNIQUE PER COLUMN (a
	// resource's Balance vs Turnover have distinct model ids, though one metaID).
	virtual ibMetaID GetColumnId() const = 0;

	// The column's PHYSICAL SQL fields — the multi-column split a DB lowering needs (a
	// composite / variant / reference column expands to several: _B / _N / _D / _S / _E /
	// _RRRef). This is the column-based replacement for resolving back to an attribute and
	// calling its GetSQLFieldData: the DB IR builder asks the COLUMN for its fields (sort /
	// group-by), no ResolveAttribute. The attribute metaobject OVERRIDES this authoritatively
	// (its own field machinery — byte-identical to the former path); the light default here
	// is the bare physical name (a single untyped field), enough for a plain temp column.
	// Value materialization / binding (reference reconstruction) still needs metadata context
	// and stays on the queryable. (docs/query-language-arc.md §22.4b)
	virtual std::vector<wxString> GetValueFields() const { return std::vector<wxString>{ GetPhysicalName() }; }

	// (No per-column primary-key flag: a source's uniqueness key is owned by the QUERYABLE —
	// ibBackendQueryable::GetPrimaryKeyColumns is the ONE authority for both the write UPSERT
	// match AND the auto-join self-reference key (a record's data-reference); the uuid read keyset
	// is GetIdentitySort. The column stays a pure typed descriptor. docs/query-language-arc.md §22.1)

	// Is this a RAW (direct) physical column — addressed by its concrete field name with NO
	// metadata translation (no TYPE/_N/_S/_RRRef expansion, no SetValueAttribute decomposition)?
	// A metadata attribute returns false (it IS translated); ibRawDBColumn returns true. The
	// provider uses this to decide: bind the value straight (raw) vs decompose it (attribute).
	// (docs/query-language-arc.md §22)
	virtual bool IsRawColumn() const { return false; }
};

// ==========================================================================
// ibRawDBColumn — a DIRECT physical column: a concrete db field named AS-IS, no metadata
// behind it and no translation. Lets the door address a real column straight (the row-key
// uuid; a balance's computed qty_balance) through the SAME ibBackendQueryColumn interface as
// a metadata attribute. The PHYSICAL TYPE is carried by the column itself (its RawType), so
// the provider binds it with no value-type guessing — use the typed factories below.
// Slicing-safe — all state is on this base, so the door takes one by ref and owns a copy.
// The uniqueness key is the queryable's concern, not the column's. (docs §22)
// ==========================================================================
class BACKEND_API ibRawDBColumn : public ibBackendQueryColumn
{
public:
	// How the provider binds the raw value — fixed by the concrete factory, not the value.
	// Reference is the FIXED reference-key binary (_RRRef = [guid 16][metaID 4], indexable); Blob is a
	// VARIABLE-length blob (a register's rowData). Guid / Blob / Reference are the schema-scaffold types
	// so the structure builder can create those columns through this same column interface, no ibDdlColumn.
	enum class RawType { String, Number, Reference, Date, Boolean, Guid, Blob };

	ibRawDBColumn(const wxString& field, RawType type)
		: m_field(field), m_type(type) {}

	wxString              GetName()         const override { return m_field; }
	wxString              GetPhysicalName() const override { return m_field; }
	ibTypeDescription&    GetTypeDesc()     const override { return m_typeDesc; }   // interface returns a non-const ref
	ibMetaID              GetColumnId()      const override { return 0; }            // no model — raw field
	std::vector<wxString> GetValueFields()    const override { return std::vector<wxString>{ m_field }; }
	bool                  IsRawColumn()     const override { return true; }

	RawType               GetRawType()      const { return m_type; }   // the provider's bind selector

	// Convenience makers — read cleaner than naming the enum at the call site, with no extra
	// type to maintain (these are static factories, not subclasses). One per RawType.
	static ibRawDBColumn String   (const wxString& field) { return ibRawDBColumn(field, RawType::String);    }
	static ibRawDBColumn Number   (const wxString& field) { return ibRawDBColumn(field, RawType::Number);    }
	static ibRawDBColumn Reference(const wxString& field) { return ibRawDBColumn(field, RawType::Reference); }
	static ibRawDBColumn Date     (const wxString& field) { return ibRawDBColumn(field, RawType::Date);      }
	static ibRawDBColumn Boolean  (const wxString& field) { return ibRawDBColumn(field, RawType::Boolean);   }
	static ibRawDBColumn Guid     (const wxString& field) { return ibRawDBColumn(field, RawType::Guid);      }
	static ibRawDBColumn Blob     (const wxString& field) { return ibRawDBColumn(field, RawType::Blob);      }

private:
	wxString                  m_field;
	RawType                   m_type;
	mutable ibTypeDescription m_typeDesc;   // mutable: GetTypeDesc() is const but returns a non-const ref
};

#endif
