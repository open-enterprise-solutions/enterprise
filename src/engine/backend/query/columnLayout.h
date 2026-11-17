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
// an L3 ibBackendQueryColumn and nothing else — no metadata. This is the one home
// for the "magic" column spread that used to be duplicated across the attribute
// metaobject, the provider codec and the DDL builder; the VALUES that fill these
// fields are the codec's, provider/database/columnCodec.h, because assembling a value needs
// the metadata. (docs/private/query-language-arc.md §22.4b)

#include "backend/databaseLayer/columnType.h"             // ibColumnType (shared canonical type — ibColumnSlot carries one)
#include "backend/query/queryColumn.h"                     // ibBackendQueryColumn

// (ibColumnRole / ibColumnSlot moved to queryColumn.h — they describe a COLUMN's field, and the
//  column answers with them (ibBackendQueryColumn::DescribeLayout). Declaring them here left the
//  column having to name the struct it returns without being able to see it.)

// The ONE role -> physical-suffix table. Every layer that names a composite column's physical fields
// (the layout below, the read keyset anchor, the DDL builder, the attribute field machinery) spells the
// suffix through this, so the lettering lives in exactly one map and can never disagree between writers.
// Raw -> empty (the column is its own field, no suffix).
BACKEND_API const ibString& ibFieldSuffix(ibColumnRole role);

// ⭐⭐ WHAT THE STATEMENT CALLS AN OUTPUT — as opposed to what the AUTHOR calls it.
//
// An output is named by the person who wrote the query (`… AS Date`), but where that name is
// WRITTEN is inside a projection of ours — a nested select, a declared query's body — and names
// there belong to that projection's namespace. So the engine spells them: `Date` is written
// `out_` and the hash of `Date`, as is every other name, Latin or not. `ibQueryOutputName`
// (queryRender.h) answers what the QUERY calls a field; this answers what the STATEMENT does.
//
// ⭐ EVERY ALIAS, NOT THE AWKWARD ONES. The prefix is a namespace, so it is unconditional; that a
// prefixed word can no longer collide with SQL's vocabulary (`AS Date` -> `-104 Token unknown`,
// Firebird, measured 2026-08-31) falls out of that rather than motivating it. Conditional escaping
// was the first version and was worse in a way that matters: it left this split exercised almost
// never, so a reader that forgot to ask here would work everywhere but one query and fail there
// SILENTLY — an unmatched name reads as an absent column, not as an error. Unconditional makes that
// mistake break on the first query instead.
//
// ⚠ NOT SOLVED BY QUOTING EITHER. Identifier quotes are off by decision, not omission — in Firebird
// a quoted name becomes case-sensitive while the schema is written unquoted (m_identQuoteOpen,
// databaseLayer.h), so quoting trades a refusal for a name that no longer matches itself. The hash
// keeps the statement ASCII for a name that is not an identifier (Cyrillic, a hyphen), and one
// spelling for every name keeps that road the one every query takes.
//
// ⭐ THE AUTHOR'S NAME SURVIVES UNTOUCHED wherever a person reads it — the composition's title, the
// constructor's cell, the column header, and the name an outer query calls this output by. Only the
// spelling inside the statement moves.
//
// ⭐ ASKED AT BOTH ENDS. Whatever writes `AS <x>` and whatever reads that column back off the cursor
// go through here, so writer and reader cannot drift apart.
BACKEND_API ibString ibSqlAliasOf(const ibString& outputName);

// ⭐ IS THIS A PLAIN SCALAR — one type, and that type a number, a string, a date or a boolean. Such a value is
// ONE field of a result and is read by its name; anything else (a reference, an enum, a composite) comes back
// as a spread reassembled under a prefix. Every reader and writer that makes that split asks here, so the
// projection and the read of one output cannot disagree about which kind it is.
BACKEND_API bool ibIsPlainScalarType(const ibTypeDescription& type);

