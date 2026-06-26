#include "backend/query/structureBuilder.h"

#include "backend/query/schemaSnapshot.h"   // DiffSnapshots — the agnostic differ over two snapshots
#include "backend/query/schemaBuilder.h"    // ibSchemaBuilder::Reset / Flush — per-save barrier + deferred drain
#include "backend/databaseLayer/databaseLayer.h"        // ibDatabaseLayer — the connection it holds the TX on
#include "backend/databaseLayer/connectionHolder.h"     // ibDatabaseConnectionHolder::EnsureConnection
#include "backend/databaseLayer/databaseLayerDef.h"     // DATABASELAYER_FIREBIRD
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/backend_exception.h"                   // ibBackendException — the exclusive-gate catch
#include "appData.h"                                     // db_query — the default connection

ibDatabaseLayer* ibStructureBuilder::Conn() const
{
	// The holder's pinned conn (same one the open TX lives on); db_query when no holder set.
	// Both db_query and EnsureConnection vend shared_ptr<ibDatabaseLayer> — take the raw layer off each.
	return m_holder != nullptr ? m_holder->EnsureConnection().get() : db_query.get();
}

bool ibStructureBuilder::OnBeforeSave()
{
	m_changes.Clear();

	// DDL needs DB-wide exclusive (monopoly) — acquire it first; a failure aborts the save (recorded to
	// the change log for the apply UI). All exception types are caught: the gate delegates to the session
	// registry, which has its own error paths (queue rejection / session-not-registered race).
	try {
		ibRestructureInfo::RequireExclusiveForDDL();
	} catch (const ibBackendException& e) {
		m_changes.AppendError(e.GetErrorDescription());
		return false;
	} catch (const std::exception& e) {
		m_changes.AppendError(wxString::FromUTF8(e.what()));
		return false;
	} catch (...) {
		m_changes.AppendError(_("Unknown error acquiring exclusive mode"));
		return false;
	}

	// Discard a leftover unfinished build — roll back its straggling transaction so this save starts clean.
	if (Conn()->IsActiveTransaction())
		Conn()->RollBack();

	ibSchemaBuilder(m_holder).Reset();   // per-save barrier tracking (on the holder: created tables + deferred seed writes)

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	Conn()->BeginTransaction();   // hold the restructuring transaction on this connection until OnAfterSave
	if (!Conn()->IsActiveTransaction()) {
		ibRestructureInfo::ReleaseAutoExclusive();   // begin failed — drop the exclusive we just took
		return false;
	}
#endif
	return true;
}

int ibStructureBuilder::OnSave(const ibSchemaSnapshot* baseline, const ibSchemaSnapshot& target)
{
	// One diff migrates everything: structure (CREATE/ALTER/DROP columns) + data (INSERT/UPDATE/DELETE value
	// rows), inside the held transaction. baseline == null => create-all. A row into a just-created table is
	// deferred past the DDL commit on Firebird (the batch barrier); other dialects fill it in-transaction.
	return DiffSnapshots(baseline, target, m_holder, &m_changes);
}

int ibStructureBuilder::OnAfterSave(bool rollback)
{
	// Pair the exclusive acquired in OnBeforeSave — released on EVERY exit (commit / rollback / error).
	struct AutoRelease { ~AutoRelease() { ibRestructureInfo::ReleaseAutoExclusive(); } } autoRelease;

	if (rollback) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		if (Conn()->IsActiveTransaction())
			Conn()->RollBack();
#endif
		return 1;
	}

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	if (!Conn()->IsActiveTransaction())
		return DATABASE_LAYER_QUERY_RESULT_ERROR;

	// Commit the restructuring (DDL + the metadata blob the Storage wrote in the same transaction).
	Conn()->Commit();
	return FlushDeferredFirebird();   // FB: drain the seeds deferred past that commit, in their own TX
#else
	return 1;
#endif
}

int ibStructureBuilder::FlushDeferredFirebird()
{
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	// The seed rows into tables created THIS save were deferred past the DDL commit (the ibSchemaBuilder
	// barrier — FB can't populate a same-TX freshly-created table). The tables are durable after that
	// commit, so flush the deferred writes in their OWN transaction. No-op off Firebird (empty queue).
	if (Conn()->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD) {
		Conn()->BeginTransaction();
		if (!ibSchemaBuilder(m_holder).Flush()) {
			Conn()->RollBack();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		Conn()->Commit();
	}
#endif
	return 1;
}

int ibStructureBuilder::Recreate(const ibSchemaSnapshot& target)
{
	// Standalone full rebuild — owns its whole transaction.
	m_changes.Clear();
	if (Conn()->IsActiveTransaction())
		Conn()->RollBack();
	ibSchemaBuilder(m_holder).Reset();

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	Conn()->BeginTransaction();
#endif

	// Errors propagate as EXCEPTIONS now — on any failure roll the rebuild back and rethrow (mirrors the
	// storage's OnSaveDatabase). A successful diff returns a non-error sentinel; affected-row counts (0 on
	// DDL or an empty cleanup) are NOT failures.
	try {
		DiffSnapshots(&target, ibSchemaSnapshot(), m_holder, &m_changes);   // target -> empty: DROP every table
		DiffSnapshots(nullptr, target, m_holder, &m_changes);              // empty -> target: CREATE + SEED every table
	}
	catch (...) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		if (Conn()->IsActiveTransaction())
			Conn()->RollBack();
#endif
		throw;
	}

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	Conn()->Commit();
	return FlushDeferredFirebird();   // FB: drain the deferred seeds in their own TX
#endif
	return 1;
}
