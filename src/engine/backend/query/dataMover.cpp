////////////////////////////////////////////////////////////////////////////
//	Description : ibDataMover — L3-3 row mover. The generic per-table dump /
//	              restore that used to be copy-pasted across the record /
//	              register metaobjects, now driven purely by the L3-2 STRUCTURE
//	              (ibSchemaTable) + the L3 column codec. See query/dataMover.h.
////////////////////////////////////////////////////////////////////////////

#include "backend/query/dataMover.h"

#include "backend/appData.h"                              // db_query (the L2 door's holder)
#include "backend/databaseLayer/databaseLayer.h"          // ibDatabaseLayer::GetHolder
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibDatabaseQueryBuilder / ibQueryStatement / ibQueryResult (L2)
#include "backend/query/columnLayout.h"                   // ColumnFieldNames + ibColumnCodec::HasReference + ibFieldSuffix / ibPersistedTypeTag
#include "backend/query/columnSpread.h"                   // ibColumnSpread::DriveSpread — shared role-spread binding
#include "backend/query/queryColumn.h"                    // ibBackendQueryColumn::GetColumnId / GetPhysicalName
#include "backend/query/schemaSnapshot.h"                 // ibSchemaTable / ibSchemaColumn / ibSchemaIndex — the mover's input
#include "backend/query/queryable.h"                      // ibBackendQueryable::GetMetaData
#include "backend/typeDescription.h"                       // ibTypeDescription::ContainType (the wire codec gates on it)
#include "backend/fnumber.h"                               // ibNumber (the wire codec round-trips it)
#include "backend/fileSystem/fs.h"                        // ibReaderMemory / ibWriterMemory (the binary wire)

#include <map>

