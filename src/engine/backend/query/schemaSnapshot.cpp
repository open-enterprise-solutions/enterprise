#include "backend/query/schemaSnapshot.h"

#include "backend/query/structureBatch.h"                  // ibStructureBatch + DiffColumnInto
#include "backend/query/schemaBuilder.h"                   // ibSchemaBuilder — the DDL door + FB barrier
#include "backend/query/columnLayout.h"                    // ColumnFieldNames + ibColumnCodec::WriteValue (seed cell spread)
#include "backend/databaseLayer/databaseQueryBuilder.h"    // ibDropIndex / ibQueryStatement (the seed upsert + uuid delete)
#include "backend/restructureInfo.h"                        // ibRestructureInfo — the apply-change ledger
#include "backend/query/queryColumn.h"                     // ibBackendQueryColumn::GetName (friendly column name)
#include "backend/query/queryable.h"                        // ibBackendQueryable::GetQueryName / GetQueryTableName
#include "appData.h"                                        // db_query — the local channel the seed writes target

const ibSchemaTable* ibSchemaSnapshot::Find(ibMetaID id) const
{
	for (const ibSchemaTable& t : m_tables)
		if (t.m_id == id)
			return &t;
	return nullptr;
}

ibSchemaTable& ibSchemaSnapshot::CreateSchemaTable(const ibBackendQueryable* queryable)
{
	ibSchemaTable t;
	t.m_id        = queryable->GetQueryTableId();
	t.m_name      = queryable->GetQueryTableName();
	t.m_queryable = queryable;
	m_tables.push_back(std::move(t));
	return m_tables.back();
}

ibSchemaTable& ibSchemaSnapshot::Shared(ibMetaID id, const wxString& name)
{
	for (ibSchemaTable& t : m_tables)
		if (t.m_id == id)
			return t;
	ibSchemaTable t;
	t.m_id   = id;
	t.m_name = name;
	m_tables.push_back(std::move(t));
	return m_tables.back();
}

