// L3 composer — RAM composition cores + co-location ROUTING gates (docs §22).
//
// PURE: no database, no appData. Two layers:
//   1. The RAM cores ibQueryComposer exposes (JoinRamTables / AppendUnionBranch) — the
//      nested-loop join and the heterogeneous union stack, asserted on exact cell values.
//   2. The routing GATES (CanColocateJoin / CanColocateUnion / CanColocateAggregate) — given a
//      query SHAPE, do they correctly decide "run server-side" vs "fall back to RAM"? Driven
//      by a minimal mock queryable (raw columns -> scalar / single-field-joinable, no metadata).
//
// Together: if the cores fold correctly AND the gates route correctly, the L3 engine's
// composition + push-down decisions are proven without a database. (The actual server-side SQL
// EXECUTION + the temp round-trip are integration scope — a real SQLite/PG connection.)

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "backend/query/queryProvider.h"     // ibQueryComposer + ibQueryRamTable
#include "backend/query/querySelector.h"     // ibSelector — traversal façade over a snapshot
#include "backend/query/dbTableProvider.h"   // ibDbTableProvider::CanColocate* (the gates)
#include "backend/query/dataQueryBuilder.h"  // ibDataQuerySpec / ibQueryNode / AggregateItem
#include "backend/query/queryable.h"         // ibBackendQueryable / ibQueryCondition
#include "backend/query/queryColumn.h"       // ibBackendQueryColumn / ibRawDBColumn

namespace {

// Minimal RAM-core column — name + model id (the row read key). No metaobject. (As in test_queryTotals.)
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

const ibTypeDescription kNoType;

// A minimal queryable for the routing gates: a named table over raw columns (raw -> scalar +
// single-field-joinable, so no type-descriptor / metadata setup is needed). Ownership is by
// pointer (each column belongs to exactly its table). Optionally COMPUTED (RAM) for the
// "computed leaf falls back to RAM" cases.
class TestQueryable : public ibBackendQueryable {
public:
	TestQueryable(const wxString& table, ibMetaID metaId, bool computed = false)
		: m_table(table), m_metaId(metaId), m_computed(computed) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }

	wxString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryMetaID()    const override { return m_metaId; }
	bool     IsComputedInRam()   const override { return m_computed; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<ibQuerySortItem> GetIdentitySort() const override { return {}; }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
private:
	wxString m_table;
	ibMetaID m_metaId;
	bool     m_computed;
	std::vector<const ibBackendQueryColumn*> m_cols;
};

// Fill an ibDataQuerySpec with all the (empty) side-vectors + a root + select list. The vectors
// must outlive the spec use, so the caller owns them.
struct SpecBuf {
	std::vector<ibQueryCondition> conds;
	std::vector<ibValue> keyIn;
	std::vector<ibQuerySortItem> sorts;
	std::vector<const ibBackendQueryColumn*> groupBy;
	std::vector<ibDataQueryBuilder::AggregateItem> aggs;
	std::vector<ibDataQueryBuilder::HavingItem> having;
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> writes;
	std::vector<ibDotWalkColumn> dots;
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> sel;

	ibDataQuerySpec Make(const ibQueryNode* root, const ibBackendQueryable* primary) {
		ibDataQuerySpec s;
		s.m_root = root; s.m_queryable = primary;
		s.m_conditions = &conds; s.m_keyIn = &keyIn; s.m_sorts = &sorts;
		s.m_groupBy = &groupBy; s.m_aggregates = &aggs; s.m_having = &having;
		s.m_writeValues = &writes; s.m_dotWalks = &dots; s.m_selectCols = &sel;
		return s;
	}
};

std::shared_ptr<ibQueryNode> Join2(const ibBackendQueryable* l, const ibBackendQueryable* r,
                                   const ibBackendQueryColumn* onL, const ibBackendQueryColumn* onR)
{
	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Join;
	root->m_left = ibQueryNode::Source(l); root->m_right = ibQueryNode::Source(r);
	root->m_onLeft = onL; root->m_onRight = onR;
	return root;
}

} // namespace

