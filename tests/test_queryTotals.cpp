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
#include "backend/query/queryRowCursor.h"  // ibQueryRowCursor — the fold's input (the streaming tests below)
#include "backend/query/querySelector.h"   // ibSelector — the WALK, which is where the branch tests read from

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

// ============================================================================================
// THE STREAMING FOLD — the acceptance criterion, stated as a test.
//
// The fold takes a CURSOR (queryRowCursor.h), and the point of that is what it does NOT do: hold
// the detail. So the rows here come from a GENERATOR that stores nothing — there is no table to
// fold, and if the fold needed one it could not get it. What it may hold is the tree, and the tree
// is groups.
// ============================================================================================

namespace {

// Rows out of thin air: `count` rows cycling over R regions x P products, amount = 1 each. Stores
// nothing but its position, and counts how many times it was read — a fold that walked the rows
// twice would say so here.
class GeneratedRows : public ibQueryRowCursor {
public:
	GeneratedRows(long count, long regions, long products, ibMetaID region, ibMetaID product, ibMetaID amount)
		: m_count(count), m_regions(regions), m_products(products)
		, m_region(region), m_product(product), m_amount(amount)
	{
		m_columns.push_back(ibQueryRamColumn{ region,  wxT("region"),  ibTypeDescription() });
		m_columns.push_back(ibQueryRamColumn{ product, wxT("product"), ibTypeDescription() });
		m_columns.push_back(ibQueryRamColumn{ amount,  wxT("amount"),  ibTypeDescription() });
	}

	bool Next() override { ++m_reads; return ++m_row < m_count; }

	ibValue Get(ibMetaID id) const override
	{
		if (id == m_region)  return ibValue(wxString::Format(wxT("R%ld"), m_row % m_regions));
		if (id == m_product) return ibValue(wxString::Format(wxT("P%ld"), m_row % m_products));
		if (id == m_amount)  return ibValue(ibNumber(1L));
		return ibValue();
	}

	const std::vector<ibQueryRamColumn>& Columns() const override { return m_columns; }

	long Reads() const { return m_reads; }

private:
	long     m_count, m_regions, m_products;
	ibMetaID m_region, m_product, m_amount;
	long     m_row   = -1;
	long     m_reads = 0;
	std::vector<ibQueryRamColumn> m_columns;
};

} // namespace

// 100 000 rows, 12 groups. The tree is right, the rows were read ONCE, and nothing but the tree
// (and the generator's one row index) ever existed.
TEST(QueryTotals, StreamingFoldHoldsGroupsNotRows)
{
	const ibMetaID REGION = 1, PRODUCT = 2, AMOUNT = 3;
	const long ROWS = 100000, REGIONS = 3, PRODUCTS = 4;

	GeneratedRows rows(ROWS, REGIONS, PRODUCTS, REGION, PRODUCT, AMOUNT);
	TestCol region(wxT("region"), REGION), product(wxT("product"), PRODUCT), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");
	ibDataQueryBuilder::AggregateItem rowCount;      // COUNT(*) — its receiver is synthetic
	rowCount.m_fn = ibDataQueryBuilder::AggregateFn::Count; rowCount.m_col = nullptr; rowCount.m_alias = wxT("rows");

	const std::vector<const ibBackendQueryColumn*> groups = { &region, &product };
	const ibSelectorTree tree = ibQueryComposer::BuildTotalsTree(rows, groups, { sum, rowCount });

	EXPECT_EQ(rows.Reads(), ROWS + 1);                      // every row once, plus the Next() that ended it

	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_TRUE(NumEq(root.m_values.at(AMOUNT), ROWS));     // the grand total is the whole read
	ASSERT_EQ(root.m_children.size(), static_cast<size_t>(REGIONS));

	// 3 regions x 4 products, and the region / product cycles are coprime-free here (12 pairs, each
	// hit ROWS/12 times) — what matters is that the SHAPE is groups, not rows.
	long leaves = 0, folded = 0;
	for (const auto& r : root.m_children) {
		EXPECT_TRUE(r->m_hasChildren);
		for (const auto& p : r->m_children) {
			++leaves;
			folded += p->m_values.at(AMOUNT).GetInteger();
			EXPECT_TRUE(p->m_children.empty());             // no detail level was asked for — no rows in the tree
		}
	}
	EXPECT_EQ(folded, ROWS);                                // every row landed in exactly one leaf group
	EXPECT_LE(leaves, REGIONS * PRODUCTS);
}

