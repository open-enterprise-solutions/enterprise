// L3 query vocabulary — the L2-free building blocks L4 lowers onto (queryable.h):
//   * ibQueryPredicate     — the boolean WHERE TREE (Leaf / And / Or / Not / IsNull)
//   * ibQueryColumnExpr    — the computed-column expression TREE (Column / Const / Arith / Case)
//
// PURE: header-only factories, no door, no provider, no database. Confirms the trees the lowering
// builds have the right SHAPE (the provider's L2 lowering of them is integration scope — a real DB).

#include <gtest/gtest.h>

#include "backend/query/queryable.h"
#include "backend/query/queryColumn.h"

namespace {

// Minimal column — only identity is needed (the factories store the pointer; they call no methods).
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

} // namespace

TEST(QueryL3Vocab, PredicateTree_Factories)
{
	TestCol code(wxT("Code"), 1);

	ibQueryCondition c;
	c.m_col   = &code;
	c.m_value = ibValue(wxT("A-01"));
	const ibQueryPredicatePtr leaf = ibQueryPredicate::Leaf(c);
	ASSERT_EQ(leaf->m_kind, ibQueryPredicateKind::Leaf);
	EXPECT_EQ(leaf->m_leaf.m_col, &code);

	// (Code = 'A-01') OR NOT (Code = 'A-01')
	const ibQueryPredicatePtr tree =
		ibQueryPredicate::Compose(ibQueryPredicateKind::Or, leaf, ibQueryPredicate::Not(leaf));
	ASSERT_EQ(tree->m_kind, ibQueryPredicateKind::Or);
	ASSERT_EQ(tree->m_children.size(), 2u);
	EXPECT_EQ(tree->m_children[0]->m_kind, ibQueryPredicateKind::Leaf);
	ASSERT_EQ(tree->m_children[1]->m_kind, ibQueryPredicateKind::Not);
	ASSERT_EQ(tree->m_children[1]->m_children.size(), 1u);

	// IS NOT NULL
	const ibQueryPredicatePtr nul = ibQueryPredicate::Null(&code, /*negated*/true);
	EXPECT_EQ(nul->m_kind, ibQueryPredicateKind::IsNull);
	EXPECT_EQ(nul->m_col, &code);
	EXPECT_TRUE(nul->m_negated);
	EXPECT_TRUE(nul->m_path.empty());   // plain column (size-1 path collapses to no path)
}

TEST(QueryL3Vocab, PredicateLeaf_CarriesDotWalkPath)
{
	TestCol producer(wxT("Producer"), 1), region(wxT("Region"), 2);
	ibQueryCondition c;
	c.m_col  = &region;                                   // the leaf
	c.m_path = { &producer, &region };                    // Producer.Region — a reference dot-walk
	const ibQueryPredicatePtr leaf = ibQueryPredicate::Leaf(c);
	ASSERT_EQ(leaf->m_leaf.m_path.size(), 2u);
	EXPECT_EQ(leaf->m_leaf.m_path.back(), &region);
}

TEST(QueryL3Vocab, ColumnExpr_ArithmeticTree)
{
	TestCol qty(wxT("Qty"), 1);

	// Qty * 2
	const ibQueryColumnExprPtr e = ibQueryColumnExpr::Arith(
		ibQueryColumnArithOp::Mul,
		ibQueryColumnExpr::Col(&qty),
		ibQueryColumnExpr::Const(ibValue(2.0)));

	ASSERT_EQ(e->m_kind, ibQueryColumnExprKind::Arith);
	EXPECT_EQ(e->m_arith, ibQueryColumnArithOp::Mul);
	ASSERT_EQ(e->m_lhs->m_kind, ibQueryColumnExprKind::Column);
	EXPECT_EQ(e->m_lhs->m_col, &qty);
	EXPECT_EQ(e->m_rhs->m_kind, ibQueryColumnExprKind::Const);
}

TEST(QueryL3Vocab, ColumnExpr_CaseTree)
{
	TestCol qty(wxT("Qty"), 1);

	// CASE WHEN (Qty IS NOT NULL) THEN Qty ELSE 0 END
	auto when = ibQueryPredicate::Null(&qty, /*negated*/true);
	auto then = ibQueryColumnExpr::Col(&qty);

	auto e = std::make_shared<ibQueryColumnExpr>();
	e->m_kind = ibQueryColumnExprKind::Case;
	e->m_cases.emplace_back(when, then);
	e->m_else = ibQueryColumnExpr::Const(ibValue(0.0));

	ASSERT_EQ(e->m_kind, ibQueryColumnExprKind::Case);
	ASSERT_EQ(e->m_cases.size(), 1u);
	EXPECT_EQ(e->m_cases[0].first->m_kind, ibQueryPredicateKind::IsNull);
	EXPECT_EQ(e->m_cases[0].second->m_col, &qty);
	ASSERT_TRUE(e->m_else != nullptr);
	EXPECT_EQ(e->m_else->m_kind, ibQueryColumnExprKind::Const);
}