namespace {

// The tabular section's OWNER-reference column — asked of the layout tier, the one place
// it is named. See ibOwnerRefField.
const wxString& kOwnerRef = ibOwnerRefField();

// The reference-blob wire chunk id — the _RTRef/_RRRef pair's payload tag in the dump stream. A
// FORMAT constant (was the rt_ref_chunk macro in metaObject.h): must stay 0x800060 for dump compat.
const u64 rt_ref_chunk = 0x800060;

// The schedule-blob wire chunk id — the _SCH field's payload tag in the dump stream. A new id
// beside the reference one; a dump written before schedules existed simply never carries it.
const u64 schedule_chunk = 0x800061;

// The type-description-blob wire chunk id — the _TD field's payload tag, next in the same series.
// The mover is what carries data ACROSS a restructuring: a role the layout emits and the wire codec
// skips is not merely unwritten, it desynchronises every cell after it in the dump.
const u64 type_desc_chunk = 0x800062;

// --- structure -> mover parameters ---------------------------------------------------------------

// The metadata context — off the table's queryable (every column of the table shares it).
const ibMetaData* MetaOf(const ibSchemaTable& table)
{
	return table.m_queryable != nullptr ? table.m_queryable->GetMetaData() : nullptr;
}

// The value columns to MOVE — the schema's logical columns (the same set the DDL differ diffs). The
// owner-reference scaffold and the declared SEED are NOT data the mover writes.
std::vector<const ibBackendQueryColumn*> ColumnsOf(const ibSchemaTable& table)
{
	std::vector<const ibBackendQueryColumn*> columns;
	columns.reserve(table.m_columns.size());
	for (const ibSchemaColumn& c : table.m_columns)
		columns.push_back(c.m_column);
	return columns;
}

// The COLUMN a dumped row is matched by, read off the structure: the column a single-column UNIQUE
// index covers (a reference object's own reference; sys_const's RECORD_KEY), else the tabular
// section's owner reference. `unique` => it identifies a row on its own, so the restore UPSERTs by it;
// a non-unique key (a section's owner, repeated per line) or none at all (a register's composite
// identity, spread across its dimensions) INSERTs.
//
// ⭐⭐ THE COLUMN, NOT ITS NAME — and that distinction is the whole of the bug this shape used to have.
// A name is what a column is called; what a table HAS are the fields the column spreads into. While
// every key was a raw single-field scaffold the two were the same string, so the mover carried the
// name and read it back with GetResultString. A reference key spreads into three (_TYPE / _RTRef /
// _RRRef) and its bare name is a field NOTHING has, so the very first dump of a catalog died on
// "Field 'fld1009' not found in the resultset" — while the same three fields were being read
// perfectly well one loop below, as an ordinary column.
struct KeyInfo { const ibBackendQueryColumn* col = nullptr; bool unique = false; };

KeyInfo KeyOf(const ibSchemaTable& table)
{
	// ⭐ THE KEY IS WHAT A UNIQUE INDEX COVERS, whichever column that is. This used to look for the
	// scaffold column BY NAME, which was true while every reference object carried one beside its
	// reference. Identified by its REFERENCE, such a table has no scaffold at all - and a key lookup
	// that found nothing did not fail: the restore fell to append-only and duplicated every row it
	// was supposed to update.
	for (const ibSchemaIndex& idx : table.m_indexes) {
		if (!idx.m_unique || idx.m_columns.size() != 1 || idx.m_columns.front() == nullptr)
			continue;   // a COMPOSITE unique key is not a single-column row identity
		return { idx.m_columns.front(), true };
	}

	// The tabular section's OWNER reference: present, repeated per owner, so NOT unique - it still names the
	// column rows are matched by when one exists.
	for (const ibBackendQueryColumn* s : table.m_scaffold)
		if (s != nullptr && s->GetPhysicalName() == kOwnerRef)
			return KeyInfo{ s, false };
	// 2) an EXTERNAL single-row table (sys_const) keys on its primary-key column (RECORD_KEY) — a PK is
	//    unique by definition, so the one row is UPSERTed in place (no Update mode, no pre-seeded row).
	if (table.m_external && table.m_queryable != nullptr) {
		const std::vector<const ibBackendQueryColumn*> pk = table.m_queryable->GetPrimaryKeyColumns();
		if (pk.size() == 1 && pk[0] != nullptr)
			return { pk[0], true };
	}
	// 3) nothing to match on — append-only (a register: its composite identity lives in the dimension columns).
	return {};
}

// --- the row loops (the wire I/O; structure-extraction lives in the public Dump/Restore) ---------

// Does the key ride as one of the columns already? A reference object's key IS its reference, which
// the mover moves like any other column — dumping it a second time as a "key" would put the same
// value on the wire twice and, being written by a different mechanism than it is read by, differently.
// A tabular section's owner reference and sys_const's RECORD_KEY are scaffold: they are NOT in the
// column list, so they do need a chunk of their own.
bool RidesAsColumn(const std::vector<const ibBackendQueryColumn*>& columns, const ibBackendQueryColumn* key)
{
	return key != nullptr && std::find(columns.begin(), columns.end(), key) != columns.end();
}

bool RunDump(const wxString& tableName, const ibMetaData* metaData,
	const std::vector<const ibBackendQueryColumn*>& columns, const ibBackendQueryColumn* keyColumn, ibWriterMemory& out)
{
	// L2 read (ibQueryResult, RAII cursor) — SELECT * via the door on db_query's holder.
	ibDatabaseQueryBuilder q(db_query->GetHolder());
	ibQueryResult result = q.From(tableName).Execute();

	unsigned int row = 0;
	while (result.Next()) {

		ibWriterMemory rowWriter;

		// Sub-chunk 0 — the key of a row whose key is NOT one of its columns (a section's owner, a
		// constant's RECORD_KEY). Written through the SAME codec as every cell: it used to go as a
		// STRING (GetResultString / SetParamString), which is identity travelling as text — the guid
		// blob read as characters on the way out and bound as characters on the way back in.
		if (keyColumn != nullptr && !RidesAsColumn(columns, keyColumn)) {
			ibWriterMemory keyWriter;
			ibDataMover::BinaryFromResult(keyColumn, metaData, keyWriter, result);
			rowWriter.w_chunk(0, keyWriter.buffer());
		}

		// Sub-chunk = column id — that column's codec output.
		for (const ibBackendQueryColumn* col : columns) {
			ibWriterMemory cellWriter;
			ibDataMover::BinaryFromResult(col, metaData, cellWriter, result);
			rowWriter.w_chunk(col->GetColumnId(), cellWriter.buffer());
		}

		out.w_chunk(row++, rowWriter.buffer());
	}

	return true;
}

bool RunRestore(const wxString& tableName, const ibMetaData* metaData,
	const std::vector<const ibBackendQueryColumn*>& columns, const ibBackendQueryColumn* keyColumn, bool upsert,
	const ibReaderMemory& rows)
{
	// Build the bind layout once: the statement column list (the separate key first when there is one,
	// then each column's physical field spread) + the column-id -> (start position, column) maps the
	// restore loop binds through. Positions are 1-based.
	//
	// The key takes the front ONLY when it does not ride as a column — see RidesAsColumn. Its fields are
	// its SPREAD, never its bare name: a reference key occupies three of them, and naming one produced a
	// statement listing a field the table does not have.
	const bool withKey = keyColumn != nullptr && !RidesAsColumn(columns, keyColumn);
	const std::vector<wxString> keyFields = keyColumn != nullptr
		? ColumnFieldNames(keyColumn)
		: std::vector<wxString>();
	std::vector<wxString> columnNames;
	if (withKey)
		columnNames = keyFields;

	std::map<ibMetaID, int>                        positionOf;
	std::map<ibMetaID, const ibBackendQueryColumn*> columnOf;
	int position = withKey ? static_cast<int>(keyFields.size()) + 1 : 1;
	for (const ibBackendQueryColumn* col : columns) {
		const std::vector<wxString> fields = ColumnFieldNames(col);
		positionOf.insert_or_assign(col->GetColumnId(), position);
		columnOf.insert_or_assign(col->GetColumnId(), col);
		position += static_cast<int>(fields.size());
		for (const wxString& f : fields)
			columnNames.push_back(f);
	}

	// UPSERT on a unique key (the mutable main record / sys_const), else a plain INSERT.
	ibQueryStatement statement(upsert ? ibQueryStatement::Kind::Upsert : ibQueryStatement::Kind::Insert,
		tableName, columnNames,
		upsert ? keyFields : std::vector<wxString>(),
		db_query->GetHolder());

	ibReaderMemory* rowReaderPrev = nullptr;
	while (true) {

		ibReaderMemory* colReaderPrev = nullptr;

		u64 row = 0;
		ibReaderMemory* rowReader = rows.open_chunk_iterator(row, rowReaderPrev);
		if (rowReader == nullptr)
			break;

		while (!rowReader->eof()) {

			u64 col = 0;
			ibReaderMemory* colReader = rowReader->open_chunk_iterator(col, colReaderPrev);
			if (colReader == nullptr)
				break;

			// col 0 = the row's separate key (when there is one); col > 0 = the column keyed by its id.
			//
			// ⭐ A COLUMN THE FILE HAS AND THIS CONFIGURATION DOES NOT IS AN ORDINARY EVENT. Dump and
			// restore exist to move data BETWEEN databases, so the file was written by some other
			// configuration and may name a column since renamed, retired, or never present here. It
			// used to be `columnOf[col]` — a std::map subscript, which INSERTS a null for the missing
			// id and hands it on to be dereferenced. Its cell is skipped and said out loud instead: the
			// row still restores, minus a column nothing here could hold anyway.
			const auto known = (col > 0) ? columnOf.find(col) : columnOf.end();
			const ibBackendQueryColumn* column = (known != columnOf.end()) ? known->second : nullptr;

			// ⭐⭐ ONE CHUNK IS ONE CELL — read once, not "until the stream ends".
			//
			// This used to loop `while (!colReader->eof())`, which assumes every read CONSUMES something.
			// Most do; some legitimately do not — a value whose encoding is "write nothing" (zero) leaves
			// the cursor exactly where it was, and the loop then read the same nothing forever. That is
			// what a load hanging on a table of numbers was: not slow, not deadlocked, just a cell being
			// re-read until the end of time.
			//
			// The wire says one cell per chunk, so the reader says it too.
			if (col > 0) {
				if (column == nullptr) {
					wxLogError(wxT("ibDataMover: %s has no column with id %llu — its cell is skipped"),
						tableName, static_cast<unsigned long long>(col));
				}
				else {
					int pos = positionOf[col];
					ibDataMover::BinaryToStatement(column, metaData, *colReader, &statement, pos);
				}
			}
			else if (keyColumn != nullptr) {
				// The separate key, read back through the codec that wrote it (chunk 0 above). A file
				// written when the key rode BOTH ways carries this chunk for a table that no longer wants
				// one — there is nothing to bind it to, and the row's own column already has the value.
				int keyPosition = 1;
				ibDataMover::BinaryToStatement(keyColumn, metaData, *colReader, &statement, keyPosition);
			}

			colReaderPrev = colReader;
		}

		statement.RunQuery();
		rowReaderPrev = rowReader;
	}

	statement.Close();
	return true;
}

} // namespace

