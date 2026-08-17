#include "backend/query/structureBuilder.h"

#include "backend/query/schemaSnapshot.h"   // DiffSnapshots — the agnostic differ over two snapshots
#include "backend/query/schemaBuilder.h"    // ibSchemaBuilder::Reset / Flush — per-save barrier + deferred drain
#include "backend/databaseLayer/databaseLayer.h"        // ibDatabaseLayer — the connection it holds the TX on
#include "backend/databaseLayer/connectionHolder.h"     // ibDatabaseConnectionHolder::EnsureConnection
#include "backend/databaseLayer/databaseLayerDef.h"     // DATABASELAYER_FIREBIRD
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/backend_exception.h"
#include "backend/session/session.h"   // ibSession::Current()->Holder() — the DDL holder IS the session's
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
			UndoAppliedDdl();   // put the schema back where the baseline still says it is
			throw;
		}
		catch (...) {
			if (Conn()->IsActiveTransaction())
				Conn()->RollBack();
			UndoAppliedDdl();
			throw;
		}
		if (!ok) {
			Conn()->RollBack();
			UndoAppliedDdl();
			return DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		Conn()->Commit();
	}
#endif
	return 1;
}

// ⭐⭐ THE SECOND COMMIT'S COMPENSATION — what makes a two-phase apply behave like one.
//
// ⚠⚠ FIREBIRD TWO-PHASE PATCH, AND THE CANONICAL DESCRIPTION OF ITS ONE HOLE. Everything in this
// function — and the two TableExists guards in schemaSnapshot.cpp that reference it — exists only
// because a Firebird apply is two commits. What the compensation CANNOT undo is a pre-existing
// derived table the first commit dropped (no RENAME TABLE on Firebird to park it, no statement in
// the ledger that knows its shape) — that absence is what the guards absorb, loudly, one apply
// later. Postgres runs the whole apply as ONE transaction and none of this machinery activates.
// Decided 2026-08-17 to KEEP this shape: the hole is narrow, loud (the Warn below) and
// self-converging. If it ever bites on live data, the first move is an undo-CREATE of the derived
// table from its baseline declaration (the diff knows the full shape at the point of the drop) —
// not a rework of the phases.
//
// Firebird cannot build the maintenance in the same transaction as the tables it addresses, so an
// apply is TWO commits and can fail between them. The first is already durable at that point, and
// nothing above rolls it back: the active configuration is not published (OnAfterSaveDatabase), so
// the baseline still describes the database as it was BEFORE the apply — while the first commit's
// DDL is all there. A differ working from that baseline then re-issues work already done and is
// refused — "table already exists" for a create, and for a column the first commit DROPPED, a DROP
// of something no longer present: refused, rolled back, on every retry, permanently.
//
// So the whole first commit is unwound from the barrier's UNDO LEDGER — one inverse action per
// statement, recorded by ibSchemaBuilder::Execute from the statements themselves (never from
// reading the database), replayed here in REVERSE: added columns come off, dropped ones return
// (EMPTY — that loss is the first commit's, and the ledger SAYS it), altered types are restored
// from the shape the statement carried, created tables are dropped wholesale (their clauses were
// never recorded — the table-drop takes them along). What cannot come back — a dropped table, a
// dropped index — is warned about instead of being silently skipped.
void ibStructureBuilder::UndoAppliedDdl()
{
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	ibSchemaBuilder schema(m_holder);
	const std::vector<std::function<void(ibDatabaseLayer*)>> undo = schema.UndoActions();
	if (undo.empty())
		return;

	// ⚠ THROUGH L2, AND NOTHING GUARDED — every entry describes work that actually RAN (Execute
	// records it after the statement succeeds), so at replay time its object exists by
	// construction, and no IF EXISTS form is needed (Firebird, the barrier dialect, has none).
	// The predecessor spelled `DROP TABLE IF EXISTS` here — a syntax error on the one engine the
	// compensation runs on, swallowed by the catch below on its very first statement.
	try {
		Conn()->BeginTransaction();
		for (auto it = undo.rbegin(); it != undo.rend(); ++it)
			(*it)(Conn());
		Conn()->Commit();
	}
	catch (...) {
		// Best effort, and deliberately silent: the caller is already carrying the REAL failure and
		// that is the one worth reporting. An undo that cannot run leaves the schema ahead of the
		// baseline — worse than this, but not worth replacing the original diagnosis with.
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
