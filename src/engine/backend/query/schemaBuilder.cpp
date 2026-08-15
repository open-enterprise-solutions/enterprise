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
int ibSchemaBuilder::Execute(const ibDdlStatement& ddl)
{
	// ⭐⭐ ON A BARRIER DIALECT, REMEMBER EVERY TABLE WHOSE SHAPE THIS SAVE CHANGED — not only the ones
	// it created. A same-TX statement against such a table must wait for the commit: it is the SHAPE
	// that is not durable yet, and Firebird's prepare answers about the shape it can see.
	//
	// ⚠ It used to record CREATE alone, and the case that exposed the difference was an ordinary edit:
	// add an analytic to a register, and the movements table gets `ALTER TABLE … ADD fld…`. The totals
	// bundle is then rebuilt, the rebuild READS the movements naming the new column, and the prepare
	// fails with "Column unknown FLD…" — about a column the very same apply had just added, three
	// statements earlier. The table existed, so nothing deferred; only its shape was new.
	//
	// The question is "did this save change what this table LOOKS like", and every DDL that alters a
	// table answers yes.
	const bool shapeChanged = ddl.m_kind == ibDdlKind::CreateTable
	                       || ddl.m_kind == ibDdlKind::AddColumn
	                       || ddl.m_kind == ibDdlKind::DropColumn
	                       || ddl.m_kind == ibDdlKind::AlterColumn
	                       || ddl.m_kind == ibDdlKind::AlterTable;
	if (shapeChanged && BarrierActive())
		if (ibDatabaseConnectionHolder* h = BarrierHolder())
			h->DdlCreatedTables().insert(ddl.m_table);

	// (No immediate pre-unique-index dedup here: a raw L1 DELETE by table name broke on a barrier dialect —
	// a table CREATEd in this same DDL TX is not yet visible to a DML statement, so the dedup hit
	// "table unknown" and aborted the apply. Fresh tables are empty (nothing to dedup); duplicate-healing
	// for a migration over existing data belongs on the deferred / L2 path, not an eager raw-L1 DELETE.)
	//
	// RENDER AND RUN ARE ONE STEP, AND IT IS L2'S. Spelling them here meant building a renderer out of
	// a dictionary this tier has no other reason to hold — and picking, at this tier, which of the
	// driver's two run doors a final statement goes through.
	return ibExecuteDdl(conn(), ddl);
}

ibDatabaseLayer& ibSchemaBuilder::Connection() const
{
	return *conn();
}

bool ibSchemaBuilder::RunOrDefer(const wxString& table, std::function<bool()> work)
{
	ibDatabaseConnectionHolder* h = BarrierHolder();
	if (BarrierActive() && h != nullptr && h->DdlCreatedTables().count(table) != 0) {
		h->DdlDeferredWrites().push_back(std::move(work));
		return true;               // deferred — real result surfaces in Flush
	}
	return work();                 // run now, report its success
}

bool ibSchemaBuilder::RunOrDefer(const wxString& tableA, const wxString& tableB, std::function<bool()> work)
{
	ibDatabaseConnectionHolder* h = BarrierHolder();
	if (BarrierActive() && h != nullptr
	    && (h->DdlCreatedTables().count(tableA) != 0 || h->DdlCreatedTables().count(tableB) != 0)) {
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
	h->DdlCreatedTables().clear();
	return ok;
}

void ibSchemaBuilder::Reset()
{
	if (ibDatabaseConnectionHolder* h = BarrierHolder()) {
		h->DdlCreatedTables().clear();
		h->DdlDeferredWrites().clear();
	}
}
