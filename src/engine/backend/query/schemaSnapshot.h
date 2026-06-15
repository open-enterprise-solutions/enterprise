#ifndef __SCHEMA_SNAPSHOT_H__
#define __SCHEMA_SNAPSHOT_H__

// The declarative STRUCTURE snapshot — a narrow projection of a configuration's physical schema
// (tables + columns + indexes), and the agnostic differ over it. This is the contract that decouples
// metadata from DDL: metadata DECLARES its tables (ibValueMetaObject::DescribeTable -> ibSchemaTable),
// SnapshotOf assembles a snapshot, and DiffSnapshots turns (baseline, target) into DDL — knowing
// NOTHING about metadata. baseline == null => create-all (a fresh DB / ReCreate).
//
// Ephemeral: built at config-save, diffed, discarded. It holds LIVE pointers (queryable / columns /
// metadata of the two in-memory configurations), never owned — valid only for the duration of a save.
//
// Identity, not name, is the match key: a table is its metaID, a column its model id. So a renamed /
// retyped object is an ALTER (matched by id), and a vanished id is a DROP. (docs/query-language-arc.md)

#include "backend.h"
#include "backend/query/queryColumn.h"   // ibRawDBColumn (the snapshot OWNS its scaffold raw columns)
#include "backend/compiler/value.h"      // ibValue — a seed row is a column-id -> value map

#include <memory>
#include <vector>
#include <deque>
#include <map>

class ibBackendQueryable;
class ibMetaData;
class ibDatabaseConnectionHolder;
class ibMetaDataConfigurationBase;
class ibRestructureInfo;
class ibStructureBatch;

// A logical column in a table snapshot — its identity (for matching) + the live column (the field-level
// diff, DiffColumnInto, reads its type off this).
struct ibSchemaColumn
{
	ibMetaID                    m_id     = 0;        // the column's model id (attribute metaID) — the match key
	const ibBackendQueryColumn* m_column = nullptr;  // the live column (type source for the field diff)
};

// An index declared by a table. Matched by name; a changed column set / uniqueness => drop + recreate.
struct ibSchemaIndex
{
	wxString                                 m_name;
	std::vector<const ibBackendQueryColumn*> m_columns;        // logical columns it covers (expand to fields at apply)
	bool                                     m_unique = false;
};

// A DATA row of the table — a value-table row declared as schema. The STRUCTURE says "what columns" (the
// table's m_columns); the SEED says "what rows must exist", each row a CELL MAP keyed by the SAME column
// ids (m_values: column metaID -> value). An enum row carries only its uuid (empty cell map); a predefined
// row carries its ~5 columns (name / code / description / isFolder / parent). The builder diffs two such
// tables BY CELLS: a row present only in target is inserted, a row whose cells changed is updated, a row
// gone from target is deleted — all written generically (resolve the column by id from m_columns, SetValue,
// Upsert), so the metaobject only DECLARES the data and the builder does the I/O.
struct ibSchemaSeedRow
{
	wxString                    m_id;      // identity (the row uuid) — the match key
	wxString                    m_name;    // friendly name for the change ledger
	std::map<ibMetaID, ibValue> m_values;  // cell map: column metaID -> value (empty for an enum: uuid only)

	// Fluent cell binding: row.Set(qc, value) — the link between a column and its value in this row. The
	// cell is keyed by the column's model id (== metaID for an attribute), so baseline/target rows from
	// two different config instances still compare cell-for-cell.
	ibSchemaSeedRow& Set(const ibBackendQueryColumn* qc, const ibValue& value) { m_values[qc->GetColumnId()] = value; return *this; }
};

// One table's declared structure. Produced by a metaobject's DescribeTable(); consumed by DiffSnapshots.
struct ibSchemaTable
{
	ibMetaID                    m_id   = 0;          // the metaobject's metaID — the table match key
	wxString                    m_name;              // physical table name
	const ibBackendQueryable*   m_queryable = nullptr;  // vends the metadata for the field diff (null for a pure drop)

	// EXTERNAL table — its CREATE / DROP is owned elsewhere (e.g. the shared sys_const, made by
	// CreateConstantSQLTable: DEFAULT / PRIMARY KEY / single-row seed the snapshot can't model). The
	// differ manages only its COLUMNS (add / drop / alter), never the table itself.
	bool                        m_external = false;

