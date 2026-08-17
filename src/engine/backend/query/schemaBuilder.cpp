#include "backend/query/schemaBuilder.h"

#include "backend/appData.h"                              // db_query (the local DDL channel)
#include "backend/databaseLayer/databaseLayer.h"          // ibDatabaseLayer (Commit / BeginTransaction)
#include "backend/databaseLayer/connectionHolder.h"       // ibDatabaseConnectionHolder::EnsureConnection + DdlCreatedTables / DdlDeferredWrites
#include "backend/databaseLayer/connectionPool.h"         // ibConnectionPool::CurrentHolder (the db_query channel's holder)
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibDdlStatement (kind / table) + the L2 capability accessors
#include "backend/logger/logger.h"                        // ibLog->Warn — report the pre-UNIQUE duplicate-key cleanup

ibSchemaBuilder::ibSchemaBuilder(ibDatabaseConnectionHolder* holder)
	: m_holder(holder)
{
}

ibDatabaseLayer* ibSchemaBuilder::conn() const
{
	// The holder's pinned/scoped conn (same one its open TX lives on); db_query when no holder given.
	// Both db_query and EnsureConnection vend shared_ptr<ibDatabaseLayer> — take the raw layer off each.
	return m_holder != nullptr ? m_holder->EnsureConnection().get() : db_query.get();
}

ibDatabaseConnectionHolder* ibSchemaBuilder::BarrierHolder() const
{
	// The barrier state's owner: the explicit holder, or the db_query channel's per-thread holder
	// (designer save) when none was given. Every ibSchemaBuilder of one save resolves to the same holder.
	return m_holder != nullptr ? m_holder : ibConnectionPool::ThreadHolder();
}

// ⭐ THE FOUR QUESTIONS BELOW ARE L2'S, AND L2 ANSWERS THEM. Each of these used to read a FIELD of
// the dialect dictionary (`conn()->GetDialect().m_…`) — this tier spelling another tier's vocabulary,
// which is how a dictionary that changes shape leaves its readers compiling and wrong. They stay as
// methods because "on MY connection" is what the schema builder means every time, and that is the one
// thing these add: the connection. (databaseQueryBuilder.h — the capability accessors.)
bool ibSchemaBuilder::BarrierActive() const
{
	return ibDdlCommitsBeforeData(conn());
}

bool ibSchemaBuilder::AlterTableMultiClause() const
{
	return ibAlterTableMultiClause(conn());
}


// Before a UNIQUE index is created over EXISTING data, drop duplicate-key rows (keep exactly ONE per key)
// so CREATE UNIQUE INDEX cannot fail on pre-existing duplicates — the register's uniqueness-by-key, healed
// in place. Correlated self-join on the dialect's physical row id (RDB$DB_KEY / rowid / ctid): the outer
// table is referenced BY NAME (SQLite forbids an alias in DELETE), the inner by alias `b`; a row survives
// unless a smaller-row-id twin shares its full key. NULL key parts do not collide (`=` skips them), which
// matches a UNIQUE index's NULL rule. Empty row id (ODBC) -> skip; the create then fails loudly.
namespace {

// The INVERSE of one coalesced clause, for the compensation ledger: an added column is dropped by
// name; a dropped one is re-added from the shape the statement recorded (EMPTY — the data died
// with the first commit).
ibAlterClause InverseOfClause(const ibAlterClause& clause)
{
	ibAlterClause inv;
	inv.m_op     = clause.m_op == ibAlterOp::Add ? ibAlterOp::Drop : ibAlterOp::Add;
	inv.m_column = clause.m_column;
	return inv;
}

} // namespace

