#include <gtest/gtest.h>

#include "backend/appHost.h"   // ibApplicationHost::GetQueryMemoryManager — the pool every space comes from
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseMemoryBase.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

// =============================================================================
// What ONE queries' memory manager for the whole process costs when many executions share it, printed - not a test.
// Run it by name:
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=QueryMemoryBench.*
// N threads, each running executions one after another. An execution takes a space, makes a table in it, fills it with
// ten rows, reads them back and lets the space go: the road a read over sources that share no base takes, the sources
// left out. Every pool call on it - the checkout, each statement finding its connection by its holder, the discard -
// goes to the manager's one pool, under its one lock, over the list of every space alive in the process.
// Beside it the same work on a pool per execution - the road before the manager, nothing shared - so the
// difference is what the sharing costs. Microseconds per execution as one thread sees it (the wall time over its count),
// best of three.
// =============================================================================

namespace {

ibDdlColumn Field(const char* name, const ibColumnType& type, bool primaryKey = false)
{
	ibDdlColumn c;
	c.m_name       = ibString::FromUTF8(name);
	c.m_type       = type;
	c.m_primaryKey = primaryKey;
	return c;
}
ibDmlAssign Set(const char* column, ibQueryExprPtr value) { return ibDmlAssign{ ibString::FromUTF8(column), std::move(value) }; }
ibQueryExprPtr Num(int n) { return ibConst(ibValue(ibNumber(n))); }
ibQueryProjItem As(const char* column) { return ibQueryProjItem{ ibCol(ibString::FromUTF8(column)), ibString() }; }

// One execution's statements, on the holder its space is bound to.
void Execute(ibDatabaseConnectionHolder* holder)
{
	{
		ibDatabaseQueryBuilder q(holder);
		q.Execute(ibCreateTable(ibT("T"), { Field("_K", ibTypeNumber(10, 0), true), Field("_N", ibTypeNumber(10, 0)) }));
	}
	{
		ibDmlStatement rows = ibInsert(ibT("T"), { Set("_K", Num(0)), Set("_N", Num(0)) });
		for (int at = 1; at < 10; ++at)
			rows.m_extraRows.push_back({ Num(at), Num(at * 10) });
		ibDatabaseQueryBuilder q(holder);
		q.Execute(rows);
	}
	ibDatabaseQueryBuilder q(holder);
	ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibProject(ibScan(ibT("T")), { As("_K"), As("_N") })));
	while (rs.Next()) {}
}

// The manager's road: a space of the process's queries' memory manager, gone with the execution.
void OnTheManager()
{
	ibMemorySpace space;
	Execute(space.Holder());
}

// The road before it: a pool made for the execution and gone with it.
void OnAPoolPerExecution()
{
	ibConnectionPool pool{ ib::AppDataCtorToken{} };
	pool.Init(std::make_shared<ibDatabaseLayerMemory>(), /*maxSize=*/1, /*minIdle=*/0);
	ibSingleConnectionHolder holder;
	holder.SetPool(&pool);
	ibConnectionScope scope(&holder);
	Execute(&holder);
}

// Microseconds per execution as one of `threads` threads sees it, each running `count` of them; best of three. A thread
// that is refused counts a failure and stops — a refusal is not let out of a thread, where it would end the process.
double PerExecution(void (*execution)(), int threads, int count, std::atomic<int>& failures)
{
	double best = -1;
	for (int run = 0; run < 3; ++run) {
		const auto start = std::chrono::steady_clock::now();
		std::vector<std::thread> workers;
		workers.reserve(threads);
		for (int t = 0; t < threads; ++t) {
			workers.emplace_back([&] {
				try {
					for (int at = 0; at < count; ++at)
						execution();
				}
				catch (...) {
					++failures;
				}
			});
		}
		for (std::thread& worker : workers)
			worker.join();
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / count;
		if (best < 0 || us < best)
			best = us;
	}
	return best;
}

} // namespace

TEST(QueryMemoryBench, DISABLED_ExecutionsSharingTheManager)
{
	const int count = 500;
	const ibConnectionPool& pool = *ibApplicationHost::GetQueryMemoryManager().GetConnectionPool();
	const std::size_t live = pool.LiveSize();

	std::printf("%u cores; %d executions a thread, microseconds each as its thread sees it\n",
		std::max(1u, std::thread::hardware_concurrency()), count);
	for (const int threads : { 1, 8, 64 }) {
		std::atomic<int> failures{ 0 };
		const double shared = PerExecution(&OnTheManager, threads, count, failures);
		const double apart  = PerExecution(&OnAPoolPerExecution, threads, count, failures);
		EXPECT_EQ(failures.load(), 0) << threads << " threads";
		std::printf("%3d threads: the manager %8.1f us, a pool per execution %8.1f us, ratio %.2f\n",
			threads, shared, apart, shared / apart);
	}
	EXPECT_EQ(pool.LiveSize(), live) << "every space forgotten, however many went at once";
}
