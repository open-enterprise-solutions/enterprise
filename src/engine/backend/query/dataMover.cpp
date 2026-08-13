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

// The leading row-key column (records / tabular sections) — asked of the layout tier, the one place
// it is named. See ibRowKeyField.
const wxString& kUuid = ibRowKeyField();

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
// row-key scaffold and the declared SEED are NOT data the mover writes.
std::vector<const ibBackendQueryColumn*> ColumnsOf(const ibSchemaTable& table)
{
	std::vector<const ibBackendQueryColumn*> columns;
	columns.reserve(table.m_columns.size());
	for (const ibSchemaColumn& c : table.m_columns)
		columns.push_back(c.m_column);
	return columns;
}

// The dumped + matched ROW KEY of a table, read off the structure: the "uuid" string scaffold
// (records / tabular sections), or — for an EXTERNAL single-row table (sys_const) — its primary-key
// column (RECORD_KEY). `unique` => the key uniquely identifies a row, so restore UPSERTs by it (the
// mutable main record; sys_const); a non-unique key (a section's repeating owner uuid) or no key (a
// register's composite identity) INSERTs. Empty name => no key column is dumped at all.
struct KeyInfo { wxString name; bool unique = false; };

KeyInfo KeyOf(const ibSchemaTable& table)
{
	// 1) the "uuid" string row-key (records / tabular sections) — UNIQUE iff a unique index covers it.
	for (const ibBackendQueryColumn* s : table.m_scaffold)
		if (s != nullptr && s->GetPhysicalName() == kUuid) {
			KeyInfo k{ kUuid, false };
			for (const ibSchemaIndex& idx : table.m_indexes)
				if (idx.m_unique) { k.unique = true; break; }
			return k;
		}
	// 2) an EXTERNAL single-row table (sys_const) keys on its primary-key column (RECORD_KEY) — a PK is
	//    unique by definition, so the one row is UPSERTed in place (no Update mode, no pre-seeded row).
	if (table.m_external && table.m_queryable != nullptr) {
		const std::vector<const ibBackendQueryColumn*> pk = table.m_queryable->GetPrimaryKeyColumns();
		if (pk.size() == 1 && pk[0] != nullptr)
			return { pk[0]->GetPhysicalName(), true };
	}
	// 3) no row key — append-only (a register: its composite identity lives in the dimension columns).
	return {};
}

// --- the row loops (the wire I/O; structure-extraction lives in the public Dump/Restore) ---------

bool RunDump(const wxString& tableName, const ibMetaData* metaData,
	const std::vector<const ibBackendQueryColumn*>& columns, const wxString& keyColumn, ibWriterMemory& out)
{
	// L2 read (ibQueryResult, RAII cursor) — SELECT * via the door on db_query's holder.
	ibDatabaseQueryBuilder q(db_query->GetHolder());
	ibQueryResult result = q.From(tableName).Execute();

	unsigned int row = 0;
	while (result.Next()) {

		ibWriterMemory rowWriter;

		// Sub-chunk 0 — the row KEY string (uuid / RECORD_KEY); absent when the table has no key.
		if (!keyColumn.empty()) {
			ibWriterMemory keyWriter;
			keyWriter.w_stringZ(result.GetResultString(keyColumn));
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
	const std::vector<const ibBackendQueryColumn*>& columns, const wxString& keyColumn, bool upsert,
	const ibReaderMemory& rows)
{
	// Build the bind layout once: the statement column list (the key column first when present, then
	// each column's physical field spread) + the column-id -> (start position, column) maps the restore
	// loop binds through. Positions are 1-based; the key (when present) takes position 1.
	const bool withKey = !keyColumn.empty();
	std::vector<wxString> columnNames;
	if (withKey)
		columnNames.push_back(keyColumn);

	std::map<ibMetaID, int>                        positionOf;
	std::map<ibMetaID, const ibBackendQueryColumn*> columnOf;
	int position = withKey ? 2 : 1;
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
		upsert ? std::vector<wxString>{ keyColumn } : std::vector<wxString>{},
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

			// col 0 = the row uuid (when present); col > 0 = the column keyed by its id.
			const ibBackendQueryColumn* column = (col > 0) ? columnOf[col] : nullptr;
			while (!colReader->eof()) {
				if (col > 0) {
					wxASSERT(column);
					int pos = positionOf[col];
					ibDataMover::BinaryToStatement(column, metaData, *colReader, &statement, pos);
				}
				else {
					statement.SetParamString(1, colReader->r_stringZ());
				}
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
	return RunDump(table.m_name, MetaOf(table), ColumnsOf(table), KeyOf(table).name, out);
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
	return RunRestore(table.m_name, MetaOf(table), ColumnsOf(table), key.name, key.unique, rows);
}

// ==========================================================================
// Binary-wire codec — the per-cell dump / restore primitive (was ibColumnCodec::Binary*). Spread a
// column's value between the binary wire and an L2 statement / cursor. The SAME physical spread + tag
// as the value codec (ibColumnSpread::DriveSpread + ibColumnCodec::HasReference are shared), so a
// dumped cell restores byte-identically; only the value SOURCE differs (the wire, not an ibValue).
// ==========================================================================

void ibDataMover::BinaryToStatement(const ibBackendQueryColumn* col, const ibMetaData* /*metaData*/,
	const ibReaderMemory& reader, ibQueryStatement* statement, int& position)
{
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
		if (!n.GetBuffer(writer))
			wxLogError(wxT("ibDataMover: failed to write TYPE_NUMBER to stream"));
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
