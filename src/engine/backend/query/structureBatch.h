#ifndef __STRUCTURE_BATCH_H__
#define __STRUCTURE_BATCH_H__

// One table's structure delta, accumulated as the metadata layer walks it. It REPLACES the bare
// table NAME that used to be threaded through the Process* methods: each metaobject subclass receives
// the batch and POURS its own work into it — DDL (create / add / drop / alter column, indexes) and
// data seeds (inserts). Nothing executes while the subclass fills the batch.
//
// At the OUTPUT the schema door (ibSchemaBuilder) flushes the batch:
//   * consecutive same-op column clauses coalesce into ONE ALTER TABLE (ADD c1, ADD c2, DROP c3) —
//     far fewer round-trips than one ALTER per column; dialects without multi-clause ALTER (SQLite)
//     get one statement per clause instead;
//   * the create / index / drop-table steps run in emission order, so create-before-alter holds;
//   * the seed INSERTs route through the barrier: if this batch ALSO created the table, on a barrier
//     dialect (Firebird) the inserts DEFER past the DDL commit — a table cannot be populated in the
//     same TX that created it. "See a create and an insert in the batch -> move the inserts past the
//     commit." The deferral re-uses ibSchemaBuilder's per-save queue (run by the orchestrator's Flush).

#include "backend.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibDdlColumn / ibAlterClause / ibDdlStatement
#include "backend/query/columnLayout.h"                    // ibColumnSlot + DescribeColumnLayout (the layout tier)

#include <functional>
#include <vector>

class ibSchemaBuilder;
class ibBackendQueryColumn;   // a logical column — the batch expands it to physical fields via the layout tier
class ibBackendQueryable;     // the schema's table handle — the batch is keyed by it (table name + metadata)
class ibMetaData;

class BACKEND_API ibStructureBatch
{
public:
	// Key the batch by the schema's QUERYABLE (the proper table handle) — it vends the table name AND
	// the metadata, so the column-level AddColumn/DropColumn need no separate metaData argument.
	explicit ibStructureBatch(const ibBackendQueryable* queryable);

	// Key by a bare table name — for tables that have no queryable yet (virtual register projections).
	explicit ibStructureBatch(const wxString& table) : m_table(table) {}

	const wxString&    GetTable()    const { return m_table; }
	const ibMetaData*  GetMetaData() const;   // the queryable's metadata (null when keyed by bare name)

	// --- column-level: expand a LOGICAL column (ibBackendQueryColumn) through the layout tier and
	//     pour every physical field. The whole-column cases — a new attribute (ADD all its fields) or
	//     a removed one (DROP all). Metadata comes from the queryable the batch is keyed by.
	void AddColumn(const ibBackendQueryColumn* column);
	void DropColumn(const ibBackendQueryColumn* column);

	// --- field-level: one physical field (the variant type-set diff adds / drops / alters individual
	//     slots of a column — that granularity is a metadata concern, so the caller hands us slots).
	//     DROP and ALTER take the slot WHOLE, not a name: the statement then carries the shape the
	//     barrier's compensation ledger restores from if the apply's second phase fails. --
	void AddField(const ibColumnSlot& slot);                    // coalesced into a batched ALTER … ADD
	void DropField(const ibColumnSlot& slot);                   // coalesced into a batched ALTER … DROP COLUMN
	void AlterField(const ibColumnSlot& slot, const ibColumnSlot& prev);   // standalone type change (not batched)

	// --- table / index -------------------------------------------------------------------------
	// Create the table from LOGICAL columns (the uuid row-key / a register's rowData are ibRawDBColumn
	// scaffolds). Each expands through the layout tier; uniqueness/identity is enforced by indexes
	// (CreateIndex), not a table-level PRIMARY KEY — so the structure speaks only columns.
	void CreateTable(std::vector<const ibBackendQueryColumn*> columns);
	void DropTable();
	// Index over LOGICAL columns — each expands to its physical field names through the layout tier
	// (a raw uuid column -> "uuid"; a reference attribute -> its _RTRef/_RRRef pair; a dimension -> its
	// fields). The structure speaks columns, not bare field strings.
	void CreateIndex(const wxString& indexName, std::vector<const ibBackendQueryColumn*> columns, bool unique = false);
	void Ddl(const ibDdlStatement& ddl);                        // any other prebuilt statement, in order

	// --- data seed the subclass pours in --------------------------------------------------------
	// A self-contained "do the write, return success" closure (the write site stays target-agnostic).
	void Insert(std::function<bool()> write) { m_inserts.push_back(std::move(write)); }

	size_t StepCount() const { return m_steps.size(); }   // DDL steps so far — lets the differ tell a REAL
	                                                      // column change (a slot diff emitted a step) from a no-op.

	// Hand the batch to the schema door: render + run the DDL (coalescing ADD/DROP runs), then route
	// each seed insert through the barrier. Returns the first error retCode (DATABASE_LAYER_QUERY_-
	// RESULT_ERROR) or 1. Clears the batch.
	int Flush(ibSchemaBuilder& schema);

private:
	const ibBackendQueryable*         m_queryable = nullptr;   // the schema handle (null when keyed by bare name)
	wxString                          m_table;
	std::vector<ibDdlStatement>       m_steps;      // single-op DDL, in emission order
	std::vector<std::function<bool()>> m_inserts;   // data seeds (deferred past the DDL commit by ibSchemaBuilder's barrier)
};

// Diff one LOGICAL column into the batch — the structure tier's column-creation, lifted off the
// metaobject (a metadata attribute no longer owns its DDL; it is just "a column with this type").
//   dst == null -> CREATE: ADD every physical field of `src`.
//   src == null -> DELETE: DROP every physical field of `dst`.
//   both         -> UPDATE: a slot diff (ADD new / DROP removed / ALTER changed) + the reference pair's
//                   per-target row cleanup. Metadata context comes from the batch's queryable.
// Returns the first DML-cleanup error retCode, or 1 (DDL errors surface at Flush).
BACKEND_API int DiffColumnInto(ibStructureBatch& batch, const ibBackendQueryColumn* srcCol, const ibBackendQueryColumn* dstCol);

#endif // !__STRUCTURE_BATCH_H__
