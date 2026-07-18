#include "backend/query/schemaBuilder.h"

#include "backend/appData.h"                              // db_query (the local DDL channel)
#include "backend/databaseLayer/databaseLayer.h"          // ibDatabaseLayer (GetDialect / RunQuery / Commit / BeginTransaction)
#include "backend/databaseLayer/connectionHolder.h"       // ibDatabaseConnectionHolder::EnsureConnection + DdlCreatedTables / DdlDeferredWrites
#include "backend/databaseLayer/connectionPool.h"         // ibConnectionPool::CurrentHolder (the db_query channel's holder)
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibQueryRenderer::RenderDDL + ibDdlStatement (kind / table)
#include "backend/databaseLayer/preparedStatement.h"      // ibPreparedStatement — the index-introspection query
#include "backend/databaseLayer/databaseResultSet.h"      // ibDatabaseResultSet — its rows
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

bool ibSchemaBuilder::BarrierActive() const
{
	return conn()->GetDialect().m_ddlCommitBeforeData;
}

bool ibSchemaBuilder::AlterTableMultiClause() const
{
	return conn()->GetDialect().m_alterTableMultiClause;
}

bool ibSchemaBuilder::CanIntrospectIndexes() const
{
	return !conn()->GetDialect().m_indexListQuery.IsEmpty();
}

wxArrayString ibSchemaBuilder::PhysicalIndexes(const wxString& table) const
{
	wxArrayString names;
	ibDatabaseLayer* c = conn();
	const wxString sql = c->GetDialect().m_indexListQuery;
	if (sql.IsEmpty())
		return names;

	// One parameterised SELECT (dialect m_indexListQuery; `?` = table name — bound, never spliced) reads
	// the names of the table's existing indexes (trim the CHAR pad Firebird returns). Best-effort: any
	// failure yields an empty list, so the differ degrades to its metadata-only diff, never aborts.
	ibPreparedStatement*  stmt = nullptr;
	ibDatabaseResultSet*  rs   = nullptr;
	try {
		stmt = c->PrepareStatement(sql);
		if (stmt != nullptr) {
			stmt->SetParamString(1, table);
			rs = stmt->RunQueryWithResults();
			if (rs != nullptr)
				while (rs->Next())
					names.Add(rs->GetResultString(1).Trim());
		}
	}
	catch (...) {}
	if (rs   != nullptr) c->CloseResultSet(rs);
	if (stmt != nullptr) c->CloseStatement(stmt);
	return names;
}

// Before a UNIQUE index is created over EXISTING data, drop duplicate-key rows (keep exactly ONE per key)
// so CREATE UNIQUE INDEX cannot fail on pre-existing duplicates — the register's uniqueness-by-key, healed
// in place. Correlated self-join on the dialect's physical row id (RDB$DB_KEY / rowid / ctid): the outer
// table is referenced BY NAME (SQLite forbids an alias in DELETE), the inner by alias `b`; a row survives
// unless a smaller-row-id twin shares its full key. NULL key parts do not collide (`=` skips them), which
// matches a UNIQUE index's NULL rule. Empty row id (MySQL / ODBC) -> skip; the create then fails loudly.
static void DedupBeforeUniqueIndex(ibDatabaseLayer* c, const ibDdlStatement& ddl)
{
	const ibDialectDictionary& dia = c->GetDialect();
	if (dia.m_rowIdColumn.IsEmpty() || ddl.m_indexColumns.empty())
		return;

	auto quote = [&](const wxString& n) { return dia.m_identQuoteOpen + n + dia.m_identQuoteClose; };
	const wxString t     = quote(ddl.m_table);
	const wxString rowId = dia.m_rowIdColumn;   // an engine pseudo-column — used verbatim, never quoted

	wxString keyEq;
	for (const wxString& col : ddl.m_indexColumns) {
		const wxString q = quote(col);
		if (!keyEq.IsEmpty()) keyEq += wxT(" AND ");
		keyEq += wxT("b.") + q + wxT(" = ") + t + wxT(".") + q;
	}

	const wxString sql =
		wxT("DELETE FROM ") + t + wxT(" WHERE EXISTS (SELECT 1 FROM ") + t + wxT(" b WHERE ") +
		keyEq + wxT(" AND b.") + rowId + wxT(" < ") + t + wxT(".") + rowId + wxT(")");

	// Fail-safe: a dedup query that the engine rejects must NOT abort the apply. If it throws, we log and
	// let the UNIQUE create run anyway — it then either succeeds (no duplicates) or fails with a clean
	// duplicate-key error, which is a far better signal than a swallowed dedup-syntax abort.
	try {
		const int removed = c->RunQuery(sql);
		if (removed > 0 && ibLog != nullptr)
			ibLog->Warn(wxT("schema"), wxT("dedup"),
				wxString::Format(wxT("Removed %d duplicate-key row(s) from %s before creating unique index %s"),
					removed, ddl.m_table, ddl.m_indexName));
	}
	catch (...) {
		if (ibLog != nullptr)
			ibLog->Warn(wxT("schema"), wxT("dedup"),
				wxString::Format(wxT("Dedup query failed for %s; the unique index create will surface duplicates directly."), ddl.m_table));
	}
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

	// Heal duplicate keys before a UNIQUE index is created over existing data, so the CREATE cannot fail on
	// pre-existing duplicates (dialects with a physical row id; a no-op when the table is already unique).
	if (ddl.m_kind == ibDdlKind::CreateIndex && ddl.m_unique)
		DedupBeforeUniqueIndex(c, ddl);

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