// ⭐ …AND DOES ITS ONE FIELD HAND IT BACK AS WHAT IT IS. A number, a string and a date come back from their field as
// themselves. A boolean is kept in an integer field and comes back as 1 or 0: a walked leaf read by its name answered
// `1` for `Ref.Posted` and `Account.OffBalance` on every road that reads a walk that way (measured 2026-09-17), so a
// report comparing it with TRUE compared a number. Such a value travels as a spread under a prefix, the way a
// reference does, and the column's own read turns it back. Asked wherever a walk is PROJECTED to be read back — the
// comparison and the typed empty keep asking ibIsPlainScalarType, since a field compares fine as the number it is.
BACKEND_API bool ibReadsBackAsItself(const ibTypeDescription& type);

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
BACKEND_API const ibString& ibOwnerRefField();

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
BACKEND_API std::shared_ptr<const ibBackendColumnRawDB> ibOwnerRefColumn();

// The authority. Decompose a logical column into its physical fields, derived purely
// from (physical name, type descriptor) — NO metadata: the reference slot is gated by the
// clsid KIND (IsReference), not a metadata lookup. Order: TYPE, B, N, D, S, E, RTRef, RRRef
// — the same fixed order the write / read codec binds in. A raw column = one slot as-is.
BACKEND_API std::vector<ibColumnSlot> DescribeColumnLayout(const ibBackendQueryColumn& col);

// Do two physical fields render to the SAME SQL type? The whole canonical L2 type, qualifiers
// included — so it answers exactly "would a DDL writer have to touch this field", and nothing wider.
//
// Two floors ask it about the same pair of slots and act oppositely, which is why it is one function
// and not two: the column diff ALTERs the field when this is false, and a DERIVED table (whose key
// is its PRIMARY KEY) must instead be REBUILT, because no engine alters a column an index stands on.
BACKEND_API bool ibSameFieldType(const ibColumnType& a, const ibColumnType& b);

// Whether a role CARRIES the column's value, as opposed to TAGGING what the value is (_TYPE, and a
// reference's _RTRef). This is the one split ColumnValueFields filters by, named so a consumer can
// ask it of a slot it already holds.
BACKEND_API bool ibIsValueRole(ibColumnRole role);

// A column's VALUE field names — its layout slots minus the _TYPE discriminator and the reference
// TYPE id (the value-carrying roles). Metadata-free, derived over DescribeColumnLayout. The DB
// provider asks this for sort / group-by / key field expansion; the column stays a pure descriptor.
BACKEND_API std::vector<ibString> ColumnValueFields(const ibBackendQueryColumn& col);

// ⭐ THE SAME FIELDS, WITH THEIR ROLE STILL ATTACHED. A consumer that must know what a field IS —
// a reference id needs its _RTRef tiebreak, a counter scan wants the number field — reads m_role.
// It exists because the suffix lettering kept escaping this tier: whoever got names only had no way
// left to ask the question except to match the tail of the string (`EndsWith("_RRRef")`), which is
// the layout re-derived by spelling, in a place that cannot be told when the lettering changes.
// Give the role alongside the name and the question is answerable where it is asked.
BACKEND_API std::vector<ibColumnSlot> ColumnValueSlots(const ibBackendQueryColumn& col);

// ⭐ WHAT A COLUMN IS SORTED ON, in order: its value fields, and a reference's _RTRef after its key (an
// empty key of one type orders apart from an empty key of another). One answer for the ORDER BY a read
// writes and for the fields an index is built over — an index whose leading fields are not the order's
// own is one the engine cannot walk, and every page of a list sorts the whole table instead.
BACKEND_API std::vector<ibString> ColumnSortFields(const ibBackendQueryColumn& col);

// The column's FIRST value slot — the field a scalar comparison / sort / anchor rides. Empty name
// and Raw role when the column spreads to nothing.
BACKEND_API ibColumnSlot FirstValueSlot(const ibBackendQueryColumn& col);

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
BACKEND_API std::vector<ibString> ColumnFieldNames(const ibBackendQueryColumn& col);
BACKEND_API ibString       ColumnFieldList(const ibBackendQueryColumn& col, const ibString& aggr = ibEmptyString);
BACKEND_API ibString       ColumnComparePredicate(const ibBackendQueryColumn& col, const ibString& cmp = ibT("="));

#endif // !__COLUMN_LAYOUT_H__