bool ibDataMover::Dump(const ibSchemaTable& table, ibWriterMemory& out)
{
	// DERIVED tables are never moved. Their rows are a function of a source table that IS moved,
	// so the destination regenerates them exactly — carrying them would ship a redundant copy at
	// best, and at worst a stale one that silently disagrees with the movements it claims to
	// summarise. "Don't move it, regenerate it" is the whole point of the derived bit.
	if (table.m_derived)
		return true;
	if (table.m_columns.empty())   // a pure scaffold / seed table (e.g. an enum) — no rows to move
		return true;
	return RunDump(table.m_name, MetaOf(table), ColumnsOf(table), KeyOf(table).col, out);
}

bool ibDataMover::Restore(const ibSchemaTable& table, const ibReaderMemory& rows)
{
	// Nothing was dumped for a derived table, so there is nothing to load. It comes back through
	// L3-4 regeneration once the source rows are in place — which is also the only order that can
	// be correct, since the totals are computed FROM those rows.
	if (table.m_derived)
		return true;
	if (table.m_columns.empty())
		return true;
	const KeyInfo key = KeyOf(table);
	return RunRestore(table.m_name, MetaOf(table), ColumnsOf(table), key.col, key.unique, rows);
}

// ==========================================================================
// Binary-wire codec — the per-cell dump / restore primitive (was ibColumnCodec::Binary*). Spread a
// column's value between the binary wire and an L2 statement / cursor. The SAME physical spread + tag
// as the value codec (ibColumnSpread::DriveSpread + ibColumnCodec::HasReference are shared), so a
// dumped cell restores byte-identically; only the value SOURCE differs (the wire, not an ibValue).
// ==========================================================================

