#ifndef __COLUMN_LAYOUT_H__
#define __COLUMN_LAYOUT_H__

// Column layout — the physical decomposition of ONE logical column, the single
// authority shared by the read/write codec (L3-1, ibDbTableProvider) and the
// schema/DDL door (L3-2). A composite / variant / reference attribute lowers to
// several physical columns, each with a ROLE (_TYPE / _B / _N / _D / _S / _E /
// _RTRef / _RRRef), a canonical TYPE (for DDL) and a DEFAULT. The field ORDER is
// byte-identical to the historical GetSQLFieldData / WriteFieldsOf order —
// LOAD-BEARING: the keyset anchor must match the first ORDER BY field.
//
// Pure descriptor: no transaction, no queryable, no cursor. It sits between L3
// (the query door) and L2 (the render): it speaks L2's ibColumnType, and is fed
// an L3 ibBackendQueryColumn plus the metadata context (for reference-type
// detection). This is the one home for the "magic" column spread that used to be
// duplicated across the attribute metaobject, the provider codec and the DDL
// builder. (docs/query-language-arc.md §22.4b)

#include "backend/databaseLayer/columnType.h"             // ibColumnType (shared canonical type — ibColumnSlot carries one)
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibQueryStatement / ibQueryResult (L2) — the codec binds/reads through them
#include "backend/query/queryColumn.h"                     // ibBackendQueryColumn

class ibMetaData;
class ibValue;             // codec currency — named only by reference in the codec signatures below
class ibReaderMemory;      // binary-wire reader (fileSystem/fs.h) — dump/restore path
class ibWriterMemory;      // binary-wire writer (fileSystem/fs.h)
// ibQueryStatement / ibQueryResult are full types from databaseQueryBuilder.h (included above) — the
// binary codec binds/reads through the L2 statement/cursor, not raw L1 (ibPreparedStatement/-ResultSet).

// (ibColumnRole / ibColumnSlot moved to queryColumn.h — they describe a COLUMN's field, and the
//  column answers with them (ibBackendQueryColumn::DescribeLayout). Declaring them here left the
//  column having to name the struct it returns without being able to see it.)

// The ONE role -> physical-suffix table. Every layer that names a composite column's physical fields
// (the layout below, the read keyset anchor, the DDL builder, the attribute field machinery) spells the
// suffix through this, so the lettering lives in exactly one map and can never disagree between writers.
// Raw -> empty (the column is its own field, no suffix).
BACKEND_API const wxString& ibFieldSuffix(ibColumnRole role);

// ⭐⭐ THE OWNER REFERENCE OF A TABULAR SECTION'S LINE — sixteen bytes naming the row's OWNER.
//
// It was called the ROW KEY, and once that was true: every table carried a scaffold column
// identifying its own row. Reference objects are identified by their REFERENCE now, so the scaffold
// survives in exactly ONE place — a tabular section, where it holds not the line's identity but the
// document or catalog item the line belongs to. Repeated across every line of one owner, and
// deliberately NOT unique (ibDataMover::KeyOf says exactly that where it matches rows by it).
//
// The old name cost real time. "Row key" reads as identity, so it kept being reached for wherever
// identity was wanted — into an object's primary key, into a cursor's identity tail, into a seed's
// match — and each time the mistake surfaced far from here ("unknown attribute 'Row_RRRef'"; a quick
// choice that would not open). A name is a claim about content, and this one made the wrong claim for
// a whole family of tables.
//
// The PHYSICAL field is unchanged (`Row` + the reference-id suffix, spelled through the same suffix
// table as every other field): existing databases carry it, and renaming a column is a migration, not
// a cleanup.
BACKEND_API const wxString& ibOwnerRefField();

// CAN THIS COLUMN BE COMPARED AT ALL? A value kept WHOLE — a schedule, a type description — lives in a
// single BLOB field, and a blob is not something SQL can test for equality or order. Filtering or
// sorting by such a column therefore cannot be lowered into a query at all: the decomposition compares
// the column field by field, and its only field is that blob.
//
// So the question is asked BEFORE anything is offered: the settings dialog leaves such a column out of
// the filter and order pickers instead of accepting a condition nobody can execute. Asked of the TYPE,
// not of a list of clsids at the call site — a value that starts being stored whole answers here.
BACKEND_API bool ibIsComparableType(const ibTypeDescription& typeDesc);