// The same rows through the DIMENSION fold (the road a report takes), with a DETAIL level under the
// grouping: the rows now DO appear as nodes — because the report asked to print them — and the
// figures above them are unchanged. Small row count: this one deliberately holds rows.
TEST(QueryTotals, StreamingFoldStampsDetailRowsFromACursor)
{
	const ibMetaID REGION = 1, PRODUCT = 2, AMOUNT = 3;
	GeneratedRows rows(6, 2, 3, REGION, PRODUCT, AMOUNT);
	TestCol region(wxT("region"), REGION), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	std::vector<ibTotalLevel> levels;
	levels.push_back(ibTotalLevel::One(&region, ibDimensionKind::Elements));
	levels.push_back(ibTotalLevel{});                       // detail records — a level with no fields

	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(rows, levels, { sum }, nullptr, nullptr);

	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_TRUE(NumEq(root.m_values.at(AMOUNT), 6));
	ASSERT_EQ(root.m_children.size(), 2u);                  // R0, R1
	const ibSelectorTree::Node& first = *root.m_children[0];
	EXPECT_EQ(first.m_kind, ibSelectorNodeKind::Group);
	EXPECT_TRUE(NumEq(first.m_values.at(AMOUNT), 3));
	ASSERT_EQ(first.m_children.size(), 3u);                 // one node per ROW under the heading
	for (const auto& row : first.m_children) {
		EXPECT_EQ(row->m_kind, ibSelectorNodeKind::Detail);
		EXPECT_TRUE(NumEq(row->m_values.at(AMOUNT), 1));    // the row's own value, not its contribution
		EXPECT_EQ(row->m_values.at(REGION).GetString().ToStdString(), "R0");   // the key above stays readable
	}
}

// ============================================================================================
// BY … PERIODS(unit[, from, to]) — the level is grouped by the period containing the value AND
// padded, so a month nothing happened in still gets its row. The padding is the reason the word
// exists: a chart with a hole where a quiet month was is a wrong chart.
// ============================================================================================

namespace {

ibValue Moment(int year, int month, int day)
{
	return ibValue(wxDateTime(static_cast<wxDateTime::wxDateTime_t>(day),
	                          static_cast<wxDateTime::Month>(month - 1), year, 12, 0, 0));
}

int MonthOf(const ibValue& v) { return static_cast<int>(v.GetDateTime().GetMonth()) + 1; }

} // namespace

TEST(QueryTotals, PeriodsLevelGroupsByMonthAndPadsTheQuietOne)
{
	const ibMetaID PERIOD = 1, AMOUNT = 2;

	// January twice, MARCH once — February is the month nothing happened in.
	ibQueryRamTable detail;
	detail.AddColumn(PERIOD, wxT("period"), ibTypeDescription());
	detail.AddColumn(AMOUNT, wxT("amount"), ibTypeDescription());
	auto add = [&](const ibValue& when, long amount) {
		const long row = detail.AppendRow();
		detail.SetCell(row, PERIOD, when);
		detail.SetCell(row, AMOUNT, ibValue(ibNumber(amount)));
	};
	add(Moment(2026, 1, 3),  10);
	add(Moment(2026, 1, 28),  5);
	add(Moment(2026, 3, 9),   7);

	TestCol period(wxT("period"), PERIOD), amount(wxT("amount"), AMOUNT);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	ibTotalLevel level = ibTotalLevel::One(&period, ibDimensionKind::Elements);
	level.m_fields.front().m_periods = std::make_shared<ibTotalPeriods>();
	level.m_fields.front().m_periods->m_unit = ibTotalsPeriod::Month;   // no bounds — pad between first and last

	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, { level }, { sum }, nullptr, nullptr);

	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_TRUE(NumEq(root.m_values.at(AMOUNT), 22));            // the grand total counts every row
	ASSERT_EQ(root.m_children.size(), 3u);                       // Jan, Feb (padded), Mar

	EXPECT_EQ(MonthOf(root.m_children[0]->m_values.at(PERIOD)), 1);
	EXPECT_TRUE(NumEq(root.m_children[0]->m_values.at(AMOUNT), 15));   // 10 + 5, both truncated to January

	// THE QUIET MONTH. It is a row, and its figure is nought — which is the number the reader came
	// for, and the one a chart needs to draw a line through.
	EXPECT_EQ(MonthOf(root.m_children[1]->m_values.at(PERIOD)), 2);
	EXPECT_TRUE(NumEq(root.m_children[1]->m_values.at(AMOUNT), 0));
	EXPECT_TRUE(root.m_children[1]->m_children.empty());
	EXPECT_FALSE(root.m_children[1]->m_hasChildren);

	EXPECT_EQ(MonthOf(root.m_children[2]->m_values.at(PERIOD)), 3);
	EXPECT_TRUE(NumEq(root.m_children[2]->m_values.at(AMOUNT), 7));
}

