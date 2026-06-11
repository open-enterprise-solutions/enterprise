// L4-2 — lambda expression recorder tests (compiler/lambdaQueryAst.{h,cpp}).
//
// Pure "script text -> lexemes -> L4 query AST": the body snippet is tokenized by
// the SCRIPT lexer (ibCompileCode::Load + PrepareLexem) and handed to the recorder
// over the full lexeme span — no compiler run, no metadata, no database.
//
// Two families: (1) the translatable subset produces the expected ibQueryAstExpr
// shape (both CES and VES spellings); (2) anything outside the subset returns null
// (the conservative bail-out — a false "translatable" would mean wrong rows).

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/lambdaQueryAst.h"
#include "backend/query/queryAst.h"

namespace {

// Tokenize a lambda BODY snippet and run the recorder over the whole stream
// (minus the trailing ENDPROGRAM sentinel the lexer appends).
std::shared_ptr<ibQueryAstExpr> Record(const wxString& body, const wxString& rowParam = wxT("x"))
{
	ibCompileCode cc;
	cc.Load(body);
	if (!cc.PrepareLexem()) return nullptr;
	const std::vector<ibLexem>& lex = cc.GetLexems();
	size_t to = lex.size();
	while (to > 0 && lex[to - 1].m_lexType == ENDPROGRAM) --to;
	return ibBuildLambdaQueryAst(lex, 0, to, rowParam);
}

class LambdaRecorderCES : public ::testing::Test {
protected:
	void SetUp() override { ibCompileCode::SetCodeStyle(CODE_CES); }
};
class LambdaRecorderVES : public ::testing::Test {
protected:
	void SetUp() override { ibCompileCode::SetCodeStyle(CODE_VES); }
};

} // namespace

// ===========================================================================
// Translatable subset — CES spelling
// ===========================================================================

TEST_F(LambdaRecorderCES, MemberCompareLiteral)
{
	auto e = Record(wxT("{ return x.Price > 100; }"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(e->m_cmp, ibQueryCompareOp::Gt);
	ASSERT_EQ(e->m_lhs->m_kind, ibQueryAstExprKind::Column);
	ASSERT_EQ(e->m_lhs->m_path.size(), 1u);
	EXPECT_EQ(e->m_lhs->m_path[0], wxT("Price"));
	EXPECT_EQ(e->m_rhs->m_kind, ibQueryAstExprKind::Literal);
}

TEST_F(LambdaRecorderCES, DotWalkChain_DropsParamSegment)
{
	auto e = Record(wxT("{ return x.Producer.Region.Name == \"South\"; }"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_cmp, ibQueryCompareOp::Eq);
	ASSERT_EQ(e->m_lhs->m_path.size(), 3u);
	EXPECT_EQ(e->m_lhs->m_path[0], wxT("Producer"));
	EXPECT_EQ(e->m_lhs->m_path[2], wxT("Name"));
}

TEST_F(LambdaRecorderCES, CapturedIdentifierBecomesParam)
{
	auto e = Record(wxT("{ return x.Price > minPrice; }"));
	ASSERT_TRUE(e != nullptr);
	ASSERT_EQ(e->m_rhs->m_kind, ibQueryAstExprKind::Param);
	EXPECT_EQ(e->m_rhs->m_paramName, wxT("minPrice"));
}

TEST_F(LambdaRecorderCES, BooleanLogic_KeywordsAndPairedCompares)
{
	// Logical ops are the And/Or/Not KEYWORDS in both styles (bare '&'/'|' belong
	// to the lexer's string-continuation syntax); comparisons pair freely (>=, !=).
	auto e = Record(wxT("{ return x.Price >= 10 And (x.Kind != 3 Or x.Posted); }"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_FALSE(e->m_isOr);                                  // top = AND
	EXPECT_EQ(e->m_lhs->m_cmp, ibQueryCompareOp::Ge);         // '>' '=' paired
	ASSERT_EQ(e->m_rhs->m_kind, ibQueryAstExprKind::Logical); // ( ... Or ... )
	EXPECT_TRUE(e->m_rhs->m_isOr);
	EXPECT_EQ(e->m_rhs->m_lhs->m_cmp, ibQueryCompareOp::Ne);  // '!' '=' paired
	EXPECT_EQ(e->m_rhs->m_rhs->m_kind, ibQueryAstExprKind::Column);  // bare truthy member
}

TEST_F(LambdaRecorderCES, ArithmeticPrecedence)
{
	auto e = Record(wxT("{ return x.Price * x.Qty - 5 > 100; }"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_kind, ibQueryAstExprKind::Compare);
	ASSERT_EQ(e->m_lhs->m_kind, ibQueryAstExprKind::Arith);
	EXPECT_EQ(e->m_lhs->m_arith, ibQueryArithOp::Sub);        // (P*Q) - 5
	EXPECT_EQ(e->m_lhs->m_lhs->m_arith, ibQueryArithOp::Mul);
}

TEST_F(LambdaRecorderCES, UnaryBangAndNegativeNumber)
{
	auto e = Record(wxT("{ return !x.Posted And x.Delta >= -5; }"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_lhs->m_kind, ibQueryAstExprKind::Not);
	EXPECT_EQ(e->m_rhs->m_cmp, ibQueryCompareOp::Ge);
	EXPECT_EQ(e->m_rhs->m_rhs->m_kind, ibQueryAstExprKind::Literal);
}

// ===========================================================================
// Translatable subset — VES spelling
// ===========================================================================

TEST_F(LambdaRecorderVES, KeywordLogic_SingleEquals_EndFunction)
{
	auto e = Record(wxT("Return x.Price = 100 Or Not x.Posted; EndFunction"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_TRUE(e->m_isOr);
	EXPECT_EQ(e->m_lhs->m_cmp, ibQueryCompareOp::Eq);
	EXPECT_EQ(e->m_rhs->m_kind, ibQueryAstExprKind::Not);
}

TEST_F(LambdaRecorderVES, DiamondNotEqual)
{
	auto e = Record(wxT("Return x.Kind <> 3;"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_cmp, ibQueryCompareOp::Ne);
}

// ===========================================================================
// Bail-out — anything outside the subset must return null
// ===========================================================================

TEST_F(LambdaRecorderCES, Bail_MethodCallOnChain)
{
	EXPECT_TRUE(Record(wxT("{ return x.Name.Trim() == \"a\"; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_CapturedMemberAccess)
{
	EXPECT_TRUE(Record(wxT("{ return x.Price > settings.Min; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_BareRowParam)
{
	EXPECT_TRUE(Record(wxT("{ return x; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_TwoStatements)
{
	EXPECT_TRUE(Record(wxT("{ var t = 1; return x.Price > t; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_NoReturn)
{
	EXPECT_TRUE(Record(wxT("{ x.Price > 100; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_SpacedPairIsNotAnOperator)
{
	// '<' and '=' separated by a space must NOT silently fuse into '<='
	EXPECT_TRUE(Record(wxT("{ return x.Price < = 100; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_FunctionCall)
{
	EXPECT_TRUE(Record(wxT("{ return Round(x.Price) > 100; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_EmptySpan)
{
	EXPECT_TRUE(Record(wxT("")) == nullptr);
}
