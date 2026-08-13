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
#include "backend/databaseLayer/databaseMaterializeBuilder.h"   // L2-2 — ibMaterializeSpec / ibTotalsPeriod (pulls databaseLayer.h)

#include <memory>
#include <vector>
#include <deque>
#include <map>
#include <functional>   // ibSchemaTable::m_beforeChange — the structural check a table may carry

class ibBackendQueryable;
class ibMetaData;
class ibDatabaseConnectionHolder;

// The L3 expression a delta carries for REGENERATION (query/queryable.h). Forward-declared rather
// than included: a shared_ptr member needs no complete type, and pulling the whole queryable header
// in here would tie the structure snapshot to the navigation contract for one field.
struct ibQueryColumnExpr;
using  ibQueryColumnExprPtr = std::shared_ptr<ibQueryColumnExpr>;
// Same reasoning for the guard's regeneration form — a predicate rather than a value expression.
struct ibQueryPredicate;
using  ibQueryPredicatePtr  = std::shared_ptr<ibQueryPredicate>;
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
	//
	// A column the configuration does NOT have is skipped, not written: an arrangement can retire one of
	// its own columns (a flat catalog has no Parent and no IsFolder), and a seed naming it would fill a
	// cell for a column the table does not carry. The same IsAllowed the structure walk uses, so the seed
	// and the columns it writes into cannot disagree.
	ibSchemaSeedRow& Set(const ibBackendQueryColumn* qc, const ibValue& value) {
		if (qc != nullptr && qc->IsAllowed())
			m_values[qc->GetColumnId()] = value;
		return *this;
	}
};

// ==========================================================================
// DERIVED-STATE declaration — a table whose rows are a FUNCTION of another table's rows, kept
// current by a database trigger. Today: a register's totals. (docs/register-totals-strategy.md)
//
// The metaobject declares INTENT here — which table feeds this one, what the key is, what each
// resource accumulates, at what period grain. It never renders SQL and never names an engine:
// the materialization renderer turns this plus the driver's ibMaterializationDialect into the
// trigger / view statements. Same division as everywhere else in this file — metadata declares,
// the differ emits.
//
// Expression fields are SQL fragments over the MOVEMENT row with one placeholder, {row}, which
// the renderer replaces by the dialect's NEW / OLD alias. They are built by the metaobject from
// its own column knowledge (which field is the record type, which is the resource), so the
// renderer needs no metadata.
// ==========================================================================

// One accumulating column: a totals column plus what a single movement contributes to it.
// For a balance register the contribution carries the sign split — receipt and expense are two
// deltas whose value expressions are complementary CASEs over the record type — so the
// accumulate itself stays unconditional and only its VALUE branches.
struct ibSchemaDelta
{
	const ibBackendQueryColumn* m_column = nullptr;   // the totals column being accumulated
	wxString                    m_valueExpr;          // SQL over {row} — what this movement adds (TRIGGER)

	// The same contribution as an L3 expression, for REGENERATION. Two forms of one thing, and the
	// duplication is forced by context rather than convenience: a trigger sees ONE row through NEW /
	// OLD aliases, while a rebuild sees the whole TABLE and sums a column across it. There is no
	// single syntax that is valid in both places.
	//
	// They must agree, and what keeps them honest is not discipline but the parity test: the
	// trigger-maintained totals and a full rebuild of the same movements have to produce identical
	// rows. A divergence here fails that immediately rather than months later.
	ibQueryColumnExprPtr        m_regenExpr;
};

struct ibSchemaMaterialize
{
	// The SOURCE this table is derived from — the QUERYABLE, not a bare name. Two floors consume
	// it and they need different things from it: L3-2 wants only the physical table name (to hang
	// the triggers on), but L3-4 has to READ the source and aggregate it, and reading is what the
	// queryable exists for — it is the contract the L3 door navigates a source through. Handing
	// down a name would leave regeneration to re-resolve it, or to reach past L3 with hand-built
	// SQL; handing down the queryable means regeneration is an ordinary L3 read.
	const ibBackendQueryable* m_source = nullptr;

	// The source's physical table — resolved from the queryable, for the trigger DDL.
	wxString SourceTable() const;

	// Lower this DECLARATION into the L2-2 render spec: logical columns expand into their physical
	// fields (a reference dimension is two), the source queryable resolves to a table name. That
	// expansion is a metadata question, so it happens HERE and never inside the renderer — which is
	// what keeps L2-1 metadata-blind and the dependency pointing downward.
	ibMaterializeSpec ToRenderSpec(const wxString& tableName) const;

	// The totals KEY — the columns the delta upserts against (period + dimensions, plus the shard
	// column when split). Also the PRIMARY KEY / unique index of the derived table.
	std::vector<const ibBackendQueryColumn*> m_keys;

