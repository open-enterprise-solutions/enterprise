#include "backend/query/structureBuilder.h"

#include "backend/query/schemaSnapshot.h"   // DiffSnapshots — the agnostic differ over two snapshots
#include "backend/query/schemaBuilder.h"    // ibSchemaBuilder::Reset / Flush — per-save barrier + deferred drain
#include "backend/databaseLayer/databaseLayer.h"        // ibDatabaseLayer — the connection it holds the TX on
#include "backend/databaseLayer/connectionHolder.h"     // ibDatabaseConnectionHolder::EnsureConnection
#include "backend/databaseLayer/databaseLayerDef.h"     // DATABASELAYER_FIREBIRD
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/backend_exception.h"
#include "backend/session/session.h"   // ibSession::Current()->Holder() — the DDL holder IS the session's
#include <set>   // UndoCreatedTables walks the barrier ledger
#include "appData.h"                                     // db_query — the default connection

ibDatabaseLayer* ibStructureBuilder::Conn() const
{
	// ⭐⭐ ONE HOLDER FOR THE WHOLE RESTRUCTURING, and the SESSION's is that holder.
	//
	// A holder owns a connection and pins it: while its transaction is open nobody else can take that
	// connection, and everyone who asks later sees the committed result. The session already has one —
	// the designer's session IS the DDL session — so there is nothing to invent here; what was missing
	// is that this builder never asked for it. `SetHolder` exists and was called from nowhere, so the
	// apply ran over `db_query` while the batches, the barrier and the deferred drain took the holder
	// they were handed. Two owners of one process, which is exactly what the trace showed: the
	// restructuring commit arriving at depth 2 (nested inside somebody else's transaction) and the
	// deferred drain running at active=1 — inside a transaction that had not committed yet, so
	// CREATE TRIGGER could not see the table the same apply had just created ("Table unknown").
	//
	// db_query stays the fallback for callers that have no session at all (codeRunner, tests).
	if (m_holder != nullptr)
		return m_holder->EnsureConnection().get();
	if (ibSession* session = ibSession::Current())
		return session->EnsureConnection().get();
	return db_query.get();
}

bool ibStructureBuilder::OnBeforeSave()
{
	m_changes.Clear();

	// ⭐⭐ THE SESSION'S HOLDER, ADOPTED FOR THE WHOLE APPLY. Everything below — Conn(), the batches,
	// the barrier and the deferred drain — must speak to ONE holder, or the apply has two owners and
	// its own commit lands nested inside somebody else's transaction. A custom holder is for the
	// pinpoint case (run another transaction's work inside one); a restructuring is not that case.
	if (m_holder == nullptr)
		if (ibSession* session = ibSession::Current())
			m_holder = session->Holder();

	// NO EXCLUSIVE DEMANDED HERE ANY MORE. It used to be taken unconditionally at this point, before the
	// diff had run — so a save that touched only modules and forms was refused for the same reason as one
	// that adds a column, and the refusal text contradicted itself by saying code-only changes need no
	// exclusive. The demand moved to ibStructureBatch::Flush, which raises it only when there are DDL steps
	// to execute; a save with nothing to run now goes through while other people are connected. The failure
	// therefore arrives later, from inside the open transaction — which is already handled: the DDL paths
	// throw, and OnAfterSave(rollback) rolls the build back.

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
	//
	// ⚠ AND A COMMIT CAN REFUSE — Firebird compiles the views and triggers a DDL transaction created
	// at COMMIT rather than at CREATE, so a bundle it cannot compile is rejected right here, with
	// every individual statement already reported as successful. Nothing is caught at this level: the
	// rollback of a refused commit belongs to ibDatabaseLayer::Commit (it is the only place that can
	// still tell there is a transaction to roll back), and the refusal travels up from there.
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

		// ⭐⭐ A REFUSAL LEAVES THIS TRANSACTION OPEN UNLESS IT IS CAUGHT HERE, and the deferred queue is
		// exactly where refusals now arrive: the maintenance (CREATE TRIGGER / CREATE VIEW) is deferred
		// past the DDL commit, and a trigger body our own renderer got wrong RAISES rather than
		// returning false. Only the `false` path used to roll back, so the throw walked out over an
		// open transaction — and the next apply met it as a DEADLOCK on the tables this one still held.
		//
		// That is the "restructuring cannot be interrupted" fault, and this is where it was born: the
		// abort path existed for the return code and not for the exception.
		bool ok = false;
		try {
			ok = ibSchemaBuilder(m_holder).Flush();
		}
		catch (const ibBackendException& err) {
			if (Conn()->IsActiveTransaction())
				Conn()->RollBack();
			UndoCreatedTables();   // put the schema back where the baseline still says it is
			throw;
		}
		catch (...) {
			if (Conn()->IsActiveTransaction())
				Conn()->RollBack();
			UndoCreatedTables();
			throw;
		}
		if (!ok) {
			Conn()->RollBack();
			UndoCreatedTables();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		Conn()->Commit();
	}
#endif
	return 1;
}

// ⭐⭐ THE SECOND COMMIT'S COMPENSATION — what makes a two-phase apply behave like one.
//
// Firebird cannot build the maintenance in the same transaction as the tables it addresses, so an
// apply is TWO commits and can fail between them. The first is already durable at that point, and
// nothing above rolls it back: the active configuration is not published (OnAfterSaveDatabase), so
// the baseline still describes the database as it was BEFORE — but the tables from the first commit
// are there, and a differ working from that baseline would try to create them again and be refused
// with "table already exists", permanently.
//
// So the created tables go away. The ledger the barrier keeps is exactly the right list: it holds
// what THIS apply created, which is the same set the barrier deferred writes for. A derived table
// costs nothing to drop — every row in it is a function of the movements and is recomputed on the
// next apply — and the tables are empty in any case, because the writes that would have filled them
// are the ones that just failed.
//
// ⚠ WHAT THIS DOES NOT UNDO: an ALTER on a table that already existed. Adding a column to a live
// table cannot be reversed without knowing it was not there before, and asking the database that is
// exactly the physical-introspection road this engine does not take. That case stays open by
// construction; it is narrower than it sounds, because the phase that fails here is the maintenance,
// and maintenance is built for tables the same apply created.
void ibStructureBuilder::UndoCreatedTables()
{
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	ibSchemaBuilder schema(m_holder);
	const std::set<wxString> created = schema.CreatedTables();
	if (created.empty())
		return;

	// ⚠ THROUGH L2, WITH `IF EXISTS` — not a hand-built string on the connection.
	//
	// Rendering and running are one step and it is level 2's (schemaBuilder.cpp), and here the rule
	// pays twice over: the renderer quotes the identifier, and it spells the engine's own "drop only
	// if present". Without that, dropping a table this apply did NOT get as far as creating is an
	// error — and on Firebird an error in a DDL transaction takes the whole transaction with it, so
	// ONE absent table would abort the compensation and leave every other created table standing.
	// That is precisely the permanent "table already exists" state this function exists to prevent.
	try {
		Conn()->BeginTransaction();
		for (const wxString& table : created)
			ibExecuteDdl(Conn(), ibDropTable(table, /*ifExists*/ true));
		Conn()->Commit();
	}
	catch (...) {
		// Best effort, and deliberately silent: the caller is already carrying the REAL failure and
		// that is the one worth reporting. A drop that cannot run leaves a table the next apply will
		// refuse to create — worse than this, but not worth replacing the original diagnosis with.
		if (Conn()->IsActiveTransaction()) {
			try { Conn()->RollBack(); } catch (...) {}
		}
	}
#endif
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