TEST(QueryTotals, PeriodsBoundsPadOutwardsAndNeverFilter)
{
	const ibMetaID PERIOD = 1, AMOUNT = 2;

	ibQueryRamTable detail;
	detail.AddColumn(PERIOD, wxT("period"), ibTypeDescription());
	detail.AddColumn(AMOUNT, wxT("amount"), ibTypeDescription());
	const long row = detail.AppendRow();
	detail.SetCell(row, PERIOD, Moment(2026, 5, 4));
	detail.SetCell(row, AMOUNT, ibValue(ibNumber(9L)));

	TestCol period(wxT("period"), PERIOD), amount(wxT("amount"), AMOUNT);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	ibTotalLevel level = ibTotalLevel::One(&period, ibDimensionKind::Elements);
	level.m_fields.front().m_periods = std::make_shared<ibTotalPeriods>();
	level.m_fields.front().m_periods->m_unit = ibTotalsPeriod::Month;
	level.m_fields.front().m_periods->m_from = Moment(2026, 3, 1);   // BEFORE the only row
	level.m_fields.front().m_periods->m_to   = Moment(2026, 6, 30);  // …and after it

	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, { level }, { sum }, nullptr, nullptr);

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 4u);                       // March, April, May, June
	EXPECT_EQ(MonthOf(root.m_children[0]->m_values.at(PERIOD)), 3);
	EXPECT_TRUE(NumEq(root.m_children[0]->m_values.at(AMOUNT), 0));
	EXPECT_EQ(MonthOf(root.m_children[2]->m_values.at(PERIOD)), 5);
	EXPECT_TRUE(NumEq(root.m_children[2]->m_values.at(AMOUNT), 9));   // the month that has the row
	EXPECT_EQ(MonthOf(root.m_children[3]->m_values.at(PERIOD)), 6);
	EXPECT_TRUE(NumEq(root.m_values.at(AMOUNT), 9));             // padding adds rows, never figures
}

// ===========================================================================
//  A CROSS-TABLE — one fold, and every heading carries its own column branch
// ===========================================================================

