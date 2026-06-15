#include "backend/query/schemaBuilder.h"

#include "backend/appData.h"                              // db_query (the local DDL channel)
#include "backend/databaseLayer/databaseLayer.h"          // ibDatabaseLayer (GetDialect / RunQuery / Commit / BeginTransaction)
#include "backend/databaseLayer/connectionHolder.h"       // ibDatabaseConnectionHolder::EnsureConnection + DdlCreatedTables / DdlDeferredWrites
#include "backend/databaseLayer/connectionPool.h"         // ibConnectionPool::CurrentHolder (the db_query channel's holder)
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibQueryRenderer::RenderDDL + ibDdlStatement (kind / table)

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

bool ibSchemaBuilder::BarrierActive() const
{
	return conn()->GetDialect().m_ddlCommitBeforeData;
}

bool ibSchemaBuilder::AlterTableMultiClause() const
{
	return conn()->GetDialect().m_alterTableMultiClause;
}

int ibSchemaBuilder::Execute(const ibDdlStatement& ddl)
{
	// On a barrier dialect, remember a freshly CREATEd table — a same-TX write to it must wait for
	// the commit (it is not yet durable for the prepared-INSERT path).
	if (ddl.m_kind == ibDdlKind::CreateTable && BarrierActive())
		if (ibDatabaseConnectionHolder* h = BarrierHolder())
			h->DdlCreatedTables().insert(ddl.m_table);

	ibDatabaseLayer* c = conn();
	ibQueryRenderer renderer(c->GetDialect());
	return c->RunQuery(renderer.RenderDDL(ddl));
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

bool ibSchemaBuilder::Flush()
{
	// The orchestrator has already committed the DDL and opened the data-phase TX; just run the
	// writes that targeted just-created tables (empty queue off a barrier dialect → no-op).
	ibDatabaseConnectionHolder* h = BarrierHolder();
	if (h == nullptr)
		return true;
	bool ok = true;
	for (auto& work : h->DdlDeferredWrites())
		if (!work()) { ok = false; break; }
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
