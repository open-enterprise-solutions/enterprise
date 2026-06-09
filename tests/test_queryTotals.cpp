// Hierarchical totals — the crown of the L3 query arc. Verifies the fold that
// turns flat detail rows into a subtotal TREE: a grand-total root, one nested level per
// grouping field, the aggregates folded at every level. Pure (no DB, no appData) — it runs
// straight on ibQueryComposer::BuildTotalsTree over a hand-built ibQueryRamTable.
//
// Scenario — `ИТОГИ Регион, Продукт ПО Сумма(Количество)`:
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
	ibMetaID           GetModelID()      const override { return m_id; }
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

	const ibQueryRamTable tree = ibQueryComposer::BuildTotalsTree(detail, groups, aggs);

	const ibMetaID AGG0 = 0x40000000u;   // first aggregate's synthetic column id
	const ibQueryRamTable::Node& root = tree.Root();

	// Grand total at the root (level 0), no group key.
	EXPECT_EQ(root.m_level, 0);
	EXPECT_TRUE(NumEq(root.m_values.at(AGG0), 25));
	ASSERT_EQ(root.m_children.size(), 2u);   // North, South — first-seen order

	// North subtree = 18, two products.
	const ibQueryRamTable::Node& north = *root.m_children[0];
	EXPECT_EQ(north.m_level, 1);
	EXPECT_EQ(north.m_values.at(REGION).GetString().ToStdString(), "North");
	EXPECT_TRUE(NumEq(north.m_values.at(AGG0), 18));
	ASSERT_EQ(north.m_children.size(), 2u);   // Apple, Pear

	const ibQueryRamTable::Node& northApple = *north.m_children[0];
	EXPECT_EQ(northApple.m_level, 2);
	EXPECT_EQ(northApple.m_values.at(PRODUCT).GetString().ToStdString(), "Apple");
	EXPECT_TRUE(NumEq(northApple.m_values.at(AGG0), 15));   // 10 + 5
	EXPECT_TRUE(northApple.m_children.empty());             // detail level — leaf

	const ibQueryRamTable::Node& northPear = *north.m_children[1];
	EXPECT_EQ(northPear.m_values.at(PRODUCT).GetString().ToStdString(), "Pear");
	EXPECT_TRUE(NumEq(northPear.m_values.at(AGG0), 3));

	// South subtree = 7, one product.
	const ibQueryRamTable::Node& south = *root.m_children[1];
	EXPECT_EQ(south.m_values.at(REGION).GetString().ToStdString(), "South");
	EXPECT_TRUE(NumEq(south.m_values.at(AGG0), 7));
	ASSERT_EQ(south.m_children.size(), 1u);
	EXPECT_TRUE(NumEq(south.m_children[0]->m_values.at(AGG0), 7));
}

// A leaf-less ИТОГИ (no group fields) collapses to a single grand-total root.
TEST(QueryTotals, GrandTotalOnlyWhenNoGroups)
{
	const ibMetaID AMOUNT = 3;
	ibQueryRamTable detail;
	detail.AddColumn(AMOUNT, wxT("amount"), ibTypeDescription());
	for (long a : { 4, 6, 11 }) { const long row = detail.AppendRow(); detail.SetCell(row, AMOUNT, ibValue(ibNumber(a))); }

	TestCol amount(wxT("amount"), AMOUNT);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amount; sum.m_alias = wxT("total");

	const ibQueryRamTable tree = ibQueryComposer::BuildTotalsTree(detail, {}, { sum });

	const ibMetaID AGG0 = 0x40000000u;
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
		out.AddColumn(c->GetModelID(), c->GetName(), ibTypeDescription());

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