// ⭐⭐ THE CELLS HANG UNDER EVERY ROW HEADING, NOT ONLY UNDER THE DEEPEST ONE.
//
// 🛑 They used to be the TAIL of one chain — rows, then columns — so only the innermost row heading
// ever stood over them. A table with two row groupings printed its figures against the inner
// heading and left the outer one blank (Max, 2026-08-26: "if one more grouping appears, it shows no
// totals above"). An outer heading's cells are a fold over a SUBSET of the keys — its own prefix and
// the columns, skipping what is between — and a subset is not a prefix of the nesting order.
//
// …AND THE ROOT IS A HEADING TOO, which is what makes its cells the COLUMN TOTALS. That is the
// second fold this replaced: the same figures, read once.
//
//   region/store/product: North/S1/Apple 10 · North/S1/Pear 3 · North/S2/Apple 5 · South/S1/Apple 7
TEST(QueryTotals, CrossTableHangsTheColumnsUnderEveryRowHeading)
{
	const ibMetaID REGION = 1, STORE = 2, PRODUCT = 3, AMOUNT = 4;

	ibQueryRamTable detail;
	detail.AddColumn(REGION,  wxT("region"),  ibTypeDescription());
	detail.AddColumn(STORE,   wxT("store"),   ibTypeDescription());
	detail.AddColumn(PRODUCT, wxT("product"), ibTypeDescription());
	detail.AddColumn(AMOUNT,  wxT("amount"),  ibTypeDescription());
	auto add = [&](const wxString& r, const wxString& s, const wxString& p, long a) {
		const long row = detail.AppendRow();
		detail.SetCell(row, REGION,  ibValue(r));
		detail.SetCell(row, STORE,   ibValue(s));
		detail.SetCell(row, PRODUCT, ibValue(p));
		detail.SetCell(row, AMOUNT,  ibValue(ibNumber(a)));
	};
	add(wxT("North"), wxT("S1"), wxT("Apple"), 10);
	add(wxT("North"), wxT("S1"), wxT("Pear"),   3);
	add(wxT("North"), wxT("S2"), wxT("Apple"),  5);
	add(wxT("South"), wxT("S1"), wxT("Apple"),  7);

	TestCol region(wxT("region"), REGION), store(wxT("store"), STORE),
	        product(wxT("product"), PRODUCT), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	// TWO ROW LEVELS AND ONE COLUMN LEVEL — the axis is stated ON the level, which is the only thing
	// that tells this fold from an ordinary three-level grouping.
	std::vector<ibTotalLevel> levels;
	levels.push_back(ibTotalLevel::One(&region,  ibDimensionKind::Elements));
	levels.push_back(ibTotalLevel::One(&store,   ibDimensionKind::Elements));
	levels.push_back(ibTotalLevel::One(&product, ibDimensionKind::Elements));
	levels.back().m_axis = ibTotalsAxis::Columns;

	// THE STREAMING ROAD — the one every report travels (a cursor, one pass, no snapshot).
	ibRamTableCursor cursor(detail);
	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(cursor, levels, { sum }, nullptr, nullptr);

	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_TRUE(NumEq(root.m_values.at(AMOUNT), 25));

	// THE CELLS COME FIRST, the sub-headings after: a pre-order walk is the only thing that says
	// whose cells these are, so the order is part of the answer.
	ASSERT_EQ(root.m_children.size(), 4u);                       // Apple, Pear, North, South
	EXPECT_EQ(root.m_children[0]->m_values.at(PRODUCT).GetString().ToStdString(), "Apple");
	EXPECT_TRUE(NumEq(root.m_children[0]->m_values.at(AMOUNT), 22));   // the COLUMN TOTAL
	EXPECT_EQ(root.m_children[1]->m_values.at(PRODUCT).GetString().ToStdString(), "Pear");
	EXPECT_TRUE(NumEq(root.m_children[1]->m_values.at(AMOUNT), 3));
	// A column key is numbered by its LEVEL, wherever it hangs — that is what tells it from a row.
	EXPECT_EQ(root.m_children[0]->m_level, 3);

	const ibSelectorTree::Node& north = *root.m_children[2];
	EXPECT_EQ(north.m_values.at(REGION).GetString().ToStdString(), "North");
	EXPECT_EQ(north.m_level, 1);
	EXPECT_TRUE(NumEq(north.m_values.at(AMOUNT), 18));

	// ⭐ THE OUTER HEADING'S OWN CELLS — the thing that was missing. `North x Apple` skips the store
	// level entirely, which is exactly why no single chain could produce it.
	ASSERT_EQ(north.m_children.size(), 4u);                      // Apple, Pear, S1, S2
	EXPECT_EQ(north.m_children[0]->m_values.at(PRODUCT).GetString().ToStdString(), "Apple");
	EXPECT_TRUE(NumEq(north.m_children[0]->m_values.at(AMOUNT), 15));   // 10 + 5, across both stores
	EXPECT_EQ(north.m_children[1]->m_values.at(PRODUCT).GetString().ToStdString(), "Pear");
	EXPECT_TRUE(NumEq(north.m_children[1]->m_values.at(AMOUNT), 3));

	// …and the inner heading keeps the cells it always had.
	const ibSelectorTree::Node& s1 = *north.m_children[2];
	EXPECT_EQ(s1.m_values.at(STORE).GetString().ToStdString(), "S1");
	EXPECT_EQ(s1.m_level, 2);
	ASSERT_EQ(s1.m_children.size(), 2u);
	EXPECT_TRUE(NumEq(s1.m_children[0]->m_values.at(AMOUNT), 10));
	EXPECT_TRUE(NumEq(s1.m_children[1]->m_values.at(AMOUNT), 3));

	const ibSelectorTree::Node& s2 = *north.m_children[3];
	ASSERT_EQ(s2.m_children.size(), 1u);                         // S2 sold no pears — no cell, not a zero
	EXPECT_TRUE(NumEq(s2.m_children[0]->m_values.at(AMOUNT), 5));
}

