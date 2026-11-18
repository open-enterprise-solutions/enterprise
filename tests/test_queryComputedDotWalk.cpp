// =============================================================================
// A computed source read by the statement a server would be given — computed columns, expression filters and sorts,
// folds and HAVING over rows the engine computes rather than reads.
//
// A COMPUTED source (register Balance / Turnover, a subquery) has NO server-side table. Its rows are copied into a
// base in memory and the spec is read there by the same translation as over a table (ibQueryComposer::OverMemorySpace,
// docs/private/query-one-road.md §7) — so a calculated measure, a group filter and an expression WHERE mean over a
// register what they mean over a catalog. This is what makes REPORTING over a register work.
//
// This harness drives the REAL door (ibDataQueryBuilder) over a hand-built computed queryable — no DB, no session:
// holder = null, the rows come from ComputeRows and the base in memory is the read's own. Its columns declare no type,
// so the copy lays each out as the widest composite (a number in its _N, a reference as its pair).
//
// (A walk through a reference of a computed source joins the copy of its target, found by what the configuration says
// the reference holds — the one answer the copies give, so it is checked on a configuration: test_queryCompositeWalk,
// over a slice and over balances and turnovers.)
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "backend/query/dataQueryBuilder.h"   // ibDataQueryBuilder / ibDataQueryResult / ibReadPageRequest / AggregateFn
#include "backend/query/queryProvider.h"          // ibComputedProviderInstance + ibQueryRamTable
#include "backend/query/queryable.h"              // ibBackendQueryable / ibQueryCondition / ibQueryFilterOp
#include "backend/query/queryColumn.h"            // ibBackendQueryColumn / ibTypeDescription

