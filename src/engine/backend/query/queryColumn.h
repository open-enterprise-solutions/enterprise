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
// (and model.h) weight: ibValueMetaObjectAttributeBase IS a query column — it
// implements this interface directly, no adapter.

#include "backend/typeDescription.h"    // ibTypeDescription (the column's L3 type)

// ⚠ NOT <wx/icon.h> — FORWARD-DECLARED. backend.dll is GUI-free (CLAUDE.md), and this header is
// included by every attribute metaobject, so one GUI include here reaches ~25 direct includers and
// everything behind them. A virtual returning wxIcon compiles against an incomplete type; only the
// two files that DEFINE such a body need the real header, and they already include it.
class wxIcon;

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
	// A SCHEDULE (JobSchedule) — a value object stored whole, in its own blob field. It is here
	// rather than folded into one of the tags above because it is neither a primitive nor a
	// reference: nothing points at it, it has no row of its own, and it is read back by
	// deserialising the blob. The tag is APPENDED, never inserted — it is persisted in every
	// composite column's _TYPE field, so a renumbering would re-read old rows as another type.
	ibFieldTypes_Schedule,
	// A TYPE DESCRIPTION — the same shape as the schedule: a value object stored whole in its own
	// blob field, neither primitive nor reference. It carries a SET OF ADMISSIBLE TYPES with their
	// qualifiers (a characteristic's own Type) and is read back by deserialising the blob.
	//
	// APPENDED AT THE END for the reason stated above — these values are persisted in every
	// composite column's _TYPE field, so inserting one renumbers every tag after it and old rows
	// start reading as a different type.
	ibFieldTypes_TypeDescription,
};

// (ibSQLField — the structured "_TYPE + per-type field" projection — is REMOVED. Its analog is the
//  column-layout tier: DescribeColumnLayout (slots) / ColumnFieldNames / ColumnFieldList. The enum
//  ibFieldTypes above stays — it is the persisted _TYPE discriminator value, used by the codec.)

// ibBackendSourceColumn — the SOURCE/UI face of a column: name, synonym, type. The minimal
// "column, like a DB column". It is the BASE of ibBackendQueryColumn (every query/DB column IS a
// source column) and, through it, of the attribute metaobject — so the source-binding dot-walk
// (ibBackendTypeSourceFactory) returns THIS, blind to the concrete class: a metaobject attribute
// OR a dynamic list's queryable column, both already ARE an ibBackendSourceColumn, no adapter.
// ibBackendAbstractColumn — the NAME / SYNONYM / COMMENT face shared by a metadata source column
// AND a form attribute (the "linking element"): a control reads the caption / comment from it without
// knowing which it is. Both ibBackendSourceColumn (metadata / query column) and the form attribute
// (ibBackendFormAttributeValue) derive it, so ONE resolver returns either, uniformly.
class BACKEND_API ibBackendAbstractColumn
{
public:
	virtual ~ibBackendAbstractColumn() = default;

	// Logical identifier — what a script / the L4 parser refers to.
	virtual wxString GetName() const = 0;

	// Display caption. Defaults to the logical name; a metaobject column / a form attribute override
	// it with their synonym.
	virtual wxString GetSynonym() const { return GetName(); }

	// Tooltip / comment. Empty by default; a metaobject column / a form attribute override it.
	virtual wxString GetComment() const { return wxEmptyString; }
};

class BACKEND_API ibBackendSourceColumn : public ibBackendAbstractColumn
{
public:
	virtual ~ibBackendSourceColumn() = default;

	// GetName / GetSynonym / GetComment come from ibBackendAbstractColumn.

	// The column's L3 type — CLSIDs + number / string / date qualifiers. The single source of
	// "what this column holds". The SAME accessor the attribute already exposes — free.
	virtual ibTypeDescription& GetTypeDesc() const = 0;

	// WHAT A VALUE HERE MAY BE — the same object for every ordinary column. Declared on BOTH bases (the
	// type factory names it too, backend_type.h) for the same reason GetTypeDesc is: an attribute is a
	// column AND a type factory, and one overrider then answers for both. A column that stands behind
	// no declaration of its own — temp, computed — inherits this default and never differs.
	virtual ibTypeDescription& GetTypeValueDesc() const { return GetTypeDesc(); }

	// Is the column usable / shown? A plain (queryable) column always is; a metaobject attribute
	// overrides — a deleted or access-denied field is not. The metadata-agnostic source explorer
	// gates on THIS instead of poking the metaobject.
	virtual bool IsAllowed() const { return true; }

	// THE COLUMN'S OWN PICTURE — asked of the column, never deduced by the reader. The default is
	// the plain ATTRIBUTE picture from the icon library, so every column is dressed even when it
	// stands behind no metaobject (a view's column, a temp table's); a column that IS a metaobject
	// answers with whatever picture its own metatype registered. That is what keeps a dimension
	// from looking like a resource without anybody, anywhere, keeping a list of kinds and a switch
	// over it — a metatype added tomorrow is dressed the day it registers an icon.
	//
	// Body in metaAttributeObject_res.cpp, next to the icon it returns: this header must not pull
	// the metadata tree in (the attribute metaobject includes THIS file).
	virtual wxIcon GetColumnIcon() const;
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

	// (The column's value-field split — a composite / variant / reference column expands to several
	// physical fields — is NOT a column method: it is the tier free function ColumnValueFields(col)
	// over DescribeColumnLayout (columnLayout.h), metadata-free, asked only by the DB provider. The
	// column stays a pure descriptor; value materialization / binding stays on the queryable.
	// docs/query-language-arc.md §22.4b)