// ⭐ A DETAIL RECORD IN A TABLE IS A ROW OF IT — it hangs under the last ROW heading (not inside a
// cell) and carries a cell of its own. The level stays LAST in the config; WHERE it hangs is the
// fold's answer, and its number is past the last dimension so no column key shares it.
TEST(QueryTotals, CrossTableDetailRecordsHangUnderTheRowHeadingWithTheirOwnCells)
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
	add(wxT("North"), wxT("Pear"),   3);

	TestCol region(wxT("region"), REGION), product(wxT("product"), PRODUCT), amount(wxT("amount"), AMOUNT);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	std::vector<ibTotalLevel> levels;
	levels.push_back(ibTotalLevel::One(&region,  ibDimensionKind::Elements));
	levels.push_back(ibTotalLevel::One(&product, ibDimensionKind::Elements));
	levels.back().m_axis = ibTotalsAxis::Columns;
	levels.push_back(ibTotalLevel{});   // …and the rows themselves — LAST in the config, always

	ibRamTableCursor cursor(detail);
	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(cursor, levels, { sum }, nullptr, nullptr);

	const ibSelectorTree::Node& north = *tree.Root().m_children[2];   // after the root's two cells
	EXPECT_EQ(north.m_values.at(REGION).GetString().ToStdString(), "North");

	// Apple, Pear (its cells), then the two records — cells first, so a reader knows whose they are.
	ASSERT_EQ(north.m_children.size(), 4u);
	EXPECT_EQ(north.m_children[0]->m_kind, ibSelectorNodeKind::Group);
	EXPECT_EQ(north.m_children[1]->m_kind, ibSelectorNodeKind::Group);

	const ibSelectorTree::Node& first = *north.m_children[2];
	EXPECT_EQ(first.m_kind, ibSelectorNodeKind::Detail);
	EXPECT_EQ(first.m_level, 3);                                  // past the last dimension
	EXPECT_TRUE(NumEq(first.m_values.at(AMOUNT), 10));            // its OWN value, not a subtotal
	ASSERT_EQ(first.m_children.size(), 1u);                       // …and one cell: its own column
	EXPECT_EQ(first.m_children[0]->m_values.at(PRODUCT).GetString().ToStdString(), "Apple");
	EXPECT_EQ(first.m_children[0]->m_level, 2);

	const ibSelectorTree::Node& second = *north.m_children[3];
	EXPECT_EQ(second.m_kind, ibSelectorNodeKind::Detail);
	ASSERT_EQ(second.m_children.size(), 1u);
	EXPECT_EQ(second.m_children[0]->m_values.at(PRODUCT).GetString().ToStdString(), "Pear");
}

// ⭐⭐ `SPLIT` — ONE READ, FOLDED SEVERAL WAYS. The common levels fold as they always did; where the
// ladder forks, the SAME row walks down every branch (Max, 2026-08-27: "the data comes from one
// common set, and each of them has a selection of its own").
//
// What the tree must show: under the common heading a FORK per branch — carrying no key of its own —
// and under each fork that branch's own headings, each holding the figures of the rows that reached
// it. The branch total equals the heading's, because a branch sees exactly the rows its parent does.
TEST(QueryTotals, SplitFoldsTheSameRowsDownEveryBranch)
{
	const ibMetaID ITEM = 1, CHARACTERISTIC = 2, SERIES = 3, AMOUNT = 4;

	ibQueryRamTable detail;
	detail.AddColumn(ITEM,           wxT("item"),   ibTypeDescription());
	detail.AddColumn(CHARACTERISTIC, wxT("charac"), ibTypeDescription());
	detail.AddColumn(SERIES,         wxT("series"), ibTypeDescription());
	detail.AddColumn(AMOUNT,         wxT("amount"), ibTypeDescription());
	auto add = [&](const wxString& i, const wxString& c, const wxString& s, long a) {
		const long row = detail.AppendRow();
		detail.SetCell(row, ITEM,           ibValue(i));
		detail.SetCell(row, CHARACTERISTIC, ibValue(c));
		detail.SetCell(row, SERIES,         ibValue(s));
		detail.SetCell(row, AMOUNT,         ibValue(ibNumber(a)));
	};
	add(wxT("Bolt"), wxT("M8"), wxT("A"), 10);
	add(wxT("Bolt"), wxT("M8"), wxT("B"),  5);
	add(wxT("Bolt"), wxT("M6"), wxT("A"),  4);

	TestCol item(wxT("item"), ITEM), charac(wxT("charac"), CHARACTERISTIC),
	        series(wxT("series"), SERIES), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn    = ibDataQueryBuilder::AggregateFn::Sum;
	sum.m_col   = &amount;
	sum.m_alias = wxT("total");

	// BY item SPLIT charac ONTO ByCharacteristic SPLIT series ONTO BySeries
	auto byCharacteristic = std::make_shared<ibTotalBranch>();
	byCharacteristic->m_name = wxT("ByCharacteristic");
	auto bySeries = std::make_shared<ibTotalBranch>();
	bySeries->m_name = wxT("BySeries");

	std::vector<ibTotalLevel> levels;
	levels.push_back(ibTotalLevel::One(&item, ibDimensionKind::Elements));
	levels.push_back(ibTotalLevel::One(&charac, ibDimensionKind::Elements));
	levels.back().m_branch = byCharacteristic;
	levels.push_back(ibTotalLevel::One(&series, ibDimensionKind::Elements));
	levels.back().m_branch = bySeries;

	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, levels, { sum }, nullptr, nullptr);

	const ibMetaID AGG0 = AMOUNT;
	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_TRUE(NumEq(root.m_values.at(AGG0), 19));
	ASSERT_EQ(root.m_children.size(), 1u);                 // one common heading: Bolt

	const ibSelectorTree::Node& bolt = *root.m_children[0];
	EXPECT_EQ(bolt.m_kind, ibSelectorNodeKind::Group);
	EXPECT_TRUE(NumEq(bolt.m_values.at(AGG0), 19));
	ASSERT_EQ(bolt.m_children.size(), 2u);                 // …and TWO forks under it, one per branch

	// THE FORK CARRIES ITS NAME AND NOTHING ELSE — no key of its own, and the same figure as the
	// heading it stands under, because it covers exactly those rows.
	const ibSelectorTree::Node& forkChar = *bolt.m_children[0];
	EXPECT_EQ(forkChar.m_kind, ibSelectorNodeKind::Branch);
	EXPECT_EQ(forkChar.m_branch, wxT("ByCharacteristic"));
	EXPECT_TRUE(NumEq(forkChar.m_values.at(AGG0), 19));
	EXPECT_EQ(forkChar.m_level, bolt.m_level);             // a fork spends no level

	// Branch one groups by characteristic: M8 = 15, M6 = 4.
	ASSERT_EQ(forkChar.m_children.size(), 2u);
	EXPECT_TRUE(NumEq(forkChar.m_children[0]->m_values.at(AGG0), 15));
	EXPECT_TRUE(NumEq(forkChar.m_children[1]->m_values.at(AGG0), 4));

	// Branch two groups the SAME rows by series: A = 14, B = 5.
	const ibSelectorTree::Node& forkSeries = *bolt.m_children[1];
	EXPECT_EQ(forkSeries.m_kind, ibSelectorNodeKind::Branch);
	EXPECT_EQ(forkSeries.m_branch, wxT("BySeries"));
	ASSERT_EQ(forkSeries.m_children.size(), 2u);
	EXPECT_TRUE(NumEq(forkSeries.m_children[0]->m_values.at(AGG0), 14));
	EXPECT_TRUE(NumEq(forkSeries.m_children[1]->m_values.at(AGG0), 5));

	// ⭐ BOTH BRANCHES STAND AT THE SAME DEPTH. They begin in the same place, so the second must not
	// print one rung deeper than the first — which is what counting depth by position in the flat
	// level list would have done.
	EXPECT_EQ(forkChar.m_children[0]->m_level, forkSeries.m_children[0]->m_level);
}

