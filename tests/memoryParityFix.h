// =============================================================================
// OES Enterprise — the base in memory as the second side of a parity, from a test
//
// Two suites checked a predicate against SQL text on SQLite by filtering rows in
// memory with a filter of the composer's own (test_queryParity, test_queryLINQExec).
// That filter is gone with the road it served: rows in memory are a base now
// (databaseLayer/databaseMemoryBase.h), read by the statement the database provider writes for
// any source (docs/private/query-one-road.md §7). So the second side is that road —
// the predicate lowered by ibDbTableProvider::BuildPredicateIR and run by the
// interpreter over a table `t` (region, qty) holding the four rows every NULL case
// needs: North 10, South 5, East 7, <NULL> 3.
// =============================================================================

#ifndef _IB_TEST_MEMORY_PARITY_FIX_H_
#define _IB_TEST_MEMORY_PARITY_FIX_H_

#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseMemoryBase.h"
#include "backend/query/provider/database/dbTableProvider.h"   // ibDbTableProvider::BuildPredicateIR — the road a read takes
#include "backend/query/queryable.h"
#include "backend/query/queryColumn.h"

#include <memory>
#include <vector>

// The table `t` as a source: its two columns raw, each one field of its own name.
class ibTestRegionTable : public ibBackendQueryable
{
public:
	static constexpr ibMetaID kRegion = 1, kQty = 2;

	ibTestRegionTable()
	{
		ibGuidImpl impl{};
		impl.m_data1 = 100;
		m_key  = ibGuid(impl);
		m_cols = { ibBackendColumnRawDB::String(ibT("region"), kRegion), ibBackendColumnRawDB::Number(ibT("qty"), kQty) };
	}

	ibString GetQueryTableName() const override { return ibT("t"); }
	ibMetaID GetQueryTableId()   const override { return 100; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_cols; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override
	{
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_cols)
			if (c == col)
				return true;
		return false;
	}

	const std::shared_ptr<const ibBackendQueryColumn>& Region() const { return m_cols[0]; }
	const std::shared_ptr<const ibBackendQueryColumn>& Qty()    const { return m_cols[1]; }

private:
	ibUniqueKey                                              m_key;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_cols;
};

// A base in memory behind a pool and a holder of its own, the table written into it.
struct ibTestMemoryRegions
{
	ibConnectionPool                   m_pool{ ib::AppDataCtorToken{} };
	ibSingleConnectionHolder           m_holder;
	std::shared_ptr<ibTestRegionTable> m_table = std::make_shared<ibTestRegionTable>();

	ibTestMemoryRegions()
	{
		m_pool.Init(std::make_shared<ibDatabaseLayerMemory>(), /*maxSize=*/1, /*minIdle=*/0);
		m_holder.SetPool(&m_pool);

		ibDdlColumn region, qty;
		region.m_name = ibT("region"); region.m_type = ibTypeString(20);
		qty.m_name    = ibT("qty");    qty.m_type    = ibTypeNumber(15, 0);
		ibDatabaseQueryBuilder(&m_holder).Execute(ibCreateTable(ibT("t"), { region, qty }));

		const auto text = [](const ibString& s) { return ibConst(ibValue(s)); };
		const auto number = [](int n) { return ibConst(ibValue(ibNumber(n))); };
		ibDmlStatement rows = ibInsert(ibT("t"), { ibDmlAssign{ ibT("region"), text(ibT("North")) }, ibDmlAssign{ ibT("qty"), number(10) } });
		rows.m_extraRows = {
			{ text(ibT("South")), number(5) },
			{ text(ibT("East")),  number(7) },
			{ ibConst(ibValue()), number(3) },   // region IS NULL — an empty value binds as NULL
		};
		ibDatabaseQueryBuilder(&m_holder).Execute(rows);
	}

	// How many rows of `t` the predicate keeps, read as a statement of the base in memory.
	int Count(const ibQueryPredicatePtr& predicate)
	{
		ibQueryRelPtr from = ibScan(ibT("t"));
		if (predicate != nullptr)
			if (ibQueryExprPtr where = ibDbTableProvider::BuildPredicateIR(m_table, predicate))
				from = ibFilter(from, where);
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibAggregate(from, { ibQueryProjItem{ ibFunc(ibT("COUNT")), ibT("cnt") } }, {})));
		return rs.Next() ? rs.GetResultInt(1) : -1;
	}
};

#endif