	// The snapshot OWNS its scaffold / index raw columns (uuid, rowData) via shared_ptr — stable heap
	// address through every move/copy of the table, so the pointers below never dangle. Live attribute
	// columns are NOT owned (they belong to the in-memory config, valid for the save).
	std::vector<std::shared_ptr<ibRawDBColumn>> m_ownedRaw;

	// Scaffold columns (the uuid row-key / a register's rowData) — created WITH the table, never altered.
	std::vector<const ibBackendQueryColumn*> m_scaffold;
	// Logical columns (attributes / dimensions / resources) — diffed by id.
	std::vector<ibSchemaColumn>              m_columns;
	std::vector<ibSchemaIndex>               m_indexes;
	// Declared DATA rows (enum / predefined values) — diffed by uuid, applied/erased through the batch.
	std::vector<ibSchemaSeedRow>            m_seed;

	// Own a raw column and return a stable pointer to it (for scaffold / index use).
	const ibBackendQueryColumn* OwnRaw(ibRawDBColumn col)
	{
		m_ownedRaw.push_back(std::make_shared<ibRawDBColumn>(std::move(col)));
		return m_ownedRaw.back().get();
	}

	// ---- fluent builders: a metaobject DECLARES its table by chaining these (structure first, then rows) --
	// (the queryable is bound at construction by ibSchemaSnapshot::CreateSchemaTable; External re-binds it
	//  for the shared sys_const table only.)
	ibSchemaTable& External(const ibBackendQueryable* q) { if (m_queryable == nullptr) { m_queryable = q; m_external = true; } return *this; }

	// Add a scaffold raw column (uuid / rowData) — owned, returns the stable pointer for index use.
	const ibBackendQueryColumn* Scaffold(ibRawDBColumn col) { const ibBackendQueryColumn* p = OwnRaw(std::move(col)); m_scaffold.push_back(p); return p; }
	// Add a logical column — keyed by the column's own model id (== metaID for an attribute), so the diff
	// matches columns across config instances by identity.
	ibSchemaTable& Add(const ibBackendQueryColumn* qc) { m_columns.push_back({ qc->GetColumnId(), qc }); return *this; }
	ibSchemaTable& Index(const wxString& name, std::vector<const ibBackendQueryColumn*> cols, bool unique = false) { m_indexes.push_back({ name, std::move(cols), unique }); return *this; }

	// Add a DATA row (enum value / predefined value) — returns it so cells bind fluently: AddRow(uuid,name).Set(qc,val)...
	ibSchemaSeedRow& AddRow(const wxString& uuid, const wxString& name = wxEmptyString)
	{
		ibSchemaSeedRow r; r.m_id = uuid; r.m_name = name;
		m_seed.push_back(std::move(r));
		return m_seed.back();
	}
};

class BACKEND_API ibSchemaSnapshot
{
public:
	// Create a NEW table from its queryable (which vends id + name + the handle) and return it for fluent
	// chaining — CreateSchemaTable(GetQueryable()).Add(qc).Add(qc).AddRow(uuid).Set(qc,v). Each call is a
	// DISTINCT table object (enum, register, catalog + each of its tabular sections). The deque backing
	// keeps the returned reference STABLE as more tables are added, so a metaobject can build its main
	// table and its tabular sections in one chain.
	ibSchemaTable& CreateSchemaTable(const ibBackendQueryable* queryable);

	// Find-or-create — for a SHARED table several metaobjects extend (sys_const: every constant is one of
	// its columns). Returns the existing table if present, else a fresh one.
	ibSchemaTable& Shared(ibMetaID id, const wxString& name);

	const std::deque<ibSchemaTable>& Tables() const { return m_tables; }
	const ibSchemaTable*             Find(ibMetaID id) const;

private:
	std::deque<ibSchemaTable> m_tables;
};

// The agnostic differ: turn (baseline -> target) into DDL on `conn` (null = the local channel, db_query).
// baseline == null => every target table is new (create-all). Per-table batches; the seed (DATA) is NOT
// here — structure only. Returns the first error retCode or 1. (Knows nothing about metadata.)
//
// `report` (optional) receives a human-readable line for each REAL structural change — CREATE / DROP
// table, add / drop column, and a column ALTER ONLY when its slot diff actually emitted DDL (a no-op
// column is silent). This is what the apply-change dialog shows; pass null to diff without reporting.
BACKEND_API int DiffSnapshots(const ibSchemaSnapshot* baseline, const ibSchemaSnapshot& target,
                              ibDatabaseConnectionHolder* holder = nullptr, ibRestructureInfo* report = nullptr);

#endif // !__SCHEMA_SNAPSHOT_H__