// ⚠ NOTHING COMMON ABOVE THEM — `BY SPLIT a SPLIT b`. Every branch then forks from the GRAND TOTAL.
// Read as "the first section is the common one" this would make branch one the trunk and hang branch
// two inside it, which is the opposite of what was asked.
TEST(QueryTotals, SplitWithNothingInCommonForksAtTheRoot)
{
	const ibMetaID ITEM = 1, STORE = 2, AMOUNT = 3;

	ibQueryRamTable detail;
	detail.AddColumn(ITEM,   wxT("item"),   ibTypeDescription());
	detail.AddColumn(STORE,  wxT("store"),  ibTypeDescription());
	detail.AddColumn(AMOUNT, wxT("amount"), ibTypeDescription());
	auto add = [&](const wxString& i, const wxString& s, long a) {
		const long row = detail.AppendRow();
		detail.SetCell(row, ITEM,   ibValue(i));
		detail.SetCell(row, STORE,  ibValue(s));
		detail.SetCell(row, AMOUNT, ibValue(ibNumber(a)));
	};
	add(wxT("Bolt"), wxT("Main"), 10);
	add(wxT("Nut"),  wxT("Main"),  5);

	TestCol item(wxT("item"), ITEM), store(wxT("store"), STORE), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn  = ibDataQueryBuilder::AggregateFn::Sum;
	sum.m_col = &amount;

	auto byItem  = std::make_shared<ibTotalBranch>();
	auto byStore = std::make_shared<ibTotalBranch>();

	std::vector<ibTotalLevel> levels;
	levels.push_back(ibTotalLevel::One(&item, ibDimensionKind::Elements));
	levels.back().m_branch = byItem;
	levels.push_back(ibTotalLevel::One(&store, ibDimensionKind::Elements));
	levels.back().m_branch = byStore;

	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, levels, { sum }, nullptr, nullptr);

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 2u);                 // two forks, side by side, not nested
	EXPECT_EQ(root.m_children[0]->m_kind, ibSelectorNodeKind::Branch);
	EXPECT_EQ(root.m_children[1]->m_kind, ibSelectorNodeKind::Branch);
	EXPECT_EQ(root.m_children[0]->m_children.size(), 2u);  // by item: Bolt, Nut
	EXPECT_EQ(root.m_children[1]->m_children.size(), 1u);  // by store: Main
}