	// The period key: which key column holds the truncated period, and the movement expression it
	// is truncated FROM. Empty column name = this derived table has no period dimension at all.
	wxString       m_periodColumn;
	wxString       m_periodSourceExpr;                       // SQL over {row} — the TRIGGER's form
	const ibBackendQueryColumn* m_periodSource = nullptr;     // the same column, for REGENERATION to group by
	ibTotalsPeriod m_periodUnit = ibTotalsPeriod::Month;      // STORED grain — the floor on what can be read back

	// Non-key columns whose values accumulate.
	std::vector<ibSchemaDelta> m_deltas;

	// Optional guard over {row}: apply this delta only when the movement populates this side.
	// Empty = unconditional. The accounting register's debit / credit split is the reason it exists.
	wxString m_guard;

	// ⭐⭐ THE SAME CONDITION, IN THE FORM EACH CONSUMER SPEAKS.
	//
	// The trigger needs SQL over {row}; the REGENERATOR builds a query IR and cannot parse text. A
	// guard with only the first form is a rule the rebuild does not know: the trigger would keep
	// inactive movements out while a regeneration quietly let them in, and the two would disagree
	// about the same register with nothing to show for it but different numbers.
	//
	// Both forms are declared together, from one line at the schema, for the same reason the deltas
	// are (see ibSchemaDelta): what cannot be derived from the other must be stated beside it.
	ibQueryPredicatePtr m_guardExpr;

	// SPLIT TOTALS — how many shard rows one logical key is spread across, to relieve write
	// contention on a hot key. 1 = not split (the default today: without the fold running on a
	// schedule the read pays O(N) shards on every row forever, so splitting is for registers PROFILED
	// hot on write. Once ibDerivedState::Collapse runs regularly the tax shrinks to the OPEN period
	// alone — closed ones fold back to one row per key — and a wider default becomes defensible;
	// docs/register-totals-strategy.md §6a). The view absorbs the split — it sums the shards — so nothing above L3 can
	// tell a split register from a plain one. Engines without ibMaterializationDialect::
	// m_connectionIdExpr (SQLite) collapse to 1: single-writer engines have no contention to split.
	unsigned int m_shards = 1;

	// The READ views — the public surface, one per virtual table. Each is an ORDINARY relation to
	// everything above L3: it joins, filters, pages and RLS-restricts through the engine's own
	// machinery, indistinguishable from a table. What it hides is the materialisation underneath —
	// the trigger-maintained totals and, when split, the shards. Swapping what sits below changes a
	// view body and nothing else.
	//
	// Declared in L2-2's own vocabulary (a shard fold, a difference, a running sum) because that
	// vocabulary is metadata-free: the METAOBJECT decides that a running sum of the signed turnover
	// is what it calls a balance, and level 2 renders it without ever learning the word. An
	// accounting register composes the same primitives into its own tables with no change below.
	//
	// By convention the names are the owning table's physical name plus a suffix.
	std::vector<ibMaterializeView> m_views;

	// ---- fluent declaration (mirrors ibSchemaTable's own builders) ----
	ibSchemaMaterialize& Key(const ibBackendQueryColumn* qc) { m_keys.push_back(qc); return *this; }
	// `source` is the movement's period COLUMN; `sourceExpr` is how the trigger names it over {row}.
	ibSchemaMaterialize& Period(const wxString& column, const ibBackendQueryColumn* source,
	                            const wxString& sourceExpr, ibTotalsPeriod unit)
	{
		m_periodColumn = column; m_periodSource = source; m_periodSourceExpr = sourceExpr; m_periodUnit = unit;
		return *this;
	}
	// Both forms of one contribution — see ibSchemaDelta on why the pair is unavoidable.
	ibSchemaMaterialize& Accumulate(const ibBackendQueryColumn* qc, const wxString& valueExpr,
	                                const ibQueryColumnExprPtr& regenExpr = nullptr)
	{
		m_deltas.push_back({ qc, valueExpr, regenExpr }); return *this;
	}
	// Declare a read view; the returned reference takes its columns.
	ibMaterializeView& View(const wxString& name, bool withPeriod, bool dropZeroRows = false)
	{
		ibMaterializeView v;
		v.m_name = name; v.m_withPeriod = withPeriod; v.m_dropZeroRows = dropZeroRows;
		m_views.push_back(std::move(v));
		return m_views.back();
	}
	ibSchemaMaterialize& Guard(const wxString& expr, const ibQueryPredicatePtr& regenExpr = nullptr)
	{
		m_guard = expr; m_guardExpr = regenExpr; return *this;
	}
	ibSchemaMaterialize& Split(unsigned int shards)  { m_shards = shards > 0 ? shards : 1; return *this; }
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

	// DERIVED table — its ROWS are a function of another table's rows, maintained by a trigger
	// (m_materialize below carries the declaration). ONE bit routes all three floors correctly:
	//   L3-2 (structure)  — builds it, its trigger and its view, like any other table
	//   L3-3 (data mover) — NEVER dumps or loads it; moving derived rows would move a stale copy
	//                       of something the destination regenerates exactly
	//   L3-4 (regenerate) — rebuilds it from the source when the trigger cannot have kept up
	//                       (restore, bulk load, a regroup that is not derivable in place)
	// "Don't move it, regenerate it, keep it consistent by trigger" — all three read off this bit.
	bool                        m_derived = false;
	ibSchemaMaterialize         m_materialize;   // meaningful only when m_derived

