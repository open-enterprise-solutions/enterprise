#ifndef __DATABASE_MEMORY_BASE_H__
#define __DATABASE_MEMORY_BASE_H__

// ⭐⭐ THE BASE IN MEMORY — the query engine's memory: a base whose tables are rows held in the process and whose
// statements are the L2-1 IR itself, executed by the interpreter (databaseQueryInterpreter.h) with no SQL text between.
// It is the second executor the query engine needs (rows in memory) put where the first one is: behind ibDatabaseLayer,
// under the same builder, the same layout of a value into fields and the same codec reading them back — so the two
// answer one question by construction and not only by tests. docs/private/query-one-road.md.
//
// ONE FOR THE PROCESS, OVER EVERY DRIVER (ibQueryMemoryManager): a read goes here exactly when no one driver can read it — rows
// computed in memory, leaves of two connections, a question to a value with no SQL — so it is no driver's companion. A
// driver's own is its temp tables. Each execution takes a SPACE of it (ibMemorySpace), as a temp table lives on its
// connection: two reads in parallel neither see nor wait for each other.

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"          // ibFieldNameHash / ibFieldNameEqual — a table found by name without case
#include "backend/databaseLayer/databaseQueryInterpreter.h"   // ibMemoryTable — what a table here is
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/appDataCtorToken.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

class ibConnectionPool;

// A CONNECTION IS A SPACE — its tables are its own. It reads no SQL: RunQuery / PrepareStatement refuse the text in
// words; its dialect says it computes in memory (ibSqlFeatures::m_inMemory), and the builder hands its interpreter the
// IR, the DDL and the DML as they are (GetQueryInterpreter).
class BACKEND_API ibDatabaseLayerMemory : public ibDatabaseLayer
{
public:
	ibDatabaseLayerMemory();
	virtual ~ibDatabaseLayerMemory();

	// The name is not a place: a space is made empty and lives while its connection does.
	virtual bool Open(const ibString& strDatabase) override;
	virtual bool Close() override;
	virtual bool IsOpen() override;

	// Stops the statement running on this connection between two rows; nothing running, nothing to stop.
	virtual void Cancel() override;

	// ⚠ A SPACE OF ITS OWN, empty — a pool grows by cloning its first connection, and a read takes a space as a temp
	// table takes its connection: what one read copies in, another does not see.
	virtual ibDatabaseLayer* Clone() override;

	virtual bool TableExists(const ibString& table) override;
	virtual bool ViewExists(const ibString& view) override;
	virtual std::vector<ibString> GetTables() override;
	virtual std::vector<ibString> GetViews() override;
	virtual std::vector<ibString> GetColumns(const ibString& table) override;

	virtual ibString GetDatabaseLayerName() const override { return ibT("Memory"); }
	virtual const ibDialectDictionary& GetDialect() const override;
	virtual const ibTempTableDialect* GetTempTableDialect() const override;

	// The IR's executor over this space — reached by a static cast where the dialect says the base computes in memory
	// (ibSqlFeatures::m_inMemory).
	ibQueryInterpreter* GetQueryInterpreter();

	// What the interpreter does — the features a query may be built with for it, and nothing it does not.
	static const ibDialectDictionary& Dialect();
	// A temporary table is a table like any other here, dropped by the one who made it.
	static const ibTempTableDialect& TempDialect();

protected:
	virtual int DoRunQuery(const ibString& strQuery, bool bParseQuery) override;
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const ibString& strQuery) override;
	virtual ibPreparedStatement* DoPrepareStatement(const ibString& strQuery) override;

	// A transaction keeps the tables as it found them and puts them back on a rollback — the whole space, as SQLite's
	// transaction holds the whole file.
	virtual void DoBeginTransaction(const ibTxOptions& opts) override;
	virtual void DoCommit() override;
	virtual void DoRollBack() override;

private:
	friend class ibQueryInterpreter;

	using ibTableMap = std::unordered_map<ibString, ibMemoryTable, ibFieldNameHash, ibFieldNameEqual>;

	// THE SPACE'S TABLES; a statement holds the lock while it runs.
	struct ibTables {
		std::mutex m_mutex;
		ibTableMap m_byName;
	};

	ibTables                            m_tables;
	std::unique_ptr<ibTableMap>         m_snapshot;   // the tables as the open transaction found them
	std::unique_ptr<ibQueryInterpreter> m_interpreter;
	std::atomic<bool>                   m_cancelled{ false };
	bool                                m_open = false;
};

// ⭐ THE QUERIES' MEMORY MANAGER — the base in memory of the process: a pool that gives each execution a space of its own
// and forgets it when the execution ends (ibMemorySpace), with no ceiling of its own — so no space is ever idle, and none
// needs emptying. Made and held by the process (ibApplicationHost::GetQueryMemoryManager) with its token, as the process
// makes its other services.
class BACKEND_API ibQueryMemoryManager
{
public:
	explicit ibQueryMemoryManager(ib::AppDataCtorToken owner);
	~ibQueryMemoryManager();

	ibQueryMemoryManager(const ibQueryMemoryManager&) = delete;
	ibQueryMemoryManager& operator=(const ibQueryMemoryManager&) = delete;

	ibConnectionPool* GetConnectionPool() const { return m_pool.get(); }

private:
	std::unique_ptr<ibConnectionPool> m_pool;
	// The pool's first connection, which every space is cloned from — bound here, so nobody is given it.
	ibSingleConnectionHolder          m_seedHolder;
	ibConnectionScope                 m_seed;         // after the holder: taken through it, let go before it
};

// ⭐ A SPACE FOR ONE EXECUTION — what a query over sources that share no base runs on: each source is copied into it
// (ibTempTableManager::Materialise), and the query is then ONE statement in ONE space, built by the same builder and
// read by the same codec as on a server (docs/private/query-one-road.md § 7). A holder over the process's base in memory
// and the connection bound to it while the execution lives — so every copy and the read land in the same space — and
// the space goes with the execution: the pool forgets it (ibConnectionScope::Discard), and no space serves twice.
//
// It lives while the execution does: the composite source of the query holds it, and the result read from it holds the
// composite (ibDbResultSource), so the rows stay until the last of them is read.
class BACKEND_API ibMemorySpace
{
public:
	ibMemorySpace();
	~ibMemorySpace();

	ibMemorySpace(const ibMemorySpace&) = delete;
	ibMemorySpace& operator=(const ibMemorySpace&) = delete;

	// The holder a query in this space runs on.
	ibDatabaseConnectionHolder* Holder() { return &m_holder; }

private:
	ibSingleConnectionHolder m_holder;
	ibConnectionScope        m_scope;    // after the holder: taken through it, discarded before it goes
};

#endif // __DATABASE_MEMORY_BASE_H__