namespace {

const ibTypeDescription kNoType;

// Minimal metadata-free column (name + model-id read key).
class TestCol : public ibBackendQueryColumn {
public:
	TestCol(const ibString& name, ibMetaID id) : m_name(name), m_id(id) {}
	ibString           GetName()         const override { return m_name; }
	ibString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
	const ibMetaData*  GetMetaData()     const override { return nullptr; }
private:
	ibString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

// A COMPUTED queryable. ComputeRows rebuilds its rows from a BUILDER each call (ibQueryRamTable is
// move-only, so the source can't hold + copy one).
class ComputedQ : public ibBackendQueryable {
public:
	ComputedQ(const ibString& name, ibMetaID id) : m_name(name), m_id(id) {
		ibGuidImpl impl{}; impl.m_data1 = static_cast<unsigned long>(id); m_key = ibGuid(impl);
	}
	void AddCol(const std::shared_ptr<const ibBackendQueryColumn>& c) { m_cols.push_back(c); }
	void SetBuilder(std::function<ibQueryRamTable()> b) { m_build = std::move(b); }

	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const override {
		return m_build ? m_build() : ibQueryRamTable();
	}
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_cols; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override {
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_cols) if (c == col) return true;
		return false;
	}
	ibString GetQueryTableName() const override { return m_name; }
	ibMetaID GetQueryTableId()   const override { return m_id; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	ibString m_name;
	ibMetaID m_id;
	ibUniqueKey m_key;   // its table guid, the id in the first word
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_cols;
	std::function<ibQueryRamTable()> m_build;
};

// Column ids for the shared scenario.
const ibMetaID BAL_ITEM = 20, BAL_QTY = 21, BAL_PRICE = 22;

// source balance:  (item, qty, price)
//   (K1, 100, 2), (K2, 50, 3), (K1, 30, 2)
ibQueryRamTable BuildBalance()
{
	ibQueryRamTable bal;
	bal.AddColumn(BAL_ITEM,  wxT("item"),  kNoType);
	bal.AddColumn(BAL_QTY,   wxT("qty"),   kNoType);
	bal.AddColumn(BAL_PRICE, wxT("price"), kNoType);
	auto add = [&](const ibString& item, long qty, long price) {
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
	std::shared_ptr<const TestCol> balItem    = std::make_shared<TestCol>(wxT("item"),    BAL_ITEM);
	std::shared_ptr<const TestCol> balQty     = std::make_shared<TestCol>(wxT("qty"),     BAL_QTY);
	std::shared_ptr<const TestCol> balPrice   = std::make_shared<TestCol>(wxT("price"),   BAL_PRICE);
	std::shared_ptr<ComputedQ>     balance    = std::make_shared<ComputedQ>(wxT("balance"), 200);

	void SetUp() override {
		balance->AddCol(balItem);
		balance->AddCol(balQty);
		balance->AddCol(balPrice);
		balance->SetBuilder(&BuildBalance);
	}
};

} // namespace

// --- computed EXPRESSIONS over a computed source — the reporting core --------

// SELECT Qty * Price — a computed column evaluated per row (100*2, 50*3, 30*2).
TEST_F(ComputedFix, ComputedColumn_Projection)
{
	ibDataQueryBuilder q(nullptr);
	q.From(balance);
	q.SelectExpr(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(balQty), ibQueryColumnExpr::Col(balPrice)), wxT("amount"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> amounts;
	while (res.Next())
		amounts.push_back(res.GetColumn(wxT("amount")).GetInteger());
	EXPECT_EQ(amounts, (std::vector<long>{ 200, 150, 60 }));
}

// ORDER BY Qty * Price — the computed source's rows sorted by an expression, computed per row: 60, 150, 200.
TEST_F(ComputedFix, ComputedExpr_OrderBy)
{
	ibDataQueryBuilder q(nullptr);
	q.From(balance);
	q.Select(balQty, wxT("qty"));
	q.OrderByExpr(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(balQty), ibQueryColumnExpr::Col(balPrice)),
	              /*ascending*/ true);
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> qtys;
	while (res.Next())
		qtys.push_back(res.GetValue(balQty).GetInteger());
	EXPECT_EQ(qtys, (std::vector<long>{ 30, 50, 100 })) << "60, 150, 200 - ordered by the expression";
}

// WHERE Qty * Price > 100 — a computed-expression filter. Rows: 200 (keep), 150 (keep), 60 (drop).
TEST_F(ComputedFix, ComputedExpr_Where)
{
	ibDataQueryBuilder q(nullptr);
	q.From(balance);
	q.Select(balQty, wxT("qty"));
	q.WhereExprCompare(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(balQty), ibQueryColumnExpr::Col(balPrice)),
	                   ibQueryFilterOp::Greater, ibValue(ibNumber(100)));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> qtys;
	while (res.Next())
		qtys.push_back(res.GetValue(balQty).GetInteger());
	EXPECT_EQ(qtys, (std::vector<long>{ 100, 50 }));   // qty of the 200 and 150 rows
}

// WHERE Qty > Price * 20 — the RIGHT side reads a field of the row too (m_valueExpr), which the
// lowering once refused as "expected a literal or a parameter". Rows: 100 > 40 (keep), 50 > 60 (drop),
// 30 > 40 (drop).
TEST_F(ComputedFix, ComputedExpr_WhereComparesTwoFieldsOfTheRow)
{
	ibDataQueryBuilder q(nullptr);
	q.From(balance);
	q.Select(balQty, wxT("qty"));
	ibQueryCondition c;
	c.m_op        = ibQueryFilterOp::Greater;
	c.m_expr      = ibQueryColumnExpr::Col(balQty);
	c.m_valueExpr = ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(balPrice),
	                                         ibQueryColumnExpr::Const(ibValue(ibNumber(20))));
	q.Where(c);
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> qtys;
	while (res.Next())
		qtys.push_back(res.GetValue(balQty).GetInteger());
	EXPECT_EQ(qtys, (std::vector<long>{ 100 }));
}

// SELECT Item, SUM(Qty * Price) GROUP BY Item — a CALCULATED MEASURE aggregated per group (the report core).
//   K1: 100*2 + 30*2 = 260   K2: 50*3 = 150
TEST_F(ComputedFix, ComputedAggregate_GroupBy)
{
	ibDataQueryBuilder q(nullptr);
	q.From(balance);
	q.GroupBy(balItem);
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum,
	            ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(balQty), ibQueryColumnExpr::Col(balPrice)),
	            wxT("total"));
	ibDataQueryResult res = q.SelectAggregate();   // the fold of the statement over the copy

	std::map<std::string, long> totals;
	while (res.Next())
		totals[res.GetValue(balItem).GetString().ToStdString()] = res.GetColumn(wxT("total")).GetInteger();
	EXPECT_EQ(totals["K1"], 260);
	EXPECT_EQ(totals["K2"], 150);
}

