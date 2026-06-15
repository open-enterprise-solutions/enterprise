#ifndef __STRUCTURE_BUILDER_H__
#define __STRUCTURE_BUILDER_H__

// ibStructureBuilder — the config-save STRUCTURE + SEED engine (the L3-2 tier). It is METADATA-AGNOSTIC:
// it works only on ibSchemaSnapshot (the declarative "what the schema is"), never on a configuration. It
// OWNS the restructuring transaction: at write-start it discards any leftover unfinished build (rolls back
// a straggling transaction) and opens a transaction on its HOLDER's connection, held until the save
// finishes (commit / rollback). The holder is the connection lifetime anchor (same seam as L2
// ibDatabaseQueryBuilder / L3 ibTempTableManager) — pluggable to the session holder, a dedicated
// restructuring holder, or null (= the local db_query channel). Set before OnBeforeSave.
//
// Metadata's only job is to BUILD snapshots (ContributeTables), hand them in, and drive the three save
// events; it keeps no knowledge of how the builder migrates or which transaction it runs in.

#include "backend.h"
#include "backend/restructureInfo.h"   // ibRestructureInfo — the builder OWNS its change log (member below)

class ibSchemaSnapshot;
class ibDatabaseLayer;
class ibDatabaseConnectionHolder;

class BACKEND_API ibStructureBuilder
{
public:
	// Pick the holder the restructuring runs through (session / a dedicated restructuring holder).
	// Null (default) => the local db_query channel. Set before OnBeforeSave.
	void SetHolder(ibDatabaseConnectionHolder* holder) { m_holder = holder; }

	// before write — discard any leftover unfinished build (roll back its straggling transaction), reset
	// the per-save barrier + change log, and OPEN the restructuring transaction on the connection. Returns
	// false if the transaction could not be opened (the save must abort).
	bool OnBeforeSave();

	// during write — diff baseline -> target into DDL (columns) + data (value rows), inside the held
	// transaction. baseline == null => create-all. Records the delta into the change log (GetChanges).
	int  OnSave(const ibSchemaSnapshot* baseline, const ibSchemaSnapshot& target);

	// after write — CLOSE the transaction: rollback if asked, else commit (+ on Firebird flush the seed
	// rows deferred past the DDL commit, in their own transaction). The just-created tables are durable.
	int  OnAfterSave(bool rollback);

	// full rebuild (ReCreateDatabase): owns its whole transaction — drop all of `target`'s tables, then
	// create + seed them all, commit. Rolls back on error.
	int  Recreate(const ibSchemaSnapshot& target);

	// The structured record of what the last run changed — CREATE / ALTER / DROP table, add / change /
	// remove column, add / change / remove value row. Read out after the run for the apply-change UI.
	const ibRestructureInfo& GetChanges() const { return m_changes; }

private:
	ibDatabaseLayer* Conn() const;   // m_holder->EnsureConnection(), or db_query when no holder
	int FlushDeferredFirebird();     // FB two-phase: drain seeds deferred past the DDL commit, own TX (no-op elsewhere)

	ibDatabaseConnectionHolder* m_holder = nullptr;
	ibRestructureInfo           m_changes;
};

#endif // !__STRUCTURE_BUILDER_H__