// ⭐⭐ A RAW COLUMN HAS NO TAG TO READ, AND NO SPREAD TO DRIVE — it is one field carrying one kind,
// fixed by the column itself. The pair below opens with the _TYPE discriminator, which a raw column
// does not have, so everything raw had to travel by some other route: the mover carried its key as a
// STRING for exactly this reason. That put identity on the wire as text (a guid blob read out as
// characters and bound back as characters) and left the codec unable to move the one column every
// tabular section has.
//
// So the codec answers the same question the WRITE door already answers (BindWriteValue): raw goes
// straight by its declared RawType, metadata goes through the spread. One door, both kinds.
static bool RawFromResult(const ibBackendQueryColumn* col, ibWriterMemory& writer, ibQueryResult& result)
{
	if (!col->IsRawColumn())
		return false;
	const ibRawDBColumn* const raw = static_cast<const ibRawDBColumn*>(col);
	const wxString f = raw->GetPhysicalName();
	switch (raw->GetRawType()) {
	case ibRawDBColumn::RawType::Boolean: writer.w_u8(result.GetResultBool(f) ? 1u : 0u); break;
	case ibRawDBColumn::RawType::Number: {
		ibNumber n = result.GetResultNumber(f);
		n.GetBuffer(writer);
		break;
	}
	case ibRawDBColumn::RawType::Date:    writer.w_u64(result.GetResultDate(f).GetValue().GetValue()); break;
	case ibRawDBColumn::RawType::String:  writer.w_stringZ(result.GetResultString(f)); break;
	default: {   // Guid / Reference / Blob — bytes, as they are stored
		wxMemoryBuffer buffer;
		result.GetResultBlob(f, buffer);
		writer.w_chunk(rt_ref_chunk, buffer);
		break;
	}
	}
	return true;
}

