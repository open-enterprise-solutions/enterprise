// =============================================================================
// Computed-source dot-walk + computed-expression resolution — the RAM analogs of the physical paths.
//
// A COMPUTED source (register Balance / Turnover, a subquery) has NO server-side table, so a reference
// dot-walk (Balance.Item.Name), a computed column (Qty * Price), a HAVING and an expression WHERE cannot
// be SQL. ibComputedProvider resolves them in RAM: it materialises reference TARGETS + LEFT-joins the leaf
// (ResolveComputedDotWalks), evaluates arithmetic / CASE per row (EvalColumnExprRow) and folds / filters
// the fold (RamAggregate + HAVING). This is what makes REPORTING over a register work — calculated
// measures, group filters, reference fields — the same as over a physical source (which pushes to the DBMS).
//
// This harness drives the REAL door (ibDataQueryBuilder) over two hand-built computed queryables — no DB,
// no session: holder = null, every read routes through ComputeRows. The "reference" is a column whose value
// equals the target's PRIMARY KEY value (JoinRamTables matches by value), so no real reference metadata is
// needed to exercise the mechanism. (docs/query-engine-layers.md — computed source; query-language-arc §22.)
// =============================================================================

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "backend/query/dataQueryBuilder.h"   // ibDataQueryBuilder / ibDataQueryResult / ibReadPageRequest / AggregateFn
#include "backend/query/queryProvider.h"       // ibComputedProviderInstance + ibQueryRamTable
#include "backend/query/queryable.h"           // ibBackendQueryable / ibQueryCondition / ibQueryFilterOp
#include "backend/query/queryColumn.h"         // ibBackendQueryColumn / ibTypeDescription

