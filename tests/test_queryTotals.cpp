// Hierarchical totals — the crown of the L3 query arc. Verifies the fold that
// turns flat detail rows into a subtotal TREE: a grand-total root, one nested level per
// grouping field, the aggregates folded at every level. Pure (no DB, no appData) — it runs
// straight on ibQueryComposer::BuildTotalsTree over a hand-built ibQueryRamTable.
//
// Scenario — `TOTALS Sum(Quantity) BY Region, Product`:
//   North/Apple 10, North/Apple 5, North/Pear 3, South/Apple 7
// expected tree:
//   root (grand) = 25
//   ├─ North = 18
//   │   ├─ Apple = 15
//   │   └─ Pear  = 3
//   └─ South = 7
//       └─ Apple = 7
// (docs/query-language-arc.md §22.1b)

#include <gtest/gtest.h>

#include "backend/query/queryProvider.h"   // ibQueryComposer::BuildTotalsTree + ibQueryRamTable + ibDataQueryBuilder
#include "backend/query/queryColumn.h"     // ibBackendQueryColumn (the test column)

namespace {

// A minimal standalone query column — name + model id (the read key). No metaobject, no
// attribute: exactly what a temp source already proves a column may be.
class TestCol : public ibBackendQueryColumn {
public:
	TestCol(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()      const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

::testing::AssertionResult NumEq(const ibValue& v, long expected) {
	if (v == ibValue(ibNumber(expected)))
		return ::testing::AssertionSuccess();
	return ::testing::AssertionFailure() << "got \"" << v.GetString().ToStdString()
	                                     << "\", expected " << expected;
}

} // namespace

TEST(QueryTotals, HierarchicalSubtotalsTree)
{
	const ibMetaID REGION = 1, PRODUCT = 2, AMOUNT = 3;

	ibQueryRamTable detail;
	detail.AddColumn(REGION,  wxT("region"),  ibTypeDescription());
	detail.AddColumn(PRODUCT, wxT("product"), ibTypeDescription());
	detail.AddColumn(AMOUNT,  wxT("amount"),  ibTypeDescription());
	auto add = [&](const wxString& r, const wxString& p, long a) {
		const long row = detail.AppendRow();
		detail.SetCell(row, REGION,  ibValue(r));
		detail.SetCell(row, PRODUCT, ibValue(p));
		detail.SetCell(row, AMOUNT,  ibValue(ibNumber(a)));
	};
	add(wxT("North"), wxT("Apple"), 10);
	add(wxT("North"), wxT("Apple"),  5);
	add(wxT("North"), wxT("Pear"),   3);
	add(wxT("South"), wxT("Apple"),  7);

	TestCol region(wxT("region"), REGION), product(wxT("product"), PRODUCT), amount(wxT("amount"), AMOUNT);
	const std::vector<const ibBackendQueryColumn*> groups = { &region, &product };

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn    = ibDataQueryBuilder::AggregateFn::Sum;
	sum.m_col   = &amount;
	sum.m_alias = wxT("total");
	const std::vector<ibDataQueryBuilder::AggregateItem> aggs = { sum };

	const ibSelectorTree tree = ibQueryComposer::BuildTotalsTree(detail, groups, aggs);

	const ibMetaID AGG0 = AMOUNT;        // aggregate rolls IN-PLACE into its own column (amount)
	const ibSelectorTree::Node& root = tree.Root();

	// Grand total at the root (level 0), no group key.
	EXPECT_EQ(root.m_level, 0);
	EXPECT_TRUE(NumEq(root.m_values.at(AGG0), 25));
	ASSERT_EQ(root.m_children.size(), 2u);   // North, South — first-seen order

	// North subtree = 18, two products.
	const ibSelectorTree::Node& north = *root.m_children[0];
	EXPECT_EQ(north.m_level, 1);
	EXPECT_EQ(north.m_values.at(REGION).GetString().ToStdString(), "North");
	EXPECT_TRUE(NumEq(north.m_values.at(AGG0), 18));
	ASSERT_EQ(north.m_children.size(), 2u);   // Apple, Pear

	const ibSelectorTree::Node& northApple = *north.m_children[0];
	EXPECT_EQ(northApple.m_level, 2);
	EXPECT_EQ(northApple.m_values.at(PRODUCT).GetString().ToStdString(), "Apple");
	EXPECT_TRUE(NumEq(northApple.m_values.at(AGG0), 15));   // 10 + 5
	EXPECT_TRUE(northApple.m_children.empty());             // detail level — leaf

	const ibSelectorTree::Node& northPear = *north.m_children[1];
	EXPECT_EQ(northPear.m_values.at(PRODUCT).GetString().ToStdString(), "Pear");
	EXPECT_TRUE(NumEq(northPear.m_values.at(AGG0), 3));

	// South subtree = 7, one product.
	const ibSelectorTree::Node& south = *root.m_children[1];
	EXPECT_EQ(south.m_values.at(REGION).GetString().ToStdString(), "South");
	EXPECT_TRUE(NumEq(south.m_values.at(AGG0), 7));
	ASSERT_EQ(south.m_children.size(), 1u);
	EXPECT_TRUE(NumEq(south.m_children[0]->m_values.at(AGG0), 7));
}

// A leaf-less TOTALS (no group fields) collapses to a single grand-total root.
TEST(QueryTotals, GrandTotalOnlyWhenNoGroups)
{
	const ibMetaID AMOUNT = 3;
	ibQueryRamTable detail;
	detail.AddColumn(AMOUNT, wxT("amount"), ibTypeDescription());
	for (long a : { 4, 6, 11 }) { const long row = detail.AppendRow(); detail.SetCell(row, AMOUNT, ibValue(ibNumber(a))); }

	TestCol amount(wxT("amount"), AMOUNT);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	const ibSelectorTree tree = ibQueryComposer::BuildTotalsTree(detail, {}, { sum });

	const ibMetaID AGG0 = AMOUNT;   // SUM has a source column -> rolls IN-PLACE into it (not a synthetic key)
	EXPECT_TRUE(NumEq(tree.Root().m_values.at(AGG0), 21));   // 4 + 6 + 11
	EXPECT_TRUE(tree.Root().m_children.empty());
}

// RAM inner JOIN: orders ⋈ customers ON orders.custId = customers.custKey, projecting
// (orders.amount, customers.name). Customer 300 (Carol) has no orders -> dropped (inner).
TEST(QueryCompose, InnerJoinByKey)
{
	const ibMetaID ORDERID = 10, CUSTID = 20, AMOUNT = 30, CUSTKEY = 21, NAME = 22;

	ibQueryRamTable orders;
	orders.AddColumn(ORDERID, wxT("orderId"), ibTypeDescription());
	orders.AddColumn(CUSTID,  wxT("custId"),  ibTypeDescription());
	orders.AddColumn(AMOUNT,  wxT("amount"),  ibTypeDescription());
	auto addOrder = [&](long id, long cust, long amt) {
		const long row = orders.AppendRow();
		orders.SetCell(row, ORDERID, ibValue(ibNumber(id)));
		orders.SetCell(row, CUSTID,  ibValue(ibNumber(cust)));
		orders.SetCell(row, AMOUNT,  ibValue(ibNumber(amt)));
	};
	addOrder(1, 100, 50);
	addOrder(2, 100, 20);
	addOrder(3, 200, 70);

	ibQueryRamTable customers;
	customers.AddColumn(CUSTKEY, wxT("custKey"), ibTypeDescription());
	customers.AddColumn(NAME,    wxT("name"),    ibTypeDescription());
	auto addCust = [&](long key, const wxString& nm) {
		const long row = customers.AppendRow();
		customers.SetCell(row, CUSTKEY, ibValue(ibNumber(key)));
		customers.SetCell(row, NAME,    ibValue(nm));
	};
	addCust(100, wxT("Alice"));
	addCust(200, wxT("Bob"));
	addCust(300, wxT("Carol"));   // no orders — inner join drops it

	TestCol custId(wxT("custId"), CUSTID), custKey(wxT("custKey"), CUSTKEY);
	TestCol amount(wxT("amount"), AMOUNT), name(wxT("name"), NAME);
	const std::vector<const ibBackendQueryColumn*> outCols = { &amount, &name };
	const std::vector<bool> fromLeft = { true, false };

	const ibQueryRamTable out = ibQueryComposer::JoinRamTables(orders, customers, &custId, &custKey, outCols, fromLeft);

	ASSERT_EQ(out.RowCount(), 3);   // order1+order2 -> Alice, order3 -> Bob; Carol dropped
	EXPECT_TRUE(NumEq(out.GetCell(0, AMOUNT), 50));
	EXPECT_EQ(out.GetCell(0, NAME).GetString().ToStdString(), "Alice");
	EXPECT_TRUE(NumEq(out.GetCell(1, AMOUNT), 20));
	EXPECT_EQ(out.GetCell(1, NAME).GetString().ToStdString(), "Alice");
	EXPECT_TRUE(NumEq(out.GetCell(2, AMOUNT), 70));
	EXPECT_EQ(out.GetCell(2, NAME).GetString().ToStdString(), "Bob");
}

// RAM UNION (heterogeneous): branch A has [code, name], branch B has [code] only. The
// shared output is [code, name]; B's rows get NULL for the absent name. Rows concatenate.
TEST(QueryCompose, UnionHeterogeneousBranches)
{
	const ibMetaID OUT_CODE = 1, OUT_NAME = 2;
	TestCol outCode(wxT("code"), OUT_CODE), outName(wxT("name"), OUT_NAME);
	const std::vector<const ibBackendQueryColumn*> outCols = { &outCode, &outName };

	ibQueryRamTable out;
	for (const ibBackendQueryColumn* c : outCols)
		out.AddColumn(c->GetColumnId(), c->GetName(), ibTypeDescription());

	// Branch A — full shape (its own column ids).
	const ibMetaID A_CODE = 10, A_NAME = 11;
	ibQueryRamTable a;
	a.AddColumn(A_CODE, wxT("code"), ibTypeDescription());
	a.AddColumn(A_NAME, wxT("name"), ibTypeDescription());
	auto addA = [&](const wxString& c, const wxString& n) {
		const long row = a.AppendRow(); a.SetCell(row, A_CODE, ibValue(c)); a.SetCell(row, A_NAME, ibValue(n));
	};
	addA(wxT("A1"), wxT("Apple"));
	addA(wxT("A2"), wxT("Pear"));
	TestCol aCode(wxT("code"), A_CODE), aName(wxT("name"), A_NAME);
	ibQueryComposer::AppendUnionBranch(out, a, outCols, { &aCode, &aName });

	// Branch B — no `name` column (heterogeneous): name resolves to null -> NULL cell.
	const ibMetaID B_CODE = 20;
	ibQueryRamTable b;
	b.AddColumn(B_CODE, wxT("code"), ibTypeDescription());
	{ const long row = b.AppendRow(); b.SetCell(row, B_CODE, ibValue(wxString(wxT("B1")))); }
	TestCol bCode(wxT("code"), B_CODE);
	ibQueryComposer::AppendUnionBranch(out, b, outCols, { &bCode, nullptr });

	ASSERT_EQ(out.RowCount(), 3);
	EXPECT_EQ(out.GetCell(0, OUT_CODE).GetString().ToStdString(), "A1");
	EXPECT_EQ(out.GetCell(0, OUT_NAME).GetString().ToStdString(), "Apple");
	EXPECT_EQ(out.GetCell(1, OUT_CODE).GetString().ToStdString(), "A2");
	EXPECT_EQ(out.GetCell(1, OUT_NAME).GetString().ToStdString(), "Pear");
	EXPECT_EQ(out.GetCell(2, OUT_CODE).GetString().ToStdString(), "B1");
	EXPECT_TRUE(out.GetCell(2, OUT_NAME).GetString().IsEmpty());   // absent in branch B -> NULL
}

// ⭐ THE DETAIL LEVEL — a level with NO FIELDS is the rows themselves, hung under the deepest
// heading ("a detail record is an empty grouping"). Not "one group of everything": that is what
// OVERALL means at the other end of the list, and the two must not be confused.
//
// `TOTALS SUM(amount) BY region, <details>` over the same four rows:
//   root = 25
//   ├─ North = 18 → three DETAIL nodes (10, 5, 3)
//   └─ South = 7  → one DETAIL node (7)
TEST(QueryTotals, EmptyLevelHangsTheDetailRows)
{
	const ibMetaID REGION = 1, PRODUCT = 2, AMOUNT = 3;

	ibQueryRamTable detail;
	detail.AddColumn(REGION,  wxT("region"),  ibTypeDescription());
	detail.AddColumn(PRODUCT, wxT("product"), ibTypeDescription());
	detail.AddColumn(AMOUNT,  wxT("amount"),  ibTypeDescription());
	auto add = [&](const wxString& r, const wxString& p, long a) {
		const long row = detail.AppendRow();
		detail.SetCell(row, REGION,  ibValue(r));
		detail.SetCell(row, PRODUCT, ibValue(p));
		detail.SetCell(row, AMOUNT,  ibValue(ibNumber(a)));
	};
	add(wxT("North"), wxT("Apple"), 10);
	add(wxT("North"), wxT("Apple"),  5);
	add(wxT("North"), wxT("Pear"),   3);
	add(wxT("South"), wxT("Apple"),  7);

	TestCol region(wxT("region"), REGION), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn    = ibDataQueryBuilder::AggregateFn::Sum;
	sum.m_col   = &amount;
	sum.m_alias = wxT("total");

	// ONE grouping level, then the detail level — an ibTotalLevel with no fields at all.
	std::vector<ibTotalLevel> levels;
	levels.push_back(ibTotalLevel::One(&region, ibDimensionKind::Elements));
	levels.push_back(ibTotalLevel{});

	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, levels, { sum }, nullptr, nullptr);