	// The snapshot OWNS its scaffold / index raw columns (uuid, rowData) via shared_ptr — stable heap
	// address through every move/copy of the table, so the pointers below never dangle. Live attribute
	// columns are NOT owned (they belong to the in-memory config, valid for the save).
	std::vector<std::shared_ptr<ibRawDBColumn>> m_ownedRaw;

	// A queryable this table owns (SelfSource) — set only for a table nothing else vends one for.
	// Empty on every table declared by a metaobject: there m_queryable points at the live config.
	std::shared_ptr<const ibBackendQueryable>   m_ownedQueryable;

	// A CHECK THIS TABLE CARRIES, evaluated by the differ BEFORE the table is touched. Declaring a
	// structure is not the same as being allowed to reach it: a chart of accounts that lowers its
	// analytics ceiling would strand rows on accounts that already hold more, and a change the data
	// cannot survive must be refused BEFORE any DDL, not discovered halfway through it.
	//
	// The check belongs to the declaration for the same reason the columns do — the object that knows
	// the rule attaches it while contributing, and the differ, which knows nothing about accounts,
	// simply asks. Returning false appends its own error to the ledger (which greys Apply) and leaves
	// the table alone. Empty = nothing to ask.
	std::function<bool(class ibRestructureInfo*)> m_beforeChange;

	// AND ITS PAIR, AFTER. Symmetric by construction and asymmetric in power: BEFORE may refuse, AFTER
	// may not — by then the table has been changed and there is nothing left to decline. It exists to
	// SAY things (what was migrated, what the owner did with the new shape), so it returns nothing.
	std::function<void(class ibRestructureInfo*)> m_afterChange;

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

	// Bind a queryable this table OWNS, for a table no metaobject vends one for — the DERIVED
	// (totals) table. Without it the table exists physically and is unreachable through the door:
	// regeneration and the shard fold both gate on a non-null queryable and quietly do nothing.
	// Held by shared_ptr so the pointer survives every move of this struct inside the snapshot's
	// deque, exactly like m_ownedRaw does for the scaffold columns.
	ibSchemaTable& SelfSource(std::shared_ptr<const ibBackendQueryable> q)
	{
		m_ownedQueryable = std::move(q);
		m_queryable      = m_ownedQueryable.get();
		return *this;
	}

	// Add a scaffold raw column (uuid / rowData) — owned, returns the stable pointer for index use.
	const ibBackendQueryColumn* Scaffold(ibRawDBColumn col) { const ibBackendQueryColumn* p = OwnRaw(std::move(col)); m_scaffold.push_back(p); return p; }
	// Add a logical column — keyed by the column's own model id (== metaID for an attribute), so the diff
	// matches columns across config instances by identity.
	ibSchemaTable& Add(const ibBackendQueryColumn* qc) { m_columns.push_back({ qc->GetColumnId(), qc }); return *this; }
	ibSchemaTable& Index(const wxString& name, std::vector<const ibBackendQueryColumn*> cols, bool unique = false) { m_indexes.push_back({ name, std::move(cols), unique }); return *this; }

	// Declare this table as DERIVED from `source`, maintained by trigger. Returns the materialize
	// spec for fluent filling: Derived(movQueryable, view).Key(dim).Period(col, expr, unit)
	// .Accumulate(res, expr).Guard(expr).Split(4). Sets m_derived, which is what L3-3 and L3-4 read
	// — declaring materialisation and being derived are the same statement, so they cannot disagree.
	ibSchemaMaterialize& Derived(const ibBackendQueryable* source)
	{
		m_derived = true;
		m_materialize.m_source = source;
		return m_materialize;
	}

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

// Do these two snapshots describe the SAME physical structure? Tables by id, their columns by id + type,
// their indexes, and a derived table's materialisation spec. Answers the one question the apply flow asks
// BEFORE it starts: is there anything here that has to be written into the database, or does the change
// live entirely in modules, forms and properties?
//
// It is a comparison of the DECLARED structure, not a rehearsal of the differ — a rehearsal would mean a
// second mode inside the differ, where the same path sometimes executes and sometimes does not, and one
// missed branch there writes to a live database during what the caller believed was a question. The
// comparison cannot do that: it holds no connection and calls nothing.
//
// A wrong "same" is caught downstream — the DDL gate in ibStructureBatch::Flush still demands exclusive
// mode when a step does materialise. So this answers what the DIALOG needs, and the apply keeps its own
// truth.
BACKEND_API bool SameStructure(const ibSchemaSnapshot* baseline, const ibSchemaSnapshot& target);

#endif // !__SCHEMA_SNAPSHOT_H__
