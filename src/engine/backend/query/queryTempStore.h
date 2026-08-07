#ifndef __QUERY_TEMP_STORE_H__
#define __QUERY_TEMP_STORE_H__

////////////////////////////////////////////////////////////////////////////
// L4-1 — the living set of temp tables. What lets ONE query's result be read by ANOTHER.
////////////////////////////////////////////////////////////////////////////
//
// A temp table is made by a statement (`SELECT … INTO Sales`) and read by the statements after it.
// The question this type answers is HOW LONG "after" lasts, and it is a question about OWNERSHIP
// rather than about queries:
//
//   * inside one package, the tables live as long as the package — the package owns the store;
//   * across SEVERAL queries, they live as long as somebody HOLDS the store — which is what the
//     script-visible `TempTablesManager` is: a value you make, hand to any number of queries, and
//     `Close()` when the tables are no longer wanted.
//
// Both are the same store; only the holder differs. That is why this is a small owning type rather
// than a scope or a flag: "who keeps these alive" has exactly one answer at a time, and it is the
// object that holds it.
//
// A stored table is a SNAPSHOT (`ibQueryRamTable`) wrapped as an ordinary queryable, so a statement
// selecting from it goes through the same door as one selecting from a catalog — `ResolveSource`
// consults the store BEFORE the metaobject factory, and a temp table is a BARE name because it has
// no metaclass to prefix it with.
//
// See docs/query-constructor.md §5c.
//
////////////////////////////////////////////////////////////////////////////

#include "queryRamTable.h"

#include <map>
#include <memory>
#include <vector>

class ibBackendQueryable;

class BACKEND_API ibQueryTempTableStore
{
public:
	ibQueryTempTableStore() = default;
	~ibQueryTempTableStore();

	// Non-copyable: the store OWNS the tables, and a second owner of the same rows is a second
	// answer to "when do these die".
	ibQueryTempTableStore(const ibQueryTempTableStore&)            = delete;
	ibQueryTempTableStore& operator=(const ibQueryTempTableStore&) = delete;

	bool Has(const wxString& name) const;

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
	void Put(const wxString& name, ibQueryRamTable&& rows,
	         const std::vector<wxString>& indexedColumns = {});

	// Release one table. False when the name is not there — the caller decides whether that is an
	// error (in a package it is: a drop of something never made is a typo or a statement that
	// did not run).
	bool Drop(const wxString& name);

	// Release everything. This is `TempTablesManager.Close()` — and the destructor's whole body,
	// so forgetting to call it costs memory until the holder dies, never correctness.
	void Close();

	bool Empty() const { return m_sources.empty(); }

	// The map an ibTempSourceScope is installed over. The scope holds a POINTER to it, so tables
	// added while the scope is active are visible to the statements that follow — which IS the
	// contract a package rests on.
	const std::map<wxString, const ibBackendQueryable*>& Sources() const { return m_sources; }

private:
	std::map<wxString, const ibBackendQueryable*>          m_sources;   // by name (non-owning view)
	std::vector<std::unique_ptr<ibBackendQueryable>>       m_owned;     // the tables themselves
};

#endif
