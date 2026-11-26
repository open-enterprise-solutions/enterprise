#ifndef __QUERY_TEMP_STORE_H__
#define __QUERY_TEMP_STORE_H__

////////////////////////////////////////////////////////////////////////////
// L4-1 — the living set of temp tables and locks. What lets ONE query's result be read by ANOTHER,
// and ONE query's lock outlive its transaction.
////////////////////////////////////////////////////////////////////////////
//
// A temp table is made by a statement (`SELECT … INTO Sales`) and read by the statements after it.
// The question this type answers is HOW LONG "after" lasts, and it is a question about OWNERSHIP
// rather than about queries:
//
//   * inside one package, the tables live as long as the package — the package owns the store;
//   * across SEVERAL queries, they live as long as somebody HOLDS the store — which is what the
//     script-visible `TempDataManager` is: a value you make, hand to any number of queries, and
//     `Close()` when the tables are no longer wanted.
//
// Both are the same store; only the holder differs. That is why this is a small owning type rather
// than a scope or a flag: "who keeps these alive" has exactly one answer at a time, and it is the
// object that holds it.
//
// ⭐⭐ A LOCK IS KEPT HERE FOR THE SAME REASON — this is what a managed lock IS. A package's LOCK holds to
// the end of its transaction; a query handed a manager is held by the MANAGER instead, and only by it:
// past a commit or a rollback, with no transaction at all, for as many queries as the manager is reused
// for — until `Close()`, an `UNLOCK` of its name in a later package, or the manager's own end. The names are
// ONE space all the same: `INTO Held` after `LOCK Held FROM …` would name two things, and a reader could not
// tell which `Held` is. A package that owns its store forgets its locks when it ends (ForgetLocks) — the
// transaction holds them.
//
// A stored table is a SNAPSHOT (`ibQueryRamTable`) wrapped as an ordinary queryable, so a statement
// selecting from it goes through the same door as one selecting from a catalog — `ResolveSource`
// consults the store BEFORE the metaobject factory, and a temp table is a BARE name because it has
// no metaclass to prefix it with.
//
// See docs/private/query-constructor.md §5c and docs/private/record-locks.md.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/query/queryRamTable.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

class ibBackendQueryable;
class ibMetaData;

class BACKEND_API ibQueryTempDataStore
{
public:
	ibQueryTempDataStore() = default;
	~ibQueryTempDataStore();

	// Non-copyable: the store holds the tables under their names, and a second store of the same names
	// is a second answer to "which table does this statement read". (A reader shares a table while it
	// reads it — the rows of a dropped one go with its last reader.)
	ibQueryTempDataStore(const ibQueryTempDataStore&)            = delete;
	ibQueryTempDataStore& operator=(const ibQueryTempDataStore&) = delete;

	// A table or a lock of that name — one name space, see the banner.
	bool Has(const ibString& name) const;

	// Take a snapshot under `name`. The caller checks Has() first and reports the clash in its own
	// words — a name declared twice is a mistake worth naming, because the second declaration would
	// otherwise silently shadow the first and the reader could not tell which one was read.
	//
	// `indexedColumns` — the names the table is INDEXED BY. An index here is not a promise for
	// later: the stored table builds a value → rows map over those columns, and a read that filters
	// one of them by equality is answered from the map instead of by walking every row. That is
	// what an index IS, and a clause that only wrote itself into the text would be a control over
	// nothing. Names the snapshot does not have are ignored — the statement that made the table
	// decides its columns, and an index over a column it did not project is simply not there.
	//
	// `metaData` — the application the query ran on behalf of: the stored table answers it, so a reference
	// in it can be followed to its catalog (a walk, «IN HIERARCHY»), as it can in the table it was read from.
	void Put(const ibString& name, ibQueryRamTable&& rows,
	         const std::vector<ibString>& indexedColumns = {}, const ibMetaData* metaData = nullptr);
	// …and a lock a LOCK took, under the name it gave (`LOCK <name> FROM`; empty — under none, and only Close lets it go):
	// its holds, one per table, each its provider's and meaning nothing here — emptied, the locks go. The store holds
	// a SHARE: the transaction keeps its own of a LOCK it holds, a manager's store is the only holder.
	void Put(const ibString& name, std::shared_ptr<std::vector<std::shared_ptr<void>>> lock);

	// Release one table. False when the name is not there — the caller decides whether that is an
	// error (in a package it is: a drop of something never made is a typo or a statement that did
	// not run).
	bool Drop(const ibString& name);
	// …and the lock's pair, UNLOCK: let go of the holds kept here NOW — a manager's lock goes, a transaction's
	// lives to its end. False as Drop is.
	bool Unlock(const ibString& name);

	// Release everything: the tables, and every lock NOW. This is `TempDataManager.Close()`.
	// ⚠ Not the destructor's body: a package's own store dropped by a refusal mid-package must leave its locks
	// to the transaction, which Close would take from it.
	void Close();

	// Stop holding the locks, releasing none by name: one the transaction holds too stays held to its end, one
	// this store alone held — a manager's — goes with this share. A package's own store, at the end of it — and
	// the destructor, so a manager that is gone has unlocked what it held.
	void ForgetLocks();

	bool Empty() const { return m_sources.empty() && m_locks.empty(); }

	// The map an ibTempSourceScope is installed over. The scope holds a POINTER to it, so tables
	// added while the scope is active are visible to the statements that follow — which IS the
	// contract a package rests on.
	const std::map<ibString, std::shared_ptr<const ibBackendQueryable>>& Sources() const { return m_sources; }

private:
	std::map<ibString, std::shared_ptr<const ibBackendQueryable>> m_sources;   // the tables themselves, by name
	std::vector<std::pair<ibString, std::shared_ptr<std::vector<std::shared_ptr<void>>>>> m_locks;   // by the name a LOCK gave, as written
};

#endif