// ⭐ AND A REPORT WITHOUT `SPLIT` IS UNTOUCHED — the levels are one ladder, no fork is opened, and
// the tree is the one this suite has been pinning all along. Stated as a test because "the old road
// is unchanged" is a claim, and a claim about behaviour belongs here rather than in a comment.
TEST(QueryTotals, WithoutSplitTheTreeHasNoForks)
{
	const ibMetaID REGION = 1, AMOUNT = 2;

	ibQueryRamTable detail;
	detail.AddColumn(REGION, wxT("region"), ibTypeDescription());
	detail.AddColumn(AMOUNT, wxT("amount"), ibTypeDescription());
	const long row = detail.AppendRow();
	detail.SetCell(row, REGION, ibValue(wxT("North")));
	detail.SetCell(row, AMOUNT, ibValue(ibNumber(7L)));

	TestCol region(wxT("region"), REGION), amount(wxT("amount"), AMOUNT);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn  = ibDataQueryBuilder::AggregateFn::Sum;
	sum.m_col = &amount;

	const std::vector<ibTotalLevel> levels{ ibTotalLevel::One(&region, ibDimensionKind::Elements) };
	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, levels, { sum }, nullptr, nullptr);

	ASSERT_EQ(tree.Root().m_children.size(), 1u);
	EXPECT_EQ(tree.Root().m_children[0]->m_kind, ibSelectorNodeKind::Group);
	EXPECT_TRUE(tree.Root().m_children[0]->m_branch.IsEmpty());
	EXPECT_EQ(tree.Root().m_children[0]->m_level, 1);
}

// ⭐⭐ ONE CURSOR, ONE FOLD, N WALKS — the half of `SPLIT` that lives on the READING side.
//
// The branches share the READ, and the rows arrive as a cursor: a cursor is spent by the first scan,
// so a second selection made over the same read finds nothing left to fetch. Live, that came back as
// the driver's "Error retrieving Next record" the moment a composition's second output started
// (2026-08-27) — the fold was right, the tree was right, and the second reader was handed a drained
// cursor. So the fold happens ONCE and the tree is handed on: this pins that the second walk reads
// its branch WITHOUT touching the cursor again.
TEST(QueryTotals, EveryBranchWalksOneFoldWithoutRereadingTheCursor)
{
	const ibMetaID REGION = 1, PRODUCT = 2, AMOUNT = 3;
	const long ROWS = 12, REGIONS = 2, PRODUCTS = 3;

	TestCol region(wxT("region"), REGION), product(wxT("product"), PRODUCT), amount(wxT("amount"), AMOUNT);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	// BY <nothing common> SPLIT ByRegion BY region SPLIT ByProduct BY product
	auto byRegion  = std::make_shared<ibTotalBranch>();
	byRegion->m_name = wxT("ByRegion");
	auto byProduct = std::make_shared<ibTotalBranch>();
	byProduct->m_name = wxT("ByProduct");

	std::vector<ibTotalLevel> levels;
	levels.push_back(ibTotalLevel::One(&region, ibDimensionKind::Elements));
	levels.back().m_branch = byRegion;
	levels.push_back(ibTotalLevel::One(&product, ibDimensionKind::Elements));
	levels.back().m_branch = byProduct;

	auto rows = std::make_unique<GeneratedRows>(ROWS, REGIONS, PRODUCTS, REGION, PRODUCT, AMOUNT);
	GeneratedRows* cursor = rows.get();

	// THE FOLD — one pass, exactly as the shared read does it before handing the tree to the branches.
	ibSelector fold(std::move(rows), ibSelectKind::ibSelectKind_ByGroups);
	fold.WithTotals(levels, { sum }, false);
	fold.ReadRows();
	const std::shared_ptr<ibSelectorTree> tree = fold.FoldedTree();
	ASSERT_NE(tree, nullptr);
	// ⚠ WHAT IS PINNED HERE IS "THE SECOND BRANCH COSTS THE SOURCE NOTHING", not how the fold got its
	// rows. An earlier shape of this also demanded exactly ROWS + 1 reads — which is a claim about
	// the road Build() took (cursor or snapshot), and that road is free to change: the test failed on
	// a fold that was perfectly correct. A test that breaks when an implementation detail moves is
	// pinning the detail, not the behaviour.
	const long readsAfterFold = cursor->Reads();

	// …AND EACH BRANCH WALKS IT. No cursor is given to either — the tree is the whole answer.
	const auto walk = [&](const wxString& branch) {
		ibSelector s(ibQueryRamTable(), ibSelectKind::ibSelectKind_ByGroups);
		s.WithTotals(levels, { sum }, false);
		s.WithReadyTree(tree);
		s.WalkBranch(branch);
		long seen = 0;
		while (s.Next()) ++seen;
		return seen;
	};

	EXPECT_EQ(walk(wxT("ByRegion")),  static_cast<long>(REGIONS));    // R0, R1
	EXPECT_EQ(walk(wxT("ByProduct")), static_cast<long>(PRODUCTS));   // P0, P1, P2

	// ⭐ THE POINT OF THE WHOLE ARC, as a number: the second branch cost the source nothing.
	EXPECT_EQ(cursor->Reads(), readsAfterFold);
}