	const ibMetaID AGG0 = AMOUNT;   // the aggregate rolls in place, into its own column
	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_TRUE(NumEq(root.m_values.at(AGG0), 25));
	ASSERT_EQ(root.m_children.size(), 2u);

	const ibSelectorTree::Node& north = *root.m_children[0];
	EXPECT_EQ(north.m_kind, ibSelectorNodeKind::Group);         // a heading, as before
	EXPECT_TRUE(NumEq(north.m_values.at(AGG0), 18));
	ASSERT_EQ(north.m_children.size(), 3u);                     // ONE NODE PER ROW, not one group

	// Every row of the group, in the order it was read, saying what it IS.
	const long expected[] = { 10, 5, 3 };
	for (size_t i = 0; i < north.m_children.size(); ++i) {
		const ibSelectorTree::Node& row = *north.m_children[i];
		EXPECT_EQ(row.m_kind, ibSelectorNodeKind::Detail);
		EXPECT_EQ(row.m_level, 2);
		EXPECT_TRUE(row.m_children.empty());
		// The row's OWN value in the resource column — a sum over one row is that row.
		EXPECT_TRUE(NumEq(row.m_values.at(AGG0), expected[i]));
		// …and the fields it was read with, including the ones no level groups by.
		EXPECT_EQ(row.m_values.at(REGION).GetString().ToStdString(), "North");
		EXPECT_FALSE(row.m_values.at(PRODUCT).GetString().IsEmpty());
	}