static bool RawToStatement(const ibBackendQueryColumn* col, const ibReaderMemory& reader,
	ibQueryStatement* statement, int& position)
{
	if (!col->IsRawColumn())
		return false;
	const ibRawDBColumn* const raw = static_cast<const ibRawDBColumn*>(col);
	switch (raw->GetRawType()) {
	case ibRawDBColumn::RawType::Boolean: statement->SetParamBool(position++, reader.r_u8() != 0); break;
	case ibRawDBColumn::RawType::Number: {
		ibNumber value;
		if (!value.SetBuffer(reader))
			wxLogError(wxT("ibDataMover: failed to read a raw number from stream"));
		statement->SetParamNumber(position++, value);
		break;
	}
	case ibRawDBColumn::RawType::Date:    statement->SetParamDate(position++, wxLongLong(reader.r_u64())); break;
	case ibRawDBColumn::RawType::String:  statement->SetParamString(position++, reader.r_stringZ()); break;
	default: {
		wxMemoryBuffer buffer;
		reader.r_chunk(rt_ref_chunk, buffer);
		statement->SetParamBlob(position++, buffer.GetData(), buffer.GetDataLen());
		break;
	}
	}
	return true;
}

void ibDataMover::BinaryToStatement(const ibBackendQueryColumn* col, const ibMetaData* /*metaData*/,
	const ibReaderMemory& reader, ibQueryStatement* statement, int& position)
{
	if (RawToStatement(col, reader, statement, position))
		return;

	const int tag = reader.r_s32();

	// No metadata: the reference slot is gated by the clsid KIND (inside DriveSpread → HasReference).
	ibColumnSpread::DriveSpread(col, tag, statement, position,
		[&](ibColumnRole role, int& p) {   // ACTIVE primitive — the real value off the wire
			switch (role) {
			case ibColumnRole::Boolean: statement->SetParamBool(p++, reader.r_u8()); break;
			case ibColumnRole::Number: {
				ibNumber value;
				if (!value.SetBuffer(reader))
					wxLogError(wxT("ibDataMover: failed to read TYPE_NUMBER from stream"));
				statement->SetParamNumber(p++, value);
				break;
			}
			case ibColumnRole::Date:   statement->SetParamDate(p++, wxLongLong(reader.r_u64())); break;
			case ibColumnRole::String: statement->SetParamString(p++, reader.r_stringZ()); break;
			case ibColumnRole::Enum:   statement->SetParamInt(p++, reader.r_s32()); break;
			case ibColumnRole::Schedule: {
				// The schedule travels as its own blob chunk, the same shape the reference pair
				// uses below — one chunk, one cell, so a truncated stream is caught by the reader
				// rather than by the column that happens to be bound next.
				wxMemoryBuffer scheduleBuffer;
				reader.r_chunk(schedule_chunk, scheduleBuffer);
				statement->SetParamBlob(p++, scheduleBuffer.GetData(), scheduleBuffer.GetDataLen());
				break;
			}
			case ibColumnRole::TypeDescription: {
				// Same shape as the schedule one line up — its own chunk, so a truncated stream is
				// caught here rather than by whichever column binds next.
				wxMemoryBuffer typeDescBuffer;
				reader.r_chunk(type_desc_chunk, typeDescBuffer);
				statement->SetParamBlob(p++, typeDescBuffer.GetData(), typeDescBuffer.GetDataLen());
				break;
			}
			default:                                                                  break;
			}
		},
		[&](ibColumnRole role, int& p) {   // reference pair — consumed off the wire only when the value IS a reference
			if (tag == ibFieldTypes_Reference) {
				if (role == ibColumnRole::ReferenceType) {
					statement->SetParamNumber(p++, reader.r_u64());
				} else {
					wxMemoryBuffer typeRRBuffer;
					reader.r_chunk(rt_ref_chunk, typeRRBuffer);
					statement->SetParamBlob(p++, typeRRBuffer.GetData(), typeRRBuffer.GetDataLen());
				}
			} else if (role == ibColumnRole::ReferenceType) {
				statement->SetParamNumber(p++, 0);
			} else {
				statement->SetParamNull(p++);
			}
		});
}

