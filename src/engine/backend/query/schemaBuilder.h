#ifndef __SCHEMA_BUILDER_H__
#define __SCHEMA_BUILDER_H__

// L3-2 — the SCHEMA door (the "skeleton"). Applies DDL (create / alter / drop tables & columns,
// indexes) to a TARGET connection, and owns the DDL/DML transaction barrier. The metaobject layer
// builds an ibDdlStatement from the column-layout tier (DescribeColumnLayout) and applies it THROUGH
// here — so the metaobject never renders SQL or names a dialect; the dialect dictionary closes the
// per-DBMS forks inside the L2 render.
//
// Responsibility split: the metadata-config orchestrator BUILDS the DDL (through the metaobjects),
// VERIFIES it (the restructure ledger) and drives the TRANSACTION; this door is the shim it applies
// DDL and the barrier through. L3-3 (the data mover) moves only the rows — no skeleton.
//
// AUTOMATIC DDL/DML BARRIER. Some engines (Firebird) cannot populate a table in the SAME transaction
// that created it — the metadata cache races and seed rows are dropped. Because schema creation runs
// fully through this door, it can TRACK which tables were created in the current save; a data write
// to such a table is then DEFERRED automatically (the write closure is recorded) and run after the
// DDL commit, when the table is durable. No `if (driver == FIREBIRD)` in the metadata layer — the
// barrier is the dialect fact m_ddlCommitBeforeData, and the deferral is local to each write site.
//
// The `holder` is the seam (same as L2 ibDatabaseQueryBuilder / L3 ibTempTableManager): it is the
// connection lifetime anchor; the door runs DDL on holder->EnsureConnection() (the holder's pinned /
// scoped conn — the SAME conn its open transaction lives on). nullptr = the local DDL channel (db_query,
// the designer's single save channel); an explicit holder = another database / a dedicated restructuring
// connection — manage / migrate a remote schema from one base.

#include "backend.h"

#include <wx/arrstr.h>   // wxArrayString — PhysicalIndexes result

#include <functional>
#include <set>
#include <vector>

struct ibDdlStatement;   // a struct in databaseQueryBuilder.h — match the tag or MSVC name-mangles Execute differently (LNK2019)
class ibDatabaseLayer;
class ibDatabaseConnectionHolder;

class BACKEND_API ibSchemaBuilder
{
public:
	// holder = the connection holder to apply schema through (its EnsureConnection conn); nullptr = the
	// local channel (db_query).
	explicit ibSchemaBuilder(ibDatabaseConnectionHolder* holder = nullptr);

	// Render the statement through the target's dialect and run it once on the target. A CreateTable
	// on a barrier dialect records the table as "created this save" (so a same-TX write to it defers).
	int Execute(const ibDdlStatement& ddl);

	// A data write to `table`. On a barrier dialect, if `table` was created in this save's DDL phase
	// (not yet durable for a same-TX write), DEFER `work` to run after the DDL commit and return true
	// (the real result surfaces in Flush). Otherwise run `work` now and return its result. The
	// write site stays agnostic: it hands a self-contained "do the write, return success" closure.
	bool RunOrDefer(const wxString& table, std::function<bool()> work);

	// Run every deferred write in the CURRENT transaction — call this in the data/seed phase AFTER the
	// orchestrator has committed the DDL and opened a fresh TX (so the just-created tables are durable).
	// Returns false if any deferred write failed (the caller rolls back); clears the tracking. On
	// engines without the barrier the deferred queue is empty, so this is a no-op returning true.
	bool Flush();

	// Does the target dialect accept a multi-clause ALTER TABLE (one statement, several ADD/DROP)? The
	// structure builder folds a batch into one ALTER when true, and into one statement per clause when
	// not (SQLite). See ibDialectDictionary::m_alterTableMultiClause.
	bool AlterTableMultiClause() const;

	// Index introspection (ibDialectDictionary::m_indexListQuery). CanIntrospectIndexes() is false when the
	// dialect has no index-list query (MySQL / ODBC) -> the differ keeps its metadata-only diff. PhysicalIndexes()
	// returns the NAMES of the indexes that currently exist on `table` (empty when it cannot introspect), so the
	// differ creates only the declared indexes the DB is physically missing — the retrofit path on engines
	// without CREATE INDEX IF NOT EXISTS (Firebird).
	bool CanIntrospectIndexes() const;
	wxArrayString PhysicalIndexes(const wxString& table) const;

	// Reset the per-save tracking (created-set + deferred queue) on this builder's barrier holder.
	// Call at the start of a save. No longer static — the state lives on the holder, not a global.
	void Reset();

private:
	bool BarrierActive() const;   // the target dialect's m_ddlCommitBeforeData
	ibDatabaseLayer* conn() const;   // holder->EnsureConnection() (the pinned conn), or db_query when no holder

	// The holder that OWNS the per-save barrier state (created-set + deferred queue): this builder's
	// explicit holder, or the current thread / session holder when none was given (the db_query channel).
	// All ibSchemaBuilder instances of one save resolve to the same holder, so they share the state.
	ibDatabaseConnectionHolder* BarrierHolder() const;

	ibDatabaseConnectionHolder* m_holder;
};

#endif // !__SCHEMA_BUILDER_H__
