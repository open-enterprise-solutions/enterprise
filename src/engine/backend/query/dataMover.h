#ifndef __DATA_MOVER_H__
#define __DATA_MOVER_H__

// L3-3 — the DATA mover (the "rows"). Dumps / restores ONE physical table's rows to / from the
// binary wire, driven ENTIRELY by the L3-2 STRUCTURE (an ibSchemaTable — the SAME object the DDL
// differ consumes) + the per-cell codec below. It knows NOTHING about metadata object types: the
// schema table already carries the physical name, the value columns, the row key and the metadata
// context, so the mover SELECTs the rows (dump) / UPSERTs them (restore) straight off it. ONE
// source of truth: a metaobject declares its tables once in ContributeTables, and that single
// declaration drives both the DDL (L3-2) and the data move (L3-3).
//
// Mode is read off the structure: a table with a "uuid" scaffold dumps it as the row key; a UNIQUE
// index on that key means restore UPSERTs (the mutable main record), else it INSERTs (tabular
// sections / registers — append into a freshly created table). A table with no value columns (a
// pure scaffold / seed table, e.g. an enum whose values are SEED rows) is skipped — nothing to move.
//
// Wire format: the rows blob is a chunk per row (id = a running index); each row is a chunk per
// cell — sub-chunk 0 (when the table has a uuid key) is the row's uuid string, and sub-chunk = a
// column's id carries that column's codec output. The caller frames the per-table blob by table id.

#include "backend.h"

struct ibSchemaTable;     // L3-2 structure — the mover's single input (query/schemaSnapshot.h)
class ibBackendQueryColumn;
class ibMetaData;
class ibReaderMemory;
class ibWriterMemory;
class ibQueryStatement;   // L2 statement — the wire codec binds through it (restore)
class ibQueryResult;      // L2 cursor — the wire codec reads through it (dump)

namespace ibDataMover {

// --- the binary-wire CODEC (the per-cell dump / restore PRIMITIVE) ------------------------------
// Spread ONE column's value between the binary wire (ibReaderMemory / ibWriterMemory) and an L2
// statement / cursor — the SAME physical field spread + variant tag as the value codec
// (ibColumnCodec::WriteValue / ReadValue), but at the serialization level. The mover's row loops
// and the constant's single-cell dump are its only callers. Shares ibColumnSpread::DriveSpread +
// ibColumnCodec::HasReference with the value codec, so a dumped cell restores byte-identically.

// WRITE — read the wire TYPE tag + the active value, bind the column's full physical spread into
// `statement` from `position` (1-based), advancing it.
BACKEND_API void BinaryToStatement(const ibBackendQueryColumn* col, const ibMetaData* metaData,
                                   const ibReaderMemory& reader, ibQueryStatement* statement, int& position);
BACKEND_API void BinaryToStatement(const ibBackendQueryColumn* col, const ibMetaData* metaData,
                                   const ibReaderMemory& reader, ibQueryStatement* statement);   // from position 1

// READ — write the column's compact wire form (tag + only the active type's value + reference pair)
// off the row in `result`.
BACKEND_API void BinaryFromResult(const ibBackendQueryColumn* col, const ibMetaData* metaData,
                                  ibWriterMemory& writer, ibQueryResult& result);

// Dump every row of `table`'s physical table (SELECT * through the L2 door on db_query's holder)
// into `out` as the per-row chunk blob described above. Reads the table name / value columns / uuid
// key off the structure. A table with no value columns is a no-op. Returns false on read error.
BACKEND_API bool Dump(const ibSchemaTable& table, ibWriterMemory& out);

// Restore the per-row chunk blob in `rows` (already unwrapped from the caller's framing chunk) into
// `table` — UPSERT when the structure has a unique key, else INSERT — binding each cell through the
// codec. Returns false on a write error (the caller rolls the transaction back).
BACKEND_API bool Restore(const ibSchemaTable& table, const ibReaderMemory& rows);

} // namespace ibDataMover

#endif // !__DATA_MOVER_H__