void ibDataMover::BinaryToStatement(const ibBackendQueryColumn* col, const ibMetaData* metaData,
	const ibReaderMemory& reader, ibQueryStatement* statement)
{
	int position = 1;
	BinaryToStatement(col, metaData, reader, statement, position);
}

void ibDataMover::BinaryFromResult(const ibBackendQueryColumn* col, const ibMetaData* /*metaData*/,
	ibWriterMemory& writer, ibQueryResult& result)
{
	if (RawFromResult(col, writer, result))
		return;   // one field, its own kind — see RawFromResult

	const ibTypeDescription& td = col->GetTypeDesc();
	const wxString f = col->GetPhysicalName();
	const int tag = result.GetResultInt(f + ibFieldSuffix(ibColumnRole::Discriminator));

	// COMPACT wire (the inverse of BinaryToStatement): the tag, then ONLY the active type's value —
	// read off the row when the column actually carries that type, else a placeholder — then, for a
	// reference, the target id + the pure guid blob. Empty / Null write the tag alone.
	writer.w_s32(tag);

	switch (tag) {
	case ibFieldTypes_Boolean:
		writer.w_u8(td.ContainType(ibValueTypes::TYPE_BOOLEAN)
		            ? result.GetResultBool(f + ibFieldSuffix(ibColumnRole::Boolean)) : false);
		break;
	case ibFieldTypes_Number: {
		ibNumber n = td.ContainType(ibValueTypes::TYPE_NUMBER)
		           ? result.GetResultNumber(f + ibFieldSuffix(ibColumnRole::Number)) : ibNumber();
		n.GetBuffer(writer);   // always a chunk, zero included — see ibNumber::GetBuffer(ibWriterMemory&)
		break;
	}
	case ibFieldTypes_Date:
		if (td.ContainType(ibValueTypes::TYPE_DATE))
			writer.w_u64(result.GetResultDate(f + ibFieldSuffix(ibColumnRole::Date)).GetValue().GetValue());
		else
			writer.w_u64(emptyDate);
		break;
	case ibFieldTypes_String:
		writer.w_stringZ(td.ContainType(ibValueTypes::TYPE_STRING)
		                 ? result.GetResultString(f + ibFieldSuffix(ibColumnRole::String)) : wxString());
		break;
	case ibFieldTypes_Enum:
		writer.w_s32(td.ContainType(ibValueTypes::TYPE_ENUM)
		             ? result.GetResultInt(f + ibFieldSuffix(ibColumnRole::Enum)) : wxNOT_FOUND);
		break;
	case ibFieldTypes_Schedule: {
		wxMemoryBuffer scheduleBuffer;
		if (td.ContainType(g_valueScheduleCLSID))
			result.GetResultBlob(f + ibFieldSuffix(ibColumnRole::Schedule), scheduleBuffer);
		writer.w_chunk(schedule_chunk, scheduleBuffer);
		break;
	}
	case ibFieldTypes_TypeDescription: {
		wxMemoryBuffer typeDescBuffer;
		if (td.ContainType(g_valueTypeDescriptionCLSID))
			result.GetResultBlob(f + ibFieldSuffix(ibColumnRole::TypeDescription), typeDescBuffer);
		writer.w_chunk(type_desc_chunk, typeDescBuffer);
		break;
	}
	case ibFieldTypes_Reference:
		if (ibColumnCodec::HasReference(col)) {
			wxMemoryBuffer bufferData;
			result.GetResultBlob(f + ibFieldSuffix(ibColumnRole::ReferenceId), bufferData);
			writer.w_u64(result.GetResultLong(f + ibFieldSuffix(ibColumnRole::ReferenceType)));
			writer.w_chunk(rt_ref_chunk, bufferData);
		} else {
			writer.w_u64(0);
			writer.w_chunk(rt_ref_chunk, wxMemoryBuffer());
		}
		break;
	default:
		break;   // Empty / Null — the tag alone, no payload
	}
}