namespace {

const ibSchemaColumn* FindColumn(const std::vector<ibSchemaColumn>& cols, ibMetaID id)
{
	for (const ibSchemaColumn& c : cols)
		if (c.m_id == id)
			return &c;
	return nullptr;
}

bool HasIndex(const std::vector<ibSchemaIndex>& indexes, const wxString& name)
{
	for (const ibSchemaIndex& i : indexes)
		if (i.m_name == name)
			return true;
	return false;
}

// The USER-facing name of a table for the change ledger — the metaobject's name as it reads in the
// metadata tree (e.g. "Enumeration3"), not the physical "ClassNNNN". The queryable vends it directly
// (it already wraps the metaobject); the physical name is the fallback (a table with no queryable).
wxString LedgerName(const ibSchemaTable& t)
{
	return t.m_queryable != nullptr ? t.m_queryable->GetQueryName() : t.m_name;
}

const ibSchemaSeedRow* FindSeedRow(const std::vector<ibSchemaSeedRow>& seed, const wxString& id)
{
	for (const ibSchemaSeedRow& r : seed)
		if (r.m_id == id)
			return &r;
	return nullptr;
}

// A row's cell fingerprint — every (columnId -> value) flattened in column-id order (std::map is sorted),
// so two rows compare equal iff every cell matches. Uses ibValue::GetHashKey() (NOT GetString): for a
// reference cell it keys by the target guid — stable and cheap — whereas GetString would return the
// presentation (unstable, possibly a DB lookup). For primitives GetHashKey is the value's string. An
// enum row has no cells -> empty -> a matched enum never re-writes.
wxString SeedSignature(const ibSchemaSeedRow& row)
{
	wxString s;
	for (const auto& cell : row.m_values)
		s << cell.first << wxT('=') << cell.second.GetHashKey() << wxT('\x1f');
	return s;
}

// Generic seed-row UPSERT — resolve each cell's column by id from the table's own m_columns and SetValue
// it; the uuid is the row key. Captured BY VALUE (queryable + resolved column ptrs live in the config, the
// cells copied) so the closure survives the FB barrier (a row into a just-created table runs post-commit,
// after the ephemeral snapshot is gone). No metadata-specific code — the row is pure data.
void WriteSeedRow(ibStructureBatch& batch, const ibSchemaTable& t, const ibSchemaSeedRow& row, ibDatabaseConnectionHolder* holder)
{
	const wxString    table    = t.m_name;
	const ibMetaData* metaData = t.m_queryable != nullptr ? t.m_queryable->GetMetaData() : nullptr;
	const wxString    uuid     = row.m_id;
	ibDatabaseConnectionHolder* h = holder != nullptr ? holder : db_query->GetHolder();   // the builder's conn, or local

	// The seed's row-key column = the row-identity SCAFFOLD that a UNIQUE index covers (the upsert conflict
	// target), read OFF THE SCHEMA — never a hardcoded "uuid". A table whose key floats over other fields
	// (a register: recorder+line / dimensions) has no unique scaffold key here — but it also carries no
	// predefined seed, so keyName stays empty and the write degrades to a plain insert. Resolved to a
	// string so the deferred lambda holds no pointer into the snapshot.
	wxString keyName;
	for (const ibBackendQueryColumn* sc : t.m_scaffold) {
		for (const ibSchemaIndex& idx : t.m_indexes) {
			if (!idx.m_unique)
				continue;
			bool covers = false;
			for (const ibBackendQueryColumn* ic : idx.m_columns)
				if (ic == sc) { covers = true; break; }
			if (covers) { keyName = sc->GetPhysicalName(); break; }
		}
		if (!keyName.empty())
			break;
	}

	// Resolve each cell to the table's live column by id (a cell for a column the table lacks is dropped).
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> cells;
	for (const auto& cell : row.m_values)
		if (const ibSchemaColumn* c = FindColumn(t.m_columns, cell.first))
			cells.push_back({ c->m_column, cell.second });

	batch.Insert([table, uuid, keyName, cells, metaData, h]() -> bool {
		// A seed row is a VALUE-TABLE row keyed by its scaffold key (the uuid) — UPSERT matching on THAT
		// key, NOT the queryable's data-reference (a seed never sets it, and an enum table has no such
		// column, so matching on it raised "FLDxxxx_TYPE does not belong"). Columns = the key + each
		// cell's physical field spread; cell values bind through the column codec like a real row write.
		const bool hasKey = !keyName.empty();
		std::vector<wxString> columns;
		if (hasKey)
			columns.push_back(keyName);
		for (const auto& cell : cells)
			for (const wxString& f : ColumnFieldNames(cell.first))
				columns.push_back(f);

		ibQueryStatement stmt(ibQueryStatement::Kind::Upsert, table, columns,
			hasKey ? std::vector<wxString>{ keyName } : std::vector<wxString>{}, h);
		int position = 1;
		if (hasKey)
			stmt.SetParamString(position++, uuid);
		for (const auto& cell : cells)
			ibColumnCodec::WriteValue(cell.first, metaData, cell.second, &stmt, position);
		stmt.RunQuery();   // a real failure THROWS; the affected-row count (0 = no change) is not an error
		return true;
	});
}

// Generic seed-row DELETE by uuid.
void EraseSeedRow(ibStructureBatch& batch, const ibSchemaTable& t, const wxString& uuid, ibDatabaseConnectionHolder* holder)
{
	const wxString table = t.m_name;
	ibDatabaseConnectionHolder* h = holder != nullptr ? holder : db_query->GetHolder();
	batch.Insert([table, uuid, h]() -> bool {
		ibQueryStatement del(ibQueryStatement::Kind::Delete, table, { wxT("uuid") }, {}, h);
		del.SetParamString(1, uuid);
		del.RunQuery();    // a real failure THROWS; deleting a row that isn't there (0 rows) is not an error
		return true;
	});
}

// SEED diff: compare two value tables (baseline vs target) cell-by-cell, poured into the SAME batch as the
// structure so a row seeded into a just-created table defers past the DDL commit (FB barrier):
//   * not in baseline            -> new      -> upsert + "Add value"
//   * in baseline, cells changed -> changed  -> upsert + "Change value"
//   * in baseline, cells equal   -> unchanged-> SKIP (no re-write, no ledger line)
//   * gone from target           -> removed  -> delete + "Remove value"
int DiffSeedInto(ibStructureBatch& batch, const ibSchemaTable* old, const ibSchemaTable& cur, ibRestructureInfo* report, ibDatabaseConnectionHolder* holder)
{
	for (const ibSchemaSeedRow& row : cur.m_seed) {
		const ibSchemaSeedRow* o = (old != nullptr) ? FindSeedRow(old->m_seed, row.m_id) : nullptr;
		if (o != nullptr && SeedSignature(*o) == SeedSignature(row))
			continue;                              // every cell unchanged -> nothing to write

		WriteSeedRow(batch, cur, row, holder);
		if (report != nullptr)
			report->AppendInfo((o != nullptr ? _("Change value ") : _("Add value ")) + row.m_name + _(" in ") + LedgerName(cur));
	}
	if (old != nullptr) {
		for (const ibSchemaSeedRow& orow : old->m_seed) {
			if (FindSeedRow(cur.m_seed, orow.m_id) != nullptr)
				continue;
			EraseSeedRow(batch, cur, orow.m_id, holder);
			if (report != nullptr)
				report->AppendWarning(_("Remove value ") + orow.m_name + _(" from ") + LedgerName(cur));
		}
	}
	return 1;
}

// A column's friendly name for the change ledger (falls back to the physical field if unnamed).
wxString ColName(const ibBackendQueryColumn* col)
{
	if (col == nullptr) return wxEmptyString;
	const wxString name = col->GetName();
	return name.empty() ? col->GetPhysicalName() : name;
}

// CREATE: a table only in target. Scaffold creates it; logical columns are ADDed; indexes follow.
// An EXTERNAL table (sys_const) is created elsewhere — the differ only ADDs its columns, no table/index.
// Reporting: a real table create is ONE ledger line; an external table reports its added columns instead
// (no table line, since it owns no create here).
int CreateTable(ibStructureBatch& batch, const ibSchemaTable& t, ibRestructureInfo* report)
{
	if (!t.m_external) {
		batch.CreateTable(t.m_scaffold);
		if (report != nullptr)
			report->AppendInfo(_("Create table ") + LedgerName(t));
	}
	for (const ibSchemaColumn& c : t.m_columns) {
		DiffColumnInto(batch, c.m_column, nullptr);
		if (report != nullptr && t.m_external)
			report->AppendInfo(_("Add ") + ColName(c.m_column) + _(" to ") + LedgerName(t));
	}
	if (!t.m_external)
		for (const ibSchemaIndex& i : t.m_indexes)
			batch.CreateIndex(i.m_name, i.m_columns, i.m_unique);
	return 1;
}

// ALTER: a table in both. Columns diff by id (DiffColumnInto handles add / drop / type-change); indexes
// diff by name (present-only-in-target -> create, only-in-baseline -> drop). Scaffold never changes.
// Reporting: add / drop is reported unconditionally; a MATCHED column is reported as "Change" ONLY when
// its slot diff actually emitted DDL (StepCount grew) — an unchanged column stays silent.
int AlterTable(ibStructureBatch& batch, const ibSchemaTable& old, const ibSchemaTable& cur, ibRestructureInfo* report)
{
	int retCode = 1;

	for (const ibSchemaColumn& c : cur.m_columns) {
		const ibSchemaColumn* o = FindColumn(old.m_columns, c.m_id);
		const size_t before = batch.StepCount();
		DiffColumnInto(batch, c.m_column, o != nullptr ? o->m_column : nullptr);   // errors THROW now
		if (report != nullptr && batch.StepCount() != before) {   // a step was emitted -> a real change
			if (o == nullptr)
				report->AppendInfo(_("Add ") + ColName(c.m_column) + _(" to ") + LedgerName(cur));
			else
				report->AppendInfo(_("Change ") + ColName(c.m_column) + _(" in ") + LedgerName(cur));
		}
	}
	for (const ibSchemaColumn& o : old.m_columns) {
		if (FindColumn(cur.m_columns, o.m_id) == nullptr) {
			DiffColumnInto(batch, nullptr, o.m_column);   // gone -> drop the whole column (errors THROW)
			if (report != nullptr)
				report->AppendWarning(_("Remove ") + ColName(o.m_column) + _(" from ") + LedgerName(cur));
		}
	}

	for (const ibSchemaIndex& i : cur.m_indexes)
		if (!HasIndex(old.m_indexes, i.m_name)) {
			batch.CreateIndex(i.m_name, i.m_columns, i.m_unique);
			if (report != nullptr)
				report->AppendInfo(_("Add index ") + i.m_name);
		}
	for (const ibSchemaIndex& o : old.m_indexes)
		if (!HasIndex(cur.m_indexes, o.m_name)) {
			batch.Ddl(ibDropIndex(o.m_name, cur.m_name));
			if (report != nullptr)
				report->AppendInfo(_("Remove index ") + o.m_name);
		}

	return retCode;
}

} // namespace