	const ibSelectorTree::Node& south = *root.m_children[1];
	ASSERT_EQ(south.m_children.size(), 1u);
	EXPECT_EQ(south.m_children[0]->m_kind, ibSelectorNodeKind::Detail);
	EXPECT_TRUE(NumEq(south.m_children[0]->m_values.at(AGG0), 7));
}

// A LEVEL OF SEVERAL FIELDS is ONE heading keyed by the TUPLE — `BY (region, product)` folds into
// one level, not two nested ones. Two rows share a heading only when BOTH fields match.
TEST(QueryTotals, TupleLevelGroupsByEveryFieldTogether)
{
	const ibMetaID REGION = 1, PRODUCT = 2, AMOUNT = 3;

	ibQueryRamTable detail;
	detail.AddColumn(REGION,  wxT("region"),  ibTypeDescription());
	detail.AddColumn(PRODUCT, wxT("product"), ibTypeDescription());
	detail.AddColumn(AMOUNT,  wxT("amount"),  ibTypeDescription());
	auto add = [&](const wxString& r, const wxString& p, long a) {
		const long row = detail.AppendRow();
		detail.SetCell(row, REGION,  ibValue(r));
		detail.SetCell(row, PRODUCT, ibValue(p));
		detail.SetCell(row, AMOUNT,  ibValue(ibNumber(a)));
	};
	add(wxT("North"), wxT("Apple"), 10);
	add(wxT("North"), wxT("Apple"),  5);   // same pair — the same heading
	add(wxT("North"), wxT("Pear"),   3);   // same region, other product — its own heading
	add(wxT("South"), wxT("Apple"),  7);

	TestCol region(wxT("region"), REGION), product(wxT("product"), PRODUCT), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	ibTotalLevel pair;
	pair.m_fields.push_back(ibTotalField{ &region,  ibDimensionKind::Elements });
	pair.m_fields.push_back(ibTotalField{ &product, ibDimensionKind::Elements });

	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, { pair }, { sum }, nullptr, nullptr);

