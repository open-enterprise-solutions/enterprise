// =============================================================================
// OES Enterprise — ibQueryColumnExpr tests
//
// ibQueryColumnExpr (backend/query/queryable.h) is the L3 computed-column
// expression tree (column / const / arithmetic / searched CASE) a projection can
// COMPUTE; the provider lowers it to the L2 IR. Evaluation is covered by
// test_queryComposer (EvalColumnExpr); this pins the TREE construction (kind +
// operands) of the factory helpers. Pure.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/compiler/value.h"
#include "backend/typeDescription.h"
#include "backend/query/queryColumn.h"   // ibBackendQueryColumn
#include "backend/query/queryable.h"     // ibQueryColumnExpr / kinds / arith ops

namespace {

class Col : public ibBackendQueryColumn {
public:
    explicit Col(const wxString& n) : m_n(n) {}
    wxString           GetName()         const override { return m_n; }
    wxString           GetPhysicalName() const override { return m_n; }
    ibTypeDescription& GetTypeDesc()     const override { return m_t; }
    ibMetaID           GetColumnId()     const override { return 1; }
private:
    wxString                  m_n;
    mutable ibTypeDescription m_t;
};

} // namespace

TEST(QueryColumnExpr, DefaultKindIsColumn) {
    ibQueryColumnExpr e;
    EXPECT_TRUE(e.m_kind == ibQueryColumnExprKind::Column);
}

TEST(QueryColumnExpr, ColCarriesColumn) {
    Col c(wxT("qty"));
    const ibQueryColumnExprPtr e = ibQueryColumnExpr::Col(&c);
    EXPECT_TRUE(e->m_kind == ibQueryColumnExprKind::Column);
    EXPECT_EQ(e->m_col, &c);
}

TEST(QueryColumnExpr, ConstCarriesValue) {
    const ibQueryColumnExprPtr e = ibQueryColumnExpr::Const(ibValue(ibNumber(5)));
    EXPECT_TRUE(e->m_kind == ibQueryColumnExprKind::Const);
    EXPECT_EQ(e->m_const.GetInteger(), 5);
}

TEST(QueryColumnExpr, ArithCarriesOpAndOperands) {
    Col c(wxT("qty"));
    const ibQueryColumnExprPtr lhs = ibQueryColumnExpr::Col(&c);
    const ibQueryColumnExprPtr rhs = ibQueryColumnExpr::Const(ibValue(ibNumber(2)));
    const ibQueryColumnExprPtr e   = ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul, lhs, rhs);
    EXPECT_TRUE(e->m_kind == ibQueryColumnExprKind::Arith);
    EXPECT_TRUE(e->m_arith == ibQueryColumnArithOp::Mul);
    EXPECT_EQ(e->m_lhs, lhs);
    EXPECT_EQ(e->m_rhs, rhs);
}

TEST(QueryColumnExpr, CaseBuildsWhenThenElse) {
    Col c(wxT("qty"));
    auto e = std::make_shared<ibQueryColumnExpr>();
    e->m_kind  = ibQueryColumnExprKind::Case;
    e->m_else  = ibQueryColumnExpr::Const(ibValue(wxString(wxT("unit"))));
    EXPECT_TRUE(e->m_kind == ibQueryColumnExprKind::Case);
    ASSERT_NE(e->m_else, nullptr);
    EXPECT_TRUE(e->m_else->m_kind == ibQueryColumnExprKind::Const);
}