	// (No per-column primary-key flag: a source's uniqueness key is owned by the QUERYABLE —
	// ibBackendQueryable::GetPrimaryKeyColumns is the ONE authority for both the write UPSERT
	// match AND the auto-join self-reference key (a record's data-reference); the uuid read keyset
	// is GetPrimaryKeyColumns. The column stays a pure typed descriptor. docs/query-language-arc.md §22.1)

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
	// Reference is the FIXED reference-key binary (_RRRef = pure [guid 16], indexable; type is the _RTRef column); Blob is a
	// VARIABLE-length blob (a register's rowData). Guid / Blob / Reference are the schema-scaffold types
	// so the structure builder can create those columns through this same column interface, no ibDdlColumn.
	enum class RawType { String, Number, Reference, Date, Boolean, Guid, Blob };

	// `modelId` gives the column an IDENTITY. Zero (the default) means "scaffold": a field created
	// with its table and never migrated — a row key, a blob of packed data. A non-zero id makes it
	// a column the schema differ can track, so it can be ADDED to or DROPPED from an existing
	// table. A derived table's accumulating columns need that: they appear and disappear as the
	// metaobject gains and loses resources, and without an identity the differ cannot see either.
	ibRawDBColumn(const wxString& field, RawType type, ibMetaID modelId = 0)
		: m_field(field), m_type(type), m_modelId(modelId) {}

	// ⭐ TWO NAMES WHERE THEY DIFFER, one where they do not. A scaffold column is its own field and
	// says so under one name. A column PUBLISHED to a query is a different case: `Ref` is what an
	// author writes, `uuid` is what the table keeps, and handing the storage spelling out as the
	// name puts the physical schema into the field tree — the exact mistake the register's view
	// columns were fixed for.
	wxString              GetName()         const override { return m_name.IsEmpty() ? m_field : m_name; }
	wxString              GetPhysicalName() const override { return m_field; }
	ibTypeDescription&    GetTypeDesc()     const override { return m_typeDesc; }   // interface returns a non-const ref
	ibMetaID              GetColumnId()      const override { return m_modelId; }   // 0 = scaffold, never diffed
	bool                  IsRawColumn()     const override { return true; }

	RawType               GetRawType()      const { return m_type; }   // the provider's bind selector
	// The declared width: a string's length, a number's PRECISION. 0 = "no reason to say", and the
	// layout tier then answers with its default.
	unsigned int          GetRawLength()    const { return m_length; }
	unsigned int          GetRawScale()     const { return m_scale; }

	// Convenience makers — read cleaner than naming the enum at the call site, with no extra
	// type to maintain (these are static factories, not subclasses). One per RawType.
	// ⭐ A WIDTH, WHERE THE COLUMN HAS A REASON TO NAME ONE. Zero = the default (255), which is right
	// for a scaffold column nobody indexes. It is NOT right for a column an INDEX stands on: Firebird
	// sizes an index key from the DECLARED length times the charset's bytes-per-character, so a
	// VARCHAR(255) in UTF8 is 1020 bytes and passes the key-size ceiling (≈ page_size / 4) on its own —
	// "key size exceeds implementation restriction", with nothing wrong but the declaration.
	static ibRawDBColumn String   (const wxString& field, ibMetaID id = 0, unsigned int length = 0) {
		ibRawDBColumn col(field, RawType::String, id);
		col.m_length = length;
		return col;
	}
	// The same for a number: a totals column has to carry the RESOURCE's own precision and scale, or a
	// figure with kopecks is stored in a column that has none and the fraction is lost on the way in.
	static ibRawDBColumn Number   (const wxString& field, ibMetaID id = 0,
	                               unsigned int precision = 0, unsigned int scale = 0) {
		ibRawDBColumn col(field, RawType::Number, id);
		col.m_length = precision;
		col.m_scale  = scale;
		return col;
	}
	// ⭐ A REFERENCE STORED AS ONE FIELD, WITH A CONSTANT TARGET.
	//
	// The ordinary reference column is a PAIR — `_RTRef` (which type) beside `_RRRef` (which row) —
	// because the value may point at several kinds. This one cannot: a tabular section belongs to
	// exactly one owner, so the type is known from the metadata and storing it per row would be a
	// column repeating one constant a million times. The target rides on the column instead, and the
	// codec reads a real reference out of the sixteen bytes it finds.
	static ibRawDBColumn Reference(const wxString& field, const ibClassID& target = 0,
	                               const wxString& name = wxEmptyString, ibMetaID modelId = 0)
	{
		ibRawDBColumn col(field, RawType::Reference, modelId);
		col.m_name = name;
		if (target != 0)
			col.m_typeDesc.SetDefaultMetaType(target);
		return col;
	}

	// What this column points at — 0 when it is not a single-target reference.
	ibClassID GetRawTarget() const { const auto& list = m_typeDesc.GetClsidList(); return list.empty() ? 0 : list.front(); }
	static ibRawDBColumn Date     (const wxString& field, ibMetaID id = 0) { return ibRawDBColumn(field, RawType::Date, id);      }
	static ibRawDBColumn Boolean  (const wxString& field) { return ibRawDBColumn(field, RawType::Boolean);   }
	static ibRawDBColumn Guid     (const wxString& field) { return ibRawDBColumn(field, RawType::Guid);      }
	static ibRawDBColumn Blob     (const wxString& field) { return ibRawDBColumn(field, RawType::Blob);      }

private:
	wxString                  m_field;
	wxString                  m_name;    // what a QUERY writes; empty = the field is its own name
	RawType                   m_type;
	unsigned int              m_length = 0;   // string length / number precision; 0 = the tier's default
	unsigned int              m_scale  = 0;   // number scale — the fraction a figure is stored with
	ibMetaID                  m_modelId;   // 0 = scaffold: created with its table, never migrated
	mutable ibTypeDescription m_typeDesc;   // mutable: GetTypeDesc() is const but returns a non-const ref
};

#endif