int ibSchemaBuilder::Execute(const ibDdlStatement& ddl)
{
	// (No immediate pre-unique-index dedup here: a raw L1 DELETE by table name broke on a barrier dialect —
	// a table CREATEd in this same DDL TX is not yet visible to a DML statement, so the dedup hit
	// "table unknown" and aborted the apply. Fresh tables are empty (nothing to dedup); duplicate-healing
	// for a migration over existing data belongs on the deferred / L2 path, not an eager raw-L1 DELETE.)
	//
	// RENDER AND RUN ARE ONE STEP, AND IT IS L2'S. Spelling them here meant building a renderer out of
	// a dictionary this tier has no other reason to hold — and picking, at this tier, which of the
	// driver's two run doors a final statement goes through.
	const int ret = ibExecuteDdl(conn(), ddl);

	// Bookkeeping AFTER the statement ran: a refused DDL threw above, changed nothing, and must not
	// be deferred around or compensated for.
	if (!BarrierActive())
		return ret;
	ibDatabaseConnectionHolder* h = BarrierHolder();
	if (h == nullptr)
		return ret;

	// ⭐⭐ ON A BARRIER DIALECT, REMEMBER EVERY TABLE WHOSE SHAPE THIS SAVE CHANGED — not only the ones
	// it created. A same-TX statement against such a table must wait for the commit: it is the SHAPE
	// that is not durable yet, and Firebird's prepare answers about the shape it can see.
	//
	// ⚠ It used to record CREATE alone, and the case that exposed the difference was an ordinary edit:
	// add an analytic to a register, and the movements table gets `ALTER TABLE … ADD fld…`. The totals
	// bundle is then rebuilt, the rebuild READS the movements naming the new column, and the prepare
	// fails with "Column unknown FLD…" — about a column the very same apply had just added, three
	// statements earlier. The table existed, so nothing deferred; only its shape was new.
	const bool shapeChanged = ddl.m_kind == ibDdlKind::CreateTable
	                       || ddl.m_kind == ibDdlKind::AddColumn
	                       || ddl.m_kind == ibDdlKind::DropColumn
	                       || ddl.m_kind == ibDdlKind::AlterColumn
	                       || ddl.m_kind == ibDdlKind::AlterTable;
	if (shapeChanged)
		h->DdlShapedTables().insert(ddl.m_table);

	// ⭐⭐ THE COMPENSATION LEDGER — the inverse of everything the first commit did to a PRE-EXISTING
	// object, recorded from the statements themselves, never from reading the database. A two-phase
	// apply can fail between its commits; the created tables are dropped wholesale (their undo below),
	// and the ALTERs used to be the documented hole: the first commit had already dropped a column for
	// good, the configuration was never published, and the next apply diffed the OLD baseline against
	// the new target — a DROP of a column no longer there, refused, forever. The inverse action per
	// statement closes that: replayed in reverse, the schema returns to what the unpublished baseline
	// still describes. Objects THIS save created need no clause-level undo — dropping the table takes
	// its columns and indexes with it.
	//
	// What stays irreversible is said at compensation time, not swallowed: a dropped TABLE cannot be
	// re-created (its shape left with it — and its data would not come back anyway), and a dropped
	// column returns EMPTY. Both are the price of the phase-one commit, not of this ledger.
	const bool onCreatedTable = h->DdlCreatedTables().count(ddl.m_table) != 0;
	switch (ddl.m_kind) {
	case ibDdlKind::CreateTable:
		if (!ddl.m_temporary) {
			h->DdlCreatedTables().insert(ddl.m_table);
			// No IF EXISTS — Firebird (the barrier dialect) has no such form and would refuse the
			// statement. None is needed: the ledger records only what actually RAN, so at replay
			// time the table exists by construction.
			const ibDdlStatement undo = ibDropTable(ddl.m_table);
			h->DdlUndoActions().push_back([undo](ibDatabaseLayer* c) { ibExecuteDdl(c, undo); });
		}
		break;
	case ibDdlKind::AddColumn:
		if (!onCreatedTable && !ddl.m_columns.empty()) {
			const ibDdlStatement undo = ibDropColumn(ddl.m_table, ddl.m_columns.front().m_name);
			h->DdlUndoActions().push_back([undo](ibDatabaseLayer* c) { ibExecuteDdl(c, undo); });
		}
		break;
	case ibDdlKind::DropColumn:
		if (!onCreatedTable && !ddl.m_columns.empty()) {
			const ibDdlStatement undo = ibAddColumn(ddl.m_table, ddl.m_columns.front());
			const wxString table = ddl.m_table, column = ddl.m_columns.front().m_name;
			h->DdlUndoActions().push_back([undo, table, column](ibDatabaseLayer* c) {
				ibExecuteDdl(c, undo);
				ibLog->Warn(wxT("restructure"), wxT("compensation"), wxString::Format(
					wxT("column %s.%s re-added EMPTY - its data died with the first commit"), table, column));
			});
		}
		break;
	case ibDdlKind::AlterColumn:
		if (!onCreatedTable && ddl.m_columns.size() > 1) {
			const ibDdlStatement undo = ibAlterColumn(ddl.m_table, ddl.m_columns[1]);
			h->DdlUndoActions().push_back([undo](ibDatabaseLayer* c) { ibExecuteDdl(c, undo); });
		}
		break;
	case ibDdlKind::AlterTable:
		if (!onCreatedTable && !ddl.m_alterClauses.empty()) {
			ibDdlStatement undo = ibAlterTable(ddl.m_table, {});
			for (auto it = ddl.m_alterClauses.rbegin(); it != ddl.m_alterClauses.rend(); ++it)
				undo.m_alterClauses.push_back(InverseOfClause(*it));
			h->DdlUndoActions().push_back([undo](ibDatabaseLayer* c) { ibExecuteDdl(c, undo); });
		}
		break;
	case ibDdlKind::CreateIndex:
		if (!onCreatedTable) {
			const ibDdlStatement undo = ibDropIndex(ddl.m_indexName, ddl.m_table);
			h->DdlUndoActions().push_back([undo](ibDatabaseLayer* c) { ibExecuteDdl(c, undo); });
		}
		break;
	case ibDdlKind::DropTable:
	case ibDdlKind::DropIndex:
		if (!onCreatedTable) {
			// Irreversible from here: the shape left with the object. Said, never swallowed — the
			// next apply will meet the absence and its refusal must be traceable to this moment.
			const wxString table = ddl.m_table;
			h->DdlUndoActions().push_back([table](ibDatabaseLayer*) {
				ibLog->Warn(wxT("restructure"), wxT("compensation"), wxString::Format(
					wxT("cannot restore dropped object of table %s - the next apply may refuse against the old baseline"), table));
			});
		}
		break;
	case ibDdlKind::Analyze:
		break;   // statistics — nothing to undo
	}
	return ret;
}

