#ifndef __COLUMN_CODEC_H__
#define __COLUMN_CODEC_H__

// The VALUE codec — an ibValue spread across a column's physical fields and reassembled from a cursor.
// It lives with the providers because a value is assembled with the metadata (a reference is rebuilt by
// its class's ctor, an enumeration by its metaobject), and in query/ the metadata is the provider's alone
// (docs/development.md section 2). The fields it fills are the layout's (columnLayout.h): the shape and
// the values stay one decision, made in two files.

#include "backend/query/columnLayout.h"                    // the layout the codec fills — DescribeColumnLayout, ibColumnRole
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibQueryStatement / ibQueryResult (L2) — the codec binds/reads through them

class ibMetaData;
class ibValue;             // codec currency — named only by reference in the codec signatures below
// ibQueryStatement / ibQueryResult are full types from databaseQueryBuilder.h (included above) — the
// binary codec binds/reads through the L2 statement/cursor, not raw L1 (ibPreparedStatement/-ResultSet).

// ⭐ A REFERENCE READ FROM ONE KEY FIELD, with the target type supplied by the COLUMN.
//
// The ordinary reference is a PAIR — `_RTRef` says which type, `_RRRef` which row — because the
// value may point at several kinds. A single-target column cannot: a tabular section belongs to one
// owner, so the type is a property of the table and storing it per row would repeat one constant on
// every line. The bytes are the identity; the type is metadata.
//
// It lives HERE, with the rest of the value <-> field codec, and not at the call site: assembling a
// reference needs the reference class, which only the provider may reach for.
BACKEND_API ibValue ReadSingleTargetReference(const ibMetaData* metaData, const ibClassID& target,
                                              const ibMemoryBuffer& keyBytes);

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
BACKEND_API void BindWriteValue(class ibQueryStatement& statement, const ibBackendQueryColumn& col,
                                const ibMetaData* metaData, const ibValue& value, int& position);

// ==========================================================================
// ibColumnCodec — the VALUE codec: the inverse pair that spreads an ibValue across a
// column's physical fields (write) and reassembles it from a result cursor (read). The
// SAME "magic" as the layout (columnLayout.h): a value bound by WriteValue lands in
// exactly the fields DescribeColumnLayout creates, in the same order. STATIC (stateless) —
// the read/write data door (L3-1) and the schema door's seed phase (L3-2, e.g. predefined
// values) share ONE codec, so a write and its DDL can never disagree on the field shape.
// This header stays light: the persisted variant tag (ibFieldTypes) is an implementation
// detail of the .cpp, not exposed here. (docs/private/query-language-arc.md §22.4b)
// ==========================================================================
class BACKEND_API ibColumnCodec
{
public:
	// WRITE — bind the value's TYPE tag + per-contained-type data + the reference pair into
	// `statement` from `position` (1-based), advancing it. The physical order is exactly the
	// one DescribeColumnLayout lays out. Binds into the L2 ibQueryStatement (a batch's rows too —
	// ibBatchInsert is one), never a raw L1 statement. Column-based — no attribute.
	static void WriteValue(const ibBackendQueryColumn& col, const ibMetaData* metaData,
	                       const ibValue& value, ibQueryStatement* statement, int& position);
	static void WriteValue(const ibBackendQueryColumn& col, const ibMetaData* metaData,
	                       const ibValue& value, ibQueryStatement* statement);   // from position 1

	// READ — reassemble the value off the column's physical fields in `result`. The col-only
	// overload reads the column's own physical name; the fieldName overload reads a given base
	// (a dot-walk alias prefix). Reference / enum reconstruction uses the metadata context.
	//
	// 🛑⭐⭐ `createData` DECIDES WHETHER A REFERENCE GOES AND FETCHES ITS ROW, and it defaults to NO.
	// A query column carries a reference's IDENTITY - its type and its guid - and that is a whole
	// answer; whoever later asks the reference what it IS pays for the read, which is exactly what
	// ibReferenceLoad::OnDemand was made the default for in August ("we made every reference read
	// nothing by default", reference.h).
	//
	// ⚠ IT DEFAULTED TO TRUE UNTIL 2026-09-06, AND THAT UNDID THE OTHER DEFAULT ENTIRELY. One rule
	// decided in two places: the reference said "read nothing", this door said "read it", and this
	// door is the one every query result comes through. What it cost is not a constant factor -
	// reading a row materialises ITS reference fields, which fetch THEIR rows, so one cell of a list
	// pulled a tree of objects with a SELECT at every node. MEASURED on a grid scroll: reading one
	// value spent almost all of itself building references - `ibColumnCodec::ReadField` 1226 samples,
	// of which `ibValueReferenceDataObject::Create` 1179 - and the descent ran at least three levels
	// deep, stopping there only because a cycle guard cuts it (reference.cpp) - a guard written to
	// save the STACK, not the database.
	//
	// A caller that truly needs the row NOW says so, in one word, at its own call site.
	static bool ReadValue(const ibBackendQueryColumn& col, const ibMetaData* metaData,
	                      ibValue& retValue, ibQueryResult& result, bool createData = false);
	static bool ReadValue(const ibString& fieldName, const ibBackendQueryColumn& col, const ibMetaData* metaData,
	                      ibValue& retValue, ibQueryResult& result, bool createData = false);

	// READ ONE physical field of a KNOWN variant type (the read leaf). `fieldType` is the persisted
	// variant tag (ibValueMetaObjectAttributeBase::ibFieldTypes) passed as a plain int, so this
	// header need not pull the heavy attribute header in. Used directly by callers that already
	// know a field's type (a register reading a numeric balance/turnover column).
	static bool ReadField(const ibString& fieldName, int fieldType, const ibBackendQueryColumn& col,
	                      const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData = false);

	// Does the column's type admit a reference value — its spread carries _RTRef/_RRRef? A column of no
	// type does (the widest composite). Pure clsid-kind gate (IsReference), no metadata.
	static bool HasReference(const ibBackendQueryColumn& col);

	// (The BINARY-WIRE codec — BinaryToStatement / BinaryFromResult — is the L3-3 dump / restore
	//  PRIMITIVE; it lives with its consumer in query/provider/database/structure/dataMover.h (ibDataMover), not here. It reuses
	//  this codec's HasReference + the shared spread driver, so the two stay byte-identical.)

private:
	// The tag switch itself, WITHOUT the "no such field" guard ReadValue wraps it in. The guard is
	// what turns a missing field into an answer the caller can act on, so the unguarded shape is not
	// offered: asking for it would be asking for the failure the guard exists to prevent.
	static bool ReadTaggedValue(const ibString& fieldName, const ibBackendQueryColumn& col, const ibMetaData* metaData,
	                            ibValue& retValue, ibQueryResult& result, bool createData);
};

#endif // !__COLUMN_CODEC_H__