// ===========================================================================
// RAM cores — JoinRamTables (nested-loop inner join, keyed by model id)
// ===========================================================================

TEST(QueryComposerCore, JoinRamTables_MatchesAndCardinality)
{
	const ibMetaID CUST_ID = 1, CUST_CODE = 2, ORD_ID = 3, ORD_CUST = 4;
	TestCol custId(wxT("custId"), CUST_ID), custCode(wxT("custCode"), CUST_CODE);
	TestCol ordId(wxT("ordId"), ORD_ID),     ordCust(wxT("ordCust"), ORD_CUST);

	ibQueryRamTable left;   // customers
	left.AddColumn(CUST_ID, wxT("custId"), kNoType);
	left.AddColumn(CUST_CODE, wxT("custCode"), kNoType);
	auto cust = [&](long id, const wxString& code) {
		const long r = left.AppendRow();
		left.SetCell(r, CUST_ID, ibValue(ibNumber(id)));
		left.SetCell(r, CUST_CODE, ibValue(code));
	};
	cust(1, wxT("A")); cust(2, wxT("B"));

	ibQueryRamTable right;  // orders -> customer
	right.AddColumn(ORD_ID, wxT("ordId"), kNoType);
	right.AddColumn(ORD_CUST, wxT("ordCust"), kNoType);
	auto ord = [&](long id, long cust) {
		const long r = right.AppendRow();
		right.SetCell(r, ORD_ID, ibValue(ibNumber(id)));
		right.SetCell(r, ORD_CUST, ibValue(ibNumber(cust)));
	};
	ord(10, 1); ord(11, 1); ord(12, 2); ord(13, 99);   // 99 = no matching customer

	const std::vector<const ibBackendQueryColumn*> outCols = { &custCode, &ordId };
	const std::vector<bool> fromLeft = { true, false };
	const ibQueryRamTable out = ibQueryComposer::JoinRamTables(left, right, &custId, &ordCust, outCols, fromLeft);

	ASSERT_EQ(out.RowCount(), 3);   // (A,10) (A,11) (B,12); ord 13 dropped
	EXPECT_EQ(out.GetCell(0, CUST_CODE).GetString().ToStdString(), "A");
	EXPECT_EQ(out.GetCell(0, ORD_ID).GetString().ToStdString(), "10");
	EXPECT_EQ(out.GetCell(2, CUST_CODE).GetString().ToStdString(), "B");
	EXPECT_EQ(out.GetCell(2, ORD_ID).GetString().ToStdString(), "12");
}

// ===========================================================================
// RAM cores — AppendUnionBranch (heterogeneous stack; absent column -> NULL)
// ===========================================================================

TEST(QueryComposerCore, AppendUnionBranch_MissingColumnIsNull)
{
	const ibMetaID CODE = 1, NAME = 2;
	TestCol code(wxT("code"), CODE), name(wxT("name"), NAME);

	ibQueryRamTable out;
	out.AddColumn(CODE, wxT("code"), kNoType);
	out.AddColumn(NAME, wxT("name"), kNoType);

	ibQueryRamTable branch;   // has code, NOT name
	branch.AddColumn(CODE, wxT("code"), kNoType);
	const long br = branch.AppendRow();
	branch.SetCell(br, CODE, ibValue(wxString(wxT("X"))));

	// outCols [code, name]; this branch supplies [code, <absent>].
	ibQueryComposer::AppendUnionBranch(out, branch, { &code, &name }, { &code, nullptr });

	ASSERT_EQ(out.RowCount(), 1);
	EXPECT_EQ(out.GetCell(0, CODE).GetString().ToStdString(), "X");
	EXPECT_TRUE(out.GetCell(0, NAME).GetString().IsEmpty());   // absent in this branch -> empty cell
}

// ===========================================================================
// Routing gates — CanColocateJoin
// ===========================================================================