ibDatabaseLayer& ibSchemaBuilder::Connection() const
{
	return *conn();
}

bool ibSchemaBuilder::RunOrDefer(const wxString& table, std::function<bool()> work)
{
	// The deferral asks the WIDE question — "did this save change what the table looks like" — which
	// is DdlShapedTables, not the created list (see connectionHolder.h on why they are two sets).
	ibDatabaseConnectionHolder* h = BarrierHolder();
	if (BarrierActive() && h != nullptr && h->DdlShapedTables().count(table) != 0) {
		h->DdlDeferredWrites().push_back(std::move(work));
		return true;               // deferred — real result surfaces in Flush
	}
	return work();                 // run now, report its success
}

bool ibSchemaBuilder::RunOrDefer(const wxString& tableA, const wxString& tableB, std::function<bool()> work)
{
	ibDatabaseConnectionHolder* h = BarrierHolder();
	if (BarrierActive() && h != nullptr
	    && (h->DdlShapedTables().count(tableA) != 0 || h->DdlShapedTables().count(tableB) != 0)) {
		h->DdlDeferredWrites().push_back(std::move(work));
		return true;
	}
	return work();
}

std::set<wxString> ibSchemaBuilder::CreatedTables() const
{
	ibDatabaseConnectionHolder* h = BarrierHolder();
	return h != nullptr ? h->DdlCreatedTables() : std::set<wxString>();
}

std::vector<std::function<void(ibDatabaseLayer*)>> ibSchemaBuilder::UndoActions() const
{
	ibDatabaseConnectionHolder* h = BarrierHolder();
	return h != nullptr ? h->DdlUndoActions() : std::vector<std::function<void(ibDatabaseLayer*)>>();
}

bool ibSchemaBuilder::Flush()
{
	// The orchestrator has already committed the DDL and opened the data-phase TX; just run the
	// writes that targeted just-created tables (empty queue off a barrier dialect → no-op).
	ibDatabaseConnectionHolder* h = BarrierHolder();
	if (h == nullptr)
		return true;
	// ⭐⭐ A DEFERRED WORK MAY DEFER WORK OF ITS OWN, and the queue must survive that. The regeneration
	// runs here and asks the barrier the same question again inside itself (the key-hash fill), so a
	// `for (auto& work : queue)` was iterating a vector that grew under it: the reference dangled at
	// the next reallocation and the process died in std::function::operator(), with a stack pointing
	// at the dispatcher rather than at anything that had gone wrong. The nested item would have been
	// lost anyway — clear() below would have thrown it away unrun.
	//
	// Draining by BATCHES answers both: each round takes the queue away, so nothing added during the
	// round can move what is being walked, and whatever was added becomes the next round. It ends
	// when a round adds nothing, which is what "the work is finished" means here.
	bool ok = true;
	while (!h->DdlDeferredWrites().empty()) {
		std::vector<std::function<bool()>> batch;
		batch.swap(h->DdlDeferredWrites());
		for (auto& work : batch)
			if (!work()) { ok = false; break; }
		if (!ok)
			break;
	}
	h->DdlDeferredWrites().clear();
	// ⚠ The undo ledger survives a FAILED drain on purpose — the compensation is exactly what runs
	// next (ibStructureBuilder::UndoAppliedDdl reads it). Cleared only on full success.
	if (ok) {
		h->DdlCreatedTables().clear();
		h->DdlShapedTables().clear();
		h->DdlUndoActions().clear();
	}
	return ok;
}

void ibSchemaBuilder::Reset()
{
	if (ibDatabaseConnectionHolder* h = BarrierHolder()) {
		h->DdlCreatedTables().clear();
		h->DdlShapedTables().clear();
		h->DdlDeferredWrites().clear();
		h->DdlUndoActions().clear();
	}
}