namespace {

const ibTypeDescription kNoType;

// Minimal metadata-free column (name + model-id read key).
class TestCol : public ibBackendQueryColumn {
public:
	TestCol(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

class ComputedQ;   // fwd — the fake provider reads the reference map off it

// The RAM harness has no real metadata (a computed register would carry the register's), so the DB
// provider's reference resolution returns null here. This fake provider — vended by ComputedQ::GetProvider()
// in place of the shared computed one — resolves a reference by the map INJECTED into the ComputedQ, so the
// tests exercise the SAME provider-side dot-walk path (queryable->GetProvider().ResolveReferenceTarget).
class FakeComputedProvider : public ibComputedProvider {
public:
	const ibBackendQueryable* ResolveReferenceTarget(const ibBackendQueryable* q, const ibBackendQueryColumn* col) const override;   // out-of-line: needs ComputedQ complete
};

// A COMPUTED (RAM) queryable. ComputeRows rebuilds its rows from a BUILDER each call (ibQueryRamTable is
// move-only, so the source can't hold + copy one). Reference targets + a primary key are injected so
// ResolveComputedDotWalks can walk + join.
class ComputedQ : public ibBackendQueryable {
public:
	ComputedQ(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }
	void SetBuilder(std::function<ibQueryRamTable()> b) { m_build = std::move(b); }
	void SetRefTarget(const ibBackendQueryColumn* refCol, const ibBackendQueryable* tgt) { m_refs[refCol] = tgt; }
	void SetPrimaryKey(const ibBackendQueryColumn* k) { m_pk = k; }

	ibBackendQueryProvider& GetProvider() const override { static FakeComputedProvider s_fake; return s_fake; }
	bool     IsComputedInRam() const override { return true; }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const override {
		return m_build ? m_build() : ibQueryRamTable();
	}
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override {
		return m_pk != nullptr ? std::vector<const ibBackendQueryColumn*>{ m_pk }
		                       : std::vector<const ibBackendQueryColumn*>{};
	}
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
	// The reference map, read by FakeComputedProvider::ResolveReferenceTarget (resolution is provider-side now).
	const ibBackendQueryable* RefTarget(const ibBackendQueryColumn* c) const {
		auto it = m_refs.find(c);
		return it != m_refs.end() ? it->second : nullptr;
	}
	wxString GetQueryTableName() const override { return m_name; }
	ibMetaID GetQueryTableId()   const override { return m_id; }
	ibGuid   GetQueryTableGuid() const override {
		ibGuidImpl impl{}; impl.m_data1 = static_cast<unsigned long>(m_id); return ibGuid(impl);
	}
	std::vector<ibQuerySortItem> GetIdentitySort() const override { return {}; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	wxString m_name;
	ibMetaID m_id;
	std::vector<const ibBackendQueryColumn*> m_cols;
	std::function<ibQueryRamTable()> m_build;
	std::map<const ibBackendQueryColumn*, const ibBackendQueryable*> m_refs;
	const ibBackendQueryColumn* m_pk = nullptr;
};

// Reference resolution over the fake computed source — reads the map injected into the ComputedQ.
const ibBackendQueryable* FakeComputedProvider::ResolveReferenceTarget(const ibBackendQueryable* q, const ibBackendQueryColumn* col) const {
	const ComputedQ* cq = dynamic_cast<const ComputedQ*>(q);
	return cq != nullptr ? cq->RefTarget(col) : nullptr;
}

// Column ids for the shared scenario.
const ibMetaID ITEM_KEY = 10, ITEM_NAME = 11, BAL_ITEM = 20, BAL_QTY = 21, BAL_PRICE = 22;

// target items:  K1 -> Apple, K2 -> Pear   (keyed on itemKey)
ibQueryRamTable BuildItems()
{
	ibQueryRamTable it;
	it.AddColumn(ITEM_KEY,  wxT("itemKey"), kNoType);
	it.AddColumn(ITEM_NAME, wxT("name"),    kNoType);
	auto add = [&](const wxString& k, const wxString& n) {
		const long r = it.AppendRow();
		it.SetCell(r, ITEM_KEY,  ibValue(k));
		it.SetCell(r, ITEM_NAME, ibValue(n));
	};
	add(wxT("K1"), wxT("Apple"));
	add(wxT("K2"), wxT("Pear"));
	return it;
}

// source balance:  (item, qty, price)  — item references an item by its key.
//   (K1, 100, 2), (K2, 50, 3), (K1, 30, 2)
ibQueryRamTable BuildBalance()
{
	ibQueryRamTable bal;
	bal.AddColumn(BAL_ITEM,  wxT("item"),  kNoType);
	bal.AddColumn(BAL_QTY,   wxT("qty"),   kNoType);
	bal.AddColumn(BAL_PRICE, wxT("price"), kNoType);
	auto add = [&](const wxString& item, long qty, long price) {
		const long r = bal.AppendRow();
		bal.SetCell(r, BAL_ITEM,  ibValue(item));
		bal.SetCell(r, BAL_QTY,   ibValue(ibNumber(qty)));
		bal.SetCell(r, BAL_PRICE, ibValue(ibNumber(price)));
	};
	add(wxT("K1"), 100, 2);
	add(wxT("K2"),  50, 3);
	add(wxT("K1"),  30, 2);
	return bal;
}

struct ComputedFix : ::testing::Test {
	TestCol   itemKey{ wxT("itemKey"), ITEM_KEY };
	TestCol   itemName{ wxT("name"),   ITEM_NAME };
	TestCol   balItem{ wxT("item"),    BAL_ITEM };
	TestCol   balQty{ wxT("qty"),      BAL_QTY };
	TestCol   balPrice{ wxT("price"),  BAL_PRICE };
	ComputedQ items{ wxT("items"),   100 };
	ComputedQ balance{ wxT("balance"), 200 };

	void SetUp() override {
		items.AddCol(&itemKey);
		items.AddCol(&itemName);
		items.SetPrimaryKey(&itemKey);
		items.SetBuilder(&BuildItems);

		balance.AddCol(&balItem);
		balance.AddCol(&balQty);
		balance.AddCol(&balPrice);
		balance.SetRefTarget(&balItem, &items);
		balance.SetBuilder(&BuildBalance);
	}
};

} // namespace

// --- dot-walk over a computed source (RAM resolve of the reference leaf) -----------------------

// SELECT Balance.Item.Name — each row's item reference resolves to the item's name.
TEST_F(ComputedFix, DotWalk_Projection)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.SelectPath({ &balItem, &itemName }, wxT("itemName"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<std::string> got;
	while (res.Next())
		got.push_back(res.GetValue(&itemName).GetString().ToStdString());
	EXPECT_EQ(got, (std::vector<std::string>{ "Apple", "Pear", "Apple" }));
}

// WHERE Item.Name = 'Apple' — the register can't filter by a dot-walk; the provider joins the leaf and
// filters it in RAM. Only the two K1 (Apple) rows survive.
TEST_F(ComputedFix, DotWalk_Where)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.Select(&balQty, wxT("qty"));
	q.Where({ &balItem, &itemName }, ibQueryFilterOp::Equal, ibValue(wxString(wxT("Apple"))));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> qtys;
	while (res.Next())
		qtys.push_back(res.GetValue(&balQty).GetInteger());
	EXPECT_EQ(qtys, (std::vector<long>{ 100, 30 }));
}

// ORDER BY Item.Name — sort by the reference leaf: Apple (K1) rows first (100, 30), then Pear (K2, 50).
TEST_F(ComputedFix, DotWalk_OrderBy)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.Select(&balQty, wxT("qty"));
	q.OrderBy({ &balItem, &itemName }, /*ascending*/ true);
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> qtys;
	while (res.Next())
		qtys.push_back(res.GetValue(&balQty).GetInteger());
	EXPECT_EQ(qtys, (std::vector<long>{ 100, 30, 50 }));
}

// --- computed EXPRESSIONS over a computed source (RAM per-row evaluation) — the reporting core --------

// SELECT Qty * Price — a computed column evaluated per row (100*2, 50*3, 30*2).
TEST_F(ComputedFix, ComputedColumn_Projection)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.SelectExpr(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(&balQty), ibQueryColumnExpr::Col(&balPrice)), wxT("amount"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> amounts;
	while (res.Next())
		amounts.push_back(res.GetColumn(wxT("amount")).GetInteger());
	EXPECT_EQ(amounts, (std::vector<long>{ 200, 150, 60 }));
}

// WHERE Qty * Price > 100 — a computed-expression filter in RAM. Rows: 200 (keep), 150 (keep), 60 (drop).
TEST_F(ComputedFix, ComputedExpr_Where)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.Select(&balQty, wxT("qty"));
	q.WhereExprCompare(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(&balQty), ibQueryColumnExpr::Col(&balPrice)),
	                   ibQueryFilterOp::Greater, ibValue(ibNumber(100)));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> qtys;
	while (res.Next())
		qtys.push_back(res.GetValue(&balQty).GetInteger());
	EXPECT_EQ(qtys, (std::vector<long>{ 100, 50 }));   // qty of the 200 and 150 rows
}