TEST(QueryComposerGate, Join_TwoDistinctDbTables_Colocatable)
{
	ibRawDBColumn aKey = ibRawDBColumn::Number(wxT("a_key"));
	ibRawDBColumn aOut = ibRawDBColumn::String(wxT("a_out"));
	ibRawDBColumn bKey = ibRawDBColumn::Number(wxT("b_key"));
	ibRawDBColumn bOut = ibRawDBColumn::String(wxT("b_out"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bOut);

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf;
	buf.sel = { { &aOut, wxT("ao") }, { &bOut, wxT("bo") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateJoin(spec));
}

TEST(QueryComposerGate, Join_SelfJoinSameTable_NotColocatable)
{
	ibRawDBColumn aKey = ibRawDBColumn::Number(wxT("a_key"));
	ibRawDBColumn bKey = ibRawDBColumn::Number(wxT("b_key"));
	ibRawDBColumn aOut = ibRawDBColumn::String(wxT("a_out"));
	TestQueryable A(wxT("Same"), 1); A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable B(wxT("Same"), 2); B.AddCol(&bKey);   // SAME table name -> ambiguous

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf; buf.sel = { { &aOut, wxT("ao") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanColocateJoin(spec));   // self-join -> RAM
}

TEST(QueryComposerGate, Join_ComputedLeaf_NotColocatable)
{
	ibRawDBColumn aKey = ibRawDBColumn::Number(wxT("a_key"));
	ibRawDBColumn aOut = ibRawDBColumn::String(wxT("a_out"));
	ibRawDBColumn cKey = ibRawDBColumn::Number(wxT("c_key"));
	TestQueryable A(wxT("TableA"), 1);             A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable C(wxT("Slice"), 2, /*computed*/true); C.AddCol(&cKey);   // RAM-computed leaf

	auto root = Join2(&A, &C, &aKey, &cKey);
	SpecBuf buf; buf.sel = { { &aOut, wxT("ao") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanColocateJoin(spec));   // a computed leaf -> RAM (or temp-promote, not this gate)
}

// ===========================================================================
// Routing gates — CanColocateUnion / CanColocateAggregate
// ===========================================================================

TEST(QueryComposerGate, Union_TwoDbBranchesScalar_Colocatable)
{
	ibRawDBColumn aCode = ibRawDBColumn::String(wxT("code"));
	ibRawDBColumn bCode = ibRawDBColumn::String(wxT("code"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aCode);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bCode);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf; buf.sel = { { &aCode, wxT("code") } };   // resolved per-branch by NAME
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateUnion(spec));
}

TEST(QueryComposerGate, Union_ComputedBranch_NotColocatableDirectly)
{
	ibRawDBColumn aCode = ibRawDBColumn::String(wxT("code"));
	ibRawDBColumn cCode = ibRawDBColumn::String(wxT("code"));
	TestQueryable A(wxT("TableA"), 1);             A.AddCol(&aCode);
	TestQueryable C(wxT("Slice"), 2, /*computed*/true); C.AddCol(&cCode);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&C));

	SpecBuf buf; buf.sel = { { &aCode, wxT("code") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	// A computed branch fails the DIRECT union gate (the composer's mixed-promote path handles it).
	EXPECT_FALSE(ibDbTableProvider::CanColocateUnion(spec));
}

TEST(QueryComposerGate, Aggregate_GroupBySum_Colocatable)
{
	ibRawDBColumn aKey = ibRawDBColumn::Number(wxT("a_key"));
	ibRawDBColumn aDim = ibRawDBColumn::String(wxT("a_dim"));
	ibRawDBColumn bKey = ibRawDBColumn::Number(wxT("b_key"));
	ibRawDBColumn bAmt = ibRawDBColumn::Number(wxT("b_amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aDim);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bAmt);

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf;
	buf.groupBy = { &aDim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &bAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateAggregate(spec));
}

// ===========================================================================
// RAM core — BuildHierarchyTree (recursive parent-ref fold, modes, has-children, subtotals)
// ===========================================================================

namespace {

const ibMetaID HKEY = 1, HPARENT = 2, HAMT = 3;
const ibMetaID AGG0 = HAMT;          // aggregate rolls IN-PLACE into its own column (amount), not a synthetic id

// F1(0) ─ E1(10), F2(0) ─ E2(5)      E3(7) top-level element
ibQueryRamTable HierDetail()
{
	ibQueryRamTable d;
	d.AddColumn(HKEY,    wxT("key"),    kNoType);
	d.AddColumn(HPARENT, wxT("parent"), kNoType);
	d.AddColumn(HAMT,    wxT("amount"), kNoType);
	auto add = [&](const wxString& key, const wxString& parent, long amt) {
		const long r = d.AppendRow();
		d.SetCell(r, HKEY,    ibValue(key));
		d.SetCell(r, HPARENT, parent.IsEmpty() ? ibValue() : ibValue(parent));
		d.SetCell(r, HAMT,    ibValue(ibNumber(amt)));
	};
	add(wxT("1"), wxString(), 0);   // F1 folder
	add(wxT("2"), wxT("1"), 10);    // E1
	add(wxT("3"), wxT("1"), 0);     // F2 folder
	add(wxT("4"), wxT("3"), 5);     // E2
	add(wxT("5"), wxString(), 7);   // E3 top element
	return d;
}

ibDataQueryBuilder::AggregateItem SumOf(const ibBackendQueryColumn* col)
{
	ibDataQueryBuilder::AggregateItem a;
	a.m_fn = ibDataQueryBuilder::AggregateFn::Sum; a.m_col = col; a.m_alias = wxT("total");
	return a;
}

} // namespace

TEST(QueryHierarchy, Hierarchy_TreeSubtotalsHasChildren)
{
	TestCol keyCol(wxT("key"), HKEY), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = ibQueryComposer::BuildHierarchyTree(
		d, &keyCol, &parentCol, { SumOf(&amtCol) }, ibDimensionKind::Hierarchy);

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 2u);                                   // F1, E3
	EXPECT_EQ(root.m_values.at(AGG0).GetString().ToStdString(), "22");       // grand total 0+10+0+5+7

	const ibSelectorTree::Node& f1 = *root.m_children[0];
	EXPECT_EQ(f1.m_values.at(HKEY).GetString().ToStdString(), "1");
	EXPECT_TRUE(f1.m_hasChildren);
	EXPECT_EQ(f1.m_values.at(AGG0).GetString().ToStdString(), "15");         // subtree subtotal
	ASSERT_EQ(f1.m_children.size(), 2u);                                     // E1, F2

	const ibSelectorTree::Node& f2 = *f1.m_children[1];
	EXPECT_TRUE(f2.m_hasChildren);
	EXPECT_EQ(f2.m_values.at(AGG0).GetString().ToStdString(), "5");

	const ibSelectorTree::Node& e3 = *root.m_children[1];
	EXPECT_FALSE(e3.m_hasChildren);                                          // leaf element
	EXPECT_EQ(e3.m_values.at(AGG0).GetString().ToStdString(), "7");
}

TEST(QueryHierarchy, HierarchyOnly_FoldersOnly)
{
	TestCol keyCol(wxT("key"), HKEY), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = ibQueryComposer::BuildHierarchyTree(
		d, &keyCol, &parentCol, { SumOf(&amtCol) }, ibDimensionKind::HierarchyOnly);

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 1u);                                   // only F1 (E3 leaf omitted)
	const ibSelectorTree::Node& f1 = *root.m_children[0];
	ASSERT_EQ(f1.m_children.size(), 1u);                                     // only F2 (E1 leaf omitted)
	const ibSelectorTree::Node& f2 = *f1.m_children[0];
	EXPECT_TRUE(f2.m_hasChildren);                                           // E2 exists in data
	EXPECT_TRUE(f2.m_children.empty());                                      // but E2 is a leaf -> omitted
	EXPECT_EQ(f1.m_values.at(AGG0).GetString().ToStdString(), "15");         // subtotal still counts hidden leaves
}

TEST(QueryHierarchy, Elements_LeafNodesWithGrandTotal)
{
	TestCol keyCol(wxT("key"), HKEY), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = ibQueryComposer::BuildHierarchyTree(
		d, &keyCol, &parentCol, { SumOf(&amtCol) }, ibDimensionKind::Elements);

	ASSERT_EQ(tree.Root().m_children.size(), 5u);                            // every row a leaf node, no nesting
	EXPECT_TRUE(tree.Root().m_children[0]->m_children.empty());             // leaves don't nest
	EXPECT_EQ(tree.Root().m_values.at(AGG0).GetString().ToStdString(), "22");
}

// ===========================================================================
// ibSelector — traversal façade: fold + mode over ONE snapshot, subtotals from it
// ===========================================================================

TEST(QuerySelector, Hierarchy_FoldsSnapshot_SubtotalsFromOneSnapshot)
{
	TestCol keyCol(wxT("key"), HKEY), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);

	// Snapshot moves INTO the Selector; the fold + traversal kind live on the Selector, not the query.
	ibSelector sel(HierDetail(), ibSelectKind::ByGroupsHierarchy);
	sel.ByParentRef(&keyCol, &parentCol).Aggregating({ SumOf(&amtCol) });
	const ibSelectorTree tree = sel.Build();

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 2u);                                   // F1, E3
	EXPECT_EQ(root.m_values.at(AGG0).GetString().ToStdString(), "22");       // grand total from the snapshot
	EXPECT_TRUE(root.m_children[0]->m_hasChildren);                          // F1 expandable
	EXPECT_EQ(root.m_children[0]->m_values.at(AGG0).GetString().ToStdString(), "15");
	EXPECT_FALSE(root.m_children[1]->m_hasChildren);                         // E3 leaf
}

// Sub-selection без WithSource рецепта inert — no DB hit, empty sub-Selector (the eager-only guard).
// The live sub-selection (Выборка → под-Выборка) runs at runtime over a real holder.
TEST(QuerySelector, Select_NoSource_EmptyAndNoDbHit)
{
	TestCol keyCol(wxT("key"), HKEY), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);

	ibSelector sel(HierDetail(), ibSelectKind::ByGroupsHierarchy);
	sel.ByParentRef(&keyCol, &parentCol).Aggregating({ SumOf(&amtCol) });   // fold set, but NO WithSource

	ASSERT_TRUE(sel.Next());                                                 // stand on the first node (F1)
	const ibSelector child = sel.Select();                                  // no WithSource → guard → empty, no DB
	EXPECT_EQ(child.Snapshot().RowCount(), 0);
	EXPECT_TRUE(child.Snapshot().Columns().empty());
}