	const ibMetaID AGG0 = AMOUNT;
	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_TRUE(NumEq(root.m_values.at(AGG0), 25));
	// THREE headings, ONE level deep — three distinct pairs, and nothing nested inside them.
	ASSERT_EQ(root.m_children.size(), 3u);
	for (const auto& node : root.m_children) {
		EXPECT_EQ(node->m_level, 1);
		EXPECT_TRUE(node->m_children.empty());
	}

	// EVERY FIELD OF THE KEY is on the heading — a reader prints them side by side.
	const ibSelectorTree::Node& northApple = *root.m_children[0];
	EXPECT_EQ(northApple.m_values.at(REGION).GetString().ToStdString(),  "North");
	EXPECT_EQ(northApple.m_values.at(PRODUCT).GetString().ToStdString(), "Apple");
	EXPECT_TRUE(NumEq(northApple.m_values.at(AGG0), 15));   // 10 + 5 — both rows of the pair
	EXPECT_TRUE(NumEq(root.m_children[1]->m_values.at(AGG0), 3));
	EXPECT_TRUE(NumEq(root.m_children[2]->m_values.at(AGG0), 7));
}

// ⭐ AN AGGREGATE ROLLS INTO ITS INPUT COLUMN'S SLOT — that is what "in place" means here, and it
// is why TWO aggregates over ONE column cannot both be kept: the second writes where the first
// wrote. `TOTALS SUM(amount), COUNT(amount)` printed one figure under two headings until the
// lowering started projecting the repeat under a synthetic alias, which gives it a column — and
// therefore a slot — of its own (found by audit, 2026-08-22).
//
// Pinned from BOTH sides, because the workaround is only right if the trap is real.
TEST(QueryTotals, TwoAggregatesOverOneColumnNeedTwoColumns)
{
	const ibMetaID REGION = 1, AMOUNT = 2, AMOUNT_AGAIN = 3;

	ibQueryRamTable detail;
	detail.AddColumn(REGION,       wxT("region"), ibTypeDescription());
	detail.AddColumn(AMOUNT,       wxT("amount"), ibTypeDescription());
	detail.AddColumn(AMOUNT_AGAIN, wxT("agg0"),   ibTypeDescription());   // the second projection of it
	auto add = [&](const wxString& r, long a) {
		const long row = detail.AppendRow();
		detail.SetCell(row, REGION,       ibValue(r));
		detail.SetCell(row, AMOUNT,       ibValue(ibNumber(a)));
		detail.SetCell(row, AMOUNT_AGAIN, ibValue(ibNumber(a)));
	};
	add(wxT("North"), 10);
	add(wxT("North"),  5);
	add(wxT("South"),  7);

	TestCol region(wxT("region"), REGION), amount(wxT("amount"), AMOUNT), again(wxT("agg0"), AMOUNT_AGAIN);
	const std::vector<ibTotalLevel> levels{ ibTotalLevel::One(&region, ibDimensionKind::Elements) };

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum;   sum.m_col = &amount; sum.m_alias = wxT("total");

	// THE TRAP: the same column twice. Both aggregates key on AMOUNT, so the tree holds ONE value —
	// whichever ran last — and a report shows the count where the sum should be.
	{
		ibDataQueryBuilder::AggregateItem countSameCol;
		countSameCol.m_fn = ibDataQueryBuilder::AggregateFn::Count;
		countSameCol.m_col = &amount;   countSameCol.m_alias = wxT("rows");

		const ibSelectorTree tree =
			ibQueryComposer::BuildDimensionTree(detail, levels, { sum, countSameCol }, nullptr, nullptr);
		const ibSelectorTree::Node& north = *tree.Root().m_children[0];
		EXPECT_TRUE(NumEq(north.m_values.at(AMOUNT), 2));   // the COUNT, standing where the SUM was
	}

	// THE ANSWER: a column of its own for the repeat — both figures survive, each in its own slot.
	{
		ibDataQueryBuilder::AggregateItem countOwnCol;
		countOwnCol.m_fn = ibDataQueryBuilder::AggregateFn::Count;
		countOwnCol.m_col = &again;    countOwnCol.m_alias = wxT("rows");

		const ibSelectorTree tree =
			ibQueryComposer::BuildDimensionTree(detail, levels, { sum, countOwnCol }, nullptr, nullptr);
		const ibSelectorTree::Node& north = *tree.Root().m_children[0];
		EXPECT_TRUE(NumEq(north.m_values.at(AMOUNT),       15));
		EXPECT_TRUE(NumEq(north.m_values.at(AMOUNT_AGAIN),  2));
		EXPECT_TRUE(NumEq(tree.Root().m_values.at(AMOUNT), 22));   // and the grand total is still whole
	}
}
