// =============================================================================
// OES Enterprise — ibQueryPredicate tests
//
// ibQueryPredicate (backend/query/queryable.h) is the boolean WHERE tree the L3
// door and the RAM filter core both evaluate (see test_queryParity for the
// three-valued NULL semantics). This pins the TREE construction: Leaf / And / Or
// / Not / IsNull kinds, child arity and the leaf condition. Pure.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/compiler/value.h"
#include "backend/typeDescription.h"
#include "backend/query/queryColumn.h"   // ibBackendQueryColumn
#include "backend/query/queryable.h"     // ibQueryPredicate / ibQueryCondition / ibQueryPredicateKind

namespace {

// Minimal column (mirrors TestCol in test_queryParity / test_queryComposer).
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

ibQueryCondition Eq(const ibBackendQueryColumn* c) {
    ibQueryCondition cond;            // no explicit op = equality
    cond.m_col   = c;
    cond.m_value = ibValue(ibNumber(1));
    return cond;
}

} // namespace

TEST(QueryPredicate, DefaultKindIsLeaf) {
    ibQueryPredicate p;
    EXPECT_TRUE(p.m_kind == ibQueryPredicateKind::Leaf);
}

TEST(QueryPredicate, LeafCarriesCondition) {
    Col c(wxT("x"));
    const ibQueryPredicatePtr p = ibQueryPredicate::Leaf(Eq(&c));
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->m_kind == ibQueryPredicateKind::Leaf);
    EXPECT_EQ(p->m_leaf.m_col, &c);
}

TEST(QueryPredicate, AndComposesTwoChildren) {
    Col c(wxT("x"));
    const ibQueryPredicatePtr a = ibQueryPredicate::Leaf(Eq(&c));
    const ibQueryPredicatePtr b = ibQueryPredicate::Leaf(Eq(&c));
    const ibQueryPredicatePtr p = ibQueryPredicate::Compose(ibQueryPredicateKind::And, a, b);
    EXPECT_TRUE(p->m_kind == ibQueryPredicateKind::And);
    ASSERT_EQ(p->m_children.size(), 2u);
    EXPECT_EQ(p->m_children[0], a);
    EXPECT_EQ(p->m_children[1], b);
}

TEST(QueryPredicate, OrComposesTwoChildren) {
    Col c(wxT("x"));
    const ibQueryPredicatePtr p = ibQueryPredicate::Compose(
        ibQueryPredicateKind::Or, ibQueryPredicate::Leaf(Eq(&c)), ibQueryPredicate::Leaf(Eq(&c)));
    EXPECT_TRUE(p->m_kind == ibQueryPredicateKind::Or);
    EXPECT_EQ(p->m_children.size(), 2u);
}

TEST(QueryPredicate, NotWrapsExactlyOneChild) {
    Col c(wxT("x"));
    const ibQueryPredicatePtr inner = ibQueryPredicate::Leaf(Eq(&c));
    const ibQueryPredicatePtr p = ibQueryPredicate::Not(inner);
    EXPECT_TRUE(p->m_kind == ibQueryPredicateKind::Not);
    ASSERT_EQ(p->m_children.size(), 1u);
    EXPECT_EQ(p->m_children[0], inner);
}

TEST(QueryPredicate, IsNullCarriesColumnAndNegation) {
    Col c(wxT("x"));
    const ibQueryPredicatePtr isNull = ibQueryPredicate::Null(&c, /*negated*/ false);
    EXPECT_TRUE(isNull->m_kind == ibQueryPredicateKind::IsNull);
    EXPECT_EQ(isNull->m_col, &c);
    EXPECT_FALSE(isNull->m_negated);

    const ibQueryPredicatePtr isNotNull = ibQueryPredicate::Null(&c, /*negated*/ true);
    EXPECT_TRUE(isNotNull->m_negated);   // IS NOT NULL
}