// The Selector is a CURSOR: Next() walks the folded tree pre-order, GetValue/Level read the current
// visit. One interface over flat-vs-tree (here: Hierarchy → folder then its subtree).
TEST(QuerySelector, Iterate_PreOrderCursorWithLevels)
{
	TestCol keyCol(wxT("key"), HKEY), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	ibSelector sel(HierDetail(), ibSelectKind::ByGroupsHierarchy);
	sel.ByParentRef(&keyCol, &parentCol).Aggregating({ SumOf(&amtCol) });

	std::vector<std::string> keys;
	std::vector<int>         levels;
	while (sel.Next()) {
		keys.push_back(sel.GetValue(&keyCol).GetString().ToStdString());
		levels.push_back(sel.Level());
	}

	ASSERT_EQ(keys.size(), 5u);                          // F1, E1, F2, E2, E3 — pre-order
	EXPECT_EQ(keys[0], "1");   EXPECT_EQ(levels[0], 1);  // F1 folder, top level
	EXPECT_EQ(keys[2], "3");                             // F2 (nested folder)
	EXPECT_EQ(levels[3], 3);                             // E2 — under F2 under F1
	EXPECT_EQ(keys[4], "5");   EXPECT_EQ(levels[4], 1);  // E3 top-level element

	// Reset rewinds — the same tree walks again (no re-query, no re-fold).
	sel.Reset();
	int again = 0;
	while (sel.Next()) ++again;
	EXPECT_EQ(again, 5);
}
