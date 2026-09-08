// L4-2 — lambda expression recorder tests (compiler/lambdaQueryAST.{h,cpp}).
//
// "lambda body -> compiled instructions -> L4 query AST": the snippet is wrapped
// in a real one-parameter lambda, compiled, and the recorder is pointed at the
// lambda's own entry — no metadata, no database. It used to be a LEXEME read
// with no compiler run at all; lambdaRecordFix.h says why that reader is gone.
//
// Two families: (1) the translatable subset produces the expected ibQueryAstExpr
// shape (both CES and VES spellings); (2) anything outside the subset returns null
// (the conservative bail-out — a false "translatable" would mean wrong rows).

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/lambdaQueryAST.h"
#include "backend/query/queryAST.h"

#include "lambdaRecordFix.h"

namespace {

// ⚠ THE SPELLINGS BELOW ARE THE LANGUAGE'S, and several of them had to be corrected: comparison is
// `=` and inequality `<>` — there is no `==` and the tests were written with one. That is not a
// detail of style. The reader these tests were written for had a grammar of its OWN, WIDER than the
// language, so it accepted operators the compiler never had; recording a predicate the program does
// not contain is exactly why it was replaced (compileCode.cpp, at the recorder callsite), and the
// tests kept asserting against the wider grammar until they were compiled for real.
std::shared_ptr<ibQueryAstExpr> Record(const wxString& body, const wxString& rowParam = wxT("x"),
	const wxString& outerNames = wxEmptyString)
{
	return ibTestRecordLambda(body, rowParam, outerNames);
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
	auto e = Record(wxT("{ return x.Producer.Region.Name = \"South\"; }"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(e->m_cmp, ibQueryCompareOp::Eq);
	ASSERT_EQ(e->m_lhs->m_path.size(), 3u);
	EXPECT_EQ(e->m_lhs->m_path[0], wxT("Producer"));
	EXPECT_EQ(e->m_lhs->m_path[2], wxT("Name"));
}

TEST_F(LambdaRecorderCES, CapturedIdentifierBecomesParam)
{
	// `minPrice` has to EXIST to be captured — the compiler refuses a body that reads a name nothing
	// declared, so a test about capturing that declares nothing tests the refusal instead.
	auto e = Record(wxT("{ return x.Price > minPrice; }"), wxT("x"), wxT("minPrice"));
	ASSERT_TRUE(e != nullptr);
	ASSERT_TRUE(e->m_rhs != nullptr);
	ASSERT_EQ(e->m_rhs->m_kind, ibQueryAstExprKind::Param);
	EXPECT_EQ(e->m_rhs->m_paramName, wxT("minPrice"));
}

TEST_F(LambdaRecorderCES, BooleanLogic_KeywordsAndPairedCompares)
{
	// Logical ops are the And/Or/Not KEYWORDS in both styles (bare '&'/'|' belong
	// to the lexer's string-continuation syntax); comparisons pair freely (>=, !=).
	auto e = Record(wxT("{ return x.Price >= 10 And (x.Kind <> 3 Or x.Posted); }"));
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

// ⚠⚠ UNARY `!` / `Not` TAKES THE WHOLE REST OF THE EXPRESSION, and this test records that rather
// than asserting it is right — because which of three readings is right is a decision about the
// LANGUAGE and has not been made.
//
// The compiler declares a precedence for it and then does not use it. `gs_operPriority['!'] = 50`
// is the highest entry in the table (`*` is 30, `And` 2, `Or` 1), but the parser calls
// `GetExpression(context)` with no priority — the argument that would carry it is written out and
// COMMENTED OUT on the same line (compileCode.cpp, the KEY_NOT / '!' branch). So the declared rule
// and the applied rule disagree, and the applied one wins silently.
//
// The three readings differ on real code, which is why this is not a one-line fix:
//   as parsed today   `!a And b` -> NOT (a AND b)   · `Not x = 5` -> NOT (x = 5)
//   as the table says `!a And b` -> (NOT a) AND b   · `Not x = 5` -> (NOT x) = 5   ← breaks the
//                                                     ordinary `If Not x = 5 Then`
//   as a person expects (VBScript, the accounting languages): tighter than And/Or, looser than the
//                     comparisons — `(NOT a) AND b` AND `NOT (x = 5)`, which is neither of the above.
//
// Uncommenting picks the second and changes the meaning of code already written. Pinned here so
// that whichever is chosen arrives as a failure with the whole question attached to it.
TEST_F(LambdaRecorderCES, UnaryNotCurrentlyTakesTheWholeExpression)
{
	auto e = Record(wxT("{ return !x.Posted And x.Delta >= -5; }"));
	ASSERT_TRUE(e != nullptr);
	EXPECT_EQ(ibDescribeQueryAst(e).ToStdString(), "NOT (Posted AND (Delta >= -5))");
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
	EXPECT_TRUE(Record(wxT("{ return x.Name.Trim() = \"a\"; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_CapturedMemberAccess)
{
	// Declared, so the refusal under test is the MEMBER ACCESS on a captured value — not the name
	// being unknown, which is a different (and earlier) refusal entirely.
	EXPECT_TRUE(Record(wxT("{ return x.Price > settings.Min; }"), wxT("x"), wxT("settings")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_BareRowParam)
{
	EXPECT_TRUE(Record(wxT("{ return x; }")) == nullptr);
}

// ⭐⭐ THE INSTRUCTION READER IS STRONGER HERE, AND THIS TEST SAYS HOW STRONG. The lexeme reader
// refused any body that was more than one `return`, because a second statement was a shape it could
// not see through. The instruction reader does not read shapes, it follows the RETURN's slot back
// through the def-use chain — so `t` is not an obstacle, it is a value it can look up.
//
// 🛑 AND THE DIFFERENCE MATTERS FOR CORRECTNESS, not tidiness. `t` is a LOCAL of the lambda, not a
// captured outer, so recording it as a Param would hand the pushdown a name to resolve at runtime
// out of the captured frames (valueQueryable.cpp, ResolveCapturedByName) — a name that is not
// there. The only right answers are the folded literal or a refusal; a Param is wrong rows.
TEST_F(LambdaRecorderCES, ALocalComputedBeforeTheReturnIsNotMistakenForACapture)
{
	auto e = Record(wxT("{ var t = 1; return x.Price > t; }"));
	if (e == nullptr)
		return;   // refusing is the other acceptable answer — see above

	ASSERT_TRUE(e->m_rhs != nullptr);
	EXPECT_EQ(e->m_rhs->m_kind, ibQueryAstExprKind::Literal)
		<< "a local of the lambda was recorded as `" << e->m_rhs->m_paramName.ToStdString()
		<< "`, which the pushdown would look for among the CAPTURED values, where it is not";
}

TEST_F(LambdaRecorderCES, Bail_NoReturn)
{
	EXPECT_TRUE(Record(wxT("{ x.Price > 100; }")) == nullptr);
}

// ⚠ WHERE THIS RULE NOW LIVES, AND THAT IT IS LOOSER THAN IT WAS. `<` and `=` written apart used to
// be refused: the lexeme reader paired operators by ADJACENCY IN THE TEXT, so a space between them
// meant two operators and no comparison. The compiler pairs them on the LEXEM SEQUENCE, where a
// space is nothing at all — so `x.Price < = 100` compiles, and compiles as `<=`.
//
// Recorded rather than asserted away, because it is the COMPILER's reading and not the recorder's
// to tighten: the place to change it, if it should be changed, is where delimiter pairs are formed
// (translateCode.cpp), and that would be a change to the language for everyone.
TEST_F(LambdaRecorderCES, ASpacedOperatorPairIsFusedByTheCompiler)
{
	auto e = Record(wxT("{ return x.Price < = 100; }"));
	ASSERT_TRUE(e != nullptr) << "the compiler no longer fuses `< =`; this test records that it did";
	EXPECT_EQ(e->m_cmp, ibQueryCompareOp::Le);
}

TEST_F(LambdaRecorderCES, Bail_FunctionCall)
{
	EXPECT_TRUE(Record(wxT("{ return Round(x.Price) > 100; }")) == nullptr);
}

TEST_F(LambdaRecorderCES, Bail_EmptySpan)
{
	EXPECT_TRUE(Record(wxT("")) == nullptr);
}