// (A separate "is there any storage behind this type" question lived here for a day. It is gone: what
//  a statement may WRITE is a fact about the COLUMN, not about a type — `ibBackendQueryColumn::Kind`
//  answers it, and a Synthetic column is neither projected nor published.)
BACKEND_API ibBackendColumnRawDB   ibOwnerRefColumn();


// The authority. Decompose a logical column into its physical fields, derived purely
// from (physical name, type descriptor) — NO metadata: the reference slot is gated by the
// clsid KIND (IsReference), not a metadata lookup. Order: TYPE, B, N, D, S, E, RTRef, RRRef
// — the same fixed order the write / read codec binds in. A raw column = one slot as-is.
BACKEND_API std::vector<ibColumnSlot> DescribeColumnLayout(const ibBackendQueryColumn* col);

// Whether a role CARRIES the column's value, as opposed to TAGGING what the value is (_TYPE, and a
// reference's _RTRef). This is the one split ColumnValueFields filters by, named so a consumer can
// ask it of a slot it already holds.
BACKEND_API bool ibIsValueRole(ibColumnRole role);

// A column's VALUE field names — its layout slots minus the _TYPE discriminator and the reference
// TYPE id (the value-carrying roles). Metadata-free, derived over DescribeColumnLayout. The DB
// provider asks this for sort / group-by / key field expansion; the column stays a pure descriptor.
BACKEND_API std::vector<wxString> ColumnValueFields(const ibBackendQueryColumn* col);

// ⭐ THE SAME FIELDS, WITH THEIR ROLE STILL ATTACHED. A consumer that must know what a field IS —
// a reference id needs its _RTRef tiebreak, a counter scan wants the number field — reads m_role.
// It exists because the suffix lettering kept escaping this tier: whoever got names only had no way
// left to ask the question except to match the tail of the string (`EndsWith("_RRRef")`), which is
// the layout re-derived by spelling, in a place that cannot be told when the lettering changes.
// Give the role alongside the name and the question is answerable where it is asked.
BACKEND_API std::vector<ibColumnSlot> ColumnValueSlots(const ibBackendQueryColumn* col);

// The column's FIRST value slot — the field a scalar comparison / sort / anchor rides. Empty name
// and Raw role when the column spreads to nothing.
BACKEND_API ibColumnSlot FirstValueSlot(const ibBackendQueryColumn* col);

// ⭐ A REFERENCE READ FROM ONE KEY FIELD, with the target type supplied by the COLUMN.
//
// The ordinary reference is a PAIR — `_RTRef` says which type, `_RRRef` which row — because the
// value may point at several kinds. A single-target column cannot: a tabular section belongs to one
// owner, so the type is a property of the table and storing it per row would repeat one constant on
// every line. The bytes are the identity; the type is metadata.
//
// It lives HERE, with the rest of the value <-> field codec, and not at the call site: assembling a
// reference needs the reference class, and a query provider that included it would be reaching a
// floor up into the metadata partials for one read.
BACKEND_API ibValue ReadSingleTargetReference(const ibMetaData* metaData, const ibClassID& target,
                                              const wxMemoryBuffer& keyBytes);

// The persisted _TYPE discriminator value a layout role stores (the ibFieldTypes tag — the wire
// format, kept here in the tier, not on the attribute). Used to clear rows whose stored type was
// just dropped. Returns ibFieldTypes_Empty for roles that carry no tag (Raw / Discriminator).
BACKEND_API int ibPersistedTypeTag(ibColumnRole role);

// SQL fragment builders over a column's physical layout — the ONE source for the field
// spellings the hand-written query/upsert SQL used to assemble per-attribute. All derive
// from DescribeColumnLayout, so the field SET + ORDER match the read/write codec exactly.
//   ColumnFieldNames        : the physical field NAMES in bind order (index columns / statement list)
//   ColumnFieldList         : "f_TYPE,f_B,f_N,…"  (SELECT list; aggr wraps the N/D measures)
//   ColumnComparePredicate  : "f_TYPE = ? AND f_B <cmp> ? AND …"  (WHERE by value)
BACKEND_API std::vector<wxString> ColumnFieldNames(const ibBackendQueryColumn* col);
BACKEND_API wxString       ColumnFieldList(const ibBackendQueryColumn* col, const wxString& aggr = wxEmptyString);
BACKEND_API wxString       ColumnComparePredicate(const ibBackendQueryColumn* col, const wxString& cmp = wxT("="));


