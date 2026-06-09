#ifndef __TEMP_TABLE_MANAGER_H__
#define __TEMP_TABLE_MANAGER_H__

// ibTempTableManager — the UNIVERSAL (driver-agnostic) L3 lifetime owner for a DB temporary table.
// It materialises a computed / RAM intermediate (an ibQueryRamTable — a register slice / balance,
// a nested-subquery result, any non-SQL leaf) into a REAL temp table, vends an
// ibDbTempTableQueryable over it (then read / joined SERVER-SIDE like any ordinary DB source), and
// DROPs it on destruction.
//
// HOLDER-ANCHORED LIFETIME. A DB temp table is connection-scoped (create / fill / query / drop must
// all run on the SAME physical connection), and the connection identity IS the holder. So the
// manager pins the holder's connection through an owned ibConnectionScope for its whole life: the
// holder is the lifetime anchor, the scope the RAII handle. Nested ibDatabaseQueryBuilder work on
// the same holder inherits this one connection, so the CREATE, the INSERTs, the JOIN that reads it,
// and the DROP all land on the one connection the temp table lives on.
//
// DRIVEN ENTIRELY BY THE L1 DICTIONARY — no per-driver fork: the temp facts come from the
// connection's ibTempTableDialect (createPrefix / onCommit / dropPrefix / autoDrops) and the column
// SQL types from its main ibDialectDictionary type-map. A driver that vends a temp dialect gets temp
// tables; one that does not (Firebird today -> nullptr) makes Materialise return null and the caller
// stays on the RAM composer (the always-works floor). Fail-fast: a runtime CREATE / INSERT failure
// also returns null (graceful RAM fallback) after best-effort dropping the half-built table.
// (docs/temp-db.md)

#include "tempTableQueryable.h"                          // ibDbTempTableQueryable (the vended source)
#include "backend/databaseLayer/databaseLayer.h"         // ibTempTableDialect
#include "backend/databaseLayer/connectionScope.h"       // ibConnectionScope (pins the holder's connection)

#include <memory>

class ibQueryRamTable;
class ibMetaData;
class ibDatabaseConnectionHolder;

class BACKEND_API ibTempTableManager
{
public:
	// Materialise `rows` into a DB temp table on `holder`'s connection. Returns null when the driver
	// vends no temp dialect (=> RAM), the connection is unavailable, `rows` has no columns, or a
	// runtime CREATE / INSERT fails (=> RAM, half-built table dropped). On success the manager OWNS
	// the table (DROP on dtor) and PINS the holder's connection for its lifetime, so the caller MUST
	// keep the manager alive while it reads / joins Queryable(); the row data the caller needs must be
	// materialised out before the manager dies.
	static std::unique_ptr<ibTempTableManager> Materialise(ibDatabaseConnectionHolder* holder,
	                                                       const ibQueryRamTable& rows,
	                                                       const ibMetaData* metaData = nullptr);

	~ibTempTableManager();
	ibTempTableManager(const ibTempTableManager&)            = delete;
	ibTempTableManager& operator=(const ibTempTableManager&) = delete;

	// The temp table as a first-class DB source — From()/Join() it like any catalog (it inherits the
	// ordinary DB provider, so it joins SERVER-SIDE). Stable for the manager's life.
	const ibDbTempTableQueryable* Queryable() const { return m_queryable.get(); }
	const wxString&               TableName() const { return m_tableName; }

private:
	ibTempTableManager(ibConnectionScope scope, ibDatabaseConnectionHolder* holder,
	                   ibTempTableDialect dialect, wxString tableName,
	                   std::unique_ptr<ibDbTempTableQueryable> queryable);

	ibConnectionScope                        m_scope;       // pins the holder's connection (the temp table's lifetime anchor)
	ibDatabaseConnectionHolder*              m_holder;      // for the DROP query (nests on the still-pinned scope in the dtor)
	ibTempTableDialect                       m_dialect;     // L1 facts (copied) — drives the DROP
	wxString                                 m_tableName;
	std::unique_ptr<ibDbTempTableQueryable>  m_queryable;
};

#endif // __TEMP_TABLE_MANAGER_H__