// SELECT Item, SUM(Qty * Price) GROUP BY Item — a CALCULATED MEASURE aggregated per group (the report core).
//   K1: 100*2 + 30*2 = 260   K2: 50*3 = 150
TEST_F(ComputedFix, ComputedAggregate_GroupBy)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.GroupBy(&balItem);
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum,
	            ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(&balQty), ibQueryColumnExpr::Col(&balPrice)),
	            wxT("total"));
	ibDataQueryResult res = q.SelectAggregate();   // aggregate execute -> ExecuteAggregate -> RamAggregate

	std::map<std::string, long> totals;
	while (res.Next())
		totals[res.GetValue(&balItem).GetString().ToStdString()] = res.GetColumn(wxT("total")).GetInteger();
	EXPECT_EQ(totals["K1"], 260);
	EXPECT_EQ(totals["K2"], 150);
}

// GROUP BY Item.Name, SUM(Qty) — a DOT-WALK group KEY over a computed source: the register can't group by
// the reference's FIELD; the provider joins the leaf and folds by it. This is the RAM analog of the SERVER
// path (a promoted temp joins the target catalog through the same ibRefJoinChain). Apple(K1): 100+30=130;
// Pear(K2): 50.
TEST_F(ComputedFix, DotWalk_GroupByReferenceField)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.GroupBy(std::vector<const ibBackendQueryColumn*>{ &balItem, &itemName });   // GROUP BY Item.Name (dot-walk key)
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &balQty, wxT("totalQty"));
	ibDataQueryResult res = q.SelectAggregate();

	std::map<std::string, long> totals;
	while (res.Next())
		totals[res.GetValue(&itemName).GetString().ToStdString()] = res.GetColumn(wxT("totalQty")).GetInteger();
	EXPECT_EQ(totals.size(), 2u);
	EXPECT_EQ(totals["Apple"], 130);   // K1 rows: 100 + 30
	EXPECT_EQ(totals["Pear"],  50);    // K2 row
}

// GROUP BY Item HAVING SUM(Qty) > 100 — group filtering the register can't do; the RAM fold does.
//   K1: SUM(qty) = 130 > 100 (keep)   K2: SUM(qty) = 50 (dropped)
TEST_F(ComputedFix, Having_DropsGroupBelowThreshold)
{
	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.GroupBy(&balItem);
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &balQty, wxT("totalQty"));
	q.Having(ibDataQueryBuilder::AggregateFn::Sum, &balQty, ibQueryFilterOp::Greater, ibValue(ibNumber(100)));
	ibDataQueryResult res = q.SelectAggregate();

	std::map<std::string, long> totals;
	while (res.Next())
		totals[res.GetValue(&balItem).GetString().ToStdString()] = res.GetColumn(wxT("totalQty")).GetInteger();
	EXPECT_EQ(totals.size(), 1u);   // K2 (SUM qty = 50) dropped by HAVING
	EXPECT_EQ(totals["K1"], 130);   // 100 + 30 > 100 -> kept
}

// Boolean WHERE (Item.Name = 'Pear' OR qty = 100) — an OR of a dot-walk leaf and a plain column over the
// computed source: the predicate-tree gather joins the reference leaf, FilterRows evaluates the tree in RAM.
//   (K1,100) via qty=100 · (K2,50) via Pear · (K1,30) dropped
TEST_F(ComputedFix, DotWalk_BooleanWhere)
{
	ibQueryCondition c1;
	c1.m_col   = &itemName;
	c1.m_path  = { &balItem, &itemName };
	c1.m_op    = ibQueryFilterOp::Equal;
	c1.m_value = ibValue(wxString(wxT("Pear")));
	ibQueryCondition c2;
	c2.m_col   = &balQty;
	c2.m_op    = ibQueryFilterOp::Equal;
	c2.m_value = ibValue(ibNumber(100));

	ibDataQueryBuilder q(nullptr);
	q.From(&balance);
	q.Select(&balQty, wxT("qty"));
	q.Where(ibQueryPredicate::Compose(ibQueryPredicateKind::Or,
	                                  ibQueryPredicate::Leaf(c1), ibQueryPredicate::Leaf(c2)));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> qtys;
	while (res.Next())
		qtys.push_back(res.GetValue(&balQty).GetInteger());
	EXPECT_EQ(qtys, (std::vector<long>{ 100, 50 }));
}