// ⭐⭐ BIND A VALUE INTO A COLUMN — THE ONE DOOR, and the thing to call instead of the codec.
//
// A RAW column (the owner reference, a computed field) is bound straight by its declared RawType; a METADATA
// column goes through ibColumnCodec below. The distinction matters and is easy to miss from outside:
// the codec's first act is the TYPE discriminator, which a raw column has no field for — bind a raw
// key through it and a NUMBER lands in the key's parameter ("a number was bound to a parameter the
// statement declares as SQL type 452"), or, where the driver is lenient, the tag is stored AS the key.
//
// The write path has always gone through here. The schema's seed phase used to bind its key by hand
// beside it, which is how a declared row's guid reached a BINARY(16) column as text.
BACKEND_API void BindWriteValue(class ibQueryStatement& statement, const ibBackendQueryColumn* col,
                                const ibMetaData* metaData, const ibValue& value, int& position);

// ==========================================================================
// ibColumnCodec — the VALUE codec: the inverse pair that spreads an ibValue across a
// column's physical fields (write) and reassembles it from a result cursor (read). The
// SAME "magic" as the layout above, co-located here: a value bound by WriteValue lands in
// exactly the fields DescribeColumnLayout creates, in the same order. STATIC (stateless) —
// the read/write data door (L3-1) and the schema door's seed phase (L3-2, e.g. predefined
// values) share ONE codec, so a write and its DDL can never disagree on the field shape.
// This header stays light: the persisted variant tag (ibFieldTypes) is an implementation
// detail of the .cpp, not exposed here. (docs/query-language-arc.md §22.4b)
// ==========================================================================
class BACKEND_API ibColumnCodec
{
public:
	// WRITE — bind the value's TYPE tag + per-contained-type data + the reference pair into
	// `statement` from `position` (1-based), advancing it. The physical order is exactly the
	// one DescribeColumnLayout lays out. Binds into the L2 ibQueryStatement, never a raw L1
	// statement. Column-based — no attribute.
	static void WriteValue(const ibBackendQueryColumn* col, const ibMetaData* metaData,
	                       const ibValue& value, ibQueryStatement* statement, int& position);
	static void WriteValue(const ibBackendQueryColumn* col, const ibMetaData* metaData,
	                       const ibValue& value, ibQueryStatement* statement);   // from position 1

	// READ — reassemble the value off the column's physical fields in `result`. The col-only
	// overload reads the column's own physical name; the fieldName overload reads a given base
	// (a dot-walk alias prefix). Reference / enum reconstruction uses the metadata context.
	static bool ReadValue(const ibBackendQueryColumn* col, const ibMetaData* metaData,
	                      ibValue& retValue, ibQueryResult& result, bool createData = true);
	static bool ReadValue(const wxString& fieldName, const ibBackendQueryColumn* col, const ibMetaData* metaData,
	                      ibValue& retValue, ibQueryResult& result, bool createData = true);

	// READ ONE physical field of a KNOWN variant type (the read leaf). `fieldType` is the persisted
	// variant tag (ibValueMetaObjectAttributeBase::ibFieldTypes) passed as a plain int, so this
	// header need not pull the heavy attribute header in. Used directly by callers that already
	// know a field's type (a register reading a numeric balance/turnover column).
	static bool ReadField(const wxString& fieldName, int fieldType, const ibBackendQueryColumn* col,
	                      const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData = true);

	// Does the column's type admit a reference value — its spread carries _RTRef/_RRRef?
	// Pure clsid-kind gate (IsReference), no metadata.
	static bool HasReference(const ibBackendQueryColumn* col);

	// (The BINARY-WIRE codec — BinaryToStatement / BinaryFromResult — is the L3-3 dump / restore
	//  PRIMITIVE; it lives with its consumer in query/dataMover.h (ibDataMover), not here. It reuses
	//  this codec's HasReference + the shared spread driver, so the two stay byte-identical.)

private:
	// The tag switch itself, WITHOUT the "no such field" guard ReadValue wraps it in. The guard is
	// what turns a missing field into an answer the caller can act on, so the unguarded shape is not
	// offered: asking for it would be asking for the failure the guard exists to prevent.
	static bool ReadTaggedValue(const wxString& fieldName, const ibBackendQueryColumn* col, const ibMetaData* metaData,
	                            ibValue& retValue, ibQueryResult& result, bool createData);
};

#endif // !__COLUMN_LAYOUT_H__