// ============================================================================================
// ⭐⭐ `OVER <level>` — A FIGURE THAT BELONGS TO ONE GROUPING (docs §27)
//
// An aggregate with an area is folded like any other — every node already holds its roll-up of the
// rows beneath it — and one pass then decides which of those numbers is the answer: the one at the
// named level. On that level it stands, below it is carried unchanged (inside the area the figure IS
// constant, which is what lets a detail row show a share's denominator), above it there is none.
//
// ⚠ ABOVE IT THE VALUE IS ABSENT, NOT ZERO. A zero joins sums as an addend and divides as a
// denominator; an absent value reads back as the runtime's NULL — "no such figure here".
// ============================================================================================
TEST(QueryTotals, AggregateOverALevelStandsThereAndIsCarriedDown)
{
	const ibMetaID ITEM = 1, WAREHOUSE = 2, AMOUNT = 3;

	// Bolt/Central 10, Bolt/North 15, Bolt/Central 5, Nut/Central 20  → Bolt = 30, Nut = 20
	ibQueryRamTable detail;
	detail.AddColumn(ITEM,      wxT("item"),      ibTypeDescription());
	detail.AddColumn(WAREHOUSE, wxT("warehouse"), ibTypeDescription());
	detail.AddColumn(AMOUNT,    wxT("amount"),    ibTypeDescription());
	const auto add = [&](const wxString& item, const wxString& house, long amount) {
		const long r = detail.AppendRow();
		detail.SetCell(r, ITEM,      ibValue(item));
		detail.SetCell(r, WAREHOUSE, ibValue(house));
		detail.SetCell(r, AMOUNT,    ibValue(ibNumber(amount)));
	};
	add(wxT("Bolt"), wxT("Central"), 10);
	add(wxT("Bolt"), wxT("North"),   15);
	add(wxT("Bolt"), wxT("Central"),  5);
	add(wxT("Nut"),  wxT("Central"), 20);

	TestCol item(wxT("item"), ITEM), warehouse(wxT("warehouse"), WAREHOUSE), amount(wxT("amount"), AMOUNT);

	ibDataQueryBuilder::AggregateItem plain;   // area from the ladder — one figure per heading
	plain.m_fn = ibDataQueryBuilder::AggregateFn::Sum; plain.m_col = &amount; plain.m_alias = wxT("total");
	ibDataQueryBuilder::AggregateItem overItem;   // …and one that belongs to the item level
	overItem.m_fn = ibDataQueryBuilder::AggregateFn::Sum; overItem.m_col = &amount;
	overItem.m_alias = wxT("inItem");
	overItem.m_scopeDepth = 1;                    // the first rung — Item

	const std::vector<ibTotalLevel> levels{
		ibTotalLevel::One(&item,      ibDimensionKind::Elements),
		ibTotalLevel::One(&warehouse, ibDimensionKind::Elements),
	};
	const ibSelectorTree tree = ibQueryComposer::BuildDimensionTree(detail, levels, { plain, overItem },
	                                                               nullptr, nullptr);

	// The two aggregates land in slots of their own; the scoped one shares the amount column, so it
	// is read by the SECOND slot (see AggSlotId — a repeated column takes a synthetic receiver).
	const ibSelectorTree::Node& root = tree.Root();

	// ABOVE THE AREA — the grand total has no such figure at all.
	ASSERT_EQ(root.m_children.size(), 2u);
	const ibSelectorTree::Node& bolt = *root.m_children[0];
	EXPECT_TRUE(NumEq(bolt.m_values.at(AMOUNT), 30));       // the ladder aggregate: the item's own total

	// ON THE AREA — the figure is the item's total…
	// …and BELOW it every warehouse of that item carries the SAME number, which is the whole point:
	// a share's denominator has to be readable where the numerator is.
	ASSERT_EQ(bolt.m_children.size(), 2u);
	for (const auto& house : bolt.m_children) {
		ASSERT_NE(house, nullptr);
		EXPECT_TRUE(NumEq(house->m_values.at(AMOUNT), 15));  // Central 10+5, North 15 — its own total
	}

	// The second item is untouched by the first one's area — each node carries ITS OWN.
	const ibSelectorTree::Node& nut = *root.m_children[1];
	EXPECT_TRUE(NumEq(nut.m_values.at(AMOUNT), 20));
}