// GROUP BY Item HAVING SUM(Qty) > 100 — group filtering the register can't do; the statement over its copy does.
//   K1: SUM(qty) = 130 > 100 (keep)   K2: SUM(qty) = 50 (dropped)
TEST_F(ComputedFix, Having_DropsGroupBelowThreshold)
{
	ibDataQueryBuilder q(nullptr);
	q.From(balance);
	q.GroupBy(balItem);
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, balQty, wxT("totalQty"));
	q.Having(ibDataQueryBuilder::AggregateFn::Sum, balQty, ibQueryFilterOp::Greater, ibValue(ibNumber(100)));
	ibDataQueryResult res = q.SelectAggregate();

	std::map<std::string, long> totals;
	while (res.Next())
		totals[res.GetValue(balItem).GetString().ToStdString()] = res.GetColumn(wxT("totalQty")).GetInteger();
	EXPECT_EQ(totals.size(), 1u);   // K2 (SUM qty = 50) dropped by HAVING
	EXPECT_EQ(totals["K1"], 130);   // 100 + 30 > 100 -> kept
}

// A field whose functional option is off. IsAvailable is that question.
class OptionOffCol : public TestCol {
public:
	using TestCol::TestCol;
	bool IsAvailable() const override { return false; }
};

// The nested table is made by walking the expression (IsColumnExprAvailable). An arithmetic already
// walks its inputs, so a switched-off field is not offered out here.
TEST(NestedQueryAvailability, AnExpressionOfASwitchedOffFieldIsNotOffered)
{
	const auto field = std::make_shared<OptionOffCol>(wxT("Field"), 8);
	ibDataQueryBuilder inner(nullptr);
	inner.SelectExpr(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul,
		ibQueryColumnExpr::Col(field), ibQueryColumnExpr::Col(field)), wxT("Product"));
	const auto nested = std::make_shared<ibSubqueryQueryable>(inner);
	const std::shared_ptr<const ibBackendQueryColumn> product = nested->Column(wxT("Product"));
	ASSERT_NE(product, nullptr);
	EXPECT_FALSE(product->IsAvailable());
}

// PRESENTATION(Field) in the nested query. The column out here is offered only while Field is.
TEST(NestedQueryAvailability, PresentationOfASwitchedOffFieldIsNotOffered)
{
	const auto field = std::make_shared<OptionOffCol>(wxT("Field"), 7);
	ibDataQueryBuilder inner(nullptr);
	inner.SelectExpr(ibQueryColumnExpr::ValueAsk(ibQueryColumnExpr::Col(field), ibQueryValueAsk::Presentation),
		wxT("Shown"));
	const auto nested = std::make_shared<ibSubqueryQueryable>(inner);
	const std::shared_ptr<const ibBackendQueryColumn> shown = nested->Column(wxT("Shown"));
	ASSERT_NE(shown, nullptr);
	EXPECT_FALSE(shown->IsAvailable());
}

TEST(NestedQueryAvailability, PresentationOfAnAvailableFieldIsOffered)
{
	const auto field = std::make_shared<TestCol>(wxT("Field"), 7);
	ibDataQueryBuilder inner(nullptr);
	inner.SelectExpr(ibQueryColumnExpr::ValueAsk(ibQueryColumnExpr::Col(field), ibQueryValueAsk::Presentation),
		wxT("Shown"));
	const auto nested = std::make_shared<ibSubqueryQueryable>(inner);
	const std::shared_ptr<const ibBackendQueryColumn> shown = nested->Column(wxT("Shown"));
	ASSERT_NE(shown, nullptr);
	EXPECT_TRUE(shown->IsAvailable());
}