int DiffSnapshots(const ibSchemaSnapshot* baseline, const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder, ibRestructureInfo* report)
{
	int retCode = 1;
	ibSchemaBuilder schema(holder);

	// Tables present in baseline but gone from target -> DROP (a vanished id).
	if (baseline != nullptr) {
		for (const ibSchemaTable& old : baseline->Tables()) {
			if (target.Find(old.m_id) != nullptr)
				continue;
			if (old.m_external)
				continue;                         // external tables (sys_const) are never dropped by the differ
			ibStructureBatch batch(old.m_name);   // a drop needs no metadata
			batch.DropTable();
			if (report != nullptr)
				report->AppendWarning(_("Drop table ") + LedgerName(old));
			batch.Flush(schema);   // errors THROW (caught by the storage's apply try/catch)
		}
	}

	// Tables in target -> CREATE (new id) or ALTER (matched id). One batch per table.
	for (const ibSchemaTable& cur : target.Tables()) {
		const ibSchemaTable* old = baseline != nullptr ? baseline->Find(cur.m_id) : nullptr;

		ibStructureBatch batch(cur.m_queryable);   // the queryable vends the metadata the field diff needs
		if (old == nullptr)
			CreateTable(batch, cur, report);
		else
			AlterTable(batch, *old, cur, report);

		// DATA after structure, SAME batch: rows into a just-created table defer past the DDL commit on
		// Firebird; other dialects fill them inside the transaction (ibStructureBatch::Flush -> RunOrDefer).
		DiffSeedInto(batch, old, cur, report, holder);

		batch.Flush(schema);   // errors THROW; a 0-row (DDL / no-change) result is not a failure
	}

	return retCode;   // 1 — success; a real DB error THREW (no return-code error signal in the apply path)
}
